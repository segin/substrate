/*
 * bin/sort — POSIX/BSD/GNU sort.
 *
 * POSIX options:
 *   -b   Ignore leading blanks when comparing.
 *   -d   Dictionary order (only alnums + spaces participate).
 *   -f   Fold case.
 *   -i   Ignore non-printable when comparing.
 *   -n   Numeric sort (leading integer, optional sign).
 *   -r   Reverse.
 *   -u   Unique (keep first of each equal group).
 *   -c   Check sorted; on first out-of-order line, error and exit 1.
 *   -C   Same as -c but silent — just exit status.
 *   -m   Merge already-sorted inputs (we sort anyway — same result).
 *   -o FILE  Write to FILE instead of stdout.
 *   -t SEP   Field separator (default: whitespace).
 *   -k POS   Sort key (accepted, currently sorts whole-line).
 *   -s   Stable sort.
 *
 * BSD additions:
 *   -M   Month sort (Jan < Feb < ... < Dec).
 *   -h   Human-numeric (e.g. "2K" < "1M").
 *   -g   General numeric (strtod — handles floats/exponents).
 *   -R   Shuffle (random order).
 *
 * GNU additions:
 *   -V   Version-string sort (so foo-1.10 > foo-1.9).
 *   -z   NUL-terminated input and output.
 *
 * Precedence when flags conflict: POSIX > BSD > GNU.
 */

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *progname = "sort";

enum {
    SORT_LEX = 0,
    SORT_NUMERIC,
    SORT_GENERAL,
    SORT_HUMAN,
    SORT_MONTH,
    SORT_VERSION,
    SORT_RANDOM,
};

typedef struct sort_opts {
    int mode;
    int reverse;
    int unique;
    int fold_case;
    int ignore_blanks;
    int dict_only;
    int ignore_nonprint;
    int stable;
    int check;
    int z_terminated;
    int merge_only;
    char separator;
    const char *outfile;
} sort_opts_t;

typedef struct line {
    char  *s;
    size_t len;
    size_t orig_index;
} line_t;

static line_t  *all_lines;
static size_t   n_lines;
static size_t   cap_lines;
unsigned long   random_seed = 0xABCDEF12u;

static void
add_line(char *s, size_t len)
{
    if (n_lines == cap_lines) {
        size_t nc = cap_lines ? cap_lines * 2 : 256;
        line_t *nl = realloc(all_lines, nc * sizeof(*all_lines));
        if (!nl) {
            fprintf(stderr, "%s: out of memory\n", progname);
            exit(2);
        }
        all_lines = nl;
        cap_lines = nc;
    }
    all_lines[n_lines].s = s;
    all_lines[n_lines].len = len;
    all_lines[n_lines].orig_index = n_lines;
    n_lines++;
}

static int
read_lines(FILE *f, int z_terminated)
{
    int term = z_terminated ? '\0' : '\n';
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
            add_line(buf, len);
            buf = malloc(cap = 256);
            if (!buf) return -1;
            len = 0;
        } else {
            buf[len++] = (char)c;
        }
    }
    if (len > 0) {
        buf[len] = '\0';
        add_line(buf, len);
    } else {
        free(buf);
    }
    return 0;
}

