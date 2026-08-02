/*
 * pthread_spin_* — a minimal atomic test-and-set spin lock.
 *
 * The lock is a single int embedded by value (0 = free, 1 = held), so a lock
 * placed in shared memory works across processes (PTHREAD_PROCESS_SHARED) as
 * well as between threads.  Substrate is preemptive, but on a single CPU a
 * pure busy-wait would starve the holder, so the contended path yields the
 * CPU (test-and-test-and-set) to guarantee forward progress.
 *
 * Other pthread functions live in pthread_create.c, pthread_mutex.c,
 * pthread_cond.c, pthread_extra.c, pthread_barrier.c, pthread_atfork.c.
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>

int pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
    if (!lock)
        return EINVAL;
    if (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED)
        return EINVAL;
    *lock = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock) {
    if (!lock)
        return EINVAL;
    return 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock) {
    if (!lock)
        return EINVAL;
    while (__sync_lock_test_and_set(lock, 1)) {
        /* Spin until it looks free, yielding so the holder can run. */
        while (*lock)
            sched_yield();
    }
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *lock) {
    if (!lock)
        return EINVAL;
    return __sync_lock_test_and_set(lock, 1) ? EBUSY : 0;
}

int pthread_spin_unlock(pthread_spinlock_t *lock) {
    if (!lock)
        return EINVAL;
    __sync_lock_release(lock);
    return 0;
}
