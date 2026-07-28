/*
 * readlink — print the target of a symbolic link, optionally canonicalized.
 *
 * Comprehensive POSIX + BSD + GNU.  Where BSD and GNU differ, BSD wins:
 *   - Option parsing stops at the first operand (BSD/POSIX getopt
 *     behaviour); GNU's argument permutation is NOT performed.
 *   - -f canonicalizes the whole path, following every symlink and
 *     allowing the final component to be missing (FreeBSD realpath
 *     semantics) — which is also where GNU's -f lands.
 *   - -n suppresses the trailing separator entirely (BSD); with several
 *     operands the targets are emitted with no separators at all.
 *   - Errors are silent by default (both BSD and GNU); -v re-enables them.
 *
 * Modes:
 *   (default)  print the raw link contents (readlink(2)); fail on a
 *              non-symlink.  This is the POSIX/BSD/GNU common behaviour.
 *   -f, --canonicalize            resolve all symlinks; all but the last
 *                                 component must exist.
 *   -e, --canonicalize-existing   like -f, but every component must exist.
 *   -m, --canonicalize-missing    like -f, but no component need exist.
 * Flags:
 *   -n, --no-newline    do not print the trailing separator.
 *   -z, --zero          separate output with NUL instead of newline.
 *   -q, --quiet / -s, --silent   suppress error messages (default).
 *   -v, --verbose       report errors.
 *   --, --help, --version
 *   Multiple FILE operands are accepted (BSD and GNU).
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define MAXLINKS 40

enum { M_NONE, M_CANON_F, M_CANON_E, M_CANON_M };

static const char *progname = "readlink";
static int verbose;

static void usage(int status)
{
	FILE *f = status ? stderr : stdout;
	fprintf(f, "usage: %s [-femnqsvz] [--] file [file ...]\n", progname);
	exit(status);
}

static void diag(const char *path)
{
	if (verbose)
		fprintf(stderr, "%s: %s: %s\n", progname, path, strerror(errno));
}

/*
 * Canonicalize NAME into RESOLVED (must hold PATH_MAX bytes) per MODE.
 * Walks the path component by component from an absolute base, resolving
 * "."/".."/symlinks, and applies the per-mode existence policy.  Returns
 * 0 on success, -1 (with errno set) on failure.
 */
static int canon(const char *name, int mode, char *resolved)
{
	char result[PATH_MAX];
	char rest[PATH_MAX * 2];
	int nlinks = 0;
	int missing = 0;	/* set once we pass a non-existent component (-m) */

	int n;
	if (name[0] == '/') {
		result[0] = '/';
		result[1] = '\0';
		n = snprintf(rest, sizeof rest, "%s", name + 1);
	} else {
		if (!getcwd(result, sizeof result))
			return -1;
		n = snprintf(rest, sizeof rest, "%s", name);
	}
	/* A silently-truncated snprintf would defeat the ENAMETOOLONG guard
	 * below and canonicalize a wrong (truncated) path (READLINK). */
	if (n < 0 || (size_t)n >= sizeof rest) {
		errno = ENAMETOOLONG;
		return -1;
	}

	while (*rest) {
		/* peel the next component off the front of rest */
		char *s = rest;
		while (*s == '/')
			s++;
		if (!*s) {
			memmove(rest, s, strlen(s) + 1);
			break;
		}
		char *e = s;
		while (*e && *e != '/')
			e++;

		char comp[PATH_MAX];
		size_t clen = (size_t)(e - s);
		if (clen >= sizeof comp) {
			errno = ENAMETOOLONG;
			return -1;
		}
		memcpy(comp, s, clen);
		comp[clen] = '\0';
		memmove(rest, e, strlen(e) + 1);	/* tail (keeps leading '/') */

		if (strcmp(comp, ".") == 0)
			continue;
		if (strcmp(comp, "..") == 0) {
			char *slash = strrchr(result, '/');
			if (slash && slash != result)
				*slash = '\0';
			else
				result[1] = '\0';	/* collapse to "/" */
			continue;
		}

		char cand[PATH_MAX];
		int  cn;
		if (strcmp(result, "/") == 0)
			cn = snprintf(cand, sizeof cand, "/%s", comp);
		else
			cn = snprintf(cand, sizeof cand, "%s/%s", result, comp);
		if (cn < 0 || (size_t)cn >= sizeof cand) {
			errno = ENAMETOOLONG;
			return -1;
		}

		if (missing) {		/* -m, past the first missing component */
			if (strlcpy(result, cand, sizeof result) >= sizeof result) {
				errno = ENAMETOOLONG;
				return -1;
			}
			continue;
		}

		struct stat st;
		if (lstat(cand, &st) < 0) {
			if (mode == M_CANON_E)
				return -1;		/* every component must exist */
			if (mode == M_CANON_F && *rest)
				return -1;		/* only the last may be absent */
			if (strlcpy(result, cand, sizeof result) >= sizeof result) {
				errno = ENAMETOOLONG;
				return -1;
			}
			if (mode == M_CANON_M)
				missing = 1;
			continue;
		}

		if (S_ISLNK(st.st_mode)) {
			if (++nlinks > MAXLINKS) {
				errno = ELOOP;
				return -1;
			}
			char link[PATH_MAX];
			ssize_t n = readlink(cand, link, sizeof link - 1);
			if (n < 0)
				return -1;
			link[n] = '\0';

			/* Re-inject the link in front of the remaining path. */
			char merged[PATH_MAX * 2];
			if (*rest)
				snprintf(merged, sizeof merged, "%s/%s", link, rest);
			else
				snprintf(merged, sizeof merged, "%s", link);
			snprintf(rest, sizeof rest, "%s", merged);

			/* Absolute link restarts from root; a relative link is
			 * relative to cand's parent, which is the unchanged
			 * `result`. */
			if (link[0] == '/') {
				result[0] = '/';
				result[1] = '\0';
			}
			continue;
		}

		/* ordinary file or directory */
		if (strlcpy(result, cand, sizeof result) >= sizeof result) {
			errno = ENAMETOOLONG;
			return -1;
		}
	}

	snprintf(resolved, PATH_MAX, "%s", result);
	return 0;
}

