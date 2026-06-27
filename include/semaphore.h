/*
 * <semaphore.h> — POSIX.1-2017 counting semaphores.
 *
 * Currently a header-only stub: substrate's in-kernel sleepq-backed
 * semaphore exists and is exposed to userland via SYS_SEM_*, but the
 * libc wrappers around sem_init / sem_post / sem_wait haven't been
 * written yet.  This header lets sources that #include <semaphore.h>
 * compile so we can spot which packages actually exercise the API
 * (and need the wrappers landed) versus which only conditionally
 * touch it.
 */
#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque to userland — substrate sticks the underlying kernel sem
 * id at the head and lets the rest be padding.  Match glibc's size
 * (16 bytes on 32-bit) so callers that allocate sem_t inline aren't
 * surprised. */
typedef struct {
    int    __sem_id;
    int    __sem_pad[3];
} sem_t;

#define SEM_FAILED ((sem_t *)0)
#define SEM_VALUE_MAX 2147483647

int     sem_init(sem_t *sem, int pshared, unsigned int value);
int     sem_destroy(sem_t *sem);
sem_t  *sem_open(const char *name, int oflag, ...);
int     sem_close(sem_t *sem);
int     sem_unlink(const char *name);
int     sem_wait(sem_t *sem);
int     sem_trywait(sem_t *sem);
int     sem_timedwait(sem_t *restrict sem,
                      const struct timespec *restrict abs_timeout);
int     sem_post(sem_t *sem);
int     sem_getvalue(sem_t *restrict sem, int *restrict sval);

#ifdef __cplusplus
}
#endif

#endif /* _SEMAPHORE_H */
