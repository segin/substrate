#include "cp_preserve.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#if defined(CP_HOST_BUILD) && defined(__has_include)
# if __has_include(<sys/xattr.h>)
#  include <sys/xattr.h>
#  define CP_HAVE_XATTR 1
# endif
# if defined(CP_ENABLE_ACL) && __has_include(<sys/acl.h>) && __has_include(<acl/libacl.h>)
#  include <sys/acl.h>
#  include <acl/libacl.h>
#  define CP_HAVE_ACL 1
# endif
#endif

#if defined(CP_HOST_BUILD)
#define CP_ST_ATIME_NSEC(st) ((st)->st_atim.tv_nsec)
#define CP_ST_MTIME_NSEC(st) ((st)->st_mtim.tv_nsec)
#else
#define CP_ST_ATIME_NSEC(st) ((st)->st_atime_nsec)
#define CP_ST_MTIME_NSEC(st) ((st)->st_mtime_nsec)
#endif

static void cp_preserve_warn(cp_preserve_warn_cb warn_cb,
                             void *warn_userdata,
                             const char *src,
                             const char *dst,
                             const char *reason,
                             int errnum)
{
    if (warn_cb) {
        warn_cb(warn_userdata, src, dst, reason, errnum);
    }
}

static void cp_copy_xattrs(const struct cp_options *opts,
                           const char *src,
                           const char *dst,
                           int is_symlink,
                           cp_preserve_warn_cb warn_cb,
                           void *warn_userdata)
{
#if defined(CP_HAVE_XATTR)
    ssize_t size;
    char *names = NULL;
    char *p;

    if (!opts->preserve_xattr && !opts->preserve_all) {
        return;
    }

    if (is_symlink) {
        size = llistxattr(src, NULL, 0);
    } else {
        size = listxattr(src, NULL, 0);
    }

    if (size < 0) {
        if (errno == ENOTSUP || errno == EOPNOTSUPP || errno == ENOSYS) {
            return;
        }
        cp_preserve_warn(warn_cb, warn_userdata, src, dst,
                         "preserve xattr list", errno);
        return;
    }

    if (size == 0) {
        return;
    }

    names = (char *)malloc((size_t)size);
    if (!names) {
        cp_preserve_warn(warn_cb, warn_userdata, src, dst,
                         "preserve xattr allocation", ENOMEM);
        return;
    }

    if (is_symlink) {
        size = llistxattr(src, names, (size_t)size);
    } else {
        size = listxattr(src, names, (size_t)size);
    }
    if (size < 0) {
        cp_preserve_warn(warn_cb, warn_userdata, src, dst,
                         "preserve xattr list read", errno);
        free(names);
        return;
    }

    p = names;
    while (p < names + size) {
        ssize_t vlen;
        void *val;
        size_t nlen = strlen(p);

        if (is_symlink) {
            vlen = lgetxattr(src, p, NULL, 0);
        } else {
            vlen = getxattr(src, p, NULL, 0);
        }
        if (vlen < 0) {
            if (errno != ENOTSUP && errno != EOPNOTSUPP) {
                cp_preserve_warn(warn_cb, warn_userdata, src, dst,
                                 "preserve xattr read", errno);
            }
            p += nlen + 1;
            continue;
        }

        val = malloc((size_t)vlen);
        if (!val) {
            cp_preserve_warn(warn_cb, warn_userdata, src, dst,
                             "preserve xattr allocation", ENOMEM);
            p += nlen + 1;
            continue;
        }

        if (is_symlink) {
            vlen = lgetxattr(src, p, val, (size_t)vlen);
        } else {
            vlen = getxattr(src, p, val, (size_t)vlen);
        }
        if (vlen >= 0) {
            int rc;
            if (is_symlink) {
                rc = lsetxattr(dst, p, val, (size_t)vlen, 0);
            } else {
                rc = setxattr(dst, p, val, (size_t)vlen, 0);
            }
            if (rc != 0 && errno != ENOTSUP && errno != EOPNOTSUPP) {
                cp_preserve_warn(warn_cb, warn_userdata, src, dst,
                                 "preserve xattr write", errno);
            }
        }

        free(val);
        p += nlen + 1;
    }

    free(names);
#else
    (void)opts;
    (void)src;
    (void)dst;
    (void)is_symlink;
    (void)warn_cb;
    (void)warn_userdata;
#endif
}

