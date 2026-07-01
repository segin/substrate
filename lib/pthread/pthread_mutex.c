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
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/thr.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

/*
 * pthread_mutexattr_t is a bare int and must stay 4 bytes so the ABI is
 * unchanged for already-compiled consumers.  Its fields are bit-packed:
 *   [0:1] type        (PTHREAD_MUTEX_NORMAL/ERRORCHECK/RECURSIVE)
 *   [3:4] protocol    (PTHREAD_PRIO_NONE/INHERIT/PROTECT)
 *   [8:15] prioceiling
 * (pshared is not stored — see pthread_mutexattr_setpshared below.)
 */
#define MA_TYPE_MASK   0x0003
#define MA_PROTO_MASK  0x0018
#define MA_PROTO_SHIFT 3
#define MA_CEIL_MASK   0xff00
#define MA_CEIL_SHIFT  8

#define M_UNLOCKED  0
#define M_LOCKED    1
#define M_CONTENDED 2

/*
 * Recursive (and error-checking) mutexes.
 *
 * pthread_mutex_t is a single 32-bit word and glib heap-allocates exactly
 * sizeof(pthread_mutex_t) for each GRecMutex, so the type cannot grow
 * without an ABI break.  Instead, a RECURSIVE/ERRORCHECK mutex stores a
 * TAGGED POINTER to a heap `rec_state` that carries the real futex word,
 * the owner tid and the recursion count.  A normal mutex only ever holds
 * 0/1/2, so the low two bits are free as a discriminator: a recursive
 * mutex word is `(ptr | 3)` (the rec_state is at least 4-byte aligned, so
 * its low bits are clear), and `(*m & 3) == 3` distinguishes the two.
 * Without this, GRecMutex (used pervasively by GTK/glib) self-deadlocks
 * on the first recursive lock — pthread_mutex_lock always realised a
 * plain non-recursive mutex and parked forever on a re-lock.
 */
struct rec_state {
    int futex;      /* 0/1/2 lock word (the actual futex)        */
    int owner;      /* owning tid, 0 = unowned                   */
    int count;      /* recursion depth                           */
    int type;       /* PTHREAD_MUTEX_RECURSIVE / ERRORCHECK      */
};

#define MTX_IS_REC(v)  (((uintptr_t)(unsigned)(v) & 3u) == 3u)
#define MTX_REC(v)     ((struct rec_state *)((uintptr_t)(unsigned)(v) & ~(uintptr_t)3))
#define MTX_TAG(p)     ((int)((uintptr_t)(p) | 3u))

static int mtx_self(void) { return (int)syscall(SYS_THR_SELF); }

