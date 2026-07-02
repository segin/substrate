/*
 * df - report filesystem disk space usage
 *
 * POSIX.1-2024 + GNU coreutils + BSD extensions.
 * Conflict policy: BSD precedence.
 *
 *   df [-aHhikmPT] [-t type] [file...]
 *
 *   -a   include filesystems with 0 blocks (pseudo)        (GNU)
 *   -h   human-readable (1024-based: 1K 1M 1G ...)         (GNU/BSD)
 *   -H   human-readable (1000-based)                       (GNU)
 *   -i   show inode usage instead of block usage           (GNU/BSD)
 *   -k   1024-byte blocks (default)                        (POSIX)
 *   -m   1-megabyte blocks                                 (BSD/GNU)
 *   -P   POSIX-portable output (forces 1024-byte blocks)   (POSIX)
 *   -T   include the filesystem-type column                (GNU/BSD)
 *   -t type   only filesystems of this type                (GNU/BSD)
 *
 * With no file operands `df` lists every mount.  With file operands
 * `df` reports the filesystem containing each file (resolved to its
 * mountpoint via the longest matching prefix in /proc/mounts).
 *
 * /proc/mounts is consulted by default; the env var DF_MOUNTS_FILE
 * overrides that path for testing.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define DF_VERSION "df (Substrate) 1.0"

enum unit_mode { U_K, U_M, U_H, U_HSI };

static enum unit_mode  unit       = U_K;
static unsigned long   block_size = 1024;
static bool            opt_T      = false;
static bool            opt_i      = false;
static bool            opt_a      = false;
static bool            opt_P      = false;
static const char     *filter_t   = NULL;
static const char     *prog       = "df";

struct mount {
	char *source;
	char *target;
	char *type;
};

static struct mount *mounts;
static int           nmounts;

struct row {
	const char *source;
	const char *target;
	const char *type;
	bool        have;
	uint64_t    bsize;
	uint64_t    blocks;
	uint64_t    used;
	uint64_t    avail;
	unsigned    cap;
	uint64_t    inodes;
	uint64_t    iused;
	uint64_t    ifree;
	unsigned    icap;
};

static void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);
	if (!q) { fprintf(stderr, "%s: out of memory\n", prog); exit(2); }
	return q;
}

/* Unescape an octal-escaped /proc/mounts field (\040 = space etc.). */
static char *unescape(const char *s)
{
	size_t n = strlen(s);
	char *out = xrealloc(NULL, n + 1);
	size_t o = 0;
	for (size_t i = 0; i < n; ) {
		if (s[i] == '\\' && i + 3 < n &&
		    s[i+1] >= '0' && s[i+1] <= '7' &&
		    s[i+2] >= '0' && s[i+2] <= '7' &&
		    s[i+3] >= '0' && s[i+3] <= '7') {
			int v = ((s[i+1]-'0') << 6)
			      | ((s[i+2]-'0') << 3)
			      |  (s[i+3]-'0');
			out[o++] = (char)v;
			i += 4;
		} else {
			out[o++] = s[i++];
		}
	}
	out[o] = '\0';
	return out;
}

static int read_mounts(void)
{
	const char *path = getenv("DF_MOUNTS_FILE");
	if (!path) path = "/proc/mounts";
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
		return -1;
	}
	int cap = 16;
	mounts = xrealloc(NULL, (size_t)cap * sizeof(*mounts));
	char line[1024];
	while (fgets(line, sizeof line, f)) {
		char src[512], tgt[512], typ[64];
		if (sscanf(line, "%511s %511s %63s", src, tgt, typ) < 3)
			continue;
		if (nmounts == cap) {
			cap *= 2;
			mounts = xrealloc(mounts, (size_t)cap * sizeof(*mounts));
		}
		mounts[nmounts].source = unescape(src);
		mounts[nmounts].target = unescape(tgt);
		mounts[nmounts].type   = unescape(typ);
		nmounts++;
	}
	fclose(f);
	return 0;
}

