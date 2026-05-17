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
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef UTIME_NOW
#define UTIME_NOW  ((1L << 30) - 1L)
#endif
#ifndef UTIME_OMIT
#define UTIME_OMIT ((1L << 30) - 2L)
#endif

static int parse_date(const char *s, struct timespec *out) {
    if (!s || !*s) return -1;
    out->tv_nsec = 0;
    /* @<epoch> */
    if (s[0] == '@') {
        char *end;
        long long v = strtoll(s + 1, &end, 10);
        if (end == s + 1 || *end != '\0') return -1;
        out->tv_sec = (time_t)v;
        return 0;
    }
    /* YYYY-MM-DD [T HH:MM:SS] — UTC.  */
    int Y, M, D, h = 0, m = 0, sec = 0;
    int n = sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec);
    if (n < 3) {
        n = sscanf(s, "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &sec);
        if (n < 3) return -1;
    }
    /* civil_from_fields → epoch (Howard Hinnant).  */
    Y -= (M <= 2);
    long era = (Y >= 0 ? Y : Y - 399) / 400;
    unsigned yoe = (unsigned)(Y - era * 400);
    unsigned doy = (153 * (unsigned)(M > 2 ? M - 3 : M + 9) + 2) / 5 + (unsigned)D - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = era * 146097LL + (long long)doe - 719468LL;
    long long secs = days * 86400LL + h * 3600LL + m * 60LL + sec;
    out->tv_sec = (time_t)secs;
    return 0;
}

static int parse_t(const char *s, struct timespec *out) {
    char buf[16];
    size_t len = strlen(s);
    if (len < 8) return -1;
    int ss = 0;
    const char *dot = strchr(s, '.');
    if (dot) {
        if (dot - s != (long)len - 3) return -1;
        ss = atoi(dot + 1);
        len -= 3;
    }
    if (len > sizeof(buf) - 1) return -1;
    memcpy(buf, s, len); buf[len] = '\0';
    int Y, M, D, h, m;
    if (len == 8) {
        time_t now = time(NULL); struct tm tmv;
        if (!gmtime_r(&now, &tmv)) return -1;
        Y = tmv.tm_year + 1900;
        sscanf(buf, "%2d%2d%2d%2d", &M, &D, &h, &m);
    } else if (len == 10) {
        int YY;
        sscanf(buf, "%2d%2d%2d%2d%2d", &YY, &M, &D, &h, &m);
        Y = (YY < 69 ? 2000 + YY : 1900 + YY);
    } else if (len == 12) {
        sscanf(buf, "%4d%2d%2d%2d%2d", &Y, &M, &D, &h, &m);
    } else return -1;
    char iso[24];
    snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d", Y, M, D, h, m, ss);
    return parse_date(iso, out);
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
        if (stat(ref, &st) != 0) { perror(ref); return 1; }
        ts[0].tv_sec = st.st_atime; ts[0].tv_nsec = 0;
        ts[1].tv_sec = st.st_mtime; ts[1].tv_nsec = 0;
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
        if (!no_create) {
            int fd = open(argv[i], O_WRONLY | O_CREAT, 0666);
            if (fd >= 0) close(fd);
            else if (errno != EEXIST) {
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
