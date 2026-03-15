#include <vfs/vnode.h>
#include <kern/console.h>

void vnode_reclaim(struct vnode *vp);

int mock_vop_reclaim_called = 0;

int mock_vop_reclaim(struct vnode *vp, struct ucred *cred) {
    (void)vp;
    (void)cred;
    mock_vop_reclaim_called = 1;
    return 0;
}

void run_vclean_tests(void) {
    kprint("\n=== TEST: vclean ===\n");
    struct vnode *test_vp;
    int error = getnewvnode("test_vclean", NULL, NULL, &test_vp);
    if (error) {
        kprintf("FAIL: getnewvnode failed: %d\n", error);
        return;
    }

    /* Setup mock state */
    struct vnodeops mock_vops = {0};
    mock_vops.vop_reclaim = mock_vop_reclaim;
    test_vp->v_op = &mock_vops;
    test_vp->v_data = (void *)0xdeadbeef;
    test_vp->v_mount = (struct mount *)0xcafebabe;
    test_vp->v_type = VREG;
    mock_vop_reclaim_called = 0;

    /* Call vclean */
    vclean(test_vp, 0);

    /* Verify results */
    if (mock_vop_reclaim_called != 1) {
        kprint("FAIL: vop_reclaim was not called\n");
    } else {
        kprint("PASS: vop_reclaim called\n");
    }

    if (test_vp->v_data != NULL) {
        kprint("FAIL: v_data not cleared\n");
    } else {
        kprint("PASS: v_data cleared\n");
    }

    if (test_vp->v_op != NULL) {
        kprint("FAIL: v_op not cleared\n");
    } else {
        kprint("PASS: v_op cleared\n");
    }

    if (test_vp->v_mount != NULL) {
        kprint("FAIL: v_mount not cleared\n");
    } else {
        kprint("PASS: v_mount cleared\n");
    }

    if (test_vp->v_type != VBAD) {
        kprint("FAIL: v_type not VBAD\n");
    } else {
        kprint("PASS: v_type set to VBAD\n");
    }

    if (test_vp->v_flag & VFREEING) {
        kprint("FAIL: VFREEING flag still set\n");
    } else {
        kprint("PASS: VFREEING flag cleared\n");
    }

    if (test_vp->v_flag & VDOOMED) {
        kprint("FAIL: VDOOMED flag still set\n");
    } else {
        kprint("PASS: VDOOMED flag cleared\n");
    }

    vnode_reclaim(test_vp);

    kprint("=== TEST COMPLETE ===\n");
}
