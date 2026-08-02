/*
 * vnode_fsnode.c — bridge the BSD vnode layer onto the live fs_node_t VFS.
 *
 * WHY THIS EXISTS
 *
 * Substrate carries two VFS implementations.  The live one is vfs.c, built
 * around fs_node_t and finddir_fs().  The second — vnode.c, vfs_lookup.c,
 * vfs_cache.c, vfs_mount.c, vnode_ops.c — is the BSD-shaped vnode/namei/
 * namecache design, and until now nothing reached any of it: getnewvnode()
 * was called only from udf_vop_mkdir(), which lives in udf_vnodeops, which
 * nothing dispatched through; namei() only from a vfs_mount() with no
 * callers; rootvnode was only ever assigned NULL.
 *
 * The obvious way to wire it up is to write a vnodeops vector for each of the
 * twelve filesystems (ext2, fat, exfat, minix, sysv, udf, procfs, devfs,
 * sysfs, shmfs, 9p, fuse).  That is a great deal of duplicated work, and it
 * strands the vnode layer behind an all-or-nothing migration: until the LAST
 * filesystem is converted, namei() cannot be used for anything.
 *
 * This file takes the other route.  Every one of those filesystems already
 * implements the operations namei() needs -- finddir, readlink, permissions,
 * attributes -- through fs_node_t.  So a SINGLE vnodeops vector that forwards
 * VOP_* to those hooks makes the vnode layer functional against all of them
 * at once, with no per-filesystem work, and lets callers migrate off
 * fs_node_t one at a time instead of in one jump.
 *
 * A vnode created here holds its fs_node_t in v_data, and THAT POINTER is the
 * vnode's identity.  The obvious key, (mount, inode number), does not work:
 * the bridge has no struct mount to name (v_mount is NULL for every bridged
 * vnode), and devfs, procfs and sysfs hand out nodes with inode 0, so
 * (NULL, 0) would collapse every one of them onto a single vnode -- a lookup
 * of /dev would return the vnode currently backing /proc/uptime.  The
 * fs_node_t address is exact, and it is the same pointer the live VFS returns
 * for repeat lookups of a file, so the vnode hash and the namecache both
 * behave.  It is stashed in v_ino purely as a hash key; nothing reads an
 * inode number out of it (vop_getattr goes to the fs_node_t for that).
 *
 * SCOPE OF THIS INCREMENT: lookup, attributes, access and readlink -- i.e.
 * exactly what namei() calls.  vop_read/vop_write are not forwarded yet
 * because they need the uio machinery, which no caller uses; they report
 * EOPNOTSUPP rather than pretending.
 *
 * ERRNO CONVENTION: negative errno, matching the rest of the kernel, so a
 * value can be handed to the syscall boundary unchanged.  The layer used to
 * be split -- vnode_ops.c / vfs_lookup.c / vfs_mount.c returned POSITIVE
 * errno while vnode.c already returned negative (-EAGAIN, -EBUSY, a bare
 * -12) -- which meant a caller could not tell a failure from a small success
 * without knowing which file it came from.  All of it is negative now.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kern/console.h>
#include <sys/errno.h>
#include <sys/namei.h>
#include <vfs/vfs.h>
#include <vfs/vnode.h>
#include <vm/vm_kmem.h>

/* The fs_node_t behind a bridged vnode. */
#define VTOFSNODE(vp)   ((fs_node_t *)((vp)->v_data))

/*
 * Map an fs_node_t's flags word onto a vnode type.  fs_node_t keeps the type
 * in the low three bits, the same way the rest of vfs.c reads it.
 */
static enum vtype fsnode_to_vtype(const fs_node_t *n)
{
    switch (n->flags & 0x7) {
    case FS_FILE:        return VREG;
    case FS_DIRECTORY:   return VDIR;
    case FS_CHARDEVICE:  return VCHR;
    case FS_BLOCKDEVICE: return VBLK;
    case FS_PIPE:        return VFIFO;
    case FS_SYMLINK:     return VLNK;
    default:             return VNON;
    }
}

/*
 * Obtain a vnode for `n` under mount `mp`, reusing the cached one if this
 * file already has a vnode.  Returns with a reference held on *vpp.
 *
 * The caller keeps ownership of `n`: fs_node_t lifetime is still governed by
 * the live VFS, which is why vop_reclaim below does not free it.  That is the
 * same open node-lifetime question tracked for the live layer, and bridging
 * must not invent a different answer for it.
 */
int fsnode_vget(struct mount *mp, fs_node_t *n, struct vnode **vpp)
{
    struct vnode *vp;
    int error;

    if (!n || !vpp)
        return -EINVAL;

    /* Already have a vnode for this fs_node_t?  See the identity note above:
     * the key is the node address, not its inode number. */
    vp = vnode_lookup_cache(mp, (uint64_t)(uintptr_t)n);
    if (vp) {
        /* vnode_lookup_cache() already took the reference. */
        vp->v_size = n->length;
        *vpp = vp;
        return 0;
    }

    error = getnewvnode("fsnode", mp, &fsnode_vnodeops, &vp);
    if (error)
        return error;

    vp->v_data = n;
    vp->v_type = fsnode_to_vtype(n);
    vp->v_ino  = (uint64_t)(uintptr_t)n;
    vp->v_size = n->length;
    vp->v_rdev = n->rdev;
    vp->v_tag  = VT_NON;         /* the bridge is not a filesystem itself */

    vnode_cache_insert(vp);

    *vpp = vp;
    return 0;
}

/*
 * vop_lookup — resolve one path component.
 *
 * namei() has already split the path, so this is a single-component lookup,
 * which is exactly what finddir_fs() does.  Symlink following and ".."
 * handling stay in namei(); using finddir_fs() (which follows) rather than
 * the non-following variant would resolve links twice.
 */
