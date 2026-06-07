/*
 * <sys/sem.h> (kernel) — System V semaphore ABI + kernel entry points.
 *
 * The sembuf / semid_ds / semun layouts and the GETxxx/SETxxx command numbers
 * MUST match the userspace include/sys/sem.h: they cross the syscall boundary.
 * The native personality calls sys_sem{get,op,ctl} directly; the Linux / BSD
 * personalities marshal their own ABI into kern_sem{get,op,ctl}.
 */
#ifndef _SYS_SEM_H
#define _SYS_SEM_H

#include <sys/types.h>
#include <sys/ipc.h>

/* semop(2) flag (in sembuf.sem_flg). */
#define SEM_UNDO  0x1000

/* semctl(2) commands (in addition to IPC_RMID/IPC_SET/IPC_STAT from <ipc.h>). */
#define GETPID    11      /* return sempid of semnum */
#define GETVAL    12      /* return semval of semnum */
#define GETALL    13      /* fetch all semval into arg.array */
#define GETNCNT   14      /* return semncnt of semnum */
#define GETZCNT   15      /* return semzcnt of semnum */
#define SETVAL    16      /* set semval of semnum from arg.val */
#define SETALL    17      /* set all semval from arg.array */

struct sembuf {
    unsigned short sem_num;   /* semaphore index in set */
    short          sem_op;    /* operation */
    short          sem_flg;   /* IPC_NOWAIT / SEM_UNDO */
};

struct semid_ds {
    struct ipc_perm sem_perm; /* ownership / permissions */
    time_t          sem_otime;/* last semop time */
    time_t          sem_ctime;/* last change time */
    unsigned long   sem_nsems;/* number of semaphores in set */
};

union semun {
    int              val;     /* SETVAL */
    struct semid_ds *buf;     /* IPC_STAT / IPC_SET */
    unsigned short  *array;   /* GETALL / SETALL */
};

/* Implementation limits. */
#define SEMMNI    128         /* max semaphore sets system-wide */
#define SEMMSL    250         /* max semaphores per set */
#define SEMOPM    32          /* max ops per semop() call */
#define SEMVMX    32767       /* max semaphore value */

/* Native-ABI syscall entry points (registered in perso_native).  These do the
 * native copyin/copyout and then call the personality-agnostic core below. */
int sys_semget(key_t key, int nsems, int semflg);
int sys_semop(int semid, struct sembuf *sops, size_t nsops);
int sys_semctl(int semid, int semnum, int cmd, uintptr_t arg);

/*
 * Personality-agnostic core.  All operate on KERNEL memory (the caller — the
 * native sys_* wrapper or a Linux/BSD personality shim — handles copyin/copyout
 * in its own ABI), so the core makes no assumption about the userspace
 * semid_ds / semun layout.  sembuf is byte-identical across our target OSes, so
 * kern_semop takes a kernel sembuf array directly.  Each returns 0/-errno
 * unless noted.
 */
int kern_semget(key_t key, int nsems, int semflg);
int kern_semop(int semid, const struct sembuf *ksops, size_t nsops);

int kern_sem_rmid(int semid);
int kern_sem_setperm(int semid, uid_t uid, gid_t gid, mode_t mode);
int kern_sem_stat(int semid, struct semid_ds *out);   /* native layout, kernel buf */
int kern_sem_getval(int semid, int semnum);           /* returns value (>=0) or -errno */
int kern_sem_setval(int semid, int semnum, int val);
int kern_sem_getpid(int semid, int semnum);           /* returns pid or -errno */
int kern_sem_getncnt(int semid, int semnum);          /* waiters-for-increase or -errno */
int kern_sem_getzcnt(int semid, int semnum);          /* waiters-for-zero or -errno */
int kern_sem_getall(int semid, unsigned short *kbuf, size_t kbuf_n); /* returns nsems or -errno */
int kern_sem_setall(int semid, const unsigned short *kbuf, size_t n);
int kern_sem_nsems(int semid);                        /* returns nsems or -errno */

/* SEM_UNDO cleanup, called from proc_exit(). */
void sem_proc_cleanup(int pid);

void sem_init(void);

#endif /* _SYS_SEM_H */
