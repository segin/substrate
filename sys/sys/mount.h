#ifndef _SYS_MOUNT_H
#define _SYS_MOUNT_H

#include <sys/types.h>
#include <sys/queue.h>
#include <stdint.h>

struct vnode;
struct fs_node;
struct nameidata;
struct thread;
struct ucred;
struct fid;
struct vfsconf;

/*
 * Filesystem statistics
 */
struct statfs {
    long    f_type;         /* type of filesystem */
    long    f_bsize;        /* optimal transfer block size */
    long    f_iosize;       /* optimal transfer block size */
    long    f_blocks;       /* total data blocks in filesystem */
    long    f_bfree;        /* free blocks in fs */
    long    f_bavail;       /* free blocks avail to non-superuser */
    long    f_files;        /* total file nodes in filesystem */
    long    f_ffree;        /* free file nodes in fs */
    long    f_fsid;         /* filesystem id */
    uid_t   f_owner;        /* user that mounted the filesystem */
    short   f_flags;        /* copy of mount exported flags */
    short   f_syncwrites;   /* count of sync writes since mount */
    short   f_asyncwrites;  /* count of async writes since mount */
    char    f_fstypename[16]; /* fs type name */
    char    f_mntonname[128]; /* directory on which mounted */
    char    f_mntfromname[128]; /* mounted filesystem */
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
    struct fs_node      *mnt_node_covered;  /* Legacy: Node we mounted on */
    struct fs_node      *mnt_node_root;     /* Legacy: root node of this fs */
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

#endif /* !_SYS_MOUNT_H */
