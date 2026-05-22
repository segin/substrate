/*
 * fmt - simple text formatter
 *
 * BSD fmt + GNU coreutils fmt.  (fmt is not a POSIX utility.)
 * Conflict policy: BSD precedence (see docs/specs/diff_cmp_fold_fmt.md).
 *
 *   fmt [-cmnps] [-d chars] [-l num] [-t num] [-w width] [-g goal]
 *       [goal [maximum]] [file...]
 *
 *   -c   crown margin: keep the indent of the first two lines  (BSD/GNU)
 *   -m   pass mail-header lines through unchanged              (BSD)
 *   -n   also reformat lines beginning with '.'                (BSD)
 *   -p   indented paragraphs: an indent change starts a new one(BSD)
 *   -s   split-only: split long lines, never join short ones   (BSD/GNU)
 *   -d   set the sentence-ending character class               (BSD)
 *   -l   input tab width (default 8)                           (BSD)
 *   -t   output tab width (accepted; output uses spaces)       (BSD)
 *   -w   target line width / -g goal width                     (GNU)
 *   goal [maximum]   positional width operands                 (BSD)
 */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FMT_VERSION "fmt (Substrate) 1.0"

static int  goal = 65;
static bool opt_split  = false;
static bool opt_crown  = false;
static bool opt_indent = false;
static bool opt_fmtdot = false;
static bool opt_mail   = false;
static int  in_tab     = 8;
static const char *sent_chars = ".!?";
static const char *prog = "fmt";

/* ------------------------------------------------------------------ */
/* Input: every line is read with tabs expanded to spaces.            */

static char **lines = NULL;
static size_t nlines = 0, caplines = 0;

static void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n);
	if (!q) {
		fprintf(stderr, "%s: out of memory\n", prog);
		exit(1);
	}
	return q;
}

static void add_line(char *s)
{
	if (nlines == caplines) {
		caplines = caplines ? caplines * 2 : 64;
		lines = xrealloc(lines, caplines * sizeof(*lines));
	}
	lines[nlines++] = s;
}

static void read_all(FILE *f)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;
	int col = 0, c;
	bool pending = false;

	while ((c = getc(f)) != EOF) {
		pending = true;
		if (c == '\n') {
			if (len == cap) buf = xrealloc(buf, cap = cap + 1);
			buf[len] = '\0';
			add_line(buf);
			buf = NULL; len = cap = 0; col = 0; pending = false;
			continue;
		}
		int add = 1;
		char ch = (char)c;
		if (c == '\t') {
			add = in_tab - col % in_tab;
			ch = ' ';
		}
		for (int i = 0; i < add; i++) {
			if (len + 1 >= cap)
				buf = xrealloc(buf, cap = cap ? cap * 2 : 128);
			buf[len++] = ch;
			col++;
		}
	}
	if (pending) {
		if (len == cap) buf = xrealloc(buf, cap = cap + 1);
		buf[len] = '\0';
		add_line(buf);
	} else {
		free(buf);
	}
}

/* ------------------------------------------------------------------ */

static int line_indent(const char *s)
{
	int n = 0;
	while (s[n] == ' ')
		n++;
	return n;
}

static bool is_blank(const char *s)
{
	return s[line_indent(s)] == '\0';
}

static bool is_dotline(const char *s)
{
	return s[line_indent(s)] == '.';
}

static bool is_header(const char *s)
{
	if (!isalpha((unsigned char)s[0]))
		return false;
	for (const char *p = s; *p; p++) {
		if (*p == ':')
			return p != s;
		if (!isalnum((unsigned char)*p) && *p != '-')
			return false;
	}
	return false;
}

/* A word ends a sentence if its last non-closing char is in the
 * sentence-character class. */
static bool sentence_end(const char *w)
{
	size_t n = strlen(w);
	while (n > 0) {
		char c = w[n - 1];
		if (c == ')' || c == ']' || c == '"' || c == '\'')
			n--;
		else
			break;
	}
	return n > 0 && strchr(sent_chars, w[n - 1]) != NULL;
}

/* ------------------------------------------------------------------ */
/* Word list for the current paragraph.                               */

static char **words = NULL;
static size_t nwords = 0, capwords = 0;

static void add_word(const char *s, size_t n)
{
	if (nwords == capwords) {
		capwords = capwords ? capwords * 2 : 64;
		words = xrealloc(words, capwords * sizeof(*words));
	}
	char *w = xrealloc(NULL, n + 1);
	memcpy(w, s, n);
	w[n] = '\0';
	words[nwords++] = w;
}

static void split_words(const char *s)
{
	while (*s) {
		while (*s == ' ')
			s++;
		const char *start = s;
		while (*s && *s != ' ')
			s++;
		if (s > start)
			add_word(start, (size_t)(s - start));
	}
}

static void free_words(void)
{
	for (size_t i = 0; i < nwords; i++)
		free(words[i]);
	nwords = 0;
}

static void put_prefix(int n)
{
	while (n-- > 0)
		putchar(' ');
}

/* Greedy-pack the current word list into output lines.  The first
 * line is indented by p1, every later line by prest. */
