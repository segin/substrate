#include "cp_copy.h"

#include "cp_atomic.h"
#include "cp_path.h"
#include "cp_preserve.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef EOPNOTSUPP
#define EOPNOTSUPP ENOSYS
#endif
#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif
#ifndef ELOOP
#define ELOOP EINVAL
#endif

#if defined(CP_HOST_BUILD) && defined(__has_include)
# if __has_include(<sys/sendfile.h>)
#  include <sys/sendfile.h>
#  define CP_HAVE_SENDFILE 1
# endif
# if __has_include(<sys/ioctl.h>)
#  include <sys/ioctl.h>
# endif
# if __has_include(<sys/types.h>)
#  include <sys/types.h>
# endif
# if __has_include(<linux/fs.h>)
#  include <linux/fs.h>
#  define CP_HAVE_FICLONE 1
# endif
#endif

#if defined(CP_HOST_BUILD)
#define CP_ST_MTIME_NSEC(st) ((st)->st_mtim.tv_nsec)
#else
#define CP_ST_MTIME_NSEC(st) ((st)->st_mtime_nsec)
#endif

static void cp_diag(struct cp_context *ctx,
                    const char *src,
                    const char *dst,
                    const char *what,
                    int errnum)
{
    if (ctx->opts->force_silent) {
        ctx->had_error = 1;
        return;
    }

    if (dst) {
        fprintf(stderr, "%s: %s -> %s: %s: %s\n",
                ctx->progname, src ? src : "(none)", dst,
                what, strerror(errnum));
    } else if (src) {
        fprintf(stderr, "%s: %s: %s: %s\n",
                ctx->progname, src, what, strerror(errnum));
    } else {
        fprintf(stderr, "%s: %s: %s\n", ctx->progname, what, strerror(errnum));
    }
    ctx->had_error = 1;
}

static void cp_warn(struct cp_context *ctx,
                    const char *src,
                    const char *dst,
                    const char *what,
                    int errnum)
{
    if (ctx->opts->force_silent) {
        return;
    }
    if (dst) {
        fprintf(stderr, "%s: %s -> %s: warning: %s: %s\n",
                ctx->progname, src ? src : "(none)", dst, what, strerror(errnum));
    } else if (src) {
        fprintf(stderr, "%s: %s: warning: %s: %s\n",
                ctx->progname, src, what, strerror(errnum));
    } else {
        fprintf(stderr, "%s: warning: %s: %s\n", ctx->progname, what, strerror(errnum));
    }
}

static void cp_verbose(struct cp_context *ctx, const char *fmt,
                       const char *a, const char *b)
{
    if (!ctx->opts->verbose || ctx->opts->force_silent) {
        return;
    }
    fprintf(stderr, "%s: ", ctx->progname);
    fprintf(stderr, fmt, a, b);
    fputc('\n', stderr);
}

static void cp_preserve_warn_bridge(void *userdata,
                                    const char *src_path,
                                    const char *dst_path,
                                    const char *reason,
                                    int errnum)
{
    struct cp_context *ctx = (struct cp_context *)userdata;
    cp_diag(ctx, src_path, dst_path, reason, errnum);
}

static int cp_is_symlink_followed(const struct cp_options *opts,
                                  int is_cmdline_arg)
{
    if (opts->link_mode != CP_LINKMODE_COPY) {
        return 0;
    }

    if (!opts->recursive) {
        return opts->symlink_mode != CP_SYMLINK_PHYSICAL;
    }

    switch (opts->symlink_mode) {
    case CP_SYMLINK_FOLLOW_ALL:
        return 1;
    case CP_SYMLINK_FOLLOW_CMDLINE:
        return is_cmdline_arg;
    case CP_SYMLINK_PHYSICAL:
    case CP_SYMLINK_AUTO:
    default:
        return 0;
    }
}

static size_t cp_select_buffer_size(const struct cp_options *opts,
                                    const struct stat *src_st,
                                    int dst_fd)
{
    size_t size = opts->buffer_size;

    if (opts->buffer_size_explicit) {
        return size;
    }

    if (src_st && (size_t)src_st->st_blksize > size) {
        size = (size_t)src_st->st_blksize;
    }

    if (dst_fd >= 0) {
        struct stat st;
        if (fstat(dst_fd, &st) == 0 && (size_t)st.st_blksize > size) {
            size = (size_t)st.st_blksize;
        }
    }

    if (size < 4096) {
        size = 4096;
    }

    return size;
}

