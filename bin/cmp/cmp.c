/*
 * cmp - compare two files byte by byte
 *
 * POSIX.1-2024 + GNU diffutils + BSD extensions.
 * Conflict policy: BSD precedence (see docs/specs/diff_cmp_fold_fmt.md).
 *
 *   cmp [-l|-s|-x] [-bhz] [-i skip|skip1:skip2] [-n limit]
 *       file1 file2 [skip1 [skip2]]
 *
 *   -b   also show differing bytes as characters     (GNU/BSD)
 *   -h   do not dereference symlink operands         (BSD)
 *   -i   skip initial bytes (skip or skip1:skip2)    (GNU)
 *   -l   list every difference, byte values octal    (POSIX)
 *   -n   compare at most LIMIT bytes                 (GNU)
 *   -s   silent: report only via exit status         (POSIX)
 *   -x   list every difference, byte values hex      (BSD)
 *   -z   compare file sizes before content           (BSD)
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "getopt.h"
#include <sys/stat.h>

#define CMP_VERSION "cmp (Substrate) 1.0"

enum mode { M_FIRST, M_LIST_OCT, M_LIST_HEX, M_SILENT };

static const char *prog = "cmp";

static void usage(FILE *o)
{
	fprintf(o,
	    "usage: %s [-bhlsxz] [-i skip|skip1:skip2] [-n limit] "
	    "file1 file2 [skip1 [skip2]]\n", prog);
}

/* Parse a byte count with an optional 1024-based k/M/G or 1000-based
 * kB/MB/GB suffix.  Returns false on malformed input. */
static bool parse_count(const char *s, unsigned long long *out)
{
	char *end;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &end, 10);
	if (end == s || errno != 0)
		return false;

	unsigned long long mul = 1;
	switch (*end) {
	case '\0': *out = v; return true;
	case 'b':  mul = 512; end++; break;
	case 'k': case 'K': mul = 1024; end++; break;
	case 'M': mul = 1024ULL * 1024; end++; break;
	case 'G': mul = 1024ULL * 1024 * 1024; end++; break;
	default: return false;
	}
	if (*end == 'B') {		/* decimal variant: kB, MB, GB */
		if (mul == 1024) mul = 1000;
		else if (mul == 1024ULL * 1024) mul = 1000000ULL;
		else if (mul == 1024ULL * 1024 * 1024) mul = 1000000000ULL;
		end++;
	}
	if (*end != '\0')
		return false;
	*out = v * mul;
	return true;
}

static FILE *open_in(const char *name)
{
	if (strcmp(name, "-") == 0)
		return stdin;
	FILE *f = fopen(name, "rb");
	if (!f)
		fprintf(stderr, "%s: %s: %s\n", prog, name, strerror(errno));
	return f;
}

/* Discard `n` bytes from a stream; works on pipes too. */
static bool skip_bytes(FILE *f, unsigned long long n)
{
	while (n--) {
		if (getc(f) == EOF)
			return false;
	}
	return true;
}

/* GNU-style printable rendering of a byte for -b. */
static void put_byte(unsigned c)
{
	if (c >= 128) {
		fputs("M-", stdout);
		c -= 128;
	}
	if (c == 127)
		fputs("^?", stdout);
	else if (c < 32)
		printf("^%c", (int)(c + 64));
	else
		putchar((int)c);
}

