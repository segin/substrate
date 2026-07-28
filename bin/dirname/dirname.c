/*
 * dirname — report the directory portion of a pathname.
 *
 * Comprehensive POSIX + BSD + GNU.  Where BSD and GNU differ, BSD wins:
 *   - Option parsing stops at the first operand (BSD/POSIX getopt
 *     behaviour); GNU's argument permutation is NOT performed, so
 *     `dirname /a/b -z` treats "-z" as a third operand, not a flag.
 *
 * Supported everywhere:
 *   POSIX:  dirname string
 *   BSD:    dirname string                 (single operand)
 *   GNU:    dirname [-z|--zero] NAME...     (multiple operands; NUL sep)
 *           --help, --version
 *
 * The directory computation is POSIX dirname(3) (libgen), which BSD and
 * GNU agree on: strip the trailing slash(es) and the last component;
 * "" and "/"-only inputs yield ".", "/" stays "/".
 */
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *progname = "dirname";

static void usage(int status)
{
	FILE *f = status ? stderr : stdout;
	fprintf(f, "usage: %s [-z|--zero] string [string ...]\n", progname);
	exit(status);
}

int main(int argc, char *argv[])
{
	int zero = 0;
	int i = 1;

	/* Options end at "--" or at the first argument that is not an
	 * option (BSD/POSIX: no GNU permutation).  A lone "-" is an
	 * operand. */
	for (; i < argc; i++) {
		const char *a = argv[i];

		if (a[0] != '-' || a[1] == '\0')
			break;
		if (strcmp(a, "--") == 0) {
			i++;
			break;
		}
		if (strcmp(a, "--zero") == 0) {
			zero = 1;
			continue;
		}
		if (strcmp(a, "--help") == 0)
			usage(0);
		if (strcmp(a, "--version") == 0) {
			printf("dirname (substrate)\n");
			return 0;
		}
		if (a[1] == '-') {
			fprintf(stderr, "%s: unknown option %s\n", progname, a);
			usage(1);
		}
		/* clustered short flags, BSD-style (only -z defined) */
		for (const char *p = a + 1; *p; p++) {
			switch (*p) {
			case 'z':
				zero = 1;
				break;
			default:
				fprintf(stderr, "%s: illegal option -- %c\n",
					progname, *p);
				usage(1);
			}
		}
	}

	if (i >= argc) {
		fprintf(stderr, "%s: missing operand\n", progname);
		usage(1);
	}

	char sep = zero ? '\0' : '\n';

	for (; i < argc; i++) {
		/* dirname(3) may modify its argument, so work on a copy. */
		char *copy = strdup(argv[i]);
		if (!copy) {
			perror(progname);
			return 1;
		}
		fputs(dirname(copy), stdout);
		putchar(sep);
		free(copy);
	}

	/* Report an output error (e.g. `dirname foo > /dev/full`) rather than
	 * exiting 0 (DIRNAME-01). */
	if (fflush(stdout) != 0 || ferror(stdout)) {
		perror(progname);
		return 1;
	}
	return 0;
}
