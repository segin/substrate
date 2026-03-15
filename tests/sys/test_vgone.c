#include <vfs/vnode.h>
#include <kern/console.h>
#include <sys/errno.h>

extern void run_vgone_tests(void);

static void test_vgone_with_references(void) {
    kprint("\n--- Test: vgone with references ---\n");
    struct vnode *vp;
    int error = getnewvnode("test_vgone", NULL, NULL, &vp);
    if (error) {
        kprintf("FAIL: getnewvnode failed: %d\n", error);
        return;
    }

    /* vp has usecount = 1 from getnewvnode */
    vgone(vp);

    if (vp->v_flag & VDOOMED) {
        kprint("PASS: vgone marked vnode as doomed\n");
    } else {
        kprint("FAIL: vgone did not mark vnode as doomed\n");
    }

    /* release the reference, which should reclaim it */
    vrele(vp);
}

static void test_vgone_immediate_reclaim(void) {
    kprint("\n--- Test: vgone immediate reclaim ---\n");
    struct vnode *vp;
    int error = getnewvnode("test_vgone2", NULL, NULL, &vp);
    if (error) {
        kprintf("FAIL: getnewvnode failed: %d\n", error);
        return;
    }

    /* decrement usecount to 0 manually to simulate 0 references */
    vp->v_usecount = 0;

    vgone(vp);

    kprint("PASS: vgone immediate reclaim completed without crashing\n");
}

void run_vgone_tests(void) {
    kprint("\n=== TEST: VNode vgone ===\n");
    test_vgone_with_references();
    test_vgone_immediate_reclaim();
    kprint("=== TEST COMPLETE ===\n");
}