static int cp_prompt_overwrite(const struct cp_options *opts, const char *path)
{
    char buf[32];
    ssize_t n;
    const char *prompt = "cp: overwrite destination? [y/N] ";

    if (!isatty(STDIN_FILENO)) {
        return opts->non_tty_default == CP_PROMPT_DEFAULT_YES;
    }

    (void)write(STDERR_FILENO, prompt, strlen(prompt));
    (void)write(STDERR_FILENO, path, strlen(path));
    (void)write(STDERR_FILENO, " ", 1);

    do {
        n = read(STDIN_FILENO, buf, sizeof(buf));
    } while (n < 0 && errno == EINTR);

    if (n <= 0) {
        return 0;
    }

    return buf[0] == 'y' || buf[0] == 'Y';
}

static int cp_should_skip_existing(struct cp_context *ctx,
                                   const char *dst,
                                   int exists)
{
    if (!exists) {
        return 0;
    }

    if (ctx->opts->overwrite_mode == CP_OVERWRITE_NOCLOBBER) {
        return 1;
    }

    if (ctx->opts->overwrite_mode == CP_OVERWRITE_INTERACTIVE) {
        return !cp_prompt_overwrite(ctx->opts, dst);
    }

    return 0;
}

static int cp_backup_exists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

static char *cp_backup_simple_path(const struct cp_options *opts, const char *dst)
{
    size_t len = strlen(dst);
    size_t slen = strlen(opts->backup_suffix ? opts->backup_suffix : "~");
    char *out = (char *)malloc(len + slen + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, dst, len);
    memcpy(out + len, opts->backup_suffix ? opts->backup_suffix : "~", slen);
    out[len + slen] = '\0';
    return out;
}

static char *cp_backup_numbered_path(const char *dst, int n)
{
    char suffix[48];
    size_t len = strlen(dst);
    int slen = snprintf(suffix, sizeof(suffix), ".~%d~", n);
    char *out;

    if (slen < 0 || (size_t)slen >= sizeof(suffix)) {
        errno = EINVAL;
        return NULL;
    }
    out = (char *)malloc(len + (size_t)slen + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, dst, len);
    memcpy(out + len, suffix, (size_t)slen);
    out[len + (size_t)slen] = '\0';
    return out;
}

static int cp_find_backup_numbered(const char *dst, int *max_found)
{
    int n;
    int seen = 0;
    int maxn = 0;

    for (n = 1; n < 10000; ++n) {
        char *candidate = cp_backup_numbered_path(dst, n);
        if (!candidate) {
            return -1;
        }
        if (cp_backup_exists(candidate)) {
            seen = 1;
            maxn = n;
        } else {
            free(candidate);
            break;
        }
        free(candidate);
    }

    *max_found = seen ? maxn : 0;
    return 0;
}

static char *cp_backup_path_for_control(const struct cp_options *opts, const char *dst)
{
    enum cp_backup_control control = opts->backup_control;

    if (control == CP_BACKUP_NONE) {
        return NULL;
    }
    if (control == CP_BACKUP_SIMPLE) {
        return cp_backup_simple_path(opts, dst);
    }
    if (control == CP_BACKUP_EXISTING || control == CP_BACKUP_NUMBERED) {
        int maxn = 0;
        if (cp_find_backup_numbered(dst, &maxn) != 0) {
            return NULL;
        }
        if (control == CP_BACKUP_EXISTING && maxn == 0) {
            return cp_backup_simple_path(opts, dst);
        }
        return cp_backup_numbered_path(dst, maxn + 1);
    }
    errno = EINVAL;
    return NULL;
}

static int cp_maybe_backup_destination(struct cp_context *ctx,
                                       const char *src,
                                       const char *dst,
                                       int dst_exists)
{
    char *backup_path;

    if (!dst_exists || ctx->opts->backup_control == CP_BACKUP_NONE) {
        return 0;
    }

    backup_path = cp_backup_path_for_control(ctx->opts, dst);
    if (!backup_path) {
        cp_diag(ctx, src, dst, "compute backup destination", errno ? errno : ENOMEM);
        return -1;
    }

    if (rename(dst, backup_path) != 0) {
        cp_diag(ctx, src, dst, "create backup", errno);
        free(backup_path);
        return -1;
    }

    cp_verbose(ctx, "backup %s -> %s", dst, backup_path);
    free(backup_path);
    return 0;
}

