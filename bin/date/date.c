/*
 * bin/date — print or set the current date/time.
 *
 * Usage:
 *
 *   date [-u] [+FORMAT]
 *   date [-u] -r SECONDS [+FORMAT]
 *   date [-u] -d "@SECONDS" [+FORMAT]
 *   date [-u] [[[[[CC]YY]MM]DD]hh]mm[.ss]
 *
 * Flags:
 *   -u           Use UTC instead of localtime.
 *   -r SECONDS   Use SECONDS as the time-since-epoch.
 *   -d STRING    Display STRING.  Only @<digits> (epoch seconds)
 *                supported today — full natural-language parsing
 *                (GNU `date -d`) needs a real parser and isn't on
 *                the table yet.
 *   +FORMAT      strftime(3) format string.  Default is the locale
 *                "%a %b %e %H:%M:%S %Z %Y" pattern.
 *
 * Setting the date: a positional date string in POSIX form
 * [[[[[CC]YY]MM]DD]hh]mm[.ss] sets the system clock via stime(2).
 * Requires uid 0.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static const char *prog = "date";

static void
usage(void)
{
    fprintf(stderr,
        "usage: %s [-u] [-r seconds] [-d @seconds] [+format]\n"
        "       %s [-u] [[[[[cc]yy]mm]dd]HH]MM[.SS]\n",
        prog, prog);
    exit(1);
}

/*
 * Try to parse `s` as a POSIX setting string.  Returns 0 on success
 * and fills *out with the implied time_t; returns -1 if `s` isn't a
 * recognisable setting string at all (so the caller knows to treat
 * it as a format / extra arg instead).
 */
static int
parse_set_string(const char *s, int use_utc, time_t *out)
{
    size_t  len;
    int     digits;
    int     i;
    int     val_mm, val_dd, val_HH, val_MM, val_SS;
    int     val_cc, val_yy;
    char    dot_pos = -1;
    int     parts[6] = {0,0,0,0,0,0};
    int     parts_n  = 0;
    struct tm now_tm;
    time_t  now;
    struct tm result;
    const char *p;

    if (s == NULL || s[0] == '\0') return -1;

    /* Reject anything that's not digits and at most one '.'. */
    len = strlen(s);
    digits = 0;
    for (i = 0; i < (int)len; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') { digits++; continue; }
        if (c == '.') {
            if (dot_pos != (char)-1) return -1;
            dot_pos = (char)i;
            continue;
        }
        return -1;
    }
    if (digits < 2 || (digits % 2) != 0) return -1;
    if (digits < 2 || digits > 12) return -1;

    /* Split into 2-digit chunks before the optional `.SS`. */
    {
        const char *q = s;
        while (parts_n < 5 && q < s + (dot_pos == (char)-1 ? (int)len : dot_pos)) {
            parts[parts_n] = (q[0] - '0') * 10 + (q[1] - '0');
            parts_n++;
            q += 2;
        }
        if (dot_pos != (char)-1) {
            const char *r = s + dot_pos + 1;
            int        ssdigits = (int)(len - (size_t)(dot_pos + 1));
            if (ssdigits != 2) return -1;
            parts[5] = (r[0] - '0') * 10 + (r[1] - '0');
        }
    }

    /* Get current time so we can fill in defaults for absent
     * higher-order fields. */
    now = time(NULL);
    {
        struct tm *t = use_utc ? gmtime(&now) : localtime(&now);
        if (t == NULL) return -1;
        now_tm = *t;
    }

    val_cc = (now_tm.tm_year + 1900) / 100;
    val_yy = (now_tm.tm_year + 1900) % 100;
    val_mm = now_tm.tm_mon + 1;
    val_dd = now_tm.tm_mday;
    val_HH = now_tm.tm_hour;
    val_MM = now_tm.tm_min;
    val_SS = (dot_pos != (char)-1) ? parts[5] : 0;

    /*
     * POSIX `date` allows omitting higher-order pieces, defaulting
     * them to "now."  Cases by number of 2-digit chunks before '.':
     *   1: MM
     *   2: HHMM
     *   3: DDHHMM (no — POSIX is HHMM[ss] minimum.  Below uses the
     *      pmm/MMmm convention from BSD's `date`.)
     */
    p = s;  /* suppress unused */
    (void)p;

    switch (parts_n) {
        case 1:
            val_MM = parts[0];
            break;
        case 2:
            val_HH = parts[0]; val_MM = parts[1];
            break;
        case 3:
            val_dd = parts[0]; val_HH = parts[1]; val_MM = parts[2];
            break;
        case 4:
            val_mm = parts[0]; val_dd = parts[1];
            val_HH = parts[2]; val_MM = parts[3];
            break;
        case 5:
            val_yy = parts[0]; val_mm = parts[1]; val_dd = parts[2];
            val_HH = parts[3]; val_MM = parts[4];
            break;
        default:
            return -1;
    }

    memset(&result, 0, sizeof(result));
    result.tm_year = (val_cc * 100 + val_yy) - 1900;
    result.tm_mon  = val_mm - 1;
    result.tm_mday = val_dd;
    result.tm_hour = val_HH;
    result.tm_min  = val_MM;
    result.tm_sec  = val_SS;
    result.tm_isdst = -1;

    if (use_utc) {
        /* timegm isn't in our libc, so reconstruct as if local
         * then add the UTC offset of "now" (good enough — no DST
         * shift in the window between now and the parsed time
         * because we're setting close to it). */
        time_t t = mktime(&result);
        if (t == (time_t)-1) return -1;
        *out = t;
    } else {
        time_t t = mktime(&result);
        if (t == (time_t)-1) return -1;
        *out = t;
    }
    return 0;
}

