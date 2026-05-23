#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
/* POSIX says fd_set and friends are in <sys/select.h>, but historical
 * BSD code expects to find them via <sys/time.h>.  Linux/glibc bring
 * them in this way too — ported software (xorg-server, ...) relies on
 * the implicit pull-in. */
#include <sys/select.h>

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


int gettimeofday(struct timeval *__restrict tp, void *__restrict tzp);
int settimeofday(const struct timeval *tp, const void *tzp);

int getitimer(int which, struct itimerval *curr_value);
int setitimer(int which, const struct itimerval *__restrict new_value,
              struct itimerval *__restrict old_value);

int utimes(const char *path, const struct timeval times[2]);
int utimensat(int dirfd, const char *path, const struct timespec times[2],
              int flags);
int futimens(int fd, const struct timespec times[2]);

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1

/* BSD-historical timeval arithmetic macros — POSIX-optional but
 * present on every modern Unix.  Used by OpenSSH (misc.c), curl,
 * inetutils, ... */
#ifndef timerclear
#define timerclear(tvp)   ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define timerisset(tvp)   ((tvp)->tv_sec || (tvp)->tv_usec)
#define timercmp(a, b, CMP) \
    (((a)->tv_sec == (b)->tv_sec) \
        ? ((a)->tv_usec CMP (b)->tv_usec) \
        : ((a)->tv_sec CMP (b)->tv_sec))
#define timeradd(a, b, res) do { \
    (res)->tv_sec  = (a)->tv_sec  + (b)->tv_sec; \
    (res)->tv_usec = (a)->tv_usec + (b)->tv_usec; \
    if ((res)->tv_usec >= 1000000) { (res)->tv_sec++; (res)->tv_usec -= 1000000; } \
} while (0)
#define timersub(a, b, res) do { \
    (res)->tv_sec  = (a)->tv_sec  - (b)->tv_sec; \
    (res)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
    if ((res)->tv_usec < 0) { (res)->tv_sec--; (res)->tv_usec += 1000000; } \
} while (0)
#endif

#ifdef __cplusplus
}
#endif
#endif
