#include <liblink.h>

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct ln_plan {
    bool use_target_dir;
    const char *target_dir;
    const char *single_target;
    int src_start;
    int src_count;
};

static const char g_ln_usage[] =
    "Usage: ln [OPTION]... [-T] TARGET LINK_NAME\n"
    "       ln [OPTION]... TARGET\n"
    "       ln [OPTION]... TARGET... DIRECTORY\n"
    "       ln [OPTION]... -t DIRECTORY TARGET...\n"
    "\n"
    "Options:\n"
    "  -f, --force                  remove existing destination\n"
    "  -i, --interactive            prompt before overwrite\n"
    "  -s, --symbolic               make symbolic links\n"
    "  -L, --logical                dereference SOURCE symlinks (hard links)\n"
    "  -P, --physical               hard-link SOURCE symlink itself\n"
    "  -h, -n, --no-dereference     do not follow destination symlink-to-dir\n"
    "  -F                           with -s, remove destination directory\n"
    "  -w                           with -s, warn if SOURCE does not exist\n"
    "  -v, --verbose                print each link action\n"
    "  -b, --backup[=METHOD]        make destination backups\n"
    "  -S, --suffix=SUFFIX          backup suffix\n"
    "  -t, --target-directory=DIR   use DIR for all destination paths\n"
    "  -T, --no-target-directory    treat destination as a normal file\n"
    "  -r, --relative               with -s, create relative symlink targets\n"
    "  -d, --directory              attempt hard links to directories\n"
    "      --help                   display this help and exit\n";

static void
ln_diag(const ln_options_t *opts, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: ", opts->progname ? opts->progname : "ln");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void
ln_diag_errno(const ln_options_t *opts, const char *path, const char *action)
{
    ln_diag(opts, "%s: %s: %s", path, action, strerror(errno));
}

static char *
ln_strdup(const char *s)
{
    size_t len;
    char *out;

    if (!s) {
        errno = EINVAL;
        return NULL;
    }

    len = strlen(s);
    out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len + 1);
    return out;
}

static const char *
ln_basename_const(const char *path)
{
    size_t len;
    const char *base;

    if (!path || *path == '\0') {
        return path ? path : "";
    }

    len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        --len;
    }

    if (len == 1 && path[0] == '/') {
        return path;
    }

    base = path + len;
    while (base > path && base[-1] != '/') {
        --base;
    }

    return base;
}

static char *
ln_dirname_copy(const char *path)
{
    size_t len;
    size_t i;
    char *out;

    if (!path || *path == '\0') {
        return ln_strdup(".");
    }

    len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        --len;
    }

    if (len == 1 && path[0] == '/') {
        return ln_strdup("/");
    }

    i = len;
    while (i > 0 && path[i - 1] != '/') {
        --i;
    }

    if (i == 0) {
        return ln_strdup(".");
    }

    while (i > 1 && path[i - 1] == '/') {
        --i;
    }

    out = (char *)malloc(i + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, path, i);
    out[i] = '\0';
    return out;
}

static char *
ln_path_join(const char *left, const char *right)
{
    size_t llen;
    size_t rlen;
    size_t need_slash;
    char *out;

    if (!left || !right) {
        errno = EINVAL;
        return NULL;
    }

    llen = strlen(left);
    rlen = strlen(right);
    need_slash = (llen > 0 && left[llen - 1] != '/') ? 1U : 0U;

    out = (char *)malloc(llen + need_slash + rlen + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, left, llen);
    if (need_slash) {
        out[llen] = '/';
    }
    memcpy(out + llen + need_slash, right, rlen);
    out[llen + need_slash + rlen] = '\0';

    return out;
}

