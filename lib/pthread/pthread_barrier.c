/*
 * pthread_barrier_* — a rendezvous barrier for a fixed number of threads,
 * built on the mutex + condition variable in this library.  Each round, the
 * threshold-th arriving thread releases every waiter and receives
 * PTHREAD_BARRIER_SERIAL_THREAD; the others get 0.  The barrier auto-recycles
 * for the next round via a generation counter, so the same barrier can be
 * reused without re-initialising it.
 *
 * Other pthread functions live in pthread_create.c, pthread_mutex.c,
 * pthread_cond.c, pthread_extra.c, pthread_spin.c, pthread_atfork.c.
 */
#include "pthread.h"
#include <errno.h>

int pthread_barrier_init(pthread_barrier_t *b,
                         const pthread_barrierattr_t *attr, unsigned int count) {
    (void)attr;                       /* pshared is advisory — see below */
    if (!b || count == 0)
        return EINVAL;
    pthread_mutex_init(&b->lock, NULL);
    pthread_cond_init(&b->cond, NULL);
    b->count = count;
    b->left  = count;
    b->cycle = 0;
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *b) {
    if (!b)
        return EINVAL;
    /* POSIX permits EBUSY when the barrier is still in use (a thread is
     * blocked in pthread_barrier_wait).  Some threads have arrived and are
     * waiting for the rest exactly when fewer than `count` slots remain, i.e.
     * left != count (pthread_barrier_destroy/2-1). */
    pthread_mutex_lock(&b->lock);
    if (b->left != b->count) {
        pthread_mutex_unlock(&b->lock);
        return EBUSY;
    }
    pthread_mutex_unlock(&b->lock);
    pthread_cond_destroy(&b->cond);
    pthread_mutex_destroy(&b->lock);
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *b) {
    if (!b)
        return EINVAL;

    pthread_mutex_lock(&b->lock);
    unsigned int gen = b->cycle;

    if (--b->left == 0) {
        /* Last thread in: open the next round and release everyone. */
        b->cycle++;
        b->left = b->count;
        pthread_cond_broadcast(&b->cond);
        pthread_mutex_unlock(&b->lock);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }

    /* Wait until the round advances (guards against spurious wakeups). */
    while (gen == b->cycle)
        pthread_cond_wait(&b->cond, &b->lock);

    pthread_mutex_unlock(&b->lock);
    return 0;
}

/* ---------------- barrier attributes ----------------
 * The only attribute is process-shared.  Substrate threads and the
 * futex-backed cond/mutex a barrier is built from are per-address-space, so
 * PTHREAD_PROCESS_SHARED is accepted and stored for source compatibility but
 * a barrier placed in shared memory is not a guaranteed cross-process
 * rendezvous. */
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
