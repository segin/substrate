#include <sys/types.h>
#include <sys/errno.h>
#include <vfs/vnode.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

bool test_vnode_reclaim_basic(void) {
    struct vnode *vp;
    int error;

    /* Initialize vnode system to create vnode_zone */
    vnode_init();

    /* Allocate a new vnode */
    error = getnewvnode("test_reclaim", NULL, NULL, &vp);
    if (error) {
        return false;
    }

    /* Set up dummy state to ensure vnode_reclaim cleans it up without crashing */
    vp->v_type = VREG;
    vp->v_data = (void *)0xdeadbeef;

    /* Reclaim the vnode */
    vnode_reclaim(vp);

    /*
     * Since vnode_reclaim frees the vnode, we can't reliably check its contents
     * without use-after-free. If we get here without a crash or panic, the
     * basic reclaim path works.
     */
    return true;
}
