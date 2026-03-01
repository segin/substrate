#ifndef _KERN_SCHED_H
#define _KERN_SCHED_H

#include <sys/proc.h>
#include <sys/smp.h>

/* Thread and CPU limits */
#define MAX_THREADS 64

extern thread_t threads[MAX_THREADS];
extern thread_t *current_thread;

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

/* Scheduler API */
void sched_init(void);
void swapper_request_work(void);

/* Thread Creation */
thread_t *sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg);

/* Fork (Clone Process) */
int sched_fork_process(process_t *parent, void *stack);
int sched_spawn_kernel_process(void (*entry)(void*), void *arg);

void sched_yield(void);
void sched_switch(thread_t *next);
int sched_get_current_tid(void);
void sched_set_priority(int tid, sched_class_t cls, int prio);
void sched_tick(void);
void sched_sleep(void *chan);
int sched_sleep_until(void *chan, uint64_t deadline_tick);
void sched_wakeup(void *chan);
void sched_wakeup_n(void *chan, int n);
process_t *sched_create_process(struct personality *pers);
thread_t *sched_get_thread(int tid);
void sched_iterate_threads(void (*callback)(thread_t *t, void *arg), void *arg);

void sched_check_timeouts(void);

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

#endif
