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

#define CHOWN_MODE_BITS (0)

/*
 * Bound recursion so a deep (or cyclic) tree can't exhaust the C stack or
 * RLIMIT_NOFILE (one dir fd is held open per level during descent).
 */
#define CHOWN_MAX_DEPTH 512

struct visited_dir {
    dev_t dev;
    ino_t ino;
};

enum walk_policy {
    WALK_PHYSICAL,
    WALK_CMDLINE_SYMLINKS,
    WALK_LOGICAL
};

struct chown_options {
    bool recursive;
    bool dir_only;
    bool force;
    bool change_symlink;
    bool use_reference;
    bool verbose;        /* -v: report every file */
    bool changes_only;   /* -c: report only files actually changed */
    enum walk_policy policy;

    const char *reference_path;
    uid_t reference_uid;
    gid_t reference_gid;

    uid_t uid;
    gid_t gid;
    bool uid_set;
    bool gid_set;
};

struct chown_context {
    const char *progname;
    struct chown_options opts;
    struct visited_dir *visited;
    size_t visited_count;
    size_t visited_cap;
    int rval;
};

static void
usage(const char *progname)
{
    fprintf(stderr,
        "Usage: %s [-R [-H | -L | -P]] [-cfhv] owner[:group] file ...\n"
        "       %s [-R [-H | -L | -P]] [-cfhv] -r file file ...\n",
        progname, progname);
}

static void
set_failed(struct chown_context *ctx)
{
    ctx->rval = 1;
}

static void
warn_errno_path(struct chown_context *ctx, const char *path, const char *context)
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
warn_message_path(struct chown_context *ctx, const char *path, const char *msg)
{
    set_failed(ctx);
    if (!ctx->opts.force) {
        fprintf(stderr, "%s: %s: %s\n", ctx->progname, path, msg);
    }
}

