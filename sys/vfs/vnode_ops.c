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
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/poll.h>
#include <string.h>

#ifndef IO_APPEND
#define IO_APPEND O_APPEND
#endif

#ifndef IO_SYNC
#define IO_SYNC O_SYNC
#endif

#ifndef IO_UNIT
#define IO_UNIT 0x0100
#endif

/* MNT_WAIT and MNT_NOWAIT defined in <sys/mount.h> */

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
 * vop_link:
 * Link a new name to an existing vnode.
 */
int
vop_link(struct vnode *tdvp, struct vnode *vp, struct componentname *cnp)
{
    if (tdvp->v_type != VDIR)
        return ENOTDIR;

    if (vp->v_type == VDIR)
        return EPERM;

    if (tdvp->v_op && tdvp->v_op->vop_link)
        return tdvp->v_op->vop_link(tdvp, vp, cnp);

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
 * vop_open:
 * Open a vnode for I/O.
 */
int
vop_open(struct vnode *vp, int mode, struct ucred *cred)
{
    int error;
    int access_mode;

    access_mode = 0;
    switch (mode & O_ACCMODE) {
    case O_WRONLY:
        access_mode = 2;
        break;
    case O_RDWR:
        access_mode = 6;
        break;
    case O_RDONLY:
    default:
        access_mode = 4;
        break;
    }

    error = vop_access(vp, access_mode, cred);
    if (error)
        return error;

    if (vp->v_op && vp->v_op->vop_open) {
        error = vp->v_op->vop_open(vp, mode, cred);
        if (error)
            return error;
    }

    spinlock_acquire(&vp->v_interlock);
    if ((mode & O_ACCMODE) == O_WRONLY || (mode & O_ACCMODE) == O_RDWR)
        vp->v_writecount++;
    vp->v_usecount++;
    spinlock_release(&vp->v_interlock);

    return 0;
}

/*
 * vop_close:
 * Close a vnode and flush dirty data when last writer closes.
 */
int
vop_close(struct vnode *vp, int fflag, struct ucred *cred)
{
    int error;
    int do_fsync;

    if (vp->v_op && vp->v_op->vop_close) {
        error = vp->v_op->vop_close(vp, fflag, cred);
        if (error)
            return error;
    }

    do_fsync = 0;
    spinlock_acquire(&vp->v_interlock);
    if ((fflag & O_ACCMODE) == O_WRONLY || (fflag & O_ACCMODE) == O_RDWR) {
        if (vp->v_writecount > 0)
            vp->v_writecount--;
        if (vp->v_writecount == 0)
            do_fsync = 1;
    }
    if (vp->v_usecount > 0)
        vp->v_usecount--;
    spinlock_release(&vp->v_interlock);

    if (do_fsync)
        return vop_fsync(vp, MNT_WAIT, cred);

    return 0;
}

/*
 * vop_read:
 * Read data from vnode and update access timestamp state.
 */
int
vop_read(struct vnode *vp, struct uio *uio, int ioflag, struct ucred *cred)
{
    size_t resid_before;
    int error;

    (void)ioflag;

    if (uio == NULL || uio->uio_iov == NULL || uio->uio_iovcnt <= 0)
        return EINVAL;

    if (uio->uio_resid == 0)
        return 0;

    if ((vp->v_type == VREG || vp->v_type == VDIR) && uio->uio_offset >= vp->v_size)
        return 0;

    if (!(vp->v_op && vp->v_op->vop_read))
        return EOPNOTSUPP;

    resid_before = uio->uio_resid;
    error = vp->v_op->vop_read(vp, uio, ioflag, cred);
    if (error)
        return error;

    if (uio->uio_resid < resid_before)
        vp->v_flag |= VACCESSTIME;

    return 0;
}

/*
 * vop_write:
 * Write data to vnode, honoring append/sync semantics.
 */
int
vop_write(struct vnode *vp, struct uio *uio, int ioflag, struct ucred *cred)
{
    size_t resid_before;
    off_t offset_before;
    int error;

    if (uio == NULL || uio->uio_iov == NULL || uio->uio_iovcnt <= 0)
        return EINVAL;

    if (!(vp->v_op && vp->v_op->vop_write))
        return EOPNOTSUPP;

    if (ioflag & IO_APPEND)
        uio->uio_offset = vp->v_size;

    resid_before = uio->uio_resid;
    offset_before = uio->uio_offset;
    error = vp->v_op->vop_write(vp, uio, ioflag, cred);
    if (error)
        return error;

    if (uio->uio_resid < resid_before) {
        off_t bytes_written;

        bytes_written = (off_t)(resid_before - uio->uio_resid);
        if (uio->uio_offset > vp->v_size)
            vp->v_size = uio->uio_offset;
        else if (offset_before + bytes_written > vp->v_size)
            vp->v_size = offset_before + bytes_written;
        vp->v_flag |= VMODIFIED;
    }

    if (ioflag & IO_SYNC)
        return vop_fsync(vp, MNT_WAIT, cred);

    return 0;
}

/*
 * vop_ioctl:
 * Issue a device/file specific ioctl.
 */
int
vop_ioctl(struct vnode *vp, uint32_t command, void *data, int fflag, struct ucred *cred)
{
    if (vp->v_op && vp->v_op->vop_ioctl)
        return vp->v_op->vop_ioctl(vp, command, data, fflag, cred);

    return EOPNOTSUPP;
}

/*
 * vop_poll:
 * Poll/select readiness for a vnode.
 */
int
vop_poll(struct vnode *vp, int events, struct ucred *cred)
{
    if (vp->v_op && vp->v_op->vop_poll)
        return vp->v_op->vop_poll(vp, events, cred);

    (void)cred;
    return events & (POLLIN | POLLOUT | POLLHUP | POLLERR);
}

/*
 * vop_fsync:
 * Flush pending dirty data to storage.
 */
int
vop_fsync(struct vnode *vp, int waitfor, struct ucred *cred)
{
    int error;

    if (waitfor != MNT_WAIT && waitfor != MNT_NOWAIT)
        return EINVAL;

    if (vp->v_op && vp->v_op->vop_fsync) {
        error = vp->v_op->vop_fsync(vp, waitfor, cred);
        if (error)
            return error;
    }

    vp->v_flag &= ~VMODIFIED;
    return 0;
}

/*
 * vop_bmap:
 * Map vnode logical blocks to physical blocks.
 */
int
vop_bmap(struct vnode *vp, off_t offset, struct vnode **vpp, uint64_t *bnp,
    int *runp, int *runb)
{
    if (vp->v_op && vp->v_op->vop_bmap)
        return vp->v_op->vop_bmap(vp, offset, vpp, bnp, runp, runb);

    if (bnp == NULL)
        return EINVAL;

    if (vpp)
        *vpp = vp;
    *bnp = (uint64_t)offset;
    if (runp)
        *runp = 0;
    if (runb)
        *runb = 0;
    return 0;
}

/*
 * vop_strategy:
 * Submit an I/O request strategy call to filesystem/device.
 */
int
vop_strategy(struct vnode *vp, void *bp)
{
    if (bp == NULL)
        return EINVAL;

    if (vp->v_op && vp->v_op->vop_strategy)
        return vp->v_op->vop_strategy(vp, bp);

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


/*
 * vop_rename:
 * Rename a file or directory.
 */
int
vop_rename(struct vnode *fdvp, struct vnode *fvp, struct componentname *fcnp,
           struct vnode *tdvp, struct vnode *tvp, struct componentname *tcnp)
{
    if (fdvp->v_type != VDIR || tdvp->v_type != VDIR)
        return ENOTDIR;

    if (fdvp->v_mount != tdvp->v_mount)
        return EXDEV;

    if (fdvp->v_op && fdvp->v_op->vop_rename)
        return fdvp->v_op->vop_rename(fdvp, fvp, fcnp, tdvp, tvp, tcnp);

    return EOPNOTSUPP;
}

/*
 * vop_symlink:
 * Create a symbolic link.
 */
int
vop_symlink(struct vnode *dvp, struct vnode **vpp, struct componentname *cnp,
            struct vattr *vap, const char *target)
{
    if (dvp->v_type != VDIR)
        return ENOTDIR;

    if (dvp->v_op && dvp->v_op->vop_symlink)
        return dvp->v_op->vop_symlink(dvp, vpp, cnp, vap, target);

    return EOPNOTSUPP;
}

/*
 * vop_readlink:
 * Read the target of a symbolic link.
 */
int
vop_readlink(struct vnode *vp, struct uio *uio, struct ucred *cred)
{
    if (vp->v_type != VLNK)
        return EINVAL;

    if (vp->v_op && vp->v_op->vop_readlink)
        return vp->v_op->vop_readlink(vp, uio, cred);

    return EOPNOTSUPP;
}

/*
 * vop_inactive:
 * Mark vnode as inactive.
 */
int
vop_inactive(struct vnode *vp, struct ucred *cred)
{
    if (vp->v_op && vp->v_op->vop_inactive)
        return vp->v_op->vop_inactive(vp, cred);

    return 0; /* Not an error if no implementation */
}

/*
 * vop_reclaim:
 * Reclaim vnode from filesystem.
 */
int
vop_reclaim(struct vnode *vp, struct ucred *cred)
{
    if (vp->v_op && vp->v_op->vop_reclaim)
        return vp->v_op->vop_reclaim(vp, cred);

    return 0;
}

/*
 * vop_print:
 * Print vnode details for debugging.
 */
int
vop_print(struct vnode *vp)
{
    if (vp->v_op && vp->v_op->vop_print)
        return vp->v_op->vop_print(vp);

    return EOPNOTSUPP;
}

/*
 * vop_lock:
 * Acquire the vnode lock.
 */
int
vop_lock(struct vnode *vp, int flags) {
    if(vp->v_op && vp->v_op->vop_lock)
        return vp->v_op->vop_lock(vp, flags);

    return vn_lock(vp, flags);
}

/*
 * vop_unlock:
 * Release the vnode lock.
 */
int
vop_unlock(struct vnode *vp, int flags) {
    if(vp->v_op && vp->v_op->vop_unlock)
        return vp->v_op->vop_unlock(vp, flags);

    (void)flags;
    vn_unlock(vp);
    return 0;
}

/*
 * vop_islocked:
 * Query the lock status of a vnode.
 */
int
vop_islocked(struct vnode *vp) {
    if(vp->v_op && vp->v_op->vop_islocked)
        return vp->v_op->vop_islocked(vp);

    return vp->v_lockstate;
}

/*
 * vop_advlock:
 * POSIX advisory locking (F_GETLK/F_SETLK/F_SETLKW).
 */
int
vop_advlock(struct vnode *vp, void *id, int op, void *fl, int flags) {
    if(vp->v_op && vp->v_op->vop_advlock)
        return vp->v_op->vop_advlock(vp, id, op, fl, flags);

    (void)id;
    (void)op;
    (void)fl;
    (void)flags;
    return EOPNOTSUPP;
}
