/*
 * sys/sys/namei.h - Pathname lookup (namei) definitions
 * 
 * Following BSD patterns for pathname translation.
 */

#ifndef _SYS_NAMEI_H
#define _SYS_NAMEI_H

#include <sys/types.h>
#include <stdint.h>
#include <sys/ucred.h>
#include <sys/uio.h>

#include <sys/uio.h>

/* Forward declarations */
struct vnode;
struct mount;

/*
 * Component name structure.
 * Describes the current path component being resolved.
 */
struct componentname {
    /* Arguments to VOP_LOOKUP */
    uint32_t    cn_nameiop;     /* namei operation */
    uint32_t    cn_flags;       /* flags */
    struct ucred *cn_cred;      /* credentials */
    
    /* Information about the component */
    char        *cn_pnbuf;      /* buffer for pathname */
    char        *cn_nameptr;    /* pointer to current component */
    size_t      cn_namelen;     /* length of component */
    uint32_t    cn_hash;        /* hash of component name */
};

/*
 * nameidata structure.
 * Encapsulates the entire pathname lookup state.
 */
struct nameidata {
    /* Arguments to namei */
    const char  *ni_dirp;       /* pathname pointer */
    enum uio_seg ni_segflg;     /* source address space */
    
    /* State during lookup */
    struct vnode *ni_startdir;  /* starting directory (CWD) */
    struct vnode *ni_rootdir;   /* root directory */
    struct vnode *ni_vp;        /* result vnode */
    struct vnode *ni_dvp;       /* result parent vnode */
    
    /* Result component information */
    struct componentname ni_cnd;
};

/*
 * namei operations (cn_nameiop)
 */
#define LOOKUP      0           /* perform a lookup */
#define CREATE      1           /* set up for creating a file */
#define DELETE      2           /* set up for deleting a file */
#define RENAME      3           /* set up for renaming a file */
#define OPMASK      3           /* mask for operation */

/*
 * namei flags (cn_flags)
 */
#define FOLLOW      0x00000001  /* follow trailing symbolic link */
#define NOFOLLOW    0x00000000  /* do not follow trailing symbolic link */
#define LOCKPARENT  0x00000002  /* lock parent directory (ni_dvp) */
#define LOCKLEAF    0x00000004  /* lock target vnode (ni_vp) */
#define NOCACHE     0x00000008  /* name masking / cache bypass */
#define NOCROSSMOUNT 0x00000010 /* do not cross mount points */
#define RDONLY      0x00000020  /* lookup for read-only ops */
#define HASBUF      0x00000040  /* nameidata contains pathname buffer */
#define SAVENAME    0x00000080  /* save last component for CREATE/RENAME/DELETE */
#define ISDOTDOT    0x00000100  /* current component is ".." */
#define ISLASTCN    0x00000200  /* current component is the last component */
#define ISSYMLINK   0x00000400  /* current component is a symbolic link */
#define ISWHITEOUT  0x00000800  /* found whiteout */
#define DOWHITEOUT  0x00001000  /* allow whiteouts */
#define WILLLOOKUP  0x00002000  /* VFS_VGET will be followed by VOP_LOOKUP */

/*
 * Symlink recursion limit
 */
#define MAXSYMLINKS 32

/*
 * AT_FDCWD: use current working directory for *at() syscalls
 */
#define AT_FDCWD    (-100)

/*
 * Initialization macros
 */
#define NDINIT(ndp, op, flags, segflg, namep) do { \
    *(ndp) = (struct nameidata){0}; \
    (ndp)->ni_cnd.cn_nameiop = (op); \
    (ndp)->ni_cnd.cn_flags = (flags); \
    (ndp)->ni_segflg = (segflg); \
    (ndp)->ni_dirp = (namep); \
} while (0)

/* Name Cache functions */
void nchinit(void);
void namei_init(void);
void cache_enter(struct vnode *dvp, struct vnode *vp, const char *name, size_t len);
int  cache_lookup(struct vnode *dvp, struct vnode **vpp, const char *name, size_t len);
void cache_purge(struct vnode *vp);
void cache_purgevfs(struct mount *mp);

/* namei entry point */
int namei(struct nameidata *ndp);

#endif /* _SYS_NAMEI_H */
