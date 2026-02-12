/*
 * sys/sys/uio.h - Scatter/gather I/O definitions
 */

#ifndef _SYS_UIO_H
#define _SYS_UIO_H

#include <sys/types.h>

enum uio_rw { UIO_READ, UIO_WRITE };
enum uio_seg { UIO_USERSPACE, UIO_SYSSPACE };

struct iovec {
    void    *iov_base;      /* Base address. */
    size_t  iov_len;       /* Length. */
};

struct uio {
    struct  iovec *uio_iov; /* scatter/gather list */
    int     uio_iovcnt;     /* length of scatter/gather list */
    off_t   uio_offset;     /* offset in target object */
    size_t  uio_resid;      /* remaining bytes to process */
    enum    uio_seg uio_segflg; /* address space */
    enum    uio_rw uio_rw;  /* operation */
    struct  thread *uio_td; /* thread doing the I/O */
};

int uiomove(void *cp, size_t n, struct uio *uio);

#endif /* _SYS_UIO_H */
