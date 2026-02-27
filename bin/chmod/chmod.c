#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "mode_parser.h"

#ifdef NATIVE_BUILD
#include <fcntl.h>
#endif

#ifndef S_ISUID
#define S_ISUID 0004000
#endif
#ifndef S_ISGID
#define S_ISGID 0002000
#endif
#ifndef S_ISVTX
#define S_ISVTX 0001000
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP ENOSYS
#endif
#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif

#define CHMOD_MODE_BITS (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)

enum walk_policy {
    WALK_PHYSICAL,
    WALK_CMDLINE_SYMLINKS,
    WALK_LOGICAL
};

struct chmod_options {
    bool recursive;
    bool dir_only;
    bool force;
    bool change_symlink;
    bool use_reference;
    enum walk_policy policy;

    const char *reference_path;
    mode_t reference_mode;

    struct chmod_mode *parsed_mode;
};

struct visited_dir {
    dev_t dev;
    ino_t ino;
};

struct chmod_context {
    const char *progname;
    struct chmod_options opts;
    struct visited_dir *visited;
    size_t visited_count;
    size_t visited_cap;
    int rval;
};

static void
usage(const char *progname)
{
    fprintf(stderr,
        "Usage: %s [-R [-H | -L | -P]] [-dfh] mode file ...\n"
        "       %s [-R [-H | -L | -P]] [-dfh] --reference=rfile file ...\n",
        progname, progname);
}

static void
set_failed(struct chmod_context *ctx)
{
    ctx->rval = 1;
}

static void
warn_errno_path(struct chmod_context *ctx, const char *path, const char *context)
{
    const int saved_errno = errno;

    set_failed(ctx);
    if (ctx->opts.force) {
        return;
    }

    if (context != NULL) {
        fprintf(stderr, "%s: %s: %s: %s\n", ctx->progname, path, context,
            strerror(saved_errno));
    } else {
        fprintf(stderr, "%s: %s: %s\n", ctx->progname, path,
            strerror(saved_errno));
    }
}

static void
warn_message_path(struct chmod_context *ctx, const char *path, const char *msg)
{
    set_failed(ctx);
    if (!ctx->opts.force) {
        fprintf(stderr, "%s: %s: %s\n", ctx->progname, path, msg);
    }
}

