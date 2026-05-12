/*
 * <sys/sem.h> — System V semaphore API.
 *
 * Substrate has no SysV-IPC implementation yet; this header exists so
 * autoconf probes (libstdc++ configures, etc.) detect a usable shape
 * and proceed.  Calls return -1 with errno=ENOSYS.
 */
#ifndef _SYS_SEM_H
#define _SYS_SEM_H

#include <sys/types.h>

#define SEM_UNDO  0x1000

struct sembuf {
    unsigned short sem_num;
    short          sem_op;
    short          sem_flg;
};

struct semid_ds {
    int    sem_perm;     /* (placeholder) */
    time_t sem_otime;
    time_t sem_ctime;
    unsigned long sem_nsems;
};

union semun {
    int              val;
    struct semid_ds *buf;
    unsigned short  *array;
};

int semget(key_t key, int nsems, int semflg);
int semctl(int semid, int semnum, int cmd, ...);
int semop(int semid, struct sembuf *sops, size_t nsops);

#endif
