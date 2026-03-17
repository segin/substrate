#include <sys/types.h>
#include <sys/errno.h>
#include <vfs/vnode.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

bool test_vnode_create_basic(void) {
    struct vnode *vp = NULL;
    int error;

    /* Initialize vnode system */
    vnode_init();

    /* Test 1: Create a regular file vnode */
    error = vnode_create(VREG, NULL, NULL, &vp);
    if (error != 0 || vp == NULL || vp->v_type != VREG) {
        if (vp) {
            vp->v_flag &= ~VONFREELIST;
            vp->v_usecount = 0;
            vnode_reclaim(vp);
        }
        return false;
    }

    /* Clean up the vnode */
    vp->v_flag &= ~VONFREELIST;
    vp->v_usecount = 0;
    vnode_reclaim(vp);

    /* Test 2: Create a directory vnode */
    vp = NULL;
    error = vnode_create(VDIR, NULL, NULL, &vp);
    if (error != 0 || vp == NULL || vp->v_type != VDIR) {
        if (vp) {
            vp->v_flag &= ~VONFREELIST;
            vp->v_usecount = 0;
            vnode_reclaim(vp);
        }
        return false;
    }

    vp->v_flag &= ~VONFREELIST;
    vp->v_usecount = 0;
    vnode_reclaim(vp);

    return true;
}
