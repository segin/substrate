#include <stdlib.h>
#include <string.h>

#include "cal_math.h"
#include "cal_opts.h"
#include "getopt.h"

enum {
    CAL_OPT_COLOR = 256,
    CAL_OPT_GREGORIAN,
    CAL_OPT_JULIAN,
    CAL_OPT_REFORM,
    CAL_OPT_NO_HIGHLIGHT,
    CAL_OPT_HELP,
    CAL_OPT_VERSION,
};

static bool
cal_parse_positive_int(const char *text, int *value_out)
{
    char *endptr;
    long value;

    if (text == NULL || text[0] == '\0') {
        return false;
    }
    value = strtol(text, &endptr, 10);
    if (endptr == text || *endptr != '\0' || value <= 0 || value > 9999) {
        return false;
    }
    *value_out = (int)value;
    return true;
}

static bool
cal_parse_month(const char *text, int *month_out)
{
    int value;

    if (!cal_parse_positive_int(text, &value) || value < 1 || value > 12) {
        return false;
    }
    *month_out = value;
    return true;
}

static bool
cal_parse_year(const char *text, int *year_out)
{
    return cal_parse_positive_int(text, year_out);
}

static bool
cal_parse_reform_date(const char *text, struct cal_date *date_out)
{
    char *copy;
    char *saveptr;
    char *part;
    int fields[3];
    int index;

    copy = NULL;
    saveptr = NULL;
    index = 0;

    if (text == NULL || text[0] == '\0') {
        return false;
    }
    copy = strdup(text);
    if (copy == NULL) {
        return false;
    }
    for (part = strtok_r(copy, "-", &saveptr);
         part != NULL && index < 3;
         part = strtok_r(NULL, "-", &saveptr)) {
        if (!cal_parse_positive_int(part, &fields[index++])) {
            free(copy);
            return false;
        }
    }
    free(copy);
    if (index != 3 || fields[1] < 1 || fields[1] > 12) {
        return false;
    }
    if (fields[2] < 1 || fields[2] > cal_days_in_month(fields[0], fields[1],
            CAL_CHRONOLOGY_GREGORIAN)) {
        return false;
    }

    date_out->year = fields[0];
    date_out->month = fields[1];
    date_out->day = fields[2];
    return true;
}

static int
cal_parse_color_mode(const char *text, enum cal_color_mode *mode_out)
{
    if (text == NULL || strcmp(text, "auto") == 0) {
        *mode_out = CAL_COLOR_AUTO;
        return 0;
    }
    if (strcmp(text, "always") == 0) {
        *mode_out = CAL_COLOR_ALWAYS;
        return 0;
    }
    if (strcmp(text, "never") == 0) {
        *mode_out = CAL_COLOR_NEVER;
        return 0;
    }
    return -1;
}

void
cal_options_init(struct cal_options *opts, const char *progname)
{
    memset(opts, 0, sizeof(*opts));
    opts->progname = (progname != NULL && progname[0] != '\0') ? progname :
        "cal";
    opts->week_start = CAL_WEEK_SUNDAY;
    opts->color_mode = CAL_COLOR_AUTO;
    opts->calendar_mode = CAL_CALENDAR_MIXED;
    opts->view_mode = CAL_VIEW_AUTO;
    opts->reform.year = CAL_DEFAULT_REFORM_YEAR;
    opts->reform.month = CAL_DEFAULT_REFORM_MONTH;
    opts->reform.day = CAL_DEFAULT_REFORM_DAY;
}

