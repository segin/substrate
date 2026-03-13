#include <kern/console.h>
#include <vfs/vnode.h>

void test_vnode_create(void) {
    kprint("\n=== TEST: vnode_create ===\n");

    struct vnode *vp = NULL;
    int error;

    /* Test 1: Create a regular file vnode */
    error = vnode_create(VREG, NULL, NULL, &vp);
    if (error == 0 && vp != NULL && vp->v_type == VREG) {
        kprint("PASS: vnode_create(VREG) created VREG vnode successfully\n");
    } else {
        kprintf("FAIL: vnode_create(VREG) failed (error=%d, type=%d)\n", error, vp ? (int)vp->v_type : -1);
    }
    if (vp) vrele(vp); /* vrele to decrement usecount */

    /* Test 2: Create a directory vnode */
    vp = NULL;
    error = vnode_create(VDIR, NULL, NULL, &vp);
    if (error == 0 && vp != NULL && vp->v_type == VDIR) {
        kprint("PASS: vnode_create(VDIR) created VDIR vnode successfully\n");
    } else {
        kprintf("FAIL: vnode_create(VDIR) failed (error=%d, type=%d)\n", error, vp ? (int)vp->v_type : -1);
    }
    if (vp) vrele(vp);

    kprint("=== TEST COMPLETE ===\n");
}
