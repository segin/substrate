/*
 * perso_ipc_shm.h — SysV shared-memory ABI shim entry points for the foreign
 * personalities (implemented in perso_ipc_shm.c, over the kernel core in
 * sys/kern/ipc_shm.c).
 */
#ifndef _PERSO_IPC_SHM_H
#define _PERSO_IPC_SHM_H

#include <sys/types.h>
#include <stdint.h>

/* Linux i386 ipc(2) multiplexer (syscall 117): SHMAT/SHMDT/SHMGET/SHMCTL. */
int linux_sys_ipc_shm(unsigned call, int first, int second, int third,
                      uint32_t ptr, int32_t fifth);

/* FreeBSD i386 direct syscalls (shmat=228, shmdt=230, shmget=231, shmctl=512).
 * shmat returns the user VA (or a negative-errno pointer the kernel reuses for
 * the libc errno bridge). */
int   freebsd_sys_shmget(key_t key, size_t size, int shmflg);
void *freebsd_sys_shmat(int shmid, uint32_t shmaddr, int shmflg);
int   freebsd_sys_shmdt(uint32_t shmaddr);
int   freebsd_sys_shmctl(int shmid, int cmd, uint32_t buf);

/* NetBSD i386 direct syscalls (shmat=228, shmdt=230, shmget=231,
 * ____shmctl50=512).  Like FreeBSD, shmat returns the user VA directly (or a
 * negative-errno pointer reused by the libc errno bridge).  ____shmctl50 uses
 * the time_t-64 shmid_ds layout. */
int   netbsd_sys_shmget(key_t key, size_t size, int shmflg);
void *netbsd_sys_shmat(int shmid, uint32_t shmaddr, int shmflg);
int   netbsd_sys_shmdt(uint32_t shmaddr);
int   netbsd_sys_shmctl(int shmid, int cmd, uint32_t buf);

#endif /* _PERSO_IPC_SHM_H */
