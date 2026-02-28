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
#define DM_TEMPLATE_ARG_CAP 64u
#define DM_RECURSION_DEFAULT_LIMIT 256

enum {
    DM_MEMBER_CONST = 1u << 0,
    DM_MEMBER_VOLATILE = 1u << 1,
    DM_MEMBER_RESTRICT = 1u << 2,
    DM_MEMBER_REF_LVALUE = 1u << 3,
    DM_MEMBER_REF_RVALUE = 1u << 4
};

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
    size_t arg_off[DM_TEMPLATE_ARG_CAP];
    size_t arg_len[DM_TEMPLATE_ARG_CAP];
    size_t arg_count;
} dm_template_frame_t;

typedef struct dm_itanium_parser {
    const char *input;
    const char *cur;
    int options;

    int recursion_depth;
    int recursion_limit;

    unsigned member_quals;

    dm_buf_t out;

    dm_subst_t substitutions[DM_SUBST_CAP];
    size_t substitution_count;

    dm_template_frame_t template_frames[DM_TEMPLATE_STACK_CAP];
    size_t template_depth;

    size_t last_template_off[DM_TEMPLATE_ARG_CAP];
    size_t last_template_len[DM_TEMPLATE_ARG_CAP];
    size_t last_template_count;
} dm_itanium_parser_t;

typedef struct dm_parse_mark {
    const char *cur;
    size_t out_len;
    unsigned member_quals;
    size_t substitution_count;
    size_t template_depth;
    size_t last_template_count;
} dm_parse_mark_t;

static int buf_reserve(dm_buf_t *buf, size_t extra);
static int buf_append(dm_buf_t *buf, const char *s, size_t n);
static int buf_appendc(dm_buf_t *buf, char ch);
static int buf_insert(dm_buf_t *buf, size_t off, const char *s, size_t n);
static int buf_printf(dm_buf_t *buf, const char *fmt, ...);
static char *buf_take(dm_buf_t *buf);
static void buf_destroy(dm_buf_t *buf);

static int parser_begin(dm_itanium_parser_t *p);
static int parser_enter(dm_itanium_parser_t *p);
static void parser_leave(dm_itanium_parser_t *p);
static int parser_push_template_frame(dm_itanium_parser_t *p);
static int parser_pop_template_frame(dm_itanium_parser_t *p);
static int parser_record_template_arg(dm_itanium_parser_t *p, size_t off, size_t len);
static int parser_capture_template_frame(dm_itanium_parser_t *p);
static int parser_add_substitution(dm_itanium_parser_t *p, size_t off, size_t len);
static int parser_append_output_slice(dm_itanium_parser_t *p, size_t off, size_t len);
static int parser_lookup_substitution(dm_itanium_parser_t *p, size_t idx);

static int parse_number(dm_itanium_parser_t *p, size_t *out);
static int parse_seq_id(dm_itanium_parser_t *p, size_t *out);
static int parse_signed_number_token(dm_itanium_parser_t *p, long long *out);

static int parse_encoding(dm_itanium_parser_t *p);
static int parse_special_name(dm_itanium_parser_t *p);
static int parse_name(dm_itanium_parser_t *p);
static int parse_nested_name(dm_itanium_parser_t *p);
static int parse_unscoped_name(dm_itanium_parser_t *p);
static int parse_local_name(dm_itanium_parser_t *p);
static int parse_name_component(dm_itanium_parser_t *p, size_t prev_off, size_t prev_len,
                                size_t *comp_off, size_t *comp_len);
static int parse_unqualified_name(dm_itanium_parser_t *p, size_t prev_off, size_t prev_len,
                                  size_t *comp_off, size_t *comp_len);
static int parse_source_name(dm_itanium_parser_t *p);
static int parse_substitution(dm_itanium_parser_t *p);
static int parse_operator_name(dm_itanium_parser_t *p);
static int parse_ctor_dtor_name(dm_itanium_parser_t *p, size_t prev_off, size_t prev_len);
static int parse_unnamed_type_name(dm_itanium_parser_t *p);

static int parse_template_args(dm_itanium_parser_t *p);
static int parse_template_arg(dm_itanium_parser_t *p);

static int parse_bare_function_type(dm_itanium_parser_t *p);

static int parse_type(dm_itanium_parser_t *p);
static int parse_template_param(dm_itanium_parser_t *p);
static int parse_function_type(dm_itanium_parser_t *p);
static int parse_array_type(dm_itanium_parser_t *p);
static int parse_pointer_to_member(dm_itanium_parser_t *p);
static int parse_decltype_type(dm_itanium_parser_t *p);
static int parse_vendor_type(dm_itanium_parser_t *p);
static int parse_expression(dm_itanium_parser_t *p);
static int parse_expression_operator(dm_itanium_parser_t *p);
static int parse_cast_expression(dm_itanium_parser_t *p);
static int parse_fold_expression(dm_itanium_parser_t *p);
static int parse_pack_expansion(dm_itanium_parser_t *p);
static int parse_expr_primary(dm_itanium_parser_t *p);

static void mark_save(dm_itanium_parser_t *p, dm_parse_mark_t *m);
static void mark_restore(dm_itanium_parser_t *p, const dm_parse_mark_t *m);

