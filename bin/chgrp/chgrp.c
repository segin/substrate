#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

/* Bound recursion (one dir fd held per level) — see chown. */
#define CHGRP_MAX_DEPTH 512

struct visited_dir {
    dev_t dev;
    ino_t ino;
};

enum walk_policy {
    WALK_PHYSICAL,
    WALK_CMDLINE_SYMLINKS,
    WALK_LOGICAL
};

struct chgrp_options {
    bool recursive;
    bool dir_only;
    bool force;
    bool change_symlink;
    bool use_reference;
    bool verbose;        /* -v: report every file */
    bool changes_only;   /* -c: report only files actually changed */
    enum walk_policy policy;

    const char *reference_path;
    gid_t reference_gid;

    gid_t gid;
    bool gid_set;
};

struct chgrp_context {
    const char *progname;
    struct chgrp_options opts;
    struct visited_dir *visited;
    size_t visited_count;
    size_t visited_cap;
    int rval;
};

static void
usage(const char *progname)
{
    fprintf(stderr,
        "Usage: %s [-R [-H | -L | -P]] [-cfhv] group file ...\n"
        "       %s [-R [-H | -L | -P]] [-fhv] -r file file ...\n",
        progname, progname);
}

static void
set_failed(struct chgrp_context *ctx)
{
    ctx->rval = 1;
}

static void
warn_errno_path(struct chgrp_context *ctx, const char *path, const char *context)
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
warn_message_path(struct chgrp_context *ctx, const char *path, const char *msg)
{
    set_failed(ctx);
    if (!ctx->opts.force) {
        fprintf(stderr, "%s: %s: %s\n", ctx->progname, path, msg);
    }
}

