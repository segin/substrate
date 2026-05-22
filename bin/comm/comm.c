/*
 * comm - select or reject lines common to two sorted files
 *
 * POSIX.1-2024 + GNU coreutils + BSD extensions.
 * Conflict policy: BSD precedence (see docs/specs/comm_cut_paste.md).
 *
 *   comm [-123i] [-z] [--output-delimiter=STR] [--total]
 *        [--check-order|--nocheck-order] file1 file2
 *
 *   -1/-2/-3              suppress column 1/2/3
 *   -i                   case-insensitive compare           (BSD)
 *   -z, --zero-terminated NUL line delimiter                (GNU)
 *   --output-delimiter=S  inter-column delimiter (default tab) (GNU)
 *   --total               print a trailing count summary    (GNU)
 *   --check-order         fail on unsorted input            (GNU)
 *   --nocheck-order       do not check input order (default, BSD)
 */

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define COMM_VERSION "comm (Substrate) 1.0"

static int         line_delim  = '\n';
static bool        sup[3]      = { false, false, false };
static bool        ignore_case = false;
static bool        opt_total   = false;
static bool        check_order = false;
static const char *odelim      = "\t";
static const char *prog        = "comm";

struct buf { char *p; size_t len, cap; };

/* Read one delimited record (delimiter stripped).  Returns 1 on a
 * record, 0 at end of input. */
static int read_rec(FILE *f, struct buf *b)
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
			if (!np) { perror(prog); exit(2); }
			b->p = np; b->cap = nc;
		}
		b->p[b->len++] = (char)c;
	}
	if (!any)
		return 0;
	if (b->len + 1 > b->cap) {
		char *np = realloc(b->p, b->len + 1);
		if (!np) { perror(prog); exit(2); }
		b->p = np; b->cap = b->len + 1;
	}
	b->p[b->len] = '\0';
	return 1;
}

static int rec_cmp(const struct buf *a, const struct buf *b)
{
	int r = ignore_case ? strcasecmp(a->p, b->p) : strcmp(a->p, b->p);
	return r;
}

static void buf_copy(struct buf *dst, const struct buf *src)
{
	if (dst->cap < src->len + 1) {
		char *np = realloc(dst->p, src->len + 1);
		if (!np) { perror(prog); exit(2); }
		dst->p = np; dst->cap = src->len + 1;
	}
	memcpy(dst->p, src->p, src->len + 1);
	dst->len = src->len;
}

static void emit(int col, const struct buf *b)
{
	int pre = 0, i;

	if (sup[col])
		return;
	for (i = 0; i < col; i++)
		if (!sup[i])
			pre++;
	for (i = 0; i < pre; i++)
		fputs(odelim, stdout);
	if (b->len)
		fwrite(b->p, 1, b->len, stdout);
	putchar(line_delim);
}

static void usage(FILE *s)
{
	fprintf(s, "Usage: %s [-123i] [-z] [--output-delimiter=STR] "
	    "[--total] [--check-order|--nocheck-order] file1 file2\n", prog);
}

int main(int argc, char **argv)
{
	static const struct option lo[] = {
		{ "zero-terminated", no_argument,       NULL, 'z' },
		{ "output-delimiter", required_argument, NULL, 1000 },
		{ "total",           no_argument,       NULL, 1001 },
		{ "check-order",     no_argument,       NULL, 1002 },
		{ "nocheck-order",   no_argument,       NULL, 1003 },
		{ "help",            no_argument,       NULL, 1004 },
		{ "version",         no_argument,       NULL, 1005 },
		{ NULL, 0, NULL, 0 }
	};
	FILE      *f[2];
	struct buf cur[2]  = {{0}}, prev[2] = {{0}};
	int        have[2] = { 0, 0 }, had_prev[2] = { 0, 0 };
	uint64_t   total[3] = { 0, 0, 0 };
	int        opt, i, exitcode = 0;

	prog = argv[0];
	while ((opt = getopt_long(argc, argv, "123iz", lo, NULL)) != -1) {
		switch (opt) {
		case '1': sup[0] = true; break;
		case '2': sup[1] = true; break;
		case '3': sup[2] = true; break;
		case 'i': ignore_case = true; break;
		case 'z': line_delim = '\0'; break;
		case 1000: odelim = optarg; break;
		case 1001: opt_total = true; break;
		case 1002: check_order = true; break;
		case 1003: check_order = false; break;
		case 1004: usage(stdout); return 0;
		case 1005: puts(COMM_VERSION); return 0;
		default: usage(stderr); return 2;
		}
	}

	if (argc - optind != 2) {
		fprintf(stderr, "%s: exactly two file operands are required\n",
		    prog);
		usage(stderr);
		return 2;
	}

	for (i = 0; i < 2; i++) {
		const char *name = argv[optind + i];
		if (strcmp(name, "-") == 0) {
			f[i] = stdin;
		} else if (!(f[i] = fopen(name, "r"))) {
			fprintf(stderr, "%s: %s: %s\n", prog, name,
			    strerror(errno));
			return 2;
		}
	}

#define NEXT(i) do {							\
		have[i] = read_rec(f[i], &cur[i]);			\
		if (have[i]) {						\
			if (check_order && had_prev[i] &&		\
			    rec_cmp(&cur[i], &prev[i]) < 0) {		\
				fprintf(stderr, "%s: file %d is not in "\
				    "sorted order\n", prog, (i) + 1);	\
				exitcode = 1; check_order = false;	\
			}						\
			buf_copy(&prev[i], &cur[i]);			\
			had_prev[i] = 1;				\
		}							\
	} while (0)

	NEXT(0);
	NEXT(1);

	while (have[0] && have[1]) {
		int c = rec_cmp(&cur[0], &cur[1]);
		if (c < 0) {
			emit(0, &cur[0]); total[0]++; NEXT(0);
		} else if (c > 0) {
			emit(1, &cur[1]); total[1]++; NEXT(1);
		} else {
			emit(2, &cur[0]); total[2]++; NEXT(0); NEXT(1);
		}
	}
	while (have[0]) { emit(0, &cur[0]); total[0]++; NEXT(0); }
	while (have[1]) { emit(1, &cur[1]); total[1]++; NEXT(1); }

	if (opt_total) {
		printf("%llu%s%llu%s%llu%stotal",
		    (unsigned long long)total[0], odelim,
		    (unsigned long long)total[1], odelim,
		    (unsigned long long)total[2], odelim);
		putchar(line_delim);
	}

	for (i = 0; i < 2; i++)
		if (f[i] != stdin)
			fclose(f[i]);
	if (fflush(stdout) != 0) {
		fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
		return 2;
	}
	return exitcode;
}
