/*
 * split - split a file into pieces.
 *
 *   split [-l line_count] [-b byte_count[k|m]] [-a suffix_len]
 *         [file [prefix]]
 *
 * Writes fixed-size output files named <prefix>aa, <prefix>ab, ...
 * (default prefix "x").  -l splits every line_count lines (default
 * 1000); -b splits every byte_count bytes (k = 1024, m = 1048576).
 *
 * The previous stub printed "not implemented" and exited 0, so any
 * pipeline that relied on the pieces silently produced nothing while
 * reporting success.
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *prog = "split";
static int   suffix_len = 2;
static char  prefix[PATH_MAX] = "x";

/* Compose the Nth suffix ("aa", "ab", ... "zz", then error at wraparound)
 * into out.  Returns 0 on success, -1 when the suffix space is exhausted. */
static int
make_name(char *out, size_t outsz, unsigned long idx)
{
    char suf[16];
    if ((size_t)suffix_len >= sizeof suf)
        return -1;

    for (int i = suffix_len - 1; i >= 0; i--) {
        suf[i] = 'a' + (int)(idx % 26);
        idx /= 26;
    }
    if (idx != 0)
        return -1;                    /* ran past the suffix width */
    suf[suffix_len] = '\0';

    if (snprintf(out, outsz, "%s%s", prefix, suf) >= (int)outsz)
        return -1;
    return 0;
}

static FILE *
open_piece(unsigned long idx)
{
    char name[PATH_MAX];
    if (make_name(name, sizeof name, idx) != 0) {
        fprintf(stderr, "%s: output suffix space exhausted\n", prog);
        return NULL;
    }
    FILE *f = fopen(name, "wb");
    if (!f)
        fprintf(stderr, "%s: %s: %s\n", prog, name, strerror(errno));
    return f;
}

/* Parse a byte count with an optional k/m suffix. */
static long
parse_bytes(const char *s)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || v <= 0 || errno == ERANGE) {
        fprintf(stderr, "%s: invalid byte count '%s'\n", prog, s);
        exit(1);
    }
    long mul = 1;
    if (*end == 'k' || *end == 'K') { mul = 1024; end++; }
    else if (*end == 'm' || *end == 'M') { mul = 1024 * 1024; end++; }
    if (*end != '\0') {
        fprintf(stderr, "%s: invalid byte count '%s'\n", prog, s);
        exit(1);
    }
    if (v > LONG_MAX / mul) {
        fprintf(stderr, "%s: byte count too large\n", prog);
        exit(1);
    }
    return v * mul;
}

static long
parse_lines(const char *s)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v <= 0 || errno == ERANGE) {
        fprintf(stderr, "%s: invalid line count '%s'\n", prog, s);
        exit(1);
    }
    return v;
}

static int
split_lines(FILE *in, long per)
{
    unsigned long idx = 0;
    long count = 0;
    FILE *out = NULL;
    int c;

    while ((c = getc(in)) != EOF) {
        if (out == NULL) {
            out = open_piece(idx++);
            if (!out)
                return 1;
        }
        if (putc(c, out) == EOF) {
            fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
            fclose(out);
            return 1;
        }
        if (c == '\n' && ++count >= per) {
            count = 0;
            if (fclose(out) != 0) {
                fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
                return 1;
            }
            out = NULL;
        }
    }
    if (out && fclose(out) != 0) {
        fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
        return 1;
    }
    return 0;
}

static int
split_bytes(FILE *in, long per)
{
    unsigned long idx = 0;
    long count = 0;
    FILE *out = NULL;
    int c;

    while ((c = getc(in)) != EOF) {
        if (out == NULL) {
            out = open_piece(idx++);
            if (!out)
                return 1;
            count = 0;
        }
        if (putc(c, out) == EOF) {
            fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
            fclose(out);
            return 1;
        }
        if (++count >= per) {
            if (fclose(out) != 0) {
                fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
                return 1;
            }
            out = NULL;
        }
    }
    if (out && fclose(out) != 0) {
        fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
        return 1;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    long lines = 1000;
    long bytes = 0;             /* 0 => line mode */
    int  i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) { i++; break; }
        if (a[0] != '-' || a[1] == '\0')
            break;
        if (strcmp(a, "-l") == 0 && i + 1 < argc) {
            lines = parse_lines(argv[++i]);
            bytes = 0;
        } else if (strncmp(a, "-l", 2) == 0) {
            lines = parse_lines(a + 2);
            bytes = 0;
        } else if (strcmp(a, "-b") == 0 && i + 1 < argc) {
            bytes = parse_bytes(argv[++i]);
        } else if (strncmp(a, "-b", 2) == 0) {
            bytes = parse_bytes(a + 2);
        } else if (strcmp(a, "-a") == 0 && i + 1 < argc) {
            suffix_len = atoi(argv[++i]);
        } else if (strncmp(a, "-a", 2) == 0) {
            suffix_len = atoi(a + 2);
        } else {
            fprintf(stderr, "%s: invalid option '%s'\n", prog, a);
            return 1;
        }
    }

    if (suffix_len < 1 || suffix_len > 8) {
        fprintf(stderr, "%s: invalid suffix length\n", prog);
        return 1;
    }

    FILE *in = stdin;
    if (i < argc && strcmp(argv[i], "-") != 0) {
        in = fopen(argv[i], "rb");
        if (!in) {
            fprintf(stderr, "%s: %s: %s\n", prog, argv[i], strerror(errno));
            return 1;
        }
    }
    if (i < argc) i++;
    if (i < argc) {
        if (snprintf(prefix, sizeof prefix, "%s", argv[i]) >= (int)sizeof prefix) {
            fprintf(stderr, "%s: prefix too long\n", prog);
            return 1;
        }
    }

    int rc = bytes > 0 ? split_bytes(in, bytes) : split_lines(in, lines);
    if (in != stdin)
        fclose(in);
    return rc;
}
