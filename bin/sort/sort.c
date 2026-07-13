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
#include <limits.h>
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
    /* -k field key (0 = whole line).  1-based field/char positions. */
    int has_key;
    int k_sfield, k_schar;
    int k_efield, k_echar;
} sort_opts_t;

typedef struct line {
    char  *s;
    size_t len;
    char  *key;        /* comparison key: points into s, or an extracted
                        * malloc for -k mode */
    size_t keylen;
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
        /* Guard the growth multiply on 32-bit (SORT-07). */
        if (nc < cap_lines || nc > SIZE_MAX / sizeof(*all_lines)) {
            fprintf(stderr, "%s: too many lines\n", progname);
            exit(2);
        }
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
    all_lines[n_lines].key = s;          /* default: whole-line key */
    all_lines[n_lines].keylen = len;
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

/*
 * Length-aware lexical compare.  The plain path uses memcmp over the
 * common length so an embedded NUL no longer truncates the key and -u
 * no longer merges distinct binary lines (SORT-01).  Filtering modes
 * (-b/-f/-d/-i) fall into a length-bounded byte loop.
 */
static int
lex_compare(const char *a, size_t alen, const char *b, size_t blen,
            const sort_opts_t *o)
{
    if (o->ignore_blanks) {
        while (alen && (*a == ' ' || *a == '\t')) { a++; alen--; }
        while (blen && (*b == ' ' || *b == '\t')) { b++; blen--; }
    }
    if (!o->fold_case && !o->dict_only && !o->ignore_nonprint) {
        size_t n = alen < blen ? alen : blen;
        int r = memcmp(a, b, n);
        if (r) return r < 0 ? -1 : 1;
        return alen < blen ? -1 : (alen > blen ? 1 : 0);
    }
    size_t ia = 0, ib = 0;
    for (;;) {
        int ca = -1, cb = -1;
        while (ia < alen && (ca = filter_char((unsigned char)a[ia++], o)) == -1)
            ;
        while (ib < blen && (cb = filter_char((unsigned char)b[ib++], o)) == -1)
            ;
        if (ca == -1 && cb == -1) return 0;
        if (ca == -1) return -1;
        if (cb == -1) return 1;
        if (ca != cb) return ca - cb;
    }
}

/*
 * Byte range [*fstart,*fend) of the 1-based field `field` in [s,s+len).
 * With -t the fields are separated by a single separator character;
 * without it, by runs of blanks (leading blanks skipped).
 */
static void
field_bounds(const char *s, size_t len, const sort_opts_t *o,
             int field, size_t *fstart, size_t *fend)
{
    if (o->separator) {
        char   sep = o->separator;
        size_t i = 0, start = 0;
        int    f = 1;
        while (f < field && i < len) {
            if (s[i] == sep) { f++; start = i + 1; }
            i++;
        }
        if (f < field) { *fstart = len; *fend = len; return; }
        size_t j = start;
        while (j < len && s[j] != sep) j++;
        *fstart = start; *fend = j;
    } else {
        size_t i = 0;
        int    f = 0;
        while (i < len) {
            while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
            if (i >= len) break;
            f++;
            size_t start = i;
            while (i < len && s[i] != ' ' && s[i] != '\t') i++;
            if (f == field) { *fstart = start; *fend = i; return; }
        }
        *fstart = len; *fend = len;
    }
}

/* Extract the -k key of a line into a fresh malloc'd, NUL-terminated
 * buffer; *outlen gets its byte length.  Returns NULL on OOM. */
static char *
extract_key(const char *s, size_t len, const sort_opts_t *o, size_t *outlen)
{
    size_t sfs, sfe, efs, efe;
    field_bounds(s, len, o, o->k_sfield, &sfs, &sfe);
    size_t start = sfs + (o->k_schar > 0 ? (size_t)(o->k_schar - 1) : 0);
    if (start > sfe && o->k_efield == o->k_sfield) start = sfe;
    if (start > len) start = len;

    size_t end;
    if (o->k_efield == 0) {
        end = len;                          /* through end of line */
    } else {
        field_bounds(s, len, o, o->k_efield, &efs, &efe);
        if (o->k_echar > 0) {
            end = efs + (size_t)o->k_echar;  /* .C inclusive */
            if (end > efe) end = efe;
        } else {
            end = efe;
        }
    }
    if (end < start) end = start;

    size_t klen = end - start;
    char *k = malloc(klen + 1);
    if (!k) return NULL;
    memcpy(k, s + start, klen);
    k[klen] = '\0';
    *outlen = klen;
    return k;
}

static long long
parse_int_key(const char *s, const sort_opts_t *o)
{
    s = strip_leading(s, o);
    errno = 0;
    char *end;
    long long v = strtoll(s, &end, 10);
    if (end == s)
        return 0;                       /* no digits: sorts as 0 */
    if (errno == ERANGE)                /* saturate instead of wrapping (SORT-05) */
        return (s[0] == '-') ? LLONG_MIN : LLONG_MAX;
    return v;
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
            /* Compare digit runs by significant length then lexically —
             * no numeric accumulation, so arbitrarily long version
             * numbers can't overflow (SORT-06). */
            while (*a == '0') a++;
            while (*b == '0') b++;
            const char *da = a, *db = b;
            while (isdigit((unsigned char)*a)) a++;
            while (isdigit((unsigned char)*b)) b++;
            size_t la = (size_t)(a - da), lb = (size_t)(b - db);
            if (la != lb) return (la < lb) ? -1 : 1;
            int c = memcmp(da, db, la);
            if (c != 0) return c < 0 ? -1 : 1;
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

    /* Comparisons operate on the extracted key (whole line when no -k). */
    switch (o->mode) {
        case SORT_NUMERIC: {
            long long va = parse_int_key(la->key, o);
            long long vb = parse_int_key(lb->key, o);
            r = (va < vb) ? -1 : (va > vb) ? 1 : 0;
            break;
        }
        case SORT_GENERAL: {
            double va = parse_general_key(la->key, o);
            double vb = parse_general_key(lb->key, o);
            r = (va < vb) ? -1 : (va > vb) ? 1 : 0;
            break;
        }
        case SORT_HUMAN: {
            double va = parse_human_key(la->key, o);
            double vb = parse_human_key(lb->key, o);
            r = (va < vb) ? -1 : (va > vb) ? 1 : 0;
            break;
        }
        case SORT_MONTH: {
            int va = parse_month_key(la->key, o);
            int vb = parse_month_key(lb->key, o);
            r = va - vb;
            break;
        }
        case SORT_VERSION:
            r = version_compare(la->key, lb->key);
            break;
        case SORT_RANDOM: {
            unsigned long ha = random_seed, hb = random_seed;
            for (size_t i = 0; i < la->keylen && i < 32; i++)
                ha = ha * 1000003u ^ (unsigned char)la->key[i];
            for (size_t i = 0; i < lb->keylen && i < 32; i++)
                hb = hb * 1000003u ^ (unsigned char)lb->key[i];
            r = (ha < hb) ? -1 : (ha > hb) ? 1 : 0;
            /* Break hash ties by the full line so -R stays a strict total
             * order and -u doesn't drop distinct colliding lines (SORT-10). */
            if (r == 0)
                r = lex_compare(la->s, la->len, lb->s, lb->len, o);
            break;
        }
        default:
            r = lex_compare(la->key, la->keylen, lb->key, lb->keylen, o);
            break;
    }

    if (o->reverse) r = -r;
    if (r == 0 && o->stable) {
        r = (la->orig_index < lb->orig_index) ? -1
          : (la->orig_index > lb->orig_index) ? 1 : 0;
    }
    return r;
}

static int
cmp_qsort_r(const void *a, const void *b, void *ctx)
{
    return compare_lines(a, b, (const sort_opts_t *)ctx);
}

static void
usage(void)
{
    fprintf(stderr,
        "usage: %s [-bcCdfghiMmnrRsuVz] [-o output] [-t sep] [-k field] [file ...]\n",
        progname);
    exit(2);
}

/*
 * Parse a -k spec: F[.C][opts][,F[.C][opts]] (1-based).  Per-key type
 * flags (n, r, ...) trailing a position are accepted but ignored — the
 * global mode/reverse options apply instead (documented limitation).
 * Returns 0 on success, -1 on a malformed spec.
 */
static int
parse_key_spec(const char *s, sort_opts_t *o)
{
    char *end;
    long  f = strtol(s, &end, 10);
    if (end == s || f < 1) return -1;
    o->k_sfield = (int)f;
    o->k_schar = 0; o->k_efield = 0; o->k_echar = 0;
    if (*end == '.') {
        const char *cs = end + 1;
        f = strtol(cs, &end, 10);
        if (end == cs || f < 1) return -1;
        o->k_schar = (int)f;
    }
    while (*end && *end != ',') end++;      /* skip trailing type flags */
    if (*end == ',') {
        const char *es = end + 1;
        f = strtol(es, &end, 10);
        if (end == es || f < 1) return -1;
        o->k_efield = (int)f;
        if (*end == '.') {
            const char *cs = end + 1;
            f = strtol(cs, &end, 10);
            if (end == cs || f < 0) return -1;
            o->k_echar = (int)f;
        }
    }
    o->has_key = 1;
    return 0;
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
            neq.stable  = 0;   /* else the orig_index tiebreak makes equal
                                * keys never compare 0, so -su emits dupes
                                * (SORT-02) */
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
                case 't': {
                    const char *sep;
                    if (p[1] != '\0') { sep = p + 1; p += strlen(p) - 1; }
                    else { if (i + 1 >= argc) usage(); sep = argv[++i]; }
                    if (sep[0] == '\0') {   /* reject empty -t '' (SORT-08) */
                        fprintf(stderr, "%s: empty tab separator\n", progname);
                        usage();
                    }
                    o.separator = sep[0];
                    if (p[1] == '\0' && sep == argv[i]) goto next_arg;
                    break;
                }
                case 'k':
                    if (p[1] != '\0') {
                        if (parse_key_spec(p + 1, &o) != 0) {
                            fprintf(stderr, "%s: invalid key '%s'\n", progname, p + 1);
                            usage();
                        }
                        p += strlen(p) - 1;
                        break;
                    }
                    if (i + 1 >= argc) usage();
                    if (parse_key_spec(argv[++i], &o) != 0) {
                        fprintf(stderr, "%s: invalid key '%s'\n", progname, argv[i]);
                        usage();
                    }
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

    /* Materialise the -k comparison key for each line (SORT-04). */
    if (o.has_key) {
        for (size_t li = 0; li < n_lines; li++) {
            size_t kl;
            char *k = extract_key(all_lines[li].s, all_lines[li].len, &o, &kl);
            if (!k) {
                fprintf(stderr, "%s: out of memory\n", progname);
                return 2;
            }
            all_lines[li].key = k;
            all_lines[li].keylen = kl;
        }
    }

    if (o.check) {
        return do_check(&o);
    }

    qsort_r(all_lines, n_lines, sizeof(*all_lines), cmp_qsort_r, &o);

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
