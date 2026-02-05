#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include <sys/types.h>

struct timeval {
    time_t      tv_sec;     /* seconds */
    suseconds_t tv_usec;    /* microseconds */
};

#ifndef _STRUCT_TIMESPEC_DEFINED
#define _STRUCT_TIMESPEC_DEFINED
struct timespec {
    time_t tv_sec;     /* seconds */
    long   tv_nsec;    /* nanoseconds */
};
#endif

struct timezone {
    int tz_minuteswest;     /* minutes west of Greenwich */
    int tz_dsttime;         /* type of DST correction */
};

struct itimerval {
    struct timeval it_interval; /* next value */
    struct timeval it_value;    /* current value */
};


int gettimeofday(struct timeval *restrict tp, void *restrict tzp);
int settimeofday(const struct timeval *tp, const void *tzp);

int getitimer(int which, struct itimerval *curr_value);
int setitimer(int which, const struct itimerval *restrict new_value,
              struct itimerval *restrict old_value);

int utimes(const char *path, const struct timeval times[2]);

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

#endif