static int cp_src_is_newer(const struct stat *src, const struct stat *dst)
{
    if (src->st_mtime > dst->st_mtime) {
        return 1;
    }
    if (src->st_mtime < dst->st_mtime) {
        return 0;
    }
    if (CP_ST_MTIME_NSEC(src) > CP_ST_MTIME_NSEC(dst)) {
        return 1;
    }
    return 0;
}

static int cp_remove_existing(const char *path, const struct stat *st)
{
    if (S_ISDIR(st->st_mode)) {
        return rmdir(path);
    }
    return unlink(path);
}

static ssize_t cp_read_retry(int fd, void *buf, size_t len)
{
    ssize_t n;
    do {
        n = read(fd, buf, len);
    } while (n < 0 && errno == EINTR);
    return n;
}

static int cp_write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t done = 0;

    while (done < len) {
        ssize_t n = write(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        done += (size_t)n;
    }

    return 0;
}

static int cp_buf_all_zero(const unsigned char *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        if (buf[i] != 0) {
            return 0;
        }
    }
    return 1;
}

#ifdef CP_TEST_EXPOSE
int cp_test_buf_all_zero(const unsigned char *buf, size_t len)
{
    return cp_buf_all_zero(buf, len);
}
#endif

static int cp_copy_sparse_seek_hole(int src_fd, int dst_fd, off_t size)
{
#if defined(SEEK_DATA) && defined(SEEK_HOLE)
    off_t off = 0;

    while (off < size) {
        off_t data = lseek(src_fd, off, SEEK_DATA);
        off_t hole;

        if (data < 0) {
            if (errno == ENXIO) {
                if (lseek(dst_fd, size, SEEK_SET) < 0) {
                    return -1;
                }
                if (ftruncate(dst_fd, size) != 0) {
                    return -1;
                }
                return 0;
            }
            return -1;
        }

        if (data > off) {
            if (lseek(dst_fd, data - off, SEEK_CUR) < 0) {
                return -1;
            }
        }

        hole = lseek(src_fd, data, SEEK_HOLE);
        if (hole < 0) {
            return -1;
        }

        if (lseek(src_fd, data, SEEK_SET) < 0) {
            return -1;
        }

        while (data < hole) {
            char buf[65536];
            off_t remain = hole - data;
            size_t todo = (size_t)(remain > (off_t)sizeof(buf) ? (off_t)sizeof(buf) : remain);
            ssize_t n = cp_read_retry(src_fd, buf, todo);
            if (n < 0) {
                return -1;
            }
            if (n == 0) {
                break;
            }
            if (cp_write_all(dst_fd, buf, (size_t)n) != 0) {
                return -1;
            }
            data += n;
        }

        off = hole;
    }

    if (ftruncate(dst_fd, size) != 0) {
        return -1;
    }

    return 0;
#else
    (void)src_fd;
    (void)dst_fd;
    (void)size;
    errno = ENOTSUP;
    return -1;
#endif
}

#ifdef CP_HOST_BUILD
static int cp_try_copy_file_range(int src_fd, int dst_fd, off_t size)
{
#if defined(__linux__)
    off_t done = 0;
    while (done < size) {
        ssize_t n = copy_file_range(src_fd, NULL, dst_fd, NULL,
                                    (size_t)((size - done) > (off_t)(1 << 20) ? (1 << 20) : (size - done)),
                                    0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            break;
        }
        done += n;
    }
    return (done == size) ? 0 : -1;
#else
    (void)src_fd;
    (void)dst_fd;
    (void)size;
    errno = ENOSYS;
    return -1;
#endif
}

static int cp_try_sendfile(int src_fd, int dst_fd, off_t size)
{
#if defined(CP_HAVE_SENDFILE)
    off_t off = 0;
    while (off < size) {
        ssize_t n = sendfile(dst_fd, src_fd, &off,
                             (size_t)((size - off) > (off_t)(1 << 20) ? (1 << 20) : (size - off)));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            break;
        }
    }
    return (off == size) ? 0 : -1;
