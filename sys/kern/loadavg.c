/*
 * loadavg.c - System Load Average Calculation
 *
 * Implements exponential moving average for 1, 5, and 15 minute intervals.
 * Sampled every 5 seconds (500 ticks).
 */

#include <kern/sched.h>
#include <sys/proc.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Fixed-point arithmetic constants.
 * We use 11 bits for the fractional part (2048 = 1.0).
 */
#define FSHIFT  11
#define FSCALE  (1<<FSHIFT)

/*
 * Constants for averages over 1, 5, and 15 minutes
 * when sampling every 5 seconds.
 *
 * EXP_1  = 1/exp(5/60)  * 2048 = 1884
 * EXP_5  = 1/exp(5/300) * 2048 = 2014
 * EXP_15 = 1/exp(5/900) * 2048 = 2037
 */
#define EXP_1   1884
#define EXP_5   2014
#define EXP_15  2037

/* Global load averages (fixed-point) */
static unsigned long avenrun[3] = {0, 0, 0};

/*
 * Callback to count runnable threads
 */
static void count_runnable_cb(thread_t *t, void *arg) {
    int *n = (int *)arg;
    if (t->state == THREAD_RUNNING || t->state == THREAD_READY) {
        (*n)++;
    }
}

/*
 * Update load averages.
 * Should be called every 5 seconds (500 ticks).
 */
void sched_loadavg_update(void) {
    int nrun = 0;

    // Count runnable threads across all threads
    sched_iterate_threads(count_runnable_cb, &nrun);

    // Convert integer count to fixed-point
    unsigned long nrun_fp = (unsigned long)nrun << FSHIFT;

    // Update averages
    // load = (old * exp + new * (fscale - exp)) / fscale
    avenrun[0] = (avenrun[0] * EXP_1 + nrun_fp * (FSCALE - EXP_1)) >> FSHIFT;
    avenrun[1] = (avenrun[1] * EXP_5 + nrun_fp * (FSCALE - EXP_5)) >> FSHIFT;
    avenrun[2] = (avenrun[2] * EXP_15 + nrun_fp * (FSCALE - EXP_15)) >> FSHIFT;
}

/*
 * Get current load averages.
 * Copies up to n values to the provided array.
 */
void sched_get_loadavg(unsigned long averages[], int n) {
    if (n > 3) n = 3;
    for (int i = 0; i < n; i++) {
        averages[i] = avenrun[i];
    }
}
