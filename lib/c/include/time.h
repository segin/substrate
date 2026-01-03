#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>
#include <sys/types.h>

// Clock IDs for clock_gettime
typedef int clockid_t;
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

struct timespec {
    time_t tv_sec;   // seconds
    long   tv_nsec;  // nanoseconds
};

time_t time(time_t *tloc);
int clock_gettime(clockid_t clk_id, struct timespec *tp);

#endif