#else
    (void)src_fd;
    (void)dst_fd;
    (void)size;
    errno = ENOSYS;
    return -1;
#endif
}
#endif

static int cp_copy_regular_rw(struct cp_context *ctx,
                              int src_fd,
                              int dst_fd,
                              const struct stat *src_st,
                              off_t *bytes_copied)
{
    size_t bufsz = cp_select_buffer_size(ctx->opts, src_st, dst_fd);
    unsigned char *buf;
    int made_hole = 0;

    buf = (unsigned char *)malloc(bufsz);
    if (!buf) {
        errno = ENOMEM;
        return -1;
    }

    *bytes_copied = 0;

    for (;;) {
        ssize_t n = cp_read_retry(src_fd, buf, bufsz);
        if (n < 0) {
            free(buf);
            return -1;
        }
        if (n == 0) {
            break;
        }

        if (ctx->opts->sparse_mode != CP_SPARSE_NEVER && cp_buf_all_zero(buf, (size_t)n)) {
            if (lseek(dst_fd, n, SEEK_CUR) >= 0) {
                made_hole = 1;
                *bytes_copied += n;
                continue;
            }
            if (errno != ESPIPE) {
                free(buf);
                return -1;
            }
            errno = 0;
        }

        if (cp_write_all(dst_fd, buf, (size_t)n) != 0) {
            free(buf);
            return -1;
        }
        *bytes_copied += n;

        if (ctx->stop_requested && *ctx->stop_requested) {
            errno = EINTR;
            free(buf);
            return -1;
        }
    }

    if (made_hole && src_st && ftruncate(dst_fd, src_st->st_size) != 0) {
        free(buf);
        return -1;
    }

    free(buf);
    return 0;
}

static int cp_link_mode_op(struct cp_context *ctx,
                           const char *src,
                           const char *dst,
                           const struct stat *dst_st,
                           int dst_exists)
{
    if (cp_should_skip_existing(ctx, dst, dst_exists)) {
        return 0;
    }

    if (cp_maybe_backup_destination(ctx, src, dst, dst_exists) != 0) {
        return -1;
    }
    if (dst_exists && ctx->opts->backup_control != CP_BACKUP_NONE) {
        dst_exists = 0;
    }

    if (dst_exists) {
        if (cp_remove_existing(dst, dst_st) != 0) {
            cp_diag(ctx, src, dst, "remove existing destination", errno);
            return -1;
        }
    }

    if (ctx->opts->link_mode == CP_LINKMODE_HARD) {
        if (link(src, dst) != 0) {
            cp_diag(ctx, src, dst, "hard link", errno);
            return -1;
        }
        cp_verbose(ctx, "hardlink %s -> %s", src, dst);
        return 0;
    }

#ifdef CP_HOST_BUILD
    if (symlink(src, dst) != 0) {
        cp_diag(ctx, src, dst, "symbolic link", errno);
        return -1;
    }
    cp_verbose(ctx, "symlink %s -> %s", src, dst);
    return 0;
#else
    (void)src;
    (void)dst;
    cp_diag(ctx, src, dst, "symbolic links unsupported on this target", ENOSYS);
    return -1;
#endif
}