static void fill_row(struct row *r, const struct mount *m)
{
	memset(r, 0, sizeof *r);
	r->source = m->source;
	r->target = m->target;
	r->type   = m->type;

	struct statvfs sv;
	if (statvfs(m->target, &sv) != 0)
		return;

	r->have   = true;
	r->bsize  = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
	r->blocks = sv.f_blocks;
	r->used   = sv.f_blocks > sv.f_bfree ? sv.f_blocks - sv.f_bfree : 0;
	r->avail  = sv.f_bavail;

	uint64_t denom = r->used + r->avail;
	r->cap = denom ? (unsigned)((100 * r->used + denom - 1) / denom) : 0;

	r->inodes = sv.f_files;
	r->iused  = sv.f_files > sv.f_ffree ? sv.f_files - sv.f_ffree : 0;
	r->ifree  = sv.f_favail;
	uint64_t id = r->iused + r->ifree;
	r->icap   = id ? (unsigned)((100 * r->iused + id - 1) / id) : 0;
}

static void human_fmt(uint64_t bytes, bool si, char *out, size_t sz)
{
	static const char suf[] = " KMGTPE";
	unsigned long div = si ? 1000 : 1024;
	int i = 0;
	double v = (double)bytes;
	while (v >= (double)div && suf[i + 1]) { v /= (double)div; i++; }
	if (i == 0)
		snprintf(out, sz, "%llu", (unsigned long long)bytes);
	else if (v < 10.0)
		snprintf(out, sz, "%.1f%c", v, suf[i]);
	else
		snprintf(out, sz, "%.0f%c", v, suf[i]);
}

static void fmt_blocks(uint64_t raw, uint64_t bsize, char *out, size_t sz)
{
	if (unit == U_H)        { human_fmt(raw * bsize, false, out, sz); return; }
	if (unit == U_HSI)      { human_fmt(raw * bsize, true,  out, sz); return; }
	snprintf(out, sz, "%llu",
	    (unsigned long long)((raw * bsize) / block_size));
}

static const char *blocks_header(void)
{
	if (unit == U_H || unit == U_HSI) return "Size";
	if (opt_P)                        return "1024-blocks";
	if (block_size == 1024)           return "1K-blocks";
	if (block_size == 1024 * 1024)    return "1M-blocks";
	if (block_size == 512)            return "512-blocks";
	return "blocks";
}

static void print_header(void)
{
	const char *avail = (unit == U_H || unit == U_HSI) ? "Avail" : "Available";
	const char *cap   = opt_P ? "Capacity" : "Use%";
	if (opt_i) {
		const char *icap = opt_P ? "ICapacity" : "IUse%";
		if (opt_T)
			printf("%-20s %-8s %10s %10s %10s %9s %s\n",
			    "Filesystem", "Type", "Inodes", "IUsed", "IFree",
			    icap, "Mounted on");
		else
			printf("%-20s %10s %10s %10s %9s %s\n",
			    "Filesystem", "Inodes", "IUsed", "IFree",
			    icap, "Mounted on");
		return;
	}
	if (opt_T)
		printf("%-20s %-8s %10s %10s %10s %9s %s\n",
		    "Filesystem", "Type", blocks_header(),
		    "Used", avail, cap, "Mounted on");
	else
		printf("%-20s %10s %10s %10s %9s %s\n",
		    "Filesystem", blocks_header(), "Used",
		    avail, cap, "Mounted on");
}

