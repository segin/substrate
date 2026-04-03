#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps_impl.h"

static void append_field(ps_field_t *fields, size_t *count, ps_field_id_t id,
                         const char *header, ps_align_t align, int min_width, bool elastic) {
    fields[*count].id = id;
    fields[*count].header = header;
    fields[*count].align = align;
    fields[*count].min_width = min_width;
    fields[*count].elastic = elastic;
    (*count)++;
}

static void append_short_preset(ps_field_t *fields, size_t *count) {
    append_field(fields, count, PS_FIELD_PID, "PID", PS_ALIGN_RIGHT, 5, false);
    append_field(fields, count, PS_FIELD_TTY, "TTY", PS_ALIGN_LEFT, 3, false);
    append_field(fields, count, PS_FIELD_STAT, "STAT", PS_ALIGN_LEFT, 4, false);
    append_field(fields, count, PS_FIELD_TIME, "TIME", PS_ALIGN_RIGHT, 8, false);
    append_field(fields, count, PS_FIELD_CMD, "CMD", PS_ALIGN_LEFT, 3, true);
}

static void append_user_preset(ps_field_t *fields, size_t *count) {
    append_field(fields, count, PS_FIELD_USER, "USER", PS_ALIGN_LEFT, 4, false);
    append_field(fields, count, PS_FIELD_PID, "PID", PS_ALIGN_RIGHT, 5, false);
    append_field(fields, count, PS_FIELD_PPID, "PPID", PS_ALIGN_RIGHT, 5, false);
    append_field(fields, count, PS_FIELD_STAT, "STAT", PS_ALIGN_LEFT, 4, false);
    append_field(fields, count, PS_FIELD_TIME, "TIME", PS_ALIGN_RIGHT, 8, false);
    append_field(fields, count, PS_FIELD_VSZ, "VSZ", PS_ALIGN_RIGHT, 4, false);
    append_field(fields, count, PS_FIELD_RSS, "RSS", PS_ALIGN_RIGHT, 3, false);
    append_field(fields, count, PS_FIELD_CMD, "CMD", PS_ALIGN_LEFT, 3, true);
}

static void append_long_preset(ps_field_t *fields, size_t *count, bool include_user) {
    if (include_user) {
        append_field(fields, count, PS_FIELD_USER, "USER", PS_ALIGN_LEFT, 4, false);
    }
    append_field(fields, count, PS_FIELD_UID, "UID", PS_ALIGN_RIGHT, 3, false);
    append_field(fields, count, PS_FIELD_PID, "PID", PS_ALIGN_RIGHT, 5, false);
    append_field(fields, count, PS_FIELD_PPID, "PPID", PS_ALIGN_RIGHT, 5, false);
    append_field(fields, count, PS_FIELD_PGID, "PGID", PS_ALIGN_RIGHT, 5, false);
    append_field(fields, count, PS_FIELD_SID, "SID", PS_ALIGN_RIGHT, 5, false);
    append_field(fields, count, PS_FIELD_NI, "NI", PS_ALIGN_RIGHT, 2, false);
    append_field(fields, count, PS_FIELD_TTY, "TTY", PS_ALIGN_LEFT, 3, false);
    append_field(fields, count, PS_FIELD_STAT, "STAT", PS_ALIGN_LEFT, 4, false);
    append_field(fields, count, PS_FIELD_TIME, "TIME", PS_ALIGN_RIGHT, 8, false);
    append_field(fields, count, PS_FIELD_VSZ, "VSZ", PS_ALIGN_RIGHT, 4, false);
    append_field(fields, count, PS_FIELD_RSS, "RSS", PS_ALIGN_RIGHT, 3, false);
    append_field(fields, count, PS_FIELD_CMD, "CMD", PS_ALIGN_LEFT, 3, true);
}

