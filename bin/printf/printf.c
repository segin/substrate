/*
 * printf(1) — POSIX format-and-print.
 *
 *   printf FORMAT [ARGS...]
 *
 * FORMAT supports the escapes \\ \a \b \f \n \r \t \v \e \nnn (octal)
 * and \xHH (hex), and the conversions %d %i %o %u %x %X %c %s %b %%
 * plus the floating conversions %e %E %f %F %g %G %a %A, with
 * width/precision/flags (including `*` taking an argument).  The FORMAT
 * is reused for successive cycles of positional arguments (the POSIX
 * "reuse format" rule), stopping once a full cycle consumes no argument.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cap width/precision so `printf '%999999999d'` cannot demand a gigabyte
 * of padding (PRINTF-03). One MiB is far beyond any real formatting need. */
#define MAX_FIELD 1048576

static const char *prog = "printf";
static int g_rc = 0;

/* Interpret backslash escapes in a FORMAT string. Returns a malloc'd copy
 * the caller frees. */
static char *interp_escapes(const char *s)
{
    size_t len = strlen(s);
    char  *out = malloc(len + 1);
    char  *o;
    if (!out)
        return NULL;
    o = out;
    while (*s) {
        if (*s != '\\') { *o++ = *s++; continue; }
        s++;
        switch (*s) {
        case '\\': *o++ = '\\'; s++; break;
        case 'a':  *o++ = '\a'; s++; break;
        case 'b':  *o++ = '\b'; s++; break;
        case 'e':  *o++ = 27;   s++; break;
        case 'f':  *o++ = '\f'; s++; break;
        case 'n':  *o++ = '\n'; s++; break;
        case 'r':  *o++ = '\r'; s++; break;
        case 't':  *o++ = '\t'; s++; break;
        case 'v':  *o++ = '\v'; s++; break;
        case 'x': {
            int v = 0, n = 0;
            s++;
            while (n < 2 && isxdigit((unsigned char)*s)) {
                char c = *s;
                int d = (c <= '9') ? c - '0'
                      : (tolower((unsigned char)c) - 'a' + 10);
                v = (v * 16) + d; s++; n++;
            }
            if (n == 0) { *o++ = '\\'; *o++ = 'x'; }
            else        { *o++ = (char)v; }
            break;
        }
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': {
            int v = 0, n = 0;
            while (n < 3 && *s >= '0' && *s <= '7') {
                v = (v * 8) + (*s - '0'); s++; n++;
            }
            *o++ = (char)v;
            break;
        }
        case '\0': *o++ = '\\'; break;
        default:   *o++ = '\\'; *o++ = *s++; break;
        }
    }
    *o = '\0';
    return out;
}

/*
 * Interpret escapes for a %b argument: the same set as FORMAT but octal is
 * written \0ooo, and \c stops all further output.  Writes into out (sized
 * >= strlen(s)+1); sets *stop if \c was seen; returns the byte count.
 */
static size_t interp_b(const char *s, char *out, int *stop)
{
    char *o = out;
    *stop = 0;
    while (*s) {
        if (*s != '\\') { *o++ = *s++; continue; }
        s++;
        switch (*s) {
        case '\\': *o++ = '\\'; s++; break;
        case 'a':  *o++ = '\a'; s++; break;
        case 'b':  *o++ = '\b'; s++; break;
        case 'f':  *o++ = '\f'; s++; break;
        case 'n':  *o++ = '\n'; s++; break;
        case 'r':  *o++ = '\r'; s++; break;
        case 't':  *o++ = '\t'; s++; break;
        case 'v':  *o++ = '\v'; s++; break;
        case 'c':  *stop = 1; return (size_t)(o - out);
        case '0': {
            int v = 0, n = 0;
            s++;
            while (n < 3 && *s >= '0' && *s <= '7') {
                v = (v * 8) + (*s - '0'); s++; n++;
            }
            *o++ = (char)v;
            break;
        }
        case '\0': *o++ = '\\'; break;
        default:   *o++ = '\\'; *o++ = *s++; break;
        }
    }
    return (size_t)(o - out);
}