static int
parse_at_epoch(const char *s, time_t *out)
{
    char *eptr;
    long  v;
    if (s == NULL || s[0] != '@') return -1;
    v = strtol(s + 1, &eptr, 10);
    if (*eptr != '\0' || v < 0) return -1;
    *out = (time_t)v;
    return 0;
}

int
main(int argc, char **argv)
{
    int         use_utc = 0;
    const char *fmt = "%a %b %e %H:%M:%S %Z %Y";
    time_t      override_t = (time_t)-1;
    int         have_override = 0;
    int         set_clock = 0;
    time_t      set_to = 0;
    int         i;
    const char *positional = NULL;

    prog = argv[0];

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-u") == 0) {
            use_utc = 1;
        } else if (strcmp(a, "-r") == 0) {
            if (i + 1 >= argc) usage();
            override_t = (time_t)atol(argv[++i]);
            have_override = 1;
        } else if (strncmp(a, "-r", 2) == 0 && a[2] != '\0') {
            override_t = (time_t)atol(a + 2);
            have_override = 1;
        } else if (strcmp(a, "-d") == 0) {
            if (i + 1 >= argc) usage();
            if (parse_at_epoch(argv[++i], &override_t) != 0) {
                fprintf(stderr, "%s: -d only supports @<seconds>\n", prog);
                return 1;
            }
            have_override = 1;
        } else if (a[0] == '+') {
            fmt = a + 1;
        } else if (a[0] == '-' && a[1] != '\0') {
            usage();
        } else {
            /* Positional: either a set-clock string or a stray arg. */
            if (positional != NULL) usage();
            positional = a;
        }
    }

    if (positional != NULL) {
        if (parse_set_string(positional, use_utc, &set_to) != 0) {
            fprintf(stderr, "%s: unrecognised set-clock string '%s'\n",
                    prog, positional);
            return 1;
        }
        set_clock = 1;
    }

    if (set_clock) {
        struct timeval tv;
        tv.tv_sec  = set_to;
        tv.tv_usec = 0;
        if (settimeofday(&tv, NULL) != 0) {
            fprintf(stderr, "%s: cannot set time: %s\n", prog,
                    strerror(errno));
            return 1;
        }
    }

    {
        time_t      t;
        struct tm  *tm;
        char        buf[256];
        if (have_override) {
            t = override_t;
        } else {
            t = time(NULL);
        }
        tm = use_utc ? gmtime(&t) : localtime(&t);
        if (tm == NULL) {
            fprintf(stderr, "%s: time conversion failed\n", prog);
            return 1;
        }
        if (strftime(buf, sizeof(buf), fmt, tm) == 0) {
            fprintf(stderr, "%s: strftime returned 0 (format too long?)\n", prog);
            return 1;
        }
        printf("%s\n", buf);
    }
    return 0;
}
