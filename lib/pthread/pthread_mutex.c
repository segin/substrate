/* pthread_mutex_* — Linux-style 3-state futex mutex.
 *
 *   0 = unlocked
 *   1 = locked, no waiters
 *   2 = locked, possibly waiters
 *
 * Algorithm: Ulrich Drepper, "Futexes Are Tricky" (2011).
 *
 * Why a real futex rather than a spinlock: with N threads
 * contending one mutex on a single CPU, the pure test-and-set
 * spinlock livelocks — every thread burns a full quantum on
 * xchg+pause before being preempted, and the holder is just one
 * of N, so lock churn dwarfs actual critical-section work.
 * torture_kernel.wakeup hit this with 9 contenders.  With
 * FUTEX_WAIT/WAKE the contended waiters block in the kernel
 * instead of burning cycles.
 *
 * Other pthread functions live in pthread_create.c, pthread_cond.c,
 * pthread_sig.c. */

#include "pthread.h"
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

#define M_UNLOCKED  0
#define M_LOCKED    1
#define M_CONTENDED 2

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr) {
    (void)attr;
    *mutex = M_UNLOCKED;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m) {
    /* Fast path: uncontended acquire (0 -> 1).
     * __sync_val_compare_and_swap returns the OLD value. */
    int c = __sync_val_compare_and_swap(m, M_UNLOCKED, M_LOCKED);
    if (c == M_UNLOCKED) {
        return 0;
    }

    /* Slow path: someone else holds it.  Mark CONTENDED so the
     * holder knows to wake us when they release, then park in the
     * kernel until they do.  The xchg loop handles the race where
     * the holder releases between our exchange and the futex_wait —
     * if c becomes 0, we already own it (with the lock now marked
     * CONTENDED, which is benign; the next unlock will wake one
     * waiter that doesn't exist, costing nothing). */
    if (c != M_CONTENDED) {
        c = __sync_lock_test_and_set(m, M_CONTENDED);
    }
    while (c != M_UNLOCKED) {
        syscall(SYS_FUTEX, (long)m, FUTEX_WAIT, M_CONTENDED, 0, 0, 0);
        c = __sync_lock_test_and_set(m, M_CONTENDED);
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
    /* If old was LOCKED (1) uncontended → new is 0, no waiters.
     * If old was CONTENDED (2) → new is 1, store 0 and wake one. */
    if (__sync_fetch_and_sub(m, 1) != M_LOCKED) {
        __sync_lock_release(m);   /* store 0 */
        syscall(SYS_FUTEX, (long)m, FUTEX_WAKE, 1, 0, 0, 0);
    }
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
    /* Non-blocking acquire: take the uncontended 0 -> 1 transition,
     * or fail with EBUSY.  Never sets the CONTENDED state, so a
     * concurrent blocking lock()'s wakeup bookkeeping is unaffected. */
    if (__sync_val_compare_and_swap(m, M_UNLOCKED, M_LOCKED) == M_UNLOCKED)
        return 0;
    return EBUSY;
}

/*
 * Mutex attributes.  pthread_mutexattr_t is a bare int holding the
 * mutex type (PTHREAD_MUTEX_NORMAL / ERRORCHECK / RECURSIVE).
 *
 * Note: pthread_mutex_t is itself a single futex word with no room
 * for an owner id or recursion count, so pthread_mutex_init() always
 * realises a normal mutex.  The attribute type is stored and reported
 * faithfully, but RECURSIVE / ERRORCHECK semantics are not honoured;
 * callers needing recursion must track ownership themselves.
 */
int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
    if (!attr) return EINVAL;
    *attr = PTHREAD_MUTEX_DEFAULT;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
    (void)attr;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    if (!attr) return EINVAL;
    if (type != PTHREAD_MUTEX_NORMAL &&
        type != PTHREAD_MUTEX_ERRORCHECK &&
        type != PTHREAD_MUTEX_RECURSIVE)
        return EINVAL;
    *attr = type;
    return 0;
}

/* Process-shared attribute.  The attr int holds the mutex type, so pshared
 * is not stored; setpshared just validates the flag and succeeds (substrate
 * mutexes are best-effort across processes — see <pthread.h>). */
int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared) {
    if (!attr) return EINVAL;
    if (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED)
        return EINVAL;
    return 0;
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr, int *pshared) {
    if (!attr || !pshared) return EINVAL;
    *pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type) {
    if (!attr || !type) return EINVAL;
    *type = *attr;
    return 0;
}
