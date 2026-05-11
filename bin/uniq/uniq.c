/*
 * bin/uniq — POSIX/BSD/GNU uniq.
 *
 * Filter adjacent matching lines.  Input is expected to be sorted
 * (otherwise duplicates that aren't adjacent won't be merged —
 * that's the contract).
 *
 * POSIX options:
 *   -c   Prefix each output line with the count of repetitions.
 *   -d   Only output lines that ARE repeated.
 *   -u   Only output lines that are NOT repeated.
 *   -f N Skip the first N fields when comparing.
 *   -s N Skip the first N chars (after -f's skip) when comparing.
 *
 * BSD additions:
 *   -i   Case-insensitive compare.
 *
 * GNU additions:
 *   -D   Print every copy of duplicate lines (not just one per group).
 *   -w N Compare at most N chars (after the skips).
 *   -z   Lines are NUL-terminated instead of newline.
 *   --all-repeated[=METHOD]  Equivalent to -D with optional METHOD
 *                            ("none" / "prepend" / "append" / "separate")
 *                            controlling group separators.
 *
 * Precedence when flags conflict: POSIX > BSD > GNU.
 *
 * Usage:  uniq [-cdiuDz] [-f N] [-s N] [-w N] [input [output]]
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *progname = "uniq";

typedef struct uniq_opts {
    int count;
    int only_dup;
    int only_uniq;
    int print_all_dup;
    int icase;
    int skip_fields;
    int skip_chars;
    int width;
    int z_terminated;
    enum { GS_NONE = 0, GS_PREPEND, GS_APPEND, GS_SEPARATE } group_sep;
} uniq_opts_t;

static void
usage(void)
{
    fprintf(stderr,
        "usage: %s [-cdiuDz] [-f num] [-s num] [-w num] "
        "[--all-repeated[=METHOD]] [input [output]]\n",
        progname);
    exit(2);
}

static const char *
skip_n_fields(const char *s, int n)
{
    int i;
    for (i = 0; i < n && *s; i++) {
        while (*s == ' ' || *s == '\t') s++;
        while (*s && *s != ' ' && *s != '\t') s++;
    }
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static const char *
compare_view(const char *s, const uniq_opts_t *o)
{
    if (o->skip_fields > 0) s = skip_n_fields(s, o->skip_fields);
    if (o->skip_chars > 0) {
        int i;
        for (i = 0; i < o->skip_chars && *s; i++) s++;
    }
    return s;
}

static int
lines_equal(const char *a, const char *b, const uniq_opts_t *o)
{
    a = compare_view(a, o);
    b = compare_view(b, o);
    if (o->width > 0) {
        if (o->icase) {
            for (int i = 0; i < o->width; i++) {
                int ca = (a[i] != '\0') ? tolower((unsigned char)a[i]) : 0;
                int cb = (b[i] != '\0') ? tolower((unsigned char)b[i]) : 0;
                if (ca != cb) return 0;
                if (ca == 0) return 1;
            }
            return 1;
        }
        return strncmp(a, b, (size_t)o->width) == 0;
    }
    if (o->icase) {
        while (*a && *b) {
            if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
                return 0;
            a++; b++;
        }
        return *a == *b;
    }
    return strcmp(a, b) == 0;
}

static int
read_line(FILE *f, char **out, size_t *out_len, int term)
{
    size_t cap = 256;
    size_t len = 0;
    char  *buf = malloc(cap);
    int    c;
    if (!buf) return -1;
    while ((c = fgetc(f)) != EOF) {
        if (len + 1 >= cap) {
            size_t nc = cap * 2;
            char *nb = realloc(buf, nc);
            if (!nb) { free(buf); return -1; }
            buf = nb; cap = nc;
        }
        if (c == term) {
            buf[len] = '\0';
            *out = buf; *out_len = len;
            return 1;
        }
        buf[len++] = (char)c;
    }
    if (len == 0) { free(buf); return 0; }
    buf[len] = '\0';
    *out = buf; *out_len = len;
    return 1;
}

static void
emit(FILE *out, const char *line, size_t len, int count,
     const uniq_opts_t *o)
{
    if (o->count) {
        fprintf(out, "%4d ", count);
    }
    fwrite(line, 1, len, out);
    fputc(o->z_terminated ? '\0' : '\n', out);
}

int
main(int argc, char **argv)
{
    uniq_opts_t o;
    int   argi = 1;
    FILE *in   = stdin;
    FILE *out  = stdout;
    char *cur = NULL, *prev = NULL;
    size_t cur_len = 0, prev_len = 0;
    int    count = 0;
    int    first_group = 1;
    int    term;

    progname = argv[0];
    memset(&o, 0, sizeof(o));

    while (argi < argc) {
        const char *a = argv[argi];
        if (a[0] != '-' || a[1] == '\0') break;
        if (strcmp(a, "--") == 0) { argi++; break; }
        if (strcmp(a, "--help") == 0) usage();
        if (strncmp(a, "--all-repeated", 14) == 0) {
            o.print_all_dup = 1;
            if (a[14] == '=') {
                const char *m = a + 15;
                if      (strcmp(m, "none")     == 0) o.group_sep = GS_NONE;
                else if (strcmp(m, "prepend")  == 0) o.group_sep = GS_PREPEND;
                else if (strcmp(m, "append")   == 0) o.group_sep = GS_APPEND;
                else if (strcmp(m, "separate") == 0) o.group_sep = GS_SEPARATE;
                else {
                    fprintf(stderr, "%s: bad --all-repeated method: %s\n",
                            progname, m);
                    return 2;
                }
            }
            argi++;
            continue;
        }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
                case 'c': o.count = 1; break;
                case 'd': o.only_dup = 1; break;
                case 'u': o.only_uniq = 1; break;
                case 'i': o.icase = 1; break;
                case 'D': o.print_all_dup = 1; break;
                case 'z': o.z_terminated = 1; break;
                case 'f':
                    if (p[1] != '\0') { o.skip_fields = atoi(p + 1); p += strlen(p) - 1; break; }
                    if (argi + 1 >= argc) usage();
                    o.skip_fields = atoi(argv[++argi]); goto next_arg;
                case 's':
                    if (p[1] != '\0') { o.skip_chars = atoi(p + 1); p += strlen(p) - 1; break; }
                    if (argi + 1 >= argc) usage();
                    o.skip_chars = atoi(argv[++argi]); goto next_arg;
                case 'w':
                    if (p[1] != '\0') { o.width = atoi(p + 1); p += strlen(p) - 1; break; }
                    if (argi + 1 >= argc) usage();
                    o.width = atoi(argv[++argi]); goto next_arg;
                default:
                    fprintf(stderr, "%s: invalid option -- '%c'\n", progname, *p);
                    usage();
            }
        }
        argi++;
next_arg: ;
    }

    if (argi < argc && strcmp(argv[argi], "-") != 0) {
        in = fopen(argv[argi], "r");
        if (!in) {
            fprintf(stderr, "%s: %s: %s\n", progname, argv[argi],
                    strerror(errno));
            return 1;
        }
    }
    if (argi < argc) argi++;
    if (argi < argc) {
        out = fopen(argv[argi], "w");
        if (!out) {
            fprintf(stderr, "%s: %s: %s\n", progname, argv[argi],
                    strerror(errno));
            return 1;
        }
    }

    term = o.z_terminated ? '\0' : '\n';

    while (read_line(in, &cur, &cur_len, term) == 1) {
        if (prev != NULL && lines_equal(prev, cur, &o)) {
            count++;
            if (o.print_all_dup) {
                if (count == 2) {
                    if (!first_group && o.group_sep == GS_SEPARATE) {
                        fputc(term, out);
                    } else if (o.group_sep == GS_PREPEND && !first_group) {
                        fputc(term, out);
                    }
                    emit(out, prev, prev_len, 1, &o);
                    first_group = 0;
                }
                emit(out, cur, cur_len, count, &o);
            }
            free(cur);
            cur = NULL; cur_len = 0;
            continue;
        }
        if (prev != NULL) {
            int is_dup = (count > 1);
            if (!o.print_all_dup) {
                int show = 1;
                if (o.only_dup  && !is_dup) show = 0;
                if (o.only_uniq &&  is_dup) show = 0;
                if (show) emit(out, prev, prev_len, count, &o);
            } else if (is_dup && o.group_sep == GS_APPEND) {
                fputc(term, out);
            }
            free(prev);
        }
        prev = cur; prev_len = cur_len;
        cur  = NULL; cur_len = 0;
        count = 1;
    }
    if (prev != NULL) {
        int is_dup = (count > 1);
        if (!o.print_all_dup) {
            int show = 1;
            if (o.only_dup  && !is_dup) show = 0;
            if (o.only_uniq &&  is_dup) show = 0;
            if (show) emit(out, prev, prev_len, count, &o);
        } else if (is_dup && o.group_sep == GS_APPEND) {
            fputc(term, out);
        }
        free(prev);
    }

    if (in != stdin) fclose(in);
    if (out != stdout) fclose(out);
    return 0;
}
