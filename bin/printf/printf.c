/*
 * printf(1) — POSIX format-and-print.
 *
 *   printf FORMAT [ARGS...]
 *
 * FORMAT supports: \\ \a \b \f \n \r \t \v \nnn (octal),
 * and the standard %d %i %o %u %x %X %c %s %% conversions plus
 * width/precision/flags.  Loops the format string once per cycle of
 * positional arguments (the POSIX "reuse format" rule).
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *interp_escapes(const char *s)
{
    size_t len = strlen(s);
    char  *out = malloc(len + 1);
    if (!out) return NULL;
    char *o = out;
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

static int emit_one(const char *spec, const char *arg)
{
    /* spec is something like "%-10.5d" — printf it directly with the
     * type from the trailing conversion char. */
    char conv = spec[strlen(spec) - 1];
    switch (conv) {
    case 'd': case 'i': {
        long v = arg ? strtol(arg, NULL, 0) : 0;
        printf(spec, v);
        return 0;
    }
    case 'o': case 'u': case 'x': case 'X': {
        unsigned long v = arg ? strtoul(arg, NULL, 0) : 0;
        printf(spec, v);
        return 0;
    }
    case 'c':
        printf(spec, arg && arg[0] ? arg[0] : 0);
        return 0;
    case 's':
        printf(spec, arg ? arg : "");
        return 0;
    case '%':
        fputs("%", stdout);
        return 0;
    default:
        fprintf(stderr, "printf: %%%c: invalid conversion\n", conv);
        return 1;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: printf FORMAT [ARGS...]\n");
        return 1;
    }
    char *fmt = interp_escapes(argv[1]);
    if (!fmt) return 1;

    int argi = 2;
    int rc = 0;

    do {
        const char *p = fmt;
        int started_cycle = (argi < argc);
        while (*p) {
            if (*p != '%') { putchar(*p++); continue; }
            /* Parse one spec %[flags][width][.precision]conv */
            char spec[32];
            size_t si = 0;
            spec[si++] = *p++;            /* '%' */
            while (*p && strchr("-+ #0", *p) && si < sizeof(spec) - 2) {
                spec[si++] = *p++;
            }
            while (*p && isdigit((unsigned char)*p) && si < sizeof(spec) - 2) {
                spec[si++] = *p++;
            }
            if (*p == '.') {
                spec[si++] = *p++;
                while (*p && isdigit((unsigned char)*p) && si < sizeof(spec) - 2) {
                    spec[si++] = *p++;
                }
            }
            if (!*p) break;
            spec[si++] = *p;
            spec[si]   = '\0';
            char conv = *p++;
            if (conv == '%') {
                fputs("%", stdout);
                continue;
            }
            const char *a = (argi < argc) ? argv[argi++] : NULL;
            spec[si - 1] = conv;   /* normalize */
            if (emit_one(spec, a) != 0) rc = 1;
        }
        if (!started_cycle) break;
    } while (argi < argc);

    free(fmt);
    return rc;
}
