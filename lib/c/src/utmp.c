/*
 * lib/c/src/utmp.c — utmp/wtmp library.
 *
 * Keeps a single open fd into whichever utmp file is currently
 * targeted (UTMP_FILE by default; utmpname() overrides).  The
 * scanning state (current fd, current record) is in libc static
 * storage — same fragility as glibc; callers must not interleave
 * setutent / pututline from multiple threads without serialization.
 *
 * pututline() walks the file looking for either an exact id match
 * (when ut_id is set) or a line match.  Found → overwrite that slot;
 * not found → append.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <utmp.h>

static const char *ut_path = UTMP_FILE;
static int         ut_fd   = -1;
static struct utmp ut_cur;

static int ut_open(void) {
    if (ut_fd >= 0) return ut_fd;
    /* Open read-write so pututline() works without a re-open dance;
     * fall back to read-only for callers that only scan. */
    ut_fd = open(ut_path, O_RDWR | O_CREAT, 0644);
    if (ut_fd < 0 && (errno == EACCES || errno == EROFS)) {
        ut_fd = open(ut_path, O_RDONLY);
    }
    return ut_fd;
}

void setutent(void) {
    if (ut_open() < 0) return;
    (void)lseek(ut_fd, 0, SEEK_SET);
}

void endutent(void) {
    if (ut_fd >= 0) {
        close(ut_fd);
        ut_fd = -1;
    }
}

int utmpname(const char *file) {
    if (!file) { errno = EINVAL; return -1; }
    endutent();
    /* Keep a copy: caller's pointer may be on the stack and we hold
     * it for the lifetime of the fd. */
    static char path_buf[256];
    size_t n = strlen(file);
    if (n >= sizeof(path_buf)) { errno = ENAMETOOLONG; return -1; }
    memcpy(path_buf, file, n + 1);
    ut_path = path_buf;
    return 0;
}

struct utmp *getutent(void) {
    if (ut_open() < 0) return NULL;
    ssize_t n = read(ut_fd, &ut_cur, sizeof(ut_cur));
    if (n != (ssize_t)sizeof(ut_cur)) return NULL;
    return &ut_cur;
}

/* Match `cur` against `key`.  The rule (glibc-compat) is:
 *
 *   - For ut_type INIT_PROCESS / LOGIN_PROCESS / USER_PROCESS /
 *     DEAD_PROCESS, match by ut_id.
 *   - Otherwise match by ut_type alone (for RUN_LVL / BOOT_TIME /
 *     etc.).
 */
static int ut_id_match(const struct utmp *cur, const struct utmp *key) {
    switch (key->ut_type) {
    case INIT_PROCESS:
    case LOGIN_PROCESS:
    case USER_PROCESS:
    case DEAD_PROCESS:
        if (cur->ut_type != INIT_PROCESS &&
            cur->ut_type != LOGIN_PROCESS &&
            cur->ut_type != USER_PROCESS &&
            cur->ut_type != DEAD_PROCESS) {
            return 0;
        }
        return memcmp(cur->ut_id, key->ut_id, sizeof(cur->ut_id)) == 0;
    case RUN_LVL:
    case BOOT_TIME:
    case NEW_TIME:
    case OLD_TIME:
        return cur->ut_type == key->ut_type;
    default:
        return 0;
    }
}

struct utmp *getutid(const struct utmp *ut) {
    if (!ut || ut_open() < 0) return NULL;
    for (;;) {
        ssize_t n = read(ut_fd, &ut_cur, sizeof(ut_cur));
        if (n != (ssize_t)sizeof(ut_cur)) return NULL;
        if (ut_id_match(&ut_cur, ut)) return &ut_cur;
    }
}

struct utmp *getutline(const struct utmp *ut) {
    if (!ut || ut_open() < 0) return NULL;
    for (;;) {
        ssize_t n = read(ut_fd, &ut_cur, sizeof(ut_cur));
        if (n != (ssize_t)sizeof(ut_cur)) return NULL;
        if ((ut_cur.ut_type == LOGIN_PROCESS ||
             ut_cur.ut_type == USER_PROCESS) &&
            strncmp(ut_cur.ut_line, ut->ut_line, UT_LINESIZE) == 0) {
            return &ut_cur;
        }
    }
}

