/*
 * sed_parse.c - script parsing for the sed stream editor.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sed.h"

/* ------------------------------------------------------------------ */
/* Script accumulation buffer                                           */
/* ------------------------------------------------------------------ */

static char  *sbuf;
static size_t slen;
static size_t scap;

static void
sbuf_grow(size_t extra)
{
    size_t need = slen + extra + 2;
    if (need <= scap) return;
    size_t nc = scap ? scap * 2 : 4096;
    while (nc < need) nc *= 2;
    char *nb = realloc(sbuf, nc);
    if (!nb) die("out of memory");
    sbuf = nb; scap = nc;
}

void
script_append(const char *s)
{
    size_t n = strlen(s);
    sbuf_grow(n + 2);
    memcpy(sbuf + slen, s, n);
    slen += n;
    sbuf[slen++] = '\n';
    sbuf[slen]   = '\0';
}

void
script_append_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) die("cannot open script '%s': %s", path, strerror(errno));
    char tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof(tmp), fp)) > 0) {
        sbuf_grow(n + 2);
        memcpy(sbuf + slen, tmp, n);
        slen += n;
    }
    if (ferror(fp)) die("read error on '%s': %s", path, strerror(errno));
    fclose(fp);
    sbuf_grow(2);
    sbuf[slen++] = '\n';
    sbuf[slen]   = '\0';
}

/* ------------------------------------------------------------------ */
/* Parser cursor                                                         */
/* ------------------------------------------------------------------ */

static const char *cur;    /* current char in sbuf */
static int         plineno; /* script line number (for errors) */

static char
pnext(void)
{
    char c = *cur;
    if (c) { if (c == '\n') plineno++; cur++; }
    return c;
}

static void skip_blanks(void) { while (*cur == ' ' || *cur == '\t') cur++; }

static void
skip_sep(void)
{
    for (;;) {
        skip_blanks();
        if (*cur == '\n' || *cur == ';') { pnext(); continue; }
        break;
    }
}

static void __attribute__((noreturn))
perror_at(const char *msg)
{
    die("script line %d: %s", plineno, msg);
}

/* ------------------------------------------------------------------ */
/* Regex helper                                                          */
/* ------------------------------------------------------------------ */

/* last pattern for empty-regex reuse */
static char    *last_pat;
static unsigned last_reflags;

/* Validate a pattern at parse time (compile+free for error checking) */
static unsigned
make_reflags(int icase, int ere)
{
    unsigned flags = REGEX_FLAG_MULTILINE;
    if (icase) flags |= REGEX_FLAG_ICASE;
    if (ere)   flags |= REGEX_FLAG_EXTENDED;
    return flags;
}

static void
validate_re(const char *pat, unsigned flags)
{
    regex_err_t err;
    regex_t *re = regex_compile(pat, flags, &err);
    if (!re) die("bad regex /%s/: error %d", pat, (int)err);
    regex_free(re);
    /* update last known good pattern */
    free(last_pat);
    last_pat = strdup(pat);
    last_reflags = flags;
}

/* parse text between current pos and delim; handle \n \\ \<delim> */
static char *
eat_regex(char delim)
{
    dynbuf_t d; db_init(&d);
    while (*cur && *cur != delim) {
        if (*cur == '\\' && cur[1]) {
            if (cur[1] == delim) { db_appendc(&d, delim); cur += 2; continue; }
            if (cur[1] == 'n')   { db_appendc(&d, '\n');  cur += 2; continue; }
            db_appendc(&d, '\\'); cur++;
            db_appendc(&d, *cur++); continue;
        }
        if (*cur == '\n') perror_at("unterminated regex");
        db_appendc(&d, *cur++);
    }
    if (*cur != delim) perror_at("unterminated regex");
    cur++; /* consume closing delim */
    db_ensure_nul(&d);
    return d.buf ? d.buf : strdup("");
}

