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

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);

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
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);
int pthread_attr_setscope(pthread_attr_t *attr, int scope);
int pthread_attr_getscope(const pthread_attr_t *attr, int *scope);

/* ---------------- read/write locks ----------------
 * Built on the mutex + condvar above; writer-preferring so writers don't
 * starve.  The body is public only because callers embed it by value. */
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

/* ---------------- cancellation (API surface only) ----------------
 * substrate has no cancellation runtime (see the cleanup-handler note
 * below).  These provide the POSIX entry points with no-op semantics so
 * threaded software (Qt/TQt and the rest of the desktop stack) links and
 * runs.  pthread_cancel() does NOT stop the target thread, and
 * pthread_testcancel() never acts on a pending cancel — there is none. */
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
 * POSIX cancellation-cleanup stack.  substrate's libpthread has no
 * cancellation runtime, so these are scope macros: pthread_cleanup_push
 * opens a block recording (routine, arg); pthread_cleanup_pop(execute)
 * closes it and, if execute is non-zero, runs routine(arg).  This gives
 * the standard "run on pop" behaviour that the overwhelming majority of
 * callers rely on (cairo, ksh, ...).  Cleanup triggered by an async
 * pthread_cancel() is not delivered (there is no cancel machinery to
 * deliver it), matching substrate's current cancellation support. */
#define pthread_cleanup_push(routine, arg)     do {         void (*__pthread_cleanup_routine)(void *) = (routine);         void *__pthread_cleanup_arg = (arg);         {
#define pthread_cleanup_pop(execute)         }         if (execute) __pthread_cleanup_routine(__pthread_cleanup_arg);     } while (0)

#ifdef __cplusplus
}
#endif

#endif
