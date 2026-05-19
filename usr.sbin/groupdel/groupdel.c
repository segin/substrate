/*
 * usr.sbin/groupdel — remove a group from /etc/group (+ /etc/gshadow).
 *
 * Usage:  groupdel [-f] GROUP
 *
 * Refuses to remove a group that is the primary group of any user
 * in /etc/passwd unless -f (force) is supplied — pulling the floor
 * out from under live accounts almost always leaves something
 * orphaned and confusing.
 *
 * Exit codes:
 *   0  success
 *   2  bad usage
 *   6  GROUP doesn't exist
 *   8  GROUP is the primary group of an existing user
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

static const char *PROGNAME = "groupdel";

struct group_del_ctx {
    const char *name;
};

static int
write_group(FILE *out, void *arg)
{
    struct group_del_ctx *ctx = (struct group_del_ctx *)arg;
    FILE *in = fopen(PWDB_GROUP, "r");
    char  line[1024];
    size_t nlen = strlen(ctx->name);

    if (in == NULL) return -1;
    while (fgets(line, sizeof(line), in) != NULL) {
        /* Match group lines whose first field equals ctx->name. */
        if (strncmp(line, ctx->name, nlen) == 0 && line[nlen] == ':') {
            continue;  /* drop */
        }
        if (fputs(line, out) == EOF) {
            fclose(in);
            return -1;
        }
    }
    fclose(in);
    return 0;
}

static int
write_gshadow(FILE *out, void *arg)
{
    struct group_del_ctx *ctx = (struct group_del_ctx *)arg;
    FILE *in = fopen(PWDB_GSHADOW, "r");
    char  line[1024];
    size_t nlen = strlen(ctx->name);

    if (in == NULL) return 0;  /* nothing to rewrite */
    while (fgets(line, sizeof(line), in) != NULL) {
        if (strncmp(line, ctx->name, nlen) == 0 && line[nlen] == ':') {
            continue;
        }
        if (fputs(line, out) == EOF) {
            fclose(in);
            return -1;
        }
    }
    fclose(in);
    return 0;
}

static void
usage(void)
{
    fprintf(stderr, "usage: groupdel [-f] GROUP\n");
    exit(2);
}

int
main(int argc, char *argv[])
{
    int opt;
    int force = 0;

    while ((opt = getopt(argc, argv, "f")) != -1) {
        switch (opt) {
        case 'f': force = 1; break;
        default:  usage();
        }
    }
    if (optind != argc - 1) usage();
    const char *name = argv[optind];

    pwdb_require_root(PROGNAME);

    int lock = pwdb_lock();
    if (lock < 0) {
        pwdb_die(PROGNAME, "cannot lock %s: %s", PWDB_LOCK, strerror(errno));
    }

    struct group *gr = getgrnam(name);
    if (gr == NULL) {
        pwdb_unlock(lock);
        fprintf(stderr, "groupdel: group '%s' does not exist\n", name);
        return 6;
    }
    gid_t victim_gid = gr->gr_gid;

    /* Primary-group safety check.  Walk /etc/passwd to see if any
     * user has this gid as pw_gid.  Bypass with -f. */
    if (!force) {
        struct passwd *pw;
        setpwent();
        while ((pw = getpwent()) != NULL) {
            if (pw->pw_gid == victim_gid) {
                endpwent();
                pwdb_unlock(lock);
                fprintf(stderr,
                    "groupdel: cannot remove the primary group of user '%s'\n",
                    pw->pw_name);
                return 8;
            }
        }
        endpwent();
    }

    struct group_del_ctx ctx = { .name = name };

    if (pwdb_atomic_rewrite(PWDB_GROUP, 0644, write_group, &ctx) < 0) {
        pwdb_unlock(lock);
        fprintf(stderr, "groupdel: failed to update %s: %s\n",
                PWDB_GROUP, strerror(errno));
        return 10;
    }
    if (access(PWDB_GSHADOW, F_OK) == 0) {
        if (pwdb_atomic_rewrite(PWDB_GSHADOW, 0640, write_gshadow, &ctx) < 0) {
            fprintf(stderr,
                "groupdel: warning: failed to update %s: %s\n",
                PWDB_GSHADOW, strerror(errno));
        }
    }

    pwdb_unlock(lock);
    return 0;
}
