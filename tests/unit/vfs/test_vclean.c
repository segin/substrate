#include <sys/types.h>
#include <sys/errno.h>
#include <vfs/vnode.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

static int mock_vop_reclaim_called = 0;

static int mock_vop_reclaim(struct vnode *vp, struct ucred *cred) {
    (void)vp;
    (void)cred;
    mock_vop_reclaim_called = 1;
    return 0;
}

bool test_vclean_basic(void) {
    struct vnode *vp;
    int error;

    vnode_init();

    error = getnewvnode("test_vclean", NULL, NULL, &vp);
    if (error) {
        return false;
    }

    struct vnodeops mock_vops = {0};
    mock_vops.vop_reclaim = mock_vop_reclaim;
    vp->v_op = &mock_vops;
    vp->v_data = (void *)0xdeadbeef;
    vp->v_mount = (struct mount *)0xcafebabe;
    vp->v_type = VREG;
    vp->v_flag |= VDOOMED | VROOT;
    mock_vop_reclaim_called = 0;

    vclean(vp, 0);

    bool success = true;

    if (mock_vop_reclaim_called != 1) success = false;
    if (vp->v_data != NULL) success = false;
    if (vp->v_op != NULL) success = false;
    if (vp->v_mount != NULL) success = false;
    if (vp->v_type != VBAD) success = false;
    if (vp->v_flag & VFREEING) success = false;
    if (vp->v_flag & VDOOMED) success = false;
    if ((vp->v_flag & VROOT) == 0) success = false;

    vp->v_usecount = 0;
    vp->v_flag &= ~VONFREELIST;
    vnode_reclaim(vp);

    return success;
}

bool test_vclean_null_reclaim(void) {
    struct vnode *vp;
    int error;

    vnode_init();

    error = getnewvnode("test_vclean_null", NULL, NULL, &vp);
    if (error) {
        return false;
    }

    struct vnodeops empty_vops = {0};
    vp->v_op = &empty_vops;
    vp->v_data = (void *)0xbeefdead;
    vp->v_mount = (struct mount *)0x87654321;
    vp->v_type = VDIR;
    vp->v_flag |= VDOOMED;
    mock_vop_reclaim_called = 0;

    vclean(vp, 0);

    bool success = true;

    if (mock_vop_reclaim_called != 0) success = false;
    if (vp->v_data != NULL) success = false;
    if (vp->v_op != NULL) success = false;
    if (vp->v_mount != NULL) success = false;
    if (vp->v_type != VBAD) success = false;

    vp->v_usecount = 0;
    vp->v_flag &= ~VONFREELIST;
    vnode_reclaim(vp);

    return success;
}
