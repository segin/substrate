/*
 * loadavg.c - System Load Average Calculation
 *
 * Implements exponential moving average for 1, 5, and 15 minute intervals.
 * Sampled every 5 seconds (500 ticks).
 */

#include <kern/sched.h>
#include <sys/proc.h>
#include <stdint.h>

/*
 * Constants for averages over 1, 5, and 15 minutes
 * when sampling every 5 seconds.
 * 2048 * exp(-5/60), etc.
 */
#define EXP_1       1884
#define EXP_5       2014
#define EXP_15      2037

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
