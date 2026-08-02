/*
 * touch — change file timestamps.  Substrate baseline used to be
 * an open-then-close stub that didn't honor any time-setting
 * options; this version implements POSIX `touch [-acm] [-d
 * date] [-r reffile] [-t time] file...` enough that ports relying
 * on -d @epoch or -r ref work.
 *
 * Supported flags:
 *   -a               update access time only
 *   -m               update modification time only
 *   -c               do not create missing files
 *   -h               affect symlink itself, not target (AT_SYMLINK_NOFOLLOW)
 *   -d <date>        set time to this date string; supports
 *                      `@<epoch>`     (seconds since 1970)
 *                      `YYYY-MM-DD`   (midnight UTC that day)
 *                      `YYYY-MM-DDTHH:MM:SS`  (UTC)
 *   -r <ref>         copy times from `ref` file
 *   -t [[CC]YY]MMDDhhmm[.SS]
 *
 * Default (no -d/-r/-t): set both atime and mtime to current time
 * (via utimensat UTIME_NOW on each field).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "getopt.h"
#include <sys/stat.h>
#include <sys/time.h>

#ifndef UTIME_NOW
#define UTIME_NOW  ((1L << 30) - 1L)
#endif
#ifndef UTIME_OMIT
#define UTIME_OMIT ((1L << 30) - 2L)
#endif

/* Validate broken-down fields and convert to an epoch timespec.  Uses
 * mktime(), so the fields are interpreted in the system's local time
 * (POSIX touch semantics), not forced to UTC (TOUCH-04). */
static int fields_to_ts(int Y, int M, int D, int h, int m, int sec,
                        long nsec, struct timespec *out) {
    /* Range-validate before any arithmetic (TOUCH-02/TOUCH-04). */
    if (M < 1 || M > 12 || D < 1 || D > 31 ||
        h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 61)
        return -1;

    struct tm tmv;
    memset(&tmv, 0, sizeof tmv);
    tmv.tm_year = Y - 1900;
    tmv.tm_mon  = M - 1;
    tmv.tm_mday = D;
    tmv.tm_hour = h;
    tmv.tm_min  = m;
    tmv.tm_sec  = sec;
    tmv.tm_isdst = -1;

    time_t t = mktime(&tmv);
    if (t == (time_t)-1)
        return -1;
    out->tv_sec  = t;
    out->tv_nsec = nsec;
    return 0;
}

static int parse_date(const char *s, struct timespec *out) {
    if (!s || !*s) return -1;
    out->tv_nsec = 0;
    /* @<epoch> — absolute seconds since 1970 (TOUCH-01: check endptr). */
    if (s[0] == '@') {
        char *end;
        errno = 0;
        long long v = strtoll(s + 1, &end, 10);
        if (end == s + 1 || *end != '\0' || errno == ERANGE) return -1;
        out->tv_sec = (time_t)v;
        return 0;
    }
    /* YYYY-MM-DD [T|space HH:MM:SS] — local time. */
    int Y, M, D, h = 0, m = 0, sec = 0;
    int n = sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec);
    if (n < 3) {
        n = sscanf(s, "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &sec);
        if (n < 3) return -1;                 /* need at least Y-M-D (TOUCH-01) */
    }
    return fields_to_ts(Y, M, D, h, m, sec, 0, out);
}

