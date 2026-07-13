/*
 * paste - merge corresponding or subsequent lines of files
 *
 * POSIX.1-2024 + GNU coreutils + BSD extensions.
 *
 *   paste [-s] [-d list] [-z] [file...]
 *
 *   -s                    serial: join each file's lines into one
 *   -d list, --delimiters list
 *                         cycled delimiter list; escapes \n \t \\ \0
 *                         (\0 == no delimiter).  Default: tab.
 *   -z, --zero-terminated NUL line delimiter                 (GNU)
 *
 * A file operand of '-', or no operand, designates standard input.
 */

#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PASTE_VERSION "paste (Substrate) 1.0"

static const char *prog       = "paste";
static int         line_delim = '\n';

/* Decoded delimiter list: dc[k] is the character, dused[k]==0 means
 * an empty delimiter (the \0 escape). */
static char  dc[256];
static char  dused[256];
static int   ndelim = 1;

struct buf { char *p; size_t len, cap; };

static void die(const char *msg)
{
	fprintf(stderr, "%s: %s\n", prog, msg);
	exit(1);
}

/* Decode a -d argument (with \n \t \\ \0 \r escapes) into dc/dused. */
static void parse_delims(const char *s)
{
	ndelim = 0;
	while (*s && ndelim < (int)sizeof(dc)) {
		char c;
		int  used = 1;
		if (*s == '\\' && s[1]) {
			s++;
			switch (*s) {
			case 'n':  c = '\n'; break;
			case 't':  c = '\t'; break;
			case '\\': c = '\\'; break;
			case 'r':  c = '\r'; break;
			case '0':  c = 0; used = 0; break;  /* empty */
			default:   c = *s;  break;
			}
			s++;
		} else {
			c = *s++;
		}
		dc[ndelim]    = c;
		dused[ndelim] = (char)used;
		ndelim++;
	}
	if (ndelim == 0)
		die("the delimiter list must not be empty");
}

static int read_line(FILE *f, struct buf *b)
{
	int c, any = 0;

	b->len = 0;
	while ((c = getc(f)) != EOF) {
		any = 1;
		if (c == line_delim)
			break;
		if (b->len + 1 >= b->cap) {
			/* Guard the doubling: on a >=2 GiB line b->cap*2 wraps to 0
			 * on the 32-bit target -> tiny realloc then heap overflow
			 * (PASTE-01). */
			if (b->cap > SIZE_MAX / 2) die("line too long");
			size_t nc = b->cap ? b->cap * 2 : 256;
			char  *np = realloc(b->p, nc);
			if (!np) die("out of memory");
			b->p = np; b->cap = nc;
		}
		b->p[b->len++] = (char)c;
	}
	return any;
}

static void put_sep(int k)
{
	if (dused[k % ndelim])
		putchar(dc[k % ndelim]);
}

/* Parallel mode: one line from each file per output line. */
static int paste_parallel(FILE **f, int n)
{
	struct buf *line = calloc((size_t)n, sizeof(*line));
	char       *eof  = calloc((size_t)n, 1);
	int         i, rc = 0;

	if (!line || !eof) die("out of memory");
	for (;;) {
		int got = 0;
		for (i = 0; i < n; i++) {
			if (eof[i]) { line[i].len = 0; continue; }
			if (read_line(f[i], &line[i]))
				got = 1;
			else { eof[i] = 1; line[i].len = 0; }
		}
		if (!got)
			break;
		for (i = 0; i < n; i++) {
			if (i > 0)
				put_sep(i - 1);
			if (line[i].len)
				fwrite(line[i].p, 1, line[i].len, stdout);
		}
		putchar(line_delim);
	}
	for (i = 0; i < n; i++)
		free(line[i].p);
	free(line);
	free(eof);
	return rc;
}

/* Serial mode: join all lines of each file onto one line. */
static int paste_serial(FILE **f, int n)
{
	struct buf b = { 0 };
	int        i;

	for (i = 0; i < n; i++) {
		int k = 0;
		while (read_line(f[i], &b)) {
			if (k > 0)
				put_sep(k - 1);
			if (b.len)
				fwrite(b.p, 1, b.len, stdout);
			k++;
		}
		if (k > 0)
			putchar(line_delim);
	}
	free(b.p);
	return 0;
}

int main(int argc, char **argv)
{
	static const struct option lo[] = {
		{ "delimiters",      required_argument, NULL, 'd' },
		{ "serial",          no_argument,       NULL, 's' },
		{ "zero-terminated", no_argument,       NULL, 'z' },
		{ "help",            no_argument,       NULL, 1000 },
		{ "version",         no_argument,       NULL, 1001 },
		{ NULL, 0, NULL, 0 }
	};
	bool   serial = false;
	int    opt, i, n, rc;
	FILE **f;

	prog = argv[0];
	dc[0] = '\t';
	dused[0] = 1;

	while ((opt = getopt_long(argc, argv, "d:sz", lo, NULL)) != -1) {
		switch (opt) {
		case 'd': parse_delims(optarg); break;
		case 's': serial = true; break;
		case 'z': line_delim = '\0'; break;
		case 1000:
			printf("Usage: %s [-s] [-d list] [-z] [file...]\n", prog);
			return 0;
		case 1001:
			puts(PASTE_VERSION);
			return 0;
		default:
			fprintf(stderr,
			    "Usage: %s [-s] [-d list] [-z] [file...]\n", prog);
			return 1;
		}
	}

	n = argc - optind;
	if (n < 1) {
		/* No operands: standard input. */
		static char *dash[] = { (char *)"-" };
		argv  = dash;
		optind = 0;
		n = 1;
	}

	f = calloc((size_t)n, sizeof(*f));
	if (!f) die("out of memory");
	for (i = 0; i < n; i++) {
		const char *name = argv[optind + i];
		if (strcmp(name, "-") == 0) {
			f[i] = stdin;
		} else if (!(f[i] = fopen(name, "r"))) {
			fprintf(stderr, "%s: %s: %s\n", prog, name,
			    strerror(errno));
			return 1;
		}
	}

	rc = serial ? paste_serial(f, n) : paste_parallel(f, n);

	for (i = 0; i < n; i++)
		if (f[i] != stdin)
			fclose(f[i]);
	free(f);
	if (fflush(stdout) != 0) {
		fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
		return 1;
	}
	return rc;
}