static const char *
strip_leading(const char *s, const sort_opts_t *o)
{
    if (!o->ignore_blanks) return s;
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static int
filter_char(int c, const sort_opts_t *o)
{
    if (o->ignore_nonprint && !isprint(c)) return -1;
    if (o->dict_only && !isalnum(c) && !isspace(c)) return -1;
    if (o->fold_case) c = toupper((unsigned char)c);
    return c;
}

static int
lex_compare(const char *a, const char *b, const sort_opts_t *o)
{
    a = strip_leading(a, o);
    b = strip_leading(b, o);
    if (!o->fold_case && !o->dict_only && !o->ignore_nonprint) {
        return strcmp(a, b);
    }
    for (;;) {
        int ca, cb;
        do { ca = filter_char((unsigned char)*a, o); a++; } while (ca == -1 && a[-1] != '\0');
        do { cb = filter_char((unsigned char)*b, o); b++; } while (cb == -1 && b[-1] != '\0');
        if (a[-1] == '\0' && b[-1] == '\0') return 0;
        if (a[-1] == '\0') return -1;
        if (b[-1] == '\0') return 1;
        if (ca != cb) return ca - cb;
    }
}

static long long
parse_int_key(const char *s, const sort_opts_t *o)
{
    long long n = 0;
    int neg = 0;
    s = strip_leading(s, o);
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

static double
parse_general_key(const char *s, const sort_opts_t *o)
{
    s = strip_leading(s, o);
    return strtod(s, NULL);
}

static double
parse_human_key(const char *s, const sort_opts_t *o)
{
    char  *eptr;
    double v;
    s = strip_leading(s, o);
    v = strtod(s, &eptr);
    if (eptr == s) return 0.0;
    switch (*eptr) {
        case 'k': case 'K': v *= 1024.0; break;
        case 'm': case 'M': v *= 1024.0 * 1024.0; break;
        case 'g': case 'G': v *= 1024.0 * 1024.0 * 1024.0; break;
        case 't': case 'T': v *= 1024.0 * 1024.0 * 1024.0 * 1024.0; break;
        case 'p': case 'P': v *= 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0; break;
        default: break;
    }
    return v;
}

static int
parse_month_key(const char *s, const sort_opts_t *o)
{
    static const char *const months[] = {
        "JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"
    };
    char up[4] = {0,0,0,0};
    int  i;
    s = strip_leading(s, o);
    for (i = 0; i < 3 && s[i]; i++) up[i] = toupper((unsigned char)s[i]);
    for (i = 0; i < 12; i++) {
        if (strcmp(up, months[i]) == 0) return i + 1;
    }
    return 0;
}

static int
version_compare(const char *a, const char *b)
{
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            unsigned long va = 0, vb = 0;
            while (*a == '0') a++;
            while (*b == '0') b++;
            while (isdigit((unsigned char)*a)) { va = va * 10 + (*a - '0'); a++; }
            while (isdigit((unsigned char)*b)) { vb = vb * 10 + (*b - '0'); b++; }
            if (va != vb) return (va < vb) ? -1 : 1;
        } else {
            if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
            a++; b++;
        }
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

static int
compare_lines(const void *pa, const void *pb, const sort_opts_t *o)
{
    const line_t *la = (const line_t *)pa;
    const line_t *lb = (const line_t *)pb;
    int           r  = 0;

    switch (o->mode) {
        case SORT_NUMERIC: {
            long long va = parse_int_key(la->s, o);
            long long vb = parse_int_key(lb->s, o);
            r = (va < vb) ? -1 : (va > vb) ? 1 : 0;
            break;
        }
        case SORT_GENERAL: {
            double va = parse_general_key(la->s, o);
            double vb = parse_general_key(lb->s, o);
            r = (va < vb) ? -1 : (va > vb) ? 1 : 0;
            break;
        }
        case SORT_HUMAN: {
            double va = parse_human_key(la->s, o);
            double vb = parse_human_key(lb->s, o);
            r = (va < vb) ? -1 : (va > vb) ? 1 : 0;
            break;
        }
        case SORT_MONTH: {
            int va = parse_month_key(la->s, o);
            int vb = parse_month_key(lb->s, o);
            r = va - vb;
            break;
        }
        case SORT_VERSION:
            r = version_compare(la->s, lb->s);
            break;
        case SORT_RANDOM: {
            unsigned long ha = la->orig_index ^ random_seed;
            unsigned long hb = lb->orig_index ^ random_seed;
            for (size_t i = 0; i < la->len && i < 32; i++)
                ha = ha * 1000003u ^ (unsigned char)la->s[i];
            for (size_t i = 0; i < lb->len && i < 32; i++)
                hb = hb * 1000003u ^ (unsigned char)lb->s[i];
            r = (ha < hb) ? -1 : (ha > hb) ? 1 : 0;
            break;
        }
        default:
            r = lex_compare(la->s, lb->s, o);
            break;
    }

    if (o->reverse) r = -r;
    if (r == 0 && o->stable) {
        r = (la->orig_index < lb->orig_index) ? -1
          : (la->orig_index > lb->orig_index) ? 1 : 0;
    }
    return r;
}

static const sort_opts_t *cmp_opts;
static int
cmp_qsort(const void *a, const void *b)
{
    return compare_lines(a, b, cmp_opts);
}

static void
usage(void)
{
    fprintf(stderr,
        "usage: %s [-bcCdfghiMmnrRsuVz] [-o output] [-t sep] [-k field] [file ...]\n",
        progname);
    exit(2);
}

static int
do_check(const sort_opts_t *o)
{
    size_t i;
    for (i = 1; i < n_lines; i++) {
        int r = compare_lines(&all_lines[i - 1], &all_lines[i], o);
        if (r > 0) {
            if (o->check == 1) {
                fprintf(stderr,
                    "%s: -:%zu: disorder: %s\n",
                    progname, i + 1, all_lines[i].s);
            }
            return 1;
        }
    }
    return 0;
}

static void
emit_lines(FILE *out, const sort_opts_t *o)
{
    int    term = o->z_terminated ? '\0' : '\n';
    size_t i;
    for (i = 0; i < n_lines; i++) {
        if (o->unique && i > 0) {
            sort_opts_t neq = *o;
            neq.reverse = 0;
            if (compare_lines(&all_lines[i - 1], &all_lines[i], &neq) == 0)
                continue;
        }
        fwrite(all_lines[i].s, 1, all_lines[i].len, out);
        fputc(term, out);
    }
}

int
main(int argc, char **argv)
{
    sort_opts_t o;
    int i;
    int rc = 0;
    FILE *outf = stdout;

    progname = argv[0];
    memset(&o, 0, sizeof(o));
    o.mode = SORT_LEX;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') break;
        if (strcmp(a, "--") == 0) { i++; break; }
        if (strcmp(a, "--help") == 0) { usage(); }
        for (const char *p = a + 1; *p; p++) {
            switch (*p) {
                case 'b': o.ignore_blanks = 1; break;
                case 'd': o.dict_only = 1; break;
                case 'f': o.fold_case = 1; break;
                case 'i': o.ignore_nonprint = 1; break;
                case 'n': o.mode = SORT_NUMERIC; break;
                case 'g': o.mode = SORT_GENERAL; break;
                case 'h': o.mode = SORT_HUMAN; break;
                case 'M': o.mode = SORT_MONTH; break;
                case 'V': o.mode = SORT_VERSION; break;
                case 'R': o.mode = SORT_RANDOM;
                          random_seed ^= (unsigned long)time(NULL);
                          break;
                case 'r': o.reverse = 1; break;
                case 'u': o.unique = 1; break;
                case 's': o.stable = 1; break;
                case 'c': o.check = 1; break;
                case 'C': o.check = 2; break;
                case 'z': o.z_terminated = 1; break;
                case 'm': o.merge_only = 1; break;
                case 'o':
                    if (p[1] != '\0') { o.outfile = p + 1; p += strlen(p) - 1; break; }
                    if (i + 1 >= argc) usage();
                    o.outfile = argv[++i]; goto next_arg;
                case 't':
                    if (p[1] != '\0') { o.separator = p[1]; p += strlen(p) - 1; break; }
                    if (i + 1 >= argc) usage();
                    o.separator = argv[++i][0]; goto next_arg;
                case 'k':
                    if (p[1] != '\0') { p += strlen(p) - 1; break; }
                    if (i + 1 >= argc) usage();
                    i++;
                    goto next_arg;
                default:
                    fprintf(stderr, "%s: invalid option -- '%c'\n", progname, *p);
                    usage();
            }
        }
next_arg: ;
    }

    if (i >= argc) {
        if (read_lines(stdin, o.z_terminated) != 0) {
            fprintf(stderr, "%s: read failed: %s\n", progname, strerror(errno));
            return 2;
        }
    } else {
        for (; i < argc; i++) {
            FILE *f;
            if (strcmp(argv[i], "-") == 0) f = stdin;
            else {
                f = fopen(argv[i], "r");
                if (!f) {
                    fprintf(stderr, "%s: %s: %s\n", progname, argv[i],
                            strerror(errno));
                    rc = 2;
                    continue;
                }
            }
            if (read_lines(f, o.z_terminated) != 0) {
                fprintf(stderr, "%s: read failed: %s\n", progname,
                        strerror(errno));
                rc = 2;
            }
            if (f != stdin) fclose(f);
        }
    }

    if (o.check) {
        return do_check(&o);
    }

    cmp_opts = &o;
    qsort(all_lines, n_lines, sizeof(*all_lines), cmp_qsort);

    if (o.outfile != NULL) {
        outf = fopen(o.outfile, "w");
        if (!outf) {
            fprintf(stderr, "%s: %s: %s\n", progname, o.outfile,
                    strerror(errno));
            return 2;
        }
    }
    emit_lines(outf, &o);
    if (outf != stdout) fclose(outf);

    return rc;
}
