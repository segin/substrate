#include "demangle_internal.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <demangle.h>

#define RUST_BUF_INITIAL_CAP 256u
#define RUST_RECURSION_DEFAULT_LIMIT 128

typedef struct rust_buf {
    char *data;
    size_t len;
    size_t cap;
} rust_buf_t;

typedef struct rust_backref {
    const char *start;
    const char *end;
} rust_backref_t;

typedef struct rust_parser {
    const char *input;
    const char *cur;
    int options;

    int recursion_depth;
    int recursion_limit;

    rust_buf_t out;

    rust_backref_t *backrefs;
    size_t backref_count;
    size_t backref_cap;
} rust_parser_t;

static int starts_with(const char *s, const char *prefix);
static int is_hex_char(char ch);
static int rust_is_v0(const char *mangled);
static int rust_is_legacy(const char *mangled);

static int rust_buf_reserve(rust_buf_t *buf, size_t extra);
static int rust_buf_append(rust_buf_t *buf, const char *s, size_t n);
static int rust_buf_appendc(rust_buf_t *buf, char ch);
static char *rust_buf_take(rust_buf_t *buf);
static void rust_buf_destroy(rust_buf_t *buf);

static int rust_parser_init(rust_parser_t *p, const char *mangled, int options);
static void rust_parser_destroy(rust_parser_t *p);
static int rust_parser_enter(rust_parser_t *p);
static void rust_parser_leave(rust_parser_t *p);
static int rust_parser_add_backref(rust_parser_t *p, const char *start, const char *end);

static int rust_parse_v0_symbol(rust_parser_t *p);
static char *rust_demangle_v0(const char *mangled, int options);
static char *rust_demangle_legacy(const char *mangled, int options);

