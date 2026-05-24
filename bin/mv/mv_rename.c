#include "mv.h"
#include "mv_backup.h"
#include "mv_path.h"
#include "mv_prompt.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int diag(const struct mv_options *o, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", o->progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return -1;
}

/* Set mtime/atime on `path` (lstat-style, so symlinks are unmodified
 * — we just skip for symlinks since utimensat with AT_SYMLINK_NOFOLLOW
 * isn't universally available).  Best-effort; failures are non-fatal. */
static void copy_times(const char *path, const struct stat *st)
{
    struct utimbuf t;
    t.actime  = st->st_atime;
    t.modtime = st->st_mtime;
    (void)utime(path, &t);
}

static int copy_regular(const char *src, const char *dst,
                        const struct stat *sst)
{
    int sfd, dfd;
    char buf[64 * 1024];
    ssize_t n;
    int ret = 0;

    sfd = open(src, O_RDONLY);
    if (sfd < 0) {
        return -1;
    }
    dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, sst->st_mode & 07777);
    if (dfd < 0) {
        close(sfd);
        return -1;
    }
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        ssize_t total = 0;
        while (total < n) {
            ssize_t w = write(dfd, buf + total, (size_t)(n - total));
            if (w < 0) {
                if (errno == EINTR) continue;
                ret = -1;
                goto done;
            }
            total += w;
        }
    }
    if (n < 0) ret = -1;

done:
    close(sfd);
    if (close(dfd) != 0) ret = -1;
    if (ret == 0) {
        (void)chmod(dst, sst->st_mode & 07777);
        (void)chown(dst, sst->st_uid, sst->st_gid);
        copy_times(dst, sst);
    }
    return ret;
}

static int copy_symlink(const char *src, const char *dst,
                        const struct stat *sst)
{
    char target[PATH_MAX];
    ssize_t n;

    n = readlink(src, target, sizeof(target) - 1u);
    if (n < 0) return -1;
    target[n] = '\0';
    (void)unlink(dst);
    if (symlink(target, dst) != 0) return -1;
    (void)lchown(dst, sst->st_uid, sst->st_gid);
    return 0;
}

static int copy_tree(const char *src, const char *dst);

static int copy_tree_inner(const char *src, const char *dst,
                           const struct stat *sst)
{
    DIR *dp;
    struct dirent *de;
    int rc = 0;

    if (mkdir(dst, sst->st_mode & 07777) != 0 && errno != EEXIST) {
        return -1;
    }
    dp = opendir(src);
    if (dp == NULL) return -1;
    while ((de = readdir(dp)) != NULL) {
        char *sp;
        char *dp2;

        if (strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0) continue;
        sp  = mv_path_join(src, de->d_name);
        dp2 = mv_path_join(dst, de->d_name);
        if (sp == NULL || dp2 == NULL) {
            rc = -1;
            free(sp); free(dp2);
            continue;
        }
        if (copy_tree(sp, dp2) != 0) rc = -1;
        free(sp); free(dp2);
    }
    closedir(dp);
    (void)chmod(dst, sst->st_mode & 07777);
    (void)chown(dst, sst->st_uid, sst->st_gid);
    copy_times(dst, sst);
    return rc;
}

static int copy_tree(const char *src, const char *dst)
{
    struct stat sst;

    if (lstat(src, &sst) != 0) return -1;
    if (S_ISLNK(sst.st_mode))  return copy_symlink(src, dst, &sst);
    if (S_ISDIR(sst.st_mode))  return copy_tree_inner(src, dst, &sst);
    if (S_ISREG(sst.st_mode))  return copy_regular(src, dst, &sst);
    /* Special files: mknod */
    if (mknod(dst, sst.st_mode, sst.st_rdev) != 0) return -1;
    (void)chmod(dst, sst.st_mode & 07777);
    (void)chown(dst, sst.st_uid, sst.st_gid);
    copy_times(dst, &sst);
    return 0;
}

static int remove_tree(const char *path)
{
    struct stat st;
    DIR *dp;
    struct dirent *de;
    int rc = 0;

    if (lstat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }
    dp = opendir(path);
    if (dp == NULL) return -1;
    while ((de = readdir(dp)) != NULL) {
        char *child;
        if (strcmp(de->d_name, ".") == 0 ||
            strcmp(de->d_name, "..") == 0) continue;
        child = mv_path_join(path, de->d_name);
        if (child == NULL) { rc = -1; continue; }
        if (remove_tree(child) != 0) rc = -1;
        free(child);
    }
    closedir(dp);
    if (rmdir(path) != 0) rc = -1;
    return rc;
}

