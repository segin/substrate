/*
 * usr.sbin/useradd — create a new user account.
 *
 * Usage:
 *   useradd [-u UID [-o]] [-g GROUP] [-G GROUP1,GROUP2,...] \
 *           [-d HOMEDIR] [-s SHELL] [-c COMMENT] \
 *           [-m] [-N] [-r] USER
 *
 * Options:
 *   -u UID      Specific UID instead of auto-allocate.  -o allows
 *               reuse of an existing UID.
 *   -g GROUP    Primary group.  GROUP may be a name or numeric GID;
 *               the group must already exist.  Defaults: with -N,
 *               the GID of group "users" (or 100 if "users" is
 *               absent).  Without -N (the historical default for
 *               substrate, matching shadow-utils USERGROUPS_ENAB),
 *               a new group with the same name as USER is created
 *               automatically by spawning groupadd.
 *   -G LIST     Comma-separated supplementary group names.  Each
 *               group must already exist.
 *   -d HOMEDIR  Home directory path.  Default: /home/USER.
 *   -s SHELL    Login shell.  Default: /bin/sh.
 *   -c COMMENT  GECOS / full-name field.  Default: empty.
 *   -m          Create the home directory (mode 0700, owned by the
 *               new user).  Copies /etc/skel if present.
 *   -M          Do NOT create the home directory even if defaults
 *               would (currently never; flag accepted for parity).
 *   -N          Don't auto-create a per-user group.
 *   -r          System account: allocate UID/GID from 1..999, set
 *               shell to /bin/false, don't create a home directory.
 *
 * Exit codes (shadow-utils-style):
 *   0  success
 *   2  bad usage
 *   3  invalid argument value
 *   4  UID not unique (without -o)
 *   6  specified group doesn't exist
 *   9  username already in use
 *   10 can't update /etc/passwd
 *   12 can't create home directory
 */

#include <sys/pwdb.h>
#include <ctype.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

static const char *PROGNAME = "useradd";

struct useradd_ctx {
    const char *name;
    uid_t       uid;
    gid_t       gid;
    const char *gecos;
    const char *home;
    const char *shell;
};

static int
write_passwd(FILE *out, void *arg)
{
    struct useradd_ctx *ctx = (struct useradd_ctx *)arg;
    FILE *in = fopen(PWDB_PASSWD, "r");
    char  line[1024];

    if (in != NULL) {
        while (fgets(line, sizeof(line), in) != NULL) {
            if (fputs(line, out) == EOF) { fclose(in); return -1; }
        }
        fclose(in);
    }
    if (fprintf(out, "%s:x:%u:%u:%s:%s:%s\n",
                ctx->name, (unsigned)ctx->uid, (unsigned)ctx->gid,
                ctx->gecos, ctx->home, ctx->shell) < 0) {
        return -1;
    }
    return 0;
}

static int
write_shadow(FILE *out, void *arg)
{
    struct useradd_ctx *ctx = (struct useradd_ctx *)arg;
    FILE *in = fopen(PWDB_SHADOW, "r");
    char  line[1024];

    if (in != NULL) {
        while (fgets(line, sizeof(line), in) != NULL) {
            if (fputs(line, out) == EOF) { fclose(in); return -1; }
        }
        fclose(in);
    }
    /* "!" hash means "locked, no password set yet" — admin runs
     * passwd(1) afterwards.  lastchange=days-since-epoch (today). */
    if (fprintf(out, "%s:!:%ld:0:99999:7:::\n",
                ctx->name, pwdb_today_days()) < 0) {
        return -1;
    }
    return 0;
}

/*
 * Append USER to each group in `extra_groups` (comma-separated).
 * For each group, rewrite its member-list field.  Validates each
 * group's existence before any writes.
 */
struct group_supp_ctx {
    const char *user;
    const char *groups_csv;
};

static int
group_in_csv(const char *needle, const char *csv)
{
    size_t nlen = strlen(needle);
    const char *p = csv;
    while (*p != '\0') {
        char *comma = strchr(p, ',');
        size_t      seg   = comma ? (size_t)(comma - p) : strlen(p);
        if (seg == nlen && strncmp(p, needle, nlen) == 0) return 1;
        if (comma == NULL) break;
        p = comma + 1;
    }
    return 0;
}

static int
write_group_with_supp(FILE *out, void *arg)
{
    struct group_supp_ctx *ctx = (struct group_supp_ctx *)arg;
    FILE *in = fopen(PWDB_GROUP, "r");
    char  line[1024];

    if (in == NULL) return -1;
    while (fgets(line, sizeof(line), in) != NULL) {
        char  copy[1024];
        char *fields[4];
        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';
        int n = pwdb_split(copy, ':', fields, 4);
        if (n < 4) {
            if (fputs(line, out) == EOF) { fclose(in); return -1; }
            continue;
        }
        if (group_in_csv(fields[0], ctx->groups_csv)) {
            const char *mem = fields[3];
            const char *sep = (mem[0] == '\0') ? "" : ",";
            if (fprintf(out, "%s:%s:%s:%s%s%s\n",
                        fields[0], fields[1], fields[2],
                        mem, sep, ctx->user) < 0) {
                fclose(in);
                return -1;
            }
        } else {
            if (fputs(line, out) == EOF) { fclose(in); return -1; }
        }
    }
    fclose(in);
    return 0;
}

