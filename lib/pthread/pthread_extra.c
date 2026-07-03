/* pthread_extra.c — thread-attribute objects, condition-variable attribute
 * objects, and thread-specific-data (TLS) keys.  These round out the POSIX
 * surface needed by larger consumers such as GLib.  Core threading lives in
 * pthread_create.c / pthread_mutex.c / pthread_cond.c.
 */
#include "pthread.h"
#include <errno.h>
#include <sched.h>
#include <time.h>

/* ---------------- thread attributes ----------------
 * pthread_attr_t is a bare int.  pthread_create honours the detach-state bit
 * (see pthread_create.c); stack size is advisory (fixed 64 KiB) and the other
 * fields are stored/reported for source compatibility. */
int pthread_attr_init(pthread_attr_t *attr)            { if (attr) *attr = PTHREAD_CREATE_JOINABLE; return 0; }
int pthread_attr_destroy(pthread_attr_t *attr)         { (void)attr; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t s) {
    if (!attr) return EINVAL;
    /* POSIX: EINVAL if the requested size is below PTHREAD_STACK_MIN.  The
     * per-thread stack is a fixed 64 KiB (== PTHREAD_STACK_MIN); a request at
     * or above the floor is accepted but remains advisory. */
    if (s < (size_t)PTHREAD_STACK_MIN) return EINVAL;
    return 0;
}
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *s) {
    (void)attr; if (s) *s = 64 * 1024; return 0;       /* the fixed default */
}

/* pthread_get/setschedparam / pthread_setschedprio live in pthread_create.c,
 * beside the per-thread registry that stores the round-trip policy/priority. */

/*
 * pthread_attr_t is a bare int (4-byte ABI, unchanged for already-compiled
 * consumers) with two bit-packed fields:
 *   bit 0 — detach state  (PTHREAD_CREATE_JOINABLE / DETACHED)
 *   bit 1 — inherit sched (PTHREAD_INHERIT_SCHED / EXPLICIT_SCHED)
 */
#define AT_DETACH_MASK   0x1
#define AT_INHERIT_MASK  0x2
#define AT_INHERIT_SHIFT 1

int pthread_attr_setdetachstate(pthread_attr_t *attr, int state) {
    if (!attr) return EINVAL;
    if (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED)
        return EINVAL;
    *attr = (*attr & ~AT_DETACH_MASK) | (state & AT_DETACH_MASK);
    return 0;
}
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state) {
    if (state) *state = attr ? (*attr & AT_DETACH_MASK) : PTHREAD_CREATE_JOINABLE;
    return 0;
}
/* inherit-sched: stored and reported faithfully, but substrate's scheduler
 * treats INHERIT and EXPLICIT alike (no per-thread policy honoured yet). */
int pthread_attr_setinheritsched(pthread_attr_t *attr, int inheritsched) {
    if (!attr) return EINVAL;
    if (inheritsched != PTHREAD_INHERIT_SCHED &&
        inheritsched != PTHREAD_EXPLICIT_SCHED)
        return EINVAL;
    *attr = (*attr & ~AT_INHERIT_MASK) | (inheritsched << AT_INHERIT_SHIFT);
    return 0;
}
int pthread_attr_getinheritsched(const pthread_attr_t *attr, int *inheritsched) {
    if (!attr || !inheritsched) return EINVAL;
    *inheritsched = (*attr & AT_INHERIT_MASK) >> AT_INHERIT_SHIFT;
    return 0;
}
/* Contention scope: substrate threads are always 1:1 system scope.  Accept
 * PTHREAD_SCOPE_SYSTEM, reject the valid-but-unsupported PTHREAD_SCOPE_PROCESS
 * with ENOTSUP, and reject any other (invalid) value with EINVAL.  Always
 * report system scope.  (The attr int holds the detach state, so scope is not
 * stored — there is only one supported value.) */
