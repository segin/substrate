/*
 * seq - print a sequence of numbers.
 *
 *   seq [-w] [-f format] [-s string] [first [incr]] last
 *
 * Prints first, first+incr, ... up to last (down to it for a negative
 * incr), one per line or separated by -s's string.  first and incr
 * default to 1.  Numbers print with as many decimal places as first and
 * incr have, or through -f's printf format (one e, f or g conversion);
 * -w pads them with leading zeros to a common width.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
    fprintf(stderr,
        "usage: seq [-w] [-f format] [-s string] [first [incr]] last\n");
    exit(1);
}

static double
number(const char *s)
{
    char *end;
    double v;

    errno = 0;
    v = strtod(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE || !isfinite(v)) {
        fprintf(stderr, "seq: invalid number '%s'\n", s);
        exit(1);
    }
    return v;
}

/* Digits after the decimal point in the way s was written ("1.50" -> 2). */
static int
decimals(const char *s)
{
    const char *dot = strchr(s, '.');
    int n = 0;

    if (dot == NULL || strpbrk(s, "eE") != NULL)
        return 0;
    for (dot++; *dot >= '0' && *dot <= '9'; dot++)
        n++;
    return n;
}

/*
 * A -f format must contain exactly one floating-point conversion (%e %E %f
 * %F %g %G, with optional flags, width and precision) and otherwise only
 * "%%".  Anything else would make printf read an argument we do not pass.
 */
static int
valid_format(const char *f)
{
    int convs = 0;

    for (; *f; f++) {
        if (*f != '%')
            continue;
        if (f[1] == '%') {
            f++;
            continue;
        }
        f++;
        f += strspn(f, "-+ #0'");
        f += strspn(f, "0123456789");
        if (*f == '.') {
            f++;
            f += strspn(f, "0123456789");
        }
        if (*f == '\0' || strchr("eEfFgG", *f) == NULL)
            return 0;
        convs++;
    }
    return convs == 1;
}

static void
put(const char *fmt, double v, int width)
{
    char buf[512];

    snprintf(buf, sizeof(buf), fmt, v);
    if (width > 0) {
        /* -w: zero-pad after any minus sign. */
        int len = (int)strlen(buf), neg = buf[0] == '-';

        if (neg)
            putchar('-');
        for (int i = len; i < width; i++)
            putchar('0');
        fputs(buf + neg, stdout);
    } else {
        fputs(buf, stdout);
    }
}

int
main(int argc, char *argv[])
{
    const char *fmt = NULL, *sep = "\n";
    char deffmt[32];
    double first = 1, incr = 1, last;
    int equal_width = 0, c, prec;

    /*
     * Options by hand rather than getopt(): an argument that looks like a
     * negative number ("-3", "-.5") is an operand, as in GNU and BSD seq.
     */
    int ai = 1;
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1] != '\0') {
        const char *a = argv[ai];

        if (strcmp(a, "--") == 0) {
            ai++;
            break;
        }
        if ((a[1] >= '0' && a[1] <= '9') || a[1] == '.')
            break;
        for (int k = 1; a[k] != '\0'; k++) {
            c = a[k];
            if (c == 'w') {
                equal_width = 1;
            } else if (c == 'f' || c == 's') {
                const char *val = a[k + 1] != '\0' ? a + k + 1 : argv[++ai];

                if (val == NULL)
                    usage();
                if (c == 'f')
                    fmt = val;
                else
                    sep = val;
                break;
            } else {
                fprintf(stderr, "seq: invalid option '-%c'\n", c);
                usage();
            }
        }
        ai++;
    }
    argc -= ai;
    argv += ai;

    switch (argc) {
    case 1:
        last = number(argv[0]);
        prec = 0;
        break;
    case 2:
        first = number(argv[0]);
        last = number(argv[1]);
        prec = decimals(argv[0]);
        break;
    case 3:
        first = number(argv[0]);
        incr = number(argv[1]);
        last = number(argv[2]);
        prec = decimals(argv[0]) > decimals(argv[1]) ?
               decimals(argv[0]) : decimals(argv[1]);
        break;
    default:
        usage();
    }
    if (incr == 0) {
        fprintf(stderr, "seq: increment must not be zero\n");
        return 1;
    }

    if (fmt != NULL) {
        if (!valid_format(fmt)) {
            fprintf(stderr, "seq: invalid format '%s'\n", fmt);
            return 1;
        }
    } else {
        snprintf(deffmt, sizeof(deffmt), "%%.%df", prec);
        fmt = deffmt;
    }

    int width = 0;
    if (equal_width) {
        char a[512], b[512];

        snprintf(a, sizeof(a), fmt, first);
        snprintf(b, sizeof(b), fmt, last);
        width = (int)(strlen(a) > strlen(b) ? strlen(a) : strlen(b));
    }

    /* Allow for rounding in the last step: 0.1 + 0.1 + 0.1 != 0.3. */
    double slack = fabs(incr) * 1e-10;
    int printed = 0;
    for (long i = 0;; i++) {
        double v = first + (double)i * incr;

        if (incr > 0 ? v > last + slack : v < last - slack)
            break;
        if (printed)
            fputs(sep, stdout);
        put(fmt, v, width);
        printed = 1;
    }
    if (printed)
        putchar('\n');
    return fflush(stdout) == 0 ? 0 : 1;
}
