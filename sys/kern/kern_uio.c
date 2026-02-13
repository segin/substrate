/*
 * sys/kern/kern_uio.c - Scatter/gather I/O utilities
 */

#include <sys/uio.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <string.h>

/* Forward declarations for arch-specific copy functions */
extern int copyin(const void *src, void *dst, size_t size);
extern int copyout(const void *src, void *dst, size_t size);

/**
 * uiomove - move data between a buffer and a uio structure
 * 
 * cp:   buffer to/from which to move data
 * n:    number of bytes to move
 * uio:  uio structure describing the move
 * 
 * Returns 0 on success, or an error code (e.g. EFAULT).
 */
int uiomove(void *cp, size_t n, struct uio *uio) {
    struct iovec *iov;
    size_t cnt;
    int error = 0;

    while (n > 0 && uio->uio_resid > 0) {
        iov = uio->uio_iov;
        cnt = iov->iov_len;
        if (cnt == 0) {
            uio->uio_iov++;
            uio->uio_iovcnt--;
            continue;
        }
        if (cnt > n) cnt = n;

        if (uio->uio_segflg == UIO_USERSPACE) {
            if (uio->uio_rw == UIO_READ) {
                error = copyout(cp, iov->iov_base, cnt);
            } else {
                error = copyin(iov->iov_base, cp, cnt);
            }
            if (error) return EFAULT;
        } else {
            if (uio->uio_rw == UIO_READ) {
                memcpy(iov->iov_base, cp, cnt);
            } else {
                memcpy(cp, iov->iov_base, cnt);
            }
        }

        iov->iov_base = (char *)iov->iov_base + cnt;
        iov->iov_len -= cnt;
        uio->uio_resid -= cnt;
        uio->uio_offset += cnt;
        cp = (char *)cp + cnt;
        n -= cnt;
    }

    return 0;
}
