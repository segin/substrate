/*
 * <sys/ipc.h> (kernel) — System V IPC keys + permissions.
 *
 * Kernel-side mirror of the userspace include/sys/ipc.h.  The struct
 * ipc_perm layout MUST match the userspace copy byte-for-byte: it crosses the
 * syscall boundary in semid_ds (IPC_STAT/IPC_SET).
 */
#ifndef _SYS_IPC_H
#define _SYS_IPC_H

#include <sys/types.h>

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

#endif /* _SYS_IPC_H */
