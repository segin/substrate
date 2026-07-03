#ifndef _SYS_SCHED_POSIX_H
#define _SYS_SCHED_POSIX_H

#include <sys/time.h>            /* struct timespec (sched_ss_* members) */

/*
 * Kernel-side POSIX scheduling definitions (sched_setscheduler(2),
 * sched_setparam(2), sched_get_priority_{min,max}(2), ...).
 *
 * The policy numbers MUST match the userland <sched.h> ABI.  They are
 * given a POSIX_ prefix here deliberately: <sys/proc.h> defines a
 * distinct kernel-internal `sched_class_t` enum whose members include
 * SCHED_IDLE, so re-using the bare POSIX macro names would collide.
 */

#define POSIX_SCHED_OTHER    0   /* default time-sharing scheduler   */
#define POSIX_SCHED_FIFO     1   /* real-time, first-in-first-out    */
#define POSIX_SCHED_RR       2   /* real-time, round-robin           */
#define POSIX_SCHED_BATCH    3   /* batch (non-interactive)          */
#define POSIX_SCHED_SPORADIC 4   /* POSIX sporadic server (real-time)*/
#define POSIX_SCHED_IDLE     5   /* very-low-priority background     */

/*
 * Real-time policies (FIFO/RR/SPORADIC) use a 1..99 priority band, matching
 * the Linux/glibc convention that the Open POSIX Test Suite expects; the
 * time-sharing policies use a single priority value of 0.
 */
#define POSIX_SCHED_PRIO_RT_MIN   1
#define POSIX_SCHED_PRIO_RT_MAX  99

/* Maximum pending replenishments for SCHED_SPORADIC ({SS_REPL_MAX});
 * mirror of the userland <sched.h> SS_REPL_MAX. */
#define POSIX_SS_REPL_MAX    4

/* Wire layout of userland `struct sched_param`.  The sporadic-server
 * members follow sched_priority; sched_priority MUST stay first so the
 * common (non-sporadic) path is layout-compatible with the old struct. */
struct sched_param {
    int             sched_priority;         /* base (high) priority       */
    int             sched_ss_low_priority;  /* low scheduling priority    */
    struct timespec sched_ss_repl_period;   /* replenishment period       */
    struct timespec sched_ss_init_budget;   /* initial execution budget   */
    int             sched_ss_max_repl;      /* max pending replenishments */
};

#endif /* _SYS_SCHED_POSIX_H */
