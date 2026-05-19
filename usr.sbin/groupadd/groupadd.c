/*
 * usr.sbin/groupadd — create a new group in /etc/group (+ /etc/gshadow).
 *
 * Usage:  groupadd [-g GID] [-r] [-f] GROUP
 *
 * Options:
 *   -g GID  Use the supplied GID instead of auto-assigning.  Refuses
 *           to clobber an existing GID unless -o is given.
 *   -o      Allow -g to reuse a GID already in /etc/group.
 *   -r      Allocate from the system range (1..999) instead of the
 *           user range (1000..60000).  No-op when -g is supplied.
 *   -f      Exit 0 instead of failing if GROUP already exists.
 *
 * Exit codes follow shadow-utils:
 *   0  success
 *   2  bad usage
 *   3  invalid argument value
 *   4  GID not unique (without -o)
 *   9  group name already in use (without -f)
 *   10 can't update /etc/group
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

static const char *PROGNAME = "groupadd";

/* Append-the-new-line callback for atomic rewrite of /etc/group. */
struct group_add_ctx {
    const char *name;
    gid_t       gid;
};

static int
write_group(FILE *out, void *arg)
{
    struct group_add_ctx *ctx = (struct group_add_ctx *)arg;
    FILE *in = fopen(PWDB_GROUP, "r");
    char  line[1024];

    if (in != NULL) {
        while (fgets(line, sizeof(line), in) != NULL) {
            if (fputs(line, out) == EOF) {
                fclose(in);
                return -1;
            }
        }
        fclose(in);
    }
    /* If the previous final line lacked a newline, fix it before
     * appending.  Skip — fgets+fputs preserved whatever was there;
     * the typical /etc/group always ends with a newline. */
    if (fprintf(out, "%s:x:%u:\n", ctx->name, (unsigned)ctx->gid) < 0) {
        return -1;
    }
    return 0;
}

static int
write_gshadow(FILE *out, void *arg)
{
    struct group_add_ctx *ctx = (struct group_add_ctx *)arg;
    FILE *in = fopen(PWDB_GSHADOW, "r");
    char  line[1024];

    if (in != NULL) {
        while (fgets(line, sizeof(line), in) != NULL) {
            if (fputs(line, out) == EOF) {
                fclose(in);
                return -1;
            }
        }
        fclose(in);
    }
    /* gshadow format: name : passwd : admin-list : member-list  */
    if (fprintf(out, "%s:!::\n", ctx->name) < 0) {
        return -1;
    }
    return 0;
}

static void
usage(void)
{
    fprintf(stderr,
        "usage: groupadd [-g GID [-o]] [-r] [-f] GROUP\n");
    exit(2);
}

int
main(int argc, char *argv[])
{
    long  gid_arg    = -1;
    int   non_unique = 0;
    int   system     = 0;
    int   force      = 0;
    int   opt;

    while ((opt = getopt(argc, argv, "g:orf")) != -1) {
        switch (opt) {
        case 'g': {
            char *end;
            errno = 0;
            long v = strtol(optarg, &end, 10);
            if (errno != 0 || *end != '\0' || v < 0 || v > USER_ID_MAX) {
                pwdb_die(PROGNAME, "invalid GID '%s'", optarg);
            }
            gid_arg = v;
            break;
        }
        case 'o': non_unique = 1; break;
        case 'r': system     = 1; break;
        case 'f': force      = 1; break;
        default:  usage();
        }
    }
    if (optind != argc - 1) usage();
    const char *name = argv[optind];

    pwdb_require_root(PROGNAME);

    if (!pwdb_valid_name(name)) {
        pwdb_die(PROGNAME, "invalid group name '%s'", name);
    }

    int lock = pwdb_lock();
    if (lock < 0) {
        pwdb_die(PROGNAME, "cannot lock %s: %s", PWDB_LOCK, strerror(errno));
    }

    /* Existence checks AFTER taking the lock so two concurrent
     * groupadd invocations see consistent state. */
    if (getgrnam(name) != NULL) {
        pwdb_unlock(lock);
        if (force) {
            return 0;
        }
        fprintf(stderr, "groupadd: group '%s' already exists\n", name);
        return 9;
    }

    if (gid_arg >= 0) {
        if (!non_unique && getgrgid((gid_t)gid_arg) != NULL) {
            pwdb_unlock(lock);
            fprintf(stderr, "groupadd: GID %ld already in use\n", gid_arg);
            return 4;
        }
    } else {
        long min = system ? SYSTEM_ID_MIN : USER_ID_MIN;
        long max = system ? SYSTEM_ID_MAX : USER_ID_MAX;
        gid_arg = pwdb_next_free_id(1, min, max);
        if (gid_arg < 0) {
            pwdb_unlock(lock);
            pwdb_die(PROGNAME, "no free GID in range %ld..%ld", min, max);
        }
    }

    struct group_add_ctx ctx = {
        .name = name,
        .gid  = (gid_t)gid_arg,
    };

    if (pwdb_atomic_rewrite(PWDB_GROUP, 0644, write_group, &ctx) < 0) {
        pwdb_unlock(lock);
        fprintf(stderr, "groupadd: failed to update %s: %s\n",
                PWDB_GROUP, strerror(errno));
        return 10;
    }
    /* /etc/gshadow is best-effort — substrate doesn't require it.
     * If it exists we keep it consistent; if not, skip silently. */
    if (access(PWDB_GSHADOW, F_OK) == 0) {
        if (pwdb_atomic_rewrite(PWDB_GSHADOW, 0640, write_gshadow, &ctx) < 0) {
            fprintf(stderr,
                "groupadd: warning: failed to update %s: %s\n",
                PWDB_GSHADOW, strerror(errno));
            /* Don't roll back /etc/group — gshadow is optional. */
        }
    }

    pwdb_unlock(lock);
    return 0;
}
