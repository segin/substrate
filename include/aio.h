/*
 * <aio.h> — POSIX asynchronous I/O.
 *
 * Substrate implements AIO as a userspace worker-thread pool over libpthread
 * (the glibc/librt model): submitted requests are queued and executed by a
 * pool of threads running blocking pread/pwrite/fsync, with results stored
 * back on the aiocb.  The aio_* functions live in librt (link with -lrt).
 */
#ifndef _AIO_H
#define _AIO_H

#include <sys/types.h>
#include <signal.h>     /* struct sigevent */
#include <time.h>       /* struct timespec */

#ifdef __cplusplus
extern "C" {
#endif

/* aio_lio_opcode values. */
#define LIO_READ   0
#define LIO_WRITE  1
#define LIO_NOP    2

/* lio_listio() mode values. */
#define LIO_WAIT   0
#define LIO_NOWAIT 1

/*
 * Maximum number of operations a single lio_listio() call may submit.  POSIX
 * only requires {_POSIX_AIO_LISTIO_MAX} (== 2); substrate bounds one batch to
 * a sane value (matching FreeBSD).  lio_listio() fails with EINVAL when nent
 * is negative or exceeds this.  The total number of concurrently outstanding
 * asynchronous operations is separately bounded by {AIO_MAX} (<limits.h>).
 */
#define AIO_LISTIO_MAX 16

/* aio_cancel() return values. */
#define AIO_CANCELED    0
#define AIO_NOTCANCELED 1
#define AIO_ALLDONE     2

struct aiocb {
    int              aio_fildes;      /* file descriptor */
    off_t            aio_offset;      /* file offset */
    volatile void   *aio_buf;         /* buffer */
    size_t           aio_nbytes;      /* transfer length */
    int              aio_reqprio;     /* request priority offset */
    struct sigevent  aio_sigevent;    /* completion notification */
    int              aio_lio_opcode;  /* LIO_READ / LIO_WRITE / LIO_NOP */

    /* librt-private — do not touch.  Set to the internal request object at
     * submit time; read by aio_error/aio_return/aio_suspend/aio_cancel. */
    void            *__aio_impl;
};

int      aio_read(struct aiocb *aiocbp);
int      aio_write(struct aiocb *aiocbp);
int      aio_error(const struct aiocb *aiocbp);
ssize_t  aio_return(struct aiocb *aiocbp);
int      aio_suspend(const struct aiocb *const list[], int nent,
                     const struct timespec *timeout);
int      aio_cancel(int fildes, struct aiocb *aiocbp);
int      aio_fsync(int op, struct aiocb *aiocbp);
int      lio_listio(int mode, struct aiocb *const restrict list[restrict],
                    int nent, struct sigevent *restrict sig);

#ifdef __cplusplus
}
#endif

#endif /* _AIO_H */