static int cp_copy_symlink_obj(struct cp_context *ctx,
                               const char *src,
                               const struct stat *src_lstat,
                               const char *dst,
                               const struct stat *dst_st,
                               int dst_exists)
{
    char *target;
    size_t cap = 256;
    ssize_t n;

    if (cp_should_skip_existing(ctx, dst, dst_exists)) {
        return 0;
    }

    if (cp_maybe_backup_destination(ctx, src, dst, dst_exists) != 0) {
        return -1;
    }
    if (dst_exists && ctx->opts->backup_control != CP_BACKUP_NONE) {
        dst_exists = 0;
    }

    if (dst_exists) {
        if (cp_remove_existing(dst, dst_st) != 0) {
            cp_diag(ctx, src, dst, "remove existing destination", errno);
            return -1;
        }
    }

    target = (char *)malloc(cap);
    if (!target) {
        cp_diag(ctx, src, dst, "allocate symlink buffer", ENOMEM);
        return -1;
    }

    for (;;) {
        n = readlink(src, target, cap - 1);
        if (n < 0) {
            cp_diag(ctx, src, dst, "read symlink", errno);
            free(target);
            return -1;
        }
        if ((size_t)n < cap - 1) {
            break;
        }
        cap *= 2;
        target = (char *)realloc(target, cap);
        if (!target) {
            cp_diag(ctx, src, dst, "grow symlink buffer", ENOMEM);
            return -1;
        }
    }
    target[n] = '\0';

#ifdef CP_HOST_BUILD
    if (symlink(target, dst) != 0) {
        cp_diag(ctx, src, dst, "create symlink", errno);
        free(target);
        return -1;
    }
    cp_verbose(ctx, "copy symlink %s -> %s", src, dst);
#else
    cp_diag(ctx, src, dst, "symbolic links unsupported on this target", ENOSYS);
    free(target);
    return -1;
#endif

    if (ctx->opts->preserve_mode || ctx->opts->preserve_owner ||
        ctx->opts->preserve_timestamps || ctx->opts->preserve_all) {
        (void)cp_preserve_metadata(ctx->opts, src, src_lstat, dst, -1, 1,
                                   cp_preserve_warn_bridge, ctx);
    }

    free(target);
    return 0;
}

static int cp_copy_special(struct cp_context *ctx,
                           const char *src,
                           const struct stat *src_st,
                           const char *dst,
                           const struct stat *dst_st,
                           int dst_exists)
{
    if (cp_should_skip_existing(ctx, dst, dst_exists)) {
        return 0;
    }

    if (cp_maybe_backup_destination(ctx, src, dst, dst_exists) != 0) {
        return -1;
    }
    if (dst_exists && ctx->opts->backup_control != CP_BACKUP_NONE) {
        dst_exists = 0;
    }

    if (dst_exists) {
        if (cp_remove_existing(dst, dst_st) != 0) {
            cp_diag(ctx, src, dst, "remove existing destination", errno);
            return -1;
        }
    }

    if (S_ISFIFO(src_st->st_mode)) {
#ifdef CP_HOST_BUILD
        if (mkfifo(dst, src_st->st_mode & 07777) != 0) {
            cp_diag(ctx, src, dst, "create FIFO", errno);
            return -1;
        }
        cp_verbose(ctx, "copy fifo %s -> %s", src, dst);
#else
        cp_diag(ctx, src, dst, "special file creation unsupported on this target", ENOSYS);
        return -1;
#endif
    } else if (S_ISCHR(src_st->st_mode) || S_ISBLK(src_st->st_mode)) {
#ifdef CP_HOST_BUILD
        if (mknod(dst, src_st->st_mode, src_st->st_rdev) != 0) {
            cp_diag(ctx, src, dst, "create device node", errno);
            return -1;
        }
        cp_verbose(ctx, "copy special %s -> %s", src, dst);
#else
        cp_diag(ctx, src, dst, "device node creation unsupported on this target", ENOSYS);
        return -1;
#endif
    } else if (S_ISSOCK(src_st->st_mode)) {
        cp_diag(ctx, src, dst, "cannot copy socket node", EOPNOTSUPP);
        return -1;
    } else {
        cp_diag(ctx, src, dst, "unsupported special file", EOPNOTSUPP);
        return -1;
    }

    if (ctx->opts->preserve_mode || ctx->opts->preserve_owner ||
        ctx->opts->preserve_timestamps || ctx->opts->preserve_all) {
        (void)cp_preserve_metadata(ctx->opts, src, src_st, dst, -1, 0,
                                   cp_preserve_warn_bridge, ctx);
    }

    return 0;
}

