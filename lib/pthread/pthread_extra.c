/* pthread_extra.c — thread-attribute objects, condition-variable attribute
 * objects, and thread-specific-data (TLS) keys.  These round out the POSIX
 * surface needed by larger consumers such as GLib.  Core threading lives in
 * pthread_create.c / pthread_mutex.c / pthread_cond.c.
 */
#include <errno.h>
#include <pthread.h>
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
 * consumers) with four bit-packed fields:
 *   bit 0     — detach state  (PTHREAD_CREATE_JOINABLE / DETACHED)
 *   bit 1     — inherit sched (PTHREAD_INHERIT_SCHED / EXPLICIT_SCHED)
 *   bits 2-3  — sched policy  (SCHED_OTHER / SCHED_FIFO / SCHED_RR)
 *   bits 4-11 — sched priority
 *
 * The two scheduling fields were added for TQt3, whose TQThread::start()
 * calls pthread_attr_getschedpolicy() and pthread_attr_setschedparam().
 * They pack into spare bits of the same int rather than widening the type,
 * so the 4-byte ABI every already-compiled consumer sees is unchanged.
 */
#define AT_DETACH_MASK   0x1
#define AT_INHERIT_MASK  0x2
#define AT_INHERIT_SHIFT 1
#define AT_POLICY_MASK   0xC
#define AT_POLICY_SHIFT  2
#define AT_PRIO_MASK     0xFF0
#define AT_PRIO_SHIFT    4

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
/*
 * Scheduling policy / parameters carried on the attr.
 *
 * Stored and reported faithfully so a get() round-trips what was set(), which
 * is what callers actually test.  As with inherit-sched above, substrate's
 * scheduler does not yet run threads strictly by POSIX policy and priority —
 * pthread_create() records them in the per-thread registry (pthread_create.c),
 * and the places that CAN honour them (rwlock acquisition ordering) consult
 * that.  Reporting a value we accepted is honest; silently rewriting it to
 * SCHED_OTHER would not be.
 */
