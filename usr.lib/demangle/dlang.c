#include "demangle_internal.h"

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <demangle.h>

#define DLANG_BUF_INITIAL_CAP 256u
#define DLANG_RECURSION_DEFAULT_LIMIT 128

typedef struct dlang_buf {
    char *data;
    size_t len;
    size_t cap;
} dlang_buf_t;

typedef struct dlang_name_ref {
    size_t off;
    size_t len;
} dlang_name_ref_t;

typedef struct dlang_parser {
    const char *input;
    const char *cur;
    int options;

    int recursion_depth;
    int recursion_limit;

    dlang_buf_t out;

    dlang_name_ref_t *name_refs;
    size_t name_ref_count;
    size_t name_ref_cap;
} dlang_parser_t;

static int starts_with(const char *s, const char *prefix);
static int dlang_is_mangled(const char *mangled);

static int dlang_buf_reserve(dlang_buf_t *buf, size_t extra);
static int dlang_buf_append(dlang_buf_t *buf, const char *s, size_t n);
static int dlang_buf_appendc(dlang_buf_t *buf, char ch);
static char *dlang_buf_take(dlang_buf_t *buf);
static void dlang_buf_destroy(dlang_buf_t *buf);

static int dlang_parser_init(dlang_parser_t *p, const char *mangled, int options);
static void dlang_parser_destroy(dlang_parser_t *p);
static int dlang_parser_enter(dlang_parser_t *p);
static void dlang_parser_leave(dlang_parser_t *p);
static int dlang_parser_add_name_ref(dlang_parser_t *p, size_t off, size_t len);

static int dlang_parse_number(dlang_parser_t *p, size_t *out);
static int dlang_parse_lname(dlang_parser_t *p);
static int dlang_parse_template_instance_name(dlang_parser_t *p);
static int dlang_parse_symbol_name(dlang_parser_t *p);
static int dlang_parse_qualified_name(dlang_parser_t *p);
static int dlang_parse_type(dlang_parser_t *p);
static int dlang_parse_mangled_name(dlang_parser_t *p);
static int dlang_is_type_start(char ch);

static char *dlang_demangle_symbol(const char *mangled, int options);

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
dlang_is_mangled(const char *mangled)
{
    return starts_with(mangled, "_D");
}

