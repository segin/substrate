/*
 * <sys/sem.h> — System V semaphore API.
 *
 * Backed by substrate's in-kernel SysV semaphore subsystem (semget/semop/
 * semctl at syscalls 402/403/404).  The sembuf / semid_ds / semun layouts and
 * the GETxxx/SETxxx command numbers match the kernel's <sys/sem.h>.
 */
#ifndef _SYS_SEM_H
#define _SYS_SEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/ipc.h>

/* semop(2) flag (in sembuf.sem_flg). */
#define SEM_UNDO  0x1000

/* semctl(2) commands (in addition to IPC_RMID/IPC_SET/IPC_STAT from <ipc.h>). */
#define GETPID    11
#define GETVAL    12
#define GETALL    13
#define GETNCNT   14
#define GETZCNT   15
#define SETVAL    16
#define SETALL    17

struct sembuf {
    unsigned short sem_num;
    short          sem_op;
    short          sem_flg;
};

struct semid_ds {
    struct ipc_perm sem_perm;
    time_t          sem_otime;
    time_t          sem_ctime;
    unsigned long   sem_nsems;
};

union semun {
    int              val;
    struct semid_ds *buf;
    unsigned short  *array;
};

/* Implementation limits. */
#define SEMMNI    128
#define SEMMSL    250
#define SEMOPM    32
#define SEMVMX    32767

int semget(key_t key, int nsems, int semflg);
int semctl(int semid, int semnum, int cmd, ...);
int semop(int semid, struct sembuf *sops, size_t nsops);

#ifdef __cplusplus
}
#endif
#endif
