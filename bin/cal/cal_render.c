#include "cal_render.h"

#include "cal_math.h"

#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static size_t
cal_body_width(const struct cal_options *opts)
{
    int cell_width;

    cell_width = opts->show_day_of_year ? 3 : 2;
    return (size_t)(7 * cell_width + 6);
}

static size_t
cal_prefix_width(const struct cal_options *opts)
{
    return opts->show_week_numbers ? 3u : 0u;
}

static size_t
cal_total_width(const struct cal_options *opts)
{
    return cal_prefix_width(opts) + cal_body_width(opts);
}

static void
cal_fill_spaces(char *buffer, size_t width)
{
    memset(buffer, ' ', width);
    buffer[width] = '\0';
}

void
cal_print_centered_header(FILE *stream, const char *text, size_t width)
{
    size_t text_len;
    size_t left_pad;

    text_len = strlen(text);
    left_pad = (width > text_len) ? (width - text_len) / 2 : 0;
    fprintf(stream, "%*s%s\n", (int)left_pad, "", text);
}

static void
cal_center_text(char *buffer, size_t width, const char *text)
{
    size_t text_len;
    size_t left_pad;

    cal_fill_spaces(buffer, width);
    text_len = strlen(text);
    if (text_len >= width) {
        memcpy(buffer, text, width);
        buffer[width] = '\0';
        return;
    }
    left_pad = (width - text_len) / 2;
    memcpy(buffer + left_pad, text, text_len);
}

static void
cal_month_name(int month, char *buffer, size_t buffer_size)
{
    static const char *fallback[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December",
    };
    struct tm tm_value;

    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = 123;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = 1;
    if (strftime(buffer, buffer_size, "%B", &tm_value) == 0) {
        snprintf(buffer, buffer_size, "%s", fallback[month]);
    }
}

