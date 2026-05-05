#ifndef CAL_MATH_H
#define CAL_MATH_H

#include "cal.h"

long cal_gregorian_jdn(int year, int month, int day);
long cal_julian_jdn(int year, int month, int day);
bool cal_is_leap_year(int year, enum cal_chronology chronology);
int cal_days_in_month(int year, int month, enum cal_chronology chronology);
int cal_add_months(int year, int month, int delta, int *out_year, int *out_month);
int cal_translate_weekday(int wday_sun0, enum cal_week_start week_start);
int cal_get_current_date(struct cal_date *today, long *today_jdn);
bool cal_get_date_info(const struct cal_options *opts, int year, int month,
    int day, struct cal_day_info *info_out);
int cal_collect_month_days(const struct cal_options *opts, int year, int month,
    struct cal_day_info *days_out, size_t max_days);
int cal_iso_week_number(long jdn);

#endif