static int cp_copy_regular(struct cp_context *ctx,
                           const char *src,
                           const struct stat *src_st,
                           const char *dst,
                           const struct stat *dst_st,
                           int dst_exists)
{
    int src_fd = -1;
    int dst_fd = -1;
    int rc = -1;
    char *tmp_path = NULL;
    int using_atomic = 0;
    mode_t create_mode = (src_st->st_mode & 0777);
    off_t bytes_copied = 0;

    if (dst_exists && src_st->st_dev == dst_st->st_dev && src_st->st_ino == dst_st->st_ino) {
        cp_diag(ctx, src, dst, "source and destination are the same file", EINVAL);
        return -1;
    }

    if (dst_exists && ctx->opts->update_only &&
        !cp_src_is_newer(src_st, dst_st)) {
        cp_verbose(ctx, "skip (destination newer or same age) %s -> %s", src, dst);
        return 0;
    }

    if (ctx->opts->preserve_links && src_st->st_nlink > 1) {
        const char *existing = cp_hardlink_map_get(&ctx->hardlinks, src_st->st_dev, src_st->st_ino);
        if (existing) {
            if (cp_should_skip_existing(ctx, dst, dst_exists)) {
                return 0;
            }
            if (dst_exists && cp_remove_existing(dst, dst_st) != 0) {
                cp_diag(ctx, src, dst, "remove existing destination", errno);
                return -1;
            }
            if (link(existing, dst) == 0) {
                return 0;
            }
            if (errno != EXDEV) {
                cp_diag(ctx, src, dst, "preserve hardlink", errno);
                return -1;
            }
            cp_warn(ctx, src, dst,
                    "cannot preserve hardlink across filesystems, copying data",
                    EXDEV);
        }
    }

    if (cp_should_skip_existing(ctx, dst, dst_exists)) {
        return 0;
    }

    src_fd = open(src, O_RDONLY, 0);
    if (src_fd < 0) {
        cp_diag(ctx, src, dst, "open source", errno);
        goto out;
    }

    if (ctx->opts->atomic_replace) {
        if (cp_atomic_open_temp(dst, 0600, &tmp_path, &dst_fd) != 0) {
            cp_diag(ctx, src, dst, "create atomic temporary file", errno);
            goto out;
        }
        using_atomic = 1;
    } else {
        if (dst_exists && S_ISDIR(dst_st->st_mode)) {
            cp_diag(ctx, src, dst, "destination is a directory", EISDIR);
            goto out;
        }
        dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, (int)create_mode);
        if (dst_fd < 0) {
            cp_diag(ctx, src, dst, "open destination", errno);
            goto out;
        }
    }

    if (ctx->opts->sparse_mode != CP_SPARSE_NEVER && src_st->st_size > 0) {
        off_t cur = lseek(src_fd, 0, SEEK_CUR);
        if (cur >= 0 && cp_copy_sparse_seek_hole(src_fd, dst_fd, src_st->st_size) == 0) {
            bytes_copied = src_st->st_size;
        } else {
            if (cur >= 0) {
                (void)lseek(src_fd, 0, SEEK_SET);
                (void)lseek(dst_fd, 0, SEEK_SET);
                (void)ftruncate(dst_fd, 0);
            }
            if (cp_copy_regular_rw(ctx, src_fd, dst_fd, src_st, &bytes_copied) != 0) {
                cp_diag(ctx, src, dst, "copy file data", errno);
                goto out;
            }
        }
    } else {
#ifdef CP_HOST_BUILD
        if (ctx->opts->sparse_mode == CP_SPARSE_NEVER && src_st->st_size > 0 &&
            cp_try_copy_file_range(src_fd, dst_fd, src_st->st_size) == 0) {
            bytes_copied = src_st->st_size;
        } else if (ctx->opts->sparse_mode == CP_SPARSE_NEVER && src_st->st_size > 0 &&
                   cp_try_sendfile(src_fd, dst_fd, src_st->st_size) == 0) {
            bytes_copied = src_st->st_size;
        } else
#endif
        {
            if (cp_copy_regular_rw(ctx, src_fd, dst_fd, src_st, &bytes_copied) != 0) {
                cp_diag(ctx, src, dst, "copy file data", errno);
                goto out;
            }
        }
    }

    if (ctx->opts->preserve_mode || ctx->opts->preserve_owner ||
        ctx->opts->preserve_timestamps || ctx->opts->preserve_all) {
        const char *dst_path_for_preserve = using_atomic ? tmp_path : dst;
        (void)cp_preserve_metadata(ctx->opts, src, src_st, dst_path_for_preserve,
                                   dst_fd, 0, cp_preserve_warn_bridge, ctx);
    }

    if (using_atomic) {
        if (cp_atomic_commit(dst_fd, tmp_path, dst) != 0) {
            cp_diag(ctx, src, dst, "atomic replace", errno);
            goto out;
        }
        dst_fd = -1;
    } else {
#ifdef CP_HOST_BUILD
        if (fsync(dst_fd) != 0 && errno != EINVAL) {
            cp_diag(ctx, src, dst, "fsync destination", errno);
            goto out;
        }
#endif
        if (close(dst_fd) != 0) {
            cp_diag(ctx, src, dst, "close destination", errno);
            dst_fd = -1;
            goto out;
        }
        dst_fd = -1;
    }

    if (ctx->opts->preserve_links && src_st->st_nlink > 1) {
        if (cp_hardlink_map_put(&ctx->hardlinks, src_st->st_dev, src_st->st_ino, dst) != 0) {
            cp_diag(ctx, src, dst, "record hardlink map", ENOMEM);
        }
    }

    if (ctx->opts->progress && !ctx->opts->force_silent) {
        fprintf(stderr, "%s: %s -> %s (%lld bytes)\n",
                ctx->progname, src, dst, (long long)bytes_copied);
    }

    rc = 0;
