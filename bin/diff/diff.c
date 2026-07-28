/*
 * diff - compare two files line by line
 *
 * POSIX.1-2024 + GNU diffutils + BSD extensions.
 * Conflict policy: BSD precedence (see docs/specs/diff_cmp_fold_fmt.md).
 *
 *   diff [-c|-C n|-e|-f|-u|-U n|-q|-n|--normal] [-abdilrstwBN]
 *        [-I regexp] [--label name] file1 file2
 *
 * Algorithm: Myers O(ND) shortest-edit-script (greedy, with a
 * per-depth trace and a backtrack pass).
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "getopt.h"
#include <sys/stat.h>

#define DIFF_VERSION "diff (Substrate) 1.0"

enum format { F_NORMAL, F_UNIFIED, F_CONTEXT, F_ED, F_FORWARD, F_RCS, F_BRIEF };

static enum format format = F_NORMAL;
static int  context = 3;
static bool opt_ignore_case  = false;	/* -i */
static bool opt_ignore_ws    = false;	/* -w */
static bool opt_ignore_wsamt = false;	/* -b */
static bool opt_ignore_blank = false;	/* -B */
static bool opt_text         = false;	/* -a */
static bool opt_recursive    = false;	/* -r */
static bool opt_report_same  = false;	/* -s */
static bool opt_new_file     = false;	/* -N */
static bool opt_expand_tabs  = false;	/* -t */
static bool have_iregex      = false;	/* -I */
static regex_t iregex;
static const char *label1 = NULL;
static const char *label2 = NULL;
static const char *prog = "diff";
static int exitcode = 0;		/* 0 same, 1 differ, 2 trouble */

/* ------------------------------------------------------------------ */

struct file {
	char  *name;
	char  *raw;
	size_t rawlen;
	char **line;		/* line text, newline stripped */
	char **key;		/* comparison key per line */
	int    nlines;
	bool   has_nl;		/* file ends with a newline */
	bool   is_binary;
};

static void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p) { fprintf(stderr, "%s: out of memory\n", prog); exit(2); }
	return p;
}

static void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);
	if (!q) { fprintf(stderr, "%s: out of memory\n", prog); exit(2); }
	return q;
}

/* Build the comparison key for a line under -i/-w/-b. */
static char *make_key(const char *s)
{
	size_t n = strlen(s);
	char *k = xmalloc(n + 1);
	size_t o = 0;

	if (opt_ignore_ws) {
		for (size_t i = 0; i < n; i++)
			if (!isspace((unsigned char)s[i]))
				k[o++] = s[i];
	} else if (opt_ignore_wsamt) {
		size_t i = 0;
		while (i < n) {
			if (isspace((unsigned char)s[i])) {
				while (i < n && isspace((unsigned char)s[i]))
					i++;
				if (i < n)	/* drop trailing blanks */
					k[o++] = ' ';
			} else {
				k[o++] = s[i++];
			}
		}
	} else {
		memcpy(k, s, n);
		o = n;
	}
	k[o] = '\0';
	if (opt_ignore_case)
		for (size_t i = 0; i < o; i++)
			k[i] = (char)tolower((unsigned char)k[i]);
	return k;
}

