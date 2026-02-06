/*
 * vnode.h - Virtual Node (vnode) definitions
 *
 * BSD-style vnode layer providing:
 * - Unified interface for all filesystem objects
 * - Reference counting for proper lifecycle management
 * - Locking for concurrent access control
 * - Operations vector for polymorphic behavior
 */

#ifndef _SYS_VNODE_H
#define _SYS_VNODE_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/lock.h>

/* Forward declarations */
struct vnode;
struct vnodeops;
struct mount;
struct uio;
struct ucred;
struct vattr;

/*
 * Vnode types
 */
enum vtype {
    VNON,       /* No type */
    VREG,       /* Regular file */
    VDIR,       /* Directory */
    VBLK,       /* Block device */
    VCHR,       /* Character device */
    VLNK,       /* Symbolic link */
    VSOCK,      /* Socket */
    VFIFO,      /* FIFO (named pipe) */
    VBAD        /* Bad/invalid */
};

/*
 * Vnode tags - identifies the filesystem type
 */
enum vtagtype {
    VT_NON,     /* Not a filesystem */
    VT_UFS,     /* Unix File System */
    VT_NFS,     /* Network File System */
    VT_EXT2,    /* ext2/ext3/ext4 */
    VT_FAT,     /* FAT/FAT32 */
    VT_EXFAT,   /* exFAT */
    VT_MINIX,   /* Minix filesystem */
    VT_UDF,     /* Universal Disk Format */
    VT_DEVFS,   /* Device filesystem */
    VT_PROCFS,  /* Process filesystem */
    VT_SYSFS,   /* System filesystem */
    VT_TMPFS,   /* Temporary filesystem */
    VT_9P,      /* Plan 9 protocol */
    VT_FUSE     /* FUSE filesystem */
};

/*
 * Vnode flags
 */
#define VROOT       0x0001  /* Root of its filesystem */
#define VTEXT       0x0002  /* Vnode is a pure text prototype */
#define VSYSTEM     0x0004  /* System vnode (bypass security) */
#define VISTTY      0x0008  /* Vnode represents a tty */
#define VEXEC       0x0010  /* File is being exec'd (execute in progress) */
#define VNOCACHE    0x0020  /* Don't cache this vnode */
#define VFREEING    0x0040  /* Vnode is being freed */
#define VDOOMED     0x0080  /* Vnode is doomed (being destroyed) */
#define VFREE       0x0100  /* Vnode is on free list */
#define VONFREELIST 0x0200  /* Vnode is on LRU free list */
#define VXLOCK      0x0400  /* Exclusive lock held */
#define VXWANT      0x0800  /* Want exclusive lock */
#define VMODIFIED   0x1000  /* Vnode has been modified */
#define VACCESSTIME 0x2000  /* Access time needs update */

/*
 * Lock operations for vn_lock
 */
#define LK_SHARED       0x01    /* Shared lock */
#define LK_EXCLUSIVE    0x02    /* Exclusive lock */
#define LK_NOWAIT       0x10    /* Don't wait for lock */
#define LK_RETRY        0x20    /* Retry on failure */

/*
 * The vnode structure
 */
struct vnode {
    /* Type and identification */
    enum vtype      v_type;         /* Vnode type */
    enum vtagtype   v_tag;          /* Filesystem type tag */
    
    /* Operations and data */
    struct vnodeops *v_op;          /* Operations vector */
    void            *v_data;        /* Private filesystem data */
    
    /* Mount relationship */
    struct mount    *v_mount;       /* Pointer to mount point */
    struct vnode    *v_mountedhere; /* Mounted filesystem's root (if mount point) */
    
    /* Reference counting */
    uint32_t        v_usecount;     /* Active references (holds vnode in use) */
    uint32_t        v_holdcount;    /* Weak references (for caching) */
    uint32_t        v_writecount;   /* Number of write opens */
    
    /* Flags and state */
    uint32_t        v_flag;         /* Vnode flags */
    
    /* Locking */
    spinlock_t      v_interlock;    /* Protects vnode fields */
    uint32_t        v_lockstate;    /* Current lock state */
    struct thread   *v_lockowner;   /* Current exclusive lock owner */
    
    /* LRU list linkage (for vnode cache) */
    struct vnode    *v_freelist_next;
    struct vnode    *v_freelist_prev;
    
    /* Hash chain for vnode lookup */
    struct vnode    *v_hash_next;
    
