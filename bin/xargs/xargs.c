/*
 * xargs.c - construct and execute command lines from standard input.
 *
 * POSIX.1-2024 + GNU + BSD; BSD wins on conflict.  See docs/specs/xargs-spec.md.
 */
#include "xargs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <getopt.h>

extern char **environ;

char       **g_template;
int          g_ntemplate;
int          g_exit_status;
int          g_stop;
const char  *g_prog = "xargs";

#ifndef LINE_MAX
#define LINE_MAX 2048
#endif

void xa_fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s: ", g_prog);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (errno)
        fprintf(stderr, ": %s", strerror(errno));
    fputc('\n', stderr);
    exit(1);
}

static long parse_num(const char *s, const char *opt, long lo)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end || v < lo)
        xa_fatal("invalid number for -%s: %s", opt, s);
    return v;
}

/* Decode a GNU -d / --delimiter spec into one byte (R-4). */
static int decode_delim(const char *s)
{
    if (s[0] != '\\')
        return (unsigned char)s[0];
    switch (s[1]) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'f': return '\f';
    case 'v': return '\v';
    case 'b': return '\b';
    case 'a': return '\a';
    case '0': return '\0';
    case '\\': return '\\';
    case 'x': return (int)strtol(s + 2, NULL, 16) & 0xff;
    default:
        if (s[1] >= '0' && s[1] <= '7')
            return (int)strtol(s + 1, NULL, 8) & 0xff;
        return (unsigned char)s[1];
    }
}

/* Bytes consumed by the current environment (CR-5). */
static size_t env_bytes(void)
{
    size_t n = 0;
    for (char **e = environ; e && *e; e++)
        n += strlen(*e) + 1 + sizeof(char *);
    return n;
}

static void usage(FILE *f)
{
    fputs(
"usage: xargs [-0oprtx] [-E eofstr] [-e[eofstr]] [-I replstr] [-J replstr]\n"
"             [-L number] [-l[number]] [-n number] [-P maxprocs]\n"
"             [-R replcount] [-S replsize] [-s size] [-a file]\n"
"             [-d delim] [--null] [--no-run-if-empty] [utility [argument ...]]\n",
        f);
}

/* Replace up to `count` (<0 = all) occurrences of `r` in `tok` with `val`.
 * Returns a malloc'd string, or NULL if `tok` contains no `r`. */
static char *replace_tok(const char *tok, const char *r, const char *val,
                         long count)
{
    size_t rl = strlen(r);
    if (rl == 0 || !strstr(tok, r))
        return NULL;
    size_t vl = strlen(val);
    /* worst case: every position is a match */
    size_t cap = strlen(tok) + 1;
    /* grow conservatively */
    size_t out_cap = cap + (vl > rl ? (vl - rl) * (cap / (rl ? rl : 1) + 1) : 0) + 1;
    char *out = malloc(out_cap);
    if (!out) xa_fatal("malloc");
    char *o = out;
    const char *p = tok;
    long done = 0;
    while (*p) {
        if ((count < 0 || done < count) && strncmp(p, r, rl) == 0) {
            size_t used = (size_t)(o - out);
            if (used + vl + 1 > out_cap) {
                out_cap = used + vl + 16;
                char *no = realloc(out, out_cap);
                if (!no) xa_fatal("realloc");
                o = no + used; out = no;
            }
            memcpy(o, val, vl); o += vl; p += rl; done++;
        } else {
            *o++ = *p++;
        }
    }
    *o = '\0';
    return out;
}

/* -I: build argv replacing replstr per template token, run once (R-17). */
static void run_replace_line(struct xargs_opts *o, const char *val)
{
    char **argv = malloc((size_t)(g_ntemplate + 1) * sizeof(char *));
    int   *owned = calloc((size_t)g_ntemplate, sizeof(int));
    if (!argv || !owned) xa_fatal("malloc");
    for (int i = 0; i < g_ntemplate; i++) {
        char *rep = replace_tok(g_template[i], o->replstr, val, o->repl_count);
        if (rep) { argv[i] = rep; owned[i] = 1; }
        else      argv[i] = g_template[i];
    }
    argv[g_ntemplate] = NULL;
    xa_run(o, argv, g_ntemplate);
    for (int i = 0; i < g_ntemplate; i++)
        if (owned[i]) free(argv[i]);
    free(argv); free(owned);
}