void ps_build_fields(const ps_options_t *opts, ps_field_t *fields, size_t *count) {
    size_t i;

    *count = 0;

    if (opts->flag_l) {
        append_long_preset(fields, count, opts->flag_u);
    } else if (opts->flag_u) {
        append_user_preset(fields, count);
    } else {
        append_short_preset(fields, count);
    }

    if (opts->flag_b) {
        for (i = 0; i < *count; i++) {
            if (fields[i].id == PS_FIELD_STAT) {
                size_t j;
                for (j = *count; j > i; j--) {
                    fields[j] = fields[j - 1];
                }
                fields[i].id = PS_FIELD_BITS;
                fields[i].header = "BITS";
                fields[i].align = PS_ALIGN_RIGHT;
                fields[i].min_width = 4;
                fields[i].elastic = false;
                (*count)++;
                return;
            }
        }
        append_field(fields, count, PS_FIELD_BITS, "BITS", PS_ALIGN_RIGHT, 4, false);
    }
}

static const char *field_value(const ps_row_t *row, ps_field_id_t id) {
    switch (id) {
        case PS_FIELD_USER: return row->user;
        case PS_FIELD_UID: return row->uid;
        case PS_FIELD_EUID: return row->euid;
        case PS_FIELD_GID: return row->gid;
        case PS_FIELD_EGID: return row->egid;
        case PS_FIELD_PID: return row->pid;
        case PS_FIELD_PPID: return row->ppid;
        case PS_FIELD_PGID: return row->pgid;
        case PS_FIELD_SID: return row->sid;
        case PS_FIELD_NI: return row->ni;
        case PS_FIELD_TTY: return row->tty;
        case PS_FIELD_STAT: return row->stat;
        case PS_FIELD_BITS: return row->bits;
        case PS_FIELD_TIME: return row->time;
        case PS_FIELD_VSZ: return row->vsize;
        case PS_FIELD_RSS: return row->rss;
        case PS_FIELD_CMD: return row->cmd;
    }
    return "";
}

static int ps_columns(void) {
    const char *env = getenv("COLUMNS");
    long value;
    char *end = NULL;

    if (env == NULL || *env == '\0') {
        return 80;
    }

    value = strtol(env, &end, 10);
    if (end == NULL || *end != '\0' || value < 20 || value > 1024) {
        return 80;
    }
    return (int)value;
}

static int max_int(int a, int b) {
    return (a > b) ? a : b;
}

void ps_print_rows(ps_row_t *rows, size_t count, const ps_options_t *opts) {
    ps_field_t fields[16];
    int widths[16];
    size_t field_count;
    size_t i;
    int columns;
    int fixed_total = 0;
    int elastic_index = -1;

    ps_build_fields(opts, fields, &field_count);
    columns = ps_columns();

    for (i = 0; i < field_count; i++) {
        size_t j;

        widths[i] = max_int(fields[i].min_width, (int)strlen(fields[i].header));
        if (fields[i].elastic) {
            elastic_index = (int)i;
            continue;
        }

        for (j = 0; j < count; j++) {
            if (rows[j].selected) {
                int len = (int)strlen(field_value(&rows[j], fields[i].id));
                widths[i] = max_int(widths[i], len);
            }
        }
        fixed_total += widths[i];
    }

    fixed_total += (int)(field_count - 1);
    if (elastic_index >= 0) {
        int remaining = columns - fixed_total;
        widths[elastic_index] = max_int(fields[elastic_index].min_width, remaining > 8 ? remaining : 8);
    }

    for (i = 0; i < field_count; i++) {
        if (i > 0) {
            putchar(' ');
        }
        if (fields[i].align == PS_ALIGN_RIGHT) {
            printf("%*s", widths[i], fields[i].header);
        } else {
            printf("%-*s", widths[i], fields[i].header);
        }
    }
    putchar('\n');

    for (i = 0; i < count; i++) {
        size_t j;

        if (!rows[i].selected) {
            continue;
        }

        for (j = 0; j < field_count; j++) {
            const char *value = field_value(&rows[i], fields[j].id);
            int width = widths[j];

            if (j > 0) {
                putchar(' ');
            }

            if ((int)j == elastic_index && (int)strlen(value) > width) {
                fwrite(value, 1, (size_t)width, stdout);
                continue;
            }

            if (fields[j].align == PS_ALIGN_RIGHT) {
                printf("%*s", width, value);
            } else {
                printf("%-*s", width, value);
            }
        }
        putchar('\n');
    }
}
