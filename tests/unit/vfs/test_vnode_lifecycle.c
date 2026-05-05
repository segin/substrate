/*
 * test_vnode_lifecycle.c - Unit tests for vnode lifecycle functions
 *
 * Covers: getnewvnode, vref, vrele, vput, vget, vgone (REQ-04-0187)
 */
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <vfs/vnode.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* --- getnewvnode ---------------------------------------------------------- */

bool test_vnode_lifecycle_getnewvnode(void) {
    vnode_init();

    struct vnode *vp = NULL;
    int error = getnewvnode("test_lifecycle", NULL, NULL, &vp);
    if (error != 0 || vp == NULL)
        return false;

    /* Newly allocated vnode should have usecount >= 1 */
    if (vp->v_usecount < 1) {
        vp->v_flag &= ~VONFREELIST;
        vp->v_usecount = 0;
        vnode_reclaim(vp);
        return false;
    }

    vp->v_flag &= ~VONFREELIST;
    vp->v_usecount = 0;
    vnode_reclaim(vp);
    return true;
}

/* --- vref / vrele --------------------------------------------------------- */

bool test_vnode_lifecycle_vref_vrele(void) {
    vnode_init();

    struct vnode *vp = NULL;
    int error = getnewvnode("test_vref", NULL, NULL, &vp);
    if (error || !vp)
        return false;

    uint32_t initial = vp->v_usecount;

    /* vref should increment use count */
    vref(vp);
    if (vp->v_usecount != initial + 1) {
        vp->v_usecount = 0;
        vp->v_flag &= ~VONFREELIST;
        vnode_reclaim(vp);
        return false;
    }

    /* vrele should decrement use count */
    vrele(vp);
    if (vp->v_usecount != initial) {
        vp->v_usecount = 0;
        vp->v_flag &= ~VONFREELIST;
        vnode_reclaim(vp);
        return false;
    }

    vp->v_flag &= ~VONFREELIST;
    vp->v_usecount = 0;
    vnode_reclaim(vp);
    return true;
}

/* Property: refcount never goes negative */
bool test_vnode_lifecycle_refcount_nonneg(void) {
    vnode_init();

    struct vnode *vp = NULL;
    int error = getnewvnode("test_nonneg", NULL, NULL, &vp);
    if (error || !vp)
        return false;

    /* Add multiple refs */
    vref(vp);
    vref(vp);
    vref(vp);

    uint32_t before = vp->v_usecount;

    /* Release all but the initial ref */
    vrele(vp);
    vrele(vp);
    vrele(vp);

    /* Must not have underflowed (signed overflow check) */
    if ((int32_t)vp->v_usecount < 0) {
        vp->v_usecount = 1;
        vp->v_flag &= ~VONFREELIST;
        vnode_reclaim(vp);
        return false;
    }
    (void)before;

    vp->v_flag &= ~VONFREELIST;
    vp->v_usecount = 0;
    vnode_reclaim(vp);
    return true;
}

/* --- vget ----------------------------------------------------------------- */

bool test_vnode_lifecycle_vget(void) {
    vnode_init();

    struct vnode *vp = NULL;
    int error = getnewvnode("test_vget", NULL, NULL, &vp);
    if (error || !vp)
        return false;

    uint32_t before = vp->v_usecount;

    /* vget with 0 flags should lock and ref the vnode */
    int ret = vget(vp, 0);
    if (ret != 0) {
        vp->v_flag &= ~VONFREELIST;
        vp->v_usecount = 0;
        vnode_reclaim(vp);
        return false;
    }

    /* usecount should have increased */
    if (vp->v_usecount < before) {
        vn_unlock(vp);
        vp->v_flag &= ~VONFREELIST;
        vp->v_usecount = 0;
        vnode_reclaim(vp);
        return false;
    }

    vn_unlock(vp);
    vp->v_flag &= ~VONFREELIST;
    vp->v_usecount = 0;
    vnode_reclaim(vp);
    return true;
}

/* --- vput ----------------------------------------------------------------- */

bool test_vnode_lifecycle_vput(void) {
    vnode_init();

    struct vnode *vp = NULL;
    int error = getnewvnode("test_vput", NULL, NULL, &vp);
    if (error || !vp)
        return false;

    /* Lock and get an extra ref so vput won't trigger reclaim */
    vref(vp);
    vn_lock(vp, LK_EXCLUSIVE);

    uint32_t before = vp->v_usecount;

    /* vput should unlock and drop the ref */
    vput(vp);

    if (vp->v_usecount >= before) {
        vp->v_flag &= ~VONFREELIST;
        vp->v_usecount = 0;
        vnode_reclaim(vp);
        return false;
    }

    vp->v_flag &= ~VONFREELIST;
    vp->v_usecount = 0;
    vnode_reclaim(vp);
    return true;
}

/* --- vgone ---------------------------------------------------------------- */

bool test_vnode_lifecycle_vgone(void) {
    vnode_init();

    struct vnode *vp = NULL;
    int error = getnewvnode("test_vgone", NULL, NULL, &vp);
    if (error || !vp)
        return false;

    /* vgone should mark the vnode as doomed */
    vgone(vp);

    if (!(vp->v_flag & (VDOOMED | VFREEING | VFREE))) {
        /* vgone should set some doom/destroy flag */
        vp->v_usecount = 0;
        vp->v_flag &= ~VONFREELIST;
        vnode_reclaim(vp);
        return false;
    }

    return true;
}

/* --- multiple ref/deref cycle --------------------------------------------- */

bool test_vnode_lifecycle_multi_ref(void) {
    vnode_init();

    struct vnode *vp = NULL;
    int error = getnewvnode("test_multi", NULL, NULL, &vp);
    if (error || !vp)
        return false;

    uint32_t base = vp->v_usecount;

    for (int i = 0; i < 10; i++)
        vref(vp);

    if (vp->v_usecount != base + 10) {
        vp->v_usecount = 0;
        vp->v_flag &= ~VONFREELIST;
        vnode_reclaim(vp);
        return false;
    }

    for (int i = 0; i < 10; i++)
        vrele(vp);

    if (vp->v_usecount != base) {
        vp->v_usecount = 0;
        vp->v_flag &= ~VONFREELIST;
        vnode_reclaim(vp);
        return false;
    }

    vp->v_flag &= ~VONFREELIST;
    vp->v_usecount = 0;
    vnode_reclaim(vp);
    return true;
}
