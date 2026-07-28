/*
 * grep_pattern.c - pattern accumulation, POSIX character-class translation,
 * compilation, and per-line matching for Substrate grep.
 *
 * The in-tree regex engine (usr.lib/regex) lacks native POSIX bracket
 * character classes, word-boundary assertions, and back-references.  This
 * file mitigates the first two at the grep layer (REQ-GREP-070..074,
 * REQ-GREP-044/045); back-references remain deferred (spec DEFER-1).
 */
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "grep.h"

/* ------------------------------------------------------------------ */
/* small growable byte buffer                                          */
/* ------------------------------------------------------------------ */
struct buf {
    char  *data;
    size_t len;
    size_t cap;
    int    oom;
};

static void buf_putc(struct buf *b, char c)
{
    if (b->oom)
        return;
    if (b->len + 1 >= b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 64;
        char *nd = realloc(b->data, ncap);
        if (!nd) { b->oom = 1; return; }
        b->data = nd;
        b->cap = ncap;
    }
    b->data[b->len++] = c;
}

static void buf_puts(struct buf *b, const char *s)
{
    while (*s)
        buf_putc(b, *s++);
}

/* ------------------------------------------------------------------ */
/* POSIX character-class translation (REQ-GREP-070..074)               */
/* ------------------------------------------------------------------ */

/* Emit one literal byte into a bracket expression, backslash-escaping the
 * four bytes that are otherwise significant there. */
static void emit_bracket_byte(struct buf *b, unsigned char c)
{
    if (c == ']' || c == '\\' || c == '^' || c == '-')
        buf_putc(b, '\\');
    buf_putc(b, (char)c);
}

/* Expand a recognized POSIX class name into bracket-expression members.
 * Returns 1 on success, 0 if the class name is unknown. */
static int emit_class(struct buf *b, const char *name, size_t nlen)
{
    /* Range-based classes use safe characters and need no escaping. */
    static const struct { const char *n; const char *r; } ranges[] = {
        { "alnum",  "0-9A-Za-z" },
        { "alpha",  "A-Za-z"    },
        { "digit",  "0-9"       },
        { "lower",  "a-z"       },
        { "upper",  "A-Z"       },
        { "xdigit", "0-9A-Fa-f" },
        { NULL, NULL }
    };
    for (int i = 0; ranges[i].n; i++) {
        if (nlen == strlen(ranges[i].n) &&
            memcmp(name, ranges[i].n, nlen) == 0) {
            buf_puts(b, ranges[i].r);
            return 1;
        }
    }

    if (nlen == 5 && memcmp(name, "blank", 5) == 0) {
        emit_bracket_byte(b, ' ');
        emit_bracket_byte(b, '\t');
        return 1;
    }
    if (nlen == 5 && memcmp(name, "space", 5) == 0) {
        emit_bracket_byte(b, ' ');
        emit_bracket_byte(b, '\t');
        emit_bracket_byte(b, '\n');
        emit_bracket_byte(b, '\v');
        emit_bracket_byte(b, '\f');
        emit_bracket_byte(b, '\r');
        return 1;
    }
    if (nlen == 5 && memcmp(name, "cntrl", 5) == 0) {
        /* 0x01..0x1f and 0x7f (NUL cannot appear in a C-string pattern). */
        for (unsigned char c = 1; c <= 0x1f; c++)
            emit_bracket_byte(b, c);
        emit_bracket_byte(b, 0x7f);
        return 1;
    }
    if ((nlen == 5 && memcmp(name, "punct", 5) == 0) ||
        (nlen == 5 && memcmp(name, "graph", 5) == 0) ||
        (nlen == 5 && memcmp(name, "print", 5) == 0)) {
        unsigned char lo = (nlen == 5 && memcmp(name, "print", 5) == 0)
                               ? 0x20 : 0x21;
        for (unsigned char c = lo; c <= 0x7e; c++) {
            if (nlen == 5 && memcmp(name, "punct", 5) == 0 &&
                (isalnum(c)))
                continue;
            emit_bracket_byte(b, c);
        }
        return 1;
    }
    return 0;
}

/* Translate POSIX [:class:] tokens inside bracket expressions of `src` into
 * explicit member sets the engine understands.  Allocates *out. */
