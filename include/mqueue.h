/*
 * <mqueue.h> — POSIX message queues.
 *
 * Named, cross-process message queues.  Backed by the in-kernel subsystem
 * (sys/kern/posix_mqueue.c) through six native syscalls; the mq_* functions
 * themselves live in librt (link with -lrt).
 */
#ifndef _MQUEUE_H
#define _MQUEUE_H

#include <sys/types.h>
#include <signal.h>     /* struct sigevent */
#include <time.h>       /* struct timespec */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * mqd_t is opaque per POSIX; on substrate it is a small integer descriptor
 * drawn from the kernel's descriptor table.  (-1) is the error return.
 */
typedef int mqd_t;

/* Number of distinct message priorities: valid msg_prio is 0 .. MQ_PRIO_MAX-1.
 * Matches the in-kernel POSIX message-queue limit (sys/kern/posix_mqueue.c). */
#ifndef MQ_PRIO_MAX
#define MQ_PRIO_MAX 64
#endif

struct mq_attr {
    long mq_flags;    /* message queue flags (O_NONBLOCK) */
    long mq_maxmsg;   /* max number of messages */
    long mq_msgsize;  /* max message size (bytes) */
    long mq_curmsgs;  /* number of messages currently queued */
};

mqd_t   mq_open(const char *name, int oflag, ...);
int     mq_close(mqd_t mqdes);
int     mq_unlink(const char *name);
int     mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                unsigned msg_prio);
ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                   unsigned *msg_prio);
int     mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                     unsigned msg_prio, const struct timespec *abs_timeout);
ssize_t mq_timedreceive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                        unsigned *msg_prio, const struct timespec *abs_timeout);
int     mq_notify(mqd_t mqdes, const struct sigevent *notification);
int     mq_getattr(mqd_t mqdes, struct mq_attr *attr);
int     mq_setattr(mqd_t mqdes, const struct mq_attr *restrict attr,
                   struct mq_attr *restrict oattr);

#ifdef __cplusplus
}
#endif

#endif /* _MQUEUE_H */