static void emit_words(int p1, int prest)
{
	size_t i = 0;
	bool first_line = true;

	while (i < nwords) {
		int indent = first_line ? p1 : prest;
		int width = indent;
		size_t start = i;
		bool prev_sent = false;

		/* Always take at least one word. */
		width += (int)strlen(words[i]);
		prev_sent = sentence_end(words[i]);
		i++;
		while (i < nwords) {
			int sep = prev_sent ? 2 : 1;
			int wl = (int)strlen(words[i]);
			if (width + sep + wl > goal)
				break;
			width += sep + wl;
			prev_sent = sentence_end(words[i]);
			i++;
		}

		put_prefix(indent);
		bool ps = false;
		for (size_t k = start; k < i; k++) {
			if (k > start)
				fputs(ps ? "  " : " ", stdout);
			fputs(words[k], stdout);
			ps = sentence_end(words[k]);
		}
		putchar('\n');
		first_line = false;
	}
}

/* ------------------------------------------------------------------ */

static void format_normal(void)
{
	size_t i = 0;

	while (i < nlines) {
		const char *s = lines[i];

		if (is_blank(s)) {
			putchar('\n');
			i++;
			continue;
		}
		if (!opt_fmtdot && is_dotline(s)) {
			puts(s);
			i++;
			continue;
		}
		if (opt_mail && is_header(s)) {
			puts(s);
			i++;
			continue;
		}

		int first_indent = line_indent(lines[i]);
		int second_indent = -1;
		size_t j = i;

		while (j < nlines && !is_blank(lines[j])
		       && !(!opt_fmtdot && is_dotline(lines[j]))
		       && !(opt_mail && is_header(lines[j]))) {
			if (j > i && opt_indent
			    && line_indent(lines[j]) != first_indent)
				break;
			if (j == i + 1)
				second_indent = line_indent(lines[j]);
			split_words(lines[j] + line_indent(lines[j]));
			j++;
		}

		int p1 = first_indent;
		int prest = opt_crown
		    ? (second_indent >= 0 ? second_indent : first_indent)
		    : first_indent;
		emit_words(p1, prest);
		free_words();
		i = j;
	}
}

/* -s: split long lines only; never merge lines. */
static void format_split(void)
{
	for (size_t i = 0; i < nlines; i++) {
		const char *s = lines[i];
		if (is_blank(s)) {
			putchar('\n');
			continue;
		}
		int indent = line_indent(s);
		split_words(s + indent);
		emit_words(indent, indent);
		free_words();
	}
}

static void usage(FILE *o)
{
	fprintf(o, "usage: %s [-cmnps] [-d chars] [-l num] [-t num] "
	    "[-w width] [-g goal] [goal [maximum]] [file...]\n", prog);
}

static bool all_digits(const char *s)
{
	if (!*s)
		return false;
	for (; *s; s++)
		if (!isdigit((unsigned char)*s))
			return false;
	return true;
}

int main(int argc, char **argv)
{
	static const struct option lopt[] = {
		{ "crown-margin", no_argument,       0, 'c' },
		{ "split-only",   no_argument,       0, 's' },
		{ "width",        required_argument, 0, 'w' },
		{ "goal",         required_argument, 0, 'g' },
		{ "help",         no_argument,       0, 'H' },
		{ "version",      no_argument,       0, 'V' },
		{ 0, 0, 0, 0 },
	};
	int c;
	bool width_set = false;

	while ((c = getopt_long(argc, argv, "cmnpsd:l:t:w:g:", lopt, NULL))
	       != -1) {
		switch (c) {
		case 'c': opt_crown = true; break;
		case 'm': opt_mail = true; break;
		case 'n': opt_fmtdot = true; break;
		case 'p': opt_indent = true; break;
		case 's': opt_split = true; break;
		case 'd': sent_chars = optarg; break;
		case 'l': in_tab = atoi(optarg); if (in_tab < 1) in_tab = 8; break;
		case 't': break;	/* output tab width: output uses spaces */
		case 'w': goal = atoi(optarg); width_set = true; break;
		case 'g': goal = atoi(optarg); width_set = true; break;
		case 'H': usage(stdout); return 0;
		case 'V': puts(FMT_VERSION); return 0;
		default:  usage(stderr); return 1;
		}
	}

	int idx = optind;
	/* BSD positional width operands: leading numeric arg(s). */
	if (!width_set && idx < argc && all_digits(argv[idx])) {
		goal = atoi(argv[idx]);
		idx++;
		if (idx < argc && all_digits(argv[idx]))
			idx++;	/* maximum: accepted, packer targets goal */
	}
	if (goal < 1) {
		fprintf(stderr, "%s: width must be a positive integer\n", prog);
		return 1;
	}

	int rc = 0;
	if (idx >= argc) {
		read_all(stdin);
	} else {
		for (int i = idx; i < argc; i++) {
			FILE *f;
			if (strcmp(argv[i], "-") == 0) {
				f = stdin;
			} else if (!(f = fopen(argv[i], "r"))) {
				fprintf(stderr, "%s: %s: %s\n", prog,
				    argv[i], strerror(errno));
				rc = 1;
				continue;
			}
			read_all(f);
			if (f != stdin)
				fclose(f);
		}
	}

	if (opt_split)
		format_split();
	else
		format_normal();

	for (size_t i = 0; i < nlines; i++)
		free(lines[i]);
	free(lines);
	free(words);
	return rc;
}