static int translate_classes(const char *src, char **out, const char **errmsg)
{
    struct buf b = {0};
    const char *p = src;
    int in_bracket = 0;

    while (*p) {
        if (!in_bracket) {
            if (*p == '\\' && p[1]) {
                buf_putc(&b, *p++);
                buf_putc(&b, *p++);
                continue;
            }
            if (*p == '[') {
                buf_putc(&b, *p++);
                in_bracket = 1;
                if (*p == '^')
                    buf_putc(&b, *p++);
                if (*p == ']') {
                    /* POSIX: a ] immediately after [ or [^ is a literal
                     * member.  The engine does not honor the bare leading-]
                     * convention but does accept an escaped \], so emit
                     * that. */
                    buf_puts(&b, "\\]");
                    p++;
                }
                continue;
            }
            buf_putc(&b, *p++);
            continue;
        }

        /* inside a bracket expression */
        if (*p == '[' && (p[1] == '.' || p[1] == '=')) {
            *errmsg = "collating symbols and equivalence classes are "
                      "not supported";
            free(b.data);
            return -1;
        }
        if (*p == '[' && p[1] == ':') {
            const char *name = p + 2;
            const char *end = strstr(name, ":]");
            if (!end) {
                *errmsg = "unterminated character class";
                free(b.data);
                return -1;
            }
            if (!emit_class(&b, name, (size_t)(end - name))) {
                *errmsg = "unknown character class name";
                free(b.data);
                return -1;
            }
            p = end + 2;
            continue;
        }
        if (*p == ']') {
            buf_putc(&b, *p++);
            in_bracket = 0;
            continue;
        }
        buf_putc(&b, *p++);
    }

    if (b.oom) {
        *errmsg = "out of memory";
        free(b.data);
        return -1;
    }
    buf_putc(&b, '\0');
    if (b.oom) {
        *errmsg = "out of memory";
        free(b.data);
        return -1;
    }
    *out = b.data;
    return 0;
}

/* ------------------------------------------------------------------ */
/* pattern accumulation                                                */
/* ------------------------------------------------------------------ */
static int push_pattern(struct grep_ctx *g, char *text, size_t len,
                        const char **errmsg)
{
    if (g->npat == g->cappat) {
        size_t ncap = g->cappat ? g->cappat * 2 : 8;
        struct grep_pattern *np = realloc(g->patterns,
                                          ncap * sizeof(*np));
        if (!np) { *errmsg = "out of memory"; free(text); return -1; }
        g->patterns = np;
        g->cappat = ncap;
    }
    struct grep_pattern *pp = &g->patterns[g->npat++];
    memset(pp, 0, sizeof(*pp));
    pp->text = text;
    pp->len = len;
    return 0;
}

int grep_add_patterns(struct grep_ctx *g, const char *buf, size_t len,
                      const char **errmsg)
{
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            /* A trailing empty segment after a final '\n' is not a pattern,
             * but an explicit empty pattern (e.g. -e '') is. */
            if (i == len && i == start && len != 0)
                break;
            size_t seglen = i - start;
            char *text = malloc(seglen + 1);
            if (!text) { *errmsg = "out of memory"; return -1; }
            memcpy(text, buf + start, seglen);
            text[seglen] = '\0';
            if (push_pattern(g, text, seglen, errmsg) != 0)
                return -1;
            start = i + 1;
        }
    }
    return 0;
}

int grep_add_pattern_file(struct grep_ctx *g, const char *path,
                          const char **errmsg)
{
    FILE *f = (strcmp(path, "-") == 0) ? stdin : fopen(path, "r");
    if (!f) {
        *errmsg = strerror(errno);
        return -1;
    }

    struct buf b = {0};
    int c;
    while ((c = fgetc(f)) != EOF)
        buf_putc(&b, (char)c);
    int ferr = ferror(f);
    if (f != stdin)
        fclose(f);
    if (ferr || b.oom) {
        free(b.data);
        *errmsg = b.oom ? "out of memory" : "read error";
        return -1;
    }

    /* An empty pattern file contributes no patterns (REQ-GREP-034). */
    int rc = 0;
    if (b.len > 0)
        rc = grep_add_patterns(g, b.data, b.len, errmsg);
    free(b.data);
    return rc;
}

/* ------------------------------------------------------------------ */
/* compilation                                                         */
/* ------------------------------------------------------------------ */
static int compile_one(struct grep_ctx *g, struct grep_pattern *p,
                       const char **errmsg)
{
    if (g->dialect == GREP_FIXED)
        return 0;   /* fixed strings keep their literal text */

    struct buf src = {0};
    if (g->line_regexp) {
        /* Anchor whole-line matches; group so alternation binds (REQ-045). */
        if (g->dialect == GREP_ERE)
            buf_puts(&src, "^(");
        else
            buf_puts(&src, "^\\(");
    }
    buf_puts(&src, p->text);
    if (g->line_regexp) {
        if (g->dialect == GREP_ERE)
            buf_puts(&src, ")$");
        else
            buf_puts(&src, "\\)$");
    }
    buf_putc(&src, '\0');
    if (src.oom) { *errmsg = "out of memory"; free(src.data); return -1; }

    char *translated = NULL;
    if (translate_classes(src.data, &translated, errmsg) != 0) {
        free(src.data);
        return -1;
    }
    free(src.data);

    unsigned flags = 0;
    if (g->dialect == GREP_ERE)
        flags |= REGEX_FLAG_EXTENDED;
    if (g->ignore_case)
        flags |= REGEX_FLAG_ICASE;

    regex_err_t err = REGEX_OK;
    p->re = regex_compile(translated, flags, &err);
    free(translated);
    if (!p->re) {
        *errmsg = "invalid regular expression";
        return -1;
    }

    size_t cc = regex_capture_count(p->re);
    p->capslots = 2 * (cc ? cc : 1) + 2;
    p->caps = calloc(p->capslots, sizeof(size_t));
    if (!p->caps) { *errmsg = "out of memory"; return -1; }
    return 0;
}

