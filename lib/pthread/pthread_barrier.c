/*
 * pthread_barrier_* — a rendezvous barrier for a fixed number of threads.
 *
 * The barrier state (count / left / cycle) is manipulated purely with atomic
 * operations and a sched_yield() spin, so a barrier placed in shared memory
 * rendezvouses across PROCESSES (PTHREAD_PROCESS_SHARED), not just threads of
 * one process.  A mutex+condvar implementation could not: substrate's futex
 * keys on the virtual address + PID (sys/kern/futex.c), so a condvar wake in
 * one process never reaches a waiter blocked in another — the two map the same
 * physical page at different virtual addresses under different PIDs.  Atomics
 * on the shared page, by contrast, operate on the same physical memory from
 * either process (the same technique pthread_spin_* uses).
 *
 * Each round, the count-th arriving thread advances the generation counter,
 * which releases every waiter, and it receives PTHREAD_BARRIER_SERIAL_THREAD;
 * the others get 0.  Waiters key on the generation (not `left`), so the
 * barrier auto-recycles for the next round with no reuse race.
 *
 * Other pthread functions live in pthread_create.c, pthread_mutex.c,
 * pthread_cond.c, pthread_extra.c, pthread_spin.c, pthread_atfork.c.
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>

int pthread_barrier_init(pthread_barrier_t *b,
                         const pthread_barrierattr_t *attr, unsigned int count) {
    (void)attr;                       /* pshared handled uniformly (see wait) */
    if (!b || count == 0)
        return EINVAL;
    b->count = count;
    b->left  = count;
    b->cycle = 0;
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *b) {
    if (!b)
        return EINVAL;
    /* POSIX permits EBUSY when the barrier is still in use (some threads have
     * arrived and are waiting for the rest, i.e. left != count —
     * pthread_barrier_destroy/2-1). */
    if (__atomic_load_n(&b->left, __ATOMIC_ACQUIRE) != b->count)
        return EBUSY;
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *b) {
    if (!b)
        return EINVAL;

    /* Snapshot the current round before arriving. */
    unsigned int gen = __atomic_load_n(&b->cycle, __ATOMIC_ACQUIRE);

    /* Arrive.  The thread whose decrement empties the barrier is the last in. */
    if (__atomic_sub_fetch(&b->left, 1, __ATOMIC_ACQ_REL) == 0) {
        /* Reopen the barrier for the next round, THEN advance the generation.
         * The release on the cycle bump publishes the reset `left` before any
         * waiter (or next-round arriver) can observe the new generation. */
        __atomic_store_n(&b->left, b->count, __ATOMIC_RELAXED);
        __atomic_add_fetch(&b->cycle, 1, __ATOMIC_ACQ_REL);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }

    /* Wait until the last arriver advances the round.  Yield so a same-CPU
     * peer (or the other process sharing this page) can make progress. */
    while (__atomic_load_n(&b->cycle, __ATOMIC_ACQUIRE) == gen)
        sched_yield();
    return 0;
}

/* ---------------- barrier attributes ----------------
 * The only attribute is process-shared.  Because pthread_barrier_wait() is
 * built from atomics on the barrier's own storage (no futex, no per-process
 * kernel object), a barrier placed in shared memory IS a real cross-process
 * rendezvous, so PTHREAD_PROCESS_SHARED is honoured, not merely stored. */
int pthread_barrierattr_init(pthread_barrierattr_t *attr) {
    if (!attr)
        return EINVAL;
    attr->pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t *attr) {
    if (!attr)
        return EINVAL;
    return 0;
}

int pthread_barrierattr_getpshared(const pthread_barrierattr_t *attr, int *pshared) {
    if (!attr || !pshared)
        return EINVAL;
    *pshared = attr->pshared;
    return 0;
}

int pthread_barrierattr_setpshared(pthread_barrierattr_t *attr, int pshared) {
    if (!attr)
        return EINVAL;
    if (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED)
        return EINVAL;
    attr->pshared = pshared;
    return 0;
}
