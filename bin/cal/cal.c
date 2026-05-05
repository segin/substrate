#include "cal.h"

#include "cal_math.h"
#include "cal_opts.h"
#include "cal_render.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

static void
cal_print_usage(FILE *stream, const char *progname)
{
    fprintf(stream,
        "Usage: %s [OPTION]... [[month] year]\n"
        "Display a calendar.\n",
        progname);
}

static void
cal_print_help(const char *progname)
{
    cal_print_usage(stdout, progname);
    fputs(
        "\n"
        "Options:\n"
        "  -1                         display a single month\n"
        "  -3                         display previous, current, and next month\n"
        "  -y                         display a full year\n"
        "  -m                         start weeks on Monday\n"
        "  -s                         start weeks on Sunday\n"
        "  -j                         display day-of-year numbers\n"
        "  -w                         display ISO week numbers\n"
        "  -n NUM                     display NUM months starting at the base month\n"
        "  -A NUM                     display NUM months after the base month\n"
        "  -B NUM                     display NUM months before the base month\n"
        "  -h, --no-highlight         do not highlight today\n"
        "      --color=WHEN           color mode: auto, always, never\n"
        "      --gregorian            use Gregorian calendar rules only\n"
        "      --julian               use Julian calendar rules only\n"
        "  -p, --reform=DATE          set Gregorian reform date (YYYY-MM-DD)\n"
        "      --help                 display this help and exit\n"
        "      --version              output version information and exit\n",
        stdout);
}

static void
cal_print_version(void)
{
    puts(CAL_VERSION);
}

static size_t
cal_month_count(const struct cal_options *opts)
{
    if (opts->view_mode == CAL_VIEW_YEAR) {
        return 12;
    }
    if (opts->view_mode == CAL_VIEW_RANGE) {
        return (size_t)(opts->months_before + opts->months_after + 1);
    }
    return 1;
}

static size_t
cal_column_count(const struct cal_options *opts, size_t month_count)
{
    if (opts->view_mode == CAL_VIEW_YEAR) {
        return 3;
    }
    return month_count >= 3 ? 3 : month_count;
}

int
main(int argc, char *argv[])
{
    const char *err_msg;
    struct cal_options opts;
    struct cal_date today;
    struct cal_month_block *blocks;
    long today_jdn;
    size_t count;
    size_t columns;
    size_t index;
    int start_year;
    int start_month;

    err_msg = NULL;
    blocks = NULL;
    setlocale(LC_TIME, "");

    cal_options_init(&opts, argv[0]);
    if (cal_parse_options(&opts, argc, argv, &err_msg) != 0) {
        cal_print_usage(stderr, opts.progname);
        if (err_msg != NULL) {
            fprintf(stderr, "%s: %s\n", opts.progname, err_msg);
        }
        return 1;
    }
    if (opts.show_help) {
        cal_print_help(opts.progname);
        return 0;
    }
    if (opts.show_version) {
        cal_print_version();
        return 0;
    }

    if (cal_get_current_date(&today, &today_jdn) != 0) {
        today.year = 1970;
        today.month = 1;
        today.day = 1;
        today_jdn = cal_gregorian_jdn(today.year, today.month, today.day);
    }

    count = cal_month_count(&opts);
    columns = cal_column_count(&opts, count);
    blocks = (struct cal_month_block *)calloc(count, sizeof(*blocks));
    if (blocks == NULL) {
        fprintf(stderr, "%s: out of memory\n", opts.progname);
        return 1;
    }

    if (opts.view_mode == CAL_VIEW_YEAR) {
        start_year = opts.base_year;
        start_month = 1;
    } else if (opts.view_mode == CAL_VIEW_RANGE) {
        if (cal_add_months(opts.base_year, opts.base_month, -opts.months_before,
                &start_year, &start_month) != 0) {
            fprintf(stderr, "%s: month range exceeds supported year bounds\n",
                opts.progname);
            free(blocks);
            return 1;
        }
    } else {
        start_year = opts.base_year;
        start_month = opts.base_month;
    }

    for (index = 0; index < count; ++index) {
        int current_year;
        int current_month;

        if (cal_add_months(start_year, start_month, (int)index, &current_year,
                &current_month) != 0 ||
            cal_make_month_block(&opts, current_year, current_month, today_jdn,
                opts.view_mode != CAL_VIEW_YEAR, &blocks[index]) != 0) {
            fprintf(stderr, "%s: failed to render %d/%d\n", opts.progname,
                current_month, current_year);
            free(blocks);
            return 1;
        }
    }

    if (opts.view_mode == CAL_VIEW_YEAR) {
        char year_title[32];
        size_t total_width;

        snprintf(year_title, sizeof(year_title), "%d", opts.base_year);
        total_width = columns * blocks[0].width + (columns - 1) * 2;
        cal_print_centered_header(stdout, year_title, total_width);
        fputc('\n', stdout);
    }
    cal_print_month_blocks(stdout, blocks, count, columns);

    free(blocks);
    return 0;
}