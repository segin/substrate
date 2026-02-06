/*
 * VNode Operations
 *
 * This file contains high-level wrappers for VNode operations.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <vfs/vnode.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <string.h>

/*
 * vop_lookup:
 * Lookup a component name in a directory.
 */
int
vop_lookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp)
{
    /* Check if dvp is a directory */
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_lookup)
        return dvp->v_op->vop_lookup(dvp, vpp, cnp);
    
    return EOPNOTSUPP;
}

/*
 * vop_cachedlookup:
 * Lookup a component name, checking the cache first.
 */
int
vop_cachedlookup(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp)
{
    int error;

    /* Check cache first */
    error = cache_lookup(dvp, vpp, cnp->cn_nameptr, cnp->cn_namelen);
    if (error == 0) {
        /* Cache hit - vpp is already set and ref'd by cache_lookup */
        return 0;
    }

    /* Cache miss - perform actual lookup */
    error = vop_lookup(dvp, vpp, cnp);
    if (error == 0) {
        /* Success - add to cache */
        cache_enter(dvp, *vpp, cnp->cn_nameptr, cnp->cn_namelen);
    }

    return error;
}

/*
 * vop_create:
 * Create a new regular file.
 */
int
vop_create(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_create)
        return dvp->v_op->vop_create(dvp, vpp, cnp, vap);

    return EOPNOTSUPP;
}

/*
 * vop_mknod:
 * Create a device node.
 */
int
vop_mknod(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_mknod)
        return dvp->v_op->vop_mknod(dvp, vpp, cnp, vap);

    return EOPNOTSUPP;
}

/*
 * vop_mkdir:
 * Create a new directory.
 */
int
vop_mkdir(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp, struct vattr *vap)
{
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_mkdir)
        return dvp->v_op->vop_mkdir(dvp, vpp, cnp, vap);

    return EOPNOTSUPP;
}

/*
 * vop_remove:
 * Remove a file.
 */
int
vop_remove(struct vnode *dvp, struct vnode *vp, struct componentname *cnp)
{
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_remove)
        return dvp->v_op->vop_remove(dvp, vp, cnp);

    return EOPNOTSUPP;
}

/*
 * vop_rmdir:
 * Remove a directory.
 */
int
vop_rmdir(struct vnode *dvp, struct vnode *vp, struct componentname *cnp)
{
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_rmdir)
        return dvp->v_op->vop_rmdir(dvp, vp, cnp);

    return EOPNOTSUPP;
}

/*
 * vop_whiteout:
 * Create/delete/lookup a whiteout entry.
 */
int
vop_whiteout(struct vnode *dvp, struct componentname *cnp, int flags)
{
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_whiteout)
        return dvp->v_op->vop_whiteout(dvp, cnp, flags);

    return EOPNOTSUPP;
}
