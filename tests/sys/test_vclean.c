#include <vfs/vnode.h>
#include <kern/console.h>
#include <string.h>

static int mock_vop_reclaim_called = 0;

static int mock_vop_reclaim(struct vnode *vp, struct ucred *cred) {
    (void)vp;
    (void)cred;
    mock_vop_reclaim_called = 1;
    return 0;
}

void test_vclean(void) {
    kprint("\n=== TEST: vclean ===\n");

    struct vnode vp;
    struct vnodeops vops;

    /* Zero out structures */
    memset(&vp, 0, sizeof(vp));
    memset(&vops, 0, sizeof(vops));

    /* Initialize interlock */
    spinlock_init(&vp.v_interlock, "vnode_interlock");

    /* Setup mock vnodeops */
    vops.vop_reclaim = mock_vop_reclaim;

    /* Initialize test values */
    vp.v_data = (void *)0xdeadbeef;
    vp.v_op = &vops;
    vp.v_mount = (struct mount *)0x12345678;
    vp.v_type = VREG;
    vp.v_flag = VDOOMED | VROOT; /* VROOT should be preserved, VDOOMED cleared */

    /* Reset mock state */
    mock_vop_reclaim_called = 0;

    /* Call function under test */
    vclean(&vp, 0);

    /* Verify VOP_RECLAIM was called */
    if (mock_vop_reclaim_called) {
        kprint("PASS: vop_reclaim was called\n");
    } else {
        kprint("FAIL: vop_reclaim was not called\n");
    }

    /* Verify fs-specific state was cleared */
    if (vp.v_data == NULL && vp.v_op == NULL && vp.v_mount == NULL) {
        kprint("PASS: v_data, v_op, v_mount cleared\n");
    } else {
        kprint("FAIL: fs-specific state not fully cleared\n");
    }

    /* Verify type changed to VBAD */
    if (vp.v_type == VBAD) {
        kprint("PASS: v_type changed to VBAD\n");
    } else {
        kprintf("FAIL: v_type is %d instead of VBAD (%d)\n", vp.v_type, VBAD);
    }

    /* Verify VFREEING and VDOOMED are cleared, VROOT preserved */
    if ((vp.v_flag & VFREEING) == 0 && (vp.v_flag & VDOOMED) == 0) {
        kprint("PASS: VFREEING and VDOOMED flags cleared\n");
    } else {
        kprintf("FAIL: VFREEING or VDOOMED not cleared. v_flag = 0x%x\n", vp.v_flag);
    }

    if ((vp.v_flag & VROOT) != 0) {
        kprint("PASS: VROOT flag preserved\n");
    } else {
        kprint("FAIL: VROOT flag not preserved\n");
    }

    /* Test path where vop_reclaim is NULL */
    kprint("--- Testing without vop_reclaim ---\n");

    /* Re-initialize for second test */
    memset(&vops, 0, sizeof(vops));
    vp.v_data = (void *)0xbeefdead;
    vp.v_op = &vops; /* vop_reclaim is now NULL */
    vp.v_mount = (struct mount *)0x87654321;
    vp.v_type = VDIR;
    vp.v_flag = VDOOMED;
    mock_vop_reclaim_called = 0;

    /* Call function under test */
    vclean(&vp, 0);

    /* Verify VOP_RECLAIM was NOT called */
    if (!mock_vop_reclaim_called) {
        kprint("PASS: vop_reclaim correctly not called (it was NULL)\n");
    } else {
        kprint("FAIL: mock vop_reclaim called despite being NULL in vops\n");
    }

    /* Verify fs-specific state was cleared */
    if (vp.v_data == NULL && vp.v_op == NULL && vp.v_mount == NULL && vp.v_type == VBAD) {
        kprint("PASS: fs-specific state cleared (NULL vop_reclaim case)\n");
    } else {
        kprint("FAIL: fs-specific state not cleared (NULL vop_reclaim case)\n");
    }

    kprint("=== TEST COMPLETE ===\n");
}
