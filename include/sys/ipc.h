/*
 * <sys/ipc.h> — System V IPC keys + permissions.
 *
 * Substrate does NOT implement a System V IPC subsystem
 * (message queues / shared memory / semaphores).  The header
 * surface is provided so that ports compile; the implementations
 * in lib/c/src/posix_extra2.c return ENOSYS.
 *
 * Programs that need shared memory should use POSIX shm_open(3)
 * which is backed by substrate's shmfs at /dev/shm.
 */

#ifndef _SYS_IPC_H
#define _SYS_IPC_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ipcget flags */
#define IPC_CREAT  0001000
#define IPC_EXCL   0002000
#define IPC_NOWAIT 0004000

/* ipc{ctl,perm} cmd selectors */
#define IPC_RMID   0
#define IPC_SET    1
#define IPC_STAT   2
#define IPC_INFO   3

/* IPC_PRIVATE — anonymous (non-keyed) IPC objects. */
#define IPC_PRIVATE ((key_t)0)

struct ipc_perm {
    key_t    __key;
    uid_t    uid;
    gid_t    gid;
    uid_t    cuid;
    gid_t    cgid;
    mode_t   mode;
    unsigned short __seq;
};

key_t ftok(const char *pathname, int proj_id);

#ifdef __cplusplus
}
#endif
#endif
