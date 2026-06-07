/*
 * perso_ipc_sem.h — SysV semaphore ABI shim entry points for the foreign
 * personalities (implemented in perso_ipc_sem.c, over the kernel core in
 * sys/kern/ipc_sem.c).
 */
#ifndef _PERSO_IPC_SEM_H
#define _PERSO_IPC_SEM_H

#include <sys/types.h>
#include <stdint.h>

/* Linux i386 ipc(2) multiplexer (syscall 117). */
int linux_sys_ipc(unsigned call, int first, int second, int third,
                  uint32_t ptr, int32_t fifth);

/* FreeBSD i386 direct syscalls (221/222/510). */
int freebsd_sys_semget(key_t key, int nsems, int semflg);
int freebsd_sys_semop(int semid, uint32_t sops, unsigned nsops);
int freebsd_sys___semctl(int semid, int semnum, int cmd, uint32_t arg);

/* NetBSD i386 direct syscalls (221/222/442 ____semctl50). */
int netbsd_sys_semget(key_t key, int nsems, int semflg);
int netbsd_sys_semop(int semid, uint32_t sops, unsigned nsops);
int netbsd_sys_semctl(int semid, int semnum, int cmd, uint32_t uptr);

#endif /* _PERSO_IPC_SEM_H */
