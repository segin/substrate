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

/*
 * vop_access:
 * Check if the given credentials have access to the vnode.
 */
int
vop_access(struct vnode *vp, int mode, struct ucred *cred)
{
    if (vp->v_op && vp->v_op->vop_access)
        return vp->v_op->vop_access(vp, mode, cred);

    /* Generic fallback */
    struct vattr va;
    int error = VOP_GETATTR(vp, &va, cred);
    if (error)
        return error;

    /* Root always has access */
    if (cred->cr_uid == 0)
        return 0;

    int mask = 0;
    if (cred->cr_uid == va.va_uid) {
        if (mode & 4) mask |= 0400;
        if (mode & 2) mask |= 0200;
        if (mode & 1) mask |= 0100;
    } else {
        int is_group = 0;
        if (cred->cr_gid == va.va_gid) {
            is_group = 1;
        } else {
            for (int i = 0; i < (int)cred->cr_ngroups; i++) {
                if (cred->cr_groups[i] == va.va_gid) {
                    is_group = 1;
                    break;
                }
            }
        }

        if (is_group) {
            if (mode & 4) mask |= 0040;
            if (mode & 2) mask |= 0020;
            if (mode & 1) mask |= 0010;
        } else {
            if (mode & 4) mask |= 0004;
            if (mode & 2) mask |= 0002;
            if (mode & 1) mask |= 0001;
        }
    }

    return (va.va_mode & mask) == mask ? 0 : -13; /* EACCES */
}

/*
 * vop_getattr:
 * Get vnode attributes.
 */
int
vop_getattr(struct vnode *vp, struct vattr *vap, struct ucred *cred)
{
    if (vp->v_op && vp->v_op->vop_getattr)
        return vp->v_op->vop_getattr(vp, vap, cred);

    return EOPNOTSUPP;
}

/*
 * vop_setattr:
 * Set vnode attributes.
 */
int
vop_setattr(struct vnode *vp, struct vattr *vap, struct ucred *cred)
{
    if (vp->v_op && vp->v_op->vop_setattr)
        return vp->v_op->vop_setattr(vp, vap, cred);

    return EOPNOTSUPP;
}

/*
 * vop_pathconf:
 * Get configurable pathname variables.
 */
int
vop_pathconf(struct vnode *vp, int name, register_t *retval)
{
    if (vp->v_op && vp->v_op->vop_pathconf)
        return vp->v_op->vop_pathconf(vp, name, retval);

    return EOPNOTSUPP;
}

/*
 * vop_readdir:
 * Read directory entries.
 */
int
vop_readdir(struct vnode *vp, struct uio *uio, struct ucred *cred, int *eofflag, int *ncookies, uint64_t **cookies)
{
    if (vp->v_type != VDIR)
        return ENOTDIR;

    if (vp->v_op && vp->v_op->vop_readdir)
        return vp->v_op->vop_readdir(vp, uio, cred, eofflag, ncookies, cookies);

    return EOPNOTSUPP;
}
