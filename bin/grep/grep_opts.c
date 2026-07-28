/*
 * grep_opts.c - argument parsing, program-name dialect selection, and the
 * --help/--version surfaces for Substrate grep.
 *
 * The parser is hand-rolled (rather than getopt_long) so it can: permute
 * options and operands GNU/BSD-style, accept the `-NUM` context shorthand,
 * support the optional argument of --color, and apply last-wins dialect
 * selection (REQ-GREP-001..009, 020..023, 105).
 */
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "grep.h"

static const char *base_name(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            b = p + 1;
    return b;
}

void grep_ctx_init(struct grep_ctx *g, const char *argv0)
{
    memset(g, 0, sizeof(*g));
    g->progname = (argv0 && argv0[0]) ? base_name(argv0) : "grep";
    g->dialect = GREP_BRE;
    g->max_count = -1;
    g->delim = '\n';
    g->color = GREP_COLOR_NEVER;
    g->diraction = GREP_DIR_SKIP;
    g->binary = GREP_BIN_BINARY;
    g->before = 0;
    g->after = 0;

    /* Program-name dialect default (REQ-GREP-004/005). */
    const char *b = g->progname;
    if (strcmp(b, "egrep") == 0)
        g->dialect = GREP_ERE;
    else if (strcmp(b, "fgrep") == 0)
        g->dialect = GREP_FIXED;
}

void grep_print_usage(FILE *out, const char *progname)
{
    fprintf(out,
        "Usage: %s [OPTION]... PATTERN [FILE]...\n"
        "       %s [OPTION]... -e PATTERN ... [FILE]...\n"
        "       %s [OPTION]... -f PATTERN_FILE ... [FILE]...\n",
        progname, progname, progname);
}

void grep_print_help(const char *progname)
{
    grep_print_usage(stdout, progname);
    fputs(
        "\nPattern selection and interpretation:\n"
        "  -E, --extended-regexp     PATTERN is an extended regular expression\n"
        "  -F, --fixed-strings       PATTERN is a set of literal strings\n"
        "  -G, --basic-regexp        PATTERN is a basic regular expression\n"
        "  -e, --regexp=PATTERN      use PATTERN for matching\n"
        "  -f, --file=FILE           take patterns from FILE, one per line\n"
        "  -i, --ignore-case         ignore case distinctions\n"
        "  -w, --word-regexp         match only whole words\n"
        "  -x, --line-regexp         match only whole lines\n"
        "  -v, --invert-match        select non-matching lines\n"
        "\nMiscellaneous:\n"
        "  -s, --no-messages         suppress error messages\n"
        "  -z, --null-data           a data line ends in 0 byte, not newline\n"
        "      --help                display this help and exit\n"
        "  -V, --version             display version information and exit\n"
        "\nOutput control:\n"
        "  -m, --max-count=NUM       stop after NUM selected lines\n"
        "  -b, --byte-offset         print the byte offset with output lines\n"
        "  -n, --line-number         print line number with output lines\n"
        "  -H, --with-filename       print file name with output lines\n"
        "  -h, --no-filename         suppress the file name prefix on output\n"
        "      --label=LABEL         use LABEL as the standard input file name\n"
        "  -o, --only-matching       show only the part of a line matching PATTERN\n"
        "  -q, --quiet, --silent     suppress all normal output\n"
        "      --binary-files=TYPE   assume that binary files are TYPE;\n"
        "                            TYPE is 'binary', 'text', or 'without-match'\n"
        "  -a, --text                equivalent to --binary-files=text\n"
        "  -I                        equivalent to --binary-files=without-match\n"
        "  -d, --directories=ACTION  how to handle directories;\n"
        "                            ACTION is 'read', 'recurse', or 'skip'\n"
        "  -r, --recursive           search directories recursively\n"
        "  -R                        likewise\n"
        "      --include=GLOB        search only files that match GLOB\n"
        "      --exclude=GLOB        skip files that match GLOB\n"
        "  -L, --files-without-match print only names of FILEs with no selected lines\n"
        "  -l, --files-with-matches  print only names of FILEs with selected lines\n"
        "  -c, --count               print only a count of selected lines per FILE\n"
        "      --color[=WHEN],\n"
        "      --colour[=WHEN]       use markers to highlight matches;\n"
        "                            WHEN is 'always', 'never', or 'auto'\n"
        "\nContext control:\n"
        "  -B, --before-context=NUM  print NUM lines of leading context\n"
        "  -A, --after-context=NUM   print NUM lines of trailing context\n"
        "  -C, --context=NUM         print NUM lines of output context\n"
        "  -NUM                      same as --context=NUM\n",
        stdout);
}