int main(int argc, char **argv)
{
	enum mode mode = M_FIRST;
	bool show_bytes = false;
	bool size_first = false;
	unsigned long long skip1 = 0, skip2 = 0;
	unsigned long long limit = 0;	/* 0 = unlimited */
	bool have_limit = false;
	int c;

	static const struct option lopt[] = {
		{ "print-bytes",    no_argument,       0, 'b' },
		{ "ignore-initial", required_argument, 0, 'i' },
		{ "verbose",        no_argument,       0, 'l' },
		{ "bytes",          required_argument, 0, 'n' },
		{ "quiet",          no_argument,       0, 's' },
		{ "silent",         no_argument,       0, 's' },
		{ "help",           no_argument,       0, 'H' },
		{ "version",        no_argument,       0, 'V' },
		{ 0, 0, 0, 0 },
	};

	while ((c = getopt_long(argc, argv, "bhi:ln:sxz", lopt, NULL)) != -1) {
		switch (c) {
		case 'b': show_bytes = true; break;
		case 'h': break;		/* symlink no-deref: accepted */
		case 'i': {
			char *colon = strchr(optarg, ':');
			if (colon) {
				*colon = '\0';
				if (!parse_count(optarg, &skip1) ||
				    !parse_count(colon + 1, &skip2)) {
					fprintf(stderr, "%s: invalid --ignore-initial\n", prog);
					return 2;
				}
			} else if (!parse_count(optarg, &skip1)) {
				fprintf(stderr, "%s: invalid --ignore-initial\n", prog);
				return 2;
			} else {
				skip2 = skip1;
			}
			break;
		}
		case 'l': mode = M_LIST_OCT; break;
		case 'n':
			if (!parse_count(optarg, &limit)) {
				fprintf(stderr, "%s: invalid --bytes\n", prog);
				return 2;
			}
			have_limit = true;
			break;
		case 's': mode = M_SILENT; break;
		case 'x': mode = M_LIST_HEX; break;
		case 'z': size_first = true; break;
		case 'H': usage(stdout); return 0;
		case 'V': puts(CMP_VERSION); return 0;
		default:  usage(stderr); return 2;
		}
	}

	int rest = argc - optind;
	if (rest < 2 || rest > 4) {
		usage(stderr);
		return 2;
	}
	const char *n1 = argv[optind];
	const char *n2 = argv[optind + 1];
	if (rest >= 3 && !parse_count(argv[optind + 2], &skip1)) {
		fprintf(stderr, "%s: invalid skip1 operand\n", prog);
		return 2;
	}
	if (rest == 4 && !parse_count(argv[optind + 3], &skip2)) {
		fprintf(stderr, "%s: invalid skip2 operand\n", prog);
		return 2;
	}
	if (strcmp(n1, "-") == 0 && strcmp(n2, "-") == 0) {
		fprintf(stderr, "%s: cannot compare standard input to itself\n", prog);
		return 2;
	}

	FILE *f1 = open_in(n1);
	FILE *f2 = open_in(n2);
	if (!f1 || !f2)
		return 2;

	if (size_first && strcmp(n1, "-") != 0 && strcmp(n2, "-") != 0) {
		struct stat s1, s2;
		if (stat(n1, &s1) == 0 && stat(n2, &s2) == 0) {
			unsigned long long a = (unsigned long long)s1.st_size;
			unsigned long long b = (unsigned long long)s2.st_size;
			a = a > skip1 ? a - skip1 : 0;
			b = b > skip2 ? b - skip2 : 0;
			if (a != b) {
				if (mode != M_SILENT)
					fprintf(stderr, "%s: EOF on %s\n", prog,
					    a < b ? n1 : n2);
				return 1;
			}
		}
	}

	if (!skip_bytes(f1, skip1) || !skip_bytes(f2, skip2)) {
		/* A short file relative to its skip — treat as empty. */
	}

	unsigned long long pos = 0;	/* 1-based once incremented */
	unsigned long long line = 1;
	int rc = 0;

	for (;;) {
		if (have_limit && pos >= limit)
			break;
		int c1 = getc(f1);
		int c2 = getc(f2);
		if (c1 == EOF || c2 == EOF) {
			if (c1 == EOF && c2 == EOF)
				break;
			/* one ran out first */
			if (mode != M_SILENT)
				fprintf(stderr, "%s: EOF on %s\n", prog,
				    c1 == EOF ? n1 : n2);
			rc = rc ? rc : 1;
			break;
		}
		pos++;
		if (c1 != c2) {
			if (rc == 0)
				rc = 1;
			switch (mode) {
			case M_SILENT:
				goto done;
			case M_FIRST:
				printf("%s %s differ: char %llu, line %llu\n",
				    n1, n2, pos, line);
				goto done;
			case M_LIST_OCT:
				printf("%llu %o %o", pos, c1, c2);
				if (show_bytes) {
					putchar(' '); put_byte((unsigned)c1);
					putchar(' '); put_byte((unsigned)c2);
				}
				putchar('\n');
				break;
			case M_LIST_HEX:
				printf("%llu %x %x", pos, c1, c2);
				if (show_bytes) {
					putchar(' '); put_byte((unsigned)c1);
					putchar(' '); put_byte((unsigned)c2);
				}
				putchar('\n');
				break;
			}
		}
		if (c1 == '\n')
			line++;
	}

done:
	if (f1 != stdin) fclose(f1);
	if (f2 != stdin) fclose(f2);
	if (ferror(stdout))
		return 2;
	return rc;
}