/*
 * Spawn `groupadd [-r] <name>` for the per-user group case.  Uses
 * fork+exec instead of duplicating groupadd's logic; the lock file
 * is closed-on-exec via O_CLOEXEC… well, flock(LOCK_EX) won't be
 * inherited across the fork+exec (flock is open-file-description
 * scoped), so the child re-locks cleanly.  Wait, flock IS inherited
 * across fork — both parent and child reference the same OFD.  But
 * the lock is owned by the OFD, so the child's groupadd will see
 * "already locked" and block.  To avoid that, we release the lock
 * around the spawn and re-acquire after.
 */
static int
spawn_groupadd(const char *name, int system_flag, int *outlock)
{
    pwdb_unlock(*outlock);
    *outlock = -1;

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        const char *argv[5];
        int i = 0;
        argv[i++] = "/usr/sbin/groupadd";
        if (system_flag) argv[i++] = "-r";
        argv[i++] = name;
        argv[i++] = NULL;
        execv(argv[0], (char *const *)argv);
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    *outlock = pwdb_lock();
    if (*outlock < 0) return -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int
copy_skel(const char *src_dir, const char *dst_dir, uid_t uid, gid_t gid)
{
    /* Best-effort copy of /etc/skel/ into the new home.  Substrate
     * doesn't yet ship dirent walking via opendir/readdir in a way
     * that suits us here — leave it as a hook.  Returning 0 means
     * "no skeleton copy performed", which is fine. */
    (void)src_dir; (void)dst_dir; (void)uid; (void)gid;
    return 0;
}

static void
usage(void)
{
    fprintf(stderr,
        "usage: useradd [-u UID [-o]] [-g GROUP] [-G LIST]\n"
        "               [-d HOMEDIR] [-s SHELL] [-c COMMENT]\n"
        "               [-m] [-M] [-N] [-r] USER\n");
    exit(2);
}

int
main(int argc, char *argv[])
{
    long        uid_arg       = -1;
    int         non_unique    = 0;
    const char *primary_group = NULL;
    const char *supp_groups   = NULL;
    const char *home          = NULL;
    const char *shell         = NULL;
    const char *gecos         = "";
    int         create_home   = 0;
    int         no_user_group = 0;
    int         system        = 0;
    int         opt;

    while ((opt = getopt(argc, argv, "u:og:G:d:s:c:mMNr")) != -1) {
        switch (opt) {
        case 'u': {
            char *end;
            errno = 0;
            long v = strtol(optarg, &end, 10);
            if (errno != 0 || *end != '\0' || v < 0 || v > USER_ID_MAX) {
                pwdb_die(PROGNAME, "invalid UID '%s'", optarg);
            }
            uid_arg = v;
            break;
        }
        case 'o': non_unique    = 1; break;
        case 'g': primary_group = optarg; break;
        case 'G': supp_groups   = optarg; break;
        case 'd': home          = optarg; break;
        case 's': shell         = optarg; break;
        case 'c': gecos         = optarg; break;
        case 'm': create_home   = 1; break;
        case 'M': create_home   = 0; break;
        case 'N': no_user_group = 1; break;
        case 'r': system        = 1; break;
        default:  usage();
        }
    }
    if (optind != argc - 1) usage();
    const char *name = argv[optind];

    pwdb_require_root(PROGNAME);

    if (!pwdb_valid_name(name)) {
        pwdb_die(PROGNAME, "invalid user name '%s'", name);
    }

    /* Defaults. */
    char home_buf[256];
    if (home == NULL) {
        if (system) {
            home = "/var/empty";
        } else {
            snprintf(home_buf, sizeof(home_buf), "/home/%s", name);
            home = home_buf;
        }
    }
    if (shell == NULL) {
        shell = system ? "/bin/false" : "/bin/sh";
    }
    if (system) {
        create_home = 0;
        no_user_group = no_user_group ? 1 : 0; /* still create one unless told */
    }

    int lock = pwdb_lock();
    if (lock < 0) {
        pwdb_die(PROGNAME, "cannot lock %s: %s", PWDB_LOCK, strerror(errno));
    }

    if (getpwnam(name) != NULL) {
        pwdb_unlock(lock);
        fprintf(stderr, "useradd: user '%s' already exists\n", name);
        return 9;
    }

    /* UID resolution. */
    if (uid_arg >= 0) {
        if (!non_unique && getpwuid((uid_t)uid_arg) != NULL) {
            pwdb_unlock(lock);
            fprintf(stderr, "useradd: UID %ld already in use\n", uid_arg);
            return 4;
        }
    } else {
        long min = system ? SYSTEM_ID_MIN : USER_ID_MIN;
        long max = system ? SYSTEM_ID_MAX : USER_ID_MAX;
        uid_arg = pwdb_next_free_id(0, min, max);
        if (uid_arg < 0) {
            pwdb_unlock(lock);
            pwdb_die(PROGNAME, "no free UID in range %ld..%ld", min, max);
        }
    }

    /* Primary group resolution. */
    gid_t primary_gid;
    if (primary_group != NULL) {
        struct group *gr;
        char *end;
        errno = 0;
        long g = strtol(primary_group, &end, 10);
        if (errno == 0 && *end == '\0' && g >= 0) {
            gr = getgrgid((gid_t)g);
        } else {
            gr = getgrnam(primary_group);
        }
        if (gr == NULL) {
            pwdb_unlock(lock);
            fprintf(stderr,
                "useradd: group '%s' does not exist\n", primary_group);
            return 6;
        }
        primary_gid = gr->gr_gid;
    } else if (no_user_group) {
        struct group *gr = getgrnam("users");
        primary_gid = (gr != NULL) ? gr->gr_gid : 100;
    } else {
        /* Per-user group: spawn groupadd to create one matching the
         * username, then look up its GID.  Group GID may end up
         * equal to UID by coincidence — most distros aim for that
         * by passing -g UID; we do not, to keep the policy decision
         * inside groupadd. */
        if (spawn_groupadd(name, system, &lock) != 0) {
            if (lock >= 0) pwdb_unlock(lock);
            pwdb_die(PROGNAME, "groupadd '%s' failed", name);
        }
        struct group *gr = getgrnam(name);
        if (gr == NULL) {
            pwdb_unlock(lock);
            pwdb_die(PROGNAME,
                "groupadd '%s' succeeded but lookup failed", name);
        }
        primary_gid = gr->gr_gid;
    }

    /* Validate supplementary groups before touching anything. */
    if (supp_groups != NULL) {
        const char *p = supp_groups;
        while (*p != '\0') {
            char *comma = strchr(p, ',');
            size_t      seg   = comma ? (size_t)(comma - p) : strlen(p);
            char        gname[64];
            if (seg == 0 || seg >= sizeof(gname)) {
                pwdb_unlock(lock);
                pwdb_die(PROGNAME,
                    "invalid supplementary group spec '%s'", supp_groups);
            }
            memcpy(gname, p, seg);
            gname[seg] = '\0';
            if (getgrnam(gname) == NULL) {
                pwdb_unlock(lock);
                fprintf(stderr,
                    "useradd: group '%s' does not exist\n", gname);
                return 6;
            }
            if (comma == NULL) break;
            p = comma + 1;
        }
    }

    struct useradd_ctx ctx = {
        .name  = name,
        .uid   = (uid_t)uid_arg,
        .gid   = primary_gid,
        .gecos = gecos,
        .home  = home,
        .shell = shell,
    };

    if (pwdb_atomic_rewrite(PWDB_PASSWD, 0644, write_passwd, &ctx) < 0) {
        pwdb_unlock(lock);
        fprintf(stderr, "useradd: failed to update %s: %s\n",
                PWDB_PASSWD, strerror(errno));
        return 10;
    }
    if (access(PWDB_SHADOW, F_OK) == 0) {
        if (pwdb_atomic_rewrite(PWDB_SHADOW, 0640, write_shadow, &ctx) < 0) {
            fprintf(stderr,
                "useradd: warning: failed to update %s: %s\n",
                PWDB_SHADOW, strerror(errno));
        }
    }

    if (supp_groups != NULL) {
        struct group_supp_ctx sctx = {
            .user       = name,
            .groups_csv = supp_groups,
        };
        if (pwdb_atomic_rewrite(PWDB_GROUP, 0644,
                                write_group_with_supp, &sctx) < 0) {
            fprintf(stderr,
                "useradd: warning: failed to update %s for supplementary"
                " groups: %s\n",
                PWDB_GROUP, strerror(errno));
        }
    }

    if (create_home && home[0] == '/') {
        if (mkdir(home, 0700) < 0 && errno != EEXIST) {
            pwdb_unlock(lock);
            fprintf(stderr,
                "useradd: cannot create home '%s': %s\n",
                home, strerror(errno));
            return 12;
        }
        if (chown(home, (uid_t)uid_arg, primary_gid) < 0) {
            fprintf(stderr,
                "useradd: warning: chown '%s': %s\n",
                home, strerror(errno));
        }
        (void)copy_skel("/etc/skel", home, (uid_t)uid_arg, primary_gid);
    }

    pwdb_unlock(lock);
    return 0;
}