void grep_print_version(void)
{
    puts(GREP_VERSION);
}

/* Parse a non-negative integer option argument. */
static int parse_count(const char *s, long *out)
{
    if (!s || !*s)
        return -1;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0)
        return -1;
    *out = v;
    return 0;
}

static int set_binary_type(struct grep_ctx *g, const char *t)
{
    if (strcmp(t, "binary") == 0)            g->binary = GREP_BIN_BINARY;
    else if (strcmp(t, "text") == 0)         g->binary = GREP_BIN_TEXT;
    else if (strcmp(t, "without-match") == 0) g->binary = GREP_BIN_WITHOUT_MATCH;
    else return -1;
    return 0;
}

static int set_directories(struct grep_ctx *g, const char *a)
{
    if (strcmp(a, "read") == 0)         g->diraction = GREP_DIR_READ;
    else if (strcmp(a, "skip") == 0)    g->diraction = GREP_DIR_SKIP;
    else if (strcmp(a, "recurse") == 0) { g->diraction = GREP_DIR_RECURSE;
                                          g->recursive = true; }
    else return -1;
    return 0;
}

static int set_color(struct grep_ctx *g, const char *w)
{
    if (!w || strcmp(w, "auto") == 0)   g->color = GREP_COLOR_AUTO;
    else if (strcmp(w, "always") == 0)  g->color = GREP_COLOR_ALWAYS;
    else if (strcmp(w, "never") == 0)   g->color = GREP_COLOR_NEVER;
    else return -1;
    return 0;
}

static int add_glob(const char ***vec, size_t *n, const char *glob,
                    const char **errmsg)
{
    const char **nv = realloc((void *)*vec, (*n + 1) * sizeof(*nv));
    if (!nv) { *errmsg = "out of memory"; return -1; }
    *vec = nv;
    (*vec)[(*n)++] = glob;
    return 0;
}

/* Apply an -f file; on failure report and signal a hard (exit-2) error. */
static int apply_file(struct grep_ctx *g, const char *path, const char **errmsg)
{
    const char *e = NULL;
    if (grep_add_pattern_file(g, path, &e) != 0) {
        fprintf(stderr, "%s: %s: %s\n", g->progname, path,
                e ? e : "cannot read pattern file");
        *errmsg = NULL;          /* already reported */
        return -1;
    }
    return 0;
}

struct longopt {
    const char *name;
    int has_arg;     /* 0 none, 1 required, 2 optional */
    int val;         /* synthetic short-equivalent, or 0x100+ for long-only */
};

enum {
    LO_HELP = 0x100, LO_LABEL, LO_INCLUDE, LO_EXCLUDE, LO_COLOR,
    LO_BINARY_FILES, LO_BASIC
};

static const struct longopt longopts[] = {
    { "extended-regexp",   0, 'E' },
    { "fixed-strings",     0, 'F' },
    { "basic-regexp",      0, LO_BASIC },
    { "regexp",            1, 'e' },
    { "file",              1, 'f' },
    { "ignore-case",       0, 'i' },
    { "word-regexp",       0, 'w' },
    { "line-regexp",       0, 'x' },
    { "invert-match",      0, 'v' },
    { "count",             0, 'c' },
    { "files-with-matches",0, 'l' },
    { "files-without-match",0,'L' },
    { "max-count",         1, 'm' },
    { "only-matching",     0, 'o' },
    { "quiet",             0, 'q' },
    { "silent",            0, 'q' },
    { "no-messages",       0, 's' },
    { "line-number",       0, 'n' },
    { "byte-offset",       0, 'b' },
    { "with-filename",     0, 'H' },
    { "no-filename",       0, 'h' },
    { "label",             1, LO_LABEL },
    { "recursive",         0, 'r' },
    { "directories",       1, 'd' },
    { "include",           1, LO_INCLUDE },
    { "exclude",           1, LO_EXCLUDE },
    { "text",              0, 'a' },
    { "binary-files",      1, LO_BINARY_FILES },
    { "null-data",         0, 'z' },
    { "color",             2, LO_COLOR },
    { "colour",            2, LO_COLOR },
    { "after-context",     1, 'A' },
    { "before-context",    1, 'B' },
    { "context",           1, 'C' },
    { "help",              0, LO_HELP },
    { "version",           0, 'V' },
    { NULL, 0, 0 }
};