/* Plain / -n / -L / -J group flush (R-10,R-12,R-16,R-18). */
static void flush_group(struct xargs_opts *o, char **items, int nitems)
{
    if (nitems == 0 && (o->replstr || o->jreplstr))
        return;

    if (o->jreplstr) {
        /* -J: insert all items at the first template token containing it. */
        int n = g_ntemplate - 1 + nitems;
        if (n < 0) n = 0;
        char **argv = malloc((size_t)(n + 2) * sizeof(char *));
        if (!argv) xa_fatal("malloc");
        int k = 0, inserted = 0;
        for (int i = 0; i < g_ntemplate; i++) {
            if (!inserted && strstr(g_template[i], o->jreplstr)) {
                for (int j = 0; j < nitems; j++) argv[k++] = items[j];
                inserted = 1;
            } else {
                argv[k++] = g_template[i];
            }
        }
        if (!inserted)                  /* no replstr: append (BSD fallback) */
            for (int j = 0; j < nitems; j++) argv[k++] = items[j];
        argv[k] = NULL;
        xa_run(o, argv, k);
        free(argv);
        return;
    }

    int n = g_ntemplate + nitems;
    char **argv = malloc((size_t)(n + 1) * sizeof(char *));
    if (!argv) xa_fatal("malloc");
    int k = 0;
    for (int i = 0; i < g_ntemplate; i++) argv[k++] = g_template[i];
    for (int i = 0; i < nitems; i++)      argv[k++] = items[i];
    argv[k] = NULL;
    xa_run(o, argv, k);
    free(argv);
}

