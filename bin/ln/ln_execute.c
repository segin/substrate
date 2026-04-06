#include "ln.h"

#include <errno.h>
#include <limits.h>
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
ln_handle_existing_dest(const ln_options_t *opts, const char *dest, bool replace_dir_with_f)
{
    bool need_replace;
    int rc;

    need_replace = false;

    if (opts->replace_mode == LN_REPLACE_INTERACTIVE) {
        if (!ln_prompt_replace(opts, dest)) {
            ln_diag(opts, "not replacing %s", dest);
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
        return -1;
    }

    if (opts->backup_mode != LN_BACKUP_NONE) {
        if (ln_backup_destination(opts, dest) != 0) {
            ln_diag_errno(opts, dest, "create backup");
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
            return -1;
        }
    }

    return 0;
}

static int
ln_process_one(const ln_options_t *opts, const char *source, const char *dest)
{
    struct stat dst_st;
    bool dst_exists;
    bool replace_dir_with_f;
    char *symlink_target;
    char *hard_source;
    const char *same_entry_source;
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
        if (ln_handle_existing_dest(opts, dest, replace_dir_with_f) != 0) {
            free(symlink_target);
            free(hard_source);
            return -1;
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
ln_build_plan_two_operands(const ln_options_t *opts, int operand_index, const char *dst,
                           struct ln_plan *plan)
{
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
        return ln_build_plan_two_operands(opts, operand_index, argv[operand_index + 1], plan);
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