/* ------------------------------------------------------------------ */
/* Address parsing                                                       */
/* ------------------------------------------------------------------ */

static bool g_ere; /* set from -E/-r option */

static bool
parse_addr(addr_t *a, bool is_a2)
{
    memset(a, 0, sizeof(*a));
    skip_blanks();

    if (is_a2 && *cur == '+') {
        cur++;
        char *e; long n = strtol(cur, &e, 10);
        if (e == cur) perror_at("expected number after ,+");
        cur = e; a->type = A_RELOFF; a->line = n; return true;
    }
    if (is_a2 && *cur == '~') {
        cur++;
        char *e; long n = strtol(cur, &e, 10);
        if (e == cur) perror_at("expected number after ,~");
        cur = e; a->type = A_RELMUL; a->line = n; return true;
    }
    if (*cur == '$') {
        cur++; a->type = A_LAST; return true;
    }
    if (isdigit((unsigned char)*cur)) {
        char *e; long n = strtol(cur, &e, 10);
        cur = e;
        if (*cur == '~') {
            cur++; char *e2; long step = strtol(cur, &e2, 10);
            if (e2 == cur) perror_at("expected step after ~");
            cur = e2; a->type = A_STEP; a->line = n; a->step = step;
        } else {
            a->type = A_LINE; a->line = n;
        }
        return true;
    }
    if (*cur == '/' || *cur == '\\') {
        char delim;
        if (*cur == '\\') { cur++; if (!*cur) perror_at("missing delim"); delim = *cur++; }
        else { delim = *cur++; }
        char *pat = eat_regex(delim);
        int icase = 0;
        if (*cur == 'I') { icase = 1; cur++; }  /* BSD: only uppercase I */
        unsigned rflags = make_reflags(icase, g_ere);
        a->type    = A_REGEX;
        a->icase   = icase;
        a->reflags = rflags;
        if (*pat) {
            validate_re(pat, rflags);
            a->pat = strdup(pat);
        } else {
            /* empty // : inherit last pattern at runtime */
            a->pat = NULL;
        }
        free(pat);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Text argument (a/i/c)                                                */
/* Supports:  cmd\<newline>lines...  and  cmd text (GNU single-line)    */
/* ------------------------------------------------------------------ */

static char *
parse_text(void)
{
    dynbuf_t d; db_init(&d);

    skip_blanks();
    if (*cur == '\\') {
        cur++;
        skip_blanks();
        if (*cur == '\n') { plineno++; cur++; }
    }

    for (;;) {
        const char *ls = cur;
        while (*cur && *cur != '\n') cur++;
        size_t ll = (size_t)(cur - ls);

        /* count trailing backslashes */
        size_t bs = 0;
        while (bs < ll && ls[ll - 1 - bs] == '\\') bs++;
        bool cont = (bs & 1) != 0;

        size_t copy = cont ? ll - 1 : ll;
        for (size_t i = 0; i < copy; i++) {
            if (ls[i] == '\\' && i + 1 < copy) {
                char nc = ls[i+1];
                if (nc == 'n') { db_appendc(&d, '\n'); i++; continue; }
                if (nc == 't') { db_appendc(&d, '\t'); i++; continue; }
                if (nc == '\\') { db_appendc(&d, '\\'); i++; continue; }
            }
            db_appendc(&d, ls[i]);
        }
        db_appendc(&d, '\n');
        if (*cur == '\n') { plineno++; cur++; }
        if (!cont) break;
    }

    db_ensure_nul(&d);
    return d.buf ? d.buf : strdup("\n");
}

/* ------------------------------------------------------------------ */
/* Filename argument (r/R/w/W)                                          */
/* ------------------------------------------------------------------ */

static char *
parse_filename(void)
{
    if (*cur == ' ') cur++; /* skip exactly one space */
    const char *s = cur;
    while (*cur && *cur != '\n' && *cur != ';') cur++;
    size_t n = (size_t)(cur - s);
    if (!n) perror_at("missing filename");
    char *fn = malloc(n + 1);
    if (!fn) die("out of memory");
    memcpy(fn, s, n); fn[n] = '\0';
    return fn;
}

/* ------------------------------------------------------------------ */
/* Label argument (b/t/T/:)                                             */
/* ------------------------------------------------------------------ */

static char *
parse_label(void)
{
    skip_blanks();
    const char *s = cur;
    while (*cur && *cur != '\n' && *cur != ';' && *cur != ' ' && *cur != '\t') cur++;
    size_t n = (size_t)(cur - s);
    char *lab = malloc(n + 1);
    if (!lab) die("out of memory");
    memcpy(lab, s, n); lab[n] = '\0';
    return lab;
}

/* ------------------------------------------------------------------ */
/* Write-file registry                                                   */
/* ------------------------------------------------------------------ */

static int
get_wfile(const char *name)
{
    for (int i = 0; i < G.write_count; i++)
        if (strcmp(G.write_files[i], name) == 0) return i;
    if (G.write_count >= MAX_WRITE_FILES) die("too many write files");
    G.write_files[G.write_count] = strdup(name);
    G.write_fps[G.write_count]   = fopen(name, "w");
    if (!G.write_fps[G.write_count])
        die("cannot open '%s': %s", name, strerror(errno));
    return G.write_count++;
}

/* ------------------------------------------------------------------ */
/* Substitution                                                          */
/* ------------------------------------------------------------------ */

static subst_t *
parse_subst(void)
{
    if (!*cur) perror_at("missing s delimiter");
    char delim = *cur++;
    char *pat  = eat_regex(delim);

    /* replacement */
    dynbuf_t r; db_init(&r);
    while (*cur && *cur != delim) {
        if (*cur == '\\' && cur[1]) {
            if (cur[1] == delim) { db_appendc(&r, delim); cur += 2; continue; }
            if (cur[1] == 'n')   { db_appendc(&r, '\n');  cur += 2; continue; }
            if (cur[1] == 't')   { db_appendc(&r, '\t');  cur += 2; continue; }
            db_appendc(&r, '\\'); cur++;
            db_appendc(&r, *cur++); continue;
        }
        if (*cur == '\n') perror_at("unterminated s replacement");
        db_appendc(&r, *cur++);
    }
    if (*cur != delim) perror_at("unterminated s command");
    cur++;
    db_ensure_nul(&r);

    /* flags (parse before compile so icase affects RE) */
    int global=0, print=0, nth=0, icase=0, exec=0, wfile=-1;
    skip_blanks();
    while (*cur && *cur != '\n') {
        char f = *cur;
        if (f == ';' || f == '}') break;
        cur++;
        if (f == 'g')             { global = 1; }
        else if (f == 'p')        { print  = 1; }
        else if (f == 'i'||f=='I'){ icase  = 1; }
        else if (f == 'e')        { exec   = 1; }
        else if (f>='1'&&f<='9')  { nth    = f - '0'; }
        else if (f == 'w') {
            char *fn = parse_filename();
            wfile = get_wfile(fn); free(fn); break;
        } else if (f==' '||f=='\t') { /* skip */ }
        else { cur--; break; }
    }

    subst_t *sub = calloc(1, sizeof(*sub));
    if (!sub) die("out of memory");
    sub->global  = global; sub->print = print;
    sub->icase   = icase;  sub->nth   = nth;
    sub->exec    = exec;   sub->wfile = wfile;
    sub->repl    = r.buf ? r.buf : strdup("");
    sub->reflags = make_reflags(icase, g_ere);

    if (*pat) {
        validate_re(pat, sub->reflags);
        sub->pat = strdup(pat);
    } else {
        sub->pat = NULL; /* empty: runtime reuse of last pattern */
    }
    free(pat);
    return sub;
}

/* ------------------------------------------------------------------ */
/* Transliteration                                                       */
/* ------------------------------------------------------------------ */

static trans_t *
parse_trans(void)
{
    if (!*cur) perror_at("missing y delimiter");
    char delim = *cur++;

    unsigned char src[512], dst[512];
    int sn=0, dn=0;

    for (int which = 0; which < 2; which++) {
        int *np = (which == 0) ? &sn : &dn;
        unsigned char *arr = (which == 0) ? src : dst;
        while (*cur && *cur != delim) {
            unsigned char c;
            if (*cur == '\\' && cur[1]) {
                cur++;
                switch (*cur) {
                case 'n': c = '\n'; break; case 't': c = '\t'; break;
                case 'a': c = '\a'; break; case 'r': c = '\r'; break;
                default:  c = (unsigned char)*cur; break;
                }
                cur++;
            } else {
                if (*cur == '\n') perror_at("unterminated y string");
                c = (unsigned char)*cur++;
            }
            if (*np >= 512) perror_at("y string too long");
            arr[(*np)++] = c;
        }
        if (*cur != delim) perror_at("unterminated y command");
        cur++;
    }
    if (sn != dn) perror_at("y: source and dest lengths differ");

    trans_t *t = malloc(sizeof(*t));
    if (!t) die("out of memory");
    for (int i = 0; i < 256; i++) t->map[i] = (unsigned char)i;
    for (int i = 0; i < sn; i++)  t->map[src[i]] = dst[i];
    return t;
}

/* ------------------------------------------------------------------ */
/* Label / branch tables                                                 */
/* ------------------------------------------------------------------ */

#define MAX_LABELS  256
#define MAX_BRANCHES 4096

static struct { char *name; cmd_t *target; } ltab[MAX_LABELS];
static int nlab;

static cmd_t *btab[MAX_BRANCHES];
static int    nbr;

static void
def_label(const char *name, cmd_t *c)
{
    for (int i = 0; i < nlab; i++)
        if (strcmp(ltab[i].name, name) == 0) die("duplicate label '%s'", name);
    if (nlab >= MAX_LABELS) die("too many labels");
    ltab[nlab].name   = strdup(name);
    ltab[nlab].target = c;
    nlab++;
}

static void
need_resolve(cmd_t *c)
{
    if (nbr >= MAX_BRANCHES) die("too many branch commands");
    btab[nbr++] = c;
}

static void
resolve_all(void)
{
    for (int i = 0; i < nbr; i++) {
        cmd_t *c = btab[i];
        if (!c->text || !c->text[0]) { c->target = NULL; continue; }
        bool found = false;
        for (int j = 0; j < nlab; j++) {
            if (strcmp(ltab[j].name, c->text) == 0) {
                c->target = ltab[j].target->next;
                found = true; break;
            }
        }
        if (!found) die("undefined label '%s'", c->text);
    }
}

/* ------------------------------------------------------------------ */
/* Command allocation / list                                             */
/* ------------------------------------------------------------------ */

static cmd_t *
new_cmd(cmdtype_t t)
{
    cmd_t *c = calloc(1, sizeof(*c));
    if (!c) die("out of memory");
    c->type = t;
    c->num  = -1;
    return c;
}

static void
push_cmd(cmd_t *c)
{
    if (!G.cmds) { G.cmds = G.cmds_tail = c; }
    else { G.cmds_tail->next = c; G.cmds_tail = c; }
}

/* ------------------------------------------------------------------ */
/* Main parser                                                           */
/* ------------------------------------------------------------------ */

static cmd_t *bstk[64]; /* brace stack */
static int    bdepth;

int
script_parse(void)
{
    if (!sbuf || !slen) return 0;

    cur        = sbuf;
    plineno    = 1;
    bdepth     = 0;
    nlab       = 0;
    nbr        = 0;
    last_pat   = NULL;
    last_reflags = 0;
    g_ere      = G.use_ere;

    bool first = true;
    skip_sep();

    while (*cur) {
        /* comment / #n */
        if (*cur == '#') {
            if (first && cur[1] == 'n') G.suppress = true;
            while (*cur && *cur != '\n') cur++;
            first = false;
            skip_sep(); continue;
        }
        first = false;

        /* addresses */
        addr_t a[2]; int naddr = 0;
        if (parse_addr(&a[0], false)) {
            naddr = 1;
            skip_blanks();
            if (*cur == ',') {
                cur++;
                if (!parse_addr(&a[1], true)) perror_at("missing second address");
                naddr = 2;
            }
        }

        /* negate */
        skip_blanks();
        int neg = 0;
        while (*cur == '!') { neg = !neg; cur++; }
        skip_blanks();

        if (!*cur || *cur == '\n') perror_at("missing command");
        char ch = *cur++;

        cmd_t *c = new_cmd((cmdtype_t)(unsigned char)ch);
        c->naddr  = naddr;
        c->negate = neg;
        if (naddr >= 1) c->addr[0] = a[0];
        if (naddr >= 2) c->addr[1] = a[1];

        /* resolve empty regex in addresses (inherit last pattern) */
        for (int i = 0; i < naddr; i++) {
            if (c->addr[i].type == A_REGEX && !c->addr[i].pat) {
                if (!last_pat) perror_at("no previous regex");
                c->addr[i].pat     = strdup(last_pat);
                c->addr[i].reflags = last_reflags;
            }
        }

        switch (ch) {
        /* text commands */
        case 'a': case 'i': case 'c':
            c->text = parse_text();
            break;
        /* branch/label */
        case 'b': case 't': case 'T':
            c->text = parse_label();
            need_resolve(c);
            break;
        case ':':
            c->text = parse_label();
            if (!c->text[0]) perror_at("empty label");
            def_label(c->text, c);
            break;
        /* file commands */
        case 'r': case 'R':
            c->text = parse_filename();
            break;
        case 'w': case 'W': {
            char *fn = parse_filename();
            c->num = get_wfile(fn); free(fn);
            break;
        }
        /* substitution */
        case 's':
            c->subst = parse_subst();
            if (!c->subst->pat) {
                /* empty s///: inherit last pattern */
                if (!last_pat) perror_at("no previous regex");
                c->subst->pat     = strdup(last_pat);
                c->subst->reflags = last_reflags;
            }
            break;
        /* transliteration */
        case 'y':
            c->trans = parse_trans();
            break;
        /* quit with optional exit code */
        case 'q': case 'Q':
            skip_blanks();
            if (isdigit((unsigned char)*cur)) {
                char *e; c->num = (int)strtol(cur, &e, 10); cur = e;
            } else { c->num = 0; }
            break;
        /* list with optional width */
        case 'l':
            skip_blanks();
            if (isdigit((unsigned char)*cur)) {
                char *e; c->num = (int)strtol(cur, &e, 10); cur = e;
            } else { c->num = G.list_wrap; }
            break;
        /* execute (e with optional command arg) */
        case 'e':
            skip_blanks();
            if (*cur && *cur != '\n' && *cur != ';')
                c->text = parse_text();
            break;
        /* braces */
        case '{':
            if (bdepth >= 64) perror_at("braces nested too deeply");
            bstk[bdepth++] = c;
            break;
        case '}':
            if (bdepth == 0) perror_at("unexpected }");
            bstk[--bdepth]->end_block = c;
            break;
        /* no-argument commands */
        case 'd': case 'D':
        case 'F':
        case 'g': case 'G':
        case 'h': case 'H':
        case 'n': case 'N':
        case 'p': case 'P':
        case 'x':
        case 'z':
        case '=':
            break;
        default:
            die("script line %d: unknown command '%c' (0x%02x)",
                plineno, isprint((unsigned char)ch) ? ch : '?',
                (unsigned char)ch);
        }

        push_cmd(c);
        skip_sep();
    }

    if (bdepth > 0) perror_at("unclosed {");
    resolve_all();
    return 0;
}
