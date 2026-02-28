#include "demangle_internal.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <demangle.h>

#define DLANG_RECURSION_DEFAULT_LIMIT 128

enum {
    DLANG_QUAL_CONST = 1u << 0,
    DLANG_QUAL_IMMUTABLE = 1u << 1,
    DLANG_QUAL_SHARED = 1u << 2,
    DLANG_QUAL_INOUT = 1u << 3
};

enum {
    DLANG_ATTR_PURE = 1u << 0,
    DLANG_ATTR_NOTHROW = 1u << 1,
    DLANG_ATTR_REF = 1u << 2,
    DLANG_ATTR_PROPERTY = 1u << 3,
    DLANG_ATTR_TRUSTED = 1u << 4,
    DLANG_ATTR_SAFE = 1u << 5,
    DLANG_ATTR_NOGC = 1u << 6,
    DLANG_ATTR_RETREF = 1u << 7
};

typedef demangle_buf_t dlang_buf_t;

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
static int dlang_is_runtime_symbol(const char *mangled);
static char *dlang_strdup(const char *s);

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
static int dlang_parser_append_name_ref(dlang_parser_t *p, size_t idx);

static int dlang_parse_number(dlang_parser_t *p, size_t *out);
static int dlang_parse_lname(dlang_parser_t *p);
static int dlang_parse_template_value(dlang_parser_t *p);
static int dlang_parse_template_arg(dlang_parser_t *p);
static int dlang_parse_template_instance_name(dlang_parser_t *p);
static int dlang_parse_symbol_name(dlang_parser_t *p);
static int dlang_parse_qualified_name(dlang_parser_t *p);
static int dlang_parse_type(dlang_parser_t *p);
static int dlang_parse_type_core(dlang_parser_t *p);
static int dlang_parse_function_type(dlang_parser_t *p, char cconv);
static int dlang_parse_mangled_name(dlang_parser_t *p);
static int dlang_is_type_start(char ch);
static int dlang_is_attr_code(char ch);

static char *dlang_take_segment(dlang_parser_t *p, size_t off);
static char *dlang_wrap_type(const char *prefix, const char *inner, const char *suffix);
static int dlang_append_attrs(dlang_buf_t *buf, unsigned attrs);
static int dlang_format_special_name(dlang_buf_t *buf, const char *s, size_t len, int *formatted);
static char *dlang_delegate_from_function(const char *inner);

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
dlang_is_runtime_symbol(const char *mangled)
{
    return starts_with(mangled, "_d_");
}

static char *
dlang_strdup(const char *s)
{
    size_t n;
    char *ret;

    if (s == NULL) {
        return NULL;
    }

    n = strlen(s);
    ret = (char *)malloc(n + 1u);
    if (ret == NULL) {
        return NULL;
    }

    memcpy(ret, s, n + 1u);
    return ret;
}

static int
dlang_buf_reserve(dlang_buf_t *buf, size_t extra)
{
    return demangle_buf_reserve(buf, extra);
}

static int
dlang_buf_append(dlang_buf_t *buf, const char *s, size_t n)
{
    return demangle_buf_append(buf, s, n);
}

static int
dlang_buf_appendc(dlang_buf_t *buf, char ch)
{
    return demangle_buf_appendc(buf, ch);
}

static char *
dlang_buf_take(dlang_buf_t *buf)
{
    return demangle_buf_take(buf);
}