/* Used only for the --reference file, which is a single up-front stat. */
static int
retry_stat(const char *path, struct stat *st)
{
    int rc;

    do {
        rc = stat(path, st);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

/*
 * fd-relative stat/chown, EINTR-retried.  The descent uses these against a
 * pinned parent directory fd + a single path component, so an attacker who
 * swaps an intermediate directory for a symlink between check and act can no
 * longer redirect the operation outside the tree (CHOWN-01/07).
 */
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
should_follow_symlink_for_walk(const struct chown_context *ctx, bool cmdline)
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
    /* Guard the length arithmetic against size_t wrap on 32-bit (CHOWN-11). */
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
visited_add(struct chown_context *ctx, dev_t dev, ino_t ino)
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

/*
 * Pop the directory pushed by the matching visited_add on the way out of a
 * subtree.  The set therefore tracks only the *ancestors* on the current
 * descent path, so an A/B/A hardlink cycle is caught while a directory
 * legitimately reachable from two sibling branches is not falsely skipped
 * (CHGRP-08/09 ancestor-only set).
 */
static void
visited_pop(struct chown_context *ctx)
{
    if (ctx->visited_count > 0) {
        ctx->visited_count--;
    }
}

/* Parse a whole-string non-negative id; 0 + *out on success, -1 otherwise. */
static int
parse_numeric_id(const char *s, unsigned long *out)
{
    char         *end;
    unsigned long v;

    if (s == NULL || s[0] == '\0') {
        return -1;
    }
    errno = 0;
    v = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE) {
        return -1;
    }
    *out = v;
    return 0;
}

/*
 * Resolve a user spec to a uid.  A real account name is preferred over a
 * numeric guess, so `4chan`/`0day` chown to the account, not uid 4/0
 * (CHOWN-03); a leading '+' forces numeric (CHOWN-09); numeric ids are
 * parsed with range checking into uid_t (CHOWN-04).
 */
static int
resolve_owner_uid(const char *name, uid_t *out)
{
    struct passwd *pw;
    unsigned long  v;

    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    if (name[0] == '+') {
        if (parse_numeric_id(name + 1, &v) != 0 || v > (unsigned long)(uid_t)-1) {
            return -1;
        }
        *out = (uid_t)v;
        return 0;
    }
    pw = getpwnam(name);
    if (pw != NULL) {
        *out = pw->pw_uid;
        return 0;
    }
    if (parse_numeric_id(name, &v) == 0 && v <= (unsigned long)(uid_t)-1) {
        *out = (uid_t)v;
        return 0;
    }
    return -1;
}

static int
resolve_group_gid(const char *name, gid_t *out)
{
    struct group *gr;
    unsigned long v;

    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    if (name[0] == '+') {
        if (parse_numeric_id(name + 1, &v) != 0 || v > (unsigned long)(gid_t)-1) {
            return -1;
        }
        *out = (gid_t)v;
        return 0;
    }
    gr = getgrnam(name);
    if (gr != NULL) {
        *out = gr->gr_gid;
        return 0;
    }
    if (parse_numeric_id(name, &v) == 0 && v <= (unsigned long)(gid_t)-1) {
        *out = (gid_t)v;
        return 0;
    }
    return -1;
}

/*
 * Apply the requested ownership to `name` relative to `dirfd` (AT_FDCWD +
 * full path for a top-level operand).  `stat_st` is the pre-change stat used
 * only to decide whether -v/-c should announce a real change.
 */
static void
apply_at(struct chown_context *ctx, int dirfd, const char *name,
    const char *display, bool is_symlink, const struct stat *stat_st)
{
    uid_t uid;
    gid_t gid;
    int   flag;
    bool  changed = false;

    uid = ctx->opts.use_reference ? ctx->opts.reference_uid : ctx->opts.uid;
    gid = ctx->opts.use_reference ? ctx->opts.reference_gid : ctx->opts.gid;
    if (!ctx->opts.uid_set) {
        uid = (uid_t)-1;
    }
    if (!ctx->opts.gid_set) {
        gid = (gid_t)-1;
    }

    if (stat_st != NULL) {
        if (uid != (uid_t)-1 && stat_st->st_uid != uid) {
            changed = true;
        }
        if (gid != (gid_t)-1 && stat_st->st_gid != gid) {
            changed = true;
        }
    }

    /* -h operates on the symlink itself; otherwise the change follows it. */
    flag = (ctx->opts.change_symlink && is_symlink) ? AT_SYMLINK_NOFOLLOW : 0;
    if (retry_fchownat(dirfd, name, uid, gid, flag) < 0) {
        warn_errno_path(ctx, display, NULL);
        return;
    }

    /* -v reports every file; -c reports only files actually changed. */
    if (ctx->opts.verbose || ctx->opts.changes_only) {
        if (changed) {
            printf("changed ownership of '%s'\n", display);
        } else if (ctx->opts.verbose) {
            printf("ownership of '%s' retained\n", display);
        }
    }
}

static void process_entry_at(struct chown_context *ctx, int dirfd,
    const char *name, const char *display, bool cmdline, int depth);

/*
 * Descend into the directory referenced by `dirfd` (which this function
 * takes ownership of and closes).  Every child is stat'd and modified
 * fd-relative to `dirfd`, so a component swapped for a symlink after the
 * parent was opened cannot redirect the walk outside the tree.
 */
static void
walk_fd(struct chown_context *ctx, int owned_fd, const char *display,
    dev_t dev, ino_t ino, int depth)
{
    DIR           *dir;
    struct dirent *de;
    int            visit_rc;

    if (depth >= CHOWN_MAX_DEPTH) {
        errno = ELOOP;
        warn_errno_path(ctx, display, "recursion too deep");
        (void)close(owned_fd);
        return;
    }

    /* Ancestor-only cycle set: pushed here, popped on the way out. */
    visit_rc = visited_add(ctx, dev, ino);
    if (visit_rc < 0) {
        errno = ENOMEM;
        warn_errno_path(ctx, display, "cannot track visited directory");
        (void)close(owned_fd);
        return;
    }
    if (visit_rc > 0) {          /* already an ancestor -> cycle */
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
process_entry_at(struct chown_context *ctx, int dirfd, const char *name,
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

        /* Refuse to follow the entry unless it is a symlink we deliberately
         * traverse (-L, or -H at the command line); the kernel now enforces
         * O_NOFOLLOW, so a swapped-in symlink yields ELOOP (CHOWN-01/07). */
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
        /* walk_fd takes ownership of cfd. */
        walk_fd(ctx, cfd, display, opened.st_dev, opened.st_ino, depth + 1);
    }
}

static int
parse_owner_spec(const char *spec, uid_t *uid, gid_t *gid, bool *uid_set, bool *gid_set)
{
    char        userbuf[256];
    const char *sep;
    const char *grp;
    size_t      ulen;

    *uid_set = false;
    *gid_set = false;

    sep = strchr(spec, ':');
    if (sep == NULL) {
        /*
         * Deprecated `user.group`: split on '.' only when the whole spec is
         * not itself a valid owner, so a real username containing a dot
         * still works (CHOWN-05).
         */
        const char *dot = strchr(spec, '.');
        if (dot != NULL) {
            uid_t tmp;
            if (resolve_owner_uid(spec, &tmp) != 0) {
                sep = dot;
            }
        }
    }

    if (sep == NULL) {
        /* The whole spec is the user (CHOWN-02: no longer includes ":grp"). */
        if (resolve_owner_uid(spec, uid) != 0) {
            return -1;
        }
        *uid_set = true;
        return 0;
    }

    ulen = (size_t)(sep - spec);
    grp  = sep + 1;

    if (ulen > 0) {
        if (ulen >= sizeof(userbuf)) {
            return -1;
        }
        memcpy(userbuf, spec, ulen);
        userbuf[ulen] = '\0';
        if (resolve_owner_uid(userbuf, uid) != 0) {
            return -1;
        }
        *uid_set = true;
    }

    if (grp[0] != '\0') {
        if (resolve_group_gid(grp, gid) != 0) {
            return -1;
        }
        *gid_set = true;
    } else if (ulen > 0) {
        /* `user:` — set the group to the user's login group. */
        struct passwd *pw = getpwnam(userbuf);
        if (pw != NULL) {
            *gid = pw->pw_gid;
            *gid_set = true;
        }
    }

    return 0;
}

static int
parse_reference_owner(struct chown_context *ctx)
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

    ctx->opts.reference_uid = st.st_uid;
    ctx->opts.reference_gid = st.st_gid;
    return 0;
}

static int
parse_args(struct chown_context *ctx, int argc, char *argv[], int *file_index)
{
    int i = 1;
    bool stop_opts = false;
    uid_t uid = -1;
    gid_t gid = -1;
    bool uid_set = false;
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
        return parse_reference_owner(ctx);
    }

    if (i >= argc) {
        usage(ctx->progname);
        return -1;
    }

    if (parse_owner_spec(argv[i], &uid, &gid, &uid_set, &gid_set) < 0) {
        fprintf(stderr, "%s: invalid owner specification '%s'\n",
            ctx->progname, argv[i]);
        return -1;
    }

    ctx->opts.uid = uid;
    ctx->opts.gid = gid;
    ctx->opts.uid_set = uid_set;
    ctx->opts.gid_set = gid_set;

    *file_index = i + 1;
    return 0;
}

int
main(int argc, char *argv[])
{
    struct chown_context ctx;
    int file_index;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.progname = (argc > 0 && argv[0] != NULL) ? argv[0] : "chown";
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