out:
    if (src_fd >= 0) {
        close(src_fd);
    }
    if (rc != 0) {
        if (using_atomic) {
            cp_atomic_cleanup(dst_fd, tmp_path);
            dst_fd = -1;
        } else if (dst_fd >= 0) {
            close(dst_fd);
            unlink(dst);
            dst_fd = -1;
        }
    }
    free(tmp_path);
    return rc;
}

static int cp_copy_path(struct cp_context *ctx,
                        const char *src,
                        const char *dst,
                        int is_cmdline_arg);

static int cp_copy_directory(struct cp_context *ctx,
                             const char *src,
                             const struct stat *src_st,
                             const char *dst,
                             const struct stat *dst_st,
                             int dst_exists)
{
    DIR *dir = NULL;
    struct dirent *de;
    int created = 0;

    if (!ctx->opts->recursive) {
        cp_diag(ctx, src, dst, "omitting directory (use -R)", EISDIR);
        return -1;
    }

    if (cp_devino_set_contains(&ctx->visited_dirs, src_st->st_dev, src_st->st_ino)) {
        cp_diag(ctx, src, dst, "detected recursive directory cycle", ELOOP);
        return -1;
    }

    if (cp_devino_set_insert(&ctx->visited_dirs, src_st->st_dev, src_st->st_ino) != 0) {
        cp_diag(ctx, src, dst, "track directory cycle set", ENOMEM);
        return -1;
    }

    if (dst_exists) {
        if (!S_ISDIR(dst_st->st_mode)) {
            cp_diag(ctx, src, dst, "destination exists and is not a directory", ENOTDIR);
            return -1;
        }
    } else {
        if (mkdir(dst, src_st->st_mode & 0777) != 0) {
            cp_diag(ctx, src, dst, "create destination directory", errno);
            return -1;
        }
        created = 1;
    }

    (void)created;

    dir = opendir(src);
    if (!dir) {
        cp_diag(ctx, src, dst, "open source directory", errno);
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
        char *child_src;
        char *child_dst;

        if (cp_path_is_dot_or_dotdot(de->d_name)) {
            continue;
        }

        child_src = cp_path_join(src, de->d_name);
        child_dst = cp_path_join(dst, de->d_name);
        if (!child_src || !child_dst) {
            free(child_src);
            free(child_dst);
            cp_diag(ctx, src, dst, "allocate child path", ENOMEM);
            closedir(dir);
            return -1;
        }

        (void)cp_copy_path(ctx, child_src, child_dst, 0);

        free(child_src);
        free(child_dst);

        if (ctx->stop_requested && *ctx->stop_requested) {
            closedir(dir);
            cp_diag(ctx, src, dst, "interrupted", EINTR);
            return -1;
        }
    }

    closedir(dir);

    if (ctx->opts->preserve_mode || ctx->opts->preserve_owner ||
        ctx->opts->preserve_timestamps || ctx->opts->preserve_all) {
        (void)cp_preserve_metadata(ctx->opts, src, src_st, dst, -1, 0,
                                   cp_preserve_warn_bridge, ctx);
    }

    return 0;
}

