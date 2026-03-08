#ifndef _TIME_H_KERN
#define _TIME_H_KERN

#include <stdint.h>
#include <sys/time.h>
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

// Get system HZ
uint32_t get_hz(void);

// Timer tick handlers (called from PIT/APIC interrupt)
void timer_tick(void);
void timer_tick_context(int is_usermode);

// Syscalls
time_t sys_time(time_t *tloc);
unsigned int sys_alarm(unsigned int sec);
int sys_getitimer(int which, struct itimerval *curr_value);
int sys_setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value);

struct timezone;
int sys_gettimeofday(struct timeval *tv, struct timezone *tz);

int sys_clock_gettime(clockid_t clk_id, struct timespec *tp);

clock_t sys_times(struct tms *buf);

struct process;
void proc_timers_init(struct process *p);
void proc_timers_cancel(struct process *p);

#endif
