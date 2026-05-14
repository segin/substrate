/*
 * <sys/uio.h> — vectored I/O.
 *
 * Substrate implements readv/writev/preadv/pwritev in
 * lib/c/src/posix_extra.c by looping over the iov array.  A real
 * scatter/gather syscall is a follow-up; the libc-loop fallback is
 * functionally correct (short-read / short-write at any element
 * stops the loop and returns the cumulative count).
 */

#ifndef _SYS_UIO_H
#define _SYS_UIO_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

ssize_t readv  (int fd, const struct iovec *iov, int iovcnt);
ssize_t writev (int fd, const struct iovec *iov, int iovcnt);
ssize_t preadv (int fd, const struct iovec *iov, int iovcnt, off_t offset);
ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset);

#ifdef __cplusplus
}
#endif
#endif