static int
retry_lstat(const char *path, struct stat *st)
{
    int rc;

    do {
        rc = lstat(path, st);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static int
retry_stat(const char *path, struct stat *st)
{
    int rc;

    do {
        rc = stat(path, st);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static int
retry_chmod(const char *path, mode_t mode)
{
    int rc;

    do {
        rc = chmod(path, mode);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static DIR *
retry_opendir(const char *path)
{
    DIR *dir;

    do {
        dir = opendir(path);
    } while (dir == NULL && errno == EINTR);
    return dir;
}

static int
retry_closedir(DIR *dir)
{
    int rc;

    do {
        rc = closedir(dir);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static bool
should_follow_symlink_for_walk(const struct chmod_context *ctx, bool cmdline)
{
    if (!ctx->opts.recursive) {
        return false;
    }

    if (ctx->opts.policy == WALK_LOGICAL) {
        return true;
    }
    if (ctx->opts.policy == WALK_CMDLINE_SYMLINKS && cmdline) {
        return true;
    }
    return false;
}

static char *
path_join(const char *base, const char *name)
{
    const size_t base_len = strlen(base);
    const size_t name_len = strlen(name);
    const bool need_slash = base_len > 0 && base[base_len - 1] != '/';
    const size_t total = base_len + (need_slash ? 1u : 0u) + name_len + 1u;
    char *joined = (char *)malloc(total);

    if (joined == NULL) {
        return NULL;
    }

    memcpy(joined, base, base_len);
    if (need_slash) {
        joined[base_len] = '/';
        memcpy(joined + base_len + 1, name, name_len);
        joined[base_len + 1 + name_len] = '\0';
    } else {
        memcpy(joined + base_len, name, name_len);
        joined[base_len + name_len] = '\0';
    }

    return joined;
}

static bool
is_dot_or_dotdot(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

static int
visited_add(struct chmod_context *ctx, dev_t dev, ino_t ino)
{
    size_t i;

    for (i = 0; i < ctx->visited_count; ++i) {
        if (ctx->visited[i].dev == dev && ctx->visited[i].ino == ino) {
            return 1;
        }
    }

    if (ctx->visited_count == ctx->visited_cap) {
        size_t new_cap = (ctx->visited_cap == 0) ? 32u : ctx->visited_cap * 2u;
        struct visited_dir *grown = (struct visited_dir *)realloc(ctx->visited,
            new_cap * sizeof(*grown));

        if (grown == NULL) {
            return -1;
        }

        ctx->visited = grown;
        ctx->visited_cap = new_cap;
    }

    ctx->visited[ctx->visited_count].dev = dev;
    ctx->visited[ctx->visited_count].ino = ino;
    ctx->visited_count++;

    return 0;
}

static int
apply_symlink_mode(mode_t mode, const char *path)
{
#if defined(NATIVE_BUILD) && \
    (defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__APPLE__))
    int rc;

    do {
        rc = lchmod(path, mode);
    } while (rc < 0 && errno == EINTR);
    return rc;
#elif defined(NATIVE_BUILD) && defined(AT_FDCWD) && defined(AT_SYMLINK_NOFOLLOW)
    int rc;

    do {
        rc = fchmodat(AT_FDCWD, path, mode, AT_SYMLINK_NOFOLLOW);
    } while (rc < 0 && errno == EINTR);
    return rc;
#else
    (void)mode;
    (void)path;
    errno = ENOSYS;
    return -1;
#endif
}

static bool
mode_is_unsupported_symlink_change_error(void)
{
    return errno == ENOSYS || errno == EOPNOTSUPP || errno == ENOTSUP ||
        errno == EINVAL;
}

static void
apply_mode_to_entry(struct chmod_context *ctx, const char *path,
    const struct stat *mode_stat, bool is_symlink)
{
    mode_t current_mode;
    mode_t nval;
    int rc;

    current_mode = mode_stat->st_mode & CHMOD_MODE_BITS;

    if (ctx->opts.use_reference) {
        nval = ctx->opts.reference_mode;
    } else {
        nval = chmod_getmode(ctx->opts.parsed_mode, mode_stat->st_mode) &
            CHMOD_MODE_BITS;
    }

    if (ctx->opts.dir_only && nval == current_mode) {
        return;
    }

    if (ctx->opts.change_symlink && is_symlink) {
        rc = apply_symlink_mode(nval, path);
        if (rc < 0) {
            if (mode_is_unsupported_symlink_change_error()) {
                warn_message_path(ctx, path,
                    "-h requested but symlink mode changes are unsupported on this platform");
            } else {
                warn_errno_path(ctx, path, NULL);
            }
        }
        return;
    }

    rc = retry_chmod(path, nval);
    if (rc < 0) {
        warn_errno_path(ctx, path, NULL);
    }
}

static void process_path(struct chmod_context *ctx, const char *path,
    bool cmdline);

static void
walk_directory(struct chmod_context *ctx, const char *path, dev_t dev, ino_t ino)
{
    DIR *dir;
    struct dirent *de;
    int visit_rc;

    visit_rc = visited_add(ctx, dev, ino);
    if (visit_rc < 0) {
        errno = ENOMEM;
        warn_errno_path(ctx, path, "cannot track visited directory");
        return;
    }
    if (visit_rc > 0) {
        return;
    }

    dir = retry_opendir(path);
    if (dir == NULL) {
        warn_errno_path(ctx, path, "cannot read directory");
        return;
    }

    for (;;) {
        char *child;

        errno = 0;
        de = readdir(dir);
        if (de == NULL) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != 0) {
                warn_errno_path(ctx, path, "directory traversal failed");
            }
            break;
        }

        if (is_dot_or_dotdot(de->d_name)) {
            continue;
        }

        child = path_join(path, de->d_name);
        if (child == NULL) {
            errno = ENOMEM;
            warn_errno_path(ctx, path, "out of memory while traversing");
            break;
        }

        process_path(ctx, child, false);
        free(child);
    }

    if (retry_closedir(dir) < 0) {
        warn_errno_path(ctx, path, "cannot close directory");
    }
}

static void
process_path(struct chmod_context *ctx, const char *path, bool cmdline)
{
    struct stat lst;
    struct stat st_target;
    const struct stat *mode_stat;
    const struct stat *walk_stat;
    bool is_symlink;
    bool follow_walk;
    bool follow_change;
    bool need_stat;
    bool have_target = false;
    bool should_change = true;
    bool should_recurse = false;

    if (retry_lstat(path, &lst) < 0) {
        warn_errno_path(ctx, path, NULL);
        return;
    }

    is_symlink = S_ISLNK(lst.st_mode);
    follow_walk = is_symlink && should_follow_symlink_for_walk(ctx, cmdline);

    if (is_symlink) {
        if (ctx->opts.change_symlink) {
            follow_change = false;
        } else if (!ctx->opts.recursive) {
            follow_change = true;
        } else {
            follow_change = follow_walk;
        }
    } else {
        follow_change = false;
    }

    need_stat = (is_symlink && (follow_walk || follow_change));
    if (need_stat) {
        if (retry_stat(path, &st_target) == 0) {
            have_target = true;
        } else if (follow_change) {
            warn_errno_path(ctx, path, NULL);
            return;
        } else if (follow_walk) {
            warn_errno_path(ctx, path, "cannot follow symbolic link during traversal");
        }
    }

    mode_stat = &lst;
    if (is_symlink && follow_change && have_target) {
        mode_stat = &st_target;
    }

    if (ctx->opts.dir_only && !S_ISDIR(mode_stat->st_mode)) {
        should_change = false;
    }

    if (ctx->opts.recursive && is_symlink && !ctx->opts.change_symlink && !follow_walk) {
        /*
         * Physical recursion should not mutate symlink targets that are
         * outside traversal scope.
         */
        should_change = false;
    }

    if (should_change) {
        apply_mode_to_entry(ctx, path, mode_stat, is_symlink);
    }

    if (!ctx->opts.recursive) {
        return;
    }

    walk_stat = &lst;
    if (is_symlink && follow_walk && have_target) {
        walk_stat = &st_target;
    }

    should_recurse = S_ISDIR(walk_stat->st_mode);
    if (should_recurse) {
        walk_directory(ctx, path, walk_stat->st_dev, walk_stat->st_ino);
    }
}

static bool
is_mode_fragment(const char *s)
{
    size_t i;

    if (s == NULL || s[0] == '\0') {
        return false;
    }

    for (i = 0; s[i] != '\0'; ++i) {
        const char c = s[i];

        if ((c >= '0' && c <= '7') || c == 'u' || c == 'g' || c == 'o' ||
            c == 'a' || c == 'r' || c == 'w' || c == 'x' || c == 'X' ||
            c == 's' || c == 't' || c == '+' || c == '-' || c == '=' ||
            c == ',') {
            continue;
        }
        return false;
    }
    return true;
}

static int
append_token(char **buf, size_t *len, const char *token)
{
    const size_t tok_len = strlen(token);
    char *grown = (char *)realloc(*buf, *len + tok_len + 1);

    if (grown == NULL) {
        return -1;
    }

    memcpy(grown + *len, token, tok_len);
    grown[*len + tok_len] = '\0';

    *buf = grown;
    *len += tok_len;
    return 0;
}

static int
consume_mode_spec(struct chmod_context *ctx, int argc, char *argv[], int start,
    int *next_index)
{
    struct chmod_mode *parsed;
    char errbuf[128];
    char *joined = NULL;
    size_t joined_len = 0;
    int i;

    if (start >= argc) {
        usage(ctx->progname);
        return -1;
    }

    parsed = chmod_setmode(argv[start], errbuf, sizeof(errbuf));
    if (parsed != NULL) {
        ctx->opts.parsed_mode = parsed;
        *next_index = start + 1;
        return 0;
    }

    if (!is_mode_fragment(argv[start])) {
        fprintf(stderr, "%s: invalid mode '%s': %s\n", ctx->progname,
            argv[start], errbuf);
        return -1;
    }

    if (append_token(&joined, &joined_len, argv[start]) < 0) {
        errno = ENOMEM;
        warn_errno_path(ctx, argv[start], "out of memory while parsing mode");
        return -1;
    }

    for (i = start + 1; i < argc; ++i) {
        if (!is_mode_fragment(argv[i])) {
            break;
        }

        if (append_token(&joined, &joined_len, argv[i]) < 0) {
            free(joined);
            errno = ENOMEM;
            warn_errno_path(ctx, argv[i], "out of memory while parsing mode");
            return -1;
        }

        parsed = chmod_setmode(joined, errbuf, sizeof(errbuf));
        if (parsed != NULL) {
            ctx->opts.parsed_mode = parsed;
            *next_index = i + 1;
            free(joined);
            return 0;
        }
    }

    fprintf(stderr, "%s: invalid mode '%s': %s\n", ctx->progname, joined,
        errbuf);
    free(joined);
    return -1;
}

static int
parse_reference_mode(struct chmod_context *ctx)
{
    struct stat st;

    if (!ctx->opts.use_reference) {
        return 0;
    }

    if (ctx->opts.reference_path == NULL || ctx->opts.reference_path[0] == '\0') {
        usage(ctx->progname);
        return -1;
    }

    if (retry_stat(ctx->opts.reference_path, &st) < 0) {
        warn_errno_path(ctx, ctx->opts.reference_path,
            "cannot stat --reference file");
        return -1;
    }

    ctx->opts.reference_mode = st.st_mode & CHMOD_MODE_BITS;
    return 0;
}

static int
parse_args(struct chmod_context *ctx, int argc, char *argv[], int *file_index)
{
    int i = 1;
    bool stop_opts = false;

    while (i < argc) {
        const char *arg = argv[i];
        size_t j;

        if (stop_opts || arg[0] != '-' || arg[1] == '\0') {
            break;
        }

        if (strcmp(arg, "--") == 0) {
            stop_opts = true;
            ++i;
            continue;
        }

        if (strncmp(arg, "--reference=", 12) == 0) {
            ctx->opts.use_reference = true;
            ctx->opts.reference_path = arg + 12;
            ++i;
            continue;
        }

        if (strcmp(arg, "--reference") == 0) {
            if (i + 1 >= argc) {
                usage(ctx->progname);
                return -1;
            }
            ctx->opts.use_reference = true;
            ctx->opts.reference_path = argv[i + 1];
            i += 2;
            continue;
        }

        if (strncmp(arg, "--", 2) == 0) {
            fprintf(stderr, "%s: unknown option '%s'\n", ctx->progname, arg);
            usage(ctx->progname);
            return -1;
        }

        for (j = 1; arg[j] != '\0'; ++j) {
            if (arg[j] == 'R') {
                ctx->opts.recursive = true;
            } else if (arg[j] == 'H') {
                ctx->opts.policy = WALK_CMDLINE_SYMLINKS;
            } else if (arg[j] == 'L') {
                ctx->opts.policy = WALK_LOGICAL;
            } else if (arg[j] == 'P') {
                ctx->opts.policy = WALK_PHYSICAL;
            } else if (arg[j] == 'd') {
                ctx->opts.dir_only = true;
            } else if (arg[j] == 'f') {
                ctx->opts.force = true;
            } else if (arg[j] == 'h') {
                ctx->opts.change_symlink = true;
            } else {
                break;
            }
        }

        if (arg[j] != '\0') {
            if (ctx->opts.use_reference) {
                fprintf(stderr, "%s: unknown option '%s'\n", ctx->progname,
                    arg);
                usage(ctx->progname);
                return -1;
            }
            break;
        }

        ++i;
    }

    if (ctx->opts.use_reference) {
        *file_index = i;
        return parse_reference_mode(ctx);
    }

    if (consume_mode_spec(ctx, argc, argv, i, &i) < 0) {
        return -1;
    }

    *file_index = i;
    return 0;
}

int
main(int argc, char *argv[])
{
    struct chmod_context ctx;
    int file_index;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.progname = (argc > 0 && argv[0] != NULL) ? argv[0] : "chmod";
    ctx.opts.policy = WALK_PHYSICAL;

    if (argc < 2) {
        usage(ctx.progname);
        return 1;
    }

    if (parse_args(&ctx, argc, argv, &file_index) < 0) {
        chmod_freemode(ctx.opts.parsed_mode);
        free(ctx.visited);
        return 1;
    }

    if (file_index >= argc) {
        usage(ctx.progname);
        chmod_freemode(ctx.opts.parsed_mode);
        free(ctx.visited);
        return 1;
    }

    for (i = file_index; i < argc; ++i) {
        process_path(&ctx, argv[i], true);
    }

    chmod_freemode(ctx.opts.parsed_mode);
    free(ctx.visited);
    return ctx.rval;
}