static void cp_copy_acls(const struct cp_options *opts,
                         const char *src,
                         const char *dst,
                         cp_preserve_warn_cb warn_cb,
                         void *warn_userdata)
{
#if defined(CP_HAVE_ACL)
    acl_t acl;

    if (!opts->preserve_acl && !opts->preserve_all && !opts->preserve_xattr) {
        return;
    }

    acl = acl_get_file(src, ACL_TYPE_ACCESS);
    if (!acl) {
        if (errno != ENOTSUP && errno != EOPNOTSUPP && errno != ENOSYS) {
            cp_preserve_warn(warn_cb, warn_userdata, src, dst,
                             "preserve ACL read", errno);
        }
        return;
    }

    if (acl_set_file(dst, ACL_TYPE_ACCESS, acl) != 0 &&
        errno != ENOTSUP && errno != EOPNOTSUPP) {
        cp_preserve_warn(warn_cb, warn_userdata, src, dst,
                         "preserve ACL write", errno);
    }

    acl_free((void *)acl);
#else
    (void)opts;
    (void)src;
    (void)dst;
    (void)warn_cb;
    (void)warn_userdata;
#endif
}

int cp_preserve_metadata(const struct cp_options *opts,
                         const char *src_path,
                         const struct stat *src_st,
                         const char *dst_path,
                         int dst_fd,
                         int is_symlink,
                         cp_preserve_warn_cb warn_cb,
                         void *warn_userdata)
{
    if (opts->preserve_owner) {
        int rc = -1;

#ifdef CP_HOST_BUILD
        if (dst_fd >= 0 && !is_symlink) {
            rc = fchown(dst_fd, src_st->st_uid, src_st->st_gid);
        } else {
# ifdef __linux__
            if (is_symlink) {
                rc = lchown(dst_path, src_st->st_uid, src_st->st_gid);
            } else
# endif
            {
                rc = chown(dst_path, src_st->st_uid, src_st->st_gid);
            }
        }
#else
        (void)dst_fd;
        (void)is_symlink;
        rc = chown(dst_path, src_st->st_uid, src_st->st_gid);
#endif

        if (rc != 0 && errno != EPERM && errno != EACCES) {
            cp_preserve_warn(warn_cb, warn_userdata, src_path, dst_path,
                             "preserve owner", errno);
        } else if (rc != 0 && (errno == EPERM || errno == EACCES)) {
            cp_preserve_warn(warn_cb, warn_userdata, src_path, dst_path,
                             "preserve owner requires privilege", errno);
        }
    }

    if (opts->preserve_mode && !is_symlink) {
        int rc = -1;

#ifdef CP_HOST_BUILD
        if (dst_fd >= 0) {
            rc = fchmod(dst_fd, src_st->st_mode & 07777);
        } else {
            rc = chmod(dst_path, src_st->st_mode & 07777);
        }
#else
        (void)dst_fd;
        rc = chmod(dst_path, src_st->st_mode & 07777);
#endif
        if (rc != 0) {
            cp_preserve_warn(warn_cb, warn_userdata, src_path, dst_path,
                             "preserve mode", errno);
        }
    }

    if (opts->preserve_timestamps) {
        int rc = -1;

#ifdef CP_HOST_BUILD
        if (!is_symlink && dst_fd >= 0) {
            struct timespec ts[2];
            ts[0].tv_sec = src_st->st_atime;
            ts[0].tv_nsec = CP_ST_ATIME_NSEC(src_st);
            ts[1].tv_sec = src_st->st_mtime;
            ts[1].tv_nsec = CP_ST_MTIME_NSEC(src_st);
            rc = futimens(dst_fd, ts);
        } else if (!is_symlink) {
            struct timeval tv[2];
            tv[0].tv_sec = src_st->st_atime;
            tv[0].tv_usec = (suseconds_t)(CP_ST_ATIME_NSEC(src_st) / 1000);
            tv[1].tv_sec = src_st->st_mtime;
            tv[1].tv_usec = (suseconds_t)(CP_ST_MTIME_NSEC(src_st) / 1000);
            rc = utimes(dst_path, tv);
        }
#else
        {
            struct timeval tv[2];
            tv[0].tv_sec = src_st->st_atime;
            tv[0].tv_usec = (suseconds_t)(CP_ST_ATIME_NSEC(src_st) / 1000);
            tv[1].tv_sec = src_st->st_mtime;
            tv[1].tv_usec = (suseconds_t)(CP_ST_MTIME_NSEC(src_st) / 1000);
            if (!is_symlink) {
                rc = utimes(dst_path, tv);
            }
        }
#endif

        if (rc != 0 && !is_symlink && errno != ENOSYS) {
            cp_preserve_warn(warn_cb, warn_userdata, src_path, dst_path,
                             "preserve timestamps", errno);
        }
    }

    cp_copy_xattrs(opts, src_path, dst_path, is_symlink, warn_cb, warn_userdata);
    cp_copy_acls(opts, src_path, dst_path, warn_cb, warn_userdata);

    if (opts->preserve_flags || opts->preserve_all) {
#if defined(CP_HOST_BUILD) && defined(__APPLE__)
        if (chflags(dst_path, src_st->st_flags) != 0 && errno != ENOTSUP) {
            cp_preserve_warn(warn_cb, warn_userdata, src_path, dst_path,
                             "preserve flags", errno);
        }
#else
        (void)warn_cb;
        (void)warn_userdata;
#endif
    }

    return 0;
}
