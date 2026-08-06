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

// Wall-clock time recorded at boot (set by rtc_init)
extern time_t boot_time;

// Get the wall-clock time recorded at boot
time_t get_boot_time(void);

// Get uptime in seconds since boot (monotonic)
time_t get_uptime(void);

// Get uptime in milliseconds since boot (monotonic)
int64_t get_uptime_ms(void);
/* Busy-wait `ms` milliseconds without relying on the timer tick, for callers
 * that run with interrupts disabled (where get_uptime_ms() cannot advance). */
void timer_busywait_ms(unsigned ms);

// Get system HZ
uint32_t get_hz(void);

// Timer tick handlers (called from PIT/APIC interrupt)
void timer_init(void);
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

/* POSIX.1b per-process interval timers (timer_create(2)). */
struct sigevent;
struct itimerspec;
void proc_ptimers_clear(struct process *p);      /* create/fork/exec/exit reset */
void proc_ptimers_fire(struct process *p);       /* per-tick expiry evaluation */
void ptimer_signal_delivered(struct process *p, int sig); /* latch overrun */

int kern_timer_create(int clockid, struct sigevent *ev, int *timerid);
int kern_timer_settime(int timerid, int flags,
                       const struct itimerspec *newval, struct itimerspec *oldval);
int kern_timer_gettime(int timerid, struct itimerspec *curr);
int kern_timer_delete(int timerid);
int kern_timer_getoverrun(int timerid);

int sys_timer_create(int clockid, struct sigevent *sevp, int *timerid);
int sys_timer_settime(int timerid, int flags,
                      const struct itimerspec *newval, struct itimerspec *oldval);
int sys_timer_gettime(int timerid, struct itimerspec *curr);
int sys_timer_delete(int timerid);
int sys_timer_getoverrun(int timerid);

#endif
