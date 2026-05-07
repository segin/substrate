#ifndef _SYS_MOUNT_H
#define _SYS_MOUNT_H

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <stdint.h>
#include <stddef.h>

#ifndef __cplusplus
/* C2x has _Static_assert as a keyword; on older compilers we use the
 * historic _Static_assert macro shipped by the C11 runtime header.
 * Either way it's a compile-time check with zero runtime cost. */
#define _MOUNT_SASSERT(cond, msg) _Static_assert((cond), msg)
#else
#define _MOUNT_SASSERT(cond, msg) static_assert((cond), msg)
#endif

struct vnode;
struct fs_node;
struct nameidata;
struct thread;
struct ucred;
struct fid;
struct vfsconf;

/*
 * Versioned mount-args blob.  Userspace passes one of these through the
 * `data` parameter of sys_mount() to communicate filesystem-specific
 * options without burning a new syscall every time we add a tunable.
 *
 * `mnt_arg_version` is bumped whenever the layout below changes;
 * filesystem-specific args follow inline (use offsetof to skip past
 * the header).
 */
struct mount_args {
    uint32_t mnt_arg_version;   /* MOUNT_ARGS_VERSION_* */
    uint32_t mnt_arg_size;      /* sizeof entire blob, header included */
    uint32_t mnt_arg_flags;     /* mirror of MNT_* */
    uint32_t mnt_arg_reserved;  /* must be 0 */
    /* filesystem-specific bytes follow */
};

#define MOUNT_ARGS_VERSION_1 1u

/*
 * Filesystem statistics
 */
struct statfs {
    uint32_t    f_type;         /* type of filesystem */
    uint64_t    f_bsize;        /* optimal transfer block size */
    uint64_t    f_iosize;       /* optimal transfer block size */
    uint64_t    f_blocks;       /* total data blocks in filesystem */
    uint64_t    f_bfree;        /* free blocks in fs */
    uint64_t    f_bavail;       /* free blocks avail to non-superuser */
    uint64_t    f_files;        /* total file nodes in filesystem */
    uint64_t    f_ffree;        /* free file nodes in fs */
    int64_t     f_fsid;         /* filesystem id */
    uid_t       f_owner;        /* user that mounted the filesystem */
    short       f_flags;        /* copy of mount exported flags */
    short       f_syncwrites;   /* count of sync writes since mount */
    short       f_asyncwrites;  /* count of async writes since mount */
    char        f_fstypename[16]; /* fs type name */
    char        f_mntonname[128]; /* directory on which mounted */
    char        f_mntfromname[128]; /* mounted filesystem */
};

TAILQ_HEAD(vnode_list, vnode);

/*
 * Mount flags.
 */
#define	MNT_RDONLY	0x00000001	/* read only filesystem */
#define	MNT_SYNCHRONOUS	0x00000002	/* file system written synchronously */
#define	MNT_NOEXEC	0x00000004	/* can't exec from filesystem */
#define	MNT_NOSUID	0x00000008	/* don't honor setuid bits on fs */
#define	MNT_NODEV	0x00000010	/* don't interpret special files */
#define	MNT_UNION	0x00000020	/* union with underlying filesystem */
#define	MNT_ASYNC	0x00000040	/* file system written asynchronously */
#define	MNT_LOCAL	0x00001000	/* filesystem is stored locally */

/*
 * Exported mount flags.
 */
#define	MNT_EXRDONLY	0x00000080	/* exported read only */
#define	MNT_EXPORTED	0x00000100	/* file system is exported */
#define	MNT_DEFEXPORTED	0x00000200	/* exported to the world */
#define	MNT_EXPORTANON	0x00000400	/* use anon uid mapping for everyone */
#define	MNT_EXKERB	0x00000800	/* exported with Kerberos uid mapping */

/*
 * Flags for sys_mount().
 */
#define	MNT_UPDATE	0x00010000	/* not a real mount, just an update */
#define	MNT_DELEXPORT	0x00020000	/* delete export host lists */
#define	MNT_RELOAD	0x00040000	/* reload filesystem data */
#define	MNT_FORCE	0x00080000	/* force unmount or downgrade */

/*
 * Flags for vfs_sync waitfor.
 */
#define MNT_WAIT	1		/* synchronous wait */
#define MNT_NOWAIT	2		/* asynchronous, start write */

/*
 * Structure per mounted file system. Each mounted file system has an
 * array of operations and an instance record. The file systems are
 * maintained on a doubly linked list.
 */
