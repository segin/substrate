#ifndef _KERN_SCHED_H
#define _KERN_SCHED_H

#include <sys/proc.h>
#include <sys/smp.h>

/*
 * Thread registry layout
 * ======================
 *
 * Every thread_t is kmalloc'd at sched_alloc_thread() time and linked
 * into two structures protected by sched.c's tid_lock:
 *
 *   allthread     — singly-linked list of every live thread, walked
 *                   by FOREACH_THREAD / thread_first / thread_next.
 *   tid_hash[]    — open-chained hash table for O(1)
 *                   sched_get_thread(tid).
 *
 * The swapper's TID-0 thread is the first one bootstrapped via
 * sched_alloc_thread(kernel_process).
 */

extern thread_t *current_thread;

thread_t *thread_first(void);
thread_t *thread_next(thread_t *t);

#define FOREACH_THREAD(var) \
    for (thread_t *var = thread_first(); (var) != NULL; (var) = thread_next(var))

/* IPI vector for scheduler preemption (must match IDT setup) */
#define SCHED_IPI_VECTOR 0xFD

/* Interactivity constants */
#define INTERACT_MAX        128     /* Maximum interactivity score */
#define INTERACT_THRESH     30      /* Threshold to be considered "interactive" */
#define INTERACT_DECAY      2       /* Decay rate per tick when running */
#define INTERACT_BOOST      4       /* Boost rate per tick when sleeping */

/* Time slice constants (in scheduler ticks) */
#define SLICE_MIN           1       /* Minimum time slice */
#define SLICE_MAX           10      /* Maximum time slice (for idle threads) */
#define SLICE_INTERACTIVE   4       /* Time slice for interactive threads */
#define SLICE_BATCH         8       /* Time slice for batch/CPU-bound threads */

/* Decay constants */
#define DECAY_PERIOD        100     /* Ticks between decay passes */
#define DECAY_AMOUNT        1       /* Priority penalty per period for running threads */
#define RECALC_PERIOD       1000    /* Ticks between full priority recalculation */

/* Anti-starvation constants */
#define STARVATION_LIMIT    500     /* Ticks before boosting starved thread */
#define STARVATION_BOOST    5       /* Priority boost for starved thread */

/* Weighted fair-share (CFS-style vruntime) for the SCHED_TIMESHARE class.
 * A running timeshare thread accrues vruntime each tick, scaled inversely by
 * its nice weight (see sched_interactivity.c); the pick chooses the lowest
 * vruntime, so CPU time ends up proportional to weight with no starvation.
 * sched_min_vruntime is the minimum across ready timeshare threads, published
 * by the pick; a waking or newly-created thread is rebased to it so a long
 * sleep (or a stale zero) can't let it monopolise the CPU. */
extern uint64_t sched_min_vruntime;

/* Scheduler API */
void sched_init(void);
void sched_init_generic(void);
void sched_smp_init(int cpu_count);
void swapper_request_work(void);

/* Thread Creation */
thread_t *sched_alloc_thread(process_t *proc);
thread_t *sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg);

/* Fork (Clone Process) */
int sched_fork_process(process_t *parent, void *stack);
int sched_fork_thread(process_t *proc, void *stack);
int arch_fork_with_stack(void *child_stack);
int sched_spawn_kernel_process(void (*entry)(void*), void *arg);

void sched_yield(void);

/* Freeze userspace for shutdown: after this the scheduler never
 * dispatches a user thread again except `keep` (the reboot caller).
 * Kernel threads keep running — the unmount/flush path needs them. */
void sched_halt_userspace(thread_t *keep);

void sched_switch(thread_t *next);
int sched_get_current_tid(void);
void sched_set_priority(int tid, sched_class_t cls, int prio);
void sched_tick(void);
void sched_vruntime_tick(thread_t *t);   /* per-tick weighted fair-share vruntime advance */
void sched_sleep(void *chan);
int sched_sleep_until(void *chan, uint64_t deadline_tick);
void sched_wakeup(void *chan);
void sched_wakeup_n(void *chan, int n);
void sched_poll_wake_pollers(void);

/*
 * poll()/select() wake channel + a monotonic counter bumped on every wake
 * fanned out to it.  kern_poll() snapshots the counter before its fd-scan and,
 * if it changed by the time the scan finds nothing ready, re-scans instead of
 * sleeping into the backstop — closing the lost-wakeup window where an event
 * arrives while the poller is RUNNING mid-scan (so sched_wakeup_n can't ready
 * it).  Bumped unconditionally when chan == &g_poll_wake_chan, even when no
 * poller was blocked (that IS the lost-wakeup case).
 */
extern char g_poll_wake_chan;
extern volatile uint64_t g_poll_wake_seq;
process_t *sched_create_process(struct personality *pers);
thread_t *sched_get_thread(int tid);
void sched_iterate_threads(void (*callback)(thread_t *t, void *arg), void *arg);
void sched_reap_process_threads(process_t *proc);

/*
 * Reap one specific thread: unlink from allthread/tid_hash, release
 * kernel stack, free the thread_t.  Caller must be sure no one else
 * holds a pointer to t (typically the thread is THREAD_ZOMBIE and a
 * joiner just confirmed it).
 */
void sched_reap_thread(thread_t *t);

void sched_check_timeouts(void);
int sched_can_run_on_cpu(thread_t *t, int cpu_id);
int sched_bind_thread(thread_t *t, int cpu_id);
void sched_unbind_thread(thread_t *t);

extern int num_cpus;
void sched_enqueue(thread_t *t);
void sched_dequeue(thread_t *t);

/* Load Average */
#define SI_LOAD_SCALE 2048
#define FSHIFT  11
#define FSCALE  SI_LOAD_SCALE
#define FIXED_1 FSCALE
#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) ((((x) & (FSCALE-1)) * 100) >> FSHIFT)

/* Load average constants (1, 5, 15 min with 5 sec interval) */
#define EXP_1  1884  /* 2048 * exp(-5/60) */
#define EXP_5  2014  /* 2048 * exp(-5/300) */
#define EXP_15 2037  /* 2048 * exp(-5/900) */

void sched_update_loadavg(void);
void sched_get_loadavg(unsigned long *loads);
uint32_t sched_count_runnable(void);
uint32_t sched_count_threads(void);
void sched_periodic_balance(void);
thread_t *sched_idle_balance(void);

#endif