/* Parse a signed integer argument, honoring the POSIX `'c`/`"c` char-code
 * form and diagnosing trailing garbage / overflow (PRINTF-04). */
static long parse_signed(const char *arg)
{
    char *end;
    long  v;
    if (!arg || !*arg)
        return 0;
    if (arg[0] == '\'' || arg[0] == '"') {
        if (arg[2] != '\0')
            fprintf(stderr, "%s: warning: extra characters after '%c'\n",
                prog, arg[1]);
        return (unsigned char)arg[1];
    }
    errno = 0;
    v = strtol(arg, &end, 0);
    if (end == arg || *end != '\0') {
        fprintf(stderr, "%s: %s: expected a numeric value\n", prog, arg);
        g_rc = 1;
    } else if (errno == ERANGE) {
        fprintf(stderr, "%s: %s: value out of range\n", prog, arg);
        g_rc = 1;
    }
    return v;
}

/* Parse an unsigned integer argument (same char-code + error handling). */
static unsigned long parse_unsigned(const char *arg)
{
    char         *end;
    unsigned long v;
    if (!arg || !*arg)
        return 0;
    if (arg[0] == '\'' || arg[0] == '"') {
        if (arg[2] != '\0')
            fprintf(stderr, "%s: warning: extra characters after '%c'\n",
                prog, arg[1]);
        return (unsigned char)arg[1];
    }
    errno = 0;
    v = strtoul(arg, &end, 0);
    if (end == arg || *end != '\0') {
        fprintf(stderr, "%s: %s: expected a numeric value\n", prog, arg);
        g_rc = 1;
    } else if (errno == ERANGE) {
        fprintf(stderr, "%s: %s: value out of range\n", prog, arg);
        g_rc = 1;
    }
    return v;
}

/* Build a printf spec string from parsed components into buf (sized bufsz).
 * `lmod` is a length modifier to inject before the conversion ("" or "l"). */
static void build_spec(char *buf, size_t bufsz, const char *flags,
    int width, int prec, const char *lmod, char conv)
{
    size_t k = 0;
    int    i;
    if (bufsz == 0)
        return;
    buf[k++] = '%';
    for (i = 0; flags[i] && k < bufsz - 1; i++)
        buf[k++] = flags[i];
    if (width >= 0 && k < bufsz - 12)
        k += (size_t)snprintf(buf + k, bufsz - k, "%d", width);
    if (prec >= 0 && k < bufsz - 12) {
        buf[k++] = '.';
        k += (size_t)snprintf(buf + k, bufsz - k, "%d", prec);
    }
    for (i = 0; lmod[i] && k < bufsz - 2; i++)
        buf[k++] = lmod[i];
    if (k < bufsz - 1)
        buf[k++] = conv;
    buf[k] = '\0';
}

