#include <stdint.h>
#include <stddef.h>

#include "../../sys/vfs/vnode.h"
#include "../../sys/include/sys/uio.h"
#include "../../sys/include/sys/fcntl.h"
#include "../../sys/include/sys/poll.h"
#include "../../sys/include/sys/ucred.h"

#ifndef MNT_WAIT
#define MNT_WAIT 1
#endif
#ifndef MNT_NOWAIT
#define MNT_NOWAIT 2
#endif

void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
int cache_lookup(struct vnode *dvp, struct vnode **vpp, const char *name, size_t len)
{ (void)dvp; (void)vpp; (void)name; (void)len; return -1; }
void cache_enter(struct vnode *dvp, struct vnode *vp, const char *name, size_t len)
{ (void)dvp; (void)vp; (void)name; (void)len; }

static int fuzz_access(struct vnode *vp, int mode, struct ucred *cred)
{ (void)vp; (void)mode; (void)cred; return 0; }
static int fuzz_open(struct vnode *vp, int mode, struct ucred *cred)
{ (void)vp; (void)mode; (void)cred; return 0; }
static int fuzz_close(struct vnode *vp, int fflag, struct ucred *cred)
{ (void)vp; (void)fflag; (void)cred; return 0; }
static int fuzz_read(struct vnode *vp, struct uio *uio, int ioflag, struct ucred *cred)
{
    (void)vp; (void)ioflag; (void)cred;
    size_t consumed = uio->uio_resid > 8 ? 8 : uio->uio_resid;
    uio->uio_resid -= consumed;
    uio->uio_offset += (off_t)consumed;
    return 0;
}
static int fuzz_write(struct vnode *vp, struct uio *uio, int ioflag, struct ucred *cred)
{
    (void)vp; (void)ioflag; (void)cred;
    size_t consumed = uio->uio_resid > 8 ? 8 : uio->uio_resid;
    uio->uio_resid -= consumed;
    uio->uio_offset += (off_t)consumed;
    return 0;
}
static int fuzz_poll(struct vnode *vp, int events, struct ucred *cred)
{ (void)vp; (void)cred; return events; }
static int fuzz_fsync(struct vnode *vp, int waitfor, struct ucred *cred)
{ (void)vp; (void)waitfor; (void)cred; return 0; }
static int fuzz_strategy(struct vnode *vp, void *bp)
{ (void)vp; (void)bp; return 0; }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct vnodeops ops = {
        .vop_access = fuzz_access,
        .vop_open = fuzz_open,
        .vop_close = fuzz_close,
        .vop_read = fuzz_read,
        .vop_write = fuzz_write,
        .vop_poll = fuzz_poll,
        .vop_fsync = fuzz_fsync,
        .vop_strategy = fuzz_strategy,
    };
    struct vnode vp = {0};
    struct ucred cred = {0};
    struct iovec iov = {0};
    struct uio uio = {0};
    uint8_t scratch[32];

    vp.v_op = &ops;
    vp.v_type = VREG;
    vp.v_size = (off_t)(size & 0x3ff);

    iov.iov_base = scratch;
    iov.iov_len = sizeof(scratch);
    uio.uio_iov = &iov;
    uio.uio_iovcnt = 1;
    uio.uio_offset = (off_t)(size ? data[0] : 0);
    uio.uio_resid = (size_t)(size & 31);

    vop_open(&vp, size > 1 ? data[1] : O_RDONLY, &cred);
    vop_read(&vp, &uio, 0, &cred);
    vop_write(&vp, &uio, (size > 2 && (data[2] & 1)) ? O_APPEND : 0, &cred);
    vop_poll(&vp, POLLIN | POLLOUT | POLLERR | POLLHUP, &cred);
    vop_fsync(&vp, (size > 3 && (data[3] & 1)) ? MNT_WAIT : MNT_NOWAIT, &cred);
    vop_strategy(&vp, scratch);
    vop_close(&vp, O_RDWR, &cred);

    return 0;
}
