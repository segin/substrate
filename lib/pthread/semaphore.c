/*
 * semaphore.c — POSIX.1-2017 unnamed counting semaphores.
 *
 * The count lives in sem_t's first word; waiters block in the kernel via
 * SYS_FUTEX (FUTEX_WAIT) and sem_post wakes them (FUTEX_WAKE), the same
 * primitive libpthread's mutex/cond use.  pshared works for free: the futex
 * is taken on the count word itself, so a sem_t in shared memory synchronises
 * across processes.  sem_open/close/unlink (named semaphores) are not provided.
 */
#include <semaphore.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <sys/syscall.h>

long syscall(long number, ...);

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

#ifndef SEM_VALUE_MAX
#define SEM_VALUE_MAX 2147483647
#endif

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
    (void)pshared;
    if (sem == NULL || value > SEM_VALUE_MAX) {
        errno = EINVAL;
        return -1;
    }
    sem->__sem_id = (int)value;
    sem->__sem_pad[0] = sem->__sem_pad[1] = sem->__sem_pad[2] = 0;
    return 0;
}

int sem_destroy(sem_t *sem)
{
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int sem_post(sem_t *sem)
{
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }
    __sync_fetch_and_add(&sem->__sem_id, 1);
    syscall(SYS_FUTEX, (long)&sem->__sem_id, FUTEX_WAKE, 1, 0, 0, 0);
    return 0;
}

int sem_trywait(sem_t *sem)
{
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        int v = sem->__sem_id;
        if (v <= 0) {
            errno = EAGAIN;
            return -1;
        }
        if (__sync_bool_compare_and_swap(&sem->__sem_id, v, v - 1)) {
            return 0;
        }
    }
}

int sem_wait(sem_t *sem)
{
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        int v = sem->__sem_id;
        if (v > 0) {
            if (__sync_bool_compare_and_swap(&sem->__sem_id, v, v - 1)) {
                return 0;
            }
            continue;
        }
        /* count is 0: block until a post raises it (re-checked on wake) */
        syscall(SYS_FUTEX, (long)&sem->__sem_id, FUTEX_WAIT, 0, 0, 0, 0);
    }
}

int sem_timedwait(sem_t *restrict sem, const struct timespec *restrict abs_timeout)
{
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        int v = sem->__sem_id;
        if (v > 0) {
            if (__sync_bool_compare_and_swap(&sem->__sem_id, v, v - 1)) {
                return 0;
            }
            continue;
        }
        if (abs_timeout != NULL) {
            struct timespec now, rel;
            if (clock_gettime(CLOCK_REALTIME, &now) == 0) {
                rel.tv_sec = abs_timeout->tv_sec - now.tv_sec;
                rel.tv_nsec = abs_timeout->tv_nsec - now.tv_nsec;
                if (rel.tv_nsec < 0) { rel.tv_sec--; rel.tv_nsec += 1000000000L; }
                if (rel.tv_sec < 0) { errno = ETIMEDOUT; return -1; }
                syscall(SYS_FUTEX, (long)&sem->__sem_id, FUTEX_WAIT, 0,
                        (long)&rel, 0, 0);
                continue;
            }
        }
        syscall(SYS_FUTEX, (long)&sem->__sem_id, FUTEX_WAIT, 0, 0, 0, 0);
    }
}

int sem_getvalue(sem_t *restrict sem, int *restrict sval)
{
    if (sem == NULL || sval == NULL) {
        errno = EINVAL;
        return -1;
    }
    int v = sem->__sem_id;
    *sval = v < 0 ? 0 : v;
    return 0;
}