int pthread_attr_setscope(pthread_attr_t *attr, int scope) {
    (void)attr;
    if (scope == PTHREAD_SCOPE_SYSTEM) return 0;
    if (scope == PTHREAD_SCOPE_PROCESS) return ENOTSUP;
    return EINVAL;
}
int pthread_attr_getscope(const pthread_attr_t *attr, int *scope) {
    (void)attr; if (scope) *scope = PTHREAD_SCOPE_SYSTEM; return 0;
}

/* ---------------- condition-variable attributes ----------------
 * A condattr is just the clock id; pthread_cond_init() copies it into the
 * cond so pthread_cond_timedwait() measures the deadline against it. */
int pthread_condattr_init(pthread_condattr_t *attr)    { if (attr) *attr = 0 /*CLOCK_REALTIME*/; return 0; }
int pthread_condattr_destroy(pthread_condattr_t *attr) { (void)attr; return 0; }
int pthread_condattr_setclock(pthread_condattr_t *attr, int clock_id) {
    if (!attr) return EINVAL;
    /* POSIX: the clock must be one that can time a condition wait; a CPU-time
     * clock (CLOCK_PROCESS_CPUTIME_ID / CLOCK_THREAD_CPUTIME_ID, which is what
     * clock_getcpuclockid returns) is not permitted and must fail with EINVAL
     * (pthread_condattr_setclock/1-3). */
    if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC)
        return EINVAL;
    *attr = clock_id;
    return 0;
}
int pthread_condattr_getclock(const pthread_condattr_t *attr, int *clock_id) {
    if (clock_id) *clock_id = attr ? *attr : 0;
    return 0;
}

/* ---------------- thread-specific data (keys) ----------------
 * Keys are a small global table; each thread holds its own value vector in a
 * __thread array (pthread_create installs per-thread TLS, so every thread —
 * including the initial one — gets a private copy).  Destructors are recorded
 * and run on thread exit via __pthread_tsd_run_destructors().
 */
/* Each key slot carries a generation counter, bumped every time the slot is
 * (re)created.  A thread's stored value is tagged with the generation it was
 * set under; if that doesn't match the slot's current generation the value
 * belongs to a deleted key that happened to reuse this slot, so getspecific
 * reports NULL.  This is how a freshly created key reads NULL in every thread
 * (POSIX) even when the slot index is recycled. */
static struct {
    int used;
    unsigned gen;
    void (*dtor)(void *);
} key_table[PTHREAD_KEYS_MAX];
static int key_lock;
/* Initial-exec TLS model, NOT the default general-dynamic: GD emits a call to
 * __tls_get_addr, which substrate's ld.so does not implement, so a GD __thread
 * in this shared library fails to link.  Initial-exec resolves to a fixed
 * %gs-relative offset (no helper call) and works because libpthread is always
 * a startup DT_NEEDED, never dlopen'd. */
static __thread struct { unsigned gen; void *val; } key_values[PTHREAD_KEYS_MAX]
    __attribute__((tls_model("initial-exec")));

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    if (!key) return EINVAL;
    while (__sync_lock_test_and_set(&key_lock, 1)) sched_yield();
    for (unsigned i = 0; i < PTHREAD_KEYS_MAX; i++) {
        if (!key_table[i].used) {
            key_table[i].used = 1;
            key_table[i].gen++;            /* new generation for this slot */
            key_table[i].dtor = destructor;
            __sync_lock_release(&key_lock);
            *key = i;
            return 0;
        }
    }
    __sync_lock_release(&key_lock);
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;
    key_table[key].used = 0;               /* gen stays; next create bumps it */
    key_table[key].dtor = NULL;
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX) return NULL;
    if (key_values[key].gen != key_table[key].gen) return NULL;  /* stale/unset */
    return key_values[key].val;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;
    key_values[key].gen = key_table[key].gen;
    key_values[key].val = (void *)value;
    return 0;
}

