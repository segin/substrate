#include <vfs/vnode.h>
#include <kern/console.h>
#include <string.h>

void vnode_reclaim(struct vnode *vp);

static int mock_vop_reclaim_called = 0;

static int mock_vop_reclaim(struct vnode *vp, struct ucred *cred) {
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
    test_vp->v_flag |= VDOOMED | VROOT; /* VROOT should be preserved, VDOOMED cleared */
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

    if ((test_vp->v_flag & VROOT) != 0) {
        kprint("PASS: VROOT flag preserved\n");
    } else {
        kprint("FAIL: VROOT flag not preserved\n");
    }

    /* Test path where vop_reclaim is NULL */
    kprint("--- Testing without vop_reclaim ---\n");
    
    /* Get another vnode for second test */
    struct vnode *test_vp2;
    error = getnewvnode("test_vclean2", NULL, NULL, &test_vp2);
    if (error) {
        kprintf("FAIL: getnewvnode 2 failed: %d\n", error);
        vnode_reclaim(test_vp);
        return;
    }

    test_vp2->v_op = &mock_vops; /* vop_reclaim is NOT NULL yet */
    struct vnodeops empty_vops = {0};
    test_vp2->v_op = &empty_vops; /* Now vop_reclaim IS NULL */
    test_vp2->v_data = (void *)0xbeefdead;
    test_vp2->v_mount = (struct mount *)0x87654321;
    test_vp2->v_type = VDIR;
    test_vp2->v_flag |= VDOOMED;
    mock_vop_reclaim_called = 0;

    /* Call function under test */
    vclean(test_vp2, 0);

    /* Verify VOP_RECLAIM was NOT called */
    if (!mock_vop_reclaim_called) {
        kprint("PASS: vop_reclaim correctly not called (it was NULL)\n");
    } else {
        kprint("FAIL: mock vop_reclaim called despite being NULL in vops\n");
    }

    /* Verify fs-specific state was cleared */
    if (test_vp2->v_data == NULL && test_vp2->v_op == NULL && test_vp2->v_mount == NULL && test_vp2->v_type == VBAD) {
        kprint("PASS: fs-specific state cleared (NULL vop_reclaim case)\n");
    } else {
        kprint("FAIL: fs-specific state not cleared (NULL vop_reclaim case)\n");
    }

    vnode_reclaim(test_vp);
    vnode_reclaim(test_vp2);

    kprint("=== TEST COMPLETE ===\n");
}