static int
starts_with(const char *s, const char *prefix)
{
    size_t n;

    if (s == NULL || prefix == NULL) {
        return 0;
    }

    n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

static int
is_hex_char(char ch)
{
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

static int
rust_is_v0(const char *mangled)
{
    return starts_with(mangled, "_R");
}

static int
rust_is_legacy(const char *mangled)
{
    size_t len;
    size_t i;

    if (!starts_with(mangled, "_ZN")) {
        return 0;
    }

    len = strlen(mangled);
    if (len < 24u || mangled[len - 1u] != 'E') {
        return 0;
    }

    if (mangled[len - 20u] != '1' || mangled[len - 19u] != '7' || mangled[len - 18u] != 'h') {
        return 0;
    }

    for (i = len - 17u; i < len - 1u; i++) {
        if (!is_hex_char(mangled[i])) {
            return 0;
        }
    }

    return 1;
}

static int
rust_buf_reserve(rust_buf_t *buf, size_t extra)
{
    size_t need;
    size_t cap;
    char *next;

    if (buf == NULL) {
        return -1;
    }

    need = buf->len + extra + 1u;
    if (need <= buf->cap) {
        return 0;
    }

    cap = (buf->cap == 0u) ? RUST_BUF_INITIAL_CAP : buf->cap;
    while (cap < need) {
        size_t doubled = cap << 1;
        if (doubled < cap) {
            return -1;
        }
        cap = doubled;
    }

    next = (char *)realloc(buf->data, cap);
    if (next == NULL) {
        return -1;
    }

    buf->data = next;
    buf->cap = cap;
    return 0;
}

static int
rust_buf_append(rust_buf_t *buf, const char *s, size_t n)
{
    if (buf == NULL || s == NULL) {
        return -1;
    }

    if (rust_buf_reserve(buf, n) != 0) {
        return -1;
    }

    if (n > 0u) {
        memcpy(buf->data + buf->len, s, n);
        buf->len += n;
    }

    buf->data[buf->len] = '\0';
    return 0;
}

static int
rust_buf_appendc(rust_buf_t *buf, char ch)
{
    if (rust_buf_reserve(buf, 1u) != 0) {
        return -1;
    }

    buf->data[buf->len++] = ch;
    buf->data[buf->len] = '\0';
    return 0;
}

static char *
rust_buf_take(rust_buf_t *buf)
{
    char *ret;

    if (buf == NULL || buf->data == NULL) {
        return NULL;
    }

    buf->data[buf->len] = '\0';
    ret = buf->data;
    buf->data = NULL;
    buf->len = 0u;
    buf->cap = 0u;
    return ret;
}

static void
rust_buf_destroy(rust_buf_t *buf)
{
    if (buf == NULL) {
        return;
    }

    free(buf->data);
    buf->data = NULL;
    buf->len = 0u;
    buf->cap = 0u;
}

static int
rust_parser_init(rust_parser_t *p, const char *mangled, int options)
{
    memset(p, 0, sizeof(*p));
    p->input = mangled;
    p->cur = mangled;
    p->options = options;
    p->recursion_limit = RUST_RECURSION_DEFAULT_LIMIT;

    if (rust_buf_reserve(&p->out, 0u) != 0) {
        return -1;
    }

    p->out.data[0] = '\0';
    return 0;
}

static void
rust_parser_destroy(rust_parser_t *p)
{
    if (p == NULL) {
        return;
    }

    rust_buf_destroy(&p->out);
    free(p->backrefs);
    p->backrefs = NULL;
    p->backref_count = 0u;
    p->backref_cap = 0u;
}

static int
rust_parser_enter(rust_parser_t *p)
{
    if (p->recursion_depth >= p->recursion_limit) {
        return -1;
    }

    p->recursion_depth++;
    return 0;
}

static void
rust_parser_leave(rust_parser_t *p)
{
    if (p->recursion_depth > 0) {
        p->recursion_depth--;
    }
}

static int
rust_parser_add_backref(rust_parser_t *p, const char *start, const char *end)
{
    rust_backref_t *next;
    size_t cap;

    if (p == NULL || start == NULL || end == NULL || end < start) {
        return -1;
    }

    if (p->backref_count == p->backref_cap) {
        cap = (p->backref_cap == 0u) ? 16u : p->backref_cap * 2u;
        if (cap < p->backref_cap) {
            return -1;
        }

        next = (rust_backref_t *)realloc(p->backrefs, cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }

        p->backrefs = next;
        p->backref_cap = cap;
    }

    p->backrefs[p->backref_count].start = start;
    p->backrefs[p->backref_count].end = end;
    p->backref_count++;
    return 0;
}

static int
rust_parse_v0_symbol(rust_parser_t *p)
{
    if (rust_parser_enter(p) != 0) {
        return -1;
    }

    if (rust_parser_add_backref(p, p->cur, p->cur) != 0) {
        rust_parser_leave(p);
        return -1;
    }

    if (rust_buf_append(&p->out, "", 0u) != 0 || rust_buf_appendc(&p->out, '\0') != 0) {
        rust_parser_leave(p);
        return -1;
    }

    p->out.len = 0u;
    p->out.data[0] = '\0';

    rust_parser_leave(p);
    return -1;
}

static char *
rust_demangle_v0(const char *mangled, int options)
{
    rust_parser_t parser;
    char *ret;

    if (!rust_is_v0(mangled)) {
        return NULL;
    }

    if (rust_parser_init(&parser, mangled, options) != 0) {
        return NULL;
    }

    parser.cur = mangled + 2;
    if (rust_parse_v0_symbol(&parser) != 0 || parser.cur[0] != '\0') {
        rust_parser_destroy(&parser);
        return NULL;
    }

    ret = rust_buf_take(&parser.out);
    rust_parser_destroy(&parser);
    return ret;
}

static char *
rust_demangle_legacy(const char *mangled, int options)
{
    (void)mangled;
    (void)options;
    return (char *)0;
}

char *
demangle_rust(const char *mangled, int options)
{
    if (mangled == NULL || mangled[0] == '\0') {
        return NULL;
    }

    if ((options & DEMANGLE_RUST) != 0) {
        if (!rust_is_v0(mangled)) {
            return NULL;
        }
        return rust_demangle_v0(mangled, options);
    }

    if (rust_is_v0(mangled)) {
        return rust_demangle_v0(mangled, options);
    }

    if (rust_is_legacy(mangled)) {
        return rust_demangle_legacy(mangled, options);
    }

    return NULL;
}
