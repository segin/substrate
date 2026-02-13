#include <stdint.h>
#include <stddef.h>

#include "../../../sys/vfs/vnode.h"
#include "../../../sys/include/sys/fcntl.h"
#include "../../../sys/include/sys/uio.h"
#include "../../../sys/include/sys/ucred.h"
#include "../../../sys/include/sys/poll.h"
#include "../../../sys/include/sys/errno.h"

#ifndef MNT_WAIT
#define MNT_WAIT 1
#endif
#ifndef MNT_NOWAIT
#define MNT_NOWAIT 2
#endif

#define CHECK(cond) do { if (!(cond)) return __LINE__; } while (0)

static int g_open_called, g_close_called, g_read_called, g_write_called;
static int g_ioctl_called, g_poll_called, g_fsync_called, g_bmap_called;
static int g_strategy_called, g_access_called, g_allow_access = 1;
static int g_last_waitfor;
static off_t g_last_write_offset;

void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }

int cache_lookup(struct vnode *dvp, struct vnode **vpp, const char *name, size_t len)
{ (void)dvp; (void)vpp; (void)name; (void)len; return -1; }
void cache_enter(struct vnode *dvp, struct vnode *vp, const char *name, size_t len)
{ (void)dvp; (void)vp; (void)name; (void)len; }

static int mock_open(struct vnode *vp, int mode, struct ucred *cred)
{ (void)vp; (void)mode; (void)cred; g_open_called++; return 0; }
static int mock_close(struct vnode *vp, int fflag, struct ucred *cred)
{ (void)vp; (void)fflag; (void)cred; g_close_called++; return 0; }
static int mock_access(struct vnode *vp, int mode, struct ucred *cred)
{ (void)vp; (void)mode; (void)cred; g_access_called++; return g_allow_access ? 0 : EACCES; }
static int mock_read(struct vnode *vp, struct uio *uio, int ioflag, struct ucred *cred)
{ (void)vp; (void)ioflag; (void)cred; g_read_called++; if (uio->uio_resid > 4) { uio->uio_resid -= 4; uio->uio_offset += 4; } return 0; }
static int mock_write(struct vnode *vp, struct uio *uio, int ioflag, struct ucred *cred)
{ (void)vp; (void)ioflag; (void)cred; g_write_called++; g_last_write_offset = uio->uio_offset; if (uio->uio_resid > 0) { size_t n = uio->uio_resid > 3 ? 3 : uio->uio_resid; uio->uio_resid -= n; uio->uio_offset += (off_t)n; } return 0; }
static int mock_ioctl(struct vnode *vp, uint32_t command, void *data, int fflag, struct ucred *cred)
{ (void)vp; (void)command; (void)data; (void)fflag; (void)cred; g_ioctl_called++; return 0; }
static int mock_poll(struct vnode *vp, int events, struct ucred *cred)
{ (void)vp; (void)cred; g_poll_called++; return events & (POLLIN | POLLOUT); }
static int mock_fsync(struct vnode *vp, int waitfor, struct ucred *cred)
{ (void)vp; (void)cred; g_fsync_called++; g_last_waitfor = waitfor; return 0; }
static int mock_bmap(struct vnode *vp, off_t offset, struct vnode **vpp, uint64_t *bnp, int *runp, int *runb)
{ g_bmap_called++; if (vpp) *vpp = vp; if (bnp) *bnp = (uint64_t)offset + 100; if (runp) *runp = 2; if (runb) *runb = 1; return 0; }
static int mock_strategy(struct vnode *vp, void *bp)
{ (void)vp; (void)bp; g_strategy_called++; return 0; }

static struct vnodeops g_ops = {
    .vop_open = mock_open, .vop_close = mock_close, .vop_access = mock_access,
    .vop_read = mock_read, .vop_write = mock_write, .vop_ioctl = mock_ioctl,
    .vop_poll = mock_poll, .vop_fsync = mock_fsync, .vop_bmap = mock_bmap,
    .vop_strategy = mock_strategy,
};

static void reset_state(void)
{
    g_open_called = g_close_called = g_read_called = g_write_called = 0;
    g_ioctl_called = g_poll_called = g_fsync_called = g_bmap_called = 0;
    g_strategy_called = g_access_called = 0;
    g_allow_access = 1;
    g_last_waitfor = 0;
    g_last_write_offset = -1;
}

int main(void)
{
    struct vnode vp = {0};
    struct ucred cred = {0};
    char buf[16] = {0};
    struct iovec iov = {.iov_base = buf, .iov_len = sizeof(buf)};
    struct uio uio = {.uio_iov = &iov, .uio_iovcnt = 1, .uio_segflg = UIO_SYSSPACE};
    uint64_t bnp = 0;
    int runp = 0, runb = 0;
    char bp = 0;

    vp.v_op = &g_ops;
    vp.v_type = VREG;

    reset_state();
    CHECK(vop_open(&vp, O_RDONLY, &cred) == 0);
    CHECK(g_access_called == 1 && g_open_called == 1);

    reset_state();
    CHECK(vop_open(&vp, O_WRONLY, &cred) == 0);
    CHECK(vp.v_writecount == 1);

    reset_state();
    g_allow_access = 0;
    CHECK(vop_open(&vp, O_RDONLY, &cred) == EACCES);

    vp.v_usecount = 2;
    vp.v_writecount = 1;
    reset_state();
    CHECK(vop_close(&vp, O_WRONLY, &cred) == 0);
    CHECK(g_fsync_called == 1 && g_last_waitfor == MNT_WAIT);

    vp.v_size = 16;
    reset_state();
    uio.uio_offset = 0;
    uio.uio_resid = 8;
    CHECK(vop_read(&vp, &uio, 0, &cred) == 0);
    CHECK(uio.uio_resid == 4);

    reset_state();
    uio.uio_offset = vp.v_size;
    uio.uio_resid = 5;
    CHECK(vop_read(&vp, &uio, 0, &cred) == 0);
    CHECK(g_read_called == 0);

    reset_state();
    vp.v_size = 20;
    uio.uio_offset = 0;
    uio.uio_resid = 3;
    CHECK(vop_write(&vp, &uio, O_APPEND, &cred) == 0);
    CHECK(g_last_write_offset == 20);

    reset_state();
    uio.uio_offset = 0;
    uio.uio_resid = 3;
    CHECK(vop_write(&vp, &uio, O_SYNC, &cred) == 0);
    CHECK(g_fsync_called == 1);

    reset_state();
    CHECK(vop_ioctl(&vp, 1, NULL, 0, &cred) == 0);
    CHECK(vop_poll(&vp, POLLIN | POLLOUT | POLLERR, &cred) == (POLLIN | POLLOUT));

    reset_state();
    CHECK(vop_fsync(&vp, MNT_NOWAIT, &cred) == 0);
    CHECK(vop_bmap(&vp, 7, NULL, &bnp, &runp, &runb) == 0);
    CHECK(bnp == 107 && runp == 2 && runb == 1);
    CHECK(vop_strategy(&vp, &bp) == 0);

    return 0;
}
