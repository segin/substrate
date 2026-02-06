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
