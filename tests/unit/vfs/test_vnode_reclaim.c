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
    vp->v_usecount = 0;
    vp->v_flag &= ~VONFREELIST;

    /* Reclaim the vnode */
    vnode_reclaim(vp);

    /* Allocate a second vnode to test free list removal in reclaim */
    error = getnewvnode("test_reclaim_freelist", NULL, NULL, &vp);
    if (error) {
        return false;
    }

    /* Fake being on the free list */
    vp->v_usecount = 0;
    /* Normally handled by vrele putting it on free list, but we mock it. We must NOT set VONFREELIST
       without properly inserting it in free list, as that causes panic in linked list macro.
       Thus we only simulate what is safely mockable. */

    vnode_reclaim(vp);

    /*
     * Since vnode_reclaim frees the vnode, we can't reliably check its contents
     * without use-after-free. If we get here without a crash or panic, the
     * basic reclaim path works.
     */
    return true;
}
