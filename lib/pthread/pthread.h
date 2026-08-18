#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <stddef.h>
#include <signal.h>     /* sigset_t for pthread_sigmask, pthread_kill */
#include <sched.h>      /* sched_yield + sched_get_priority_* — POSIX makes
                         * these visible via <pthread.h>, and libstdc++/libgcc
                         * gthr-posix.h relies on it (matches glibc). */

#ifdef __cplusplus
extern "C" {
#endif

typedef int pthread_t;
typedef int pthread_attr_t;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine) (void *), void *arg);

void pthread_exit(void *retval);
int pthread_join(pthread_t thread, void **retval);

/* Mark a thread detached: it is never joined, and its resources are
 * reclaimed automatically when it finishes. */
int pthread_detach(pthread_t thread);

/* Current-thread identity.  Returns the kernel-assigned TID. */
pthread_t pthread_self(void);

/* Thread scheduling policy/priority.  substrate's MLFQ does not yet honour an
 * explicit per-thread policy, so get reports SCHED_OTHER/0 and set is an
 * accepted no-op (consumers that bump worker priority tolerate it). */
struct sched_param;
int pthread_getschedparam(pthread_t thread, int *policy, struct sched_param *param);
int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param *param);
int pthread_setschedprio(pthread_t thread, int prio);

/* Compare two thread IDs for equality (POSIX pthread_equal). */
static inline int pthread_equal(pthread_t t1, pthread_t t2) {
    return t1 == t2;
}

/* Send signal `sig` to thread `thread` — POSIX thread-directed
 * signal delivery via SYS_THR_KILL.  `sig == 0` probes whether the
 * thread exists (returns 0/ESRCH without delivering). */
int pthread_kill(pthread_t thread, int sig);

/* Per-thread signal mask.  Wraps the same kernel sigprocmask used
 * by single-threaded process callers — substrate stores sig_mask
 * per-thread, so this is mostly a renamed sigprocmask. */
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);

typedef int pthread_mutex_t;
typedef int pthread_mutexattr_t;

#define PTHREAD_MUTEX_INITIALIZER  0

#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_ERRORCHECK 1
#define PTHREAD_MUTEX_RECURSIVE  2
#define PTHREAD_MUTEX_DEFAULT    PTHREAD_MUTEX_NORMAL

struct timespec;

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
/* Timed lock: acquire the mutex, or fail with ETIMEDOUT once the
 * absolute CLOCK_REALTIME deadline `abstime` has passed. */
int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime);

/* Process-shared attribute.  Substrate mutexes are best-effort across
 * processes: setpshared accepts the flag so cross-process consumers (LMDB)
 * link and run, but a mutex placed in shared memory is not guaranteed to be
 * a robust inter-process lock. */
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED  1
int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type);
int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared);
int pthread_mutexattr_getpshared(const pthread_mutexattr_t *attr, int *pshared);

/* Priority-inheritance protocol + priority ceiling.  Substrate's MLFQ
 * scheduler does not implement PTHREAD_PRIO_INHERIT / PTHREAD_PRIO_PROTECT
 * mutexes, so these store and faithfully report the requested value in the
 * attribute object (PTHREAD_PRIO_NONE / ceiling 0 by default) but do not
 * change actual scheduling behaviour. */
#define PTHREAD_PRIO_NONE     0
#define PTHREAD_PRIO_INHERIT  1
#define PTHREAD_PRIO_PROTECT  2
int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int protocol);
int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *attr, int *protocol);
int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *attr, int prioceiling);
int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *attr, int *prioceiling);
/* MUTEX-level priority-ceiling accessors (distinct from the mutexattr ones
 * above): query, and atomically replace, the priority ceiling of a mutex that
 * was initialised with the PTHREAD_PRIO_PROTECT protocol.  A mutex of any other
 * protocol has no ceiling, so both fail with EINVAL.  setprioceiling reports the
 * previous ceiling through *old_ceiling. */
int pthread_mutex_getprioceiling(const pthread_mutex_t *mutex, int *prioceiling);
int pthread_mutex_setprioceiling(pthread_mutex_t *mutex, int prioceiling,
                                 int *old_ceiling);

/* ---------------- condition variables ----------------
 * Linux/glibc-style sequence-number cond_var built on top of
 * substrate's futex syscall.  Full POSIX surface — signal,
 * broadcast, wait, timedwait. */
/* A condattr just carries the clock id (CLOCK_REALTIME / CLOCK_MONOTONIC). */
typedef int pthread_condattr_t;
typedef struct {
    volatile int seq;       /* incremented on every signal/broadcast */
    int clock;              /* CLOCK_* the timedwait deadline is measured in */
} pthread_cond_t;

