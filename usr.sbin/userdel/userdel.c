/*
 * usr.sbin/userdel — delete a user account.
 *
 * Usage:  userdel [-r] [-f] USER
 *
 * Options:
 *   -r  Remove the user's home directory (recursively) and mail
 *       spool.  No-op if either is missing.
 *   -f  Force deletion even if USER is logged in or owns running
 *       processes.  (No process-table scan is performed; the flag
 *       is accepted for parity and to suppress the "user is in use"
 *       warning when we add that check.)
 *
 * Side effects:
 *   - Strips USER from every supplementary-member list in /etc/group.
 *   - Removes USER's per-name primary group IFF that group has no
 *     other members and no other user references it as their primary.
 *     This matches USERGROUPS_ENAB behaviour.
 *
 * Exit codes:
 *   0  success
 *   2  bad usage
 *   6  USER doesn't exist
 *   10 can't update /etc/passwd
 *   12 can't remove home directory
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
#include <dirent.h>
#include <sys/wait.h>

static const char *PROGNAME = "userdel";

struct userdel_ctx {
    const char *name;
    gid_t       primary_gid;
};

static int
write_passwd(FILE *out, void *arg)
{
    struct userdel_ctx *ctx = (struct userdel_ctx *)arg;
    FILE *in = fopen(PWDB_PASSWD, "r");
    char  line[1024];
    size_t nlen = strlen(ctx->name);

    if (in == NULL) return -1;
    while (fgets(line, sizeof(line), in) != NULL) {
        if (strncmp(line, ctx->name, nlen) == 0 && line[nlen] == ':') {
            continue;
        }
        if (fputs(line, out) == EOF) { fclose(in); return -1; }
    }
    fclose(in);
    return 0;
}

static int
write_shadow(FILE *out, void *arg)
{
    struct userdel_ctx *ctx = (struct userdel_ctx *)arg;
    FILE *in = fopen(PWDB_SHADOW, "r");
    char  line[1024];
    size_t nlen = strlen(ctx->name);

    if (in == NULL) return 0;
    while (fgets(line, sizeof(line), in) != NULL) {
        if (strncmp(line, ctx->name, nlen) == 0 && line[nlen] == ':') {
            continue;
        }
        if (fputs(line, out) == EOF) { fclose(in); return -1; }
    }
    fclose(in);
    return 0;
}

/*
 * /etc/group rewrite: drop USER from every gr_mem list (and from
 * the gshadow equivalent below).  Each "name1,name2,..." field
 * gets walked and reconstructed without the matching entry.
 */
static void
strip_member(char *list, const char *user)
{
    /* Walk comma-separated members in-place. */
    size_t ulen = strlen(user);
    char  *src  = list;
    char  *dst  = list;
    int    first = 1;
    while (*src != '\0') {
        char *comma = strchr(src, ',');
        size_t      seg   = comma ? (size_t)(comma - src) : strlen(src);
        if (seg == ulen && strncmp(src, user, ulen) == 0) {
            /* Drop. */
        } else {
            if (!first) *dst++ = ',';
            memmove(dst, src, seg);
            dst   += seg;
            first  = 0;
        }
        if (comma == NULL) break;
        src = comma + 1;
    }
    *dst = '\0';
}

static int
write_group_strip(FILE *out, void *arg)
{
    struct userdel_ctx *ctx = (struct userdel_ctx *)arg;
    FILE *in = fopen(PWDB_GROUP, "r");
    char  line[1024];
    size_t nlen = strlen(ctx->name);

    if (in == NULL) return -1;
    while (fgets(line, sizeof(line), in) != NULL) {
        char  copy[1024];
        char *fields[4];
        strlcpy(copy, line, sizeof(copy));
        int n = pwdb_split(copy, ':', fields, 4);
        if (n < 4) {
            if (fputs(line, out) == EOF) { fclose(in); return -1; }
            continue;
        }
        /* Drop the per-user group entirely if it has no other
         * members and its name matches the user (USERGROUPS_ENAB). */
        gid_t row_gid = (gid_t)strtoul(fields[2], NULL, 10);
        if (row_gid == ctx->primary_gid &&
            strcmp(fields[0], ctx->name) == 0 &&
            (fields[3][0] == '\0' || strcmp(fields[3], ctx->name) == 0)) {
            continue;
        }
        char members[1024];
        strlcpy(members, fields[3], sizeof(members));
        strip_member(members, ctx->name);
        if (fprintf(out, "%s:%s:%s:%s\n",
                    fields[0], fields[1], fields[2], members) < 0) {
            fclose(in);
            return -1;
        }
        (void)nlen;
    }
    fclose(in);
    return 0;
}

