/*
 * <sys/shm.h> — System V shared memory.
 *
 * Substrate implements System V shared memory natively in the kernel
 * (sys/kern/ipc_shm.c, native syscalls SYS_SHMGET/SHMAT/SHMDT/SHMCTL).
 * A segment owns shared physical pages; shmat() maps them into the
 * caller's address space so writes are visible across every attacher.
 */

#ifndef _SYS_SHM_H
#define _SYS_SHM_H

#include <sys/ipc.h>
#include <sys/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* shmat(2) flags (in shmflg). */
#define SHM_RDONLY 010000     /* attach read-only */
#define SHM_RND    020000     /* round shmaddr down to SHMLBA */
#define SHM_REMAP  040000     /* take over an existing mapping */
#define SHM_EXEC   0100000    /* attach executable */

/* shmctl(2) commands (in addition to IPC_RMID/IPC_SET/IPC_STAT from <ipc.h>). */
#define SHM_LOCK   11         /* lock segment in memory */
#define SHM_UNLOCK 12         /* unlock segment */
#define SHM_STAT   13         /* stat by slot index (ipcs) */
#define SHM_INFO   14         /* system-wide info (ipcs) */

/* shm_perm.mode flags reported in IPC_STAT. */
#define SHM_DEST    01000     /* segment will be destroyed on last detach */
#define SHM_LOCKED  02000     /* segment locked in memory */

/* Attach boundary: shmaddr with SHM_RND is rounded down to a multiple. */
#define SHMLBA     4096

/* Implementation limits. */
#define SHMMIN     1                  /* min segment size (bytes) */
#define SHMMAX     (8 * 1024 * 1024)  /* max segment size (bytes) */
#define SHMMNI     128                /* max segments system-wide */
#define SHMSEG     SHMMNI             /* max attaches per process */

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
