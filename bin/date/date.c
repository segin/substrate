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
#include <time.h>
#include <unistd.h>

#include <sys/time.h>

static const char *prog = "date";

/*
 * timegm(3) isn't in our libc, so convert a broken-down UTC time to
 * epoch seconds directly (Howard Hinnant's days_from_civil).  Unlike
 * mktime(), this does not apply the local timezone offset (DATE-02).
 */
static time_t
tm_to_utc(const struct tm *tm)
{
    int      Y = tm->tm_year + 1900;
    int      M = tm->tm_mon + 1;
    int      D = tm->tm_mday;
    Y -= (M <= 2);
    long     era = (Y >= 0 ? Y : Y - 399) / 400;
    unsigned yoe = (unsigned)(Y - era * 400);
    unsigned doy = (153 * (unsigned)(M > 2 ? M - 3 : M + 9) + 2) / 5 + (unsigned)D - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = era * 146097LL + (long long)doe - 719468LL;
    return (time_t)(days * 86400LL + tm->tm_hour * 3600LL +
                    tm->tm_min * 60LL + tm->tm_sec);
}

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
    int     dot_pos = -1;         /* string index; int so it can't alias
                                   * the (char)-1 sentinel (DATE-07) */
    int     cc_given = 0, yy_given = 0;
    int     ss_val = 0;
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
            if (dot_pos != -1) return -1;
            dot_pos = i;
            continue;
        }
        return -1;
    }
    (void)digits;

    /* Digits before the optional '.SS' must form 1..6 two-digit chunks
     * (mm up to CCYYMMDDhhmm); SS is exactly two digits (DATE-01). */
    {
        int main_digits = (dot_pos == -1) ? (int)len : dot_pos;
        if (main_digits < 2 || main_digits > 12 || (main_digits % 2) != 0)
            return -1;
    }

    /* Split into 2-digit chunks before the optional `.SS`.  Allow the
     * full six chunks (CCYYMMDDhhmm), not five (DATE-01). */
    {
        const char *q = s;
        while (parts_n < 6 && q < s + (dot_pos == -1 ? (int)len : dot_pos)) {
            parts[parts_n] = (q[0] - '0') * 10 + (q[1] - '0');
            parts_n++;
            q += 2;
        }
        if (dot_pos != -1) {
            const char *r = s + dot_pos + 1;
            int        ssdigits = (int)(len - (size_t)(dot_pos + 1));
            if (ssdigits != 2) return -1;
            ss_val = (r[0] - '0') * 10 + (r[1] - '0');
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
    val_SS = (dot_pos != -1) ? ss_val : 0;

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
            yy_given = 1;
            break;
        case 6:                                     /* CCYYMMDDhhmm (DATE-01) */
            val_cc = parts[0]; val_yy = parts[1]; val_mm = parts[2];
            val_dd = parts[3]; val_HH = parts[4]; val_MM = parts[5];
            cc_given = 1; yy_given = 1;
            break;
        default:
            return -1;
    }

    /* Century pivot when a 2-digit year is given without a century:
     * 69..99 -> 1969..1999, 00..68 -> 2000..2068 (DATE-06). */
    if (yy_given && !cc_given)
        val_cc = (val_yy < 69) ? 20 : 19;

    /* Range-validate every field before the conversion so a bogus
     * setting string ('date 13322500') is rejected, not silently
     * normalised into the system clock as root (DATE-04). */
    if (val_mm < 1 || val_mm > 12 || val_dd < 1 || val_dd > 31 ||
        val_HH < 0 || val_HH > 23 || val_MM < 0 || val_MM > 59 ||
        val_SS < 0 || val_SS > 61)
        return -1;

    memset(&result, 0, sizeof(result));
    result.tm_year = (val_cc * 100 + val_yy) - 1900;
    result.tm_mon  = val_mm - 1;
    result.tm_mday = val_dd;
    result.tm_hour = val_HH;
    result.tm_min  = val_MM;
    result.tm_sec  = val_SS;
    result.tm_isdst = -1;

    if (use_utc) {
        /* -u interprets the fields as UTC; use the timegm-equivalent so
         * the clock isn't skewed by the local TZ offset (DATE-02). */
        *out = tm_to_utc(&result);
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
    char      *eptr;
    long long  v;
    if (s == NULL || s[0] != '@') return -1;
    errno = 0;
    /* time_t is 64-bit on Substrate; parse with strtoll so a value past
     * LONG_MAX isn't truncated, and reject overflow (DATE-03). */
    v = strtoll(s + 1, &eptr, 10);
    if (eptr == s + 1 || *eptr != '\0' || errno == ERANGE) return -1;
    *out = (time_t)v;
    return 0;
}

/* Parse a bare seconds-since-epoch operand for -r (DATE-03/08). */
static int
parse_seconds(const char *s, time_t *out)
{
    char      *eptr;
    long long  v;
    if (s == NULL || s[0] == '\0') return -1;
    errno = 0;
    v = strtoll(s, &eptr, 10);
    if (eptr == s || *eptr != '\0' || errno == ERANGE) return -1;
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
            if (parse_seconds(argv[++i], &override_t) != 0) {
                fprintf(stderr, "%s: invalid seconds '%s'\n", prog, argv[i]);
                return 1;
            }
            have_override = 1;
        } else if (strncmp(a, "-r", 2) == 0 && a[2] != '\0') {
            if (parse_seconds(a + 2, &override_t) != 0) {
                fprintf(stderr, "%s: invalid seconds '%s'\n", prog, a + 2);
                return 1;
            }
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
        /* stime(2) is the simplest clock-setter wired into native
         * dispatch.  settimeofday is declared in <sys/time.h> but
         * not implemented in libc yet. */
        if (stime(&set_to) != 0) {
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
        /* strftime returns 0 both when the output is legitimately empty
         * (date +'') and when it overflows buf.  An empty format can't
         * overflow, so only treat 0 as an error for a non-empty format
         * (DATE-05). */
        size_t n = strftime(buf, sizeof(buf), fmt, tm);
        if (n == 0 && fmt[0] != '\0') {
            fprintf(stderr, "%s: format result too long\n", prog);
            return 1;
        }
        buf[n] = '\0';
        printf("%s\n", buf);
    }
    return 0;
}