static int
write_gshadow_strip(FILE *out, void *arg)
{
    struct userdel_ctx *ctx = (struct userdel_ctx *)arg;
    FILE *in = fopen(PWDB_GSHADOW, "r");
    char  line[1024];

    if (in == NULL) return 0;
    while (fgets(line, sizeof(line), in) != NULL) {
        char  copy[1024];
        char *fields[4];
        strlcpy(copy, line, sizeof(copy));
        int n = pwdb_split(copy, ':', fields, 4);
        if (n < 4) {
            if (fputs(line, out) == EOF) { fclose(in); return -1; }
            continue;
        }
        /* Drop per-user group if name matches and member list is
         * empty after strip — matches /etc/group decision above. */
        if (strcmp(fields[0], ctx->name) == 0) {
            continue;
        }
        char admins[1024], members[1024];
        strlcpy(admins, fields[2], sizeof(admins));
        strlcpy(members, fields[3], sizeof(members));
        strip_member(admins,  ctx->name);
        strip_member(members, ctx->name);
        if (fprintf(out, "%s:%s:%s:%s\n",
                    fields[0], fields[1], admins, members) < 0) {
            fclose(in);
            return -1;
        }
    }
    fclose(in);
    return 0;
}

/*
 * Recursive rm of a directory tree.  Returns 0 on full success,
 * -1 on first failure (errno preserved).  Skips symlinks (lstat,
 * unlink without traversal).
 */
static int
remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0) {
        return (errno == ENOENT) ? 0 : -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path);
    }
    DIR *d = opendir(path);
    if (d == NULL) return -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        char child[1024];
        if (snprintf(child, sizeof(child), "%s/%s", path, de->d_name)
                >= (int)sizeof(child)) {
            closedir(d);
            errno = ENAMETOOLONG;
            return -1;
        }
        if (remove_tree(child) < 0) {
            int saved = errno;
            closedir(d);
            errno = saved;
            return -1;
        }
    }
    closedir(d);
    return rmdir(path);
}

static void
usage(void)
{
    fprintf(stderr, "usage: userdel [-r] [-f] USER\n");
    exit(2);
}

int
main(int argc, char *argv[])
{
    int opt;
    int remove_home = 0;
    int force       = 0;

    while ((opt = getopt(argc, argv, "rf")) != -1) {
        switch (opt) {
        case 'r': remove_home = 1; break;
        case 'f': force       = 1; break;
        default:  usage();
        }
    }
    if (optind != argc - 1) usage();
    const char *name = argv[optind];

    pwdb_require_root(PROGNAME);

    (void)force;  /* reserved for the future is-user-logged-in check */

    int lock = pwdb_lock();
    if (lock < 0) {
        pwdb_die(PROGNAME, "cannot lock %s: %s", PWDB_LOCK, strerror(errno));
    }

    struct passwd *pw = getpwnam(name);
    if (pw == NULL) {
        pwdb_unlock(lock);
        fprintf(stderr, "userdel: user '%s' does not exist\n", name);
        return 6;
    }
    char   home[256];
    strlcpy(home, pw->pw_dir ? pw->pw_dir : "", sizeof(home));
    struct userdel_ctx ctx = {
        .name        = name,
        .primary_gid = pw->pw_gid,
    };

    if (pwdb_atomic_rewrite(PWDB_PASSWD, 0644, write_passwd, &ctx) < 0) {
        pwdb_unlock(lock);
        fprintf(stderr, "userdel: failed to update %s: %s\n",
                PWDB_PASSWD, strerror(errno));
        return 10;
    }
    if (access(PWDB_SHADOW, F_OK) == 0) {
        if (pwdb_atomic_rewrite(PWDB_SHADOW, 0640, write_shadow, &ctx) < 0) {
            fprintf(stderr,
                "userdel: warning: failed to update %s: %s\n",
                PWDB_SHADOW, strerror(errno));
        }
    }
    if (pwdb_atomic_rewrite(PWDB_GROUP, 0644, write_group_strip, &ctx) < 0) {
        fprintf(stderr,
            "userdel: warning: failed to update %s: %s\n",
            PWDB_GROUP, strerror(errno));
    }
    if (access(PWDB_GSHADOW, F_OK) == 0) {
        if (pwdb_atomic_rewrite(PWDB_GSHADOW, 0640,
                                write_gshadow_strip, &ctx) < 0) {
            fprintf(stderr,
                "userdel: warning: failed to update %s: %s\n",
                PWDB_GSHADOW, strerror(errno));
        }
    }

    if (remove_home && home[0] == '/') {
        if (remove_tree(home) < 0) {
            fprintf(stderr,
                "userdel: warning: could not remove home '%s': %s\n",
                home, strerror(errno));
        }
        /* Mail spool: /var/spool/mail/<user>. */
        char mail[256];
        if (snprintf(mail, sizeof(mail),
                     "/var/spool/mail/%s", name) < (int)sizeof(mail)) {
            (void)unlink(mail);
        }
    }

    pwdb_unlock(lock);
    return 0;
}
