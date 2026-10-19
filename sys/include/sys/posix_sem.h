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
#include <vm/vm_map.h>

/* Implementation limits. */
#define KSEM_MAX        128            /* max ksem objects system-wide */
#define KSEM_NAME_MAX   64             /* max name incl. leading '/' and NUL */
#define KSEM_VALUE_MAX  0x7fffffff     /* == SEM_VALUE_MAX */

/*
 * Native-ABI syscall entry points (registered in perso_native).  Each does the
 * copyin / copyout and then calls the personality-agnostic core below.  The
 * open family returns a ksem id (>= 0) on success; the rest return 0.  All
 * return -errno on failure.  `usem` is the caller's sem_t (NULL for none):
 * a semaphore made by ksem_init may be used by any process that maps the
 * memory it was made in.
 */
int sys_ksem_open(const char *uname, int oflag, mode_t mode, unsigned int value);
int sys_ksem_init(const void *usem, unsigned int value);
int sys_ksem_close(int id);
int sys_ksem_unlink(const char *uname);
int sys_ksem_wait(int id, const void *usem);
int sys_ksem_trywait(int id, const void *usem);
int sys_ksem_timedwait(int id, const struct timespec *uabstime,
                       const void *usem);
int sys_ksem_post(int id, const void *usem);
int sys_ksem_getvalue(int id, int *usval, const void *usem);

/*
 * Personality-agnostic core — operates on KERNEL memory only.  `kname` is a
 * NUL-terminated kernel buffer beginning with '/'; a NULL name creates an
 * anonymous (unnamed) ksem.  kern_ksem_init makes the one process-shared
 * sem_init uses, bound to the memory `key` names (and takes over its
 * reference).  `kabstime` is an absolute CLOCK_REALTIME deadline in kernel
 * memory (NULL = wait forever).  `key` on the operations is the memory the
 * caller's sem_t is in, or NULL.  Each returns 0 / ksem id or -errno.
 */
int kern_ksem_open(const char *kname, int oflag, mode_t mode, unsigned int value);
int kern_ksem_init(struct vm_shared_key *key, unsigned int value);
int kern_ksem_close(int id);
int kern_ksem_unlink(const char *kname);
int kern_ksem_wait(int id, const struct vm_shared_key *key);
int kern_ksem_trywait(int id, const struct vm_shared_key *key);
int kern_ksem_timedwait(int id, const struct timespec *kabstime,
                        const struct vm_shared_key *key);
int kern_ksem_post(int id, const struct vm_shared_key *key);
int kern_ksem_getvalue(int id, int *ksval, const struct vm_shared_key *key);

/* Physical memory [pa, pa + len) is being freed: unbind sem_init semaphores
 * keyed into it, so its next owner cannot match them. */
void ksem_unbind_memory(uintptr_t pa, size_t len);

/* Drop this pid's still-open descriptors at process exit. */
void ksem_proc_cleanup(int pid);
/* Give a forked child the parent's open semaphores. */
int  ksem_proc_fork(int parent_pid, int child_pid);

void ksem_init(void);

#endif /* _SYS_POSIX_SEM_H */
