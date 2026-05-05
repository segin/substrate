#include "cal_math.h"

#include <string.h>
#include <time.h>

static void
cal_gregorian_from_jdn(long jdn, struct cal_date *out)
{
    long a;
    long b;
    long c;
    long d;
    long e;
    long m;

    a = jdn + 32044;
    b = (4 * a + 3) / 146097;
    c = a - (146097 * b) / 4;
    d = (4 * c + 3) / 1461;
    e = c - (1461 * d) / 4;
    m = (5 * e + 2) / 153;

    out->day = (int)(e - (153 * m + 2) / 5 + 1);
    out->month = (int)(m + 3 - 12 * (m / 10));
    out->year = (int)(100 * b + d - 4800 + m / 10);
}

long
cal_gregorian_jdn(int year, int month, int day)
{
    int a;
    int y;
    int m;

    a = (14 - month) / 12;
    y = year + 4800 - a;
    m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365L * y + y / 4 - y / 100 + y / 400 - 32045;
}

long
cal_julian_jdn(int year, int month, int day)
{
    int a;
    int y;
    int m;

    a = (14 - month) / 12;
    y = year + 4800 - a;
    m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365L * y + y / 4 - 32083;
}

bool
cal_is_leap_year(int year, enum cal_chronology chronology)
{
    if (chronology == CAL_CHRONOLOGY_JULIAN) {
        return (year % 4) == 0;
    }
    return (year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0);
}

int
cal_days_in_month(int year, int month, enum cal_chronology chronology)
{
    static const int base_days[] = {
        0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };

    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && cal_is_leap_year(year, chronology)) {
        return 29;
    }
    return base_days[month];
}

int
cal_add_months(int year, int month, int delta, int *out_year, int *out_month)
{
    int absolute_month;

    absolute_month = year * 12 + (month - 1) + delta;
    if (absolute_month < 1 * 12 || absolute_month > 9999 * 12 - 1) {
        return -1;
    }
    *out_year = absolute_month / 12;
    *out_month = absolute_month % 12 + 1;
    return 0;
}

int
cal_translate_weekday(int wday_sun0, enum cal_week_start week_start)
{
    if (week_start == CAL_WEEK_MONDAY) {
        return (wday_sun0 + 6) % 7;
    }
    return wday_sun0;
}

int
cal_get_current_date(struct cal_date *today, long *today_jdn)
{
    time_t now;
    struct tm current_tm;

    now = time(NULL);
    if (localtime_r(&now, &current_tm) == NULL) {
        return -1;
    }
    today->year = current_tm.tm_year + 1900;
    today->month = current_tm.tm_mon + 1;
    today->day = current_tm.tm_mday;
    *today_jdn = cal_gregorian_jdn(today->year, today->month, today->day);
    return 0;
}

static bool
cal_try_mixed_date(const struct cal_options *opts, int year, int month, int day,
    struct cal_day_info *info_out)
{
    long reform_jdn;
    long gregorian_jdn;
    long julian_jdn;
    bool gregorian_valid;
    bool julian_valid;

    reform_jdn = cal_gregorian_jdn(opts->reform.year, opts->reform.month,
        opts->reform.day);
    gregorian_valid = day <= cal_days_in_month(year, month,
        CAL_CHRONOLOGY_GREGORIAN);
    julian_valid = day <= cal_days_in_month(year, month,
        CAL_CHRONOLOGY_JULIAN);

    if (gregorian_valid) {
        gregorian_jdn = cal_gregorian_jdn(year, month, day);
        if (gregorian_jdn >= reform_jdn) {
            info_out->jdn = gregorian_jdn;
            info_out->chronology = CAL_CHRONOLOGY_GREGORIAN;
            return true;
        }
    }
    if (julian_valid) {
        julian_jdn = cal_julian_jdn(year, month, day);
        if (julian_jdn < reform_jdn) {
            info_out->jdn = julian_jdn;
            info_out->chronology = CAL_CHRONOLOGY_JULIAN;
            return true;
        }
    }
    return false;
}

static int
cal_get_date_info_base(const struct cal_options *opts, int year, int month,
    int day, struct cal_day_info *info_out)
{
    if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        day > 31) {
        return 0;
    }
    memset(info_out, 0, sizeof(*info_out));
    info_out->day = day;

    switch (opts->calendar_mode) {
    case CAL_CALENDAR_GREGORIAN:
        if (day > cal_days_in_month(year, month, CAL_CHRONOLOGY_GREGORIAN)) {
            return 0;
        }
        info_out->jdn = cal_gregorian_jdn(year, month, day);
        info_out->chronology = CAL_CHRONOLOGY_GREGORIAN;
        break;
    case CAL_CALENDAR_JULIAN:
        if (day > cal_days_in_month(year, month, CAL_CHRONOLOGY_JULIAN)) {
            return 0;
        }
        info_out->jdn = cal_julian_jdn(year, month, day);
        info_out->chronology = CAL_CHRONOLOGY_JULIAN;
        break;
    case CAL_CALENDAR_MIXED:
    default:
        if (!cal_try_mixed_date(opts, year, month, day, info_out)) {
            return 0;
        }
        break;
    }

    info_out->wday_sun0 = (int)((info_out->jdn + 1) % 7);
    return 1;
}

static int
cal_compute_day_of_year(const struct cal_options *opts, int year, int month,
    int day)
{
    int current_month;
    int current_day;
    int ordinal;
    struct cal_day_info info;

    ordinal = 0;
    memset(&info, 0, sizeof(info));
    for (current_month = 1; current_month <= month; ++current_month) {
        for (current_day = 1; current_day <= 31; ++current_day) {
            if (!cal_get_date_info_base(opts, year, current_month, current_day,
                    &info)) {
                continue;
            }
            ++ordinal;
            if (current_month == month && current_day == day) {
                return ordinal;
            }
        }
    }
    return 0;
}

bool
cal_get_date_info(const struct cal_options *opts, int year, int month,
    int day, struct cal_day_info *info_out)
{
    if (!cal_get_date_info_base(opts, year, month, day, info_out)) {
        return false;
    }
    info_out->ordinal = cal_compute_day_of_year(opts, year, month, day);
    return true;
}

int
cal_collect_month_days(const struct cal_options *opts, int year, int month,
    struct cal_day_info *days_out, size_t max_days)
{
    int day;
    int count;

    count = 0;
    for (day = 1; day <= 31; ++day) {
        struct cal_day_info info;

        if (!cal_get_date_info(opts, year, month, day, &info)) {
            continue;
        }
        if ((size_t)count < max_days) {
            days_out[count] = info;
        }
        ++count;
    }
    return count;
}

int
cal_iso_week_number(long jdn)
{
    struct cal_date date;
    int wday_sun0;
    int iso_wday;
    long thursday_jdn;
    struct cal_date iso_year_date;
    long jan4_jdn;
    int jan4_sun0;
    int jan4_wday;
    long week1_start;

    cal_gregorian_from_jdn(jdn, &date);
    wday_sun0 = (int)((jdn + 1) % 7);
    iso_wday = (wday_sun0 == 0) ? 7 : wday_sun0;
    thursday_jdn = jdn + (4 - iso_wday);
    cal_gregorian_from_jdn(thursday_jdn, &iso_year_date);
    jan4_jdn = cal_gregorian_jdn(iso_year_date.year, 1, 4);
    jan4_sun0 = (int)((jan4_jdn + 1) % 7);
    jan4_wday = (jan4_sun0 == 0) ? 7 : jan4_sun0;
    week1_start = jan4_jdn - (jan4_wday - 1);
    return (int)((thursday_jdn - week1_start) / 7 + 1);
}