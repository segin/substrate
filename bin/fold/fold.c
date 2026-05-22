/*
 * fold - fold long lines for finite-width output devices
 *
 * POSIX.1-2024 + GNU coreutils + BSD extensions.
 * Conflict policy: BSD precedence (see docs/specs/diff_cmp_fold_fmt.md).
 *
 *   fold [-bs] [-w width | -width] [file...]
 *
 *   -b   count bytes, not display columns            (POSIX)
 *   -s   break at the last blank within the width    (POSIX)
 *   -w   line width (default 80)                     (POSIX)
 *   -width   obsolete numeric-width form             (POSIX legacy)
 */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FOLD_VERSION "fold (Substrate) 1.0"

static int  width  = 80;
static bool bytes  = false;
static bool spaces = false;
static const char *prog = "fold";

struct line { char *p; size_t len, cap; };

static void buf_push(struct line *b, char c)
{
	if (b->len == b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 256;
		char *np = realloc(b->p, nc);
		if (!np) {
			fprintf(stderr, "%s: out of memory\n", prog);
			exit(1);
		}
		b->p = np;
		b->cap = nc;
	}
	b->p[b->len++] = c;
}

/* Column reached after placing byte c starting from column col. */
static int advance(int col, unsigned char c)
{
	if (bytes)
		return col + 1;
	switch (c) {
	case '\t': return col + (8 - col % 8);
	case '\b': return col > 0 ? col - 1 : 0;
	case '\r': return 0;
	default:   return col + 1;
	}
}

/* Replay the column accounting over a span of bytes. */
static int width_of(const char *p, size_t n)
{
	int col = 0;
	for (size_t i = 0; i < n; i++)
		col = advance(col, (unsigned char)p[i]);
	return col;
}

static void fold_stream(FILE *f)
{
	struct line buf = { 0 };
	int col = 0;
	int c;

	while ((c = getc(f)) != EOF) {
		if (c == '\n') {
			fwrite(buf.p, 1, buf.len, stdout);
			putchar('\n');
			buf.len = 0;
			col = 0;
			continue;
		}

		int ncol = advance(col, (unsigned char)c);
		if (col > 0 && ncol > width) {
			/* The pending line is full; emit a break. */
			size_t cut = buf.len;	/* default: break here */
			if (spaces) {
				size_t k = buf.len;
				while (k > 0 && buf.p[k - 1] != ' '
				       && buf.p[k - 1] != '\t')
					k--;
				if (k > 0)
					cut = k;	/* after last blank */
			}
			fwrite(buf.p, 1, cut, stdout);
			putchar('\n');
			memmove(buf.p, buf.p + cut, buf.len - cut);
			buf.len -= cut;
			col = width_of(buf.p, buf.len);
		}
		buf_push(&buf, (char)c);
		col = advance(col, (unsigned char)c);
	}
	if (buf.len > 0)
		fwrite(buf.p, 1, buf.len, stdout);
	free(buf.p);
}

static void usage(FILE *o)
{
	fprintf(o, "usage: %s [-bs] [-w width] [file...]\n", prog);
}

int main(int argc, char **argv)
{
	/* Pull out the obsolete -<digits> width form before getopt. */
	char *nargv[argc + 1];
	int nargc = 0;
	bool endopt = false;
	nargv[nargc++] = argv[0];
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!endopt && strcmp(a, "--") == 0)
			endopt = true;
		if (!endopt && a[0] == '-' && isdigit((unsigned char)a[1])) {
			const char *q = a + 1;
			while (isdigit((unsigned char)*q))
				q++;
			if (*q == '\0') {
				width = atoi(a + 1);
				continue;	/* consumed */
			}
		}
		nargv[nargc++] = argv[i];
	}
	nargv[nargc] = NULL;

	static const struct option lopt[] = {
		{ "bytes",   no_argument,       0, 'b' },
		{ "spaces",  no_argument,       0, 's' },
		{ "width",   required_argument, 0, 'w' },
		{ "help",    no_argument,       0, 'H' },
		{ "version", no_argument,       0, 'V' },
		{ 0, 0, 0, 0 },
	};
	int c;
	while ((c = getopt_long(nargc, nargv, "bsw:", lopt, NULL)) != -1) {
		switch (c) {
		case 'b': bytes = true; break;
		case 's': spaces = true; break;
		case 'w': width = atoi(optarg); break;
		case 'H': usage(stdout); return 0;
		case 'V': puts(FOLD_VERSION); return 0;
		default:  usage(stderr); return 1;
		}
	}
	if (width < 1) {
		fprintf(stderr, "%s: width must be a positive integer\n", prog);
		return 1;
	}

	int rc = 0;
	if (optind >= nargc) {
		fold_stream(stdin);
	} else {
		for (int i = optind; i < nargc; i++) {
			FILE *f;
			if (strcmp(nargv[i], "-") == 0) {
				f = stdin;
			} else if (!(f = fopen(nargv[i], "r"))) {
				fprintf(stderr, "%s: %s: %s\n", prog,
				    nargv[i], strerror(errno));
				rc = 1;
				continue;
			}
			fold_stream(f);
			if (f != stdin)
				fclose(f);
		}
	}
	return rc;
}