static int
dlang_buf_reserve(dlang_buf_t *buf, size_t extra)
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

    cap = (buf->cap == 0u) ? DLANG_BUF_INITIAL_CAP : buf->cap;
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
dlang_buf_append(dlang_buf_t *buf, const char *s, size_t n)
{
    if (buf == NULL || s == NULL) {
        return -1;
    }

    if (dlang_buf_reserve(buf, n) != 0) {
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
dlang_buf_appendc(dlang_buf_t *buf, char ch)
{
    if (dlang_buf_reserve(buf, 1u) != 0) {
        return -1;
    }

    buf->data[buf->len++] = ch;
    buf->data[buf->len] = '\0';
    return 0;
}

static char *
dlang_buf_take(dlang_buf_t *buf)
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
dlang_buf_destroy(dlang_buf_t *buf)
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
dlang_parser_init(dlang_parser_t *p, const char *mangled, int options)
{
    memset(p, 0, sizeof(*p));
    p->input = mangled;
    p->cur = mangled;
    p->options = options;
    p->recursion_limit = DLANG_RECURSION_DEFAULT_LIMIT;

    if (dlang_buf_reserve(&p->out, 0u) != 0) {
        return -1;
    }

    p->out.data[0] = '\0';
    return 0;
}

static void
dlang_parser_destroy(dlang_parser_t *p)
{
    if (p == NULL) {
        return;
    }

    dlang_buf_destroy(&p->out);
    free(p->name_refs);
    p->name_refs = NULL;
    p->name_ref_count = 0u;
    p->name_ref_cap = 0u;
}

static int
dlang_parser_enter(dlang_parser_t *p)
{
    if (p->recursion_depth >= p->recursion_limit) {
        return -1;
    }

    p->recursion_depth++;
    return 0;
}

static void
dlang_parser_leave(dlang_parser_t *p)
{
    if (p->recursion_depth > 0) {
        p->recursion_depth--;
    }
}

static int
dlang_parser_add_name_ref(dlang_parser_t *p, size_t off, size_t len)
{
    dlang_name_ref_t *next;
    size_t cap;

    if (p == NULL) {
        return -1;
    }

    if (p->name_ref_count == p->name_ref_cap) {
        cap = (p->name_ref_cap == 0u) ? 16u : p->name_ref_cap * 2u;
        if (cap < p->name_ref_cap) {
            return -1;
        }

        next = (dlang_name_ref_t *)realloc(p->name_refs, cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }

        p->name_refs = next;
        p->name_ref_cap = cap;
    }

    p->name_refs[p->name_ref_count].off = off;
    p->name_refs[p->name_ref_count].len = len;
    p->name_ref_count++;
    return 0;
}

static int
dlang_parse_number(dlang_parser_t *p, size_t *out)
{
    size_t v;

    if (p == NULL || out == NULL || !isdigit((unsigned char)p->cur[0])) {
        return -1;
    }

    v = 0u;
    while (isdigit((unsigned char)p->cur[0])) {
        size_t nv = v * 10u + (size_t)(p->cur[0] - '0');
        if (nv < v) {
            return -1;
        }
        v = nv;
        p->cur++;
    }

    *out = v;
    return 0;
}

static int
dlang_parse_lname(dlang_parser_t *p)
{
    const char *s;
    size_t len;
    size_t off;

    if (dlang_parse_number(p, &len) != 0 || len == 0u) {
        return -1;
    }

    s = p->cur;
    if (s[len - 1u] == '\0') {
        return -1;
    }

    off = p->out.len;
    if (dlang_buf_append(&p->out, s, len) != 0) {
        return -1;
    }

    p->cur += len;
    return dlang_parser_add_name_ref(p, off, len);
}

static int
dlang_parse_template_instance_name(dlang_parser_t *p)
{
    int first;

    if (!(p->cur[0] == '_' && p->cur[1] == '_' && p->cur[2] == 'T')) {
        return -1;
    }

    p->cur += 3;
    if (dlang_parse_lname(p) != 0 || dlang_buf_append(&p->out, "!(", 2u) != 0) {
        return -1;
    }

    first = 1;
    while (p->cur[0] != '\0' && p->cur[0] != 'Z') {
        if (!first && dlang_buf_append(&p->out, ", ", 2u) != 0) {
            return -1;
        }

        if ((p->cur[0] == '_' && p->cur[1] == '_' && p->cur[2] == 'T')) {
            if (dlang_parse_template_instance_name(p) != 0) {
                return -1;
            }
        } else if (isdigit((unsigned char)p->cur[0])) {
            if (dlang_parse_lname(p) != 0) {
                return -1;
            }
        } else {
            if (dlang_buf_appendc(&p->out, '?') != 0) {
                return -1;
            }
            p->cur++;
        }

        first = 0;
    }

    if (p->cur[0] != 'Z' || dlang_buf_appendc(&p->out, ')') != 0) {
        return -1;
    }

    p->cur++;
    return 0;
}

static int
dlang_parse_symbol_name(dlang_parser_t *p)
{
    if (p->cur[0] == '_' && p->cur[1] == '_' && p->cur[2] == 'T') {
        return dlang_parse_template_instance_name(p);
    }

    return dlang_parse_lname(p);
}

static int
dlang_parse_qualified_name(dlang_parser_t *p)
{
    int first;

    first = 1;
    while (isdigit((unsigned char)p->cur[0]) ||
           (p->cur[0] == '_' && p->cur[1] == '_' && p->cur[2] == 'T')) {
        if (!first && dlang_buf_appendc(&p->out, '.') != 0) {
            return -1;
        }

        if (dlang_parse_symbol_name(p) != 0) {
            return -1;
        }

        first = 0;
    }

    return first ? -1 : 0;
}

static int
dlang_is_type_start(char ch)
{
    if (ch == '\0') {
        return 0;
    }

    switch (ch) {
    case 'v': case 'g': case 'h': case 's': case 't': case 'i': case 'k':
    case 'l': case 'm': case 'f': case 'd': case 'e': case 'o': case 'p':
    case 'j': case 'q': case 'r': case 'c': case 'b': case 'a': case 'u':
    case 'w': case 'A': case 'G': case 'H': case 'P': case 'E': case 'C':
    case 'S': case 'I': case 'D': case 'F': case 'U': case 'W': case 'V':
    case 'R': case 'B': case 'n': case 'x': case 'y': case 'O': case 'N':
        return 1;
    default:
        return 0;
    }
}

static int
dlang_parse_type(dlang_parser_t *p)
{
    if (p->cur[0] == '\0') {
        return -1;
    }

    while (p->cur[0] == 'x' || p->cur[0] == 'y' || p->cur[0] == 'O') {
        p->cur++;
    }
    if (p->cur[0] == 'N' && p->cur[1] == 'g') {
        p->cur += 2;
    }

    switch (p->cur[0]) {
    case 'A':
        p->cur++;
        return dlang_parse_type(p);
    case 'G':
        p->cur++;
        if (dlang_parse_number(p, &(size_t){0}) != 0) {
            return -1;
        }
        return dlang_parse_type(p);
    case 'H':
        p->cur++;
        if (dlang_parse_type(p) != 0 || dlang_parse_type(p) != 0) {
            return -1;
        }
        return 0;
    case 'P':
    case 'D':
        p->cur++;
        return dlang_parse_type(p);
    case 'E':
    case 'C':
    case 'S':
    case 'I':
        p->cur++;
        return dlang_parse_qualified_name(p);
    case 'B':
        p->cur++;
        if (dlang_parse_number(p, &(size_t){0}) != 0) {
            return -1;
        }
        return dlang_parse_type(p);
    case 'F':
    case 'U':
    case 'W':
    case 'V':
    case 'R':
        p->cur++;
        while (p->cur[0] != '\0' && p->cur[0] != 'Z') {
            while (p->cur[0] == 'J' || p->cur[0] == 'K' || p->cur[0] == 'L' ||
                   p->cur[0] == 'M' || p->cur[0] == 'N') {
                p->cur++;
            }
            if (p->cur[0] == 'X') {
                p->cur++;
                break;
            }
            if (dlang_parse_type(p) != 0) {
                return -1;
            }
        }
        if (p->cur[0] != 'Z') {
            return -1;
        }
        p->cur++;
        return dlang_parse_type(p);
    default:
        if (isalpha((unsigned char)p->cur[0])) {
            p->cur++;
            return 0;
        }
        return -1;
    }
}

static int
dlang_parse_mangled_name(dlang_parser_t *p)
{
    if (dlang_parser_enter(p) != 0) {
        return -1;
    }

    if (dlang_parse_qualified_name(p) != 0) {
        dlang_parser_leave(p);
        return -1;
    }

    if (!dlang_is_type_start(p->cur[0])) {
        dlang_parser_leave(p);
        return -1;
    }

    if (dlang_parse_type(p) != 0) {
        dlang_parser_leave(p);
        return -1;
    }

    if (p->cur[0] != '\0') {
        if (dlang_parse_type(p) != 0) {
            dlang_parser_leave(p);
            return -1;
        }
    }

    dlang_parser_leave(p);
    return 0;
}

static char *
dlang_demangle_symbol(const char *mangled, int options)
{
    dlang_parser_t p;
    char *ret;

    if (!dlang_is_mangled(mangled)) {
        return NULL;
    }

    if (dlang_parser_init(&p, mangled, options) != 0) {
        return NULL;
    }

    p.cur = mangled + 2;
    if (dlang_parse_mangled_name(&p) != 0 || p.cur[0] != '\0') {
        dlang_parser_destroy(&p);
        return NULL;
    }

    ret = dlang_buf_take(&p.out);
    dlang_parser_destroy(&p);
    return ret;
}

char *
demangle_dlang(const char *mangled, int options)
{
    if (mangled == NULL || mangled[0] == '\0') {
        return NULL;
    }

    if ((options & DEMANGLE_DLANG) != 0) {
        if (!dlang_is_mangled(mangled)) {
            return NULL;
        }
        return dlang_demangle_symbol(mangled, options);
    }

    if (dlang_is_mangled(mangled)) {
        return dlang_demangle_symbol(mangled, options);
    }

    return NULL;
}
