/*
 * usr.sbin/groupmod — modify an existing group.
 *
 * Usage:  groupmod [-g GID [-o]] [-n NEW_NAME] GROUP
 *
 * Options:
 *   -g GID      Assign a new GID.  Refuses to clobber an existing
 *               GID unless -o is also given.  When the GID changes,
 *               every entry in /etc/passwd whose pw_gid matched the
 *               old value is updated in place.
 *   -o          Allow -g to reuse an existing GID.
 *   -n NEW_NAME Rename the group.  Updates the gr_name field in
 *               /etc/group (+ /etc/gshadow if present) and every
 *               reference in supplementary-member lists.
 *
 * Exit codes:
 *   0  success
 *   2  bad usage
 *   3  invalid argument value
 *   4  GID not unique (without -o)
 *   6  GROUP doesn't exist
 *   9  NEW_NAME already in use
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

static const char *PROGNAME = "groupmod";

struct groupmod_ctx {
    const char *old_name;
    const char *new_name;  /* NULL = unchanged */
    gid_t       old_gid;
    gid_t       new_gid;
    int         change_gid;
};

static int
write_group(FILE *out, void *arg)
{
    struct groupmod_ctx *ctx = (struct groupmod_ctx *)arg;
    FILE *in = fopen(PWDB_GROUP, "r");
    char  line[1024];
    size_t old_len = strlen(ctx->old_name);

    if (in == NULL) return -1;

    while (fgets(line, sizeof(line), in) != NULL) {
        /* Each line: name:passwd:gid:member-list */
        if (strncmp(line, ctx->old_name, old_len) == 0 &&
            line[old_len] == ':') {
            /* The target group itself: rewrite name and/or gid. */
            char *fields[4];
            char  copy[1024];
            strncpy(copy, line, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            int n = pwdb_split(copy, ':', fields, 4);
            if (n < 4) {
                /* Malformed line — preserve as-is. */
                if (fputs(line, out) == EOF) { fclose(in); return -1; }
                continue;
            }
            const char *name_out = ctx->new_name ? ctx->new_name
                                                 : ctx->old_name;
            gid_t       gid_out  = ctx->change_gid ? ctx->new_gid
                                                   : ctx->old_gid;
            if (fprintf(out, "%s:%s:%u:%s\n",
                        name_out, fields[1], (unsigned)gid_out,
                        fields[3]) < 0) {
                fclose(in);
                return -1;
            }
            continue;
        }
        /* Other group lines: rewrite member-list if it references
         * the renamed group?  No — gr_mem references USERS, not
         * other groups.  Pass through verbatim. */
        if (fputs(line, out) == EOF) { fclose(in); return -1; }
    }
    fclose(in);
    return 0;
}

static int
write_passwd(FILE *out, void *arg)
{
    struct groupmod_ctx *ctx = (struct groupmod_ctx *)arg;
    FILE *in = fopen(PWDB_PASSWD, "r");
    char  line[1024];

    if (in == NULL) return -1;
    while (fgets(line, sizeof(line), in) != NULL) {
        char  copy[1024];
        char *fields[7];

        strncpy(copy, line, sizeof(copy) - 1);
        copy[sizeof(copy) - 1] = '\0';

        int n = pwdb_split(copy, ':', fields, 7);
        if (n < 7) {
            if (fputs(line, out) == EOF) { fclose(in); return -1; }
            continue;
        }
        gid_t row_gid = (gid_t)strtoul(fields[3], NULL, 10);
        if (row_gid == ctx->old_gid) {
            if (fprintf(out, "%s:%s:%s:%u:%s:%s:%s\n",
                        fields[0], fields[1], fields[2],
                        (unsigned)ctx->new_gid,
                        fields[4], fields[5], fields[6]) < 0) {
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

static int
write_gshadow(FILE *out, void *arg)
{
    struct groupmod_ctx *ctx = (struct groupmod_ctx *)arg;
    FILE *in = fopen(PWDB_GSHADOW, "r");
    char  line[1024];
    size_t old_len = strlen(ctx->old_name);

    if (in == NULL) return 0;
    while (fgets(line, sizeof(line), in) != NULL) {
        if (ctx->new_name != NULL &&
            strncmp(line, ctx->old_name, old_len) == 0 &&
            line[old_len] == ':') {
            /* Rename the gshadow row's first field. */
            const char *rest = line + old_len; /* points at the colon */
            if (fprintf(out, "%s%s", ctx->new_name, rest) < 0) {
                fclose(in);
                return -1;
            }
            continue;
        }
        if (fputs(line, out) == EOF) { fclose(in); return -1; }
    }
    fclose(in);
    return 0;
}

static void
usage(void)
{
    fprintf(stderr, "usage: groupmod [-g GID [-o]] [-n NEW_NAME] GROUP\n");
    exit(2);
}

int
main(int argc, char *argv[])
{
    long        gid_arg     = -1;
    int         non_unique  = 0;
    const char *new_name    = NULL;
    int         opt;

    while ((opt = getopt(argc, argv, "g:on:")) != -1) {
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
        case 'n':
            if (!pwdb_valid_name(optarg)) {
                pwdb_die(PROGNAME, "invalid group name '%s'", optarg);
            }
            new_name = optarg;
            break;
        default:  usage();
        }
    }
    if (optind != argc - 1) usage();
    const char *name = argv[optind];

    pwdb_require_root(PROGNAME);

    if (gid_arg < 0 && new_name == NULL) {
        /* No-op call.  Treat as success — matches shadow-utils. */
        return 0;
    }

    int lock = pwdb_lock();
    if (lock < 0) {
        pwdb_die(PROGNAME, "cannot lock %s: %s", PWDB_LOCK, strerror(errno));
    }

    struct group *gr = getgrnam(name);
    if (gr == NULL) {
        pwdb_unlock(lock);
        fprintf(stderr, "groupmod: group '%s' does not exist\n", name);
        return 6;
    }
    gid_t old_gid = gr->gr_gid;

    if (new_name != NULL && strcmp(new_name, name) != 0) {
        if (getgrnam(new_name) != NULL) {
            pwdb_unlock(lock);
            fprintf(stderr,
                "groupmod: name '%s' already in use\n", new_name);
            return 9;
        }
    }

    if (gid_arg >= 0 && (gid_t)gid_arg != old_gid) {
        if (!non_unique && getgrgid((gid_t)gid_arg) != NULL) {
            pwdb_unlock(lock);
            fprintf(stderr, "groupmod: GID %ld already in use\n", gid_arg);
            return 4;
        }
    }

    struct groupmod_ctx ctx = {
        .old_name   = name,
        .new_name   = (new_name != NULL && strcmp(new_name, name) != 0)
                          ? new_name : NULL,
        .old_gid    = old_gid,
        .new_gid    = (gid_arg >= 0) ? (gid_t)gid_arg : old_gid,
        .change_gid = (gid_arg >= 0 && (gid_t)gid_arg != old_gid) ? 1 : 0,
    };

    if (pwdb_atomic_rewrite(PWDB_GROUP, 0644, write_group, &ctx) < 0) {
        pwdb_unlock(lock);
        fprintf(stderr, "groupmod: failed to update %s: %s\n",
                PWDB_GROUP, strerror(errno));
        return 10;
    }
    if (ctx.change_gid) {
        if (pwdb_atomic_rewrite(PWDB_PASSWD, 0644, write_passwd, &ctx) < 0) {
            pwdb_unlock(lock);
            fprintf(stderr,
                "groupmod: warning: failed to renumber pw_gid in %s: %s\n",
                PWDB_PASSWD, strerror(errno));
            return 10;
        }
    }
    if (access(PWDB_GSHADOW, F_OK) == 0) {
        if (pwdb_atomic_rewrite(PWDB_GSHADOW, 0640, write_gshadow, &ctx) < 0) {
            fprintf(stderr,
                "groupmod: warning: failed to update %s: %s\n",
                PWDB_GSHADOW, strerror(errno));
        }
    }

    pwdb_unlock(lock);
    return 0;
}