    /* Filesystem-specific cached info */
    uint64_t        v_id;           /* Capability identifier (generation) */
    uint64_t        v_ino;          /* Inode number (if applicable) */
    dev_t           v_rdev;         /* Device number (for VBLK/VCHR) */
    off_t           v_size;         /* File size in bytes */
};

/*
 * Vnode operations vector
 * Each filesystem implements these operations
 */
struct vnodeops {
    int (*vop_lookup)(struct vnode *dvp, struct vnode **vpp, 
                      const char *name, struct ucred *cred);
    int (*vop_create)(struct vnode *dvp, struct vnode **vpp,
                      const char *name, int mode, struct ucred *cred);
    int (*vop_open)(struct vnode *vp, int mode, struct ucred *cred);
    int (*vop_close)(struct vnode *vp, int fflag, struct ucred *cred);
    int (*vop_read)(struct vnode *vp, struct uio *uio, int ioflag,
                    struct ucred *cred);
    int (*vop_write)(struct vnode *vp, struct uio *uio, int ioflag,
                     struct ucred *cred);
    int (*vop_ioctl)(struct vnode *vp, unsigned long command, void *data,
                     int fflag, struct ucred *cred);
    int (*vop_getattr)(struct vnode *vp, struct vattr *vap,
                       struct ucred *cred);
    int (*vop_setattr)(struct vnode *vp, struct vattr *vap,
                       struct ucred *cred);
    int (*vop_access)(struct vnode *vp, int mode, struct ucred *cred);
    int (*vop_readlink)(struct vnode *vp, struct uio *uio,
                        struct ucred *cred);
    int (*vop_symlink)(struct vnode *dvp, struct vnode **vpp,
                       const char *name, const char *target,
                       struct ucred *cred);
    int (*vop_link)(struct vnode *tdvp, struct vnode *vp,
                    const char *name, struct ucred *cred);
    int (*vop_remove)(struct vnode *dvp, struct vnode *vp,
                      const char *name, struct ucred *cred);
    int (*vop_rename)(struct vnode *fdvp, struct vnode *fvp,
                      const char *fname, struct vnode *tdvp,
                      struct vnode *tvp, const char *tname,
                      struct ucred *cred);
    int (*vop_mkdir)(struct vnode *dvp, struct vnode **vpp,
                     const char *name, int mode, struct ucred *cred);
    int (*vop_rmdir)(struct vnode *dvp, struct vnode *vp,
                     const char *name, struct ucred *cred);
    int (*vop_readdir)(struct vnode *vp, struct uio *uio,
                       struct ucred *cred, int *eofflag);
    int (*vop_fsync)(struct vnode *vp, int waitfor, struct ucred *cred);
    int (*vop_inactive)(struct vnode *vp, struct ucred *cred);
    int (*vop_reclaim)(struct vnode *vp, struct ucred *cred);
    int (*vop_strategy)(struct vnode *vp, void *bp);
    int (*vop_bmap)(struct vnode *vp, off_t offset, struct vnode **vpp,
                    off_t *bnp, int *runp, int *runb);
    int (*vop_print)(struct vnode *vp);
};

/*
 * Vnode attributes (for getattr/setattr)
 */
struct vattr {
    enum vtype  va_type;        /* Vnode type */
    mode_t      va_mode;        /* File mode */
    nlink_t     va_nlink;       /* Number of references */
    uid_t       va_uid;         /* Owner user id */
    gid_t       va_gid;         /* Owner group id */
    dev_t       va_fsid;        /* Filesystem id */
    uint64_t    va_fileid;      /* File id (inode) */
    off_t       va_size;        /* File size in bytes */
    blksize_t   va_blocksize;   /* Block size */
    int64_t     va_atime;       /* Access time */
    int64_t     va_mtime;       /* Modification time */
    int64_t     va_ctime;       /* Change time */
    int64_t     va_birthtime;   /* Creation time */
    uint64_t    va_gen;         /* Generation number */
    uint32_t    va_flags;       /* Flags */
    dev_t       va_rdev;        /* Device number (for dev nodes) */
    uint64_t    va_bytes;       /* Bytes of disk space used */
    uint64_t    va_filerev;     /* File revision number */
    uint32_t    va_vaflags;     /* Operations flags */
};

/* va_vaflags */
#define VA_UTIMES_NULL  0x01    /* utimes with NULL times */
#define VA_EXCLUSIVE    0x02    /* O_EXCL create */