/* Run the destructors for the CURRENT thread's set keys, the way POSIX
 * thread exit does: repeat up to PTHREAD_DESTRUCTOR_ITERATIONS times, since a
 * destructor may set another key.  Called from pthread_exit() and the
 * thread trampoline when the start routine returns. */
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
void __pthread_tsd_run_destructors(void) {
    for (int pass = 0; pass < PTHREAD_DESTRUCTOR_ITERATIONS; pass++) {
        int ran = 0;
        for (unsigned i = 0; i < PTHREAD_KEYS_MAX; i++) {
            void *v = key_values[i].val;
            if (v && key_table[i].used && key_table[i].dtor &&
                key_values[i].gen == key_table[i].gen) {
                key_values[i].val = NULL;   /* clear before calling, per POSIX */
                key_table[i].dtor(v);
                ran = 1;
            }
        }
        if (!ran) break;
    }
}

/* ---------------- read/write locks ----------------
 * A writer-preferring rwlock over the mutex + condvar.  readers: >0 = that
 * many read holders, -1 = a single write holder, 0 = free.  waiting_writers
 * blocks new readers so writers can't starve. */

/*
 * Per-thread set of rwlocks this thread holds for WRITING.  Used to detect a
 * self-deadlock: a thread that re-locks (for writing or reading) a write lock
 * it already owns would otherwise block forever on its own hold.  POSIX
 * permits pthread_rwlock_wr/rdlock to fail with EDEADLK in that case.
 *
 * Kept in libpthread TLS rather than in pthread_rwlock_t, whose layout is
 * fixed for already-compiled consumers (growing it would corrupt their
 * by-value allocations).  Initial-exec TLS model — libpthread is a startup
 * DT_NEEDED, never dlopen'd.  Best-effort: a thread holding more than
 * RW_WR_HELD_MAX write locks simply isn't tracked past the cap (the EDEADLK
 * detection is a "may fail", so a missed entry only reverts to the old
 * block-forever behaviour for that unusual case).
 */
#define RW_WR_HELD_MAX 32
static __thread pthread_rwlock_t *rw_wr_held[RW_WR_HELD_MAX]
    __attribute__((tls_model("initial-exec")));

static int rw_wr_owned(pthread_rwlock_t *rw) {
    for (int i = 0; i < RW_WR_HELD_MAX; i++)
        if (rw_wr_held[i] == rw) return 1;
    return 0;
}
static void rw_wr_add(pthread_rwlock_t *rw) {
    for (int i = 0; i < RW_WR_HELD_MAX; i++)
        if (!rw_wr_held[i]) { rw_wr_held[i] = rw; return; }
}
static void rw_wr_del(pthread_rwlock_t *rw) {
    for (int i = 0; i < RW_WR_HELD_MAX; i++)
        if (rw_wr_held[i] == rw) { rw_wr_held[i] = NULL; return; }
}

