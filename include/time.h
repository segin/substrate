#ifndef _TIME_H
#define _TIME_H

#ifdef __cplusplus
extern "C" {
#endif

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
int    stime(const time_t *t);  /* SVR4/Linux: set wall clock */
double difftime(time_t time1, time_t time0);
time_t mktime(struct tm *timeptr);
size_t strftime(char *__restrict s, size_t maxsize, const char *__restrict format, const struct tm *__restrict timeptr);
char *strptime(const char *__restrict s, const char *__restrict format, struct tm *__restrict tm);
int timespec_get(struct timespec *ts, int base);

char *asctime(const struct tm *timeptr);
char *ctime(const time_t *timer);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);

/* tz globals — POSIX-shape declarations.  tzset() re-parses $TZ
 * and refreshes tzname / timezone / daylight.  Substrate's libc
 * stores them in a fixed buffer; threading-wise the writes race
 * but the reads never tear (all 3 are plain pointers/longs).  */
extern char *tzname[2];
extern long  timezone;
extern int   daylight;
void tzset(void);

// POSIX Reentrant versions
char *asctime_r(const struct tm *__restrict timeptr, char *__restrict buf);
char *ctime_r(const time_t *timer, char *buf);
struct tm *gmtime_r(const time_t *__restrict timer, struct tm *__restrict result);
struct tm *localtime_r(const time_t *__restrict timer, struct tm *__restrict result);

/* getdate(3): parse against the $DATEMSK strptime templates.  getdate_r returns
 * 0 or a getdate error code; the non-reentrant getdate sets getdate_err. */
extern int getdate_err;
int getdate_r(const char *string, struct tm *tm);

clock_t clock(void);
int clock_gettime(clockid_t clk_id, struct timespec *tp);
int clock_settime(clockid_t clk_id, const struct timespec *tp);
int clock_getres(clockid_t clk_id, struct timespec *res);
int clock_nanosleep(clockid_t clk_id, int flags,
                    const struct timespec *req, struct timespec *rem);
int clock_getcpuclockid(pid_t pid, clockid_t *clock_id);
int nanosleep(const struct timespec *req, struct timespec *rem);

/* TIMER_ABSTIME flag for clock_nanosleep. */
#define TIMER_ABSTIME 1

/* Per-process / per-thread CPU-time clocks. */
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3

/* timer_create / delete / get / set / overrun.  Stubs in libc
 * — substrate has no per-process timer infrastructure yet. */
typedef int timer_t;
struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};
int timer_create (clockid_t clk_id, void *sevp, timer_t *timerid);
int timer_delete (timer_t timerid);
int timer_getoverrun(timer_t timerid);
int timer_gettime(timer_t timerid, struct itimerspec *curr);
int timer_settime(timer_t timerid, int flags,
                  const struct itimerspec *new_value,
                  struct itimerspec *old_value);

#define TIME_UTC 1

#ifdef __cplusplus
}
#endif
#endif
