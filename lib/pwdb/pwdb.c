/*
 * lib/pwdb/pwdb.c — implementation of <sys/pwdb.h>.
 *
 * See <sys/pwdb.h> for the per-function contract.  This file is
 * built into libpwdb.a (static) and libpwdb.so.0 (dynamic) and
 * is linked by every substrate user/group admin tool.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/pwdb.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

void
pwdb_die(const char *progname, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

int
pwdb_valid_name(const char *name)
{
    if (name == NULL || *name == '\0') return 0;
    /* POSIX-portable: starts alpha or underscore, then [a-z0-9_-]. */
    if (!isalpha((unsigned char)name[0]) && name[0] != '_') return 0;
    for (const char *p = name; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '_' || c == '-')) return 0;
    }
    /* Max length: 32 chars matches our utmp/utmpx UT_NAMESIZE. */
    if (strlen(name) > 32) return 0;
    return 1;
}

int
pwdb_lock(void)
{
    /* POSIX advisory lock on /etc/.pwd.lock.  Use fcntl(F_SETLKW)
     * — works without dragging in the kernel-shadowed <sys/file.h>
     * and is the more portable spelling anyway. */
    int fd = open(PWDB_LOCK, O_WRONLY | O_CREAT, 0600);
    if (fd < 0) return -1;
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;    /* whole file */
    if (fcntl(fd, F_SETLKW, &fl) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

void
pwdb_unlock(int fd)
{
    if (fd < 0) return;
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;
    (void)fcntl(fd, F_SETLK, &fl);
    close(fd);
}

int
pwdb_atomic_rewrite(const char *path, mode_t default_mode,
                    pwdb_write_cb cb, void *arg)
{
    char        tmp[256];
    struct stat st;
    mode_t      mode      = default_mode;
    uid_t       owner_uid = 0;
    gid_t       owner_gid = 0;
    int         fd;
    FILE       *fp;
    int         rc;

    if (snprintf(tmp, sizeof(tmp), "%s.new", path) >= (int)sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (stat(path, &st) == 0) {
        mode      = st.st_mode & 07777;
        owner_uid = st.st_uid;
        owner_gid = st.st_gid;
    }

    /* O_EXCL so a leftover .new from a prior crash doesn't get
     * silently overwritten — fail loudly and let the admin clean
     * up before retrying. */
    fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, mode);
    if (fd < 0) return -1;

    fp = fdopen(fd, "w");
    if (fp == NULL) {
        int saved = errno;
        close(fd);
        unlink(tmp);
        errno = saved;
        return -1;
    }

    rc = cb(fp, arg);
    if (rc != 0) {
        fclose(fp);
        unlink(tmp);
        return -1;
    }

    if (fflush(fp) != 0) {
        fclose(fp);
        unlink(tmp);
        return -1;
    }
    /* fileno is still valid until fclose. */
    if (fsync(fileno(fp)) < 0) {
        fclose(fp);
        unlink(tmp);
        return -1;
    }
    if (fclose(fp) != 0) {
        unlink(tmp);
        return -1;
    }

    /* Preserve ownership when running as root onto a non-root file. */
    if (chown(tmp, owner_uid, owner_gid) < 0 && errno != EPERM) {
        int saved = errno;
        unlink(tmp);
        errno = saved;
        return -1;
    }

    if (rename(tmp, path) < 0) {
        int saved = errno;
        unlink(tmp);
        errno = saved;
        return -1;
    }
    return 0;
}

long
pwdb_next_free_id(int is_group, long min, long max)
{
    if (max < min) {
        errno = EINVAL;
        return -1;
    }
    char *seen = calloc((size_t)(max - min + 1), sizeof(char));
    if (seen == NULL) return -1;

    if (is_group) {
        struct group *gr;
        setgrent();
        while ((gr = getgrent()) != NULL) {
            long id = (long)gr->gr_gid;
            if (id >= min && id <= max) seen[id - min] = 1;
        }
        endgrent();
    } else {
        struct passwd *pw;
        setpwent();
        while ((pw = getpwent()) != NULL) {
            long id = (long)pw->pw_uid;
            if (id >= min && id <= max) seen[id - min] = 1;
        }
        endpwent();
    }

    for (long i = min; i <= max; i++) {
        if (!seen[i - min]) {
            free(seen);
            return i;
        }
    }
    free(seen);
    return -1;
}

long
pwdb_today_days(void)
{
    return (long)(time(NULL) / 86400);
}

int
pwdb_split(char *line, char delim, char **out, int out_max)
{
    int n = 0;
    char *p = line;
    while (n < out_max) {
        out[n++] = p;
        char *q = strchr(p, delim);
        if (q == NULL) break;
        *q = '\0';
        p = q + 1;
    }
    /* Strip trailing newline from the last field. */
    if (n > 0) {
        char *last = out[n - 1];
        size_t l = strlen(last);
        while (l > 0 && (last[l - 1] == '\n' || last[l - 1] == '\r')) {
            last[--l] = '\0';
        }
    }
    return n;
}

void
pwdb_require_root(const char *progname)
{
    if (geteuid() != 0) {
        pwdb_die(progname, "must be run as root");
    }
}