static void
cal_day_label(int weekday_sun0, char *buffer, size_t buffer_size)
{
    static const char *fallback[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
    char temp[32];
    struct tm tm_value;

    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = 123;
    tm_value.tm_mon = 0;
    tm_value.tm_mday = 1 + weekday_sun0;
    tm_value.tm_wday = weekday_sun0;
    if (strftime(temp, sizeof(temp), "%a", &tm_value) == 0 || temp[0] == '\0') {
        snprintf(buffer, buffer_size, "%s", fallback[weekday_sun0]);
        return;
    }
    snprintf(buffer, buffer_size, "%.2s", temp);
}

static void
cal_build_title(const struct cal_options *opts, int year, int month,
    bool include_year, char *buffer, size_t buffer_size)
{
    char month_name[64];
    char year_text[16];
    size_t current_len;

    (void)opts;
    cal_month_name(month, month_name, sizeof(month_name));
    snprintf(buffer, buffer_size, "%s", month_name);
    if (include_year) {
        snprintf(year_text, sizeof(year_text), "%d", year);
        current_len = strlen(buffer);
        if (current_len + 1 < buffer_size) {
            strncat(buffer, " ", buffer_size - current_len - 1);
        }
        current_len = strlen(buffer);
        if (current_len < buffer_size) {
            strncat(buffer, year_text, buffer_size - current_len - 1);
        }
    }
}

static int
cal_should_highlight(const struct cal_options *opts)
{
    if (opts->no_highlight) {
        return 0;
    }
    if (opts->color_mode == CAL_COLOR_ALWAYS) {
        return 1;
    }
    if (opts->color_mode == CAL_COLOR_NEVER) {
        return 0;
    }
    return isatty(STDOUT_FILENO);
}

static void
cal_append_text(char *buffer, size_t buffer_size, const char *text)
{
    size_t current_len;
    size_t available;

    current_len = strlen(buffer);
    if (current_len >= buffer_size - 1) {
        return;
    }
    available = buffer_size - current_len - 1;
    strncat(buffer, text, available);
}

static void
cal_append_cell(char *buffer, size_t buffer_size, int value, int field_width,
    bool highlight)
{
    char cell[64];

    if (highlight) {
        snprintf(cell, sizeof(cell), "\033[7m%*d\033[0m", field_width, value);
    } else {
        snprintf(cell, sizeof(cell), "%*d", field_width, value);
    }
    cal_append_text(buffer, buffer_size, cell);
}

static void
cal_build_day_header(const struct cal_options *opts, char *buffer,
    size_t buffer_size)
{
    int column;
    int cell_width;

    buffer[0] = '\0';
    cell_width = opts->show_day_of_year ? 3 : 2;
    if (opts->show_week_numbers) {
        cal_append_text(buffer, buffer_size, "Wk ");
    }
    for (column = 0; column < 7; ++column) {
        char label[16];
        int weekday;
        char padded[16];

        weekday = (opts->week_start == CAL_WEEK_MONDAY) ? (column + 1) % 7 : column;
        cal_day_label(weekday, label, sizeof(label));
        snprintf(padded, sizeof(padded), "%*s", cell_width, label);
        cal_append_text(buffer, buffer_size, padded);
        if (column != 6) {
            cal_append_text(buffer, buffer_size, " ");
        }
    }
}

int
cal_make_month_block(const struct cal_options *opts, int year, int month,
    long today_jdn, bool include_year, struct cal_month_block *block_out)
{
    struct cal_day_info days[31];
    int count;
    int first_column;
    int current_row;
    int current_column;
    int row;
    int column;
    int rows_used;
    int highlight_enabled;
    int row_start_offset;
    size_t width;
    int cell_width;
    int week_anchor;
    char title[64];
    struct cal_day_info first_day;
    struct cal_day_info grid[6][7];
    bool present[6][7];

    memset(block_out, 0, sizeof(*block_out));
    memset(grid, 0, sizeof(grid));
    memset(present, 0, sizeof(present));

    count = cal_collect_month_days(opts, year, month, days, 31);
    if (count <= 0) {
        return -1;
    }
    first_day = days[0];
    first_column = cal_translate_weekday(first_day.wday_sun0, opts->week_start);
    current_row = 0;
    current_column = first_column;

    for (row = 0; row < count; ++row) {
        if (current_row >= 6) {
            break;
        }
        grid[current_row][current_column] = days[row];
        present[current_row][current_column] = true;
        ++current_column;
        if (current_column == 7) {
            current_column = 0;
            ++current_row;
        }
    }

    rows_used = current_row + (current_column != 0 ? 1 : 0);
    if (rows_used == 0) {
        rows_used = 1;
    }

    width = cal_total_width(opts);
    cell_width = opts->show_day_of_year ? 3 : 2;
    highlight_enabled = cal_should_highlight(opts);
    week_anchor = (opts->week_start == CAL_WEEK_MONDAY) ? 3 : 4;
    row_start_offset = first_column;

    cal_build_title(opts, year, month, include_year, title, sizeof(title));
    block_out->width = width;
    block_out->line_count = (size_t)(2 + rows_used);
    cal_center_text(block_out->lines[0], width, title);
    cal_build_day_header(opts, block_out->lines[1], sizeof(block_out->lines[1]));

    for (row = 0; row < rows_used; ++row) {
        long row_start_jdn;

        block_out->lines[2 + row][0] = '\0';
        if (opts->show_week_numbers) {
            row_start_jdn = first_day.jdn - row_start_offset + row * 7;
            snprintf(block_out->lines[2 + row], sizeof(block_out->lines[2 + row]),
                "%2d ", cal_iso_week_number(row_start_jdn + week_anchor));
        }
        for (column = 0; column < 7; ++column) {
            if (!present[row][column]) {
                char blank[8];

                snprintf(blank, sizeof(blank), "%*s", cell_width, "");
                cal_append_text(block_out->lines[2 + row],
                    sizeof(block_out->lines[2 + row]), blank);
            } else {
                int value;
                bool highlight;

                value = opts->show_day_of_year ? grid[row][column].ordinal :
                    grid[row][column].day;
                highlight = highlight_enabled && grid[row][column].jdn == today_jdn;
                cal_append_cell(block_out->lines[2 + row],
                    sizeof(block_out->lines[2 + row]), value, cell_width,
                    highlight);
            }
            if (column != 6) {
                cal_append_text(block_out->lines[2 + row],
                    sizeof(block_out->lines[2 + row]), " ");
            }
        }
    }
    return 0;
}

void
cal_print_month_blocks(FILE *stream, const struct cal_month_block *blocks,
    size_t count, size_t columns)
{
    size_t base;

    if (columns == 0) {
        columns = 1;
    }
    for (base = 0; base < count; base += columns) {
        size_t line;
        size_t end;
        size_t max_lines;

        end = base + columns;
        if (end > count) {
            end = count;
        }
        max_lines = 0;
        for (line = base; line < end; ++line) {
            if (blocks[line].line_count > max_lines) {
                max_lines = blocks[line].line_count;
            }
        }

        for (line = 0; line < max_lines; ++line) {
            size_t index;

            for (index = base; index < end; ++index) {
                if (line < blocks[index].line_count) {
                    fputs(blocks[index].lines[line], stream);
                } else {
                    fprintf(stream, "%*s", (int)blocks[index].width, "");
                }
                if (index + 1 < end) {
                    fputs("  ", stream);
                }
            }
            fputc('\n', stream);
        }
        if (end < count) {
            fputc('\n', stream);
        }
    }
}