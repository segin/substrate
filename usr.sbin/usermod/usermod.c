/*
 * usr.sbin/usermod — modify an existing user account.
 *
 * Usage:
 *   usermod [-l NEW_LOGIN] [-u UID [-o]] [-g GROUP]
 *           [-G LIST] [-a] [-d HOMEDIR [-m]] [-s SHELL]
 *           [-c COMMENT] [-L | -U] USER
 *
 * Options:
 *   -l NEW_LOGIN  Rename the user.  Updates /etc/passwd, /etc/shadow,
 *                 and every gr_mem reference in /etc/group.
 *   -u UID [-o]   Change UID.  -o allows non-unique.  All files in
 *                 the home directory owned by the old UID are NOT
 *                 chowned; admin must do that if desired.
 *   -g GROUP      Change primary group (name or numeric).  Must exist.
 *   -G LIST       Replace the supplementary-group set with LIST.
 *   -a            With -G, APPEND to the existing supplementary set
 *                 instead of replacing it.
 *   -d HOMEDIR    Change the recorded home directory.
 *   -m            With -d, move the existing home directory contents
 *                 to the new location.
 *   -s SHELL      Change the login shell.
 *   -c COMMENT    Change the GECOS field.
 *   -L            Lock the account (prepend '!' to the shadow hash).
 *   -U            Unlock the account (strip leading '!').
 *
 * Exit codes mirror useradd/shadow-utils.
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

static const char *PROGNAME = "usermod";

struct usermod_ctx {
    const char *old_name;
    const char *new_name;     /* NULL == unchanged */
    uid_t       old_uid;
    uid_t       new_uid;
    int         change_uid;
    gid_t       new_gid;
    int         change_gid;
    const char *new_home;     /* NULL == unchanged */
    const char *new_shell;    /* NULL == unchanged */
    const char *new_gecos;    /* NULL == unchanged */
    int         lock_account;
    int         unlock_account;
    /* Supplementary-group bookkeeping. */
    const char *supp_csv;     /* NULL == leave alone */
    int         supp_append;  /* 1 = -aG; 0 = -G replace */
};

