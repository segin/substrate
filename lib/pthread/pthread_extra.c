/* pthread_extra.c — thread-attribute objects, condition-variable attribute
 * objects, and thread-specific-data (TLS) keys.  These round out the POSIX
 * surface needed by larger consumers such as GLib.  Core threading lives in
 * pthread_create.c / pthread_mutex.c / pthread_cond.c.
 */
#include "pthread.h"
#include <errno.h>
#include <sched.h>

/* ---------------- thread attributes ----------------
 * pthread_attr_t is an int.  pthread_create currently ignores the attribute
 * object (fixed stack, always joinable), so these record only what fits and
 * are otherwise no-ops that return success — enough for callers that build an
 * attr, set a stack size / detach state, and create a thread. */
int pthread_attr_init(pthread_attr_t *attr)            { if (attr) *attr = PTHREAD_CREATE_JOINABLE; return 0; }
int pthread_attr_destroy(pthread_attr_t *attr)         { (void)attr; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t s)    { (void)attr; (void)s; return 0; }
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *s) {
    (void)attr; if (s) *s = 64 * 1024; return 0;       /* the fixed default */
}
int pthread_attr_setdetachstate(pthread_attr_t *attr, int state) { if (attr) *attr = state; return 0; }
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state) {
    if (state) *state = attr ? *attr : PTHREAD_CREATE_JOINABLE;
    return 0;
}
/* Contention scope: substrate threads are always 1:1 system scope.  Accept
 * PTHREAD_SCOPE_SYSTEM, reject PTHREAD_SCOPE_PROCESS, and always report
 * system scope.  (The attr int holds the detach state, so scope is not
 * stored — there is only one valid value.) */
int pthread_attr_setscope(pthread_attr_t *attr, int scope) {
    (void)attr;
    return scope == PTHREAD_SCOPE_SYSTEM ? 0 : ENOTSUP;
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
    pthread_mutex_lock(&rw->lock);
    rw->waiting_writers++;
    while (rw->readers != 0)
        pthread_cond_wait(&rw->cond, &rw->lock);
    rw->waiting_writers--;
    rw->readers = -1;
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    int ok = (rw->readers == 0);
    if (ok) rw->readers = -1;
    pthread_mutex_unlock(&rw->lock);
    return ok ? 0 : EBUSY;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    if (rw->readers < 0) rw->readers = 0;      /* drop the writer */
    else if (rw->readers > 0) rw->readers--;   /* drop one reader */
    pthread_cond_broadcast(&rw->cond);         /* waiters re-check the predicate */
    pthread_mutex_unlock(&rw->lock);
    return 0;
}

/* ---------------- cancellation (no-op surface) ----------------
 * substrate has no cancellation runtime; provide the POSIX entry points
 * so threaded consumers (Qt/TQt, ...) link.  pthread_cancel() is a no-op
 * (it does not stop the target thread); the state/type setters just
 * report the defaults and pthread_testcancel() never has a pending
 * cancel to act on. */
int pthread_cancel(pthread_t thread) { (void)thread; return 0; }

int pthread_setcancelstate(int state, int *oldstate) {
    (void)state;
    if (oldstate) *oldstate = PTHREAD_CANCEL_ENABLE;
    return 0;
}

int pthread_setcanceltype(int type, int *oldtype) {
    (void)type;
    if (oldtype) *oldtype = PTHREAD_CANCEL_DEFERRED;
    return 0;
}

void pthread_testcancel(void) { }