static char *
ln_path_normalize_copy(const char *path)
{
    size_t len;
    bool abs;
    char *tmp;
    char *saveptr;
    char *tok;
    char **segs;
    size_t seg_cap;
    size_t seg_len;
    size_t out_cap;
    char *out;
    size_t pos;
    size_t i;

    if (!path) {
        errno = EINVAL;
        return NULL;
    }

    if (*path == '\0') {
        return ln_strdup(".");
    }

    len = strlen(path);
    abs = (path[0] == '/');

    tmp = ln_strdup(path);
    if (!tmp) {
        return NULL;
    }

    seg_cap = len + 1;
    segs = (char **)calloc(seg_cap, sizeof(char *));
    if (!segs) {
        free(tmp);
        return NULL;
    }

    seg_len = 0;
    saveptr = NULL;
    tok = strtok_r(tmp, "/", &saveptr);
    while (tok) {
        if (strcmp(tok, ".") == 0 || tok[0] == '\0') {
            tok = strtok_r(NULL, "/", &saveptr);
            continue;
        }
        if (strcmp(tok, "..") == 0) {
            if (seg_len > 0 && strcmp(segs[seg_len - 1], "..") != 0) {
                --seg_len;
            } else if (!abs) {
                segs[seg_len++] = tok;
            }
            tok = strtok_r(NULL, "/", &saveptr);
            continue;
        }
        segs[seg_len++] = tok;
        tok = strtok_r(NULL, "/", &saveptr);
    }

    out_cap = len + 4;
    out = (char *)malloc(out_cap);
    if (!out) {
        free(segs);
        free(tmp);
        return NULL;
    }

    pos = 0;
    if (seg_len == 0) {
        if (abs) {
            out[pos++] = '/';
        } else {
            out[pos++] = '.';
        }
        out[pos] = '\0';
        free(segs);
        free(tmp);
        return out;
    }

    if (abs) {
        out[pos++] = '/';
    }

    for (i = 0; i < seg_len; ++i) {
        size_t slen = strlen(segs[i]);
        if (i > 0) {
            out[pos++] = '/';
        }
        memcpy(out + pos, segs[i], slen);
        pos += slen;
    }
    out[pos] = '\0';

    free(segs);
    free(tmp);
    return out;
}

static char *
ln_path_absolute_normalized(const char *path)
{
    char cwd[PATH_MAX];
    char *joined;
    char *norm;

    if (!path) {
        errno = EINVAL;
        return NULL;
    }

    if (path[0] == '/') {
        return ln_path_normalize_copy(path);
    }

    if (!getcwd(cwd, sizeof(cwd))) {
        return NULL;
    }

    joined = ln_path_join(cwd, path);
    if (!joined) {
        return NULL;
    }

    norm = ln_path_normalize_copy(joined);
    free(joined);
    return norm;
}

static int
ln_append_text(char **buf, size_t *cap, size_t *len, const char *text)
{
    size_t tlen;
    char *grown;

    if (!buf || !cap || !len || !text) {
        errno = EINVAL;
        return -1;
    }

    tlen = strlen(text);
    if (*len + tlen + 1 > *cap) {
        size_t ncap = *cap;
        while (*len + tlen + 1 > ncap) {
            ncap *= 2;
        }
        grown = (char *)realloc(*buf, ncap);
        if (!grown) {
            return -1;
        }
        *buf = grown;
        *cap = ncap;
    }

    memcpy(*buf + *len, text, tlen);
    *len += tlen;
    (*buf)[*len] = '\0';
    return 0;
}

static void
ln_components_free(char **parts, size_t count)
{
    size_t i;
    if (!parts) {
        return;
    }
    for (i = 0; i < count; ++i) {
        free(parts[i]);
    }
    free(parts);
}

static int
ln_split_abs_components(const char *abs_path, char ***out_parts, size_t *out_count)
{
    char *tmp;
    char *saveptr;
    char *tok;
    char **parts;
    size_t count;
    size_t cap;

    if (!abs_path || abs_path[0] != '/' || !out_parts || !out_count) {
        errno = EINVAL;
        return -1;
    }

    tmp = ln_strdup(abs_path + 1);
    if (!tmp) {
        return -1;
    }

    cap = strlen(abs_path) + 1;
    parts = (char **)calloc(cap, sizeof(char *));
    if (!parts) {
        free(tmp);
        return -1;
    }

    count = 0;
    saveptr = NULL;
    tok = strtok_r(tmp, "/", &saveptr);
    while (tok) {
        parts[count] = ln_strdup(tok);
        if (!parts[count]) {
            ln_components_free(parts, count);
            free(tmp);
            return -1;
        }
        ++count;
        tok = strtok_r(NULL, "/", &saveptr);
    }

    free(tmp);
    *out_parts = parts;
    *out_count = count;
    return 0;
}