struct utmp *pututline(const struct utmp *ut) {
    if (!ut || ut_open() < 0) return NULL;
    /* Walk from the start looking for a slot to overwrite. */
    if (lseek(ut_fd, 0, SEEK_SET) < 0) return NULL;

    off_t found_at = (off_t)-1;
    struct utmp probe;
    for (;;) {
        off_t pos = lseek(ut_fd, 0, SEEK_CUR);
        ssize_t n = read(ut_fd, &probe, sizeof(probe));
        if (n != (ssize_t)sizeof(probe)) break;
        if (ut_id_match(&probe, ut)) {
            found_at = pos;
            break;
        }
    }

    if (found_at >= 0) {
        if (lseek(ut_fd, found_at, SEEK_SET) < 0) return NULL;
    } else {
        if (lseek(ut_fd, 0, SEEK_END) < 0) return NULL;
    }
    ssize_t w = write(ut_fd, ut, sizeof(*ut));
    if (w != (ssize_t)sizeof(*ut)) return NULL;
    /* Cache so chained scan-then-update callers see the most recent record. */
    memcpy(&ut_cur, ut, sizeof(ut_cur));
    return &ut_cur;
}

void updwtmp(const char *wtmp_file, const struct utmp *ut) {
    if (!wtmp_file || !ut) return;
    int fd = open(wtmp_file, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return;
    (void)write(fd, ut, sizeof(*ut));
    close(fd);
}

void logwtmp(const char *line, const char *name, const char *host) {
    struct utmp ut;
    memset(&ut, 0, sizeof(ut));
    ut.ut_pid  = getpid();
    ut.ut_type = (name && name[0]) ? USER_PROCESS : DEAD_PROCESS;
    if (line) {
        strlcpy(ut.ut_line, line, sizeof(ut.ut_line));
    }
    if (name) {
        strlcpy(ut.ut_user, name, sizeof(ut.ut_user));
    }
    if (host) {
        strlcpy(ut.ut_host, host, sizeof(ut.ut_host));
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ut.ut_tv.tv_sec  = (int32_t)tv.tv_sec;
    ut.ut_tv.tv_usec = (int32_t)tv.tv_usec;
    updwtmp(WTMP_FILE, &ut);
}

/*
 * BSD login(3) / logout(3) — the convenience pair that older Unix code
 * (e.g. libtdecore's KPty) calls directly instead of pututline/updwtmp.
 *
 * login() records a USER_PROCESS entry composed from the caller's
 * partially-filled struct utmp; logout() turns the matching ut_line
 * entry into a DEAD_PROCESS one.  Both keep utmp (current) and wtmp
 * (history) in sync, mirroring logwtmp()'s behaviour.
 */
void login(const struct utmp *ut) {
    if (!ut) return;
    struct utmp e = *ut;
    e.ut_type = USER_PROCESS;
    if (e.ut_pid == 0)
        e.ut_pid = getpid();
    if (e.ut_tv.tv_sec == 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        e.ut_tv.tv_sec  = (int32_t)tv.tv_sec;
        e.ut_tv.tv_usec = (int32_t)tv.tv_usec;
    }
    pututline(&e);
    updwtmp(WTMP_FILE, &e);
    endutent();
}

int logout(const char *line) {
    if (!line) return 0;
    struct utmp key;
    memset(&key, 0, sizeof(key));
    strlcpy(key.ut_line, line, sizeof(key.ut_line));

    setutent();
    struct utmp *found = getutline(&key);
    if (!found) { endutent(); return 0; }

    struct utmp e = *found;
    e.ut_type = DEAD_PROCESS;
    memset(e.ut_user, 0, sizeof(e.ut_user));
    memset(e.ut_host, 0, sizeof(e.ut_host));
    struct timeval tv;
    gettimeofday(&tv, NULL);
    e.ut_tv.tv_sec  = (int32_t)tv.tv_sec;
    e.ut_tv.tv_usec = (int32_t)tv.tv_usec;
    pututline(&e);
    updwtmp(WTMP_FILE, &e);
    endutent();
    return 1;
}