/* Read a whole file (or stdin for "-") into struct file. */
static int read_file(struct file *F, const char *name)
{
	FILE *f;
	bool is_stdin = strcmp(name, "-") == 0;

	memset(F, 0, sizeof(*F));
	F->name = (char *)name;
	F->has_nl = true;

	f = is_stdin ? stdin : fopen(name, "rb");
	if (!f) {
		fprintf(stderr, "%s: %s: %s\n", prog, name, strerror(errno));
		return -1;
	}

	size_t cap = 4096;
	F->raw = xmalloc(cap);
	size_t got;
	while ((got = fread(F->raw + F->rawlen, 1, cap - F->rawlen, f)) > 0) {
		F->rawlen += got;
		if (F->rawlen == cap) {
			/* Cap the slurp: doubling at ~2 GiB wraps cap to 0 on the
			 * 32-bit target, so the next fread count (cap - rawlen)
			 * becomes a huge value and overflows the heap (DIFF-03). */
			if (cap > (512UL * 1024 * 1024)) {
				fprintf(stderr, "%s: %s: file too large\n", prog, name);
				if (!is_stdin) fclose(f);
				free(F->raw); F->raw = NULL;
				return -1;
			}
			cap *= 2;
			F->raw = xrealloc(F->raw, cap);
		}
	}
	/* Distinguish a read error from EOF so a truncated read isn't silently
	 * compared as the whole file (DIFF-08). */
	if (ferror(f)) {
		fprintf(stderr, "%s: %s: %s\n", prog, name, strerror(errno));
		if (!is_stdin) fclose(f);
		free(F->raw); F->raw = NULL;
		return -1;
	}
	if (!is_stdin)
		fclose(f);

	if (!opt_text && memchr(F->raw, '\0', F->rawlen))
		F->is_binary = true;

	/* Split into lines. */
	int caplines = 64;
	F->line = xmalloc((size_t)caplines * sizeof(char *));
	size_t start = 0;
	for (size_t i = 0; i < F->rawlen; i++) {
		if (F->raw[i] == '\n') {
			size_t len = i - start;
			char *ln = xmalloc(len + 1);
			memcpy(ln, F->raw + start, len);
			ln[len] = '\0';
			if (F->nlines == caplines) {
				caplines *= 2;
				F->line = xrealloc(F->line,
				    (size_t)caplines * sizeof(char *));
			}
			F->line[F->nlines++] = ln;
			start = i + 1;
		}
	}
	if (start < F->rawlen) {		/* trailing partial line */
		size_t len = F->rawlen - start;
		char *ln = xmalloc(len + 1);
		memcpy(ln, F->raw + start, len);
		ln[len] = '\0';
		if (F->nlines == caplines) {
			caplines *= 2;
			F->line = xrealloc(F->line,
			    (size_t)caplines * sizeof(char *));
		}
		F->line[F->nlines++] = ln;
		F->has_nl = false;
	}

	F->key = xmalloc((size_t)(F->nlines ? F->nlines : 1) * sizeof(char *));
	for (int i = 0; i < F->nlines; i++)
		F->key[i] = make_key(F->line[i]);

	/* A missing final newline is itself a difference: tag the last
	 * line's key so it cannot match a newline-terminated line. */
	if (!F->has_nl && F->nlines > 0) {
		char  *k  = F->key[F->nlines - 1];
		size_t kl = strlen(k);
		k = xrealloc(k, kl + 2);
		k[kl] = '\1';
		k[kl + 1] = '\0';
		F->key[F->nlines - 1] = k;
	}
	return 0;
}

static void free_file(struct file *F)
{
	for (int i = 0; i < F->nlines; i++) {
		free(F->line[i]);
		free(F->key[i]);
	}
	free(F->line);
	free(F->key);
	free(F->raw);
}

/* ------------------------------------------------------------------ */
/* Myers O(ND) diff.  Fills am[i] = matching B index or -1.            */

