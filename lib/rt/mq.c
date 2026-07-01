/*
 * mq.c — POSIX message-queue wrappers (librt).
 *
 * Thin shims over the kernel's native mq syscalls
 * (sys/kern/posix_mqueue.c).  The raw native syscall() returns the kernel
 * value directly: a negative errno in [-4095,-1] on error.  Every wrapper
 * runs its return through __rt_ret() to honour the POSIX -1 + errno contract.
 */
#include <mqueue.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>

/* Map a raw negative-errno syscall return to -1 + errno. */
static long __rt_ret(long r)
{
    if (r < 0 && r >= -4095) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

mqd_t mq_open(const char *name, int oflag, ...)
{
    mode_t mode = 0;
    struct mq_attr *attr = NULL;

    if (oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode = (mode_t)va_arg(ap, int);       /* mode_t promotes to int */
        attr = va_arg(ap, struct mq_attr *);
        va_end(ap);
    }
    return (mqd_t)__rt_ret(syscall(SYS_MQ_OPEN, name, oflag,
                                   (int)mode, attr));
}

int mq_close(mqd_t mqdes)
{
    return (int)__rt_ret(syscall(SYS_MQ_CLOSE, mqdes));
}

int mq_unlink(const char *name)
{
    return (int)__rt_ret(syscall(SYS_MQ_UNLINK, name));
}

int mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                 unsigned msg_prio, const struct timespec *abs_timeout)
{
    return (int)__rt_ret(syscall(SYS_MQ_TIMEDSEND, mqdes, msg_ptr,
                                 msg_len, msg_prio, abs_timeout));
}

int mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
            unsigned msg_prio)
{
    return mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, NULL);
}

ssize_t mq_timedreceive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                        unsigned *msg_prio, const struct timespec *abs_timeout)
{
    return (ssize_t)__rt_ret(syscall(SYS_MQ_TIMEDRECEIVE, mqdes, msg_ptr,
                                     msg_len, msg_prio, abs_timeout));
}

ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                   unsigned *msg_prio)
{
    return mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio, NULL);
}

int mq_notify(mqd_t mqdes, const struct sigevent *notification)
{
    return (int)__rt_ret(syscall(SYS_MQ_NOTIFY, mqdes, notification));
}

int mq_getattr(mqd_t mqdes, struct mq_attr *attr)
{
    return (int)__rt_ret(syscall(SYS_MQ_GETSETATTR, mqdes,
                                 (struct mq_attr *)0, attr));
}

int mq_setattr(mqd_t mqdes, const struct mq_attr *restrict attr,
               struct mq_attr *restrict oattr)
{
    return (int)__rt_ret(syscall(SYS_MQ_GETSETATTR, mqdes, attr, oattr));
}