int main(int argc, char **argv)
{
    struct xargs_opts o;
    memset(&o, 0, sizeof(o));
    o.dmode = XA_DELIM_WS;
    o.max_procs = 1;
    o.repl_count = -1;

    const char *argfiles[64];
    o.argfiles = argfiles;

    static const struct option lopts[] = {
        {"null",            no_argument,       0, '0'},
        {"arg-file",        required_argument, 0, 'a'},
        {"delimiter",       required_argument, 0, 'd'},
        {"eof",             optional_argument, 0, 'e'},
        {"replace",         optional_argument, 0, 'i'},
        {"max-lines",       optional_argument, 0, 'l'},
        {"max-args",        required_argument, 0, 'n'},
        {"open-tty",        no_argument,       0, 'o'},
        {"max-procs",       required_argument, 0, 'P'},
        {"interactive",     no_argument,       0, 'p'},
        {"no-run-if-empty", no_argument,       0, 'r'},
        {"max-chars",       required_argument, 0, 's'},
        {"verbose",         no_argument,       0, 't'},
        {"exit",            no_argument,       0, 'x'},
        {"help",            no_argument,       0, 1000},
        {"version",         no_argument,       0, 1001},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv,
                            "+0a:d:E:e::I:i::J:L:l::n:oP:pR:rS:s:tx",
                            lopts, NULL)) != -1) {
        switch (c) {
        case '0': o.dmode = XA_DELIM_NUL; o.eofstr = NULL; break;
        case 'a':
            if (o.nargfiles >= 64) xa_fatal("too many -a files");
            argfiles[o.nargfiles++] = optarg; break;
        case 'd': o.dmode = XA_DELIM_CHAR; o.delim_char = decode_delim(optarg);
                  o.eofstr = NULL; break;
        case 'E': o.eofstr = (optarg && *optarg) ? optarg : NULL; break;
        case 'e': o.eofstr = (optarg && *optarg) ? optarg : NULL; break;
        case 'I': o.replstr = optarg; break;
        case 'i': o.replstr = (optarg && *optarg) ? optarg : "{}"; break;
        case 'J': o.jreplstr = optarg; break;
        case 'L': o.max_lines = parse_num(optarg, "L", 1); break;
        case 'l': o.max_lines = optarg ? parse_num(optarg, "l", 1) : 1; break;
        case 'n': o.max_args = parse_num(optarg, "n", 1); break;
        case 'o': o.f_opentty = 1; break;
        case 'P': o.max_procs = parse_num(optarg, "P", 0); break;
        case 'p': o.f_prompt = 1; o.f_trace = 1; break;
        case 'R': o.repl_count = parse_num(optarg, "R", LONG_MIN); break;
        case 'r': o.f_norun = 1; break;
        case 'S': o.repl_size = parse_num(optarg, "S", 0); break;
        case 's': o.max_chars = parse_num(optarg, "s", 1); break;
        case 't': o.f_trace = 1; break;
        case 'x': o.f_exit = 1; break;
        case 1000: usage(stdout); return 0;
        case 1001: puts("xargs (substrate) 1.0"); return 0;
        default: usage(stderr); return 1;
        }
    }

    /* CR-2 implications of -I (R-17). */
    if (o.replstr) {
        o.max_lines = 1;
        o.f_exit = 1;
    }
    /* -0 / -d disable logical EOF (R-9). */
    if (o.dmode != XA_DELIM_WS)
        o.eofstr = NULL;

    /* Operand template (R-10,R-11): default utility echo. */
    if (optind < argc) {
        g_template = &argv[optind];
        g_ntemplate = argc - optind;
    } else {
        static char *def[] = { "echo", NULL };
        g_template = def;
        g_ntemplate = 1;
    }

    /* -s default per CR-5 (R-14). */
    if (o.max_chars <= 0) {
        long am = sysconf(_SC_ARG_MAX);
        if (am <= 0) am = 131072;
        long avail = am - (long)env_bytes() - 2048;
        if (avail < LINE_MAX) avail = LINE_MAX;
        o.max_chars = avail;
    }

    xa_input_open(&o);

    /* base byte cost of the template (R-14 accounting). */
    size_t base = 0;
    for (int i = 0; i < g_ntemplate; i++)
        base += strlen(g_template[i]) + 1;

    int any = 0;
    char *item;
    int   eol;

    if (o.replstr) {
        /* -I: one invocation per logical line; line = items joined by ' '. */
        char *line = NULL; size_t ll = 0, lc = 0;
        while (!g_stop && xa_next_item(&o, &item, &eol)) {
            any = 1;
            size_t need = ll + strlen(item) + 2;
            if (need > lc) { lc = need + 64; line = realloc(line, lc);
                             if (!line) xa_fatal("realloc"); }
            if (ll) line[ll++] = ' ';
            strcpy(line + ll, item); ll += strlen(item);
            free(item);
            if (eol) { run_replace_line(&o, line ? line : ""); ll = 0; if (line) line[0] = '\0'; }
        }
        if (ll) run_replace_line(&o, line);
        free(line);
        /* -I never runs on empty input (R-17). */
    } else {
        char **items = NULL; int nitems = 0, cap = 0, lines = 0;
        size_t cur = base;
        while (!g_stop && xa_next_item(&o, &item, &eol)) {
            any = 1;
            size_t isz = strlen(item) + 1;
            /* Would this item overflow the -s budget? Flush first (R-16). */
            if (nitems > 0 && cur + isz > (size_t)o.max_chars) {
                flush_group(&o, items, nitems);
                for (int i = 0; i < nitems; i++) free(items[i]);
                nitems = 0; lines = 0; cur = base;
            }
            /* Even a single item won't fit: -x is an error (R-15). */
            if (nitems == 0 && base + isz > (size_t)o.max_chars && o.f_exit)
                xa_fatal("argument line too long");
            if (nitems >= cap) { cap = cap ? cap * 2 : 64;
                                 items = realloc(items, (size_t)cap * sizeof(char *));
                                 if (!items) xa_fatal("realloc"); }
            items[nitems++] = item; cur += isz;
            if (eol) lines++;
            if ((o.max_args > 0 && nitems >= o.max_args) ||
                (o.max_lines > 0 && eol && lines >= o.max_lines)) {
                flush_group(&o, items, nitems);
                for (int i = 0; i < nitems; i++) free(items[i]);
                nitems = 0; lines = 0; cur = base;
            }
        }
        if (nitems > 0) {
            flush_group(&o, items, nitems);
            for (int i = 0; i < nitems; i++) free(items[i]);
        }
        free(items);

        /* Empty input rule (CR-3 / R-26): plain mode runs once unless -r. */
        if (!any && !o.f_norun && !o.jreplstr)
            flush_group(&o, NULL, 0);
    }

    xa_wait_all(&o);
    return g_exit_status;
}