static void myers(struct file *A, struct file *B, int *am, int *bm)
{
	int N = A->nlines, M = B->nlines;

	for (int i = 0; i < N; i++) am[i] = -1;
	for (int j = 0; j < M; j++) bm[j] = -1;

	int MAX = N + M;
	if (MAX <= 0)
		return;

	size_t  vsz = (size_t)MAX * 2u + 1u;
	/*
	 * The O(ND) tracer keeps a vsz-int vector for every edit depth, i.e.
	 * ~(MAX+1)*vsz ints — quadratic in the file size (two dissimilar 10k-
	 * line files would need ~3.2 GB and OOM on the 32-bit target).  Guard
	 * the size_t multiplies (DIFF-02) and bound the total to a memory
	 * budget; when the pair is too large fall back to leaving am/bm all -1
	 * (already initialised), which yields a correct if non-minimal diff
	 * rather than OOMing (DIFF-01).
	 */
	if (vsz > SIZE_MAX / sizeof(int) ||
	    (size_t)(MAX + 1) > SIZE_MAX / sizeof(int *) ||
	    (size_t)(MAX + 1) > (128UL * 1024 * 1024) / (vsz * sizeof(int)))
		return;

	int     off = MAX;
	int    *V   = xmalloc(vsz * sizeof(int));
	for (size_t i = 0; i < vsz; i++) V[i] = 0;
	int **trace = xmalloc((size_t)(MAX + 1) * sizeof(int *));
	int   D = -1;

	for (int d = 0; d <= MAX; d++) {
		trace[d] = xmalloc(vsz * sizeof(int));
		memcpy(trace[d], V, vsz * sizeof(int));

		bool done = false;
		for (int k = -d; k <= d; k += 2) {
			int x;
			if (k == -d ||
			    (k != d && V[k - 1 + off] < V[k + 1 + off]))
				x = V[k + 1 + off];
			else
				x = V[k - 1 + off] + 1;
			int y = x - k;
			while (x < N && y < M &&
			       strcmp(A->key[x], B->key[y]) == 0) {
				x++; y++;
			}
			V[k + off] = x;
			if (x >= N && y >= M) { done = true; break; }
		}
		if (done) { D = d; break; }
	}

	/* Backtrack, recording diagonal matches. */
	int x = N, y = M;
	for (int d = D; d > 0 && (x > 0 || y > 0); d--) {
		int *v = trace[d];
		int k = x - y;
		int prev_k;
		if (k == -d || (k != d && v[k - 1 + off] < v[k + 1 + off]))
			prev_k = k + 1;
		else
			prev_k = k - 1;
		int prev_x = v[prev_k + off];
		int prev_y = prev_x - prev_k;

		while (x > prev_x && y > prev_y) {
			am[x - 1] = y - 1;
			bm[y - 1] = x - 1;
			x--; y--;
		}
		x = prev_x;
		y = prev_y;
	}
	while (x > 0 && y > 0) {		/* depth-0 diagonal */
		am[x - 1] = y - 1;
		bm[y - 1] = x - 1;
		x--; y--;
	}

	for (int d = 0; d <= (D < 0 ? MAX : D); d++)
		free(trace[d]);
	free(trace);
	free(V);
}

/* ------------------------------------------------------------------ */
/* Hunk extraction.                                                    */

struct hunk { int a0, a1, b0, b1; };

static struct hunk *extract(struct file *A, struct file *B,
                            int *am, int *bm, int *nh_out)
{
	int N = A->nlines, M = B->nlines;
	struct hunk *h = NULL;
	int nh = 0, cap = 0;
	int i = 0, j = 0;

	while (i < N || j < M) {
		if (i < N && j < M && am[i] == j) {
			i++; j++;
			continue;
		}
		int a0 = i, b0 = j;
		while (i < N && am[i] < 0) i++;
		while (j < M && bm[j] < 0) j++;

		/* -B / -I: skip a hunk whose every line is ignorable. */
		bool ignore = (opt_ignore_blank || have_iregex);
		if (ignore) {
			for (int p = a0; p < i && ignore; p++) {
				const char *s = A->line[p];
				if (opt_ignore_blank) {
					while (*s && isspace((unsigned char)*s)) s++;
					if (*s) ignore = false;
				} else if (regexec(&iregex, A->line[p],
				                   0, NULL, 0) != 0) {
					ignore = false;
				}
			}
			for (int p = b0; p < j && ignore; p++) {
				const char *s = B->line[p];
				if (opt_ignore_blank) {
					while (*s && isspace((unsigned char)*s)) s++;
					if (*s) ignore = false;
				} else if (regexec(&iregex, B->line[p],
				                   0, NULL, 0) != 0) {
					ignore = false;
				}
			}
		}
		if (ignore)
			continue;

		if (nh == cap) {
			cap = cap ? cap * 2 : 16;
			h = xrealloc(h, (size_t)cap * sizeof(*h));
		}
		h[nh].a0 = a0; h[nh].a1 = i;
		h[nh].b0 = b0; h[nh].b1 = j;
		nh++;
	}
	*nh_out = nh;
	return h;
}

