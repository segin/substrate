/*
 * <semaphore.h> — POSIX.1-2017 counting semaphores.
 *
 * Substrate provides two flavours behind one sem_t:
 *
 *   - Process-LOCAL unnamed semaphores (sem_init with pshared == 0) are a
 *     single count word blocked on with SYS_FUTEX — the fast path, the same
 *     primitive libpthread's mutex/cond use.
 *
 *   - Named semaphores (sem_open/sem_close/sem_unlink) and process-SHARED
 *     unnamed semaphores (sem_init with pshared != 0) are backed by a real
 *     kernel object (a "ksem", see sys/kern/posix_sem.c) reached through the
 *     SYS_KSEM_* syscalls.  Substrate's futex is keyed by a per-process
 *     virtual address and never crosses an address space, so a genuinely
 *     shared semaphore has to live in the kernel.
 *
 * The __sem_kind tag selects which path sem_wait/sem_post/... take.
 */
#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* __sem_kind values (internal). */
#define __SEM_LOCAL   0     /* process-local futex sem (sem_init, pshared==0) */
#define __SEM_KANON   1     /* anonymous kernel ksem (sem_init, pshared!=0)   */
#define __SEM_KNAMED  2     /* named kernel ksem (sem_open), heap-allocated   */

/* Opaque to userland.  For a local sem, __sem_id holds the count word the
 * futex blocks on; for a kernel-backed sem, __sem_id is the ksem descriptor.
 * Sized to match glibc's 16 bytes on 32-bit so callers that embed sem_t inline
 * aren't surprised. */
typedef struct {
    int    __sem_kind;
    int    __sem_id;
    int    __sem_pad[2];
} sem_t;

#define SEM_FAILED    ((sem_t *)0)
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