static char *
ln_relative_from_to(const char *target_abs, const char *base_abs)
{
    char **target_parts;
    char **base_parts;
    size_t target_count;
    size_t base_count;
    size_t i;
    size_t cap;
    size_t len;
    char *out;

    if (!target_abs || !base_abs || target_abs[0] != '/' || base_abs[0] != '/') {
        errno = EINVAL;
        return NULL;
    }

    if (ln_split_abs_components(target_abs, &target_parts, &target_count) != 0) {
        return NULL;
    }

    if (ln_split_abs_components(base_abs, &base_parts, &base_count) != 0) {
        ln_components_free(target_parts, target_count);
        return NULL;
    }

    i = 0;
    while (i < target_count && i < base_count && strcmp(target_parts[i], base_parts[i]) == 0) {
        ++i;
    }

    cap = 64;
    len = 0;
    out = (char *)malloc(cap);
    if (!out) {
        ln_components_free(target_parts, target_count);
        ln_components_free(base_parts, base_count);
        return NULL;
    }
    out[0] = '\0';

    while (i < base_count) {
        if (ln_append_text(&out, &cap, &len, "../") != 0) {
            free(out);
            ln_components_free(target_parts, target_count);
            ln_components_free(base_parts, base_count);
            return NULL;
        }
        ++i;
    }

    i = 0;
    while (i < target_count && i < base_count && strcmp(target_parts[i], base_parts[i]) == 0) {
        ++i;
    }

    while (i < target_count) {
        if (ln_append_text(&out, &cap, &len, target_parts[i]) != 0) {
            free(out);
            ln_components_free(target_parts, target_count);
            ln_components_free(base_parts, base_count);
            return NULL;
        }
        if (i + 1 < target_count) {
            if (ln_append_text(&out, &cap, &len, "/") != 0) {
                free(out);
                ln_components_free(target_parts, target_count);
                ln_components_free(base_parts, base_count);
                return NULL;
            }
        }
        ++i;
    }

    if (len == 0) {
        if (ln_append_text(&out, &cap, &len, ".") != 0) {
            free(out);
            ln_components_free(target_parts, target_count);
            ln_components_free(base_parts, base_count);
            return NULL;
        }
    }

    ln_components_free(target_parts, target_count);
    ln_components_free(base_parts, base_count);

    if (len >= 3 && strcmp(out + len - 3, "../") == 0) {
        out[len - 1] = '\0';
    }

    return out;
}

static char *
ln_readlink_dup(const char *path)
{
    char buf[PATH_MAX + 1];
    ssize_t n;

    n = readlink(path, buf, PATH_MAX);
    if (n < 0) {
        return NULL;
    }

    buf[n] = '\0';
    return ln_strdup(buf);
}

static bool
ln_same_entry(const char *left, const char *right)
{
    struct stat a;
    struct stat b;

    if (!left || !right) {
        return false;
    }

    if (lstat(left, &a) != 0) {
        return false;
    }

    if (lstat(right, &b) != 0) {
        return false;
    }

    return (a.st_dev == b.st_dev) && (a.st_ino == b.st_ino);
}