int grep_compile_patterns(struct grep_ctx *g, const char **errmsg)
{
    for (size_t i = 0; i < g->npat; i++) {
        if (compile_one(g, &g->patterns[i], errmsg) != 0)
            return -1;
    }
    return 0;
}

void grep_free_patterns(struct grep_ctx *g)
{
    for (size_t i = 0; i < g->npat; i++) {
        free(g->patterns[i].text);
        free(g->patterns[i].caps);
        if (g->patterns[i].re)
            regex_free(g->patterns[i].re);
    }
    free(g->patterns);
    g->patterns = NULL;
    g->npat = g->cappat = 0;
}

/* ------------------------------------------------------------------ */
/* matching                                                            */
/* ------------------------------------------------------------------ */
static int lc(int c) { return tolower((unsigned char)c); }

static int is_word(unsigned char c)
{
    return isalnum(c) || c == '_';
}

/* Length-aware substring search honoring -i. */
static int fixed_search(const char *hay, size_t hlen, const char *needle,
                        size_t nlen, int icase, size_t from,
                        size_t *ms, size_t *me)
{
    if (nlen == 0) { *ms = *me = from <= hlen ? from : hlen; return from <= hlen; }
    if (from > hlen || hlen - from < nlen)
        return 0;
    for (size_t i = from; i + nlen <= hlen; i++) {
        size_t j = 0;
        for (; j < nlen; j++) {
            char a = hay[i + j], b = needle[j];
            if (icase) { a = (char)lc(a); b = (char)lc(b); }
            if (a != b)
                break;
        }
        if (j == nlen) { *ms = i; *me = i + nlen; return 1; }
    }
    return 0;
}

static int regex_search(struct grep_pattern *p, const char *line, size_t len,
                        size_t from, size_t *ms, size_t *me)
{
    if (from > len)
        return 0;
    ssize_t r = regex_match(p->re, line + from, len - from,
                            p->caps, p->capslots, NULL);
    /* regex_match returns the capture count (>=1) on a match and a negative
     * value on no-match/error.  The narrow cast keeps the sign test correct
     * regardless of ssize_t width: every valid return fits in int, and the
     * engine's no-match sentinel survives as a negative int on both the
     * 32-bit target and a 64-bit host test build. */
    if ((int)r < 0)
        return 0;
    *ms = from + p->caps[0];
    *me = from + p->caps[1];
    return 1;
}

static int word_ok(const char *line, size_t len, size_t s, size_t e)
{
    if (s > 0 && is_word((unsigned char)line[s - 1]))
        return 0;
    if (e < len && is_word((unsigned char)line[e]))
        return 0;
    return 1;
}

int grep_find_match(struct grep_ctx *g, const char *line, size_t len,
                    size_t from, size_t *ms, size_t *me)
{
    size_t best_s = SIZE_MAX, best_e = 0;

    for (size_t i = 0; i < g->npat; i++) {
        struct grep_pattern *p = &g->patterns[i];
        size_t cur = from, s, e;
        for (;;) {
            int ok = (g->dialect == GREP_FIXED)
                         ? fixed_search(line, len, p->text, p->len,
                                        g->ignore_case, cur, &s, &e)
                         : regex_search(p, line, len, cur, &s, &e);
            if (!ok)
                break;
            if (g->word && !word_ok(line, len, s, e)) {
                cur = (e > s) ? s + 1 : e + 1;
                if (cur > len)
                    break;
                continue;
            }
            if (s < best_s) { best_s = s; best_e = e; }
            break;
        }
    }

    if (best_s == SIZE_MAX)
        return 0;
    *ms = best_s;
    *me = best_e;
    return 1;
}

int grep_line_match(struct grep_ctx *g, const char *line, size_t len)
{
    if (g->line_regexp) {
        for (size_t i = 0; i < g->npat; i++) {
            struct grep_pattern *p = &g->patterns[i];
            if (g->dialect == GREP_FIXED) {
                if (len != p->len)
                    continue;
                if (g->ignore_case) {
                    size_t j = 0;
                    for (; j < len; j++)
                        if (lc(line[j]) != lc(p->text[j]))
                            break;
                    if (j == len)
                        return 1;
                } else if (memcmp(line, p->text, len) == 0) {
                    return 1;
                }
            } else {
                if ((int)regex_match(p->re, line, len, p->caps, p->capslots,
                                     NULL) >= 0)
                    return 1;
            }
        }
        return 0;
    }

    size_t ms, me;
    return grep_find_match(g, line, len, 0, &ms, &me);
}
