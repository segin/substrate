#ifndef _TIME_H_KERN
#define _TIME_H_KERN

#include <stdint.h>
#include <sys/types.h>
#include <sys/times.h>

// Get current ticks
uint64_t get_ticks(void);

// Get current Unix timestamp (wall-clock time)
time_t get_time(void);

// Get uptime in seconds since boot (monotonic)
time_t get_uptime(void);

// Get uptime in milliseconds since boot (monotonic)
int64_t get_uptime_ms(void);

// Get current system ticks
uint64_t get_ticks(void);

// Get system HZ
uint32_t get_hz(void);

// Timer tick handler (called from PIT/APIC interrupt)
void timer_tick(void);

// Syscalls
int sys_time(time_t *tloc);

struct timeval;
struct timezone;
int sys_gettimeofday(struct timeval *tv, struct timezone *tz);

struct timespec;
int sys_clock_gettime(clockid_t clk_id, struct timespec *tp);

clock_t sys_times(struct tms *buf);

#endif
