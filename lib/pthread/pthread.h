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

/* Current-thread identity.  Returns the kernel-assigned TID. */
pthread_t pthread_self(void);

/* Send signal `sig` to thread `thread` — POSIX thread-directed
 * signal delivery via SYS_THR_KILL.  `sig == 0` probes whether the
 * thread exists (returns 0/ESRCH without delivering). */
int pthread_kill(pthread_t thread, int sig);

/* Per-thread signal mask.  Wraps the same kernel sigprocmask used
 * by single-threaded process callers — substrate stores sig_mask
 * per-thread, so this is mostly a renamed sigprocmask. */
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);

typedef int pthread_mutex_t;
int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutex_destroy(pthread_mutex_t *mutex);

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

#ifdef __cplusplus
}
#endif

#endif