int main(int argc, char **argv)
{
    char *fmt;
    int   argi;

    if (argv[0] && argv[0][0])
        prog = argv[0];
    if (argc < 2) {
        fprintf(stderr, "usage: printf FORMAT [ARGS...]\n");
        return 1;
    }
    fmt = interp_escapes(argv[1]);
    if (!fmt)
        return 1;

    argi = 2;

    do {
        const char *p = fmt;
        int         argi_start = argi;

        while (*p) {
            char  flags[8];
            char  spec[64];
            int   width = -1, prec = -1;
            size_t fi = 0;
            char  conv;

            if (*p != '%') { putchar(*p++); continue; }
            p++;                              /* past '%' */

            if (*p == '%') { putchar('%'); p++; continue; }

            /* flags */
            while (*p && strchr("-+ #0", *p) && fi < sizeof(flags) - 1)
                flags[fi++] = *p++;
            flags[fi] = '\0';

            /* width: number or '*' (argument) */
            if (*p == '*') {
                long w = parse_signed(argi < argc ? argv[argi++] : NULL);
                p++;
                if (w < 0) {                  /* negative width => left-align */
                    if (fi < sizeof(flags) - 1) { flags[fi++] = '-'; flags[fi] = '\0'; }
                    w = -w;
                }
                width = (w > MAX_FIELD) ? MAX_FIELD : (int)w;
            } else if (isdigit((unsigned char)*p)) {
                long w = 0;
                while (isdigit((unsigned char)*p)) {
                    w = w * 10 + (*p++ - '0');
                    if (w > MAX_FIELD) w = MAX_FIELD;   /* clamp, don't overflow */
                }
                width = (int)w;
            }

            /* precision: '.' then number or '*' */
            if (*p == '.') {
                p++;
                if (*p == '*') {
                    long pr = parse_signed(argi < argc ? argv[argi++] : NULL);
                    p++;
                    prec = (pr < 0) ? 0 : (pr > MAX_FIELD ? MAX_FIELD : (int)pr);
                } else {
                    long pr = 0;
                    while (isdigit((unsigned char)*p)) {
                        pr = pr * 10 + (*p++ - '0');
                        if (pr > MAX_FIELD) pr = MAX_FIELD;
                    }
                    prec = (int)pr;
                }
            }

            if (!*p) {                        /* trailing '%' with no conv */
                fputc('%', stdout);
                break;
            }
            conv = *p++;

            switch (conv) {
            case 'd': case 'i': {
                const char *a = (argi < argc) ? argv[argi++] : NULL;
                build_spec(spec, sizeof(spec), flags, width, prec, "l", conv);
                printf(spec, parse_signed(a));
                break;
            }
            case 'o': case 'u': case 'x': case 'X': {
                const char *a = (argi < argc) ? argv[argi++] : NULL;
                build_spec(spec, sizeof(spec), flags, width, prec, "l", conv);
                printf(spec, parse_unsigned(a));
                break;
            }
            case 'e': case 'E': case 'f': case 'F':
            case 'g': case 'G': case 'a': case 'A': {
                const char *a = (argi < argc) ? argv[argi++] : NULL;
                char *end;
                double d;
                errno = 0;
                d = a ? strtod(a, &end) : 0.0;
                if (a && (*a == '\0' || *end != '\0')) {
                    fprintf(stderr, "%s: %s: expected a numeric value\n", prog, a);
                    g_rc = 1;
                }
                build_spec(spec, sizeof(spec), flags, width, prec, "", conv);
                printf(spec, d);
                break;
            }
            case 'c': {
                const char *a = (argi < argc) ? argv[argi++] : NULL;
                build_spec(spec, sizeof(spec), flags, width, -1, "", 'c');
                if (a && a[0])
                    printf(spec, a[0]);
                break;
            }
            case 's': {
                const char *a = (argi < argc) ? argv[argi++] : NULL;
                build_spec(spec, sizeof(spec), flags, width, prec, "", 's');
                printf(spec, a ? a : "");
                break;
            }
            case 'b': {
                const char *a = (argi < argc) ? argv[argi++] : "";
                char  *tmp = malloc(strlen(a) + 1);
                int    stop;
                size_t n;
                if (!tmp) { g_rc = 1; break; }
                n = interp_b(a, tmp, &stop);
                /* Apply precision as a byte limit, then pad to width. */
                if (prec >= 0 && (size_t)prec < n)
                    n = (size_t)prec;
                if (width > 0 && (size_t)width > n &&
                    !strchr(flags, '-')) {
                    int pad = width - (int)n;
                    while (pad-- > 0) putchar(' ');
                }
                fwrite(tmp, 1, n, stdout);
                if (width > 0 && (size_t)width > n &&
                    strchr(flags, '-')) {
                    int pad = width - (int)n;
                    while (pad-- > 0) putchar(' ');
                }
                free(tmp);
                if (stop) { free(fmt); return g_rc; }
                break;
            }
            default:
                fprintf(stderr, "%s: %%%c: invalid conversion\n", prog, conv);
                g_rc = 1;
                break;
            }
        }

        /* Stop reusing the format once a full cycle consumed no argument,
         * otherwise `printf hello x` would loop forever (PRINTF-01). */
        if (argi == argi_start)
            break;
    } while (argi < argc);

    free(fmt);
    return g_rc;
}
