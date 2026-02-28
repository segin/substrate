#include "demangle_internal.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <demangle.h>

#define DM_BUF_INITIAL_CAP 256u
#define DM_SUBST_CAP 256u
#define DM_TEMPLATE_STACK_CAP 64u
#define DM_RECURSION_DEFAULT_LIMIT 256

typedef struct dm_buf {
    char *data;
    size_t len;
    size_t cap;
} dm_buf_t;

typedef struct dm_subst {
    size_t off;
    size_t len;
} dm_subst_t;

typedef struct dm_template_frame {
    size_t output_off;
} dm_template_frame_t;

typedef struct dm_itanium_parser {
    const char *input;
    const char *cur;
    int options;

    int recursion_depth;
    int recursion_limit;

    dm_buf_t out;

    dm_subst_t substitutions[DM_SUBST_CAP];
    size_t substitution_count;

    dm_template_frame_t template_frames[DM_TEMPLATE_STACK_CAP];
    size_t template_depth;
} dm_itanium_parser_t;

static int buf_appendc(dm_buf_t *buf, char ch);
static int buf_printf(dm_buf_t *buf, const char *fmt, ...);
static int parser_push_template_frame(dm_itanium_parser_t *p);
static int parser_pop_template_frame(dm_itanium_parser_t *p);
static int parser_add_substitution(dm_itanium_parser_t *p, size_t off, size_t len);
static int parser_lookup_substitution(dm_itanium_parser_t *p, size_t idx);
static int parse_number(dm_itanium_parser_t *p, size_t *out);

static void
keep_section2a_symbols(void)
{
    (void)&buf_appendc;
    (void)&buf_printf;
    (void)&parser_push_template_frame;
    (void)&parser_pop_template_frame;
    (void)&parser_add_substitution;
    (void)&parser_lookup_substitution;
    (void)&parse_number;
}

static int
buf_reserve(dm_buf_t *buf, size_t extra)
{
    size_t need;
    size_t next_cap;
    char *next;

    if (buf == NULL) {
        return -1;
    }

    need = buf->len + extra + 1u;
    if (need <= buf->cap) {
        return 0;
    }

    next_cap = (buf->cap == 0u) ? DM_BUF_INITIAL_CAP : buf->cap;
    while (next_cap < need) {
        size_t doubled = next_cap << 1;
        if (doubled < next_cap) {
            return -1;
        }
        next_cap = doubled;
    }

    next = (char *)realloc(buf->data, next_cap);
    if (next == NULL) {
        return -1;
    }

    buf->data = next;
    buf->cap = next_cap;
    return 0;
}