/* ------------------------------------------------------------------ */
/* Emitters.                                                           */

static void put_line(const char *pfx, struct file *F, int idx)
{
	fputs(pfx, stdout);
	if (opt_expand_tabs) {
		int col = 0;
		for (const char *p = F->line[idx]; *p; p++) {
			if (*p == '\t') {
				int adv = 8 - col % 8;
				while (adv--) { putchar(' '); col++; }
			} else {
				putchar(*p); col++;
			}
		}
	} else {
		fputs(F->line[idx], stdout);
	}
	putchar('\n');
	if (idx == F->nlines - 1 && !F->has_nl)
		fputs("\\ No newline at end of file\n", stdout);
}

/* Normal-format line range: "N" or "N,M" (1-based inclusive). */
static void normal_range(int lo, int hi)	/* [lo,hi) 0-based */
{
	if (hi - lo == 1)
		printf("%d", lo + 1);
	else
		printf("%d,%d", lo + 1, hi);
}

static void emit_normal(struct file *A, struct file *B,
                        struct hunk *h, int nh)
{
	for (int n = 0; n < nh; n++) {
		int a0 = h[n].a0, a1 = h[n].a1, b0 = h[n].b0, b1 = h[n].b1;
		bool del = a1 > a0, ins = b1 > b0;

		if (del && ins) {
			normal_range(a0, a1); putchar('c'); normal_range(b0, b1);
			putchar('\n');
			for (int i = a0; i < a1; i++) put_line("< ", A, i);
			fputs("---\n", stdout);
			for (int j = b0; j < b1; j++) put_line("> ", B, j);
		} else if (del) {
			normal_range(a0, a1); printf("d%d\n", b0);
			for (int i = a0; i < a1; i++) put_line("< ", A, i);
		} else {
			printf("%da", a0); normal_range(b0, b1); putchar('\n');
			for (int j = b0; j < b1; j++) put_line("> ", B, j);
		}
	}
}

static void emit_ed(struct file *A, struct file *B, struct hunk *h, int nh)
{
	(void)A;
	for (int n = nh - 1; n >= 0; n--) {	/* reverse order */
		int a0 = h[n].a0, a1 = h[n].a1, b0 = h[n].b0, b1 = h[n].b1;
		bool del = a1 > a0, ins = b1 > b0;

		if (del && ins) {
			normal_range(a0, a1); fputs("c\n", stdout);
			for (int j = b0; j < b1; j++) puts(B->line[j]);
			fputs(".\n", stdout);
		} else if (del) {
			normal_range(a0, a1); fputs("d\n", stdout);
		} else {
			printf("%da\n", a0);
			for (int j = b0; j < b1; j++) puts(B->line[j]);
			fputs(".\n", stdout);
		}
	}
}

static void emit_forward(struct file *A, struct file *B,
                         struct hunk *h, int nh)
{
	(void)A;
	for (int n = 0; n < nh; n++) {
		int a0 = h[n].a0, a1 = h[n].a1, b0 = h[n].b0, b1 = h[n].b1;
		bool del = a1 > a0, ins = b1 > b0;
		char cmd = del && ins ? 'c' : del ? 'd' : 'a';

		if (cmd == 'a')
			printf("a%d\n", a0);
		else if (a1 - a0 == 1)
			printf("%c%d\n", cmd, a0 + 1);
		else
			printf("%c%d %d\n", cmd, a0 + 1, a1);
		if (cmd != 'd') {
			for (int j = b0; j < b1; j++) puts(B->line[j]);
			fputs(".\n", stdout);
		}
	}
}

static void emit_rcs(struct file *A, struct file *B, struct hunk *h, int nh)
{
	(void)A;
	for (int n = 0; n < nh; n++) {
		int a0 = h[n].a0, a1 = h[n].a1, b0 = h[n].b0, b1 = h[n].b1;
		if (a1 > a0)
			printf("d%d %d\n", a0 + 1, a1 - a0);
		if (b1 > b0) {
			printf("a%d %d\n", a1, b1 - b0);
			for (int j = b0; j < b1; j++) puts(B->line[j]);
		}
	}
}

