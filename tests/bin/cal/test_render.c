#include "cal_render.h"
#include "cal_opts.h"
#include "cal_math.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

#define CHECK_STR_EQ(actual, expected) do { \
    if (strcmp(actual, expected) != 0) { \
        fprintf(stderr, "CHECK_STR_EQ failed: %s:%d: \nActual:   '%s'\nExpected: '%s'\n", __FILE__, __LINE__, actual, expected); \
        return 1; \
    } \
} while (0)

static int
test_standard_month(void)
{
    struct cal_options opts;
    struct cal_month_block block;
    long today_jdn;

    cal_options_init(&opts, "cal");
    today_jdn = cal_gregorian_jdn(2024, 1, 15);

    CHECK(cal_make_month_block(&opts, 2024, 1, today_jdn, false, &block) == 0);
    CHECK(block.line_count == 7);
    CHECK(block.width == 20);
    CHECK_STR_EQ(block.lines[0], "      January       ");
    CHECK_STR_EQ(block.lines[1], "Su Mo Tu We Th Fr Sa");
    CHECK_STR_EQ(block.lines[2], "    1  2  3  4  5  6");
    CHECK_STR_EQ(block.lines[3], " 7  8  9 10 11 12 13");
    CHECK_STR_EQ(block.lines[4], "14 15 16 17 18 19 20");
    CHECK_STR_EQ(block.lines[5], "21 22 23 24 25 26 27");
    CHECK_STR_EQ(block.lines[6], "28 29 30 31         ");

    return 0;
}

static int
test_leap_year_february(void)
{
    struct cal_options opts;
    struct cal_month_block block;
    long today_jdn;

    cal_options_init(&opts, "cal");
    today_jdn = cal_gregorian_jdn(2024, 2, 1);

    CHECK(cal_make_month_block(&opts, 2024, 2, today_jdn, false, &block) == 0);
    CHECK(block.line_count == 7);
    CHECK(block.width == 20);
    CHECK_STR_EQ(block.lines[0], "      February      ");
    CHECK_STR_EQ(block.lines[1], "Su Mo Tu We Th Fr Sa");
    CHECK_STR_EQ(block.lines[2], "             1  2  3");
    CHECK_STR_EQ(block.lines[3], " 4  5  6  7  8  9 10");
    CHECK_STR_EQ(block.lines[4], "11 12 13 14 15 16 17");
    CHECK_STR_EQ(block.lines[5], "18 19 20 21 22 23 24");
    CHECK_STR_EQ(block.lines[6], "25 26 27 28 29      ");

    return 0;
}

static int
test_reform_gap(void)
{
    struct cal_options opts;
    struct cal_month_block block;
    long today_jdn;

    cal_options_init(&opts, "cal");
    today_jdn = cal_gregorian_jdn(1752, 9, 1);

    CHECK(cal_make_month_block(&opts, 1752, 9, today_jdn, true, &block) == 0);
    CHECK(block.line_count == 5);
    CHECK(block.width == 20);
    CHECK_STR_EQ(block.lines[0], "   September 1752   ");
    CHECK_STR_EQ(block.lines[1], "Su Mo Tu We Th Fr Sa");
    CHECK_STR_EQ(block.lines[2], "       1  2 14 15 16");
    CHECK_STR_EQ(block.lines[3], "17 18 19 20 21 22 23");
    CHECK_STR_EQ(block.lines[4], "24 25 26 27 28 29 30");

    return 0;
}

static int
test_week_numbers_and_monday_start(void)
{
    struct cal_options opts;
    struct cal_month_block block;
    long today_jdn;

    cal_options_init(&opts, "cal");
    opts.show_week_numbers = true;
    opts.week_start = CAL_WEEK_MONDAY;
    opts.show_day_of_year = true;
    opts.no_highlight = true;
    today_jdn = cal_gregorian_jdn(2024, 1, 15);

    CHECK(cal_make_month_block(&opts, 2024, 1, today_jdn, true, &block) == 0);
    CHECK(block.line_count == 7);
    CHECK(block.width == 30);
    CHECK_STR_EQ(block.lines[0], "         January 2024         ");
    CHECK_STR_EQ(block.lines[1], "Wk  Mo  Tu  We  Th  Fr  Sa  Su");
    CHECK_STR_EQ(block.lines[2], " 1   1   2   3   4   5   6   7");
    CHECK_STR_EQ(block.lines[3], " 2   8   9  10  11  12  13  14");
    CHECK_STR_EQ(block.lines[4], " 3  15  16  17  18  19  20  21");
    CHECK_STR_EQ(block.lines[5], " 4  22  23  24  25  26  27  28");
    CHECK_STR_EQ(block.lines[6], " 5  29  30  31                ");

    return 0;
}

static int
test_highlighting(void)
{
    struct cal_options opts;
    struct cal_month_block block;
    long today_jdn;

    cal_options_init(&opts, "cal");
    opts.color_mode = CAL_COLOR_ALWAYS;
    today_jdn = cal_gregorian_jdn(2024, 2, 14);

    CHECK(cal_make_month_block(&opts, 2024, 2, today_jdn, false, &block) == 0);
    CHECK(block.line_count == 7);
    CHECK(block.width == 20);
    CHECK_STR_EQ(block.lines[4], "11 12 13 \033[7m14\033[0m 15 16 17");

    return 0;
}

int
main(void)
{
    if (test_standard_month() != 0 ||
        test_leap_year_february() != 0 ||
        test_reform_gap() != 0 ||
        test_week_numbers_and_monday_start() != 0 ||
        test_highlighting() != 0) {
        return 1;
    }
    puts("test_render: ok");
    return 0;
}