/* Dispatch one resolved option (short val or LO_*). `arg` is its argument
 * (may be NULL for no-arg / optional-absent). Returns 0 / -1(usage) and may
 * set *errmsg. */
static int apply_option(struct grep_ctx *g, int val, const char *arg,
                        const char **errmsg)
{
    switch (val) {
    case 'E': g->dialect = GREP_ERE;   break;   /* last-wins (REQ-023) */
    case 'F': g->dialect = GREP_FIXED; break;
    case 'G': case LO_BASIC: g->dialect = GREP_BRE; break;
    case 'e':
        if (grep_add_patterns(g, arg, strlen(arg), errmsg) != 0) return -1;
        break;
    case 'f':
        if (apply_file(g, arg, errmsg) != 0) return -1;
        break;
    case 'i': g->ignore_case = true; break;
    case 'w': g->word = true; break;
    case 'x': g->line_regexp = true; break;
    case 'v': g->invert = true; break;
    case 'c': g->count = true; break;
    case 'l': g->files_with = true; break;
    case 'L': g->files_without = true; break;
    case 'o': g->only_matching = true; break;
    case 'q': g->quiet = true; break;
    case 's': g->no_messages = true; break;
    case 'n': g->line_number = true; break;
    case 'b': g->byte_offset = true; break;
    case 'H': g->with_filename = true; g->no_filename = false; break;
    case 'h': g->no_filename = true; g->with_filename = false; break;
    case 'a': g->binary = GREP_BIN_TEXT; break;
    case 'I': g->binary = GREP_BIN_WITHOUT_MATCH; break;
    case 'z': g->null_data = true; g->delim = '\0'; break;
    case 'r': case 'R':
        g->recursive = true; g->diraction = GREP_DIR_RECURSE; break;
    case 'V': g->show_version = true; break;
    case LO_HELP: g->show_help = true; break;
    case LO_LABEL: g->label = arg; break;
    case 'm':
        if (parse_count(arg, &g->max_count) != 0) {
            *errmsg = "invalid max count"; return -1; }
        break;
    case 'A':
        if (parse_count(arg, &g->after) != 0) {
            *errmsg = "invalid context length"; return -1; }
        break;
    case 'B':
        if (parse_count(arg, &g->before) != 0) {
            *errmsg = "invalid context length"; return -1; }
        break;
    case 'C': {
        long n;
        if (parse_count(arg, &n) != 0) {
            *errmsg = "invalid context length"; return -1; }
        g->before = g->after = n;
        break;
    }
    case 'd':
        if (set_directories(g, arg) != 0) {
            *errmsg = "invalid --directories action"; return -1; }
        break;
    case LO_BINARY_FILES:
        if (set_binary_type(g, arg) != 0) {
            *errmsg = "invalid --binary-files type"; return -1; }
        break;
    case LO_COLOR:
        if (set_color(g, arg) != 0) {
            *errmsg = "invalid --color argument"; return -1; }
        break;
    case LO_INCLUDE:
        if (add_glob(&g->include, &g->ninclude, arg, errmsg) != 0) return -1;
        break;
    case LO_EXCLUDE:
        if (add_glob(&g->exclude, &g->nexclude, arg, errmsg) != 0) return -1;
        break;
    default:
        *errmsg = "invalid option";
        return -1;
    }
    return 0;
}

/* Short options that require an argument. */
static int short_takes_arg(int c)
{
    return c == 'e' || c == 'f' || c == 'm' || c == 'A' || c == 'B' ||
           c == 'C' || c == 'd';
}