/* Group hunks whose gap is <= 2*context for unified/context output. */
static int group_end(struct hunk *h, int nh, int start)
{
	int e = start;
	while (e + 1 < nh && h[e + 1].a0 - h[e].a1 <= 2 * context)
		e++;
	return e;
}

static void uni_range(int start0, int count)
{
	if (count == 1)
		printf("%d", start0 + 1);
	else
		printf("%d,%d", count ? start0 + 1 : start0, count);
}

static const char *disp_name(const char *real, const char *label)
{
	return label ? label : real;
}

static void emit_header2(const char *m1, const char *n1, const char *l1,
                         const char *m2, const char *n2, const char *l2)
{
	printf("%s %s\n", m1, disp_name(n1, l1));
	printf("%s %s\n", m2, disp_name(n2, l2));
}

static void emit_unified(struct file *A, struct file *B,
                         struct hunk *h, int nh)
{
	emit_header2("---", A->name, label1, "+++", B->name, label2);

	int g = 0;
	while (g < nh) {
		int e = group_end(h, nh, g);
		int a0 = h[g].a0 - context; if (a0 < 0) a0 = 0;
		int a1 = h[e].a1 + context; if (a1 > A->nlines) a1 = A->nlines;
		int b0 = h[g].b0 - context; if (b0 < 0) b0 = 0;
		int b1 = h[e].b1 + context; if (b1 > B->nlines) b1 = B->nlines;

		fputs("@@ -", stdout);
		uni_range(a0, a1 - a0);
		fputs(" +", stdout);
		uni_range(b0, b1 - b0);
		fputs(" @@\n", stdout);

		int i = a0, j = b0;
		for (int n = g; n <= e; n++) {
			while (i < h[n].a0) { put_line(" ", A, i); i++; j++; }
			while (i < h[n].a1) { put_line("-", A, i); i++; }
			while (j < h[n].b1) { put_line("+", B, j); j++; }
		}
		while (i < a1) { put_line(" ", A, i); i++; j++; }
		g = e + 1;
	}
}

static void emit_context(struct file *A, struct file *B,
                         struct hunk *h, int nh)
{
	emit_header2("***", A->name, label1, "---", B->name, label2);

	int g = 0;
	while (g < nh) {
		int e = group_end(h, nh, g);
		int a0 = h[g].a0 - context; if (a0 < 0) a0 = 0;
		int a1 = h[e].a1 + context; if (a1 > A->nlines) a1 = A->nlines;
		int b0 = h[g].b0 - context; if (b0 < 0) b0 = 0;
		int b1 = h[e].b1 + context; if (b1 > B->nlines) b1 = B->nlines;

		fputs("***************\n", stdout);
		fputs("*** ", stdout);
		printf("%d,%d", a0 + 1, a1); fputs(" ****\n", stdout);
		for (int n = g, i = a0; n <= e || i < a1; ) {
			if (n <= e && i == h[n].a0 && h[n].a1 == h[n].a0) {
				n++; continue;
			}
			if (n <= e && i >= h[n].a0 && i < h[n].a1) {
				const char *p = h[n].b1 > h[n].b0 ? "! " : "- ";
				put_line(p, A, i); i++;
				if (i == h[n].a1) n++;
			} else if (i < a1) {
				put_line("  ", A, i); i++;
			} else {
				n++;
			}
		}
		fputs("--- ", stdout);
		printf("%d,%d", b0 + 1, b1); fputs(" ----\n", stdout);
		for (int n = g, j = b0; n <= e || j < b1; ) {
			if (n <= e && j == h[n].b0 && h[n].b1 == h[n].b0) {
				n++; continue;
			}
			if (n <= e && j >= h[n].b0 && j < h[n].b1) {
				const char *p = h[n].a1 > h[n].a0 ? "! " : "+ ";
				put_line(p, B, j); j++;
				if (j == h[n].b1) n++;
			} else if (j < b1) {
				put_line("  ", B, j); j++;
			} else {
				n++;
			}
		}
		g = e + 1;
	}
}