int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy) {
    if (!attr) return EINVAL;
    if (policy != SCHED_OTHER && policy != SCHED_FIFO && policy != SCHED_RR)
        return EINVAL;
    *attr = (*attr & ~AT_POLICY_MASK) | ((policy << AT_POLICY_SHIFT) & AT_POLICY_MASK);
    return 0;
}
int pthread_attr_getschedpolicy(const pthread_attr_t *attr, int *policy) {
    if (!attr || !policy) return EINVAL;
    *policy = (*attr & AT_POLICY_MASK) >> AT_POLICY_SHIFT;
    return 0;
}
int pthread_attr_setschedparam(pthread_attr_t *attr,
                               const struct sched_param *param) {
    if (!attr || !param) return EINVAL;
    int prio = param->sched_priority;
    /* The field is 8 bits.  Reject anything that would not survive the
     * round-trip rather than silently truncating it. */
    if (prio < 0 || prio > (int)(AT_PRIO_MASK >> AT_PRIO_SHIFT))
        return EINVAL;
    *attr = (*attr & ~AT_PRIO_MASK) | ((prio << AT_PRIO_SHIFT) & AT_PRIO_MASK);
    return 0;
}
int pthread_attr_getschedparam(const pthread_attr_t *attr,
                               struct sched_param *param) {
    if (!attr || !param) return EINVAL;
    param->sched_priority = (*attr & AT_PRIO_MASK) >> AT_PRIO_SHIFT;
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
 * A priority-aware rwlock over the mutex + condvar.  readers: >0 = that many
 * read holders, -1 = a single write holder, 0 = free.  waiting_writers counts
 * blocked writers.
 *
 * At uniform priority (every thread SCHED_OTHER at priority 0 — the common
 * case) it is writer-preferring: a reader blocks whenever a writer waits, so
 * writers can't starve.  When threads run under SCHED_FIFO/SCHED_RR at distinct
 * priorities, POSIX (_POSIX_THREAD_PRIORITY_SCHEDULING) instead orders
 * acquisition by scheduling priority — a reader is not blocked behind a
 * strictly-lower-priority waiting writer, and a freed lock goes to the highest-
 * priority waiter (a writer winning ties with an equal-priority reader).  The
 * priority logic lives in the block predicates below (rw_reader_blocks /
 * rw_writer_blocks); see the waiter-registry comment. */

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

/*
 * Priority-aware acquisition registry (POSIX Thread Execution Scheduling).
 *
 * substrate's kernel scheduler does not run threads strictly by SCHED_FIFO
 * priority, and libpthread's pthread_setschedparam only records the requested
 * policy/priority in the userspace thread registry (pthread_create.c) — so the
 * POSIX priority ordering of rwlock acquisition is enforced HERE, by consulting
 * that recorded priority.  Every blocked waiter publishes itself on a global
 * list with its snapshotted priority and reader/writer kind; the block
 * predicates consult the list to decide whether the caller must yield to a
 * competing waiter.  A waiter node lives in the blocking thread's own TLS (a
 * thread blocks on at most one rwlock at a time), so the list needs no
 * allocation and scales with the thread count.  Initial-exec TLS model —
 * libpthread is a startup DT_NEEDED, never dlopen'd.
 *
 * The list spinlock is a leaf: it is taken only while already holding an
 * rwlock's internal mutex (order: rw->lock -> rw_wait_lock) and never across a
 * blocking call, so it cannot deadlock.
 */
struct rw_waiter {
    struct rw_waiter *next;
    pthread_rwlock_t *rw;      /* the rwlock this thread is blocked on */
    int prio;                  /* snapshotted scheduling priority       */
    int is_writer;             /* 1 = write waiter, 0 = read waiter     */
};
static __thread struct rw_waiter rw_self_waiter
    __attribute__((tls_model("initial-exec")));
static struct rw_waiter *rw_wait_head;    /* global list of blocked waiters */
static int rw_wait_lock;                  /* leaf spinlock guarding the list */

static void rw_wait_register(pthread_rwlock_t *rw, int prio, int is_writer) {
    struct rw_waiter *n = &rw_self_waiter;
    while (__sync_lock_test_and_set(&rw_wait_lock, 1)) sched_yield();
    n->rw = rw; n->prio = prio; n->is_writer = is_writer;
    n->next = rw_wait_head; rw_wait_head = n;
    __sync_lock_release(&rw_wait_lock);
}
static void rw_wait_unregister(void) {
    struct rw_waiter *n = &rw_self_waiter;
    while (__sync_lock_test_and_set(&rw_wait_lock, 1)) sched_yield();
    for (struct rw_waiter **pp = &rw_wait_head; *pp; pp = &(*pp)->next)
        if (*pp == n) { *pp = n->next; break; }
    n->next = NULL;
    __sync_lock_release(&rw_wait_lock);
}
/* Is some OTHER thread a blocked WRITER on rw with priority >= myprio?  A reader
 * yields to a writer of equal-or-higher priority (write precedence on ties). */
static int rw_writer_ge_waiting(pthread_rwlock_t *rw, int myprio) {
    int found = 0;
    while (__sync_lock_test_and_set(&rw_wait_lock, 1)) sched_yield();
    for (struct rw_waiter *n = rw_wait_head; n; n = n->next)
        if (n != &rw_self_waiter && n->rw == rw && n->is_writer && n->prio >= myprio) {
            found = 1; break;
        }
    __sync_lock_release(&rw_wait_lock);
    return found;
}
/* Is some OTHER thread a blocked waiter on rw with priority STRICTLY > myprio?
 * A writer yields only to a strictly-higher-priority waiter (reader or writer). */
static int rw_waiter_gt_waiting(pthread_rwlock_t *rw, int myprio) {
    int found = 0;
    while (__sync_lock_test_and_set(&rw_wait_lock, 1)) sched_yield();
    for (struct rw_waiter *n = rw_wait_head; n; n = n->next)
        if (n != &rw_self_waiter && n->rw == rw && n->prio > myprio) {
            found = 1; break;
        }
    __sync_lock_release(&rw_wait_lock);
    return found;
}

/* Calling thread's scheduling priority (0 for SCHED_OTHER), read back from the
 * libpthread thread registry that pthread_setschedparam populates. */
static int rw_self_priority(void) {
    int policy = 0;
    struct sched_param sp;
    sp.sched_priority = 0;
    pthread_getschedparam(pthread_self(), &policy, &sp);
    return sp.sched_priority;
}

/* Reader block predicate: block while a writer holds the lock, or a waiting
 * writer of equal-or-higher priority is queued.  At uniform priority this is
 * exactly the old "readers < 0 || waiting_writers > 0" writer-preference. */
static int rw_reader_blocks(pthread_rwlock_t *rw, int myprio) {
    if (rw->readers < 0) return 1;
    if (rw->waiting_writers == 0) return 0;
    return rw_writer_ge_waiting(rw, myprio);
}
/* Writer block predicate: block while the lock is held, or a strictly-higher-
 * priority waiter (reader or writer) is queued ahead.  At uniform priority this
 * is exactly the old "readers != 0". */
static int rw_writer_blocks(pthread_rwlock_t *rw, int myprio) {
    if (rw->readers != 0) return 1;
    return rw_waiter_gt_waiting(rw, myprio);
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
    /* Fast path: no writer holds and none waits — priority is irrelevant. */
    if (rw->readers < 0 || rw->waiting_writers > 0) {
        int myprio = rw_self_priority();
        if (rw_reader_blocks(rw, myprio)) {
            rw_wait_register(rw, myprio, 0);
            do { pthread_cond_wait(&rw->cond, &rw->lock); }
            while (rw_reader_blocks(rw, myprio));
            rw_wait_unregister();
        }
    }
    rw->readers++;
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    int ok = (rw->readers >= 0 && rw->waiting_writers == 0) ||
             !rw_reader_blocks(rw, rw_self_priority());
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
    /* Fast path: lock free and no writer waiting — priority is irrelevant. */
    if (rw->readers != 0 || rw->waiting_writers > 0) {
        int myprio = rw_self_priority();
        if (rw_writer_blocks(rw, myprio)) {
            rw->waiting_writers++;
            rw_wait_register(rw, myprio, 1);
            do { pthread_cond_wait(&rw->cond, &rw->lock); }
            while (rw_writer_blocks(rw, myprio));
            rw_wait_unregister();
            rw->waiting_writers--;
        }
    }
    rw->readers = -1;
    rw_wr_add(rw);
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    int ok = (rw->readers == 0 && rw->waiting_writers == 0) ||
             !rw_writer_blocks(rw, rw_self_priority());
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

/* Timed read-lock: block on the (priority-aware) read predicate until the read
 * lock is granted or the absolute CLOCK_REALTIME deadline lapses.  The
 * rwlock's condvar is CLOCK_REALTIME (init'd with a NULL condattr), which is
 * the clock POSIX specifies for these calls. */
int pthread_rwlock_timedrdlock(pthread_rwlock_t *rw, const struct timespec *abstime) {
    pthread_mutex_lock(&rw->lock);
    if (rw->readers < 0 || rw->waiting_writers > 0) {
        int myprio = rw_self_priority();
        if (rw_reader_blocks(rw, myprio)) {
            rw_wait_register(rw, myprio, 0);
            for (;;) {
                int rc = pthread_cond_timedwait(&rw->cond, &rw->lock, abstime);
                /* Lock available now?  Acquire it, even if we also timed out:
                 * while we were parked a signal handler may have run and the
                 * lock been released — POSIX says the wait resumes "as if it was
                 * not interrupted", so an available lock must be granted rather
                 * than spuriously timing out (timedrdlock/6-2). */
                if (!rw_reader_blocks(rw, myprio))
                    break;
                if (rc == ETIMEDOUT && rwlock_deadline_passed(abstime)) {
                    rw_wait_unregister();
                    pthread_mutex_unlock(&rw->lock);
                    return ETIMEDOUT;
                } else if (rc != 0 && rc != ETIMEDOUT) {
                    rw_wait_unregister();
                    pthread_mutex_unlock(&rw->lock);
                    return rc;
                }
                /* early timeout / spurious wake / signal: loop re-checks */
            }
            rw_wait_unregister();
        }
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
    if (rw->readers != 0 || rw->waiting_writers > 0) {
        int myprio = rw_self_priority();
        if (rw_writer_blocks(rw, myprio)) {
            rw->waiting_writers++;
            rw_wait_register(rw, myprio, 1);
            for (;;) {
                int rc = pthread_cond_timedwait(&rw->cond, &rw->lock, abstime);
                /* Lock free now?  Acquire it, even if we also timed out — the
                 * lock may have been released while a signal handler ran
                 * (timedwrlock/6-2). */
                if (!rw_writer_blocks(rw, myprio))
                    break;
                if (rc == ETIMEDOUT && rwlock_deadline_passed(abstime)) {
                    rw_wait_unregister();
                    rw->waiting_writers--;
                    pthread_cond_broadcast(&rw->cond);   /* release readers we blocked */
                    pthread_mutex_unlock(&rw->lock);
                    return ETIMEDOUT;
                } else if (rc != 0 && rc != ETIMEDOUT) {
                    rw_wait_unregister();
                    rw->waiting_writers--;
                    pthread_cond_broadcast(&rw->cond);
                    pthread_mutex_unlock(&rw->lock);
                    return rc;
                }
                /* early timeout / spurious wake / signal: loop re-checks */
            }
            rw_wait_unregister();
            rw->waiting_writers--;
        }
    }
    rw->readers = -1;
    rw_wr_add(rw);
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

/* Cancellation (pthread_cancel / setcancelstate / setcanceltype /
 * testcancel) and the cleanup-handler stack live in pthread_cancel.c. */
