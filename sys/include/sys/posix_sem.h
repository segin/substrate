/*
 * <sys/posix_sem.h> (kernel) — POSIX named / kernel semaphore objects (ksem).
 *
 * Backs sem_open(3) / sem_close(3) / sem_unlink(3) and process-shared
 * sem_init(3) with a real, system-wide kernel object (mirroring FreeBSD's
 * ksem model), so they synchronise correctly across unrelated processes.
 *
 * Substrate's futex is keyed by a per-process VIRTUAL address and scoped to the
 * calling process (see sys/kern/futex.c — a physical key is not stable, so
 * cross-process MAP_SHARED futexes are deliberately not supported).  A
 * process-shared semaphore therefore CANNOT be a futex word in shared memory;
 * it must live in the kernel.  That is exactly what this subsystem provides.
 *
 * Names live in a small in-kernel namespace (each begins with '/').  The object
 * persists until it is unlinked AND every open descriptor is closed (POSIX
 * persistence semantics), just like FreeBSD's ksem.
 *
 * The core (kern_ksem_*) works on KERNEL memory only and makes no assumption
 * about any userspace ABI, so the native personality (and, in future, foreign
 * personalities) can marshal their own argument layout on top of it.
 */
#ifndef _SYS_POSIX_SEM_H
#define _SYS_POSIX_SEM_H

#include <sys/types.h>
#include <sys/time.h>

/* Implementation limits. */
#define KSEM_MAX        128            /* max ksem objects system-wide */
#define KSEM_NAME_MAX   64             /* max name incl. leading '/' and NUL */
#define KSEM_VALUE_MAX  0x7fffffff     /* == SEM_VALUE_MAX */

/*
 * Native-ABI syscall entry points (registered in perso_native).  Each does the
 * copyin / copyout and then calls the personality-agnostic core below.  The
 * open family returns a ksem id (>= 0) on success; the rest return 0.  All
 * return -errno on failure.
 */
int sys_ksem_open(const char *uname, int oflag, mode_t mode, unsigned int value);
int sys_ksem_close(int id);
int sys_ksem_unlink(const char *uname);
int sys_ksem_wait(int id);
int sys_ksem_trywait(int id);
int sys_ksem_timedwait(int id, const struct timespec *uabstime);
int sys_ksem_post(int id);
int sys_ksem_getvalue(int id, int *usval);

/*
 * Personality-agnostic core — operates on KERNEL memory only.  `kname` is a
 * NUL-terminated kernel buffer beginning with '/'; a NULL name creates an
 * anonymous (unnamed) ksem, which is how process-shared sem_init is backed.
 * `kabstime` is an absolute CLOCK_REALTIME deadline in kernel memory
 * (NULL = wait forever).  Each returns 0 / ksem id or -errno.
 */
int kern_ksem_open(const char *kname, int oflag, mode_t mode, unsigned int value);
int kern_ksem_close(int id);
int kern_ksem_unlink(const char *kname);
int kern_ksem_wait(int id);
int kern_ksem_trywait(int id);
int kern_ksem_timedwait(int id, const struct timespec *kabstime);
int kern_ksem_post(int id);
int kern_ksem_getvalue(int id, int *ksval);

/* Drop this pid's still-open descriptors at process exit. */
void ksem_proc_cleanup(int pid);

void ksem_init(void);

#endif /* _SYS_POSIX_SEM_H */