static bool
ln_exists_lstat(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

static int
ln_stat_target_operand(const char *path, bool no_follow, struct stat *out)
{
    if (no_follow) {
        return lstat(path, out);
    }
    return stat(path, out);
}

static bool
ln_is_existing_dir_operand(const char *path, bool no_follow)
{
    struct stat st;

    if (ln_stat_target_operand(path, no_follow, &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

#ifdef LINK_HAVE_SYMLINK
static int
ln_make_symlink(const char *target, const char *linkpath)
{
    return symlink(target, linkpath);
}
#else
static int
ln_make_symlink(const char *target, const char *linkpath)
{
    (void)target;
    (void)linkpath;
    errno = ENOSYS;
    return -1;
}
#endif

static char *
ln_hard_source_path(const char *source, ln_source_deref_mode_t mode)
{
    struct stat st;
    char *target;
    char *dir;
    char *joined;
    char *norm;

    if (mode == LN_DEREF_PHYSICAL) {
        return ln_strdup(source);
    }

    if (lstat(source, &st) != 0) {
        return NULL;
    }

    if (!S_ISLNK(st.st_mode)) {
        return ln_strdup(source);
    }

    target = ln_readlink_dup(source);
    if (!target) {
        return NULL;
    }

    if (target[0] == '/') {
        norm = ln_path_normalize_copy(target);
        free(target);
        return norm;
    }

    dir = ln_dirname_copy(source);
    if (!dir) {
        free(target);
        return NULL;
    }

    joined = ln_path_join(dir, target);
    free(dir);
    free(target);
    if (!joined) {
        return NULL;
    }

    norm = ln_path_normalize_copy(joined);
    free(joined);
    return norm;
}

static int
ln_source_is_directory_for_hardlink(const char *source, const char *hard_source,
                                    ln_source_deref_mode_t mode)
{
    struct stat st;
    int rc;

    if (mode == LN_DEREF_PHYSICAL) {
        rc = lstat(source, &st);
    } else {
        rc = stat(hard_source ? hard_source : source, &st);
    }

    if (rc != 0) {
        return -1;
    }

    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static ln_backup_mode_t
ln_backup_mode_from_string(const char *method, bool *ok)
{
    if (!method || *method == '\0') {
        if (ok) {
            *ok = true;
        }
        return LN_BACKUP_EXISTING;
    }

    if (strcmp(method, "none") == 0 || strcmp(method, "off") == 0) {
        if (ok) {
            *ok = true;
        }
        return LN_BACKUP_NONE;
    }

    if (strcmp(method, "simple") == 0 || strcmp(method, "never") == 0) {
        if (ok) {
            *ok = true;
        }
        return LN_BACKUP_SIMPLE;
    }

    if (strcmp(method, "numbered") == 0 || strcmp(method, "t") == 0) {
        if (ok) {
            *ok = true;
        }
        return LN_BACKUP_NUMBERED;
    }

    if (strcmp(method, "existing") == 0 || strcmp(method, "nil") == 0) {
        if (ok) {
            *ok = true;
        }
        return LN_BACKUP_EXISTING;
    }

    if (ok) {
        *ok = false;
    }
    return LN_BACKUP_NONE;
}

static char *
ln_backup_simple_path(const char *dst, const char *suffix)
{
    size_t dlen;
    size_t slen;
    char *out;

    dlen = strlen(dst);
    slen = strlen(suffix);

    out = (char *)malloc(dlen + slen + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, dst, dlen);
    memcpy(out + dlen, suffix, slen);
    out[dlen + slen] = '\0';

    return out;
}

static char *
ln_backup_numbered_path(const char *dst, unsigned n)
{
    int nlen;
    size_t dlen;
    char *out;

    dlen = strlen(dst);
    nlen = snprintf(NULL, 0, ".~%u~", n);
    if (nlen < 0) {
        return NULL;
    }

    out = (char *)malloc(dlen + (size_t)nlen + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, dst, dlen);
    snprintf(out + dlen, (size_t)nlen + 1, ".~%u~", n);
    return out;
}

static char *
ln_backup_pick_path(const ln_options_t *opts, const char *dst)
{
    const char *suffix;
    ln_backup_mode_t mode;
    unsigned n;
    bool saw_numbered;
    char *cand;

    mode = opts->backup_mode;
    suffix = opts->backup_suffix;
    if (!suffix || *suffix == '\0') {
        suffix = getenv("SIMPLE_BACKUP_SUFFIX");
    }
    if (!suffix || *suffix == '\0') {
        suffix = "~";
    }

    if (mode == LN_BACKUP_SIMPLE) {
        return ln_backup_simple_path(dst, suffix);
    }

    saw_numbered = false;
    for (n = 1; n < 100000U; ++n) {
        cand = ln_backup_numbered_path(dst, n);
        if (!cand) {
            return NULL;
        }
        if (ln_exists_lstat(cand)) {
            saw_numbered = true;
            free(cand);
            continue;
        }

        if (mode == LN_BACKUP_NUMBERED) {
            return cand;
        }

        if (mode == LN_BACKUP_EXISTING) {
            if (saw_numbered) {
                return cand;
            }
            free(cand);
            return ln_backup_simple_path(dst, suffix);
        }

        free(cand);
        break;
    }

    if (mode == LN_BACKUP_NUMBERED) {
        errno = ENOSPC;
        return NULL;
    }

    return ln_backup_simple_path(dst, suffix);
}

static int
ln_backup_destination(const ln_options_t *opts, const char *dst)
{
    char *backup;

    if (opts->backup_mode == LN_BACKUP_NONE) {
        return 0;
    }

    backup = ln_backup_pick_path(opts, dst);
    if (!backup) {
        return -1;
    }

    if (rename(dst, backup) != 0) {
        free(backup);
        return -1;
    }

    free(backup);
    return 0;
}

static int
ln_prompt_replace(const ln_options_t *opts, const char *dst)
{
    char ans[32];

    fprintf(stderr, "%s: replace '%s'? ", opts->progname ? opts->progname : "ln", dst);
    fflush(stderr);

    if (!fgets(ans, sizeof(ans), stdin)) {
        return 0;
    }

    return (ans[0] == 'y' || ans[0] == 'Y') ? 1 : 0;
}

static char *
ln_compute_symlink_target(const ln_options_t *opts, const char *source, const char *dest)
{
    char *source_abs;
    char *dest_dir;
    char *dest_dir_abs;
    char *rel;

    if (!opts->relative) {
        return ln_strdup(source);
    }

    source_abs = ln_path_absolute_normalized(source);
    if (!source_abs) {
        return NULL;
    }

    dest_dir = ln_dirname_copy(dest);
    if (!dest_dir) {
        free(source_abs);
        return NULL;
    }

    dest_dir_abs = ln_path_absolute_normalized(dest_dir);
    free(dest_dir);
    if (!dest_dir_abs) {
        free(source_abs);
        return NULL;
    }

    rel = ln_relative_from_to(source_abs, dest_dir_abs);
    free(source_abs);
    free(dest_dir_abs);
    return rel;
}

static int
ln_process_one(const ln_options_t *opts, const char *source, const char *dest)
{
    struct stat dst_st;
    bool dst_exists;
    bool replace_dir_with_f;
    bool need_replace;
    char *symlink_target;
    char *hard_source;
    const char *same_entry_source;
    int rc;
    int source_is_dir;

    symlink_target = NULL;
    hard_source = NULL;

    if (opts->symbolic) {
        symlink_target = ln_compute_symlink_target(opts, source, dest);
        if (!symlink_target) {
            ln_diag_errno(opts, source, "compute link target");
            return -1;
        }
        same_entry_source = source;
    } else {
        hard_source = ln_hard_source_path(source, opts->source_deref);
        if (!hard_source) {
            ln_diag_errno(opts, source, "resolve source");
            return -1;
        }
        same_entry_source = hard_source;

        source_is_dir = ln_source_is_directory_for_hardlink(source, hard_source, opts->source_deref);
        if (source_is_dir == 1 && !opts->allow_hardlink_dir) {
            errno = EPERM;
            ln_diag_errno(opts, source, "hard links to directories require -d/--directory");
            free(hard_source);
            return -1;
        }
    }

    if (lstat(dest, &dst_st) == 0) {
        dst_exists = true;
    } else if (errno == ENOENT) {
        dst_exists = false;
    } else {
        ln_diag_errno(opts, dest, "inspect destination");
        free(symlink_target);
        free(hard_source);
        return -1;
    }

    if (dst_exists && ln_same_entry(same_entry_source, dest)) {
        ln_diag(opts, "%s and %s are the same file", source, dest);
        free(symlink_target);
        free(hard_source);
        return -1;
    }

    replace_dir_with_f = opts->symbolic && opts->bsd_remove_target_dir &&
                         dst_exists && S_ISDIR(dst_st.st_mode);

    if (dst_exists) {
        need_replace = false;

        if (opts->replace_mode == LN_REPLACE_INTERACTIVE) {
            if (!ln_prompt_replace(opts, dest)) {
                ln_diag(opts, "not replacing %s", dest);
                free(symlink_target);
                free(hard_source);
                return -1;
            }
            need_replace = true;
        } else if (opts->replace_mode == LN_REPLACE_FORCE) {
            need_replace = true;
        } else if (opts->backup_mode != LN_BACKUP_NONE) {
            need_replace = true;
        } else if (replace_dir_with_f) {
            need_replace = true;
        }

        if (!need_replace) {
            ln_diag(opts, "%s: destination exists", dest);
            free(symlink_target);
            free(hard_source);
            return -1;
        }

        if (opts->backup_mode != LN_BACKUP_NONE) {
            if (ln_backup_destination(opts, dest) != 0) {
                ln_diag_errno(opts, dest, "create backup");
                free(symlink_target);
                free(hard_source);
                return -1;
            }
        } else {
            if (replace_dir_with_f) {
                rc = rmdir(dest);
            } else {
                rc = unlink(dest);
            }
            if (rc != 0) {
                ln_diag_errno(opts, dest, "remove destination");
                free(symlink_target);
                free(hard_source);
                return -1;
            }
        }
    }

    if (opts->symbolic) {
        struct stat src_st;
        if (opts->warn_missing && lstat(source, &src_st) != 0) {
            ln_diag(opts, "warning: symbolic target does not exist: %s", source);
        }

        if (ln_make_symlink(symlink_target, dest) != 0) {
            ln_diag_errno(opts, dest, "create symbolic link");
            free(symlink_target);
            free(hard_source);
            return -1;
        }

        if (opts->verbose) {
            printf("%s -> %s\n", dest, symlink_target);
        }
    } else {
        if (link(hard_source, dest) != 0) {
            ln_diag_errno(opts, dest, "create hard link");
            free(symlink_target);
            free(hard_source);
            return -1;
        }

        if (opts->verbose) {
            printf("%s => %s\n", dest, hard_source);
        }
    }

    free(symlink_target);
    free(hard_source);
    return 0;
}

static int
ln_build_plan(const ln_options_t *opts, int argc, char **argv, int operand_index,
              struct ln_plan *plan)
{
    int n_operands;

    n_operands = argc - operand_index;
    if (n_operands <= 0) {
        ln_diag(opts, "missing file operand");
        return -1;
    }

    memset(plan, 0, sizeof(*plan));

    if (opts->target_directory) {
        if (!ln_is_existing_dir_operand(opts->target_directory, opts->no_target_deref)) {
            ln_diag(opts, "%s: target directory does not exist", opts->target_directory);
            return -1;
        }

        plan->use_target_dir = true;
        plan->target_dir = opts->target_directory;
        plan->src_start = operand_index;
        plan->src_count = n_operands;
        return 0;
    }

    if (n_operands == 1) {
        ln_diag(opts, "missing destination file operand after '%s'", argv[operand_index]);
        return -1;
    }

    if (n_operands == 2) {
        const char *dst = argv[operand_index + 1];
        bool dst_is_dir = ln_is_existing_dir_operand(dst, opts->no_target_deref);

        if (opts->symbolic && opts->bsd_remove_target_dir && dst_is_dir) {
            plan->use_target_dir = false;
            plan->single_target = dst;
            plan->src_start = operand_index;
            plan->src_count = 1;
            return 0;
        }

        if (!opts->no_target_directory && dst_is_dir) {
            plan->use_target_dir = true;
            plan->target_dir = dst;
            plan->src_start = operand_index;
            plan->src_count = 1;
            return 0;
        }

        plan->use_target_dir = false;
        plan->single_target = dst;
        plan->src_start = operand_index;
        plan->src_count = 1;
        return 0;
    }

    if (opts->no_target_directory) {
        ln_diag(opts, "-T/--no-target-directory cannot be used with multiple sources");
        return -1;
    }

    plan->target_dir = argv[argc - 1];
    if (!ln_is_existing_dir_operand(plan->target_dir, opts->no_target_deref)) {
        ln_diag(opts, "%s: target is not an existing directory", plan->target_dir);
        return -1;
    }

    plan->use_target_dir = true;
    plan->src_start = operand_index;
    plan->src_count = n_operands - 1;
    return 0;
}

void
ln_options_init(ln_options_t *opts, const char *progname)
{
    if (!opts) {
        return;
    }

    memset(opts, 0, sizeof(*opts));
    opts->progname = progname ? progname : "ln";
    opts->source_deref = LN_DEREF_LOGICAL;
    opts->replace_mode = LN_REPLACE_DEFAULT;
    opts->backup_mode = LN_BACKUP_NONE;
    opts->backup_suffix = NULL;
}

const char *
ln_usage(void)
{
    return g_ln_usage;
}

static int
ln_apply_parsed_option(ln_options_t *opts, char opt, const char *optval,
                       bool *seen_force, bool *seen_interactive)
{
    bool ok;
    const char *version_control;

    switch (opt) {
    case 'b':
        if (optval && *optval != '\0') {
            opts->backup_mode = ln_backup_mode_from_string(optval, &ok);
            if (!ok) {
                ln_diag(opts, "invalid backup method '%s'", optval);
                return -1;
            }
        } else {
            version_control = getenv("VERSION_CONTROL");
            opts->backup_mode = ln_backup_mode_from_string(version_control, &ok);
            if (!ok) {
                opts->backup_mode = LN_BACKUP_EXISTING;
            }
        }
        return 0;
    case 'S':
        if (!optval) {
            ln_diag(opts, "-S/--suffix requires an argument");
            return -1;
        }
        opts->backup_suffix = optval;
        return 0;
    case 't':
        if (!optval) {
            ln_diag(opts, "-t/--target-directory requires an argument");
            return -1;
        }
        opts->target_directory = optval;
        return 0;
    case 'T':
        opts->no_target_directory = true;
        return 0;
    case 'r':
        opts->relative = true;
        return 0;
    case 'd':
        opts->allow_hardlink_dir = true;
        return 0;
    case 'F':
        opts->bsd_remove_target_dir = true;
        return 0;
    case 'f':
        opts->replace_mode = LN_REPLACE_FORCE;
        opts->warn_missing = false;
        *seen_force = true;
        return 0;
    case 'i':
        opts->replace_mode = LN_REPLACE_INTERACTIVE;
        *seen_interactive = true;
        return 0;
    case 'h':
    case 'n':
        opts->no_target_deref = true;
        return 0;
    case 'L':
        opts->source_deref = LN_DEREF_LOGICAL;
        return 0;
    case 'P':
        opts->source_deref = LN_DEREF_PHYSICAL;
        return 0;
    case 's':
        opts->symbolic = true;
        return 0;
    case 'v':
        opts->verbose = true;
        return 0;
    case 'w':
        opts->warn_missing = true;
        return 0;
    default:
        ln_diag(opts, "invalid option -- '%c'", opt);
        return -1;
    }
}

static int
ln_parse_long_option(ln_options_t *opts, const char *arg, int argc, char **argv, int *index,
                     int *show_help, bool *seen_force, bool *seen_interactive)
{
    const char *name;
    const char *eq;
    const char *value;
    size_t nlen;

    name = arg + 2;
    eq = strchr(name, '=');
    nlen = eq ? (size_t)(eq - name) : strlen(name);
    value = eq ? (eq + 1) : NULL;

    if (nlen == 4 && strncmp(name, "help", 4) == 0) {
        if (value) {
            ln_diag(opts, "--help does not accept an argument");
            return -1;
        }
        *show_help = 1;
        return 1;
    }

    if (nlen == 6 && strncmp(name, "backup", 6) == 0) {
        return ln_apply_parsed_option(opts, 'b', value, seen_force, seen_interactive);
    }

    if (nlen == 6 && strncmp(name, "suffix", 6) == 0) {
        if (!value) {
            if (*index + 1 >= argc) {
                ln_diag(opts, "--suffix requires an argument");
                return -1;
            }
            ++(*index);
            value = argv[*index];
        }
        return ln_apply_parsed_option(opts, 'S', value, seen_force, seen_interactive);
    }

    if (nlen == 16 && strncmp(name, "target-directory", 16) == 0) {
        if (!value) {
            if (*index + 1 >= argc) {
                ln_diag(opts, "--target-directory requires an argument");
                return -1;
            }
            ++(*index);
            value = argv[*index];
        }
        return ln_apply_parsed_option(opts, 't', value, seen_force, seen_interactive);
    }

    if (nlen == 19 && strncmp(name, "no-target-directory", 19) == 0) {
        if (value) {
            ln_diag(opts, "--no-target-directory does not accept an argument");
            return -1;
        }
        return ln_apply_parsed_option(opts, 'T', NULL, seen_force, seen_interactive);
    }

    if (nlen == 8 && strncmp(name, "relative", 8) == 0) {
        if (value) {
            ln_diag(opts, "--relative does not accept an argument");
            return -1;
        }
        return ln_apply_parsed_option(opts, 'r', NULL, seen_force, seen_interactive);
    }

    if (nlen == 9 && strncmp(name, "directory", 9) == 0) {
        if (value) {
            ln_diag(opts, "--directory does not accept an argument");
            return -1;
        }
        return ln_apply_parsed_option(opts, 'd', NULL, seen_force, seen_interactive);
    }

    if (nlen == 5 && strncmp(name, "force", 5) == 0) {
        if (value) {
            ln_diag(opts, "--force does not accept an argument");
            return -1;
        }
        return ln_apply_parsed_option(opts, 'f', NULL, seen_force, seen_interactive);
    }

    if (nlen == 11 && strncmp(name, "interactive", 11) == 0) {
        if (value) {
            ln_diag(opts, "--interactive does not accept an argument");
            return -1;
        }
        return ln_apply_parsed_option(opts, 'i', NULL, seen_force, seen_interactive);
    }

    if (nlen == 8 && strncmp(name, "symbolic", 8) == 0) {
        if (value) {
            ln_diag(opts, "--symbolic does not accept an argument");
            return -1;
        }
        return ln_apply_parsed_option(opts, 's', NULL, seen_force, seen_interactive);
    }

    if (nlen == 7 && strncmp(name, "logical", 7) == 0) {
        if (value) {
            ln_diag(opts, "--logical does not accept an argument");
            return -1;
        }
        return ln_apply_parsed_option(opts, 'L', NULL, seen_force, seen_interactive);
    }

    if (nlen == 8 && strncmp(name, "physical", 8) == 0) {
        if (value) {
            ln_diag(opts, "--physical does not accept an argument");
            return -1;
        }
        return ln_apply_parsed_option(opts, 'P', NULL, seen_force, seen_interactive);
    }

    if (nlen == 14 && strncmp(name, "no-dereference", 14) == 0) {
        if (value) {
            ln_diag(opts, "--no-dereference does not accept an argument");
            return -1;
        }
        return ln_apply_parsed_option(opts, 'h', NULL, seen_force, seen_interactive);
    }

    if (nlen == 7 && strncmp(name, "verbose", 7) == 0) {
        if (value) {
            ln_diag(opts, "--verbose does not accept an argument");
            return -1;
        }
        return ln_apply_parsed_option(opts, 'v', NULL, seen_force, seen_interactive);
    }

    ln_diag(opts, "invalid option: %s", arg);
    return -1;
}

int
ln_parse_options(ln_options_t *opts, int argc, char **argv, int *operand_index, int *show_help)
{
    int i;
    bool seen_force;
    bool seen_interactive;

    if (!opts || !operand_index || !show_help) {
        errno = EINVAL;
        return -1;
    }

    *show_help = 0;
    seen_force = false;
    seen_interactive = false;

    i = 1;
    while (i < argc) {
        char *arg = argv[i];

        if (arg[0] != '-' || arg[1] == '\0') {
            break;
        }

        if (strcmp(arg, "--") == 0) {
            ++i;
            break;
        }

        if (arg[1] == '-') {
            int rc = ln_parse_long_option(opts, arg, argc, argv, &i, show_help,
                                          &seen_force, &seen_interactive);
            if (rc < 0) {
                return -1;
            }
            if (rc > 0) {
                *operand_index = i + 1;
                return 0;
            }
            ++i;
            continue;
        }

        {
            size_t j = 1;
            while (arg[j] != '\0') {
                char opt = arg[j];
                const char *optval = NULL;
                int rc;

                if (opt == 'S' || opt == 't') {
                    if (arg[j + 1] != '\0') {
                        optval = &arg[j + 1];
                        j = strlen(arg);
                    } else {
                        if (i + 1 >= argc) {
                            ln_diag(opts, "option requires an argument -- '%c'", opt);
                            return -1;
                        }
                        ++i;
                        optval = argv[i];
                    }
                } else if (opt == 'b') {
                    if (arg[j + 1] != '\0') {
                        optval = &arg[j + 1];
                        j = strlen(arg);
                    }
                }

                rc = ln_apply_parsed_option(opts, opt, optval, &seen_force, &seen_interactive);
                if (rc != 0) {
                    return -1;
                }

                if (opt == 'S' || opt == 't' || opt == 'b') {
                    break;
                }

                ++j;
            }
        }

        ++i;
    }

    if (opts->target_directory && opts->no_target_directory) {
        ln_diag(opts, "-t/--target-directory and -T/--no-target-directory are mutually exclusive");
        return -1;
    }

    if (opts->relative && !opts->symbolic) {
        ln_diag(opts, "-r/--relative requires -s/--symbolic");
        return -1;
    }

    if (opts->bsd_remove_target_dir && opts->symbolic &&
        !seen_force && !seen_interactive &&
        opts->replace_mode == LN_REPLACE_DEFAULT) {
        opts->replace_mode = LN_REPLACE_FORCE;
    }

    *operand_index = i;
    return 0;
}

int
ln_execute(const ln_options_t *opts, int argc, char **argv, int operand_index)
{
    struct ln_plan plan;
    int had_error;
    int i;

    if (ln_build_plan(opts, argc, argv, operand_index, &plan) != 0) {
        return 1;
    }

    had_error = 0;

    for (i = 0; i < plan.src_count; ++i) {
        const char *src;
        char *dst;

        src = argv[plan.src_start + i];
        if (plan.use_target_dir) {
            const char *base = ln_basename_const(src);
            dst = ln_path_join(plan.target_dir, base);
            if (!dst) {
                ln_diag_errno(opts, plan.target_dir, "build destination path");
                had_error = 1;
                continue;
            }
        } else {
            dst = ln_strdup(plan.single_target);
            if (!dst) {
                ln_diag_errno(opts, plan.single_target, "build destination path");
                had_error = 1;
                continue;
            }
        }

        if (ln_process_one(opts, src, dst) != 0) {
            had_error = 1;
        }

        free(dst);
    }

    return had_error ? 1 : 0;
}