int
cal_parse_options(struct cal_options *opts, int argc, char **argv,
    const char **err_msg)
{
    static const struct option long_options[] = {
        { "color", required_argument, NULL, CAL_OPT_COLOR },
        { "gregorian", no_argument, NULL, CAL_OPT_GREGORIAN },
        { "julian", no_argument, NULL, CAL_OPT_JULIAN },
        { "reform", required_argument, NULL, CAL_OPT_REFORM },
        { "no-highlight", no_argument, NULL, CAL_OPT_NO_HIGHLIGHT },
        { "help", no_argument, NULL, CAL_OPT_HELP },
        { "version", no_argument, NULL, CAL_OPT_VERSION },
        { NULL, 0, NULL, 0 },
    };
    int option;
    struct cal_date today;
    long today_jdn;

    *err_msg = NULL;
    opterr = 0;
    optind = 1;

    while ((option = getopt_long(argc, argv, "13ymsjwn:A:B:hp:",
                long_options, NULL)) != -1) {
        switch (option) {
        case '1':
            opts->view_mode = CAL_VIEW_SINGLE;
            break;
        case '3':
            if (opts->view_mode == CAL_VIEW_YEAR || opts->month_span != 0 ||
                opts->months_before != 0 || opts->months_after != 0) {
                *err_msg = "-3 cannot be combined with other range modes";
                return -1;
            }
            opts->view_mode = CAL_VIEW_RANGE;
            opts->months_before = 1;
            opts->months_after = 1;
            break;
        case 'y':
            if (opts->view_mode == CAL_VIEW_RANGE) {
                *err_msg = "-y cannot be combined with month-range options";
                return -1;
            }
            opts->view_mode = CAL_VIEW_YEAR;
            break;
        case 'm':
            opts->week_start = CAL_WEEK_MONDAY;
            break;
        case 's':
            opts->week_start = CAL_WEEK_SUNDAY;
            break;
        case 'j':
            opts->show_day_of_year = true;
            break;
        case 'w':
            opts->show_week_numbers = true;
            break;
        case 'n':
            if (opts->view_mode == CAL_VIEW_YEAR || opts->months_before != 0 ||
                opts->months_after != 0) {
                *err_msg = "-n cannot be combined with other range modes";
                return -1;
            }
            if (!cal_parse_positive_int(optarg, &opts->month_span)) {
                *err_msg = "invalid month span";
                return -1;
            }
            opts->view_mode = CAL_VIEW_RANGE;
            break;
        case 'A':
            if (opts->view_mode == CAL_VIEW_YEAR || opts->month_span != 0) {
                *err_msg = "-A cannot be combined with -y or -n";
                return -1;
            }
            if (!cal_parse_positive_int(optarg, &opts->months_after)) {
                *err_msg = "invalid month count for -A";
                return -1;
            }
            opts->view_mode = CAL_VIEW_RANGE;
            break;
        case 'B':
            if (opts->view_mode == CAL_VIEW_YEAR || opts->month_span != 0) {
                *err_msg = "-B cannot be combined with -y or -n";
                return -1;
            }
            if (!cal_parse_positive_int(optarg, &opts->months_before)) {
                *err_msg = "invalid month count for -B";
                return -1;
            }
            opts->view_mode = CAL_VIEW_RANGE;
            break;
        case 'h':
        case CAL_OPT_NO_HIGHLIGHT:
            opts->no_highlight = true;
            break;
        case 'p':
        case CAL_OPT_REFORM:
            if (!cal_parse_reform_date(optarg, &opts->reform)) {
                *err_msg = "invalid reform date (expected YYYY-MM-DD)";
                return -1;
            }
            break;
        case CAL_OPT_COLOR:
            if (cal_parse_color_mode(optarg, &opts->color_mode) != 0) {
                *err_msg = "invalid --color value (expected auto, always, never)";
                return -1;
            }
            break;
        case CAL_OPT_GREGORIAN:
            opts->calendar_mode = CAL_CALENDAR_GREGORIAN;
            break;
        case CAL_OPT_JULIAN:
            opts->calendar_mode = CAL_CALENDAR_JULIAN;
            break;
        case CAL_OPT_HELP:
            opts->show_help = true;
            break;
        case CAL_OPT_VERSION:
            opts->show_version = true;
            break;
        case '?':
        default:
            *err_msg = "invalid option";
            return -1;
        }
    }

    opts->operand_start = optind;
    opts->operand_count = argc - optind;

    if (opts->show_help || opts->show_version) {
        return 0;
    }
    if (opts->operand_count > 2) {
        *err_msg = "too many operands";
        return -1;
    }
    if (cal_get_current_date(&today, &today_jdn) != 0) {
        today.year = 1970;
        today.month = 1;
        today.day = 1;
        today_jdn = cal_gregorian_jdn(today.year, today.month, today.day);
    }
    opts->base_year = today.year;
    opts->base_month = today.month;

    if (opts->operand_count == 1) {
        if (opts->view_mode == CAL_VIEW_SINGLE || opts->view_mode == CAL_VIEW_RANGE) {
            *err_msg = "month-range displays require both month and year operands";
            return -1;
        }
        if (!cal_parse_year(argv[optind], &opts->base_year)) {
            *err_msg = "invalid year";
            return -1;
        }
        if (opts->view_mode == CAL_VIEW_AUTO) {
            opts->view_mode = CAL_VIEW_YEAR;
        }
        opts->base_month = 1;
    } else if (opts->operand_count == 2) {
        if (opts->view_mode == CAL_VIEW_YEAR) {
            *err_msg = "year view accepts at most one operand";
            return -1;
        }
        if (!cal_parse_month(argv[optind], &opts->base_month)) {
            *err_msg = "invalid month";
            return -1;
        }
        if (!cal_parse_year(argv[optind + 1], &opts->base_year)) {
            *err_msg = "invalid year";
            return -1;
        }
        if (opts->view_mode == CAL_VIEW_AUTO) {
            opts->view_mode = CAL_VIEW_SINGLE;
        }
    } else if (opts->view_mode == CAL_VIEW_AUTO) {
        opts->view_mode = CAL_VIEW_SINGLE;
    }

    if (opts->view_mode == CAL_VIEW_RANGE && opts->month_span > 0) {
        opts->months_before = 0;
        opts->months_after = opts->month_span - 1;
    }
    if (opts->view_mode == CAL_VIEW_RANGE && opts->month_span == 0 &&
        opts->months_before == 0 && opts->months_after == 0) {
        opts->months_after = 0;
        opts->months_before = 0;
    }
    return 0;
}