static void print_row(const struct row *r)
{
	char a[16], b[16], c[16], cap[8];

	if (opt_i) {
		if (r->have) {
			snprintf(a, sizeof a, "%llu", (unsigned long long)r->inodes);
			snprintf(b, sizeof b, "%llu", (unsigned long long)r->iused);
			snprintf(c, sizeof c, "%llu", (unsigned long long)r->ifree);
		} else {
			strlcpy(a, "-", sizeof a); strlcpy(b, "-", sizeof b); strlcpy(c, "-", sizeof c);
		}
		if (r->have && r->inodes > 0) snprintf(cap, sizeof cap, "%u%%", r->icap);
		else                          strlcpy(cap, "-", sizeof cap);
	} else {
		if (r->have) {
			fmt_blocks(r->blocks, r->bsize, a, sizeof a);
			fmt_blocks(r->used,   r->bsize, b, sizeof b);
			fmt_blocks(r->avail,  r->bsize, c, sizeof c);
		} else {
			strlcpy(a, "-", sizeof a); strlcpy(b, "-", sizeof b); strlcpy(c, "-", sizeof c);
		}
		if (r->have && r->blocks > 0) snprintf(cap, sizeof cap, "%u%%", r->cap);
		else                          strlcpy(cap, "-", sizeof cap);
	}

	if (opt_T)
		printf("%-20s %-8s %10s %10s %10s %9s %s\n",
		    r->source, r->type, a, b, c, cap, r->target);
	else
		printf("%-20s %10s %10s %10s %9s %s\n",
		    r->source, a, b, c, cap, r->target);
}

/* For a file argument, find the mount entry whose `target` is the
 * longest prefix of `path`.  Returns the index or -1. */
static int mount_for(const char *path)
{
	int best = -1;
	size_t bestlen = 0;
	for (int i = 0; i < nmounts; i++) {
		const char *t = mounts[i].target;
		size_t tl = strlen(t);
		if (tl == 0)
			continue;
		if (strncmp(path, t, tl) != 0)
			continue;
		bool at_boundary = path[tl] == '/' || path[tl] == '\0' ||
		    (tl == 1 && t[0] == '/');
		if (!at_boundary)
			continue;
		if (tl > bestlen) { best = i; bestlen = tl; }
	}
	return best;
}

static void usage(FILE *o)
{
	fprintf(o,
	    "usage: %s [-aHhikmPT] [-t type] [file...]\n", prog);
}

int main(int argc, char **argv)
{
	static const struct option lopt[] = {
		{ "all",             no_argument,       0, 'a' },
		{ "human-readable",  no_argument,       0, 'h' },
		{ "si",              no_argument,       0, 'H' },
		{ "inodes",          no_argument,       0, 'i' },
		{ "portability",     no_argument,       0, 'P' },
		{ "print-type",      no_argument,       0, 'T' },
		{ "type",            required_argument, 0, 't' },
		{ "help",            no_argument,       0, 1   },
		{ "version",         no_argument,       0, 2   },
		{ 0, 0, 0, 0 },
	};
	int c;
	while ((c = getopt_long(argc, argv, "aHhikmPTt:", lopt, NULL)) != -1) {
		switch (c) {
		case 'a': opt_a = true; break;
		case 'H': unit = U_HSI; break;
		case 'h': unit = U_H;   break;
		case 'i': opt_i = true; break;
		case 'k': unit = U_K; block_size = 1024;        break;
		case 'm': unit = U_K; block_size = 1024 * 1024; break;
		case 'P': opt_P = true; unit = U_K; block_size = 1024; break;
		case 'T': opt_T = true; break;
		case 't': filter_t = optarg; break;
		case 1:  usage(stdout); return 0;
		case 2:  puts(DF_VERSION); return 0;
		default: usage(stderr); return 1;
		}
	}

	if (read_mounts() < 0)
		return 1;

	print_header();

	if (optind == argc) {
		for (int i = 0; i < nmounts; i++) {
			if (filter_t && strcmp(mounts[i].type, filter_t) != 0)
				continue;
			struct row r;
			fill_row(&r, &mounts[i]);
			/* Skip 0-block pseudo filesystems unless -a. */
			if (!opt_a && r.have && r.blocks == 0)
				continue;
			print_row(&r);
		}
	} else {
		for (int i = optind; i < argc; i++) {
			char resolved[1024];
			if (!realpath(argv[i], resolved)) {
				strlcpy(resolved, argv[i], sizeof resolved);
			}
			int m = mount_for(resolved);
			if (m < 0) {
				fprintf(stderr, "%s: %s: no matching mount\n",
				    prog, argv[i]);
				continue;
			}
			if (filter_t && strcmp(mounts[m].type, filter_t) != 0)
				continue;
			struct row r;
			fill_row(&r, &mounts[m]);
			print_row(&r);
		}
	}

	return 0;
}
