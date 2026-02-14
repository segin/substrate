#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "../../../sys/vfs/vnode.h"
#include "../../../sys/include/sys/uio.h"
#include "../../../sys/include/sys/fcntl.h"
#include "../../../sys/include/sys/ucred.h"

#ifndef MNT_WAIT
#define MNT_WAIT 1
#endif

void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
int cache_lookup(struct vnode *dvp, struct vnode **vpp, const char *name, size_t len)
{ (void)dvp; (void)vpp; (void)name; (void)len; return -1; }
void cache_enter(struct vnode *dvp, struct vnode *vp, const char *name, size_t len)
{ (void)dvp; (void)vp; (void)name; (void)len; }

static int prop_access_ok(struct vnode *vp, int mode, struct ucred *cred)
{ (void)vp; (void)mode; (void)cred; return 0; }
static int prop_open_ok(struct vnode *vp, int mode, struct ucred *cred)
{ (void)vp; (void)mode; (void)cred; return 0; }
static int prop_close_ok(struct vnode *vp, int fflag, struct ucred *cred)
{ (void)vp; (void)fflag; (void)cred; return 0; }
static int prop_fsync_ok(struct vnode *vp, int waitfor, struct ucred *cred)
{ (void)vp; (void)waitfor; (void)cred; return 0; }

bool prop_open_close_writecount_non_negative(int iterations)
{
    struct vnodeops ops = {
        .vop_access = prop_access_ok,
        .vop_open = prop_open_ok,
        .vop_close = prop_close_ok,
        .vop_fsync = prop_fsync_ok,
    };
    struct vnode vp = {0};
    struct ucred cred = {0};
    int i;

    vp.v_op = &ops;
    vp.v_type = VREG;

    for (i = 0; i < iterations; i++) {
        int mode = (i % 3 == 0) ? O_RDONLY : ((i % 3 == 1) ? O_WRONLY : O_RDWR);

        if (vop_open(&vp, mode, &cred) != 0)
            return false;
        if (vop_close(&vp, mode, &cred) != 0)
            return false;
        if ((int)vp.v_writecount < 0)
            return false;
    }

    return true;
}
