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
#define SCHED_SPORADIC  4       /* POSIX sporadic server (real-time) */
#define SCHED_IDLE      5       /* idle priority */

/*
 * Maximum number of pending replenishment operations for the sporadic
 * server scheduling policy ({SS_REPL_MAX}).  POSIX requires a value of at
 * least {_POSIX_SS_REPL_MAX} (4).
 */
#define SS_REPL_MAX     4

/*
 * Sporadic-server option constant.  The canonical definition lives in
 * <unistd.h> beside the other _POSIX_* option macros; it is mirrored here
 * (guarded) so a translation unit that includes only <sched.h> — e.g. the
 * OPTS sched_setscheduler/19-2 test — still sees the option advertised and
 * compiles the SCHED_SPORADIC code path instead of the unsupported stub.
 */
#ifndef _POSIX_SPORADIC_SERVER
#define _POSIX_SPORADIC_SERVER          200809L
#endif
#ifndef _POSIX_THREAD_SPORADIC_SERVER
#define _POSIX_THREAD_SPORADIC_SERVER   200809L
#endif

/*
 * Process- and thread-priority scheduling option constants.  Canonical in
 * <unistd.h>; mirrored here (guarded) so a translation unit that pulls in only
 * <sched.h> still sees the SCHED_FIFO/RR priority option advertised.
 */
#ifndef _POSIX_PRIORITY_SCHEDULING
#define _POSIX_PRIORITY_SCHEDULING          200809L
#endif
#ifndef _POSIX_THREAD_PRIORITY_SCHEDULING
#define _POSIX_THREAD_PRIORITY_SCHEDULING   200809L
#endif

/*
 * The sporadic-server members (sched_ss_*) are appended AFTER
 * sched_priority so the structure stays backward-compatible: code that
 * only touches sched_priority keeps working and its offset is unchanged.
 */
struct sched_param {
    int             sched_priority;         /* base (high) priority       */
    int             sched_ss_low_priority;  /* low scheduling priority    */
    struct timespec sched_ss_repl_period;   /* replenishment period       */
    struct timespec sched_ss_init_budget;   /* initial execution budget   */
    int             sched_ss_max_repl;      /* max pending replenishments */
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
