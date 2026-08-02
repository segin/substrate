/*
 * xargs_input.c - item tokenizer for substrate xargs.
 *
 * Implements R-1..R-9, R-13 line tracking: default whitespace splitting with
 * single/double-quote grouping and backslash escaping (CR-6); NUL mode (-0);
 * single-byte delimiter mode (-d); and the -E/-e logical EOF string.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xargs.h"

/* Source list: stdin, or the -a files in order. */
static FILE **xa_src;
static int    xa_nsrc;
static int    xa_cur;

int xa_input_open(struct xargs_opts *o)
{
    if (o->nargfiles > 0) {
        xa_src = calloc((size_t)o->nargfiles, sizeof(*xa_src));
        if (!xa_src) xa_fatal("calloc");
        for (int i = 0; i < o->nargfiles; i++) {
            if (strcmp(o->argfiles[i], "-") == 0) {
                xa_src[i] = stdin;
            } else {
                xa_src[i] = fopen(o->argfiles[i], "r");
                if (!xa_src[i]) xa_fatal("%s", o->argfiles[i]);
            }
        }
        xa_nsrc = o->nargfiles;
    } else {
        static FILE *only_stdin;
        only_stdin = stdin;
        xa_src = &only_stdin;
        xa_nsrc = 1;
    }
    xa_cur = 0;
    return 0;
}

/* Next raw byte across the source list, or EOF. */
static int xa_getc(void)
{
    for (;;) {
        if (xa_cur >= xa_nsrc)
            return EOF;
        int c = getc(xa_src[xa_cur]);
        if (c != EOF)
            return c;
        xa_cur++;            /* exhausted this source — advance */
    }
}

/* Growable item buffer. */
struct buf { char *p; size_t len, cap; };

static void buf_putc(struct buf *b, int c)
{
    if (b->len + 1 >= b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 64;
        char *np = realloc(b->p, nc);
        if (!np) xa_fatal("realloc");
        b->p = np;
        b->cap = nc;
    }
    b->p[b->len++] = (char)c;
}

static int is_ws(int c) { return c == ' ' || c == '\t' || c == '\n'; }

int xa_next_item(struct xargs_opts *o, char **out, int *is_eol)
{
    struct buf b = { NULL, 0, 0 };
    int eol = 0;
    int c;

    *out = NULL;
    *is_eol = 0;

    if (o->dmode == XA_DELIM_NUL || o->dmode == XA_DELIM_CHAR) {
        int delim = (o->dmode == XA_DELIM_NUL) ? '\0' : o->delim_char;
        c = xa_getc();
        if (c == EOF)
            return 0;
        for (; c != EOF && c != delim; c = xa_getc())
            buf_putc(&b, c);
        buf_putc(&b, '\0');
        b.len--;                 /* don't count the NUL terminator */
        *out = b.p ? b.p : strdup("");
        *is_eol = 1;             /* each delimited item is its own "line" */
        return 1;
    }

    /* Default whitespace mode (CR-6). Skip leading separators. */
    do {
        c = xa_getc();
    } while (c != EOF && is_ws(c));
    if (c == EOF)
        return 0;

    int sawany = 0;
    for (; c != EOF; c = xa_getc()) {
        if (is_ws(c)) {
            if (c == '\n')
                eol = 1;
            break;                /* unquoted separator ends the item */
        }
        sawany = 1;
        if (c == '\\') {
            int n = xa_getc();    /* backslash escapes the next byte */
            if (n == EOF) { buf_putc(&b, '\\'); break; }
            buf_putc(&b, n);
        } else if (c == '\'') {
            int q;
            while ((q = xa_getc()) != EOF && q != '\'')
                buf_putc(&b, q);
        } else if (c == '"') {
            int q;
            while ((q = xa_getc()) != EOF && q != '"')
                buf_putc(&b, q);
        } else {
            buf_putc(&b, c);
        }
    }
    (void)sawany;
    buf_putc(&b, '\0');
    b.len--;

    /* Logical EOF (R-7): only in default mode, only on an exact match. */
    if (o->eofstr && b.p && strcmp(b.p, o->eofstr) == 0) {
        free(b.p);
        return 0;
    }

    *out = b.p ? b.p : strdup("");
    *is_eol = eol;
    return 1;
}