TAILQ_HEAD(mountlist, mount);
extern struct mountlist mountlist;

struct mount {
	TAILQ_ENTRY(mount) mnt_list;		/* mount list */
	struct vfsops	    *mnt_op;		    /* operations on fs */
    struct vnode        *mnt_vnodecovered;  /* Vnode we mounted on */
    struct vnode        *mnt_root;          /* root vnode of this fs */
    struct vnode        *mnt_syncer;        /* syncer vnode */
    void                *mnt_data;          /* private data */
    char                mnt_stat_path[128]; /* mounted path (debug/stat) */
    uint32_t            mnt_flag;           /* flags */
    struct vnode_list   mnt_vnodelist;      /* list of active vnodes */
    struct statfs       mnt_stat;           /* cached filesystem statistics */
    int                 mnt_maxsymlinklen;  /* max symlink target inline */
    rwlock_t            mnt_lock;           /* mount-level reader/writer lock */
    struct fs_node      *mnt_node_covered;  /* Legacy: Node we mounted on */
    struct fs_node      *mnt_node_root;     /* Legacy: root node of this fs */
    uint64_t            mnt_covered_ino;    /* inode of covered directory (snapshot) */
    struct mount        *mnt_covered_mp;    /* mount of covered directory (snapshot) */
};

/*
 * Filesystem type switch table.
 */
struct vfsconf {
	struct vfsops	*vfc_vfsops;	/* filesystem operations */
	char		    vfc_name[32];	/* filesystem type name */
	int		        vfc_typenum;	/* historic implementation */
	int		        vfc_refcount;	/* number mounted of this type */
	int		        vfc_flags;	/* permanent flags */
	struct vfsconf	*vfc_next;	/* next in list */
};

/*
 * Operations supported on mounted file system.
 */
struct vfsops {
	int	(*vfs_mount)(struct mount *mp, const char *path, void *data,
				struct nameidata *ndp, struct thread *td);
	int	(*vfs_start)(struct mount *mp, int flags, struct thread *td);
	int	(*vfs_unmount)(struct mount *mp, int mntflags, struct thread *td);
	int	(*vfs_root)(struct mount *mp, struct vnode **vpp);
	int	(*vfs_quotactl)(struct mount *mp, int cmds, uid_t uid,
				void *arg, struct thread *td);
	int	(*vfs_statfs)(struct mount *mp, struct statfs *sbp, struct thread *td);
	int	(*vfs_sync)(struct mount *mp, int waitfor, struct ucred *cred,
				struct thread *td);
	int	(*vfs_vget)(struct mount *mp, void *ino, struct vnode **vpp);
	int	(*vfs_fhtovp)(struct mount *mp, struct fid *fhp,
				struct vnode **vpp);
	int	(*vfs_vptofh)(struct vnode *vp, struct fid *fhp);
	int	(*vfs_init)(struct vfsconf *);
	int	(*vfs_uninit)(struct vfsconf *);
};

#ifndef _KERNEL
#ifdef __cplusplus
extern "C" {
#endif
int mount(const char *type, const char *dir, int flags, void *data);
int unmount(const char *dir, int flags);
#ifdef __cplusplus
}
#endif
#endif

/*
 * Build-time invariants.  These guard against accidental ABI breakage
 * when somebody reorders fields or changes a type without updating the
 * copyin/copyout glue and userspace mirrors.
 */
_MOUNT_SASSERT(sizeof(struct mount_args) == 16,
               "struct mount_args ABI changed; bump MOUNT_ARGS_VERSION");
_MOUNT_SASSERT(offsetof(struct mount_args, mnt_arg_version) == 0,
               "mnt_arg_version must be the first field");
_MOUNT_SASSERT(offsetof(struct mount_args, mnt_arg_size) == 4,
               "mnt_arg_size offset changed");
_MOUNT_SASSERT(sizeof(((struct statfs *)0)->f_fstypename) == 16,
               "statfs f_fstypename must be exactly 16 bytes");
_MOUNT_SASSERT(sizeof(((struct statfs *)0)->f_mntonname) == 128,
               "statfs f_mntonname must be exactly 128 bytes");
_MOUNT_SASSERT(sizeof(((struct statfs *)0)->f_mntfromname) == 128,
               "statfs f_mntfromname must be exactly 128 bytes");

#endif /* !_SYS_MOUNT_H */
