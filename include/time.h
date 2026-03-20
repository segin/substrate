#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>
#include <sys/types.h>

// Clock IDs for clock_gettime
typedef int clockid_t;
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

#define CLOCKS_PER_SEC 1000000

struct tm {
    int tm_sec;   // seconds [0,60]
    int tm_min;   // minutes [0,59]
    int tm_hour;  // hours [0,23]
    int tm_mday;  // day of month [1,31]
    int tm_mon;   // month of year [0,11]
    int tm_year;  // years since 1900
    int tm_wday;  // day of week [0,6] (Sunday = 0)
    int tm_yday;  // day of year [0,365]
    int tm_isdst; // daylight savings flag
    // Common extensions
    long tm_gmtoff;
    const char *tm_zone;
};

#ifndef _STRUCT_TIMESPEC_DEFINED
#define _STRUCT_TIMESPEC_DEFINED
struct timespec {
    time_t tv_sec;   // seconds
    long   tv_nsec;  // nanoseconds
};
#endif

time_t time(time_t *tloc);
double difftime(time_t time1, time_t time0);
time_t mktime(struct tm *timeptr);
size_t strftime(char *__restrict s, size_t maxsize, const char *__restrict format, const struct tm *__restrict timeptr);
char *strptime(const char *__restrict s, const char *__restrict format, struct tm *__restrict tm);
int timespec_get(struct timespec *ts, int base);

char *asctime(const struct tm *timeptr);
char *ctime(const time_t *timer);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);

// POSIX Reentrant versions
char *asctime_r(const struct tm *__restrict timeptr, char *__restrict buf);
char *ctime_r(const time_t *timer, char *buf);
struct tm *gmtime_r(const time_t *__restrict timer, struct tm *__restrict result);
struct tm *localtime_r(const time_t *__restrict timer, struct tm *__restrict result);

clock_t clock(void);
int clock_gettime(clockid_t clk_id, struct timespec *tp);

#define TIME_UTC 1

#endif