static int is_void_text(const dm_buf_t *buf, size_t off, size_t len);
static int has_n_bytes(const char *s, size_t n);

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
buf_insert(dm_buf_t *buf, size_t off, const char *s, size_t n)
{
    if (buf == NULL || s == NULL || off > buf->len) {
        return -1;
    }

    if (n == 0u) {
        return 0;
    }

    if (buf_reserve(buf, n) != 0) {
        return -1;
    }

    memmove(buf->data + off + n, buf->data + off, buf->len - off + 1u);
    memcpy(buf->data + off, s, n);
    buf->len += n;
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

    memset(&p->template_frames[p->template_depth], 0, sizeof(p->template_frames[p->template_depth]));
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
parser_record_template_arg(dm_itanium_parser_t *p, size_t off, size_t len)
{
    dm_template_frame_t *frame;

    if (p->template_depth == 0u) {
        return -1;
    }

    frame = &p->template_frames[p->template_depth - 1u];
    if (frame->arg_count >= DM_TEMPLATE_ARG_CAP) {
        return -1;
    }

    frame->arg_off[frame->arg_count] = off;
    frame->arg_len[frame->arg_count] = len;
    frame->arg_count++;
    return 0;
}

static int
parser_capture_template_frame(dm_itanium_parser_t *p)
{
    dm_template_frame_t *frame;
    size_t i;

    if (p->template_depth == 0u) {
        return -1;
    }

    frame = &p->template_frames[p->template_depth - 1u];
    p->last_template_count = frame->arg_count;
    for (i = 0u; i < frame->arg_count; i++) {
        p->last_template_off[i] = frame->arg_off[i];
        p->last_template_len[i] = frame->arg_len[i];
    }

    return 0;
}

static int
parser_add_substitution(dm_itanium_parser_t *p, size_t off, size_t len)
{
    if (len == 0u) {
        return 0;
    }

    if (p->substitution_count >= DM_SUBST_CAP) {
        return -1;
    }

    if (off + len > p->out.len) {
        return -1;
    }

    p->substitutions[p->substitution_count].off = off;
    p->substitutions[p->substitution_count].len = len;
    p->substitution_count++;
    return 0;
}

static int
parser_append_output_slice(dm_itanium_parser_t *p, size_t off, size_t len)
{
    if (off + len > p->out.len) {
        return -1;
    }

    if (buf_reserve(&p->out, len) != 0) {
        return -1;
    }

    memmove(p->out.data + p->out.len, p->out.data + off, len);
    p->out.len += len;
    p->out.data[p->out.len] = '\0';
    return 0;
}

static int
parser_lookup_substitution(dm_itanium_parser_t *p, size_t idx)
{
    if (idx >= p->substitution_count) {
        return -1;
    }

    return parser_append_output_slice(p, p->substitutions[idx].off, p->substitutions[idx].len);
}

static void
mark_save(dm_itanium_parser_t *p, dm_parse_mark_t *m)
{
    m->cur = p->cur;
    m->out_len = p->out.len;
    m->member_quals = p->member_quals;
    m->substitution_count = p->substitution_count;
    m->template_depth = p->template_depth;
    m->last_template_count = p->last_template_count;
}

static void
mark_restore(dm_itanium_parser_t *p, const dm_parse_mark_t *m)
{
    p->cur = m->cur;
    p->member_quals = m->member_quals;
    p->substitution_count = m->substitution_count;
    p->template_depth = m->template_depth;
    p->last_template_count = m->last_template_count;

    if (m->out_len <= p->out.len) {
        p->out.len = m->out_len;
        p->out.data[p->out.len] = '\0';
    }
}

static int
has_n_bytes(const char *s, size_t n)
{
    size_t i;

    for (i = 0u; i < n; i++) {
        if (s[i] == '\0') {
            return 0;
        }
    }

    return 1;
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

static int
parse_seq_id(dm_itanium_parser_t *p, size_t *out)
{
    size_t v;
    int saw;

    if (p == NULL || out == NULL) {
        return -1;
    }

    v = 0u;
    saw = 0;
    while (isalnum((unsigned char)p->cur[0])) {
        unsigned d;
        size_t nv;

        saw = 1;
        if (p->cur[0] >= '0' && p->cur[0] <= '9') {
            d = (unsigned)(p->cur[0] - '0');
        } else if (p->cur[0] >= 'A' && p->cur[0] <= 'Z') {
            d = 10u + (unsigned)(p->cur[0] - 'A');
        } else if (p->cur[0] >= 'a' && p->cur[0] <= 'z') {
            d = 10u + (unsigned)(p->cur[0] - 'a');
        } else {
            return -1;
        }

        nv = v * 36u + d;
        if (nv < v) {
            return -1;
        }

        v = nv;
        p->cur++;
    }

    if (!saw) {
        return -1;
    }

    *out = v;
    return 0;
}

static int
parse_signed_number_token(dm_itanium_parser_t *p, long long *out)
{
    size_t mag;
    int neg;

    if (p == NULL || out == NULL) {
        return -1;
    }

    neg = 0;
    if (p->cur[0] == 'n') {
        neg = 1;
        p->cur++;
    }

    if (parse_number(p, &mag) != 0 || p->cur[0] != '_') {
        return -1;
    }

    p->cur++;
    if (neg) {
        *out = -(long long)mag;
    } else {
        *out = (long long)mag;
    }

    return 0;
}

static int
parse_source_name(dm_itanium_parser_t *p)
{
    const char *id;
    size_t len;
    size_t off;

    if (parse_number(p, &len) != 0) {
        return -1;
    }

    id = p->cur;
    if (len == 0u || !has_n_bytes(id, len)) {
        return -1;
    }

    off = p->out.len;
    if (buf_append(&p->out, id, len) != 0) {
        return -1;
    }

    p->cur += len;
    if (parser_add_substitution(p, off, len) != 0) {
        return -1;
    }

    return 0;
}

static int
parse_substitution(dm_itanium_parser_t *p)
{
    size_t idx;

    if (p->cur[0] != 'S') {
        return -1;
    }

    if (p->cur[1] == '_') {
        p->cur += 2;
        return parser_lookup_substitution(p, 0u);
    }

    switch (p->cur[1]) {
    case 't':
        p->cur += 2;
        return buf_append(&p->out, "std", 3u);
    case 'a':
        p->cur += 2;
        return buf_append(&p->out, "std::allocator", 14u);
    case 'b':
        p->cur += 2;
        return buf_append(&p->out, "std::basic_string", 17u);
    case 's':
        p->cur += 2;
        return buf_append(&p->out, "std::string", 11u);
    case 'i':
        p->cur += 2;
        return buf_append(&p->out, "std::istream", 12u);
    case 'o':
        p->cur += 2;
        return buf_append(&p->out, "std::ostream", 12u);
    case 'd':
        p->cur += 2;
        return buf_append(&p->out, "std::iostream", 13u);
    default:
        break;
    }

    p->cur++;
    if (parse_seq_id(p, &idx) != 0 || p->cur[0] != '_') {
        return -1;
    }

    p->cur++;
    return parser_lookup_substitution(p, idx + 1u);
}

static int
parse_operator_name(dm_itanium_parser_t *p)
{
    static const struct {
        const char code[3];
        const char *name;
    } ops[] = {
        { "nw", "operator new" },
        { "na", "operator new[]" },
        { "dl", "operator delete" },
        { "da", "operator delete[]" },
        { "ps", "operator+" },
        { "ng", "operator-" },
        { "ad", "operator&" },
        { "de", "operator*" },
        { "co", "operator~" },
        { "pl", "operator+" },
        { "mi", "operator-" },
        { "ml", "operator*" },
        { "dv", "operator/" },
        { "rm", "operator%" },
        { "an", "operator&" },
        { "or", "operator|" },
        { "eo", "operator^" },
        { "ls", "operator<<" },
        { "rs", "operator>>" },
        { "eq", "operator==" },
        { "ne", "operator!=" },
        { "lt", "operator<" },
        { "gt", "operator>" },
        { "le", "operator<=" },
        { "ge", "operator>=" },
        { "ss", "operator<=>" },
        { "nt", "operator!" },
        { "pp", "operator++" },
        { "mm", "operator--" },
        { "cm", "operator," },
        { "pm", "operator->*" },
        { "pt", "operator->" },
        { "cl", "operator()" },
        { "ix", "operator[]" },
        { "qu", "operator?" },
        { "st", "sizeof" },
        { "sz", "sizeof..." },
        { "at", "alignof" },
        { "az", "alignof..." },
        { "li", "operator\"\"" }
    };
    size_t i;

    if (p->cur[0] == 'c' && p->cur[1] == 'v') {
        p->cur += 2;
        if (buf_append(&p->out, "operator ", 9u) != 0) {
            return -1;
        }
        return parse_type(p);
    }

    if (p->cur[0] == 'v' && isdigit((unsigned char)p->cur[1])) {
        p->cur += 2;
        if (buf_append(&p->out, "operator ", 9u) != 0) {
            return -1;
        }
        return parse_source_name(p);
    }

    for (i = 0u; i < sizeof(ops) / sizeof(ops[0]); i++) {
        if (p->cur[0] == ops[i].code[0] && p->cur[1] == ops[i].code[1]) {
            p->cur += 2;
            return buf_append(&p->out, ops[i].name, strlen(ops[i].name));
        }
    }

    return -1;
}

static int
parse_ctor_dtor_name(dm_itanium_parser_t *p, size_t prev_off, size_t prev_len)
{
    int is_dtor;

    if (prev_len == 0u) {
        return -1;
    }

    is_dtor = 0;

    if (p->cur[0] == 'C' && (p->cur[1] == '1' || p->cur[1] == '2' || p->cur[1] == '3')) {
        p->cur += 2;
    } else if (p->cur[0] == 'C' && p->cur[1] == 'I' && (p->cur[2] == '1' || p->cur[2] == '2')) {
        p->cur += 3;
    } else if (p->cur[0] == 'D' && (p->cur[1] == '0' || p->cur[1] == '1' || p->cur[1] == '2')) {
        p->cur += 2;
        is_dtor = 1;
    } else {
        return -1;
    }

    if (is_dtor && buf_appendc(&p->out, '~') != 0) {
        return -1;
    }

    return parser_append_output_slice(p, prev_off, prev_len);
}

static int
parse_unnamed_type_name(dm_itanium_parser_t *p)
{
    size_t id;

    if (!(p->cur[0] == 'U' && p->cur[1] == 't')) {
        return -1;
    }

    p->cur += 2;

    if (isdigit((unsigned char)p->cur[0])) {
        if (parse_number(p, &id) != 0) {
            return -1;
        }
        if (p->cur[0] != '_') {
            return -1;
        }
        p->cur++;
        return buf_printf(&p->out, "{unnamed type#%zu}", id);
    }

    if (p->cur[0] != '_') {
        return -1;
    }

    p->cur++;
    return buf_append(&p->out, "{unnamed type}", 14u);
}

static int
parse_unqualified_name(dm_itanium_parser_t *p, size_t prev_off, size_t prev_len,
                       size_t *comp_off, size_t *comp_len)
{
    dm_parse_mark_t m;

    if (comp_off == NULL || comp_len == NULL) {
        return -1;
    }

    mark_save(p, &m);
    *comp_off = p->out.len;

    if (parse_source_name(p) == 0 ||
        parse_operator_name(p) == 0 ||
        parse_ctor_dtor_name(p, prev_off, prev_len) == 0 ||
        parse_unnamed_type_name(p) == 0) {
        *comp_len = p->out.len - *comp_off;
        return 0;
    }

    mark_restore(p, &m);
    return -1;
}

static int
parse_name_component(dm_itanium_parser_t *p, size_t prev_off, size_t prev_len,
                     size_t *comp_off, size_t *comp_len)
{
    dm_parse_mark_t m;
    size_t off;
    size_t len;

    if (parse_unqualified_name(p, prev_off, prev_len, &off, &len) != 0) {
        mark_save(p, &m);
        off = p->out.len;
        if (parse_substitution(p) != 0) {
            mark_restore(p, &m);
            return -1;
        }
        len = p->out.len - off;
    }

    if (p->cur[0] == 'I' && parse_template_args(p) != 0) {
        return -1;
    }

    *comp_off = off;
    *comp_len = p->out.len - off;
    return 0;
}

static int
parse_nested_name(dm_itanium_parser_t *p)
{
    size_t name_off;
    size_t last_off;
    size_t last_len;
    int first;

    if (p->cur[0] != 'N') {
        return -1;
    }

    p->cur++;
    name_off = p->out.len;
    first = 1;
    last_off = 0u;
    last_len = 0u;

    while (p->cur[0] == 'K' || p->cur[0] == 'V' || p->cur[0] == 'r') {
        if (p->cur[0] == 'K') {
            p->member_quals |= DM_MEMBER_CONST;
        } else if (p->cur[0] == 'V') {
            p->member_quals |= DM_MEMBER_VOLATILE;
        } else {
            p->member_quals |= DM_MEMBER_RESTRICT;
        }
        p->cur++;
    }

    if (p->cur[0] == 'R') {
        p->member_quals |= DM_MEMBER_REF_LVALUE;
        p->cur++;
    } else if (p->cur[0] == 'O') {
        p->member_quals |= DM_MEMBER_REF_RVALUE;
        p->cur++;
    }

    while (p->cur[0] != '\0' && p->cur[0] != 'E') {
        if (!first && buf_append(&p->out, "::", 2u) != 0) {
            return -1;
        }

        if (parse_name_component(p, last_off, last_len, &last_off, &last_len) != 0) {
            return -1;
        }

        first = 0;
    }

    if (first || p->cur[0] != 'E') {
        return -1;
    }

    p->cur++;

    if (parser_add_substitution(p, name_off, p->out.len - name_off) != 0) {
        return -1;
    }

    return 0;
}

static int
parse_unscoped_name(dm_itanium_parser_t *p)
{
    size_t name_off;
    size_t dummy_off;
    size_t dummy_len;

    name_off = p->out.len;

    if (parse_name_component(p, 0u, 0u, &dummy_off, &dummy_len) != 0) {
        return -1;
    }

    if (parser_add_substitution(p, name_off, p->out.len - name_off) != 0) {
        return -1;
    }

    return 0;
}

static int
parse_local_name(dm_itanium_parser_t *p)
{
    if (p->cur[0] != 'Z') {
        return -1;
    }

    p->cur++;

    if (parser_enter(p) != 0) {
        return -1;
    }

    if (parse_encoding(p) != 0) {
        parser_leave(p);
        return -1;
    }

    parser_leave(p);

    if (p->cur[0] != 'E') {
        return -1;
    }

    p->cur++;

    if (buf_append(&p->out, "::", 2u) != 0) {
        return -1;
    }

    if (parse_name(p) != 0) {
        return -1;
    }

    if (p->cur[0] == '_') {
        p->cur++;
        while (isdigit((unsigned char)p->cur[0])) {
            p->cur++;
        }
    }

    return 0;
}

static int
parse_name(dm_itanium_parser_t *p)
{
    if (p->cur[0] == 'N') {
        return parse_nested_name(p);
    }

    if (p->cur[0] == 'Z') {
        return parse_local_name(p);
    }

    return parse_unscoped_name(p);
}

static int
append_member_qualifiers(dm_itanium_parser_t *p)
{
    if ((p->options & DEMANGLE_NO_VERBOSE) != 0) {
        p->member_quals = 0u;
        return 0;
    }

    if ((p->member_quals & DM_MEMBER_CONST) != 0 && buf_append(&p->out, " const", 6u) != 0) {
        return -1;
    }
    if ((p->member_quals & DM_MEMBER_VOLATILE) != 0 && buf_append(&p->out, " volatile", 9u) != 0) {
        return -1;
    }
    if ((p->member_quals & DM_MEMBER_RESTRICT) != 0 && buf_append(&p->out, " restrict", 9u) != 0) {
        return -1;
    }
    if ((p->member_quals & DM_MEMBER_REF_LVALUE) != 0 && buf_append(&p->out, " &", 2u) != 0) {
        return -1;
    }
    if ((p->member_quals & DM_MEMBER_REF_RVALUE) != 0 && buf_append(&p->out, " &&", 3u) != 0) {
        return -1;
    }

    p->member_quals = 0u;
    return 0;
}

static int
is_void_text(const dm_buf_t *buf, size_t off, size_t len)
{
    return len == 4u && off + len <= buf->len && memcmp(buf->data + off, "void", 4u) == 0;
}

static int
parse_bare_function_type(dm_itanium_parser_t *p)
{
    int suppress_params;
    int first;

    suppress_params = ((p->options & DEMANGLE_NO_PARAMS) != 0);

    if (!suppress_params && buf_appendc(&p->out, '(') != 0) {
        return -1;
    }

    first = 1;
    while (p->cur[0] != '\0') {
        size_t before;
        size_t parsed_len;

        before = p->out.len;
        if (parse_type(p) != 0) {
            return -1;
        }

        parsed_len = p->out.len - before;
        if (first && p->cur[0] == '\0' && is_void_text(&p->out, before, parsed_len)) {
            p->out.len = before;
            p->out.data[p->out.len] = '\0';
            break;
        }

        if (suppress_params) {
            p->out.len = before;
            p->out.data[p->out.len] = '\0';
        } else if (!first) {
            if (buf_insert(&p->out, before, ", ", 2u) != 0) {
                return -1;
            }
        }

        first = 0;
    }

    if (!suppress_params && buf_appendc(&p->out, ')') != 0) {
        return -1;
    }

    return append_member_qualifiers(p);
}

static int
parse_template_args(dm_itanium_parser_t *p)
{
    int first;
    int pushed;

    if (p->cur[0] != 'I') {
        return -1;
    }

    p->cur++;
    pushed = 0;

    if (parser_push_template_frame(p) != 0) {
        return -1;
    }
    pushed = 1;

    if (buf_appendc(&p->out, '<') != 0) {
        goto fail;
    }

    first = 1;
    while (p->cur[0] != '\0' && p->cur[0] != 'E') {
        size_t arg_off;

        if (!first && buf_append(&p->out, ", ", 2u) != 0) {
            goto fail;
        }

        arg_off = p->out.len;
        if (parse_template_arg(p) != 0) {
            goto fail;
        }

        if (parser_record_template_arg(p, arg_off, p->out.len - arg_off) != 0) {
            goto fail;
        }

        first = 0;
    }

    if (p->cur[0] != 'E') {
        goto fail;
    }

    p->cur++;

    if (buf_appendc(&p->out, '>') != 0) {
        goto fail;
    }

    if (parser_capture_template_frame(p) != 0) {
        goto fail;
    }

    if (parser_pop_template_frame(p) != 0) {
        goto fail;
    }

    return 0;

fail:
    if (pushed) {
        (void)parser_pop_template_frame(p);
    }
    return -1;
}

static int
parse_template_arg(dm_itanium_parser_t *p)
{
    dm_parse_mark_t m;

    if (p->cur[0] == 'X') {
        p->cur++;
        if (parse_expression(p) != 0 || p->cur[0] != 'E') {
            return -1;
        }
        p->cur++;
        return 0;
    }

    if (p->cur[0] == 'J') {
        int first;

        p->cur++;
        if (buf_append(&p->out, "pack<", 5u) != 0) {
            return -1;
        }

        first = 1;
        while (p->cur[0] != '\0' && p->cur[0] != 'E') {
            if (!first && buf_append(&p->out, ", ", 2u) != 0) {
                return -1;
            }
            if (parse_template_arg(p) != 0) {
                return -1;
            }
            first = 0;
        }

        if (p->cur[0] != 'E' || buf_appendc(&p->out, '>') != 0) {
            return -1;
        }

        p->cur++;
        return 0;
    }

    mark_save(p, &m);
    if (parse_type(p) == 0) {
        return 0;
    }

    mark_restore(p, &m);
    return parse_expr_primary(p);
}

static int
parse_expr_primary(dm_itanium_parser_t *p)
{
    dm_parse_mark_t m;

    if (p->cur[0] == 'L') {
        size_t num_len;

        p->cur++;

        if (p->cur[0] == 'D' && p->cur[1] == 'n' && p->cur[2] == 'E') {
            p->cur += 3;
            return buf_append(&p->out, "nullptr", 7u);
        }

        mark_save(p, &m);
        if (parse_type(p) != 0) {
            return -1;
        }
        p->out.len = m.out_len;
        p->out.data[p->out.len] = '\0';

        if (p->cur[0] == 'n') {
            if (buf_appendc(&p->out, '-') != 0) {
                return -1;
            }
            p->cur++;
        }

        num_len = 0u;
        while (p->cur[num_len] != '\0' && isdigit((unsigned char)p->cur[num_len])) {
            num_len++;
        }

        if (num_len == 0u || p->cur[num_len] != 'E') {
            return -1;
        }

        if (buf_append(&p->out, p->cur, num_len) != 0) {
            return -1;
        }

        p->cur += num_len + 1u;
        return 0;
    }

    return -1;
}

static int
parse_cast_expression(dm_itanium_parser_t *p)
{
    if (!(p->cur[0] == 'c' && p->cur[1] == 'v')) {
        return -1;
    }

    p->cur += 2;
    if (buf_appendc(&p->out, '(') != 0) {
        return -1;
    }
    if (parse_type(p) != 0) {
        return -1;
    }
    if (buf_appendc(&p->out, ')') != 0) {
        return -1;
    }
    return parse_expression(p);
}

static int
parse_fold_expression(dm_itanium_parser_t *p)
{
    if (p->cur[0] != 'f' ||
        (p->cur[1] != 'L' && p->cur[1] != 'R' && p->cur[1] != 'l' && p->cur[1] != 'r')) {
        return -1;
    }

    if (buf_printf(&p->out, "fold[%c%c](", p->cur[0], p->cur[1]) != 0) {
        return -1;
    }
    p->cur += 2;

    if (parse_expression(p) != 0) {
        return -1;
    }

    if (p->cur[0] != 'E') {
        if (buf_append(&p->out, ", ", 2u) != 0) {
            return -1;
        }
        if (parse_expression(p) != 0) {
            return -1;
        }
    }

    return buf_appendc(&p->out, ')');
}

static int
parse_pack_expansion(dm_itanium_parser_t *p)
{
    if (!(p->cur[0] == 's' && p->cur[1] == 'p')) {
        return -1;
    }

    p->cur += 2;
    if (buf_append(&p->out, "pack_expand(", 12u) != 0) {
        return -1;
    }
    if (parse_expression(p) != 0) {
        return -1;
    }
    return buf_appendc(&p->out, ')');
}

static int
parse_expression_operator(dm_itanium_parser_t *p)
{
    static const struct {
        const char code[3];
        const char *name;
        int arity;
    } ops[] = {
        { "cl", "call", 2 },
        { "ix", "index", 2 },
        { "qu", "cond", 3 },
        { "st", "sizeof", 1 },
        { "sz", "sizeof...", 1 },
        { "at", "alignof", 1 },
        { "az", "alignof...", 1 }
    };
    size_t i;
    int argi;

    for (i = 0u; i < sizeof(ops) / sizeof(ops[0]); i++) {
        if (p->cur[0] == ops[i].code[0] && p->cur[1] == ops[i].code[1]) {
            p->cur += 2;
            if (buf_append(&p->out, ops[i].name, strlen(ops[i].name)) != 0 ||
                buf_appendc(&p->out, '(') != 0) {
                return -1;
            }

            for (argi = 0; argi < ops[i].arity; argi++) {
                if (argi > 0 && buf_append(&p->out, ", ", 2u) != 0) {
                    return -1;
                }
                if (parse_expression(p) != 0) {
                    return -1;
                }
            }

            return buf_appendc(&p->out, ')');
        }
    }

    return -1;
}

static int
parse_expression(dm_itanium_parser_t *p)
{
    dm_parse_mark_t m;
    int ok;

    if (parser_enter(p) != 0) {
        return -1;
    }

    mark_save(p, &m);
    ok = parse_expr_primary(p);
    if (ok == 0) {
        parser_leave(p);
        return 0;
    }

    mark_restore(p, &m);
    ok = parse_cast_expression(p);
    if (ok == 0) {
        parser_leave(p);
        return 0;
    }

    mark_restore(p, &m);
    ok = parse_fold_expression(p);
    if (ok == 0) {
        parser_leave(p);
        return 0;
    }

    mark_restore(p, &m);
    ok = parse_pack_expansion(p);
    if (ok == 0) {
        parser_leave(p);
        return 0;
    }

    mark_restore(p, &m);
    ok = parse_expression_operator(p);
    if (ok == 0) {
        parser_leave(p);
        return 0;
    }

    mark_restore(p, &m);
    ok = parse_name(p);
    parser_leave(p);
    return ok;
}

static int
parse_template_param(dm_itanium_parser_t *p)
{
    size_t idx;

    if (p->cur[0] != 'T') {
        return -1;
    }

    p->cur++;
    if (p->cur[0] == '_') {
        idx = 0u;
        p->cur++;
    } else {
        size_t seq;

        if (parse_seq_id(p, &seq) != 0 || p->cur[0] != '_') {
            return -1;
        }

        p->cur++;
        idx = seq + 1u;
    }

    if (idx >= p->last_template_count) {
        return -1;
    }

    return parser_append_output_slice(p, p->last_template_off[idx], p->last_template_len[idx]);
}

static int
parse_function_type(dm_itanium_parser_t *p)
{
    int first;
    int extern_c;

    if (p->cur[0] != 'F') {
        return -1;
    }

    p->cur++;
    extern_c = 0;

    if (p->cur[0] == 'Y') {
        extern_c = 1;
        p->cur++;
    }

    if (extern_c && buf_append(&p->out, "extern \"C\" ", 11u) != 0) {
        return -1;
    }

    if (parse_type(p) != 0) {
        return -1;
    }

    if (buf_append(&p->out, " (", 2u) != 0) {
        return -1;
    }

    first = 1;
    while (p->cur[0] != '\0' && p->cur[0] != 'E') {
        size_t before;
        size_t parsed_len;

        before = p->out.len;
        if (parse_type(p) != 0) {
            return -1;
        }

        parsed_len = p->out.len - before;
        if (first && p->cur[0] == 'E' && is_void_text(&p->out, before, parsed_len)) {
            p->out.len = before;
            p->out.data[p->out.len] = '\0';
            break;
        }

        if (!first && buf_insert(&p->out, before, ", ", 2u) != 0) {
            return -1;
        }

        first = 0;
    }

    if (p->cur[0] != 'E') {
        return -1;
    }

    p->cur++;

    return buf_appendc(&p->out, ')');
}

static int
parse_array_type(dm_itanium_parser_t *p)
{
    size_t dim;
    int has_dim;

    if (p->cur[0] != 'A') {
        return -1;
    }

    p->cur++;

    has_dim = 0;
    dim = 0u;
    if (isdigit((unsigned char)p->cur[0])) {
        has_dim = 1;
        if (parse_number(p, &dim) != 0) {
            return -1;
        }
    }

    if (p->cur[0] != '_') {
        return -1;
    }

    p->cur++;

    if (parse_type(p) != 0) {
        return -1;
    }

    if (has_dim) {
        return buf_printf(&p->out, "[%zu]", dim);
    }

    return buf_append(&p->out, "[]", 2u);
}

static int
parse_pointer_to_member(dm_itanium_parser_t *p)
{
    size_t class_off;
    size_t class_len;

    if (p->cur[0] != 'M') {
        return -1;
    }

    p->cur++;

    class_off = p->out.len;
    if (parse_type(p) != 0) {
        return -1;
    }
    class_len = p->out.len - class_off;

    if (buf_append(&p->out, "::*", 3u) != 0) {
        return -1;
    }

    if (parse_type(p) != 0) {
        return -1;
    }

    (void)class_len;
    return 0;
}

static int
parse_decltype_type(dm_itanium_parser_t *p)
{
    if (p->cur[0] != 'D' || (p->cur[1] != 't' && p->cur[1] != 'T')) {
        return -1;
    }

    p->cur += 2;

    if (buf_append(&p->out, "decltype(", 9u) != 0) {
        return -1;
    }

    if (parse_expression(p) != 0) {
        return -1;
    }

    if (p->cur[0] != 'E') {
        return -1;
    }

    p->cur++;
    return buf_appendc(&p->out, ')');
}

static int
parse_vendor_type(dm_itanium_parser_t *p)
{
    if (p->cur[0] != 'u') {
        return -1;
    }

    p->cur++;
    if (buf_append(&p->out, "vendor(", 7u) != 0) {
        return -1;
    }

    if (parse_source_name(p) != 0) {
        return -1;
    }

    return buf_appendc(&p->out, ')');
}

static int
parse_type(dm_itanium_parser_t *p)
{
    static const struct {
        char code;
        const char *name;
    } builtin[] = {
        { 'v', "void" },
        { 'w', "wchar_t" },
        { 'b', "bool" },
        { 'c', "char" },
        { 'a', "signed char" },
        { 'h', "unsigned char" },
        { 's', "short" },
        { 't', "unsigned short" },
        { 'i', "int" },
        { 'j', "unsigned int" },
        { 'l', "long" },
        { 'm', "unsigned long" },
        { 'x', "long long" },
        { 'y', "unsigned long long" },
        { 'n', "__int128" },
        { 'o', "unsigned __int128" },
        { 'f', "float" },
        { 'd', "double" },
        { 'e', "long double" },
        { 'g', "__float128" },
        { 'z', "..." }
    };
    size_t i;

    if (p->cur[0] == 'P' || p->cur[0] == 'R' || p->cur[0] == 'O') {
        char kind = p->cur[0];
        p->cur++;
        if (parse_type(p) != 0) {
            return -1;
        }
        if (kind == 'P') {
            return buf_appendc(&p->out, '*');
        }
        if (kind == 'R') {
            return buf_appendc(&p->out, '&');
        }
        return buf_append(&p->out, "&&", 2u);
    }

    if (p->cur[0] == 'K' || p->cur[0] == 'V' || p->cur[0] == 'r') {
        char qual = p->cur[0];
        p->cur++;
        if (parse_type(p) != 0) {
            return -1;
        }
        if (qual == 'K') {
            return buf_append(&p->out, " const", 6u);
        }
        if (qual == 'V') {
            return buf_append(&p->out, " volatile", 9u);
        }
        return buf_append(&p->out, " restrict", 9u);
    }

    if (p->cur[0] == 'C') {
        p->cur++;
        if (parse_type(p) != 0) {
            return -1;
        }
        return buf_append(&p->out, " _Complex", 9u);
    }

    if (p->cur[0] == 'G') {
        p->cur++;
        if (parse_type(p) != 0) {
            return -1;
        }
        return buf_append(&p->out, " _Imaginary", 11u);
    }

    if (parse_function_type(p) == 0 ||
        parse_array_type(p) == 0 ||
        parse_pointer_to_member(p) == 0 ||
        parse_template_param(p) == 0 ||
        parse_decltype_type(p) == 0 ||
        parse_vendor_type(p) == 0) {
        return 0;
    }

    if (p->cur[0] == 'D') {
        const char *name = NULL;

        switch (p->cur[1]) {
        case 'd': name = "decimal64"; break;
        case 'e': name = "decimal128"; break;
        case 'f': name = "decimal32"; break;
        case 'h': name = "half"; break;
        case 'i': name = "char32_t"; break;
        case 's': name = "char16_t"; break;
        case 'a': name = "auto"; break;
        case 'c': name = "decltype(auto)"; break;
        case 'n': name = "std::nullptr_t"; break;
        default: break;
        }

        if (name != NULL) {
            p->cur += 2;
            return buf_append(&p->out, name, strlen(name));
        }
    }

    for (i = 0u; i < sizeof(builtin) / sizeof(builtin[0]); i++) {
        if (p->cur[0] == builtin[i].code) {
            p->cur++;
            return buf_append(&p->out, builtin[i].name, strlen(builtin[i].name));
        }
    }

    return parse_name(p);
}

static int
parse_special_name(dm_itanium_parser_t *p)
{
    if (p->cur[0] == 'T' && p->cur[1] == 'V') {
        p->cur += 2;
        if (buf_append(&p->out, "vtable for ", 11u) != 0) {
            return -1;
        }
        return parse_type(p);
    }

    if (p->cur[0] == 'T' && p->cur[1] == 'T') {
        p->cur += 2;
        if (buf_append(&p->out, "VTT for ", 8u) != 0) {
            return -1;
        }
        return parse_type(p);
    }

    if (p->cur[0] == 'T' && p->cur[1] == 'I') {
        p->cur += 2;
        if (buf_append(&p->out, "typeinfo for ", 13u) != 0) {
            return -1;
        }
        return parse_type(p);
    }

    if (p->cur[0] == 'T' && p->cur[1] == 'S') {
        p->cur += 2;
        if (buf_append(&p->out, "typeinfo name for ", 18u) != 0) {
            return -1;
        }
        return parse_type(p);
    }

    if (p->cur[0] == 'G' && p->cur[1] == 'V') {
        p->cur += 2;
        if (buf_append(&p->out, "guard variable for ", 19u) != 0) {
            return -1;
        }
        return parse_name(p);
    }

    if (p->cur[0] == 'T' && (p->cur[1] == 'c' || p->cur[1] == 'h' || p->cur[1] == 'v')) {
        char kind = p->cur[1];
        long long off1 = 0;
        long long off2 = 0;
        int have1 = 0;
        int have2 = 0;

        p->cur += 2;

        if (parse_signed_number_token(p, &off1) == 0) {
            have1 = 1;
            if (kind != 'h' && parse_signed_number_token(p, &off2) == 0) {
                have2 = 1;
            }
        }

        if (kind == 'c') {
            if (buf_append(&p->out, "covariant return thunk to ", 26u) != 0) {
                return -1;
            }
        } else if (kind == 'h') {
            if (buf_append(&p->out, "non-virtual thunk to ", 21u) != 0) {
                return -1;
            }
        } else {
            if (buf_append(&p->out, "virtual thunk to ", 17u) != 0) {
                return -1;
            }
        }

        if (parser_enter(p) != 0) {
            return -1;
        }
        if (parse_encoding(p) != 0) {
            parser_leave(p);
            return -1;
        }
        parser_leave(p);

        if (have1) {
            if (have2) {
                return buf_printf(&p->out, " [offsets %lld, %lld]", off1, off2);
            }
            return buf_printf(&p->out, " [offset %lld]", off1);
        }

        return 0;
    }

    if (p->cur[0] == 'T') {
        p->cur++;
        if (buf_append(&p->out, "transaction clone for ", 22u) != 0) {
            return -1;
        }

        if (parser_enter(p) != 0) {
            return -1;
        }
        if (parse_encoding(p) != 0) {
            parser_leave(p);
            return -1;
        }
        parser_leave(p);
        return 0;
    }

    return -1;
}

static int
parse_encoding(dm_itanium_parser_t *p)
{
    dm_parse_mark_t m;

    mark_save(p, &m);
    if (parse_special_name(p) == 0) {
        return 0;
    }
    mark_restore(p, &m);

    if (parse_name(p) != 0) {
        return -1;
    }

    if (p->cur[0] != '\0') {
        if (parse_bare_function_type(p) != 0) {
            return -1;
        }
    }

    return 0;
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

    if (mangled[0] == '_' && mangled[1] == 'Z') {
        if (parse_encoding(&parser) != 0 || parser.cur[0] != '\0') {
            parser_leave(&parser);
            buf_destroy(&parser.out);
            return NULL;
        }
    } else {
        if ((options & DEMANGLE_TYPES) == 0 || parse_type(&parser) != 0 || parser.cur[0] != '\0') {
            parser_leave(&parser);
            buf_destroy(&parser.out);
            return NULL;
        }
    }

    parser_leave(&parser);

    ret = buf_take(&parser.out);
    buf_destroy(&parser.out);
    return ret;
}
