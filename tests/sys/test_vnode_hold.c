#include <vfs/vnode.h>
#include <kern/console.h>

void run_vnode_hold_tests(void) {
    kprint("\n=== TEST: VNode Hold / Drop ===\n");

    struct vnode *test_vp;
    int error = getnewvnode("test_hold", NULL, NULL, &test_vp);
    if (error) {
        kprintf("FAIL: getnewvnode failed: %d\n", error);
        return;
    }
    kprint("Created test vnode.\n");

    if (test_vp->v_holdcount != 0) {
        kprintf("FAIL: Initial holdcount is not 0: %d\n", test_vp->v_holdcount);
    } else {
        kprint("PASS: Initial holdcount is 0\n");
    }

    vhold(test_vp);
    if (test_vp->v_holdcount != 1) {
        kprintf("FAIL: holdcount after vhold is not 1: %d\n", test_vp->v_holdcount);
    } else {
        kprint("PASS: holdcount after vhold is 1\n");
    }

    vhold(test_vp);
    if (test_vp->v_holdcount != 2) {
        kprintf("FAIL: holdcount after second vhold is not 2: %d\n", test_vp->v_holdcount);
    } else {
        kprint("PASS: holdcount after second vhold is 2\n");
    }

    vdrop(test_vp);
    if (test_vp->v_holdcount != 1) {
        kprintf("FAIL: holdcount after vdrop is not 1: %d\n", test_vp->v_holdcount);
    } else {
        kprint("PASS: holdcount after vdrop is 1\n");
    }

    vdrop(test_vp);
    if (test_vp->v_holdcount != 0) {
        kprintf("FAIL: holdcount after second vdrop is not 0: %d\n", test_vp->v_holdcount);
    } else {
        kprint("PASS: holdcount after second vdrop is 0\n");
    }

    kprint("=== TEST COMPLETE ===\n");
}
