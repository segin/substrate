#include <sys/types.h>
#include <sys/namei.h>
#include <vfs/vnode.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <vm/vm_kmem.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <string.h>
#include <kern/panic.h>
#include <vm/uma.h>

#define MAXSYMLINKS 8

static uma_zone_t *namei_zone;

void namei_init(void) {
    namei_zone = uma_zcreate("namei", 1024, NULL, NULL, NULL, NULL, 0, 0);
}

/*
 * pathname lookup (namei)
 * 
 * Translates a pathname to a vnode.
 * 
 * ndp: pathname lookup data structure
 */
int
namei(struct nameidata *ndp)
{
    struct componentname *cnp = &ndp->ni_cnd;
    struct vnode *dp;
    int error;
    int nlink = 0;
    char component[256];
    const char *p;

    ndp->ni_vp = NULL;
    ndp->ni_dvp = NULL;

    /*
     * Allocation of path buffer.
     */
    if (namei_zone) {
        cnp->cn_pnbuf = uma_zalloc(namei_zone, 0);
    } else {
        cnp->cn_pnbuf = kmalloc(1024);
    }
    if (cnp->cn_pnbuf == NULL)
        return ENOMEM;

    /*
     * Copy the pathname from user or kernel space.
     */
    if (ndp->ni_segflg == UIO_USERSPACE) {
        size_t len;
        error = copyinstr(ndp->ni_dirp, cnp->cn_pnbuf, 1024, &len);
        if (error) {
            if (namei_zone) uma_zfree(namei_zone, cnp->cn_pnbuf); else kfree(cnp->cn_pnbuf, 1024);
            return error;
        }
    } else {
        size_t len = strlen(ndp->ni_dirp);
        if (len >= 1024) {
            if (namei_zone) uma_zfree(namei_zone, cnp->cn_pnbuf); else kfree(cnp->cn_pnbuf, 1024);
            return ENAMETOOLONG;
        }
        strncpy(cnp->cn_pnbuf, ndp->ni_dirp, 1024 - 1);
        cnp->cn_pnbuf[1023] = '\0';
    }

    ndp->ni_dirp = cnp->cn_pnbuf;

    /*
     * Determine starting directory.
     */
    if (ndp->ni_dirp[0] == '/') {
        dp = ndp->ni_rootdir;
        if (dp == NULL) {
            extern struct vnode *rootvnode;
            dp = rootvnode;
        }
        /* Skip leading slashes */
        while (*ndp->ni_dirp == '/')
            ndp->ni_dirp++;
    } else {
        dp = ndp->ni_startdir;
        if (dp == NULL) {
            extern struct vnode *rootvnode;
            dp = rootvnode;
        }
    }

    if (dp == NULL) {
        if (namei_zone) uma_zfree(namei_zone, cnp->cn_pnbuf); else kfree(cnp->cn_pnbuf, 1024);
        return ENOENT;
    }

    vref(dp);

    /*
     * Main lookup loop.
     */
    for (;;) {
        if (*ndp->ni_dirp == '\0') {
            ndp->ni_vp = dp;
            return 0;
        }

        /* Extract next component */
        p = ndp->ni_dirp;
        while (*p != '\0' && *p != '/')
            p++;
        
        cnp->cn_namelen = p - ndp->ni_dirp;
        if (cnp->cn_namelen >= sizeof(component)) {
            vrele(dp);
            return ENAMETOOLONG;
        }
        memcpy(component, ndp->ni_dirp, cnp->cn_namelen);
        component[cnp->cn_namelen] = '\0';
        cnp->cn_nameptr = component;

        /* Skip trailing slashes of this component */
        while (*p == '/')
            p++;
        
        /* Check if this is the last component */
        if (*p == '\0') {
            cnp->cn_flags |= ISLASTCN;
        } else {
            cnp->cn_flags &= ~ISLASTCN;
        }

        /* Update pathname pointer for next iteration */
        ndp->ni_dirp = p;

        /*
         * Handle ".." crossing back over mount points.
         */
        if (cnp->cn_namelen == 2 && component[0] == '.' && component[1] == '.') {
            for (;;) {
                if (dp->v_flag & VROOT) {
                    struct vnode *tvp = dp->v_mount->mnt_vnodecovered;
                    if (tvp != NULL) {
                        vref(tvp);
                        vrele(dp);
                        dp = tvp;
                        continue;
                    }
                }
                break;
            }
        }

        /* First check the name cache */
        error = cache_lookup(dp, &ndp->ni_vp, cnp->cn_nameptr, cnp->cn_namelen);
        if (error == 0) {
            /* Cache hit - target vnode in ndp->ni_vp */
        } else {
            /* Cache miss - perform full lookup */
            error = VOP_LOOKUP(dp, &ndp->ni_vp, cnp);
            if (error) {
                vrele(dp);
                return error;
            }
            /* Enter into cache if successful */
            cache_enter(dp, ndp->ni_vp, cnp->cn_nameptr, cnp->cn_namelen);
        }

        /*
         * Handle Mount Point Crossing
         */
        while (ndp->ni_vp->v_mountedhere != NULL) {
            struct vnode *tvp;
            struct mount *mp = ndp->ni_vp->v_mountedhere;
            
            error = VFS_ROOT(mp, &tvp);
            if (error) {
                vrele(ndp->ni_vp);
                vrele(dp);
                return error;
            }
            vrele(ndp->ni_vp);
            ndp->ni_vp = tvp;
        }

        /*
         * Handle Symbolic Links
         */
        if (ndp->ni_vp->v_type == VLNK && 
            ((cnp->cn_flags & FOLLOW) || !(cnp->cn_flags & ISLASTCN))) {
            
            if (nlink++ >= MAXSYMLINKS) {
                vrele(ndp->ni_vp);
                vrele(dp);
                if (namei_zone) uma_zfree(namei_zone, cnp->cn_pnbuf); else kfree(cnp->cn_pnbuf, 1024);
                return ELOOP;
            }

            /* Read symlink target */
            char *target = namei_zone ? uma_zalloc(namei_zone, 0) : kmalloc(1024);
            if (target == NULL) {
                vrele(ndp->ni_vp);
                vrele(dp);
                if (namei_zone) uma_zfree(namei_zone, cnp->cn_pnbuf); else kfree(cnp->cn_pnbuf, 1024);
                return ENOMEM;
            }

            struct iovec aiov;
            struct uio auio;
            aiov.iov_base = target;
            aiov.iov_len = 1024;
            auio.uio_iov = &aiov;
            auio.uio_iovcnt = 1;
            auio.uio_offset = 0;
            auio.uio_resid = 1024;
            auio.uio_segflg = UIO_SYSSPACE;
            auio.uio_rw = UIO_READ;

            error = VOP_READLINK(ndp->ni_vp, &auio, cnp->cn_cred);
            if (error) {
                if (namei_zone) uma_zfree(namei_zone, target); else kfree(target, 1024);
                vrele(ndp->ni_vp);
                vrele(dp);
                if (namei_zone) uma_zfree(namei_zone, cnp->cn_pnbuf); else kfree(cnp->cn_pnbuf, 1024);
                return error;
            }

            size_t target_len = 1024 - auio.uio_resid;
            if (target_len >= 1024) {
                if (namei_zone) uma_zfree(namei_zone, target); else kfree(target, 1024);
                vrele(ndp->ni_vp);
                vrele(dp);
                if (namei_zone) uma_zfree(namei_zone, cnp->cn_pnbuf); else kfree(cnp->cn_pnbuf, 1024);
                return ENAMETOOLONG;
            }
            target[target_len] = '\0';

            /*
             * Combine target with remaining path.
             */
            size_t rem_len = strlen(p);
            if (target_len + rem_len >= 1024) {
                if (namei_zone) uma_zfree(namei_zone, target); else kfree(target, 1024);
                vrele(ndp->ni_vp);
                vrele(dp);
                if (namei_zone) uma_zfree(namei_zone, cnp->cn_pnbuf); else kfree(cnp->cn_pnbuf, 1024);
                return ENAMETOOLONG;
            }

            char *new_path = namei_zone ? uma_zalloc(namei_zone, 0) : kmalloc(1024);
            if (new_path == NULL) {
                if (namei_zone) uma_zfree(namei_zone, target); else kfree(target, 1024);
                vrele(ndp->ni_vp);
                vrele(dp);
                if (namei_zone) uma_zfree(namei_zone, cnp->cn_pnbuf); else kfree(cnp->cn_pnbuf, 1024);
                return ENOMEM;
            }

            memcpy(new_path, target, target_len);
            strncpy(new_path + target_len, p, 1024 - target_len - 1);
            new_path[1023] = '\0';

            if (namei_zone) uma_zfree(namei_zone, target); else kfree(target, 1024);
            if (namei_zone) uma_zfree(namei_zone, cnp->cn_pnbuf); else kfree(cnp->cn_pnbuf, 1024);
            cnp->cn_pnbuf = new_path;
            ndp->ni_dirp = cnp->cn_pnbuf;

            /* If absolute, restart from root */
            if (ndp->ni_dirp[0] == '/') {
                vrele(dp);
                dp = ndp->ni_rootdir;
                if (dp == NULL) {
                    extern struct vnode *rootvnode;
                    dp = rootvnode;
                }
                vref(dp);
                while (*ndp->ni_dirp == '/')
                    ndp->ni_dirp++;
            }
            vrele(ndp->ni_vp);
            continue;
        }

        /* Advance to next directory */
        vrele(dp);
        dp = ndp->ni_vp;

        if (cnp->cn_flags & ISLASTCN) {
            /* Success - leaf vnode in ndp->ni_vp */
            break;
        }

        /* Terminal check: must be a directory to continue */
        if (dp->v_type != VDIR) {
            vrele(dp);
            return ENOTDIR;
        }
    }

    if (namei_zone) uma_zfree(namei_zone, cnp->cn_pnbuf); else kfree(cnp->cn_pnbuf, 1024);
    return 0;
}
