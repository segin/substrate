#include "cal_opts.h"

#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int
test_year_operand_defaults_to_year_view(void)
{
    struct cal_options opts;
    const char *err_msg;
    char *argv[] = { "cal", "2024", NULL };

    err_msg = NULL;
    cal_options_init(&opts, argv[0]);
    CHECK(cal_parse_options(&opts, 2, argv, &err_msg) == 0);
    CHECK(opts.view_mode == CAL_VIEW_YEAR);
    CHECK(opts.base_year == 2024);
    return 0;
}

static int
test_range_options(void)
{
    struct cal_options opts;
    const char *err_msg;
    char *argv[] = { "cal", "-3", "1", "2024", NULL };

    err_msg = NULL;
    cal_options_init(&opts, argv[0]);
    CHECK(cal_parse_options(&opts, 4, argv, &err_msg) == 0);
    CHECK(opts.view_mode == CAL_VIEW_RANGE);
    CHECK(opts.months_before == 1);
    CHECK(opts.months_after == 1);
    CHECK(opts.base_month == 1);
    CHECK(opts.base_year == 2024);
    return 0;
}

static int
test_reform_and_color_parse(void)
{
    struct cal_options opts;
    const char *err_msg;
    char *argv[] = {
        "cal",
        "--color=never",
        "--reform=1582-10-15",
        "9",
        "1752",
        NULL,
    };

    err_msg = NULL;
    cal_options_init(&opts, argv[0]);
    CHECK(cal_parse_options(&opts, 5, argv, &err_msg) == 0);
    CHECK(opts.color_mode == CAL_COLOR_NEVER);
    CHECK(opts.reform.year == 1582);
    CHECK(opts.reform.month == 10);
    CHECK(opts.reform.day == 15);
    return 0;
}

static int
test_invalid_one_operand_range(void)
{
    struct cal_options opts;
    const char *err_msg;
    char *argv[] = { "cal", "-n", "2", "2024", NULL };

    err_msg = NULL;
    cal_options_init(&opts, argv[0]);
    CHECK(cal_parse_options(&opts, 4, argv, &err_msg) != 0);
    CHECK(err_msg != NULL);
    return 0;
}

int
main(void)
{
    if (test_year_operand_defaults_to_year_view() != 0 ||
        test_range_options() != 0 ||
        test_reform_and_color_parse() != 0 ||
        test_invalid_one_operand_range() != 0) {
        return 1;
    }
    puts("test_opts: ok");
    return 0;
}