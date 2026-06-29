/*
 * <sys/shm.h> (kernel) — System V shared memory ABI + kernel entry points.
 *
 * The shmid_ds layout and the SHM_* command numbers MUST match the userspace
 * include/sys/shm.h: they cross the syscall boundary.  The native personality
 * calls sys_shm{get,at,dt,ctl} directly; the Linux / BSD personalities marshal
 * their own ABI into kern_shm{get,...}.
 *
 * A segment owns a run of physically-contiguous, kernel-direct-mapped pages.
 * shmat maps those pages into the calling process's address space as a SHARED
 * (not copy-on-write) device-pager region, so a write in one attacher is
 * visible in every other.  The backing pages persist until the segment is
 * IPC_RMID'd and the last attach is dropped (the SysV "mark-for-deletion"
 * semantics).
 */
#ifndef _SYS_SHM_H
#define _SYS_SHM_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <stddef.h>

/* shmat(2) flags (in shmflg). */
#define SHM_RDONLY 010000     /* attach read-only */
#define SHM_RND    020000     /* round shmaddr down to SHMLBA */
#define SHM_REMAP  040000     /* take over an existing mapping (unsupported) */
#define SHM_EXEC   0100000    /* attach executable */

/* shmctl(2) commands (in addition to IPC_RMID/IPC_SET/IPC_STAT from <ipc.h>). */
#define SHM_LOCK   11         /* lock segment in memory */
#define SHM_UNLOCK 12         /* unlock segment */
#define SHM_STAT   13         /* stat by slot index (ipcs) */
#define SHM_INFO   14         /* system-wide info (ipcs) */

/* shmctl shm_perm.mode flags reported in IPC_STAT. */
#define SHM_DEST    01000     /* segment will be destroyed on last detach */
#define SHM_LOCKED  02000     /* segment locked in memory */

/* Attach boundary: shmaddr with SHM_RND is rounded down to a multiple. */
#define SHMLBA     4096

typedef unsigned long shmatt_t;

struct shmid_ds {
    struct ipc_perm shm_perm;   /* ownership / permissions */
    size_t   shm_segsz;         /* size of segment in bytes */
    time_t   shm_atime;         /* last attach time */
    time_t   shm_dtime;         /* last detach time */
    time_t   shm_ctime;         /* last change time */
    pid_t    shm_cpid;          /* pid of creator */
    pid_t    shm_lpid;          /* pid of last attach/detach */
    shmatt_t shm_nattch;        /* number of current attaches */
};

/* Implementation limits. */
#define SHMMNI    128                       /* max segments system-wide */
#define SHMMIN    1                         /* min segment size (bytes) */
#define SHMMAX    (8 * 1024 * 1024)         /* max segment size (bytes) */
#define SHMALL    (SHMMNI * (SHMMAX / 4096))/* max pages system-wide */
#define SHMSEG    SHMMNI                    /* max attaches per process */

/* Native-ABI syscall entry points (registered in perso_native).  These do the
 * native copyin/copyout and then call the personality-agnostic core below. */
int    sys_shmget(key_t key, size_t size, int shmflg);
void  *sys_shmat(int shmid, const void *shmaddr, int shmflg);
int    sys_shmdt(const void *shmaddr);
int    sys_shmctl(int shmid, int cmd, struct shmid_ds *buf);

/*
 * Personality-agnostic core.  All operate on KERNEL memory (the caller — the
 * native sys_* wrapper or a Linux/BSD personality shim — handles copyin/copyout
 * in its own ABI), so the core makes no assumption about the userspace
 * shmid_ds layout.  Each returns 0/-errno unless noted.
 */
int    kern_shmget(key_t key, size_t size, int shmflg);  /* returns shmid or -errno */
void  *kern_shmat(int shmid, const void *shmaddr, int shmflg, int *err);
int    kern_shmdt(const void *shmaddr);
int    kern_shm_rmid(int shmid);
int    kern_shm_setperm(int shmid, uid_t uid, gid_t gid, mode_t mode);
int    kern_shm_stat(int shmid, struct shmid_ds *out);   /* native layout, kernel buf */
int    kern_shm_lock(int shmid, int lock);               /* lock!=0 => SHM_LOCK */

/* Reverse every attachment this process still holds, at proc_exit(). */
void   shm_proc_cleanup(int pid);

void   shm_init(void);

#endif /* _SYS_SHM_H */
