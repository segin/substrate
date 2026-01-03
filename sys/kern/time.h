#ifndef _TIME_H_KERN
#define _TIME_H_KERN

#include <stdint.h>

// Get current Unix timestamp (wall-clock time)
int64_t get_time(void);

// Get uptime in seconds since boot (monotonic)
int64_t get_uptime(void);

// Timer tick handler (called from PIT/APIC interrupt)
void timer_tick(void);

// Syscalls
int64_t sys_time(int64_t *tloc);

struct timeval;
struct timezone;
int sys_gettimeofday(struct timeval *tv, struct timezone *tz);

struct timespec;
int sys_clock_gettime(int clk_id, struct timespec *tp);

struct tms;
int32_t sys_times(struct tms *buf);

#endif