/* ------------------------------------------------------------------ */

/* Compare two regular files.  Returns 0 same, 1 differ, 2 trouble. */
static int diff_files(const char *n1, const char *n2)
{
	struct file A, B;

	if (read_file(&A, n1) < 0)
		return 2;
	if (read_file(&B, n2) < 0) { free_file(&A); return 2; }

	int rc = 0;

	if ((A.is_binary || B.is_binary)) {
		bool same = A.rawlen == B.rawlen &&
		    memcmp(A.raw, B.raw, A.rawlen) == 0;
		if (!same) {
			printf("Binary files %s and %s differ\n", n1, n2);
			rc = 1;
		} else if (opt_report_same) {
			printf("Files %s and %s are identical\n", n1, n2);
		}
		free_file(&A); free_file(&B);
		return rc;
	}

	int *am = xmalloc((size_t)(A.nlines ? A.nlines : 1) * sizeof(int));
	int *bm = xmalloc((size_t)(B.nlines ? B.nlines : 1) * sizeof(int));
	myers(&A, &B, am, bm);

	int nh;
	struct hunk *h = extract(&A, &B, am, bm, &nh);

	if (nh == 0) {
		if (opt_report_same)
			printf("Files %s and %s are identical\n", n1, n2);
	} else {
		rc = 1;
		switch (format) {
		case F_BRIEF:
			printf("Files %s and %s differ\n", n1, n2);
			break;
		case F_NORMAL:  emit_normal(&A, &B, h, nh);  break;
		case F_UNIFIED: emit_unified(&A, &B, h, nh); break;
		case F_CONTEXT: emit_context(&A, &B, h, nh); break;
		case F_ED:      emit_ed(&A, &B, h, nh);      break;
		case F_FORWARD: emit_forward(&A, &B, h, nh); break;
		case F_RCS:     emit_rcs(&A, &B, h, nh);     break;
		}
	}

	free(h); free(am); free(bm);
	free_file(&A); free_file(&B);
	return rc;
}

/* ------------------------------------------------------------------ */
/* Directory handling.                                                 */

#define DIFF_MAX_DEPTH 64
static int dir_depth = 0;

