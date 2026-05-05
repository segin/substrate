#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef NATIVE_BUILD
#include <fcntl.h>
#endif

#define CHOWN_MODE_BITS (0)

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
        "Usage: %s [-R [-H | -L | -P]] [-fhv] owner[:group] file ...\n"
        "       %s [-R [-H | -L | -P]] [-fhv] -r file file ...\n",
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
retry_chown(const char *path, uid_t uid, gid_t gid)
{
    int rc;

    do {
        rc = chown(path, uid, gid);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static int
retry_lchown(const char *path, uid_t uid, gid_t gid)
{
    int rc;

    do {
        rc = lchown(path, uid, gid);
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

static int
resolve_owner_uid(const char *name)
{
    struct passwd *pw;

    if (name == NULL || name[0] == '\0') {
        return -1;
    }

    if (isdigit((unsigned char)name[0])) {
        long val = strtol(name, NULL, 10);
        if (val < 0) {
            return -1;
        }
        return (int)val;
    }

    pw = getpwnam(name);
    if (pw != NULL) {
        return pw->pw_uid;
    }

    return -1;
}

static int
resolve_group_gid(const char *name)
{
    struct group *gr;

    if (name == NULL || name[0] == '\0') {
        return -1;
    }

    if (isdigit((unsigned char)name[0])) {
        long val = strtol(name, NULL, 10);
        if (val < 0) {
            return -1;
        }
        return (int)val;
    }

    gr = getgrnam(name);
    if (gr != NULL) {
        return gr->gr_gid;
    }

    return -1;
}

static void
apply_owner_to_entry(struct chown_context *ctx, const char *path,
    bool is_symlink)
{
    uid_t uid;
    gid_t gid;
    int rc;

    uid = ctx->opts.uid;
    gid = ctx->opts.gid;

    if (ctx->opts.use_reference) {
        uid = ctx->opts.reference_uid;
        gid = ctx->opts.reference_gid;
    }

    if (!ctx->opts.uid_set) {
        uid = -1;
    }
    if (!ctx->opts.gid_set) {
        gid = -1;
    }

    if (ctx->opts.change_symlink && is_symlink) {
        rc = retry_lchown(path, uid, gid);
    } else {
        rc = retry_chown(path, uid, gid);
    }

    if (rc < 0) {
        warn_errno_path(ctx, path, NULL);
    }
}

static void process_path(struct chown_context *ctx, const char *path,
    bool cmdline);

static void
walk_directory(struct chown_context *ctx, const char *path, dev_t dev, ino_t ino)
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
process_path(struct chown_context *ctx, const char *path, bool cmdline)
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

    if (is_symlink && (follow_walk || follow_change)) {
        if (retry_stat(path, &st_target) == 0) {
            have_target = true;
        } else if (follow_change) {
            warn_errno_path(ctx, path, NULL);
            return;
        } else if (follow_walk) {
            warn_message_path(ctx, path, "cannot follow symbolic link during traversal");
        }
    }

    stat_st = &lst;
    if (is_symlink && follow_change && have_target) {
        stat_st = &st_target;
    }

    if (ctx->opts.dir_only && !S_ISDIR(stat_st->st_mode)) {
        should_change = false;
    }

    if (ctx->opts.recursive && is_symlink && !ctx->opts.change_symlink && !follow_walk) {
        should_change = false;
    }

    if (should_change) {
        apply_owner_to_entry(ctx, path, is_symlink);
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

static int
parse_owner_spec(const char *spec, uid_t *uid, gid_t *gid, bool *uid_set, bool *gid_set)
{
    const char *colon;
    int val;

    colon = strchr(spec, ':');

    if (colon != NULL) {
        if (*(colon + 1) == '\0') {
            val = resolve_owner_uid(spec);
            if (val < 0) {
                return -1;
            }
            *uid = (uid_t)val;
            *uid_set = true;
            *gid_set = false;
        } else {
            val = resolve_owner_uid(spec);
            if (val < 0) {
                return -1;
            }
            *uid = (uid_t)val;
            *uid_set = true;

            val = resolve_group_gid(colon + 1);
            if (val < 0) {
                return -1;
            }
            *gid = (gid_t)val;
            *gid_set = true;
        }
    } else {
        val = resolve_owner_uid(spec);
        if (val < 0) {
            return -1;
        }
        *uid = (uid_t)val;
        *uid_set = true;
        *gid_set = false;
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
                /* verbose - no-op, ignore */
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
        process_path(&ctx, argv[i], true);
    }

    free(ctx.visited);
    return ctx.rval;
}