static int
write_passwd(FILE *out, void *arg)
{
    struct usermod_ctx *ctx = (struct usermod_ctx *)arg;
    FILE *in = fopen(PWDB_PASSWD, "r");
    char  line[1024];
    size_t nlen = strlen(ctx->old_name);

    if (in == NULL) return -1;
    while (fgets(line, sizeof(line), in) != NULL) {
        if (strncmp(line, ctx->old_name, nlen) == 0 && line[nlen] == ':') {
            char  copy[1024];
            char *fields[7];
            strncpy(copy, line, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            int n = pwdb_split(copy, ':', fields, 7);
            if (n < 7) {
                if (fputs(line, out) == EOF) { fclose(in); return -1; }
                continue;
            }
            const char *name_o  = ctx->new_name  ? ctx->new_name  : fields[0];
            uid_t       uid_o   = ctx->change_uid ? ctx->new_uid
                                                  : (uid_t)strtoul(fields[2], NULL, 10);
            gid_t       gid_o   = ctx->change_gid ? ctx->new_gid
                                                  : (gid_t)strtoul(fields[3], NULL, 10);
            const char *gecos_o = ctx->new_gecos ? ctx->new_gecos : fields[4];
            const char *home_o  = ctx->new_home  ? ctx->new_home  : fields[5];
            const char *shell_o = ctx->new_shell ? ctx->new_shell : fields[6];
            if (fprintf(out, "%s:%s:%u:%u:%s:%s:%s\n",
                        name_o, fields[1],
                        (unsigned)uid_o, (unsigned)gid_o,
                        gecos_o, home_o, shell_o) < 0) {
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

static int
write_shadow(FILE *out, void *arg)
{
    struct usermod_ctx *ctx = (struct usermod_ctx *)arg;
    FILE *in = fopen(PWDB_SHADOW, "r");
    char  line[1024];
    size_t nlen = strlen(ctx->old_name);

    if (in == NULL) return 0;
    while (fgets(line, sizeof(line), in) != NULL) {
        if (strncmp(line, ctx->old_name, nlen) == 0 && line[nlen] == ':') {
            char  copy[1024];
            char *fields[9];
            strncpy(copy, line, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            int n = pwdb_split(copy, ':', fields, 9);
            if (n < 9) {
                if (fputs(line, out) == EOF) { fclose(in); return -1; }
                continue;
            }
            const char *name_o = ctx->new_name ? ctx->new_name : fields[0];
            char        hash[512];
            strncpy(hash, fields[1], sizeof(hash) - 2);
            hash[sizeof(hash) - 1] = '\0';
            if (ctx->lock_account && hash[0] != '!') {
                /* Prepend ! to the existing hash. */
                memmove(hash + 1, hash, strlen(hash) + 1);
                hash[0] = '!';
            } else if (ctx->unlock_account && hash[0] == '!') {
                memmove(hash, hash + 1, strlen(hash));
            }
            if (fprintf(out, "%s:%s:%s:%s:%s:%s:%s:%s:%s\n",
                        name_o, hash,
                        fields[2], fields[3], fields[4],
                        fields[5], fields[6], fields[7], fields[8]) < 0) {
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

/*
 * Rewrite /etc/group: rename USER in every gr_mem list, and apply
 * the supplementary-group changes from -G / -aG.
 */
static int
list_contains(const char *list, const char *name)
{
    size_t nlen = strlen(name);
    const char *p = list;
    while (*p != '\0') {
        char *comma = strchr(p, ',');
        size_t seg = comma ? (size_t)(comma - p) : strlen(p);
        if (seg == nlen && strncmp(p, name, nlen) == 0) return 1;
        if (comma == NULL) break;
        p = comma + 1;
    }
    return 0;
}

static int
csv_contains_group(const char *csv, const char *gname)
{
    return list_contains(csv, gname);
}

static void
list_remove(char *list, const char *name)
{
    size_t nlen = strlen(name);
    char  *src  = list;
    char  *dst  = list;
    int    first = 1;
    while (*src != '\0') {
        char *comma = strchr(src, ',');
        size_t seg = comma ? (size_t)(comma - src) : strlen(src);
        if (seg == nlen && strncmp(src, name, nlen) == 0) {
            /* drop */
        } else {
            if (!first) *dst++ = ',';
            memmove(dst, src, seg);
            dst += seg;
            first = 0;
        }
        if (comma == NULL) break;
        src = comma + 1;
    }
    *dst = '\0';
}

static void
list_replace(char *list, size_t list_sz, const char *old, const char *neu)
{
    char tmp[1024];
    size_t out = 0;
    size_t olen = strlen(old);
    const char *p = list;
    int first = 1;
    while (*p != '\0') {
        char *comma = strchr(p, ',');
        size_t seg = comma ? (size_t)(comma - p) : strlen(p);
        const char *src;
        size_t     slen;
        if (seg == olen && strncmp(p, old, olen) == 0) {
            src  = neu;
            slen = strlen(neu);
        } else {
            src  = p;
            slen = seg;
        }
        if (!first) {
            if (out + 1 >= sizeof(tmp)) break;
            tmp[out++] = ',';
        }
        if (out + slen >= sizeof(tmp)) break;
        memcpy(tmp + out, src, slen);
        out += slen;
        first = 0;
        if (comma == NULL) break;
        p = comma + 1;
    }
    tmp[out] = '\0';
    strncpy(list, tmp, list_sz - 1);
    list[list_sz - 1] = '\0';
}

static int
write_group(FILE *out, void *arg)
{
    struct usermod_ctx *ctx = (struct usermod_ctx *)arg;
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
        char members[1024];
        strncpy(members, fields[3], sizeof(members) - 1);
        members[sizeof(members) - 1] = '\0';

        /* Rename existing references. */
        if (ctx->new_name != NULL && list_contains(members, ctx->old_name)) {
            list_replace(members, sizeof(members),
                         ctx->old_name, ctx->new_name);
        }

        /* -G / -aG handling: presence in members is governed by
         * supp_csv membership.  In append mode, we leave existing
         * membership alone and only add; in replace mode, we ensure
         * USER is in `gname` iff `gname` is in supp_csv. */
        if (ctx->supp_csv != NULL) {
            const char *user = ctx->new_name ? ctx->new_name : ctx->old_name;
            int should_be_in = csv_contains_group(ctx->supp_csv, fields[0]);
            int currently_in = list_contains(members, user);
            if (should_be_in && !currently_in) {
                size_t cur = strlen(members);
                if (cur > 0 && cur < sizeof(members) - 1) {
                    members[cur++] = ',';
                    members[cur] = '\0';
                }
                strncat(members, user, sizeof(members) - cur - 1);
            } else if (!should_be_in && currently_in && !ctx->supp_append) {
                list_remove(members, user);
            }
        }

        if (fprintf(out, "%s:%s:%s:%s\n",
                    fields[0], fields[1], fields[2], members) < 0) {
            fclose(in);
            return -1;
        }
    }
    fclose(in);
    return 0;
}

/* Recursive move-merge of `src` into `dst`.  Used by -d -m. */
static int
move_tree(const char *src, const char *dst)
{
    /* If both are on the same filesystem, rename(2) handles the
     * whole tree in one syscall.  Fall back to copy-rm if rename
     * fails with EXDEV.  Keep it simple: only the rename path for
     * now; cross-fs copy is left as a TODO. */
    if (rename(src, dst) == 0) return 0;
    if (errno == EXDEV) {
        errno = ENOSYS; /* cross-fs move not implemented */
    }
    return -1;
}

static void
usage(void)
{
    fprintf(stderr,
        "usage: usermod [-l NEW_LOGIN] [-u UID [-o]] [-g GROUP]\n"
        "               [-G LIST] [-a] [-d HOMEDIR [-m]] [-s SHELL]\n"
        "               [-c COMMENT] [-L | -U] USER\n");
    exit(2);
}

int
main(int argc, char *argv[])
{
    const char *new_login = NULL;
    long        uid_arg   = -1;
    int         non_unique = 0;
    const char *primary_group = NULL;
    const char *supp_csv   = NULL;
    int         supp_append = 0;
    const char *home       = NULL;
    int         move_home  = 0;
    const char *shell      = NULL;
    const char *gecos      = NULL;
    int         lock       = 0;
    int         unlock     = 0;
    int         opt;

    while ((opt = getopt(argc, argv, "l:u:og:G:ad:ms:c:LU")) != -1) {
        switch (opt) {
        case 'l':
            if (!pwdb_valid_name(optarg)) {
                pwdb_die(PROGNAME, "invalid new name '%s'", optarg);
            }
            new_login = optarg;
            break;
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
        case 'o': non_unique     = 1; break;
        case 'g': primary_group  = optarg; break;
        case 'G': supp_csv       = optarg; break;
        case 'a': supp_append    = 1; break;
        case 'd': home           = optarg; break;
        case 'm': move_home      = 1; break;
        case 's': shell          = optarg; break;
        case 'c': gecos          = optarg; break;
        case 'L': lock           = 1; break;
        case 'U': unlock         = 1; break;
        default:  usage();
        }
    }
    if (optind != argc - 1) usage();
    const char *name = argv[optind];
    if (lock && unlock) {
        pwdb_die(PROGNAME, "-L and -U are mutually exclusive");
    }
    if (supp_append && supp_csv == NULL) {
        pwdb_die(PROGNAME, "-a requires -G");
    }

    pwdb_require_root(PROGNAME);

    int lockfd = pwdb_lock();
    if (lockfd < 0) {
        pwdb_die(PROGNAME, "cannot lock %s: %s", PWDB_LOCK, strerror(errno));
    }

    struct passwd *pw = getpwnam(name);
    if (pw == NULL) {
        pwdb_unlock(lockfd);
        fprintf(stderr, "usermod: user '%s' does not exist\n", name);
        return 6;
    }

    /* Resolve primary group if requested. */
    int   change_gid = 0;
    gid_t new_gid    = pw->pw_gid;
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
            pwdb_unlock(lockfd);
            fprintf(stderr, "usermod: group '%s' does not exist\n", primary_group);
            return 6;
        }
        if (gr->gr_gid != pw->pw_gid) {
            change_gid = 1;
            new_gid    = gr->gr_gid;
        }
    }

    /* UID uniqueness check. */
    if (uid_arg >= 0 && (uid_t)uid_arg != pw->pw_uid &&
        !non_unique && getpwuid((uid_t)uid_arg) != NULL) {
        pwdb_unlock(lockfd);
        fprintf(stderr, "usermod: UID %ld already in use\n", uid_arg);
        return 4;
    }
    if (new_login != NULL && strcmp(new_login, name) != 0 &&
        getpwnam(new_login) != NULL) {
        pwdb_unlock(lockfd);
        fprintf(stderr, "usermod: name '%s' already in use\n", new_login);
        return 9;
    }

    /* Validate supplementary groups. */
    if (supp_csv != NULL) {
        const char *p = supp_csv;
        while (*p != '\0') {
            char *comma = strchr(p, ',');
            size_t      seg   = comma ? (size_t)(comma - p) : strlen(p);
            char        gname[64];
            if (seg == 0 || seg >= sizeof(gname)) {
                pwdb_unlock(lockfd);
                pwdb_die(PROGNAME, "invalid group spec '%s'", supp_csv);
            }
            memcpy(gname, p, seg);
            gname[seg] = '\0';
            if (getgrnam(gname) == NULL) {
                pwdb_unlock(lockfd);
                fprintf(stderr, "usermod: group '%s' does not exist\n", gname);
                return 6;
            }
            if (comma == NULL) break;
            p = comma + 1;
        }
    }

    struct usermod_ctx ctx = {
        .old_name        = name,
        .new_name        = (new_login != NULL && strcmp(new_login, name) != 0)
                              ? new_login : NULL,
        .old_uid         = pw->pw_uid,
        .new_uid         = (uid_arg >= 0) ? (uid_t)uid_arg : pw->pw_uid,
        .change_uid      = (uid_arg >= 0 && (uid_t)uid_arg != pw->pw_uid),
        .new_gid         = new_gid,
        .change_gid      = change_gid,
        .new_home        = home,
        .new_shell       = shell,
        .new_gecos       = gecos,
        .lock_account    = lock,
        .unlock_account  = unlock,
        .supp_csv        = supp_csv,
        .supp_append     = supp_append,
    };

    char old_home[256];
    strncpy(old_home, pw->pw_dir ? pw->pw_dir : "", sizeof(old_home) - 1);
    old_home[sizeof(old_home) - 1] = '\0';

    if (pwdb_atomic_rewrite(PWDB_PASSWD, 0644, write_passwd, &ctx) < 0) {
        pwdb_unlock(lockfd);
        fprintf(stderr, "usermod: failed to update %s: %s\n",
                PWDB_PASSWD, strerror(errno));
        return 10;
    }
    if (access(PWDB_SHADOW, F_OK) == 0) {
        if (pwdb_atomic_rewrite(PWDB_SHADOW, 0640, write_shadow, &ctx) < 0) {
            fprintf(stderr,
                "usermod: warning: failed to update %s: %s\n",
                PWDB_SHADOW, strerror(errno));
        }
    }
    if (ctx.new_name != NULL || ctx.supp_csv != NULL) {
        if (pwdb_atomic_rewrite(PWDB_GROUP, 0644, write_group, &ctx) < 0) {
            fprintf(stderr,
                "usermod: warning: failed to update %s: %s\n",
                PWDB_GROUP, strerror(errno));
        }
    }

    if (home != NULL && move_home && old_home[0] == '/' && home[0] == '/' &&
        strcmp(old_home, home) != 0 && access(old_home, F_OK) == 0) {
        if (move_tree(old_home, home) < 0) {
            fprintf(stderr,
                "usermod: warning: could not move home '%s' -> '%s': %s\n",
                old_home, home, strerror(errno));
        } else if (chown(home, ctx.new_uid, ctx.new_gid) < 0) {
            fprintf(stderr,
                "usermod: warning: chown '%s': %s\n",
                home, strerror(errno));
        }
    }

    pwdb_unlock(lockfd);
    return 0;
}
