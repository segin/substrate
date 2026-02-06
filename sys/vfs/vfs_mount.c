#include <sys/types.h>
#include <sys/mount.h>
#include <vfs/vnode.h>
#include <sys/namei.h>
#include <vm/vm_kmem.h>
#include <sys/errno.h>
#include <string.h>
#include <kern/console.h>

extern struct mountlist mountlist;
extern struct vnode *rootvnode;

/*
 * vfs_mount:
 * Common entry point for mounting filesystems.
 */
int
vfs_mount(struct mount *mp, const char *path, void *data, struct nameidata *ndp, struct thread *td)
{
    struct vnode *vp;
    int error;

    /*
     * Lookup the mount point
     */
    NDINIT(ndp, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, path);
    error = namei(ndp);
    if (error)
        return error;
    
    vp = ndp->ni_vp;

    /*
     * Validity checks
     */
    if (vp->v_type != VDIR) {
        vrele(vp);
        return ENOTDIR;
    }

    if (vp->v_mountedhere != NULL) {
        vrele(vp);
        return EBUSY; /* Already mounted */
    }

    /*
     * Allocate and initialize the mount structure
     * (Normally mp serves as a template or is allocated here)
     */
    if (mp == NULL) {
        mp = kmalloc(sizeof(struct mount));
        if (mp == NULL) {
            vrele(vp);
            return ENOMEM;
        }
        memset(mp, 0, sizeof(struct mount));
    }
    
    /* 
     * Initialize mount structure 
     */
    TAILQ_INIT(&mp->mnt_vnodelist);
    mp->mnt_vnodecovered = vp;
    
    /*
     * In a full implementation, we would look up the filesystem type
     * in 'vfsconf' and call its vfs_mount.
     * For now, we assume 'mp' might already have ops or we need to find them.
     * This is a skeleton for the generic logic.
     */
    
    /*
     * Call filesystem specific mount
     * (Assuming mp->mnt_op is correctly set by caller or lookup)
     */
     if (mp->mnt_op && mp->mnt_op->vfs_mount) {
         error = mp->mnt_op->vfs_mount(mp, path, data, ndp, td);
         if (error) {
             vrele(vp);
             kfree(mp, sizeof(struct mount));
             return error;
         }
     }

    /*
     * Success: link into the directory tree
     */
    vp->v_mountedhere = mp;
    
    /* Add to global mount list */
    TAILQ_INSERT_TAIL(&mountlist, mp, mnt_list);

    return 0;
}

/*
 * vfs_unmount:
 * Common entry point for unmounting filesystems.
 */
int
vfs_unmount(struct mount *mp, int mntflags, struct thread *td)
{
    int error;
    
    /*
     * Check if filesystem is busy (open files, etc.)
     */
    if ((mntflags & MNT_FORCE) == 0) {
        struct vnode *vp;
        TAILQ_FOREACH(vp, &mp->mnt_vnodelist, v_mntlist) {
            if (vp->v_usecount > 0)
                return EBUSY;
        }
    }

    /*
     * Call filesystem specific unmount
     */
    if (mp->mnt_op && mp->mnt_op->vfs_unmount) {
        error = mp->mnt_op->vfs_unmount(mp, mntflags, td);
        if (error)
            return error;
    }

    /*
     * Unlink from directory tree
     */
    if (mp->mnt_vnodecovered) {
        mp->mnt_vnodecovered->v_mountedhere = NULL;
        vrele(mp->mnt_vnodecovered);
    }

    /* Remove from global list and free */
    TAILQ_REMOVE(&mountlist, mp, mnt_list);
    kfree(mp, sizeof(struct mount));

    return 0;
}
