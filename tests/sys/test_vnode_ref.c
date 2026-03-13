#include <vfs/vnode.h>
#include <kern/console.h>

void test_vref_basic(void) {
    struct vnode *vp;
    int err = getnewvnode("test_vref", NULL, NULL, &vp);
    if (err) {
        kprintf("FAIL: getnewvnode failed\n");
        return;
    }

    /* Initial usecount should be 1 */
    if (vp->v_usecount != 1) {
        kprintf("FAIL: initial usecount is %d\n", vp->v_usecount);
        return;
    }

    vref(vp);
    if (vp->v_usecount != 2) {
        kprintf("FAIL: usecount after vref is %d\n", vp->v_usecount);
        return;
    }

    vrele(vp);
    if (vp->v_usecount != 1) {
        kprintf("FAIL: usecount after vrele is %d\n", vp->v_usecount);
        return;
    }

    /* Drop to 0, vnode should go to free list */
    vrele(vp);
    if (vp->v_usecount != 0) {
        kprintf("FAIL: usecount after second vrele is %d\n", vp->v_usecount);
        return;
    }
    if (!(vp->v_flag & VONFREELIST)) {
        kprintf("FAIL: vnode not on free list after usecount hits 0\n");
        return;
    }

    /* Revive vnode */
    vref(vp);
    if (vp->v_usecount != 1) {
        kprintf("FAIL: usecount after revive is %d\n", vp->v_usecount);
        return;
    }
    if (vp->v_flag & VONFREELIST) {
        kprintf("FAIL: vnode still on free list after revive\n");
        return;
    }

    /* Clean up */
    vrele(vp);
    kprint("test_vref_basic: PASS\n");
}

void run_vnode_ref_tests(void) {
    kprint("\n=== TEST: VNode Reference Counting ===\n");
    test_vref_basic();
}
