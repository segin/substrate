#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <stddef.h>
#include <signal.h>     /* sigset_t for pthread_sigmask, pthread_kill */

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

int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type);

/* ---------------- condition variables ----------------
 * Linux/glibc-style sequence-number cond_var built on top of
 * substrate's futex syscall.  Full POSIX surface — signal,
 * broadcast, wait, timedwait. */
typedef int pthread_condattr_t;
typedef struct {
    volatile int seq;       /* incremented on every signal/broadcast */
} pthread_cond_t;

#define PTHREAD_COND_INITIALIZER  { 0 }

struct timespec;

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime);

/*
 * pthread_once — one-shot initializer.  Encoded as a single int
 * with three states: 0=not run, 1=in progress, 2=complete.  The
 * libpthread implementation spins on sched_yield() under contention.
 */
typedef int pthread_once_t;
#define PTHREAD_ONCE_INIT 0
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));

#ifdef __cplusplus
}
#endif

#endif