static void
dlang_buf_destroy(dlang_buf_t *buf)
{
    demangle_buf_destroy(buf);
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

    if (p == NULL || off + len > p->out.len) {
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
dlang_parser_append_name_ref(dlang_parser_t *p, size_t idx)
{
    dlang_name_ref_t *ref;

    if (p == NULL || idx >= p->name_ref_count) {
        return -1;
    }

    ref = &p->name_refs[idx];
    if (ref->off + ref->len > p->out.len) {
        return -1;
    }

    return dlang_buf_append(&p->out, p->out.data + ref->off, ref->len);
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
dlang_format_special_name(dlang_buf_t *buf, const char *s, size_t len, int *formatted)
{
    if (formatted != NULL) {
        *formatted = 0;
    }

    if (len >= 8u && memcmp(s, "__lambda", 8u) == 0) {
        if (dlang_buf_append(buf, "{lambda#", 8u) != 0 ||
            dlang_buf_append(buf, s + 8u, len - 8u) != 0 ||
            dlang_buf_appendc(buf, '}') != 0) {
            return -1;
        }
        if (formatted != NULL) {
            *formatted = 1;
        }
        return 0;
    }

    if (len >= 11u && memcmp(s, "__dgliteral", 11u) == 0) {
        if (dlang_buf_append(buf, "{delegate literal#", 18u) != 0 ||
            dlang_buf_append(buf, s + 11u, len - 11u) != 0 ||
            dlang_buf_appendc(buf, '}') != 0) {
            return -1;
        }
        if (formatted != NULL) {
            *formatted = 1;
        }
        return 0;
    }

    if (len >= 10u && memcmp(s, "__unittest", 10u) == 0) {
        if (dlang_buf_append(buf, "{unittest#", 10u) != 0 ||
            dlang_buf_append(buf, s + 10u, len - 10u) != 0 ||
            dlang_buf_appendc(buf, '}') != 0) {
            return -1;
        }
        if (formatted != NULL) {
            *formatted = 1;
        }
        return 0;
    }

    if (len == 9u && memcmp(s, "__modctor", 9u) == 0) {
        if (dlang_buf_append(buf, "{module ctor}", 13u) != 0) {
            return -1;
        }
        if (formatted != NULL) {
            *formatted = 1;
        }
        return 0;
    }

    if (len == 9u && memcmp(s, "__moddtor", 9u) == 0) {
        if (dlang_buf_append(buf, "{module dtor}", 13u) != 0) {
            return -1;
        }
        if (formatted != NULL) {
            *formatted = 1;
        }
        return 0;
    }

    if (len >= 6u && memcmp(s, "__aggr", 6u) == 0) {
        if (dlang_buf_append(buf, "{aggregate#", 11u) != 0 ||
            dlang_buf_append(buf, s + 6u, len - 6u) != 0 ||
            dlang_buf_appendc(buf, '}') != 0) {
            return -1;
        }
        if (formatted != NULL) {
            *formatted = 1;
        }
        return 0;
    }

    if (len == 7u && memcmp(s, "__initZ", 7u) == 0) {
        if (dlang_buf_append(buf, "{init}", 6u) != 0) {
            return -1;
        }
        if (formatted != NULL) {
            *formatted = 1;
        }
        return 0;
    }

    if (len == 8u && memcmp(s, "__ClassZ", 8u) == 0) {
        if (dlang_buf_append(buf, "{classinfo}", 11u) != 0) {
            return -1;
        }
        if (formatted != NULL) {
            *formatted = 1;
        }
        return 0;
    }

    if (len == 7u && memcmp(s, "__vtblZ", 7u) == 0) {
        if (dlang_buf_append(buf, "{vtable}", 8u) != 0) {
            return -1;
        }
        if (formatted != NULL) {
            *formatted = 1;
        }
        return 0;
    }

    if (len == 12u && memcmp(s, "__InterfaceZ", 12u) == 0) {
        if (dlang_buf_append(buf, "{interfaceinfo}", 15u) != 0) {
            return -1;
        }
        if (formatted != NULL) {
            *formatted = 1;
        }
        return 0;
    }

    return 0;
}

static int
dlang_parse_lname(dlang_parser_t *p)
{
    const char *s;
    size_t len;
    size_t off;
    int formatted;

    if (dlang_parse_number(p, &len) != 0 || len == 0u) {
        return -1;
    }

    s = p->cur;
    if (s[len - 1u] == '\0') {
        return -1;
    }

    off = p->out.len;
    formatted = 0;
    if (dlang_format_special_name(&p->out, s, len, &formatted) != 0) {
        return -1;
    }
    if (!formatted && dlang_buf_append(&p->out, s, len) != 0) {
        return -1;
    }

    p->cur += len;
    return dlang_parser_add_name_ref(p, off, p->out.len - off);
}

static int
dlang_parse_template_value(dlang_parser_t *p)
{
    if (p->cur[0] == 'n') {
        p->cur++;
        if (dlang_buf_appendc(&p->out, '-') != 0) {
            return -1;
        }
    }

    if (isdigit((unsigned char)p->cur[0])) {
        size_t n = 0u;
        while (isdigit((unsigned char)p->cur[0])) {
            n = n * 10u + (size_t)(p->cur[0] - '0');
            if (dlang_buf_appendc(&p->out, p->cur[0]) != 0) {
                return -1;
            }
            p->cur++;
        }

        if (p->cur[0] == '_') {
            p->cur++;
            return 0;
        }

        if (n > 0u && p->cur[n - 1u] != '\0') {
            if (dlang_buf_appendc(&p->out, '"') != 0 ||
                dlang_buf_append(&p->out, p->cur, n) != 0 ||
                dlang_buf_appendc(&p->out, '"') != 0) {
                return -1;
            }
            p->cur += n;
            return 0;
        }
    }

    if (p->cur[0] == 'N') {
        p->cur++;
        return dlang_buf_append(&p->out, "null", 4u);
    }

    if (p->cur[0] == '\0') {
        return -1;
    }

    if (dlang_buf_appendc(&p->out, p->cur[0]) != 0) {
        return -1;
    }
    p->cur++;
    return 0;
}

static int
dlang_parse_template_arg(dlang_parser_t *p)
{
    size_t off;
    char *ty;
    char *val;
    size_t n;

    if (p->cur[0] == 'T') {
        p->cur++;
        return dlang_parse_type(p);
    }

    if (p->cur[0] == 'V') {
        p->cur++;
        off = p->out.len;
        if (dlang_parse_type(p) != 0) {
            return -1;
        }
        ty = dlang_take_segment(p, off);
        if (ty == NULL) {
            return -1;
        }

        off = p->out.len;
        if (dlang_parse_template_value(p) != 0) {
            free(ty);
            return -1;
        }
        val = dlang_take_segment(p, off);
        if (val == NULL) {
            free(ty);
            return -1;
        }

        if (dlang_buf_append(&p->out, ty, strlen(ty)) != 0 ||
            dlang_buf_appendc(&p->out, '(') != 0 ||
            dlang_buf_append(&p->out, val, strlen(val)) != 0 ||
            dlang_buf_appendc(&p->out, ')') != 0) {
            free(ty);
            free(val);
            return -1;
        }

        free(ty);
        free(val);
        return 0;
    }

    if (p->cur[0] == 'S') {
        p->cur++;
        return dlang_parse_qualified_name(p);
    }

    if (p->cur[0] == 'X') {
        p->cur++;
        if (dlang_parse_number(p, &n) != 0 || n == 0u || p->cur[n - 1u] == '\0') {
            return -1;
        }
        if (dlang_buf_append(&p->out, p->cur, n) != 0) {
            return -1;
        }
        p->cur += n;
        return 0;
    }

    return -1;
}

static int
dlang_parse_template_instance_name(dlang_parser_t *p)
{
    int first;
    size_t off;

    if (dlang_parser_enter(p) != 0) {
        return -1;
    }

    if (!(p->cur[0] == '_' && p->cur[1] == '_' && p->cur[2] == 'T')) {
        dlang_parser_leave(p);
        return -1;
    }

    off = p->out.len;
    p->cur += 3;

    if (dlang_parse_lname(p) != 0 || dlang_buf_append(&p->out, "!(", 2u) != 0) {
        dlang_parser_leave(p);
        return -1;
    }

    first = 1;
    while (p->cur[0] != '\0' && p->cur[0] != 'Z') {
        if (!first && dlang_buf_append(&p->out, ", ", 2u) != 0) {
            dlang_parser_leave(p);
            return -1;
        }

        if (dlang_parse_template_arg(p) != 0) {
            dlang_parser_leave(p);
            return -1;
        }

        first = 0;
    }

    if (p->cur[0] != 'Z' || dlang_buf_appendc(&p->out, ')') != 0) {
        dlang_parser_leave(p);
        return -1;
    }

    p->cur++;
    if (dlang_parser_add_name_ref(p, off, p->out.len - off) != 0) {
        dlang_parser_leave(p);
        return -1;
    }

    dlang_parser_leave(p);
    return 0;
}

static int
dlang_parse_symbol_name(dlang_parser_t *p)
{
    if (p->cur[0] == '_' && p->cur[1] == '_' && p->cur[2] == 'T') {
        return dlang_parse_template_instance_name(p);
    }

    if (p->cur[0] == 'Q') {
        size_t idx;

        p->cur++;
        if (dlang_parse_number(p, &idx) != 0) {
            return -1;
        }
        return dlang_parser_append_name_ref(p, idx);
    }

    return dlang_parse_lname(p);
}

static int
dlang_parse_qualified_name(dlang_parser_t *p)
{
    int first;

    first = 1;
    while (isdigit((unsigned char)p->cur[0]) ||
           (p->cur[0] == '_' && p->cur[1] == '_' && p->cur[2] == 'T') ||
           p->cur[0] == 'Q') {
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

static char *
dlang_take_segment(dlang_parser_t *p, size_t off)
{
    size_t len;
    char *seg;

    if (off > p->out.len) {
        return NULL;
    }

    len = p->out.len - off;
    seg = (char *)malloc(len + 1u);
    if (seg == NULL) {
        return NULL;
    }

    if (len > 0u) {
        memcpy(seg, p->out.data + off, len);
    }
    seg[len] = '\0';

    p->out.len = off;
    p->out.data[p->out.len] = '\0';
    return seg;
}

static char *
dlang_wrap_type(const char *prefix, const char *inner, const char *suffix)
{
    size_t a;
    size_t b;
    size_t c;
    char *ret;

    if (prefix == NULL || inner == NULL || suffix == NULL) {
        return NULL;
    }

    a = strlen(prefix);
    b = strlen(inner);
    c = strlen(suffix);

    ret = (char *)malloc(a + b + c + 1u);
    if (ret == NULL) {
        return NULL;
    }

    memcpy(ret, prefix, a);
    memcpy(ret + a, inner, b);
    memcpy(ret + a + b, suffix, c);
    ret[a + b + c] = '\0';
    return ret;
}

static int
dlang_append_attrs(dlang_buf_t *buf, unsigned attrs)
{
    if ((attrs & DLANG_ATTR_PURE) != 0 && dlang_buf_append(buf, " pure", 5u) != 0) {
        return -1;
    }
    if ((attrs & DLANG_ATTR_NOTHROW) != 0 && dlang_buf_append(buf, " nothrow", 8u) != 0) {
        return -1;
    }
    if ((attrs & DLANG_ATTR_REF) != 0 && dlang_buf_append(buf, " ref", 4u) != 0) {
        return -1;
    }
    if ((attrs & DLANG_ATTR_PROPERTY) != 0 && dlang_buf_append(buf, " @property", 10u) != 0) {
        return -1;
    }
    if ((attrs & DLANG_ATTR_TRUSTED) != 0 && dlang_buf_append(buf, " @trusted", 9u) != 0) {
        return -1;
    }
    if ((attrs & DLANG_ATTR_SAFE) != 0 && dlang_buf_append(buf, " @safe", 6u) != 0) {
        return -1;
    }
    if ((attrs & DLANG_ATTR_NOGC) != 0 && dlang_buf_append(buf, " @nogc", 6u) != 0) {
        return -1;
    }
    if ((attrs & DLANG_ATTR_RETREF) != 0 && dlang_buf_append(buf, " return ref", 11u) != 0) {
        return -1;
    }

    return 0;
}

static int
dlang_is_attr_code(char ch)
{
    switch (ch) {
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'i':
    case 'j':
        return 1;
    default:
        return 0;
    }
}

static char *
dlang_delegate_from_function(const char *inner)
{
    const char *needle;
    const char *pos;
    size_t head_len;
    size_t tail_len;
    char *out;

    if (inner == NULL) {
        return NULL;
    }

    needle = " function(";
    pos = strstr(inner, needle);
    if (pos == NULL) {
        return dlang_wrap_type("", inner, " delegate");
    }

    head_len = (size_t)(pos - inner);
    tail_len = strlen(pos + strlen(needle));
    out = (char *)malloc(head_len + 10u + tail_len + 1u);
    if (out == NULL) {
        return NULL;
    }

    if (head_len > 0u) {
        memcpy(out, inner, head_len);
    }
    memcpy(out + head_len, " delegate(", 10u);
    memcpy(out + head_len + 10u, pos + strlen(needle), tail_len);
    out[head_len + 10u + tail_len] = '\0';
    return out;
}

static int
dlang_parse_function_type(dlang_parser_t *p, char cconv)
{
    unsigned attrs;
    char **params;
    size_t param_count;
    size_t param_cap;
    char *ret;
    size_t ret_off;

    attrs = 0u;
    params = NULL;
    param_count = 0u;
    param_cap = 0u;
    ret = NULL;

    while (p->cur[0] == 'N' && dlang_is_attr_code(p->cur[1])) {
        switch (p->cur[1]) {
        case 'a': attrs |= DLANG_ATTR_PURE; break;
        case 'b': attrs |= DLANG_ATTR_NOTHROW; break;
        case 'c': attrs |= DLANG_ATTR_REF; break;
        case 'd': attrs |= DLANG_ATTR_PROPERTY; break;
        case 'e': attrs |= DLANG_ATTR_TRUSTED; break;
        case 'f': attrs |= DLANG_ATTR_SAFE; break;
        case 'i': attrs |= DLANG_ATTR_NOGC; break;
        case 'j': attrs |= DLANG_ATTR_RETREF; break;
        default: break;
        }
        p->cur += 2;
    }

    while (p->cur[0] != '\0' && p->cur[0] != 'Z') {
        char prefix[64];
        size_t prefix_len;
        size_t off;
        char *param;

        if (p->cur[0] == 'X') {
            p->cur++;
            param = (char *)malloc(4u);
            if (param == NULL) {
                goto fail;
            }
            memcpy(param, "...", 4u);
        } else {
            prefix[0] = '\0';
            prefix_len = 0u;

            while (p->cur[0] == 'J' || p->cur[0] == 'K' || p->cur[0] == 'L' ||
                   p->cur[0] == 'M' || (p->cur[0] == 'N' && !dlang_is_attr_code(p->cur[1]))) {
                const char *tok = NULL;
                size_t tok_len = 0u;

                switch (p->cur[0]) {
                case 'J': tok = "out "; tok_len = 4u; break;
                case 'K': tok = "ref "; tok_len = 4u; break;
                case 'L': tok = "lazy "; tok_len = 5u; break;
                case 'M': tok = "scope "; tok_len = 6u; break;
                case 'N': tok = "return "; tok_len = 7u; break;
                default: break;
                }

                if (tok != NULL && prefix_len + tok_len < sizeof(prefix)) {
                    memcpy(prefix + prefix_len, tok, tok_len);
                    prefix_len += tok_len;
                    prefix[prefix_len] = '\0';
                }

                p->cur++;
            }

            off = p->out.len;
            if (dlang_parse_type(p) != 0) {
                goto fail;
            }

            param = dlang_take_segment(p, off);
            if (param == NULL) {
                goto fail;
            }

            if (prefix_len > 0u) {
                char *tmp = dlang_wrap_type(prefix, param, "");
                free(param);
                param = tmp;
                if (param == NULL) {
                    goto fail;
                }
            }
        }

        if (param_count == param_cap) {
            size_t next_cap = (param_cap == 0u) ? 8u : param_cap * 2u;
            char **next = (char **)realloc(params, next_cap * sizeof(*next));
            if (next == NULL) {
                free(param);
                goto fail;
            }
            params = next;
            param_cap = next_cap;
        }

        params[param_count++] = param;
    }

    if (p->cur[0] != 'Z') {
        goto fail;
    }
    p->cur++;

    ret_off = p->out.len;
    if (dlang_parse_type(p) != 0) {
        goto fail;
    }
    ret = dlang_take_segment(p, ret_off);
    if (ret == NULL) {
        goto fail;
    }

    if (dlang_buf_append(&p->out, ret, strlen(ret)) != 0 || dlang_buf_appendc(&p->out, ' ') != 0) {
        goto fail;
    }

    if (cconv != 'F') {
        const char *cc = NULL;
        switch (cconv) {
        case 'U': cc = "extern(C) "; break;
        case 'W': cc = "extern(Windows) "; break;
        case 'V': cc = "extern(Pascal) "; break;
        case 'R': cc = "extern(C++) "; break;
        default: break;
        }
        if (cc != NULL && dlang_buf_append(&p->out, cc, strlen(cc)) != 0) {
            goto fail;
        }
    }

    if (dlang_buf_append(&p->out, "function(", 9u) != 0) {
        goto fail;
    }

    for (size_t i = 0u; i < param_count; i++) {
        if (i > 0u && dlang_buf_append(&p->out, ", ", 2u) != 0) {
            goto fail;
        }
        if (dlang_buf_append(&p->out, params[i], strlen(params[i])) != 0) {
            goto fail;
        }
    }

    if (dlang_buf_appendc(&p->out, ')') != 0) {
        goto fail;
    }

    if (dlang_append_attrs(&p->out, attrs) != 0) {
        goto fail;
    }

    free(ret);
    for (size_t i = 0u; i < param_count; i++) {
        free(params[i]);
    }
    free(params);
    return 0;

fail:
    free(ret);
    for (size_t i = 0u; i < param_count; i++) {
        free(params[i]);
    }
    free(params);
    return -1;
}

static int
dlang_parse_type_core(dlang_parser_t *p)
{
    static const struct {
        char code;
        const char *name;
    } basic[] = {
        { 'v', "void" }, { 'g', "byte" }, { 'h', "ubyte" }, { 's', "short" },
        { 't', "ushort" }, { 'i', "int" }, { 'k', "uint" }, { 'l', "long" },
        { 'm', "ulong" }, { 'f', "float" }, { 'd', "double" }, { 'e', "real" },
        { 'o', "ifloat" }, { 'p', "idouble" }, { 'j', "ireal" }, { 'q', "cfloat" },
        { 'r', "cdouble" }, { 'c', "creal" }, { 'b', "bool" }, { 'a', "char" },
        { 'u', "wchar" }, { 'w', "dchar" }, { 'n', "typeof(null)" }
    };
    size_t i;

    for (i = 0u; i < sizeof(basic) / sizeof(basic[0]); i++) {
        if (p->cur[0] == basic[i].code) {
            p->cur++;
            return dlang_buf_append(&p->out, basic[i].name, strlen(basic[i].name));
        }
    }

    if (p->cur[0] == 'A') {
        size_t off;
        char *inner;

        p->cur++;
        off = p->out.len;
        if (dlang_parse_type(p) != 0) {
            return -1;
        }
        inner = dlang_take_segment(p, off);
        if (inner == NULL) {
            return -1;
        }
        if (dlang_buf_append(&p->out, inner, strlen(inner)) != 0 ||
            dlang_buf_append(&p->out, "[]", 2u) != 0) {
            free(inner);
            return -1;
        }
        free(inner);
        return 0;
    }

    if (p->cur[0] == 'G') {
        size_t n;
        size_t off;
        char *inner;

        p->cur++;
        if (dlang_parse_number(p, &n) != 0) {
            return -1;
        }

        off = p->out.len;
        if (dlang_parse_type(p) != 0) {
            return -1;
        }
        inner = dlang_take_segment(p, off);
        if (inner == NULL) {
            return -1;
        }
        if (dlang_buf_append(&p->out, inner, strlen(inner)) != 0 ||
            dlang_buf_appendc(&p->out, '[') != 0) {
            free(inner);
            return -1;
        }
        {
            char numbuf[32];
            int numlen = snprintf(numbuf, sizeof(numbuf), "%zu", n);
            if (numlen < 0 || dlang_buf_append(&p->out, numbuf, (size_t)numlen) != 0 ||
                dlang_buf_appendc(&p->out, ']') != 0) {
                free(inner);
                return -1;
            }
        }
        free(inner);
        return 0;
    }

    if (p->cur[0] == 'H') {
        size_t key_off;
        size_t val_off;
        char *key;
        char *val;

        p->cur++;

        key_off = p->out.len;
        if (dlang_parse_type(p) != 0) {
            return -1;
        }
        key = dlang_take_segment(p, key_off);
        if (key == NULL) {
            return -1;
        }

        val_off = p->out.len;
        if (dlang_parse_type(p) != 0) {
            free(key);
            return -1;
        }
        val = dlang_take_segment(p, val_off);
        if (val == NULL) {
            free(key);
            return -1;
        }

        if (dlang_buf_append(&p->out, val, strlen(val)) != 0 ||
            dlang_buf_appendc(&p->out, '[') != 0 ||
            dlang_buf_append(&p->out, key, strlen(key)) != 0 ||
            dlang_buf_appendc(&p->out, ']') != 0) {
            free(key);
            free(val);
            return -1;
        }

        free(key);
        free(val);
        return 0;
    }

    if (p->cur[0] == 'P') {
        size_t off;
        char *inner;

        p->cur++;
        off = p->out.len;
        if (dlang_parse_type(p) != 0) {
            return -1;
        }
        inner = dlang_take_segment(p, off);
        if (inner == NULL) {
            return -1;
        }
        if (dlang_buf_append(&p->out, inner, strlen(inner)) != 0 || dlang_buf_appendc(&p->out, '*') != 0) {
            free(inner);
            return -1;
        }
        free(inner);
        return 0;
    }

    if (p->cur[0] == 'E' || p->cur[0] == 'C' || p->cur[0] == 'S' || p->cur[0] == 'I') {
        p->cur++;
        return dlang_parse_qualified_name(p);
    }

    if (p->cur[0] == 'D') {
        size_t off;
        char *inner;
        char *delegate_text;

        p->cur++;
        off = p->out.len;
        if (dlang_parse_type(p) != 0) {
            return -1;
        }
        inner = dlang_take_segment(p, off);
        if (inner == NULL) {
            return -1;
        }
        delegate_text = dlang_delegate_from_function(inner);
        free(inner);
        if (delegate_text == NULL) {
            return -1;
        }

        if (dlang_buf_append(&p->out, delegate_text, strlen(delegate_text)) != 0) {
            free(delegate_text);
            return -1;
        }
        free(delegate_text);
        return 0;
    }
    if (p->cur[0] == 'B') {
        size_t n;
        size_t off;
        char *inner;

        p->cur++;
        if (dlang_parse_number(p, &n) != 0 || n == 0u) {
            return -1;
        }

        off = p->out.len;
        if (dlang_parse_type(p) != 0) {
            return -1;
        }
        inner = dlang_take_segment(p, off);
        if (inner == NULL) {
            return -1;
        }

        if (dlang_buf_append(&p->out, "Tuple!(", 7u) != 0) {
            free(inner);
            return -1;
        }
        for (size_t i = 0u; i < n; i++) {
            if (i > 0u && dlang_buf_append(&p->out, ", ", 2u) != 0) {
                free(inner);
                return -1;
            }
            if (dlang_buf_append(&p->out, inner, strlen(inner)) != 0) {
                free(inner);
                return -1;
            }
        }
        if (dlang_buf_appendc(&p->out, ')') != 0) {
            free(inner);
            return -1;
        }

        free(inner);
        return 0;
    }

    if (p->cur[0] == 'F' || p->cur[0] == 'U' || p->cur[0] == 'W' || p->cur[0] == 'V' || p->cur[0] == 'R') {
        char cconv;
        cconv = p->cur[0];
        p->cur++;
        return dlang_parse_function_type(p, cconv);
    }

    return -1;
}

static int
dlang_parse_type(dlang_parser_t *p)
{
    unsigned quals;
    unsigned attrs;
    size_t off;
    char *seg;
    char *tmp;

    quals = 0u;
    attrs = 0u;

    while (p->cur[0] == 'x' || p->cur[0] == 'y' || p->cur[0] == 'O' ||
           (p->cur[0] == 'N' && (p->cur[1] == 'g' || p->cur[1] == 'a' || p->cur[1] == 'b' ||
                                 p->cur[1] == 'c' || p->cur[1] == 'd' || p->cur[1] == 'e' ||
                                 p->cur[1] == 'f' || p->cur[1] == 'i' || p->cur[1] == 'j'))) {
        if (p->cur[0] == 'x') {
            quals |= DLANG_QUAL_CONST;
            p->cur++;
        } else if (p->cur[0] == 'y') {
            quals |= DLANG_QUAL_IMMUTABLE;
            p->cur++;
        } else if (p->cur[0] == 'O') {
            quals |= DLANG_QUAL_SHARED;
            p->cur++;
        } else if (p->cur[0] == 'N' && p->cur[1] == 'g') {
            quals |= DLANG_QUAL_INOUT;
            p->cur += 2;
        } else if (p->cur[0] == 'N') {
            switch (p->cur[1]) {
            case 'a': attrs |= DLANG_ATTR_PURE; break;
            case 'b': attrs |= DLANG_ATTR_NOTHROW; break;
            case 'c': attrs |= DLANG_ATTR_REF; break;
            case 'd': attrs |= DLANG_ATTR_PROPERTY; break;
            case 'e': attrs |= DLANG_ATTR_TRUSTED; break;
            case 'f': attrs |= DLANG_ATTR_SAFE; break;
            case 'i': attrs |= DLANG_ATTR_NOGC; break;
            case 'j': attrs |= DLANG_ATTR_RETREF; break;
            default: break;
            }
            p->cur += 2;
        }
    }

    off = p->out.len;
    if (dlang_parse_type_core(p) != 0) {
        return -1;
    }

    seg = dlang_take_segment(p, off);
    if (seg == NULL) {
        return -1;
    }

    if ((quals & DLANG_QUAL_INOUT) != 0) {
        tmp = dlang_wrap_type("inout(", seg, ")");
        free(seg);
        seg = tmp;
        if (seg == NULL) {
            return -1;
        }
    }
    if ((quals & DLANG_QUAL_CONST) != 0) {
        tmp = dlang_wrap_type("const(", seg, ")");
        free(seg);
        seg = tmp;
        if (seg == NULL) {
            return -1;
        }
    }
    if ((quals & DLANG_QUAL_IMMUTABLE) != 0) {
        tmp = dlang_wrap_type("immutable(", seg, ")");
        free(seg);
        seg = tmp;
        if (seg == NULL) {
            return -1;
        }
    }
    if ((quals & DLANG_QUAL_SHARED) != 0) {
        tmp = dlang_wrap_type("shared(", seg, ")");
        free(seg);
        seg = tmp;
        if (seg == NULL) {
            return -1;
        }
    }

    if (dlang_buf_append(&p->out, seg, strlen(seg)) != 0) {
        free(seg);
        return -1;
    }
    free(seg);

    return dlang_append_attrs(&p->out, attrs);
}

static int
dlang_parse_mangled_name(dlang_parser_t *p)
{
    size_t type_off;
    char *type_text;

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

    type_off = p->out.len;
    if (dlang_parse_type(p) != 0) {
        dlang_parser_leave(p);
        return -1;
    }

    type_text = dlang_take_segment(p, type_off);
    if (type_text == NULL) {
        dlang_parser_leave(p);
        return -1;
    }

    if (dlang_buf_append(&p->out, ": ", 2u) != 0 ||
        dlang_buf_append(&p->out, type_text, strlen(type_text)) != 0) {
        free(type_text);
        dlang_parser_leave(p);
        return -1;
    }
    free(type_text);

    dlang_parser_leave(p);
    return 0;
}

static char *
dlang_demangle_symbol(const char *mangled, int options)
{
    dlang_parser_t p;
    char *ret;

    (void)options;

    if (dlang_is_runtime_symbol(mangled)) {
        return dlang_strdup(mangled);
    }

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

    if (dlang_is_runtime_symbol(mangled)) {
        return dlang_strdup(mangled);
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
