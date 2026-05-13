/* pthread_mutex_*  — test-and-set with PAUSE-spin then yield.  Other
 * pthread functions live in pthread_create.c, pthread_cond.c,
 * pthread_sig.c. */

#include "pthread.h"
#include <sched.h>

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr) {
    (void)attr;
    *mutex = 0;
    return 0;
}

/*
 * Why a hybrid spin/yield instead of either pure approach:
 *
 *   pure spin    — fastest under low contention, but on a single CPU
 *                  with N>2 contending threads the scheduler can't make
 *                  progress: every thread eats a full timeslice doing
 *                  xchg.  torture_kernel.wakeup hit this — 8 workers +
 *                  main contending one mutex ran at ~0.2 rounds/sec
 *                  because main was starved waiting to acquire mu.
 *
 *   pure yield   — fair, but every contended acquire syscalls.  Short
 *                  critical sections (set a flag, unlock) get destroyed.
 *
 * Hybrid: try once; if contended, spin SPIN_BUDGET times with PAUSE
 * to ride out the typical "set a flag, unlock" critical section;
 * yield if still contended.  64 spins is the canonical glibc value.
 */
#define SPIN_BUDGET 64

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (!__sync_lock_test_and_set(mutex, 1)) {
        return 0;
    }

    for (;;) {
        for (int i = 0; i < SPIN_BUDGET; i++) {
            if (!__sync_lock_test_and_set(mutex, 1)) {
                return 0;
            }
            __asm__ volatile("pause" ::: "memory");
        }
        sched_yield();
    }
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    __sync_lock_release(mutex);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}