int grep_parse_args(struct grep_ctx *g, int argc, char **argv,
                    char ***files_out, int *nfiles_out, const char **errmsg)
{
    const char **operands = NULL;
    size_t nop = 0, capop = 0;
    bool endopts = false;
    int rc = 0;

    *files_out = NULL;
    *nfiles_out = 0;

    for (int i = 1; i < argc; i++) {
        char *tok = argv[i];

        if (!endopts && strcmp(tok, "--") == 0) { endopts = true; continue; }

        if (endopts || tok[0] != '-' || tok[1] == '\0') {
            /* operand (including "-" for stdin) */
            if (nop == capop) {
                size_t ncap = capop ? capop * 2 : 8;
                const char **no = realloc((void *)operands, ncap * sizeof(*no));
                if (!no) { *errmsg = "out of memory"; rc = -1; goto done; }
                operands = no; capop = ncap;
            }
            operands[nop++] = tok;
            continue;
        }

        /* -NUM context shorthand (REQ-GREP-105). */
        if (isdigit((unsigned char)tok[1])) {
            const char *p = tok + 1;
            bool alldig = true;
            for (; *p; p++)
                if (!isdigit((unsigned char)*p)) { alldig = false; break; }
            if (alldig) {
                long n;
                if (parse_count(tok + 1, &n) != 0) {
                    *errmsg = "invalid context length";
                    g->usage_error = true; rc = -1; goto done;
                }
                g->before = g->after = n;
                continue;
            }
        }

        if (tok[1] == '-') {
            /* long option */
            const char *name = tok + 2;
            const char *eq = strchr(name, '=');
            size_t nlen = eq ? (size_t)(eq - name) : strlen(name);
            const struct longopt *m = NULL, *amb = NULL;
            for (const struct longopt *lo = longopts; lo->name; lo++) {
                if (strncmp(lo->name, name, nlen) == 0) {
                    if (strlen(lo->name) == nlen) { m = lo; amb = NULL; break; }
                    if (!m) m = lo; else amb = lo;
                }
            }
            if (!m || amb) {
                *errmsg = amb ? "ambiguous option" : "unrecognized option";
                g->usage_error = true; rc = -1; goto done;
            }
            const char *arg = NULL;
            if (m->has_arg == 1) {
                if (eq) arg = eq + 1;
                else if (i + 1 < argc) arg = argv[++i];
                else { *errmsg = "option requires an argument";
                       g->usage_error = true; rc = -1; goto done; }
            } else if (m->has_arg == 2) {
                arg = eq ? eq + 1 : NULL;
            } else if (eq) {
                *errmsg = "option does not take an argument";
                g->usage_error = true; rc = -1; goto done;
            }
            if (apply_option(g, m->val, arg, errmsg) != 0) {
                if (*errmsg) g->usage_error = true;
                rc = -1; goto done;
            }
            continue;
        }

        /* short option cluster */
        for (size_t j = 1; tok[j]; j++) {
            int c = (unsigned char)tok[j];
            if (short_takes_arg(c)) {
                const char *arg;
                if (tok[j + 1] != '\0') arg = &tok[j + 1];
                else if (i + 1 < argc) arg = argv[++i];
                else { *errmsg = "option requires an argument";
                       g->usage_error = true; rc = -1; goto done; }
                if (apply_option(g, c, arg, errmsg) != 0) {
                    if (*errmsg) g->usage_error = true;
                    rc = -1; goto done;
                }
                break;  /* argument consumed rest of token */
            }
            if (apply_option(g, c, NULL, errmsg) != 0) {
                if (*errmsg) g->usage_error = true;
                rc = -1; goto done;
            }
        }
    }

    if (g->show_help || g->show_version) {
        rc = 0; goto done;
    }

    /* If no -e/-f patterns were given, the first operand is the pattern. */
    size_t file_start = 0;
    if (g->npat == 0) {
        if (nop == 0) {
            *errmsg = "missing pattern";
            g->usage_error = true; rc = -1; goto done;
        }
        if (grep_add_patterns(g, operands[0], strlen(operands[0]), errmsg) != 0) {
            rc = -1; goto done;
        }
        file_start = 1;
    }

    int nf = (int)(nop - file_start);
    if (nf > 0) {
        char **fv = malloc((size_t)nf * sizeof(*fv));
        if (!fv) { *errmsg = "out of memory"; rc = -1; goto done; }
        for (int k = 0; k < nf; k++)
            fv[k] = (char *)operands[file_start + (size_t)k];
        *files_out = fv;
        *nfiles_out = nf;
    }

done:
    free((void *)operands);
    return rc;
}