int main(int argc, char *argv[])
{
	int mode = M_NONE;
	int no_newline = 0, zero = 0;
	int i = 1;

	for (; i < argc; i++) {
		const char *a = argv[i];

		if (a[0] != '-' || a[1] == '\0')
			break;			/* operand (incl. lone "-") */
		if (strcmp(a, "--") == 0) {
			i++;
			break;
		}
		if (a[1] == '-') {		/* long option (GNU) */
			if (!strcmp(a, "--canonicalize"))
				mode = M_CANON_F;
			else if (!strcmp(a, "--canonicalize-existing"))
				mode = M_CANON_E;
			else if (!strcmp(a, "--canonicalize-missing"))
				mode = M_CANON_M;
			else if (!strcmp(a, "--no-newline"))
				no_newline = 1;
			else if (!strcmp(a, "--zero"))
				zero = 1;
			else if (!strcmp(a, "--quiet") || !strcmp(a, "--silent"))
				verbose = 0;
			else if (!strcmp(a, "--verbose"))
				verbose = 1;
			else if (!strcmp(a, "--help"))
				usage(0);
			else if (!strcmp(a, "--version")) {
				printf("readlink (substrate)\n");
				return 0;
			} else {
				fprintf(stderr, "%s: unknown option %s\n",
					progname, a);
				usage(1);
			}
			continue;
		}
		for (const char *p = a + 1; *p; p++) {	/* clustered short flags */
			switch (*p) {
			case 'f': mode = M_CANON_F; break;
			case 'e': mode = M_CANON_E; break;
			case 'm': mode = M_CANON_M; break;
			case 'n': no_newline = 1; break;
			case 'z': zero = 1; break;
			case 'q': case 's': verbose = 0; break;
			case 'v': verbose = 1; break;
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
	int rc = 0;

	for (; i < argc; i++) {
		const char *path = argv[i];
		char out[PATH_MAX];
		int ok;

		if (mode == M_NONE) {
			ssize_t n = readlink(path, out, sizeof out - 1);
			if (n < 0) {
				diag(path);
				ok = 0;
			} else {
				out[n] = '\0';
				ok = 1;
			}
		} else {
			ok = (canon(path, mode, out) == 0);
			if (!ok)
				diag(path);
		}

		if (ok) {
			fputs(out, stdout);
			if (!no_newline)
				putchar(sep);
		} else {
			rc = 1;
		}
	}

	return rc;
}
