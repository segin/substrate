/*
 * torture_pifutex — contended priority-inheritance futexes.
 *
 * Several threads share one PI lock built directly on FUTEX_LOCK_PI /
 * FUTEX_UNLOCK_PI: take it with a 0 -> tid compare-and-swap, fall into
 * the kernel when that fails, and release with tid -> 0 or
 * FUTEX_UNLOCK_PI when the word has more than our tid in it.
 *
 * The kernel must:
 *   - never free its pi_state under a thread that is acquiring the
 *     lock (a woken waiter left the waiter list before it became the
 *     owner, so the owner fields landed in freed memory);
 *   - hand the lock on: every worker finishes, and the count comes out
 *     exact.
 *
 * Substrate-only (native SYS_FUTEX and thr_self).
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/futex.h>
#include <sys/syscall.h>

#define THREADS 6
#define ROUNDS  5000

static volatile uint32_t word;
static volatile long count;
static volatile long sys_err;
static volatile int done;

static void pi_lock(uint32_t tid) {
    uint32_t zero = 0;
    if (__atomic_compare_exchange_n(&word, &zero, tid, 0, __ATOMIC_ACQUIRE,
                                    __ATOMIC_RELAXED))
        return;
    long r = syscall(SYS_FUTEX, (long)&word, FUTEX_LOCK_PI, 0, 0, 0, 0);
    if (r != 0)
        sys_err = r;
}

static void pi_unlock(uint32_t tid) {
    uint32_t mine = tid;
    if (__atomic_compare_exchange_n(&word, &mine, 0, 0, __ATOMIC_RELEASE,
                                    __ATOMIC_RELAXED))
        return;
    long r = syscall(SYS_FUTEX, (long)&word, FUTEX_UNLOCK_PI, 0, 0, 0, 0);
    if (r != 0)
        sys_err = r;
}

static void *worker(void *arg) {
    (void)arg;
    uint32_t tid = (uint32_t)syscall(SYS_THR_SELF);
    for (int i = 0; i < ROUNDS && !sys_err; i++) {
        pi_lock(tid);
        long c = count;
        if ((i & 15) == 0)
            sched_yield();          /* hold it while others queue */
        count = c + 1;
        pi_unlock(tid);
    }
    __atomic_add_fetch(&done, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

int main(void) {
    pthread_t t[THREADS];
    setvbuf(stdout, NULL, _IONBF, 0);

    for (int i = 0; i < THREADS; i++)
        pthread_create(&t[i], NULL, worker, NULL);

    /* A worker the kernel never wakes would hang the join: watch the
     * finished count for a minute instead. */
    for (int ticks = 0; done < THREADS && ticks < 6000; ticks++)
        usleep(10000);
    if (done < THREADS) {
        printf("FAIL  stuck: %d of %d workers finished, count %ld, word %#x\n",
               done, THREADS, count, (unsigned)word);
        printf("torture_pifutex: FAIL\n");
        return 1;
    }
    for (int i = 0; i < THREADS; i++)
        pthread_join(t[i], NULL);

    long want = (long)THREADS * ROUNDS;
    int ok = !sys_err && count == want;
    printf("%s futex errors: %ld\n", sys_err ? "FAIL " : "ok   ", sys_err);
    printf("%s count %ld (want %ld)\n", count == want ? "ok   " : "FAIL ",
           count, want);
    printf("torture_pifutex: %s\n", ok ? "PASS" : "FAIL");
    return !ok;
}
