#ifndef _SCHED_H
#define _SCHED_H

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Scheduling policies (Linux/POSIX compatible numbering). */
#define SCHED_OTHER     0       /* default time-sharing scheduler */
#define SCHED_FIFO      1       /* real-time, first-in-first-out */
#define SCHED_RR        2       /* real-time, round-robin */
#define SCHED_BATCH     3       /* batch (non-interactive) */
#define SCHED_IDLE      5       /* idle priority */

struct sched_param {
    int sched_priority;
};

/* Voluntarily yield the CPU to another runnable thread.  Substrate
 * kernel: sys_yield (in sys/kern/syscall.c).  Always succeeds. */
int sched_yield(void);

/* These are declared so binaries that pull in <sched.h> as a
 * dependency of some other header link cleanly; the implementations
 * may be ENOSYS stubs until substrate grows the matching kernel
 * surface. */
int sched_getscheduler(pid_t pid);
int sched_setscheduler(pid_t pid, int policy,
                       const struct sched_param *param);
int sched_getparam(pid_t pid, struct sched_param *param);
int sched_setparam(pid_t pid, const struct sched_param *param);
int sched_get_priority_min(int policy);
int sched_get_priority_max(int policy);
int sched_rr_get_interval(pid_t pid, struct timespec *interval);

#ifdef __cplusplus
}
#endif

#endif /* _SCHED_H */