#define PTHREAD_COND_INITIALIZER  { 0, 0 }

struct timespec;

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime);

int pthread_condattr_init(pthread_condattr_t *attr);
int pthread_condattr_destroy(pthread_condattr_t *attr);
int pthread_condattr_setclock(pthread_condattr_t *attr, int clock_id);
int pthread_condattr_getclock(const pthread_condattr_t *attr, int *clock_id);

/* ---------------- thread attributes ----------------
 * Minimal: pthread_create uses a fixed stack and always-joinable threads,
 * so these record their arguments for source compatibility (GLib & co.
 * call them) -- stacksize is currently advisory. */
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
/* Contention scope.  Substrate threads are 1:1 with kernel threads, i.e.
 * always system scope; process scope is not supported. */
#define PTHREAD_SCOPE_SYSTEM    0
#define PTHREAD_SCOPE_PROCESS   1
/* inherit-scheduling attribute: whether a created thread takes its
 * scheduling parameters from the creating thread (INHERIT) or from the
 * attribute object (EXPLICIT).  Stored/reported in the attr; substrate's
 * scheduler treats both alike. */
#define PTHREAD_INHERIT_SCHED   0
#define PTHREAD_EXPLICIT_SCHED  1
/* Minimum thread-stack size (bytes).  POSIX also exposes this via <limits.h>;
 * both headers agree.  libpthread's per-thread stack is a fixed 64 KiB, so a
 * stacksize below this floor gains nothing.  The guard lets a translation unit
 * include both <pthread.h> and <limits.h> without a redefinition warning. */
#ifndef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN 65536
#endif
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);
int pthread_attr_setscope(pthread_attr_t *attr, int scope);
int pthread_attr_getscope(const pthread_attr_t *attr, int *scope);
/* POSIX thread scheduling attributes.  Stored on the attr and round-tripped;
 * substrate's scheduler does not yet run threads strictly by policy/priority
 * (see lib/pthread/pthread_extra.c). */
int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy);
int pthread_attr_getschedpolicy(const pthread_attr_t *attr, int *policy);
int pthread_attr_setschedparam(pthread_attr_t *attr,
                               const struct sched_param *param);
int pthread_attr_getschedparam(const pthread_attr_t *attr,
                               struct sched_param *param);
int pthread_attr_setinheritsched(pthread_attr_t *attr, int inheritsched);
int pthread_attr_getinheritsched(const pthread_attr_t *attr, int *inheritsched);

/* ---------------- read/write locks ----------------
 * Built on the mutex + condvar above.  Writer-preferring at uniform priority
 * (so writers don't starve); orders acquisition by SCHED_FIFO/RR scheduling
 * priority when threads run at distinct priorities (POSIX
 * _POSIX_THREAD_PRIORITY_SCHEDULING — see lib/pthread/pthread_extra.c).  The
 * body is public only because callers embed it by value. */
typedef int pthread_rwlockattr_t;
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    int readers;          /* >0 = N readers, -1 = one writer, 0 = free */
    int waiting_writers;
} pthread_rwlock_t;
#define PTHREAD_RWLOCK_INITIALIZER { 0, { 0, 0 }, 0, 0 }
int pthread_rwlock_init(pthread_rwlock_t *rw, const pthread_rwlockattr_t *attr);
int pthread_rwlock_destroy(pthread_rwlock_t *rw);
int pthread_rwlock_rdlock(pthread_rwlock_t *rw);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rw);
int pthread_rwlock_wrlock(pthread_rwlock_t *rw);
int pthread_rwlock_trywrlock(pthread_rwlock_t *rw);
int pthread_rwlock_unlock(pthread_rwlock_t *rw);
/* Timed lock acquisition: block until the lock is granted or the absolute
 * CLOCK_REALTIME deadline `abstime` lapses (ETIMEDOUT). */
int pthread_rwlock_timedrdlock(pthread_rwlock_t *rw, const struct timespec *abstime);
int pthread_rwlock_timedwrlock(pthread_rwlock_t *rw, const struct timespec *abstime);

/*
 * pthread_once — one-shot initializer.  Encoded as a single int
 * with three states: 0=not run, 1=in progress, 2=complete.  The
 * libpthread implementation spins on sched_yield() under contention.
 */
typedef int pthread_once_t;
#define PTHREAD_ONCE_INIT 0
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));

/*
 * pthread_key_t — thread-specific-data (TLS) key.  Keys are global; each
 * thread sees its own value per key.  Backed by a per-thread value array
 * (a __thread in libpthread, which pthread_create gives every thread).
 */
typedef unsigned int pthread_key_t;
#define PTHREAD_KEYS_MAX 128
int   pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int   pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int   pthread_setspecific(pthread_key_t key, const void *value);

