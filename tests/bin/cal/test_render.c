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

static int
test_make_month_block_basic(void)
{
    struct cal_options opts;
    struct cal_month_block block;

    cal_options_init(&opts, "cal");
    /* February 2024 - Leap Year */
    CHECK(cal_make_month_block(&opts, 2024, 2, 0, false, &block) == 0);

    /* width should be 7 * 2 + 6 = 20 */
    CHECK(block.width == 20);
    CHECK(block.line_count >= 6); // 2 header + at least 4 for weeks (5 actually for Feb 2024)

    /* Title should be centered */
    CHECK(strstr(block.lines[0], "February") != NULL);

    /* Check days header */
    CHECK(strstr(block.lines[1], "Su Mo Tu We Th Fr Sa") != NULL);

    return 0;
}

static int
test_make_month_block_with_year(void)
{
    struct cal_options opts;
    struct cal_month_block block;

    cal_options_init(&opts, "cal");
    /* February 2024 - Leap Year */
    CHECK(cal_make_month_block(&opts, 2024, 2, 0, true, &block) == 0);

    /* Title should contain year */
    CHECK(strstr(block.lines[0], "February 2024") != NULL);

    return 0;
}

static int
test_make_month_block_reform_gap(void)
{
    struct cal_options opts;
    struct cal_month_block block;

    cal_options_init(&opts, "cal");
    /* September 1752 */
    CHECK(cal_make_month_block(&opts, 1752, 9, 0, false, &block) == 0);

    /* Verify gap is rendered (days 3-13 missing) */
    /* Usually this results in a shorter week line or blank spaces */
    CHECK(strstr(block.lines[2], " 1  2 14 15 16") != NULL);

    return 0;
}

static int
test_make_month_block_week_numbers(void)
{
    struct cal_options opts;
    struct cal_month_block block;

    cal_options_init(&opts, "cal");
    opts.show_week_numbers = true;

    /* January 2024 */
    CHECK(cal_make_month_block(&opts, 2024, 1, 0, false, &block) == 0);

    /* Verify week number prefix exists */
    CHECK(strncmp(block.lines[1], "Wk ", 3) == 0);

    return 0;
}

static int
test_make_month_block_highlight(void)
{
    struct cal_options opts;
    struct cal_month_block block;
    long today_jdn = cal_gregorian_jdn(2024, 1, 15);

    cal_options_init(&opts, "cal");
    opts.color_mode = CAL_COLOR_ALWAYS; /* Force highlight */
    opts.no_highlight = false;

    /* January 2024 */
    CHECK(cal_make_month_block(&opts, 2024, 1, today_jdn, false, &block) == 0);

    /* Since color mode is ALWAYS, the number 15 should have highlight escape codes */
    CHECK(strstr(block.lines[4], "\033[7m") != NULL);

    return 0;
}

int
main(void)
{
    if (test_make_month_block_basic() != 0 ||
        test_make_month_block_with_year() != 0 ||
        test_make_month_block_reform_gap() != 0 ||
        test_make_month_block_week_numbers() != 0 ||
        test_make_month_block_highlight() != 0) {
        return 1;
    }
    puts("test_render: ok");
    return 0;
}