static int cp_copy_path(struct cp_context *ctx,
                        const char *src,
                        const char *dst,
                        int is_cmdline_arg)
{
    struct stat src_lstat;
    struct stat src_stat;
    struct stat dst_lstat;
    int dst_exists = 0;
    int follow_symlink;

    if (ctx->stop_requested && *ctx->stop_requested) {
        cp_diag(ctx, src, dst, "interrupted", EINTR);
        return -1;
    }

    if (lstat(src, &src_lstat) != 0) {
        cp_diag(ctx, src, dst, "lstat source", errno);
        return -1;
    }

    if (lstat(dst, &dst_lstat) == 0) {
        dst_exists = 1;
    } else if (errno != ENOENT) {
        cp_diag(ctx, src, dst, "lstat destination", errno);
        return -1;
    }

    if (ctx->opts->link_mode != CP_LINKMODE_COPY) {
        return cp_link_mode_op(ctx, src, dst, &dst_lstat, dst_exists);
    }

    follow_symlink = S_ISLNK(src_lstat.st_mode) && cp_is_symlink_followed(ctx->opts, is_cmdline_arg);

    if (follow_symlink) {
        if (stat(src, &src_stat) != 0) {
            cp_diag(ctx, src, dst, "stat source symlink target", errno);
            return -1;
        }
    } else {
        src_stat = src_lstat;
    }

    if (S_ISLNK(src_lstat.st_mode) && !follow_symlink) {
        return cp_copy_symlink_obj(ctx, src, &src_lstat, dst, &dst_lstat, dst_exists);
    }

    if (S_ISDIR(src_stat.st_mode)) {
        return cp_copy_directory(ctx, src, &src_stat, dst, &dst_lstat, dst_exists);
    }

    if (S_ISREG(src_stat.st_mode)) {
        return cp_copy_regular(ctx, src, &src_stat, dst, &dst_lstat, dst_exists);
    }

    return cp_copy_special(ctx, src, &src_stat, dst, &dst_lstat, dst_exists);
}

int cp_context_init(struct cp_context *ctx,
                    const struct cp_options *opts,
                    const char *progname,
                    volatile sig_atomic_t *stop_requested)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->progname = progname;
    ctx->opts = opts;
    ctx->stop_requested = stop_requested;

    if (cp_hardlink_map_init(&ctx->hardlinks) != 0) {
        return -1;
    }
    if (cp_devino_set_init(&ctx->visited_dirs) != 0) {
        cp_hardlink_map_destroy(&ctx->hardlinks);
        return -1;
    }

    return 0;
}

void cp_context_destroy(struct cp_context *ctx)
{
    cp_hardlink_map_destroy(&ctx->hardlinks);
    cp_devino_set_destroy(&ctx->visited_dirs);
}

int cp_execute(struct cp_context *ctx, int argc, char **argv)
{
    struct stat dst_stat;
    int dst_is_dir = 0;
    int i;
    (void)argc;

    if (ctx->opts->source_count > 1) {
        if (stat(ctx->opts->dest, &dst_stat) != 0) {
            cp_diag(ctx, NULL, ctx->opts->dest,
                    "destination for multiple sources must exist and be a directory",
                    errno);
            return 1;
        }
        if (!S_ISDIR(dst_stat.st_mode)) {
            cp_diag(ctx, NULL, ctx->opts->dest,
                    "destination for multiple sources must be a directory",
                    ENOTDIR);
            return 1;
        }
        dst_is_dir = 1;
    } else {
        if (stat(ctx->opts->dest, &dst_stat) == 0 && S_ISDIR(dst_stat.st_mode)) {
            dst_is_dir = 1;
        }
    }

    for (i = 0; i < ctx->opts->source_count; ++i) {
        const char *src = argv[ctx->opts->source_start + i];
        char *dst_path;

        if (dst_is_dir) {
            const char *base = cp_path_basename(src);
            dst_path = cp_path_join(ctx->opts->dest, base);
        } else {
            dst_path = strdup(ctx->opts->dest);
        }

        if (!dst_path) {
            cp_diag(ctx, src, ctx->opts->dest, "allocate destination path", ENOMEM);
            continue;
        }

        (void)cp_copy_path(ctx, src, dst_path, 1);
        free(dst_path);

        if (ctx->stop_requested && *ctx->stop_requested) {
            cp_diag(ctx, NULL, NULL, "interrupted", EINTR);
            break;
        }
    }

    return ctx->had_error ? 1 : 0;
}
