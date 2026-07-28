/*
 * cut - cut out selected fields of each line of a file
 *
 * POSIX.1-2024 + GNU coreutils + BSD extensions.
 * Conflict policy: BSD precedence (see docs/specs/comm_cut_paste.md).
 *
 *   cut -b list [-n] [-z] [file...]
 *   cut -c list [-z] [file...]
 *   cut -f list [-d delim] [-s] [-z] [file...]
 *   cut -f list -w [-s] [-z] [file...]            (-w: BSD)
 *
 *   --complement            select the inverse of list      (GNU)
 *   --output-delimiter=STR  delimiter between outputs        (GNU)
 *   -z, --zero-terminated   NUL line delimiter               (GNU)
 *
 * In the C locale -b and -c are identical and -n is a no-op.
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "getopt.h"

#define CUT_VERSION "cut (Substrate) 1.0"

static const char *prog       = "cut";
static int         line_delim = '\n';
static char        mode       = 0;     /* 'b' | 'c' | 'f' */
static char        delim      = '\t';
static bool        suppress   = false; /* -s */
static bool        w_mode     = false; /* -w */
static bool        complement = false;
static const char *odelim     = NULL;  /* --output-delimiter */

struct range { unsigned lo, hi; };     /* 1-based; hi==UINT_MAX => open */
static struct range *ranges;
static size_t        nranges, cranges;

static void die(const char *msg)
{
	fprintf(stderr, "%s: %s\n", prog, msg);
	exit(2);
}

static void add_range(unsigned lo, unsigned hi)
{
	if (nranges == cranges) {
		cranges = cranges ? cranges * 2 : 16;
		ranges  = realloc(ranges, cranges * sizeof(*ranges));
		if (!ranges) die("out of memory");
	}
	ranges[nranges].lo = lo;
	ranges[nranges].hi = hi;
	nranges++;
}

static int range_cmp(const void *a, const void *b)
{
	const struct range *x = a, *y = b;
	if (x->lo < y->lo) return -1;
	if (x->lo > y->lo) return 1;
	return 0;
}

/* Parse a comma/dash range list (POSIX); merge overlaps. */
static void parse_list(const char *s)
{
	while (*s) {
		unsigned lo = 0, hi = UINT_MAX;
		bool     have_lo = false, have_hi = false, dash = false;
		const char *start = s;

		while (*s && *s != ',') {
			if (*s == '-') {
				if (dash) die("invalid field range");
				dash = true;
				s++;
			} else if (*s >= '0' && *s <= '9') {
				unsigned v = 0;
				while (*s >= '0' && *s <= '9') {
					unsigned d = (unsigned)(*s - '0');
					/* v*10 wraps mod 2^32 -> silently wrong (CUT-01). */
					if (v > (UINT_MAX - d) / 10u)
						die("byte/character/field value out of range");
					v = v * 10 + d;
					s++;
				}
				if (!dash) { lo = v; have_lo = true; }
				else       { hi = v; have_hi = true; }
			} else {
				die("invalid byte/character/field list");
			}
		}
		if (s == start)
			die("invalid byte/character/field list");
		if (!dash) {
			if (!have_lo) die("invalid list");
			hi = lo;
		} else {
			/* A bare '-' (no number on either side) is not a valid range
			 * (CUT-03). */
			if (!have_lo && !have_hi)
				die("invalid range with no endpoint");
			if (!have_lo) lo = 1;        /* -M */
			/* N- leaves hi == UINT_MAX */
		}
		if (lo == 0 || hi == 0)
			die("fields and positions are numbered from 1");
		if (lo > hi)
			die("invalid decreasing range");
		add_range(lo, hi);
		if (*s == ',') s++;
	}
	if (nranges == 0)
		die("invalid (empty) list");

	qsort(ranges, nranges, sizeof(*ranges), range_cmp);
	size_t w = 0;
	for (size_t r = 1; r < nranges; r++) {
		if (ranges[r].lo <= ranges[w].hi ||
		    (ranges[w].hi != UINT_MAX && ranges[r].lo == ranges[w].hi + 1)) {
			if (ranges[r].hi > ranges[w].hi)
				ranges[w].hi = ranges[r].hi;
		} else {
			ranges[++w] = ranges[r];
		}
	}
	nranges = w + 1;
}

static bool in_list(unsigned p)
{
	bool hit = false;
	for (size_t r = 0; r < nranges; r++)
		if (p >= ranges[r].lo && p <= ranges[r].hi) { hit = true; break; }
	return complement ? !hit : hit;
}

struct buf { char *p; size_t len, cap; };

static int read_line(FILE *f, struct buf *b)
{
	int c, any = 0;

	b->len = 0;
	while ((c = getc(f)) != EOF) {
		any = 1;
		if (c == line_delim)
			break;
		if (b->len + 1 >= b->cap) {
			size_t nc = b->cap ? b->cap * 2 : 256;
			char  *np = realloc(b->p, nc);
			if (!np) die("out of memory");
			b->p = np; b->cap = nc;
		}
		b->p[b->len++] = (char)c;
	}
	if (!any)
		return 0;
	return 1;
}

