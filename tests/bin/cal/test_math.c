#include "cal_math.h"
#include "cal_opts.h"

#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int
test_leap_year_rules(void)
{
    CHECK(cal_is_leap_year(2000, CAL_CHRONOLOGY_GREGORIAN) == true);
    CHECK(cal_is_leap_year(1900, CAL_CHRONOLOGY_GREGORIAN) == false);
    CHECK(cal_is_leap_year(1900, CAL_CHRONOLOGY_JULIAN) == true);
    return 0;
}

static int
test_reform_gap(void)
{
    struct cal_options opts;
    struct cal_day_info info;

    cal_options_init(&opts, "cal");
    CHECK(cal_get_date_info(&opts, 1752, 9, 3, &info) == false);
    CHECK(cal_get_date_info(&opts, 1752, 9, 14, &info) == true);
    CHECK(info.day == 14);
    return 0;
}

static int
test_calendar_modes(void)
{
    struct cal_options gregorian_opts;
    struct cal_options julian_opts;
    struct cal_day_info info;

    cal_options_init(&gregorian_opts, "cal");
    gregorian_opts.calendar_mode = CAL_CALENDAR_GREGORIAN;
    CHECK(cal_get_date_info(&gregorian_opts, 1900, 2, 29, &info) == false);

    cal_options_init(&julian_opts, "cal");
    julian_opts.calendar_mode = CAL_CALENDAR_JULIAN;
    CHECK(cal_get_date_info(&julian_opts, 1900, 2, 29, &info) == true);
    return 0;
}

static int
test_iso_week_number(void)
{
    CHECK(cal_iso_week_number(cal_gregorian_jdn(2024, 1, 1)) == 1);
    CHECK(cal_iso_week_number(cal_gregorian_jdn(2024, 12, 31)) == 1);
    return 0;
}

int
main(void)
{
    if (test_leap_year_rules() != 0 || test_reform_gap() != 0 ||
        test_calendar_modes() != 0 || test_iso_week_number() != 0) {
        return 1;
    }
    puts("test_math: ok");
    return 0;
}