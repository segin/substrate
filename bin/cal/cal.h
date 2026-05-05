#ifndef CAL_H
#define CAL_H

#include <stdbool.h>
#include <stddef.h>

#define CAL_VERSION "cal (Substrate) 0.1"

#define CAL_DEFAULT_REFORM_YEAR 1752
#define CAL_DEFAULT_REFORM_MONTH 9
#define CAL_DEFAULT_REFORM_DAY 14

enum cal_week_start {
    CAL_WEEK_SUNDAY = 0,
    CAL_WEEK_MONDAY = 1,
};

enum cal_color_mode {
    CAL_COLOR_AUTO = 0,
    CAL_COLOR_ALWAYS,
    CAL_COLOR_NEVER,
};

enum cal_calendar_mode {
    CAL_CALENDAR_MIXED = 0,
    CAL_CALENDAR_GREGORIAN,
    CAL_CALENDAR_JULIAN,
};

enum cal_view_mode {
    CAL_VIEW_AUTO = 0,
    CAL_VIEW_SINGLE,
    CAL_VIEW_RANGE,
    CAL_VIEW_YEAR,
};

enum cal_chronology {
    CAL_CHRONOLOGY_GREGORIAN = 1,
    CAL_CHRONOLOGY_JULIAN = 2,
};

struct cal_date {
    int year;
    int month;
    int day;
};

struct cal_options {
    const char *progname;
    bool show_help;
    bool show_version;
    bool show_day_of_year;
    bool show_week_numbers;
    bool no_highlight;
    enum cal_week_start week_start;
    enum cal_color_mode color_mode;
    enum cal_calendar_mode calendar_mode;
    enum cal_view_mode view_mode;
    struct cal_date reform;
    int operand_start;
    int operand_count;
    int base_month;
    int base_year;
    int months_before;
    int months_after;
    int month_span;
};

struct cal_day_info {
    int day;
    int ordinal;
    int wday_sun0;
    long jdn;
    enum cal_chronology chronology;
};

#endif