static int
buf_append(dm_buf_t *buf, const char *s, size_t n)
{
    if (buf == NULL || s == NULL) {
        return -1;
    }

    if (buf_reserve(buf, n) != 0) {
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
buf_appendc(dm_buf_t *buf, char ch)
{
    if (buf_reserve(buf, 1u) != 0) {
        return -1;
    }

    buf->data[buf->len++] = ch;
    buf->data[buf->len] = '\0';
    return 0;
}

static int
buf_printf(dm_buf_t *buf, const char *fmt, ...)
{
    va_list ap;
    va_list ap_copy;
    int need;

    if (buf == NULL || fmt == NULL) {
        return -1;
    }

    va_start(ap, fmt);
    va_copy(ap_copy, ap);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) {
        va_end(ap_copy);
        return -1;
    }

    if (buf_reserve(buf, (size_t)need) != 0) {
        va_end(ap_copy);
        return -1;
    }

    if (vsnprintf(buf->data + buf->len, buf->cap - buf->len, fmt, ap_copy) != need) {
        va_end(ap_copy);
        return -1;
    }
    va_end(ap_copy);

    buf->len += (size_t)need;
    return 0;
}

static char *
buf_take(dm_buf_t *buf)
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
buf_destroy(dm_buf_t *buf)
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
parser_begin(dm_itanium_parser_t *p)
{
    memset(p, 0, sizeof(*p));
    p->recursion_limit = DM_RECURSION_DEFAULT_LIMIT;
    keep_section2a_symbols();

    if (buf_reserve(&p->out, 0u) != 0) {
        return -1;
    }

    p->out.data[0] = '\0';
    return 0;
}

static int
parser_enter(dm_itanium_parser_t *p)
{
    if (p->recursion_depth >= p->recursion_limit) {
        return -1;
    }

    p->recursion_depth++;
    return 0;
}

static void
parser_leave(dm_itanium_parser_t *p)
{
    if (p->recursion_depth > 0) {
        p->recursion_depth--;
    }
}

static int
parser_push_template_frame(dm_itanium_parser_t *p)
{
    if (p->template_depth >= DM_TEMPLATE_STACK_CAP) {
        return -1;
    }

    p->template_frames[p->template_depth].output_off = p->out.len;
    p->template_depth++;
    return 0;
}

static int
parser_pop_template_frame(dm_itanium_parser_t *p)
{
    if (p->template_depth == 0u) {
        return -1;
    }

    p->template_depth--;
    return 0;
}

static int
parser_add_substitution(dm_itanium_parser_t *p, size_t off, size_t len)
{
    if (p->substitution_count >= DM_SUBST_CAP) {
        return -1;
    }

    p->substitutions[p->substitution_count].off = off;
    p->substitutions[p->substitution_count].len = len;
    p->substitution_count++;
    return 0;
}

static int
parser_lookup_substitution(dm_itanium_parser_t *p, size_t idx)
{
    dm_subst_t *s;

    if (idx >= p->substitution_count) {
        return -1;
    }

    s = &p->substitutions[idx];
    if (s->off + s->len > p->out.len) {
        return -1;
    }

    return buf_append(&p->out, p->out.data + s->off, s->len);
}

static int
is_eof(const dm_itanium_parser_t *p)
{
    return p->cur == NULL || p->cur[0] == '\0';
}

static int
parse_number(dm_itanium_parser_t *p, size_t *out)
{
    const char *s;
    size_t v;

    if (p == NULL || out == NULL || !isdigit((unsigned char)p->cur[0])) {
        return -1;
    }

    s = p->cur;
    v = 0u;
    while (*s != '\0' && isdigit((unsigned char)*s)) {
        unsigned d = (unsigned)(*s - '0');
        size_t nv = v * 10u + d;
        if (nv < v) {
            return -1;
        }
        v = nv;
        s++;
    }

    p->cur = s;
    *out = v;
    return 0;
}

/*
 * Section 2a only lays down parser architecture. Grammar production
 * handlers are filled in by subsequent subsection commits.
 */
static int
parse_encoding(dm_itanium_parser_t *p)
{
    (void)p;
    return -1;
}

char *
demangle_itanium(const char *mangled, int options)
{
    dm_itanium_parser_t parser;
    char *ret;

    if (mangled == NULL || mangled[0] == '\0') {
        return NULL;
    }

    if ((options & DEMANGLE_TYPES) == 0 && !(mangled[0] == '_' && mangled[1] == 'Z')) {
        return NULL;
    }

    if (parser_begin(&parser) != 0) {
        return NULL;
    }

    parser.input = mangled;
    parser.cur = mangled;
    parser.options = options;

    if (mangled[0] == '_' && mangled[1] == 'Z') {
        parser.cur += 2;
    }

    if (parser_enter(&parser) != 0) {
        buf_destroy(&parser.out);
        return NULL;
    }

    if (parse_encoding(&parser) != 0 || !is_eof(&parser)) {
        parser_leave(&parser);
        buf_destroy(&parser.out);
        return NULL;
    }

    parser_leave(&parser);

    ret = buf_take(&parser.out);
    buf_destroy(&parser.out);
    return ret;
}
