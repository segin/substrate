/*
 * loadavg.c - System Load Average Calculation
 *
 * Implements the standard UNIX load average calculation (1, 5, 15 minutes).
 * Updates are performed every 5 seconds.
 */

#include <kern/sched.h>
#include <sys/proc.h>
#include <stdint.h>

/* Fixed-point arithmetic constants (11-bit shift, scale 2048) */
#define FSHIFT      11
#define FSCALE      (1 << FSHIFT)
#define FIXED_1     FSCALE

/*
 * Exponential decay constants for 5 second update interval:
 * exp_n = e^(-5/n) * FSCALE
 */
#define EXP_1       1884    /* 1 minute: e^(-5/60) */
#define EXP_5       2014    /* 5 minutes: e^(-5/300) */
#define EXP_15      2037    /* 15 minutes: e^(-5/900) */

#define CALC_LOAD(load, exp, active) \
    (((unsigned long)(load) * (exp) + \
      ((unsigned long)(active) * FIXED_1) * (FIXED_1 - (exp))) >> FSHIFT)

/* Global load averages (1, 5, 15 min) */
static unsigned long avenrun[3] = { 0, 0, 0 };

/* Access to scheduler thread table */
extern thread_t threads[MAX_THREADS];

/*
 * Count runnable threads.
 * FreeBSD definition: threads in run queue (READY or RUNNING).
 */
uint32_t sched_count_runnable(void) {
    uint32_t count = 0;

    /*
     * Note: In a more advanced scheduler with per-cpu runqueues,
     * we would sum the lengths of those queues.
     * Here we scan the global thread table.
     */
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1) {
            if (threads[i].state == THREAD_RUNNING ||
                threads[i].state == THREAD_READY) {
                count++;
            }
        }
    }

    return count;
}

/*
 * Count total threads (active, not free).
 */
uint32_t sched_count_threads(void) {
    uint32_t count = 0;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid != -1) {
            count++;
        }
    }
    return count;
}

/*
 * Update load averages.
 * Should be called every 5 seconds.
 */
void sched_update_loadavg(void) {
    uint32_t active = sched_count_runnable();

    avenrun[0] = CALC_LOAD(avenrun[0], EXP_1, active);
    avenrun[1] = CALC_LOAD(avenrun[1], EXP_5, active);
    avenrun[2] = CALC_LOAD(avenrun[2], EXP_15, active);
}

/*
 * Retrieve current load averages.
 */
void sched_get_loadavg(unsigned long *loads) {
    loads[0] = avenrun[0];
    loads[1] = avenrun[1];
    loads[2] = avenrun[2];
}