/*
 * Mount structure (partial, for reference)
 */
struct mount {
    struct vnode        *mnt_vnodecovered;  /* Vnode we mounted on */
    struct vnode        *mnt_root;          /* Root vnode of this fs */
    uint32_t            mnt_flag;           /* Mount flags */
    const char          *mnt_type;          /* Filesystem type name */
    void                *mnt_data;          /* Private filesystem data */
    struct mount        *mnt_next;          /* Next in global mount list */
};

/* Mount flags */
#define MNT_RDONLY      0x0001  /* Read only filesystem */
#define MNT_NOSUID      0x0002  /* Setuid/setgid forbidden */
#define MNT_NODEV       0x0004  /* Device nodes forbidden */
#define MNT_NOEXEC      0x0008  /* Execution forbidden */
#define MNT_SYNCHRONOUS 0x0010  /* Synchronous writes */
#define MNT_ASYNC       0x0020  /* Asynchronous writes */
#define MNT_LOCAL       0x1000  /* Local filesystem */
#define MNT_QUOTA       0x2000  /* Quotas enabled */

/*
 * Vnode lifecycle functions
 */

/* Allocate a new vnode from the vnode zone */
int getnewvnode(const char *tag, struct mount *mp, 
                struct vnodeops *vops, struct vnode **vpp);

/* Increment use count (acquire reference) */
void vref(struct vnode *vp);

/* Decrement use count, trigger inactive/reclaim if zero */
void vrele(struct vnode *vp);

/* Unlock and vrele */
void vput(struct vnode *vp);

/* Lock and then vref (acquire locked reference) */
int vget(struct vnode *vp, int flags);

/* Mark vnode for doom/destruction */
void vgone(struct vnode *vp);

/* Disassociate vnode from filesystem data */
void vclean(struct vnode *vp, int flags);

/* Increment hold count (weak reference for caching) */
void vhold(struct vnode *vp);

/* Decrement hold count */
void vdrop(struct vnode *vp);

/* Lock a vnode */
int vn_lock(struct vnode *vp, int flags);

/* Unlock a vnode */
void vn_unlock(struct vnode *vp);

/* Check if vnode is locked */
int vn_islocked(struct vnode *vp);

/*
 * Vnode cache management
 */
void vnode_init(void);
void vnode_fini(void);
int vnode_create(enum vtype type, struct mount *mp, 
                 struct vnodeops *ops, struct vnode **vpp);
void vnode_reclaim(struct vnode *vp);

/*
 * Vnode lookup cache
 */
struct vnode *vnode_lookup_cache(struct mount *mp, uint64_t ino);
void vnode_cache_insert(struct vnode *vp);
void vnode_cache_remove(struct vnode *vp);

/*
 * Vnode statistics
 */
struct vnode_stats {
    uint32_t    numvnodes;      /* Total vnodes in system */
    uint32_t    freevnodes;     /* Vnodes on free list */
    uint32_t    vnode_alloc;    /* Vnode allocations */
    uint32_t    vnode_recycle;  /* Vnode recycles */
    uint32_t    vnode_free;     /* Vnode frees */
};

extern struct vnode_stats vnstats;

/*
 * Helper macros
 */
#define VATTR_NULL(vap) do { \
    (vap)->va_type = VNON; \
    (vap)->va_mode = (mode_t)VNOVAL; \
    (vap)->va_nlink = VNOVAL; \
    (vap)->va_uid = (uid_t)VNOVAL; \
    (vap)->va_gid = (gid_t)VNOVAL; \
    (vap)->va_size = VNOVAL; \
    (vap)->va_atime = VNOVAL; \
    (vap)->va_mtime = VNOVAL; \
    (vap)->va_ctime = VNOVAL; \
} while (0)

#define VNOVAL  (-1)    /* Attribute not set / no change */

/* Type checking macros */
#define VREG_P(vp)  ((vp)->v_type == VREG)
#define VDIR_P(vp)  ((vp)->v_type == VDIR)
#define VBLK_P(vp)  ((vp)->v_type == VBLK)
#define VCHR_P(vp)  ((vp)->v_type == VCHR)
#define VLNK_P(vp)  ((vp)->v_type == VLNK)

/* Vnode reference macros for assertions */
#define VREF_ASSERT(vp) do { \
    if ((vp)->v_usecount == 0) \
        panic("vnode has zero usecount"); \
} while (0)

#endif /* _SYS_VNODE_H */
