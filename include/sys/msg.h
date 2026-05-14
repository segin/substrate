/*
 * <sys/msg.h> — System V message queues (substrate stubs).
 *
 * Every entry returns -1 with errno = ENOSYS; substrate has no
 * msg queue implementation.  Surface exists so ports compile.
 * Programs should use pipes, sockets (when available), or
 * shm_open + an in-band protocol instead.
 */

#ifndef _SYS_MSG_H
#define _SYS_MSG_H

#include <sys/ipc.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MSG_NOERROR 010000

typedef unsigned long msgqnum_t;
typedef unsigned long msglen_t;

struct msqid_ds {
    struct ipc_perm msg_perm;
    time_t   msg_stime;
    time_t   msg_rtime;
    time_t   msg_ctime;
    msgqnum_t msg_qnum;
    msglen_t  msg_qbytes;
    pid_t    msg_lspid;
    pid_t    msg_lrpid;
};

int   msgctl(int msqid, int cmd, struct msqid_ds *buf);
int   msgget(key_t key, int msgflg);
ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);
int   msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);

#ifdef __cplusplus
}
#endif
#endif
