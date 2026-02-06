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
vop_lookup(struct vnode *dvp, struct vnode **vpp, const char *name, struct ucred *cred)
{
    /* Check if dvp is a directory */
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_lookup)
        return dvp->v_op->vop_lookup(dvp, vpp, name, cred);
    
    return EOPNOTSUPP;
}

/*
 * vop_cachedlookup:
 * Lookup a component name, checking the cache first.
 */
int
vop_cachedlookup(struct vnode *dvp, struct vnode **vpp, const char *name, struct ucred *cred)
{
    int error;
    size_t len = strlen(name);

    /* Check cache first */
    error = cache_lookup(dvp, vpp, name, len);
    if (error == 0) {
        /* Cache hit - vpp is already set and ref'd by cache_lookup */
        return 0;
    }

    /* Cache miss - perform actual lookup */
    error = vop_lookup(dvp, vpp, name, cred);
    if (error == 0) {
        /* Success - add to cache */
        cache_enter(dvp, *vpp, name, len);
    }

    return error;
}

/*
 * vop_create:
 * Create a new regular file.
 */
int
vop_create(struct vnode *dvp, struct vnode **vpp, const char *name, mode_t mode, struct ucred *cred)
{
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_create)
        return dvp->v_op->vop_create(dvp, vpp, name, mode, cred);

    return EOPNOTSUPP;
}

/*
 * vop_mknod:
 * Create a device node.
 */
int
vop_mknod(struct vnode *dvp, struct vnode **vpp, const char *name, mode_t mode, dev_t dev, struct ucred *cred)
{
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_mknod)
        return dvp->v_op->vop_mknod(dvp, vpp, name, mode, dev, cred);

    return EOPNOTSUPP;
}

/*
 * vop_mkdir:
 * Create a new directory.
 */
int
vop_mkdir(struct vnode *dvp, struct vnode **vpp, const char *name, mode_t mode, struct ucred *cred)
{
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_mkdir)
        return dvp->v_op->vop_mkdir(dvp, vpp, name, mode, cred);

    return EOPNOTSUPP;
}