static int is_dir(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int exists(const char *p)
{
	struct stat st;
	return strcmp(p, "-") == 0 || stat(p, &st) == 0;
}

static int cmp_str(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static char **list_dir(const char *d, int *n_out)
{
	DIR *dp = opendir(d);
	if (!dp) {
		fprintf(stderr, "%s: %s: %s\n", prog, d, strerror(errno));
		*n_out = -1;
		return NULL;
	}
	char **names = NULL;
	int n = 0, cap = 0;
	struct dirent *e;
	while ((e = readdir(dp)) != NULL) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
			continue;
		if (n == cap) {
			cap = cap ? cap * 2 : 32;
			names = xrealloc(names, (size_t)cap * sizeof(char *));
		}
		names[n] = strdup(e->d_name);         /* was unchecked (DIFF-07) */
		if (!names[n]) {
			fprintf(stderr, "%s: out of memory\n", prog);
			for (int k = 0; k < n; k++) free(names[k]);
			free(names);
			closedir(dp);
			*n_out = -1;
			return NULL;
		}
		n++;
	}
	closedir(dp);
	if (n > 0)                                 /* qsort(NULL,0,...) is UB (DIFF-10) */
		qsort(names, (size_t)n, sizeof(char *), cmp_str);
	*n_out = n;
	return names;
}

static char *join(const char *d, const char *n)
{
	size_t ld = strlen(d);
	bool slash = ld > 0 && d[ld - 1] == '/';
	size_t len = ld + strlen(n) + 2;
	char *p = xmalloc(len);
	snprintf(p, len, "%s%s%s", d, slash ? "" : "/", n);
	return p;
}

static int diff_path(const char *p1, const char *p2);

static int diff_dirs(const char *d1, const char *d2)
{
	int n1, n2;
	char **e1 = list_dir(d1, &n1);
	char **e2 = list_dir(d2, &n2);
	if (n1 < 0 || n2 < 0) {
		exitcode = 2;
		return 2;
	}

	int rc = 0, i = 0, j = 0;
	while (i < n1 || j < n2) {
		int c;
		if (i >= n1)      c = 1;
		else if (j >= n2) c = -1;
		else              c = strcmp(e1[i], e2[j]);

		if (c < 0) {
			if (opt_new_file) {
				char *p1 = join(d1, e1[i]);
				char *p2 = join(d2, e1[i]);
				if (!is_dir(p1)) {
					int r = diff_files(p1, "/dev/null");
					if (r > rc) rc = r;
				}
				free(p1); free(p2);
			} else {
				printf("Only in %s: %s\n", d1, e1[i]);
				if (rc < 1) rc = 1;
			}
			i++;
		} else if (c > 0) {
			if (opt_new_file) {
				char *p2 = join(d2, e2[j]);
				if (!is_dir(p2)) {
					int r = diff_files("/dev/null", p2);
					if (r > rc) rc = r;
				}
				free(p2);
			} else {
				printf("Only in %s: %s\n", d2, e2[j]);
				if (rc < 1) rc = 1;
			}
			j++;
		} else {
			char *p1 = join(d1, e1[i]);
			char *p2 = join(d2, e2[j]);
			int r = diff_path(p1, p2);
			if (r > rc) rc = r;
			free(p1); free(p2);
			i++; j++;
		}
	}
	for (int k = 0; k < n1; k++) free(e1[k]);
	for (int k = 0; k < n2; k++) free(e2[k]);
	free(e1); free(e2);
	if (rc > exitcode) exitcode = rc;
	return rc;
}

/* Dispatch on whether the operands are files or directories. */
static int diff_path(const char *p1, const char *p2)
{
	bool d1 = is_dir(p1), d2 = is_dir(p2);

	if (d1 && d2) {
		if (!opt_recursive) {
			printf("Common subdirectories: %s and %s\n", p1, p2);
			return 0;
		}
		/* Cap recursion depth so a symlink loop (a directory that contains
		 * a symlink back to an ancestor) cannot recurse until the C stack
		 * overflows (DIFF-04). */
		if (dir_depth >= DIFF_MAX_DEPTH) {
			fprintf(stderr, "%s: %s: directory nesting too deep\n", prog, p1);
			exitcode = 2;
			return 2;
		}
		dir_depth++;
		int r = diff_dirs(p1, p2);
		dir_depth--;
		return r;
	}
	if (d1 != d2) {
		fprintf(stderr,
		    "%s: %s is a directory and %s is not\n",
		    prog, d1 ? p1 : p2, d1 ? p2 : p1);
		exitcode = 2;
		return 2;
	}

	if (format != F_BRIEF && format != F_ED && format != F_FORWARD &&
	    format != F_RCS && opt_recursive)
		printf("diff %s %s\n", p1, p2);

	int r = diff_files(p1, p2);
	if (r > exitcode) exitcode = r;
	return r;
}

/* ------------------------------------------------------------------ */

static void usage(FILE *o)
{
	fprintf(o,
	    "usage: %s [-cefnqrsuabitwBN] [-C n] [-U n] [-I regexp] "
	    "[--label name] file1 file2\n", prog);
}

/* Parse a context-line count: reject garbage/negatives and clamp so that
 * 2*context (used in hunk grouping) cannot overflow int (DIFF-06). */
static int parse_context(const char *s)
{
	char *e;
	long  v;
	errno = 0;
	v = strtol(s, &e, 10);
	if (e == s || *e != '\0' || errno == ERANGE || v < 0 || v > (1 << 20)) {
		fprintf(stderr, "%s: invalid context length '%s'\n", prog, s);
		exit(2);
	}
	return (int)v;
}

int main(int argc, char **argv)
{
	static const struct option lopt[] = {
		{ "normal",                 no_argument,       0, 1   },
		{ "brief",                  no_argument,       0, 'q' },
		{ "recursive",              no_argument,       0, 'r' },
		{ "report-identical-files", no_argument,       0, 's' },
		{ "new-file",               no_argument,       0, 'N' },
		{ "text",                   no_argument,       0, 'a' },
		{ "ignore-case",            no_argument,       0, 'i' },
		{ "ignore-all-space",       no_argument,       0, 'w' },
		{ "ignore-space-change",    no_argument,       0, 'b' },
		{ "ignore-blank-lines",     no_argument,       0, 'B' },
		{ "expand-tabs",            no_argument,       0, 't' },
		{ "unified",                optional_argument, 0, 2   },
		{ "context",                optional_argument, 0, 3   },
		{ "ed",                     no_argument,       0, 'e' },
		{ "rcs",                    no_argument,       0, 'n' },
		{ "label",                  required_argument, 0, 'L' },
		{ "help",                   no_argument,       0, 'H' },
		{ "version",                no_argument,       0, 'V' },
		{ 0, 0, 0, 0 },
	};
	int c;

	while ((c = getopt_long(argc, argv, "abC:cefiI:nqrstuU:wBNL:",
	                        lopt, NULL)) != -1) {
		switch (c) {
		case 'a': opt_text = true; break;
		case 'b': opt_ignore_wsamt = true; break;
		case 'c': format = F_CONTEXT; break;
		case 'C': format = F_CONTEXT; context = parse_context(optarg); break;
		case 'e': format = F_ED; break;
		case 'f': format = F_FORWARD; break;
		case 'i': opt_ignore_case = true; break;
		case 'n': format = F_RCS; break;
		case 'q': format = F_BRIEF; break;
		case 'r': opt_recursive = true; break;
		case 's': opt_report_same = true; break;
		case 't': opt_expand_tabs = true; break;
		case 'u': format = F_UNIFIED; break;
		case 'U': format = F_UNIFIED; context = parse_context(optarg); break;
		case 'w': opt_ignore_ws = true; break;
		case 'B': opt_ignore_blank = true; break;
		case 'N': opt_new_file = true; break;
		case 'I':
			if (regcomp(&iregex, optarg, REG_EXTENDED | REG_NOSUB)) {
				fprintf(stderr, "%s: bad regexp: %s\n",
				    prog, optarg);
				return 2;
			}
			have_iregex = true;
			break;
		case 'L':
			if (!label1) label1 = optarg;
			else         label2 = optarg;
			break;
		case 1: format = F_NORMAL; break;
		case 2:
			format = F_UNIFIED;
			if (optarg) context = parse_context(optarg);
			break;
		case 3:
			format = F_CONTEXT;
			if (optarg) context = parse_context(optarg);
			break;
		case 'H': usage(stdout); return 0;
		case 'V': puts(DIFF_VERSION); return 0;
		default:  usage(stderr); return 2;
		}
	}
	if (context < 0)
		context = 0;

	if (argc - optind != 2) {
		usage(stderr);
		return 2;
	}
	const char *f1 = argv[optind];
	const char *f2 = argv[optind + 1];

	/* If exactly one operand is a directory, look up the other's
	 * basename inside it. */
	char *alloc1 = NULL, *alloc2 = NULL;
	if (is_dir(f1) && !is_dir(f2) && strcmp(f2, "-") != 0) {
		const char *base = strrchr(f2, '/');
		base = base ? base + 1 : f2;
		alloc1 = join(f1, base);
		f1 = alloc1;
	} else if (is_dir(f2) && !is_dir(f1) && strcmp(f1, "-") != 0) {
		const char *base = strrchr(f1, '/');
		base = base ? base + 1 : f1;
		alloc2 = join(f2, base);
		f2 = alloc2;
	}

	if (!exists(f1) && opt_new_file) f1 = "/dev/null";
	if (!exists(f2) && opt_new_file) f2 = "/dev/null";

	diff_path(f1, f2);

	free(alloc1);
	free(alloc2);
	if (have_iregex)
		regfree(&iregex);
	return exitcode;
}
