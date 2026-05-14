/*
 * <sys/shm.h> — System V shared memory (substrate stubs).
 *
 * Every entry returns -1 with errno = ENOSYS.  POSIX shared memory
 * via shm_open(3) is what substrate actually implements (backed by
 * shmfs at /dev/shm); System V shm is here only for source-compat.
 */

#ifndef _SYS_SHM_H
#define _SYS_SHM_H

#include <sys/ipc.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHM_RDONLY 010000
#define SHM_RND    020000
#define SHM_REMAP  040000
#define SHM_EXEC   0100000

#define SHM_LOCK   11
#define SHM_UNLOCK 12

typedef unsigned long shmatt_t;

struct shmid_ds {
    struct ipc_perm shm_perm;
    size_t   shm_segsz;
    time_t   shm_atime;
    time_t   shm_dtime;
    time_t   shm_ctime;
    pid_t    shm_cpid;
    pid_t    shm_lpid;
    shmatt_t shm_nattch;
};

int   shmget(key_t key, size_t size, int shmflg);
void *shmat (int shmid, const void *shmaddr, int shmflg);
int   shmdt (const void *shmaddr);
int   shmctl(int shmid, int cmd, struct shmid_ds *buf);

#ifdef __cplusplus
}
#endif
#endif