int pthread_rwlock_init(pthread_rwlock_t *rw, const pthread_rwlockattr_t *attr) {
    (void)attr;
    pthread_mutex_init(&rw->lock, NULL);
    pthread_cond_init(&rw->cond, NULL);
    rw->readers = 0;
    rw->waiting_writers = 0;
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rw) {
    pthread_cond_destroy(&rw->cond);
    pthread_mutex_destroy(&rw->lock);
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    while (rw->readers < 0 || rw->waiting_writers > 0)
        pthread_cond_wait(&rw->cond, &rw->lock);
    rw->readers++;
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    int ok = !(rw->readers < 0 || rw->waiting_writers > 0);
    if (ok) rw->readers++;
    pthread_mutex_unlock(&rw->lock);
    return ok ? 0 : EBUSY;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rw) {
    /* Re-locking a write lock the calling thread already holds would block
     * forever waiting for itself to release; report the deadlock instead. */
    if (rw_wr_owned(rw))
        return EDEADLK;
    pthread_mutex_lock(&rw->lock);
    rw->waiting_writers++;
    while (rw->readers != 0)
        pthread_cond_wait(&rw->cond, &rw->lock);
    rw->waiting_writers--;
    rw->readers = -1;
    rw_wr_add(rw);
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    int ok = (rw->readers == 0);
    if (ok) rw->readers = -1;
    pthread_mutex_unlock(&rw->lock);
    if (ok) rw_wr_add(rw);
    return ok ? 0 : EBUSY;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    if (rw->readers < 0) { rw->readers = 0; rw_wr_del(rw); }  /* drop the writer */
    else if (rw->readers > 0) rw->readers--;   /* drop one reader */
    pthread_cond_broadcast(&rw->cond);         /* waiters re-check the predicate */
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

/* Has the absolute CLOCK_REALTIME deadline genuinely passed?  The kernel
 * futex timeout has coarse (tick) granularity and can fire a hair early, so
 * an ETIMEDOUT from pthread_cond_timedwait is only honoured once now has
 * actually reached abstime — otherwise it is treated as a spurious wake and
 * the predicate is re-checked (POSIX forbids returning ETIMEDOUT early). */
static int rwlock_deadline_passed(const struct timespec *abstime) {
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return 1;
    return now.tv_sec > abstime->tv_sec ||
           (now.tv_sec == abstime->tv_sec && now.tv_nsec >= abstime->tv_nsec);
}

/* Timed read-lock: block on the writer-preferring predicate until the read
 * lock is granted or the absolute CLOCK_REALTIME deadline lapses.  The
 * rwlock's condvar is CLOCK_REALTIME (init'd with a NULL condattr), which is
 * the clock POSIX specifies for these calls. */
int pthread_rwlock_timedrdlock(pthread_rwlock_t *rw, const struct timespec *abstime) {
    pthread_mutex_lock(&rw->lock);
    while (rw->readers < 0 || rw->waiting_writers > 0) {
        int rc = pthread_cond_timedwait(&rw->cond, &rw->lock, abstime);
        if (rc == ETIMEDOUT && rwlock_deadline_passed(abstime)) {
            /* Deadline genuinely passed.  Re-check the predicate one last
             * time before timing out: while we were parked a signal handler
             * may have run and the lock may have been released — POSIX says
             * the wait resumes "as if it was not interrupted", so a lock that
             * is now available must be granted rather than spuriously timing
             * out (pthread_rwlock_timedrdlock/6-2). */
            if (rw->readers < 0 || rw->waiting_writers > 0) {
                pthread_mutex_unlock(&rw->lock);
                return ETIMEDOUT;
            }
            break;   /* lock available — fall through to acquire */
        } else if (rc != 0 && rc != ETIMEDOUT) {
            pthread_mutex_unlock(&rw->lock);
            return rc;
        }
        /* early timeout / spurious wake / signal: loop re-checks the predicate */
    }
    rw->readers++;
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

/* Timed write-lock: same, but on a genuine timeout stop counting as a waiting
 * writer and wake any readers we were blocking. */
int pthread_rwlock_timedwrlock(pthread_rwlock_t *rw, const struct timespec *abstime) {
    if (rw_wr_owned(rw))
        return EDEADLK;
    pthread_mutex_lock(&rw->lock);
    rw->waiting_writers++;
    while (rw->readers != 0) {
        int rc = pthread_cond_timedwait(&rw->cond, &rw->lock, abstime);
        if (rc == ETIMEDOUT && rwlock_deadline_passed(abstime)) {
            /* Re-check before timing out — the lock may have been released
             * while a signal handler ran (pthread_rwlock_timedwrlock/6-2). */
            if (rw->readers != 0) {
                rw->waiting_writers--;
                pthread_cond_broadcast(&rw->cond);
                pthread_mutex_unlock(&rw->lock);
                return ETIMEDOUT;
            }
            break;   /* lock free now — fall through to acquire */
        } else if (rc != 0 && rc != ETIMEDOUT) {
            rw->waiting_writers--;
            pthread_cond_broadcast(&rw->cond);
            pthread_mutex_unlock(&rw->lock);
            return rc;
        }
        /* early timeout / spurious wake / signal: loop re-checks the predicate */
    }
    rw->waiting_writers--;
    rw->readers = -1;
    rw_wr_add(rw);
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

/* Cancellation (pthread_cancel / setcancelstate / setcanceltype /
 * testcancel) and the cleanup-handler stack live in pthread_cancel.c. */