static int mv_cross_device(const char *src, const char *dst,
                           const struct mv_options *opts)
{
    if (copy_tree(src, dst) != 0) {
        return diag(opts,
            "cannot copy '%s' to '%s' across filesystems: %s",
            src, dst, strerror(errno));
    }
    if (remove_tree(src) != 0) {
        return diag(opts,
            "cross-device copy of '%s' succeeded but removing source failed: %s",
            src, strerror(errno));
    }
    return 0;
}

/* Should we skip per --update? */
static bool update_should_skip(const struct mv_options *opts,
                               const char *src, const char *dst)
{
    struct stat ss, ds;

    if (opts->update == MV_UPDATE_ALL) return false;
    if (opts->update == MV_UPDATE_NONE) {
        /* Treat target as if it always already exists; if it does,
         * skip silently. */
        return lstat(dst, &ds) == 0;
    }
    if (opts->update == MV_UPDATE_NONE_FAIL) {
        return false;   /* handled by caller as an explicit error */
    }
    /* MV_UPDATE_OLDER */
    if (lstat(dst, &ds) != 0) return false;     /* dst missing → proceed */
    if (lstat(src, &ss) != 0) return false;
    return ss.st_mtime <= ds.st_mtime;
}

int mv_rename_one(const char *src, const char *dst,
                  const struct mv_options *opts)
{
    struct stat dst_st;
    bool dst_exists;
    bool dst_is_dir;
    int rc;

    /* Same-file early exit. */
    if (mv_path_same_file(src, dst)) {
        return diag(opts, "'%s' and '%s' are the same file", src, dst);
    }

    dst_exists = (lstat(dst, &dst_st) == 0);
    dst_is_dir = dst_exists && S_ISDIR(dst_st.st_mode) &&
                 !(opts->symlink_target_as_self && S_ISLNK(dst_st.st_mode));

    /* `-T` forbids treating an existing directory as a target dir
     * — caller is responsible for that classification.  Here we
     * focus on overwrite policy. */
    if (dst_exists && !dst_is_dir) {
        /* --update policies */
        if (opts->update == MV_UPDATE_NONE_FAIL) {
            return diag(opts,
                "not replacing '%s' (--update=none-fail)", dst);
        }
        if (update_should_skip(opts, src, dst)) {
            return 0;
        }

        if (opts->prompt == MV_PROMPT_NOCLOBBER) {
            /* Skip silently. */
            return 0;
        }
        if (opts->prompt == MV_PROMPT_INTERACTIVE) {
            if (!mv_prompt_yn("%s: overwrite '%s'? ",
                              opts->progname, dst)) {
                return 0;
            }
        } else if (opts->prompt == MV_PROMPT_FORCE) {
            /* BSD: prompt for read-only target on a tty. */
            if (access(dst, W_OK) != 0 && isatty(fileno(stdin))) {
                if (!mv_prompt_yn(
                        "%s: override mode %04o '%s'? ",
                        opts->progname,
                        (unsigned)(dst_st.st_mode & 07777), dst)) {
                    return 0;
                }
            }
        }
    }

    /* Optional backup (no-op if no target or MV_BACKUP_NONE). */
    {
        char *bpath = NULL;
        if (dst_exists && !dst_is_dir &&
            mv_perform_backup(dst, opts, &bpath) != 0) {
            return diag(opts, "backup of '%s' failed: %s",
                        dst, strerror(errno));
        }
        if (bpath != NULL) {
            if (opts->verbose) {
                fprintf(stdout, "(backup: '%s')\n", bpath);
            }
            free(bpath);
        }
    }

    rc = rename(src, dst);
    if (rc == 0) {
        if (opts->verbose) {
            fprintf(stdout, "renamed '%s' -> '%s'\n", src, dst);
        }
        return 0;
    }
    if (errno == EXDEV) {
        rc = mv_cross_device(src, dst, opts);
        if (rc == 0 && opts->verbose) {
            fprintf(stdout, "renamed '%s' -> '%s'\n", src, dst);
        }
        return rc;
    }
    return diag(opts, "cannot move '%s' to '%s': %s",
                src, dst, strerror(errno));
}
