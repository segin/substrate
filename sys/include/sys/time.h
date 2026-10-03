#ifdef HOST_TEST
#include_next <sys/time.h>
#else
#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include <sys/types.h>
#include <sys/abi32.h>

/* timeval and timespec keep the i386 layout on both kernels
 * (<sys/abi32.h>): 4-byte-aligned 64-bit seconds, a 32-bit tv_nsec. */
struct timeval {
    abi_int64_t tv_sec;     /* seconds (time_t) */
    abi_int64_t tv_usec;    /* microseconds (suseconds_t) */
};

struct timezone {
    int tz_minuteswest;     /* minutes west of Greenwich */
    int tz_dsttime;         /* type of DST correction */
};

struct itimerval {
    struct timeval it_interval; /* next value */
    struct timeval it_value;    /* current value */
};

#ifndef _STRUCT_TIMESPEC_DEFINED
#define _STRUCT_TIMESPEC_DEFINED
struct timespec {
    abi_int64_t tv_sec;     /* seconds (time_t) */
    abi_long_t  tv_nsec;    /* nanoseconds */
};
#endif
ABI32_ASSERT_SIZE(struct timespec, 12);
ABI32_ASSERT_SIZE(struct timeval, 16);
ABI32_ASSERT_SIZE(struct itimerval, 32);

// FD_SET macros are often in sys/select.h, but historically here too.
// For now, minimal.

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
#endif
