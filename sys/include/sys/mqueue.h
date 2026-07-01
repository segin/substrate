/*
 * <sys/mqueue.h> (kernel) — POSIX message queues.
 *
 * Kernel-side interface to the named message-queue subsystem
 * (sys/kern/posix_mqueue.c).  A POSIX message queue is a named,
 * cross-process object, so — like the System V IPC objects — it lives in
 * the kernel as a fixed table of queues.  mq_open() hands userspace an
 * opaque integer descriptor (mqd_t) drawn from a per-descriptor table; the
 * queue itself is reference-counted and destroyed once the last descriptor
 * closes and the name has been unlinked.
 *
 * The core (kern_mq_*) operates on kernel memory only; the native sys_mq_*
 * wrappers do the copyin/copyout.  struct mq_attr is byte-identical to the
 * userspace <mqueue.h> definition (it crosses the syscall boundary in
 * mq_getattr/mq_setattr).
 */
#ifndef _SYS_MQUEUE_H
#define _SYS_MQUEUE_H

#include <sys/types.h>
#include <sys/ipc.h>

/* Forward declarations — the full definitions live in <sys/time.h> and
 * <sys/signal.h>; declaring them at file scope keeps the prototypes below
 * referring to the same (single) incomplete type. */
struct timespec;
struct sigevent;

/* POSIX message-queue attributes (matches include/mqueue.h). */
struct mq_attr {
    long mq_flags;    /* O_NONBLOCK — per open description */
    long mq_maxmsg;   /* max # of messages on the queue */
    long mq_msgsize;  /* max size of a single message (bytes) */
    long mq_curmsgs;  /* # of messages currently queued */
};

/* Implementation limits.  Values follow FreeBSD's mqueuefs (the BSD way):
 * MQ_PRIO_MAX 64, and the kern.mqueue.maxmsg / maxmsgsize defaults. */
#define MQ_OPEN_MAX      16      /* max message queues system-wide */
#define MQ_DESC_MAX      64      /* max open descriptors system-wide */
#define MQ_PRIO_MAX      64      /* priorities 0 .. MQ_PRIO_MAX-1 (FreeBSD) */
#define MQ_NAME_MAX      255     /* max characters in a queue name */
#define MQ_MAXMSG_LIMIT  100     /* ceiling for mq_maxmsg (FreeBSD kern.mqueue.maxmsg) */
#define MQ_MSGSIZE_LIMIT 16384   /* ceiling for mq_msgsize (FreeBSD kern.mqueue.maxmsgsize) */
#define MQ_DFL_MAXMSG    10      /* default mq_maxmsg */
#define MQ_DFL_MSGSIZE   1024    /* default mq_msgsize */

/*
 * Native-ABI syscall entry points (registered in perso_native).  These do the
 * native copyin/copyout and then call the personality-agnostic core.
 *
 *   mqd_t is returned as a small non-negative int; errors are -errno.
 *   name is a userspace pointer, buffers are userspace pointers.
 */
int  sys_mq_open(const char *uname, int oflag, mode_t mode,
                 const struct mq_attr *uattr);
int  sys_mq_close(int mqd);
int  sys_mq_unlink(const char *uname);
int  sys_mq_timedsend(int mqd, const char *umsg, size_t len,
                      unsigned prio, const struct timespec *uts);
ssize_t sys_mq_timedreceive(int mqd, char *umsg, size_t len,
                            unsigned *uprio, const struct timespec *uts);
int  sys_mq_notify(int mqd, const struct sigevent *usev);
int  sys_mq_getsetattr(int mqd, const struct mq_attr *unew,
                       struct mq_attr *uold);

/*
 * Personality-agnostic core.  All operate on KERNEL memory (the caller does
 * copyin/copyout).  Each returns 0/-errno unless noted.  These mirror the
 * kern_sem* / kern_shm* style so a foreign personality could marshal its own
 * ABI on top if one is ever added.
 */
int  kern_mq_open(const char *name, int oflag, mode_t mode,
                  const struct mq_attr *attr);
int  kern_mq_unlink(const char *name);
int  kern_mq_close(int mqd);
int  kern_mq_send(int mqd, const char *msg, size_t len, unsigned prio,
                  const struct timespec *abstime);
ssize_t kern_mq_receive(int mqd, char *msg, size_t len, unsigned *prio,
                        const struct timespec *abstime);
int  kern_mq_notify(int mqd, const struct sigevent *sev);
int  kern_mq_getattr(int mqd, struct mq_attr *attr);
int  kern_mq_setattr(int mqd, const struct mq_attr *newattr,
                     struct mq_attr *oldattr);

/* Close descriptors + drop notify registration at process exit. */
void mq_proc_cleanup(int pid);

void mq_init(void);

#endif /* _SYS_MQUEUE_H */
