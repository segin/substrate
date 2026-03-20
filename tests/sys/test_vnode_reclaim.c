#include <kern/console.h>
#include <vfs/vnode.h>

void test_vnode_reclaim(void) {
    kprint("\n=== TEST: vnode_reclaim ===\n");

    struct vnode *vp = NULL;
    int error;

    /* Create a regular file vnode */
    error = vnode_create(VREG, NULL, NULL, &vp);
    if (error == 0 && vp != NULL && vp->v_type == VREG) {
        kprint("PASS: vnode_create(VREG) created VREG vnode successfully\n");

        /* Drop the reference count so that it's 0 when calling reclaim */
        vp->v_usecount = 0;

        /* Clear any flags that might trigger free list removal logic
           since we didn't actually put it on the free list */
        vp->v_flag &= ~VONFREELIST;

        /* Call vnode_reclaim to clean it up */
        vnode_reclaim(vp);

        kprint("PASS: vnode_reclaim executed without crashing\n");
    } else {
        kprintf("FAIL: vnode_create(VREG) failed (error=%d, type=%d)\n", error, vp ? (int)vp->v_type : -1);
    }

    kprint("=== TEST COMPLETE ===\n");
}