/* Drepper 3-state futex lock/unlock on a bare word. */
static void mtx_lock_word(int *w) {
    int c = __sync_val_compare_and_swap(w, M_UNLOCKED, M_LOCKED);
    if (c == M_UNLOCKED)
        return;
    if (c != M_CONTENDED)
        c = __sync_lock_test_and_set(w, M_CONTENDED);
    while (c != M_UNLOCKED) {
        syscall(SYS_FUTEX, (long)w, FUTEX_WAIT, M_CONTENDED, 0, 0, 0);
        c = __sync_lock_test_and_set(w, M_CONTENDED);
    }
}
static void mtx_unlock_word(int *w) {
    if (__sync_fetch_and_sub(w, 1) != M_LOCKED) {
        __sync_lock_release(w);   /* store 0 */
        syscall(SYS_FUTEX, (long)w, FUTEX_WAKE, 1, 0, 0, 0);
    }
}

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr) {
    int type = attr ? (*(const pthread_mutexattr_t *)attr & MA_TYPE_MASK)
                    : PTHREAD_MUTEX_DEFAULT;
    if (type == PTHREAD_MUTEX_RECURSIVE || type == PTHREAD_MUTEX_ERRORCHECK) {
        struct rec_state *s = malloc(sizeof *s);
        if (!s)
            return ENOMEM;
        s->futex = M_UNLOCKED;
        s->owner = 0;
        s->count = 0;
        s->type  = type;
        *mutex = MTX_TAG(s);
    } else {
        *mutex = M_UNLOCKED;
    }
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m) {
    int v = *m;
    if (MTX_IS_REC(v)) {
        struct rec_state *s = MTX_REC(v);
        int me = mtx_self();
        if (s->owner == me) {
            if (s->type == PTHREAD_MUTEX_ERRORCHECK)
                return EDEADLK;
            s->count++;        /* recursive re-acquire — no syscall */
            return 0;
        }
        mtx_lock_word(&s->futex);
        s->owner = me;
        s->count = 1;
        return 0;
    }

    /* Plain mutex: the word itself is the futex.  Fast path 0 -> 1. */
    int c = __sync_val_compare_and_swap(m, M_UNLOCKED, M_LOCKED);
    if (c == M_UNLOCKED)
        return 0;
    if (c != M_CONTENDED)
        c = __sync_lock_test_and_set(m, M_CONTENDED);
    while (c != M_UNLOCKED) {
        syscall(SYS_FUTEX, (long)m, FUTEX_WAIT, M_CONTENDED, 0, 0, 0);
        c = __sync_lock_test_and_set(m, M_CONTENDED);
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
    int v = *m;
    if (MTX_IS_REC(v)) {
        struct rec_state *s = MTX_REC(v);
        if (s->owner != mtx_self())
            return EPERM;
        if (--s->count == 0) {
            s->owner = 0;
            mtx_unlock_word(&s->futex);
        }
        return 0;
    }
    /* If old was LOCKED (1) uncontended → new is 0, no waiters.
     * If old was CONTENDED (2) → new is 1, store 0 and wake one. */
    if (__sync_fetch_and_sub(m, 1) != M_LOCKED) {
        __sync_lock_release(m);   /* store 0 */
        syscall(SYS_FUTEX, (long)m, FUTEX_WAKE, 1, 0, 0, 0);
    }
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m) {
    int v = *m;
    if (MTX_IS_REC(v)) {
        free(MTX_REC(v));
        *m = M_UNLOCKED;
    }
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
    int v = *m;
    if (MTX_IS_REC(v)) {
        struct rec_state *s = MTX_REC(v);
        int me = mtx_self();
        if (s->owner == me) {
            if (s->type == PTHREAD_MUTEX_ERRORCHECK)
                return EDEADLK;
            s->count++;
            return 0;
        }
        if (__sync_val_compare_and_swap(&s->futex, M_UNLOCKED, M_LOCKED) == M_UNLOCKED) {
            s->owner = me;
            s->count = 1;
            return 0;
        }
        return EBUSY;
    }
    /* Non-blocking acquire: take the uncontended 0 -> 1 transition,
     * or fail with EBUSY.  Never sets the CONTENDED state, so a
     * concurrent blocking lock()'s wakeup bookkeeping is unaffected. */
    if (__sync_val_compare_and_swap(m, M_UNLOCKED, M_LOCKED) == M_UNLOCKED)
        return 0;
    return EBUSY;
}

/*
 * Timed variant of mtx_lock_word: block on the futex word until it is
 * acquired or the absolute CLOCK_REALTIME deadline `abstime` passes.
 * Substrate's FUTEX_WAIT takes a RELATIVE timespec, so we convert
 * abstime - now on every parking attempt (see pthread_cond_timedwait).
 * Returns 0 on acquire, ETIMEDOUT on deadline, EINVAL on a malformed
 * timeout.
 */
static int mtx_timedlock_word(int *w, const struct timespec *abstime) {
    int c = __sync_val_compare_and_swap(w, M_UNLOCKED, M_LOCKED);
    if (c == M_UNLOCKED)
        return 0;
    if (!abstime ||
        abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)
        return EINVAL;
    if (c != M_CONTENDED)
        c = __sync_lock_test_and_set(w, M_CONTENDED);
    while (c != M_UNLOCKED) {
        struct timespec now, rel;
        if (clock_gettime(CLOCK_REALTIME, &now) != 0)
            return EINVAL;
        rel.tv_sec  = abstime->tv_sec  - now.tv_sec;
        rel.tv_nsec = abstime->tv_nsec - now.tv_nsec;
        if (rel.tv_nsec < 0) { rel.tv_sec -= 1; rel.tv_nsec += 1000000000L; }
        if (rel.tv_sec < 0 || (rel.tv_sec == 0 && rel.tv_nsec <= 0))
            return ETIMEDOUT;
        syscall(SYS_FUTEX, (long)w, FUTEX_WAIT, M_CONTENDED,
                (long)&rel, 0, 0);
        c = __sync_lock_test_and_set(w, M_CONTENDED);
        if (c == M_UNLOCKED)
            return 0;
        /* Any wake (timeout, EAGAIN, EINTR, real) loops back to the top,
         * which recomputes the delta and only reports ETIMEDOUT once the
         * ABSOLUTE deadline has genuinely passed — the kernel futex timeout
         * has coarse (tick) granularity and can fire a hair early. */
    }
    return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *abstime) {
    int v = *m;
    if (MTX_IS_REC(v)) {
        struct rec_state *s = MTX_REC(v);
        int me = mtx_self();
        if (s->owner == me) {
            if (s->type == PTHREAD_MUTEX_ERRORCHECK)
                return EDEADLK;
            s->count++;
            return 0;
        }
        int rc = mtx_timedlock_word(&s->futex, abstime);
        if (rc != 0)
            return rc;
        s->owner = me;
        s->count = 1;
        return 0;
    }
    return mtx_timedlock_word(m, abstime);
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
    *attr = (*attr & ~MA_TYPE_MASK) | type;      /* preserve packed protocol/ceiling */
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
    *type = *attr & MA_TYPE_MASK;
    return 0;
}

/*
 * Priority protocol + ceiling.  Substrate does not implement priority-
 * inheritance mutexes, so these only store and report the requested value
 * (bit-packed into the attr int); they do not change scheduling.
 */
int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int protocol) {
    if (!attr) return EINVAL;
    if (protocol != PTHREAD_PRIO_NONE &&
        protocol != PTHREAD_PRIO_INHERIT &&
        protocol != PTHREAD_PRIO_PROTECT)
        return EINVAL;
    *attr = (*attr & ~MA_PROTO_MASK) | (protocol << MA_PROTO_SHIFT);
    return 0;
}

int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr, int *protocol) {
    if (!attr || !protocol) return EINVAL;
    *protocol = (*attr & MA_PROTO_MASK) >> MA_PROTO_SHIFT;
    return 0;
}

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *attr, int prioceiling) {
    if (!attr) return EINVAL;
    /* Accept any priority in the SCHED_FIFO range substrate reports
     * (sched_get_priority_min/max == 0..99). */
    if (prioceiling < 0 || prioceiling > 0xff)
        return EINVAL;
    *attr = (*attr & ~MA_CEIL_MASK) | ((prioceiling & 0xff) << MA_CEIL_SHIFT);
    return 0;
}

int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *attr, int *prioceiling) {
    if (!attr || !prioceiling) return EINVAL;
    *prioceiling = (*attr & MA_CEIL_MASK) >> MA_CEIL_SHIFT;
    return 0;
}