static int parse_t(const char *s, struct timespec *out) {
    char buf[16];
    size_t len = strlen(s);
    if (len < 8) return -1;
    int ss = 0;
    const char *dot = strchr(s, '.');
    if (dot) {
        if (dot - s != (long)len - 3) return -1;
        /* Validate the two seconds digits (TOUCH-04: ss unchecked). */
        if (dot[1] < '0' || dot[1] > '9' || dot[2] < '0' || dot[2] > '9')
            return -1;
        ss = (dot[1] - '0') * 10 + (dot[2] - '0');
        len -= 3;
    }
    if (len > sizeof(buf) - 1) return -1;
    memcpy(buf, s, len); buf[len] = '\0';
    int Y, M, D, h, m, nread;
    if (len == 8) {
        time_t now = time(NULL); struct tm tmv;
        if (!localtime_r(&now, &tmv)) return -1;
        Y = tmv.tm_year + 1900;
        nread = sscanf(buf, "%2d%2d%2d%2d", &M, &D, &h, &m);
        if (nread != 4) return -1;            /* TOUCH-01: reject partial */
    } else if (len == 10) {
        int YY;
        nread = sscanf(buf, "%2d%2d%2d%2d%2d", &YY, &M, &D, &h, &m);
        if (nread != 5) return -1;
        Y = (YY < 69 ? 2000 + YY : 1900 + YY);
    } else if (len == 12) {
        nread = sscanf(buf, "%4d%2d%2d%2d%2d", &Y, &M, &D, &h, &m);
        if (nread != 5) return -1;
    } else return -1;
    return fields_to_ts(Y, M, D, h, m, ss, 0, out);
}

int main(int argc, char **argv) {
    int a_only = 0, m_only = 0, no_create = 0, no_follow = 0;
    const char *date_str = NULL, *ref = NULL, *time_arg = NULL;
    int ch;
    while ((ch = getopt(argc, argv, "acmhd:r:t:")) != -1) {
        switch (ch) {
        case 'a': a_only = 1; break;
        case 'm': m_only = 1; break;
        case 'c': no_create = 1; break;
        case 'h': no_follow = 1; break;
        case 'd': date_str = optarg; break;
        case 'r': ref = optarg; break;
        case 't': time_arg = optarg; break;
        default:
            fprintf(stderr, "usage: touch [-acm] [-d date | -r ref | -t time] file...\n");
            return 1;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "touch: missing file operand\n"); return 1;
    }

    struct timespec ts[2];
    ts[0].tv_sec = 0; ts[0].tv_nsec = UTIME_NOW;
    ts[1].tv_sec = 0; ts[1].tv_nsec = UTIME_NOW;

    if (ref) {
        struct stat st;
        /* With -h, take the times of the symlink itself (TOUCH-04). */
        int r = no_follow ? lstat(ref, &st) : stat(ref, &st);
        if (r != 0) { perror(ref); return 1; }
        /* Preserve sub-second resolution (TOUCH-04: don't drop nsec). */
        ts[0].tv_sec = st.st_atime; ts[0].tv_nsec = st.st_atime_nsec;
        ts[1].tv_sec = st.st_mtime; ts[1].tv_nsec = st.st_mtime_nsec;
    } else if (date_str) {
        struct timespec t;
        if (parse_date(date_str, &t) != 0) {
            fprintf(stderr, "touch: bad date '%s'\n", date_str); return 1;
        }
        ts[0] = ts[1] = t;
    } else if (time_arg) {
        struct timespec t;
        if (parse_t(time_arg, &t) != 0) {
            fprintf(stderr, "touch: bad time '%s'\n", time_arg); return 1;
        }
        ts[0] = ts[1] = t;
    }

    if (a_only && !m_only) ts[1].tv_nsec = UTIME_OMIT;
    if (m_only && !a_only) ts[0].tv_nsec = UTIME_OMIT;

    int rc_all = 0;
    for (int i = optind; i < argc; i++) {
        if (!no_create && !no_follow) {
            /* Create the file if it doesn't exist.  O_CREAT without
             * O_EXCL never yields EEXIST, so the old EEXIST test was
             * dead (TOUCH-04); the real "already exists" cases are a
             * successful open (regular file) or EISDIR (a directory) —
             * both mean "exists, just set its times" (TOUCH-03).  With
             * -h we skip the create probe entirely: touch can't create
             * a symlink, and O_WRONLY would follow one to its target. */
            int fd = open(argv[i], O_WRONLY | O_CREAT, 0666);
            if (fd >= 0) {
                close(fd);
            } else if (errno != EISDIR) {
                perror(argv[i]); rc_all = 1; continue;
            }
        }
        int flags = no_follow ? AT_SYMLINK_NOFOLLOW : 0;
        if (utimensat(AT_FDCWD, argv[i], ts, flags) != 0) {
            perror(argv[i]); rc_all = 1;
        }
    }
    return rc_all;
}