static int fsnode_vop_lookup(struct vnode *dvp, struct vnode **vpp,
                             struct componentname *cnp)
{
    fs_node_t *dir, *child;
    char name[256];

    if (!dvp || !vpp || !cnp || !cnp->cn_nameptr)
        return -EINVAL;
    if (dvp->v_type != VDIR)
        return -ENOTDIR;

    dir = VTOFSNODE(dvp);
    if (!dir)
        return -EINVAL;

    if (cnp->cn_namelen >= sizeof(name))
        return -ENAMETOOLONG;
    memcpy(name, cnp->cn_nameptr, cnp->cn_namelen);
    name[cnp->cn_namelen] = '\0';

    child = finddir_fs(dir, name);
    if (!child)
        return -ENOENT;

    return fsnode_vget(dvp->v_mount, child, vpp);
}

static int fsnode_vop_getattr(struct vnode *vp, struct vattr *vap,
                              struct ucred *cred)
{
    fs_node_t *n;

    (void)cred;
    if (!vp || !vap)
        return -EINVAL;
    n = VTOFSNODE(vp);
    if (!n)
        return -EINVAL;

    VATTR_NULL(vap);
    vap->va_type      = fsnode_to_vtype(n);
    vap->va_mode      = n->mask;
    vap->va_nlink     = 1;
    vap->va_uid       = n->uid;
    vap->va_gid       = n->gid;
    vap->va_fileid    = n->inode;
    vap->va_size      = n->length;
    vap->va_blocksize = 512;
    vap->va_atime     = n->atime;
    vap->va_mtime     = n->mtime;
    vap->va_ctime     = n->ctime;
    vap->va_rdev      = n->rdev;
    return 0;
}

/*
 * vop_access — defer to the live VFS's permission check so the two layers
 * cannot disagree about who may traverse what.  vfs_check_permissions()
 * answers 0 or -1 ("denied"); translate that into a negative errno.
 */
static int fsnode_vop_access(struct vnode *vp, int mode, struct ucred *cred)
{
    fs_node_t *n;

    (void)cred;
    if (!vp)
        return -EINVAL;
    n = VTOFSNODE(vp);
    if (!n)
        return -EINVAL;

    if (vfs_check_permissions(n, 0, 0, mode) != 0)
        return -EACCES;
    return 0;
}

static int fsnode_vop_readlink(struct vnode *vp, struct uio *uio,
                               struct ucred *cred)
{
    (void)uio; (void)cred;
    if (!vp)
        return -EINVAL;
    if (vp->v_type != VLNK)
        return -EINVAL;
    /*
     * namei() reads link targets through its own path, and no caller drives
     * uio yet.  Report honestly rather than silently returning an empty
     * target, which would look like a link to "".
     */
    return -EOPNOTSUPP;
}

/*
 * vop_inactive / vop_reclaim.
 *
 * The fs_node_t in v_data is NOT freed here.  Its lifetime belongs to the
 * live VFS -- some backends hand out pointers into a node cache, others into
 * a per-mount ring -- and that ownership question is the same one open for
 * the live layer.  Dropping the pointer is all this bridge may safely do.
 */
static int fsnode_vop_inactive(struct vnode *vp, struct ucred *cred)
{
    (void)vp; (void)cred;
    return 0;
}

static int fsnode_vop_reclaim(struct vnode *vp, struct ucred *cred)
{
    (void)cred;
    if (vp)
        vp->v_data = NULL;
    return 0;
}

struct vnodeops fsnode_vnodeops = {
    .vop_lookup   = fsnode_vop_lookup,
    .vop_getattr  = fsnode_vop_getattr,
    .vop_access   = fsnode_vop_access,
    .vop_readlink = fsnode_vop_readlink,
    .vop_inactive = fsnode_vop_inactive,
    .vop_reclaim  = fsnode_vop_reclaim,
    /* Everything else stays NULL: the vop_* wrappers in vnode_ops.c already
     * return EOPNOTSUPP for a missing method, which is the truthful answer
     * for operations this bridge does not yet forward. */
};

/*
 * Publish rootvnode.  Called once the live VFS has a root, which is what
 * makes namei() usable: vfs_lookup.c starts every absolute path at
 * rootvnode, and it was NULL forever.
 */
int vnode_bridge_init(void)
{
    static int subsys_ready = 0;
    struct vnode *rvp = NULL;
    int error;

    if (!fs_root) {
        kprintf("vnode: bridge init skipped, no fs_root yet\n");
        return -EINVAL;
    }
    if (rootvnode) {
        return 0;               /* already published */
    }

    /*
     * Bring the vnode subsystem up.  vnode_init() and nchinit() have no other
     * callers -- the layer was dormant, so nobody ever ran them, and
     * getnewvnode() failed ENOMEM against a NULL uma zone while the namecache
     * LRU had a NULL tqh_last that the first insert would have written
     * through.  They belong here rather than in main.c because this is the
     * only thing that makes the layer reachable; the guard keeps a second
     * bridge_init from re-creating the zone.
     */
    if (!subsys_ready) {
        vnode_init();
        nchinit();
        namei_init();           /* namei_zone, likewise never created */
        subsys_ready = 1;
    }

    error = fsnode_vget(NULL, fs_root, &rvp);
    if (error) {
        kprintf("vnode: bridge init failed to wrap fs_root (%d)\n", error);
        return error;
    }

    rvp->v_type = VDIR;
    rootvnode = rvp;
    kprintf("vnode: bridge up, rootvnode published\n");
    return 0;
}
