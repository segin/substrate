/* Thread-identity + per-thread signal delivery: pthread_self,
 * pthread_kill, pthread_sigmask.  Other pthread functions live
 * in pthread_create.c, pthread_mutex.c, pthread_cond.c. */

#include "pthread.h"
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/syscall.h>

pthread_t pthread_self(void) {
    return (pthread_t)syscall(SYS_THR_SELF);
}

int pthread_kill(pthread_t thread, int sig) {
    long rc = syscall(SYS_THR_KILL, (long)thread, sig);
    return rc < 0 ? (int)-rc : 0;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset) {
    /* Substrate's sig_mask is per-thread (thread_t.sig_mask), so
     * sigprocmask already does the per-thread thing in this kernel.
     * pthread_sigmask is just a renamed wrapper for portability. */
    extern int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
    return sigprocmask(how, set, oldset) == 0 ? 0 : errno;
}
