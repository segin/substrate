/* Condition variables — sequence-number, futex-backed.  Other pthread
 * functions live in pthread_create.c, pthread_mutex.c, pthread_sig.c.
 *
 * Standard Linux glibc shape:
 *
 *   wait:   record seq under mutex, unlock mutex, FUTEX_WAIT on
 *           &cond->seq with the recorded value (kernel atomically
 *           parks iff seq still == recorded), then re-lock mutex.
 *   signal: bump seq, FUTEX_WAKE 1.
 *   broadcast: bump seq, FUTEX_WAKE INT_MAX.
 *
 * Per POSIX, callers must re-check the predicate after wait — FUTEX_WAIT
 * can return early on a signal.  We don't loop here. */

#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <sys/futex.h>
#include <sys/syscall.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    cond->seq = 0;
    /* A condattr is just the clock id; default CLOCK_REALTIME (0). */
    cond->clock = attr ? *attr : 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
    __atomic_add_fetch(&cond->seq, 1, __ATOMIC_SEQ_CST);
    syscall(SYS_FUTEX, (long)&cond->seq, FUTEX_WAKE, 1, 0, 0, 0);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    __atomic_add_fetch(&cond->seq, 1, __ATOMIC_SEQ_CST);
    /* INT_MAX waiters — kernel iterates the futex wait queue. */
    syscall(SYS_FUTEX, (long)&cond->seq, FUTEX_WAKE, 0x7fffffff, 0, 0, 0);
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    int old_seq = __atomic_load_n(&cond->seq, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(mutex);
    /* Atomic check-and-park inside the kernel: FUTEX_WAIT only
     * parks if *addr still == old_seq.  If another thread already
     * signaled (incrementing seq) between our load and the syscall,
     * the wait returns immediately with EAGAIN and we re-acquire
     * the mutex with the new state visible — which is exactly what
     * cond_wait must guarantee to avoid lost wakeups. */
    syscall(SYS_FUTEX, (long)&cond->seq, FUTEX_WAIT, old_seq, 0, 0, 0);
    pthread_mutex_lock(mutex);
    return 0;
}

/* pthread_cond_timedwait — same as cond_wait but with an absolute
 * CLOCK_REALTIME deadline.  Returns 0 on signal, ETIMEDOUT on
 * deadline elapsed.  Substrate's FUTEX_WAIT takes a RELATIVE
 * timespec, so we convert abstime - now and pass that. */
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    if (!abstime) return pthread_cond_wait(cond, mutex);

    int old_seq = __atomic_load_n(&cond->seq, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(mutex);

    long rc = 0;
    for (;;) {
        struct timespec now, rel;
        /* Measure against the clock the cond was created with (GLib's GCond
         * uses CLOCK_MONOTONIC via pthread_condattr_setclock). */
        if (clock_gettime(cond->clock, &now) != 0) {
            rc = -EINVAL;
            break;
        }
        /* relative = abstime - now */
        rel.tv_sec  = abstime->tv_sec  - now.tv_sec;
        rel.tv_nsec = abstime->tv_nsec - now.tv_nsec;
        if (rel.tv_nsec < 0) {
            rel.tv_sec  -= 1;
            rel.tv_nsec += 1000000000L;
        }
        if (rel.tv_sec < 0 || (rel.tv_sec == 0 && rel.tv_nsec <= 0)) {
            rc = -ETIMEDOUT;
            break;
        }

        rc = syscall(SYS_FUTEX, (long)&cond->seq, FUTEX_WAIT, old_seq,
                     (long)&rel, 0, 0);
        if (rc == 0)                 break;   /* normal wake */
        if (rc == -EAGAIN)            { rc = 0; break; }   /* seq moved — proceed */
        if (rc == -ETIMEDOUT)         break;
        if (rc == -EINTR)             continue; /* spurious; retry with fresh delta */
        break;
    }

    pthread_mutex_lock(mutex);
    if (rc == -ETIMEDOUT) return ETIMEDOUT;
    if (rc < 0)           return (int)-rc;
    return 0;
}