static void cut_bytes(const struct buf *b)
{
	unsigned p;
	long     last = -1;

	for (p = 1; p <= b->len; p++) {
		if (!in_list(p))
			continue;
		if (odelim && last >= 0 && (long)p > last + 1)
			fputs(odelim, stdout);
		putchar(b->p[p - 1]);
		last = p;
	}
	putchar(line_delim);
}

static void cut_fields(const struct buf *b)
{
	const char *out = odelim ? odelim : (w_mode ? "\t" : (char[]){ delim, 0 });
	size_t      i = 0;
	unsigned    fno = 0;
	bool        emitted = false;

	/* No delimiter present at all: whole line, unless -s. */
	if (w_mode) {
		bool any_ws = false;
		for (size_t k = 0; k < b->len; k++)
			if (b->p[k] == ' ' || b->p[k] == '\t') { any_ws = true; break; }
		if (!any_ws) {
			/* No delimiter: pass the whole line through. */
			if (!suppress) {
				fwrite(b->p, 1, b->len, stdout);
				putchar(line_delim);
			}
			return;
		}
		while (i < b->len && (b->p[i] == ' ' || b->p[i] == '\t'))
			i++;  /* skip leading whitespace */
	} else {
		bool found = false;
		for (size_t k = 0; k < b->len; k++)
			if (b->p[k] == delim) { found = true; break; }
		if (!found) {
			/* No delimiter: pass the whole line through. */
			if (!suppress) {
				fwrite(b->p, 1, b->len, stdout);
				putchar(line_delim);
			}
			return;
		}
	}

	while (i <= b->len) {
		size_t start = i;
		while (i < b->len) {
			if (w_mode) {
				if (b->p[i] == ' ' || b->p[i] == '\t') break;
			} else if (b->p[i] == delim) {
				break;
			}
			i++;
		}
		fno++;
		if (in_list(fno)) {
			if (emitted) fputs(out, stdout);
			fwrite(b->p + start, 1, i - start, stdout);
			emitted = true;
		}
		if (i >= b->len)
			break;
		if (w_mode) {
			while (i < b->len && (b->p[i] == ' ' || b->p[i] == '\t'))
				i++;
			if (i >= b->len) break;   /* trailing whitespace */
		} else {
			i++;                       /* skip the delimiter */
			if (i > b->len) break;
		}
	}
	putchar(line_delim);
}

static int process(FILE *f)
{
	struct buf b = { 0 };
	while (read_line(f, &b)) {
		if (mode == 'f')
			cut_fields(&b);
		else
			cut_bytes(&b);
	}
	free(b.p);
	return 0;
}

static void usage(FILE *s)
{
	fprintf(s, "Usage: %s -b|-c|-f LIST [-d DELIM] [-snw] [-z] "
	    "[--complement] [--output-delimiter=STR] [file...]\n", prog);
}

int main(int argc, char **argv)
{
	static const struct option lo[] = {
		{ "bytes",            required_argument, NULL, 'b' },
		{ "characters",       required_argument, NULL, 'c' },
		{ "fields",           required_argument, NULL, 'f' },
		{ "delimiter",        required_argument, NULL, 'd' },
		{ "only-delimited",   no_argument,       NULL, 's' },
		{ "zero-terminated",  no_argument,       NULL, 'z' },
		{ "complement",       no_argument,       NULL, 1000 },
		{ "output-delimiter", required_argument, NULL, 1001 },
		{ "help",             no_argument,       NULL, 1002 },
		{ "version",          no_argument,       NULL, 1003 },
		{ NULL, 0, NULL, 0 }
	};
	const char *list = NULL;
	int         opt, rc = 0;

	prog = argv[0];
	while ((opt = getopt_long(argc, argv, "b:c:d:f:nswz", lo, NULL)) != -1) {
		switch (opt) {
		case 'b': case 'c': case 'f':
			if (mode && mode != (char)opt)
				die("only one of -b, -c, -f may be used");
			mode = (char)opt;
			list = optarg;
			break;
		case 'd':
			if (strlen(optarg) != 1)
				die("the delimiter must be a single character");
			delim = optarg[0];
			break;
		case 'n': break;                    /* no-op in C locale */
		case 's': suppress = true; break;
		case 'w': w_mode = true; break;
		case 'z': line_delim = '\0'; break;
		case 1000: complement = true; break;
		case 1001: odelim = optarg; break;
		case 1002: usage(stdout); return 0;
		case 1003: puts(CUT_VERSION); return 0;
		default: usage(stderr); return 2;
		}
	}

	if (!mode)
		die("you must specify a list of bytes, characters, or fields");
	if (w_mode && mode != 'f')
		die("-w may only be used with -f");
	parse_list(list);

	if (optind == argc) {
		process(stdin);
	} else {
		for (int i = optind; i < argc; i++) {
			FILE *f;
			if (strcmp(argv[i], "-") == 0) {
				process(stdin);
				continue;
			}
			if (!(f = fopen(argv[i], "r"))) {
				fprintf(stderr, "%s: %s: %s\n", prog, argv[i],
				    strerror(errno));
				rc = 1;
				continue;
			}
			process(f);
			fclose(f);
		}
	}

	free(ranges);
	if (fflush(stdout) != 0) {
		fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
		return 2;
	}
	return rc;
}