/* Used only for the --reference file (a single up-front stat). */
static int
retry_stat(const char *path, struct stat *st)
{
    int rc;

    do {
        rc = stat(path, st);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

/* fd-relative stat/chown, EINTR-retried, for the pinned-parent descent
 * (CHGRP-01/05). */
static int
retry_fstatat(int dirfd, const char *name, struct stat *st, int flag)
{
    int rc;

    do {
        rc = fstatat(dirfd, name, st, flag);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static int
retry_fchownat(int dirfd, const char *name, uid_t uid, gid_t gid, int flag)
{
    int rc;

    do {
        rc = fchownat(dirfd, name, uid, gid, flag);
    } while (rc < 0 && errno == EINTR);
    return rc;
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
should_follow_symlink_for_walk(const struct chgrp_context *ctx, bool cmdline)
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
    /* Guard the length arithmetic against size_t wrap on 32-bit (CHGRP-08). */
    if (base_len > SIZE_MAX - name_len - 2u) {
        return NULL;
    }
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
visited_add(struct chgrp_context *ctx, dev_t dev, ino_t ino)
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

/* Pop the directory pushed on entry, so the set tracks only current-path
 * ancestors (CHGRP-08/09 ancestor-only cycle set). */
static void
visited_pop(struct chgrp_context *ctx)
{
    if (ctx->visited_count > 0) {
        ctx->visited_count--;
    }
}

/*
 * Resolve a group spec to a gid via an out-parameter (no int/(gid_t)-1
 * sentinel collision, CHGRP-07).  A real group name is preferred over a
 * numeric guess; numeric ids are parsed with strtoul + endptr + range so
 * "12abc" and out-of-range values are rejected rather than truncated
 * (CHGRP-04).
 */
static int
resolve_group_gid(const char *name, gid_t *out)
{
    struct group *gr;
    char         *end;
    unsigned long v;

    if (name == NULL || name[0] == '\0') {
        return -1;
    }

    gr = getgrnam(name);
    if (gr != NULL) {
        *out = gr->gr_gid;
        return 0;
    }

    errno = 0;
    v = strtoul(name, &end, 10);
    if (end != name && *end == '\0' && errno != ERANGE &&
        v <= (unsigned long)(gid_t)-1) {
        *out = (gid_t)v;
        return 0;
    }
    return -1;
}

static void
apply_at(struct chgrp_context *ctx, int dirfd, const char *name,
    const char *display, bool is_symlink, const struct stat *stat_st)
{
    gid_t gid;
    int   flag;
    bool  changed = false;

    gid = ctx->opts.gid;
    if (ctx->opts.use_reference) {
        gid = ctx->opts.reference_gid;
    }
    /* --reference implies a target gid even though it doesn't set gid_set;
     * without this the reference gid was clobbered to -1 and chgrp became a
     * silent no-op (CHGRP-03). */
    if (!ctx->opts.gid_set && !ctx->opts.use_reference) {
        gid = (gid_t)-1;
    }

    if (stat_st != NULL && gid != (gid_t)-1 && stat_st->st_gid != gid) {
        changed = true;
    }

    flag = (ctx->opts.change_symlink && is_symlink) ? AT_SYMLINK_NOFOLLOW : 0;
    if (retry_fchownat(dirfd, name, (uid_t)-1, gid, flag) < 0) {
        warn_errno_path(ctx, display, NULL);
        return;
    }

    if (ctx->opts.verbose || ctx->opts.changes_only) {
        if (changed) {
            printf("changed group of '%s'\n", display);
        } else if (ctx->opts.verbose) {
            printf("group of '%s' retained\n", display);
        }
    }
}

static void process_entry_at(struct chgrp_context *ctx, int dirfd,
    const char *name, const char *display, bool cmdline, int depth);

/* Descend into `owned_fd` (taken ownership of + closed here), operating
 * fd-relative so a swapped-in symlink can't redirect the walk (CHGRP-01/05). */
static void
walk_fd(struct chgrp_context *ctx, int owned_fd, const char *display,
    dev_t dev, ino_t ino, int depth)
{
    DIR           *dir;
    struct dirent *de;
    int            visit_rc;

    if (depth >= CHGRP_MAX_DEPTH) {
        errno = ELOOP;
        warn_errno_path(ctx, display, "recursion too deep");
        (void)close(owned_fd);
        return;
    }

    visit_rc = visited_add(ctx, dev, ino);
    if (visit_rc < 0) {
        errno = ENOMEM;
        warn_errno_path(ctx, display, "cannot track visited directory");
        (void)close(owned_fd);
        return;
    }
    if (visit_rc > 0) {
        (void)close(owned_fd);
        return;
    }

    dir = fdopendir(owned_fd);
    if (dir == NULL) {
        warn_errno_path(ctx, display, "cannot read directory");
        (void)close(owned_fd);
        visited_pop(ctx);
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
                warn_errno_path(ctx, display, "directory traversal failed");
            }
            break;
        }

        if (is_dot_or_dotdot(de->d_name)) {
            continue;
        }

        child = path_join(display, de->d_name);
        if (child == NULL) {
            errno = ENOMEM;
            warn_errno_path(ctx, display, "out of memory while traversing");
            break;
        }

        process_entry_at(ctx, dirfd(dir), de->d_name, child, false, depth);
        free(child);
    }

    if (retry_closedir(dir) < 0) {
        warn_errno_path(ctx, display, "cannot close directory");
    }
    visited_pop(ctx);
}

static void
process_entry_at(struct chgrp_context *ctx, int dirfd, const char *name,
    const char *display, bool cmdline, int depth)
{
    struct stat lst;
    struct stat st_target;
    const struct stat *stat_st;
    const struct stat *walk_stat;
    bool is_symlink;
    bool follow_walk;
    bool follow_change;
    bool have_target = false;
    bool should_change = true;

    if (retry_fstatat(dirfd, name, &lst, AT_SYMLINK_NOFOLLOW) < 0) {
        warn_errno_path(ctx, display, NULL);
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

    if (is_symlink && (follow_walk || follow_change)) {
        if (retry_fstatat(dirfd, name, &st_target, 0) == 0) {
            have_target = true;
        } else if (follow_change) {
            warn_errno_path(ctx, display, NULL);
            return;
        } else if (follow_walk) {
            warn_message_path(ctx, display,
                "cannot follow symbolic link during traversal");
        }
    }

    stat_st = &lst;
    if (is_symlink && follow_change && have_target) {
        stat_st = &st_target;
    }

    if (ctx->opts.dir_only && !S_ISDIR(stat_st->st_mode)) {
        should_change = false;
    }

    if (ctx->opts.recursive && is_symlink && !ctx->opts.change_symlink &&
        !follow_walk) {
        should_change = false;
    }

    if (should_change) {
        apply_at(ctx, dirfd, name, display, is_symlink, stat_st);
    }

    if (!ctx->opts.recursive) {
        return;
    }

    walk_stat = &lst;
    if (is_symlink && follow_walk && have_target) {
        walk_stat = &st_target;
    }

    if (S_ISDIR(walk_stat->st_mode)) {
        int flags = O_RDONLY | O_DIRECTORY;
        int cfd;
        struct stat opened;

        if (!(is_symlink && follow_walk)) {
            flags |= O_NOFOLLOW;
        }

        do {
            cfd = openat(dirfd, name, flags);
        } while (cfd < 0 && errno == EINTR);
        if (cfd < 0) {
            warn_errno_path(ctx, display, "cannot read directory");
            return;
        }
        if (fstat(cfd, &opened) < 0) {
            warn_errno_path(ctx, display, NULL);
            (void)close(cfd);
            return;
        }
        walk_fd(ctx, cfd, display, opened.st_dev, opened.st_ino, depth + 1);
    }
}

static int
parse_group_spec(const char *spec, gid_t *gid, bool *gid_set)
{
    if (spec == NULL || spec[0] == '\0') {
        return -1;
    }
    if (resolve_group_gid(spec, gid) != 0) {
        return -1;
    }
    *gid_set = true;
    return 0;
}

static int
parse_reference_group(struct chgrp_context *ctx)
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

    ctx->opts.reference_gid = st.st_gid;
    return 0;
}

static int
parse_args(struct chgrp_context *ctx, int argc, char *argv[], int *file_index)
{
    int i = 1;
    bool stop_opts = false;
    gid_t gid = -1;
    bool gid_set = false;

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
            } else if (arg[j] == 'v') {
                ctx->opts.verbose = true;      /* report every file */
            } else if (arg[j] == 'c') {
                ctx->opts.changes_only = true; /* report only real changes */
            } else if (arg[j] == 'r') {
                ctx->opts.use_reference = true;
                if (i + 1 >= argc) {
                    usage(ctx->progname);
                    return -1;
                }
                ctx->opts.reference_path = argv[i + 1];
                i++;
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
        return parse_reference_group(ctx);
    }

    if (i >= argc) {
        usage(ctx->progname);
        return -1;
    }

    if (parse_group_spec(argv[i], &gid, &gid_set) < 0) {
        fprintf(stderr, "%s: invalid group specification '%s'\n",
            ctx->progname, argv[i]);
        return -1;
    }

    ctx->opts.gid = gid;
    ctx->opts.gid_set = gid_set;

    *file_index = i + 1;
    return 0;
}

int
main(int argc, char *argv[])
{
    struct chgrp_context ctx;
    int file_index;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.progname = (argc > 0 && argv[0] != NULL) ? argv[0] : "chgrp";
    ctx.opts.policy = WALK_PHYSICAL;

    if (argc < 2) {
        usage(ctx.progname);
        return 1;
    }

    if (parse_args(&ctx, argc, argv, &file_index) < 0) {
        free(ctx.visited);
        return 1;
    }

    if (file_index >= argc) {
        usage(ctx.progname);
        free(ctx.visited);
        return 1;
    }

    for (i = file_index; i < argc; ++i) {
        process_entry_at(&ctx, AT_FDCWD, argv[i], argv[i], true, 0);
    }

    free(ctx.visited);
    return ctx.rval;
}