/* ---------------- cancellation ----------------
 * Real deferred + asynchronous cancellation.  Per-thread cancel state
 * (PTHREAD_CANCEL_ENABLE/DISABLE) and type (DEFERRED/ASYNCHRONOUS) live in
 * TLS.  pthread_cancel() posts a cancel to the target thread; a deferred
 * cancel is acted upon at the next cancellation point (pthread_testcancel,
 * pthread_join, pthread_cond_wait/timedwait, sem_wait/timedwait, sleep via
 * the cancel signal), an asynchronous cancel is acted upon immediately.
 * Acting on a cancel runs the thread's cleanup stack and TSD destructors and
 * terminates the thread with PTHREAD_CANCELED. */
#define PTHREAD_CANCEL_ENABLE        0
#define PTHREAD_CANCEL_DISABLE       1
#define PTHREAD_CANCEL_DEFERRED      0
#define PTHREAD_CANCEL_ASYNCHRONOUS  1
#define PTHREAD_CANCELED             ((void *)-1)
int  pthread_cancel(pthread_t thread);
int  pthread_setcancelstate(int state, int *oldstate);
int  pthread_setcanceltype(int type, int *oldtype);
void pthread_testcancel(void);

/* ---------------- cleanup handlers ----------------
 * POSIX cancellation-cleanup stack.  pthread_cleanup_push records
 * (routine, arg) on a per-thread runtime stack; pthread_cleanup_pop removes
 * the top entry and, if execute is non-zero, runs it.  Handlers still on the
 * stack when the thread terminates (an explicit pthread_exit, a return from
 * the start routine, or a cancellation acted upon at a cancellation point)
 * are run automatically in last-in-first-out order, per POSIX.  push/pop must
 * be paired within the same lexical scope (the buffer is a block local). */
struct __pthread_cleanup {
    void (*__routine)(void *);
    void  *__arg;
    struct __pthread_cleanup *__next;
};
void __pthread_cleanup_push(struct __pthread_cleanup *__buf,
                            void (*__routine)(void *), void *__arg);
void __pthread_cleanup_pop(struct __pthread_cleanup *__buf, int __execute);
#define pthread_cleanup_push(routine, arg) \
    do { struct __pthread_cleanup __pthread_cleanup_buf; \
         __pthread_cleanup_push(&__pthread_cleanup_buf, (routine), (arg));
#define pthread_cleanup_pop(execute) \
         __pthread_cleanup_pop(&__pthread_cleanup_buf, (execute)); } while (0)

/* ---------------- barriers ----------------
 * A rendezvous barrier for `count` threads, built on the mutex + condvar
 * above.  The (count)th thread to arrive releases the rest and is the one
 * that gets PTHREAD_BARRIER_SERIAL_THREAD; every other waiter gets 0.  The
 * barrier auto-recycles for the next round.  The body is public only
 * because callers embed it by value. */
typedef struct { int pshared; } pthread_barrierattr_t;
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    unsigned int    count;    /* number of threads that must rendezvous */
    unsigned int    left;     /* still-to-arrive in the current round   */
    unsigned int    cycle;    /* round generation, for auto-reuse        */
} pthread_barrier_t;

#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned int count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_wait(pthread_barrier_t *barrier);

int pthread_barrierattr_init(pthread_barrierattr_t *attr);
int pthread_barrierattr_destroy(pthread_barrierattr_t *attr);
int pthread_barrierattr_getpshared(const pthread_barrierattr_t *attr, int *pshared);
int pthread_barrierattr_setpshared(pthread_barrierattr_t *attr, int pshared);

/* ---------------- spin locks ----------------
 * A plain atomic test-and-set spinlock.  The lock word is embedded by
 * value, so a lock placed in shared memory works across processes
 * (PTHREAD_PROCESS_SHARED); the contended waiter yields the CPU so a
 * single-CPU holder can make progress. */
typedef volatile int pthread_spinlock_t;
int pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int pthread_spin_destroy(pthread_spinlock_t *lock);
int pthread_spin_lock(pthread_spinlock_t *lock);
int pthread_spin_trylock(pthread_spinlock_t *lock);
int pthread_spin_unlock(pthread_spinlock_t *lock);

/* ---------------- fork handlers ----------------
 * Register prepare/parent/child handlers run around fork(3): prepare
 * before the fork (in reverse registration order), parent afterwards in
 * the parent, child afterwards in the child (both in registration order).
 * libc's fork() invokes these through weak hooks that libpthread exports. */
int pthread_atfork(void (*prepare)(void), void (*parent)(void),
                   void (*child)(void));

#ifdef __cplusplus
}
#endif

#endif
