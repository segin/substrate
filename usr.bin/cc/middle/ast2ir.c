#include "cc_frontend.h"
#include "cc_ssa.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <math.h>

static int g_pointer_size_bytes = 8;

typedef struct {
    char *name;
    cc_type_t type;
    int struct_id;
    long array_len;
    int array_ndim;
    long array_dims[CC_MAX_ARRAY_DIMS];
    int value;
    int is_static_storage;
    char *static_sym;
    int depth;
} var_entry_t;

typedef struct {
    char *name;
    int label;
} label_entry_t;

typedef struct {
    const cc_stmt_t *decl;
    int value;
} hoisted_alloc_entry_t;

typedef struct {
    label_entry_t *labels;
    size_t label_count;
    const cc_function_t *fn;
    cc_ssa_module_t *mod;
    hoisted_alloc_entry_t *hoisted_allocs;
    size_t hoisted_alloc_count;
} lower_ctx_t;

static int emit_trap_instr(cc_ssa_function_t *sf);
static int emit_global_addr(cc_ssa_function_t *sf, const char *name, cc_diag_t *diag);
static int lower_stmt(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, var_entry_t **vars, size_t *var_count,
                      const lower_ctx_t *ctx, int depth, int break_label, int continue_label, const cc_stmt_t *s,
                      int *saw_ret, cc_diag_t *diag);
static int eval_global_init_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long *out_i, double *out_f,
                                 int *out_is_float, char **out_sym);
static int eval_global_addr_symbol_addend(const cc_translation_unit_t *tu, const cc_expr_t *e, char **out_sym,
                                          long *out_addend);
static const cc_expr_t *selected_generic_expr(const cc_expr_t *e);
static int var_find_visible(var_entry_t *vars, size_t var_count, const char *name, int depth);
static const cc_global_t *find_global(const cc_translation_unit_t *tu, const char *name);
static int member_base_struct_id(const cc_expr_t *e);
static const cc_struct_member_t *find_struct_member(const cc_translation_unit_t *tu, int sid, const char *name);
static const cc_expr_t *unwrap_self_designated_init_list(const cc_expr_t *init_list, const char *member_name);

static int asm_constraint_has(const char *c, char ch) {
    return c != NULL && strchr(c, ch) != NULL;
}

static int asm_constraint_is_memory_only(const char *c) {
    if (c == NULL) {
        return 0;
    }
    if (asm_constraint_has(c, 'm') && !asm_constraint_has(c, 'r') && !asm_constraint_has(c, 'q') &&
        !asm_constraint_has(c, 'a') && !asm_constraint_has(c, 'b') && !asm_constraint_has(c, 'c') &&
        !asm_constraint_has(c, 'd') && !asm_constraint_has(c, 'S') && !asm_constraint_has(c, 'D')) {
        return 1;
    }
    return 0;
}

static void free_asm_instr_fields(cc_ssa_instr_t *in) {
    size_t i;
    if (in == NULL) {
        return;
    }
    free(in->sym);
    free(in->asm_out_values);
    free(in->asm_in_values);
    if (in->asm_out_constraints != NULL) {
        for (i = 0; i < in->asm_out_count; ++i) {
            free(in->asm_out_constraints[i]);
        }
    }
    free(in->asm_out_constraints);
    if (in->asm_out_names != NULL) {
        for (i = 0; i < in->asm_out_count; ++i) {
            free(in->asm_out_names[i]);
        }
    }
    free(in->asm_out_names);
    if (in->asm_in_constraints != NULL) {
        for (i = 0; i < in->asm_in_count; ++i) {
            free(in->asm_in_constraints[i]);
        }
    }
    free(in->asm_in_constraints);
    if (in->asm_in_names != NULL) {
        for (i = 0; i < in->asm_in_count; ++i) {
            free(in->asm_in_names[i]);
        }
    }
    free(in->asm_in_names);
    if (in->asm_clobbers != NULL) {
        for (i = 0; i < in->asm_clobber_count; ++i) {
            free(in->asm_clobbers[i]);
        }
    }
    free(in->asm_clobbers);
    free(in->asm_goto_labels);
    if (in->asm_goto_names != NULL) {
        for (i = 0; i < in->asm_goto_count; ++i) {
            free(in->asm_goto_names[i]);
        }
    }
    free(in->asm_goto_names);
    memset(in, 0, sizeof(*in));
}

static char *xstrdup(const char *s) {
    size_t n;
    char *p;
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static size_t utf8_seq_len(unsigned char b0) {
    if ((b0 & 0x80u) == 0) return 1;
    if ((b0 & 0xE0u) == 0xC0u) return 2;
    if ((b0 & 0xF0u) == 0xE0u) return 3;
    if ((b0 & 0xF8u) == 0xF0u) return 4;
    return 1;
}

static unsigned long decode_escape_unit(const unsigned char *s, size_t n, size_t *adv_out) {
    unsigned long v = 0;
    size_t i = 0;
    if (adv_out != NULL) {
        *adv_out = 0;
    }
    if (n == 0) {
        return 0;
    }
    if (s[0] == 'x') {
        i = 1;
        while (i < n) {
            unsigned char c = s[i];
            unsigned d;
            if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') d = 10u + (unsigned)(c - 'a');
            else if (c >= 'A' && c <= 'F') d = 10u + (unsigned)(c - 'A');
            else break;
            v = (v << 4) | d;
            i++;
        }
        if (i == 1) {
            i = 1;
            v = 0;
        }
        if (adv_out != NULL) {
            *adv_out = i;
        }
        return v;
    }
    if (s[0] >= '0' && s[0] <= '7') {
        int k = 0;
        while (i < n && k < 3 && s[i] >= '0' && s[i] <= '7') {
            v = (v << 3) | (unsigned long)(s[i] - '0');
            i++;
            k++;
        }
        if (adv_out != NULL) {
            *adv_out = i;
        }
        return v;
    }
    switch (s[0]) {
    case 'a': v = '\a'; break;
    case 'b': v = '\b'; break;
    case 'f': v = '\f'; break;
    case 'n': v = '\n'; break;
    case 'r': v = '\r'; break;
    case 't': v = '\t'; break;
    case 'v': v = '\v'; break;
    case '\\': v = '\\'; break;
    case '\'': v = '\''; break;
    case '\"': v = '\"'; break;
    case '?': v = '\?'; break;
    default: v = s[0]; break;
    }
    if (adv_out != NULL) {
        *adv_out = 1;
    }
    return v;
}

static int decode_string_units(const cc_expr_t *e, int wide, unsigned long **out_units, size_t *out_count) {
    const unsigned char *s;
    size_t n;
    size_t i;
    unsigned long *vals = NULL;
    size_t count = 0;

    if (out_units == NULL || out_count == NULL || e == NULL || e->kind != CC_EXPR_STR || e->ident == NULL) {
        return -1;
    }
    *out_units = NULL;
    *out_count = 0;
    s = (const unsigned char *)e->ident;
    n = strlen(e->ident);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        i = 1;
        n -= 1;
    } else {
        i = 0;
    }
    while (i < n) {
        unsigned long v;
        size_t adv = 1;
        unsigned long *next;
        if (s[i] == '\\') {
            i++;
            if (i >= n) {
                break;
            }
            v = decode_escape_unit(s + i, n - i, &adv);
            if (adv == 0) adv = 1;
            i += adv;
        } else if (!wide) {
            v = s[i];
            i++;
        } else {
            size_t seq = utf8_seq_len(s[i]);
            size_t j;
            if (seq == 0) seq = 1;
            if (i + seq > n) seq = 1;
            v = 0;
            if (seq == 1) {
                v = s[i];
            } else if (seq == 2) {
                v = (unsigned long)(s[i] & 0x1Fu);
            } else if (seq == 3) {
                v = (unsigned long)(s[i] & 0x0Fu);
            } else {
                v = (unsigned long)(s[i] & 0x07u);
            }
            for (j = 1; j < seq; ++j) {
                v = (v << 6) | (unsigned long)(s[i + j] & 0x3Fu);
            }
            i += seq;
        }
        next = (unsigned long *)realloc(vals, (count + 1) * sizeof(*next));
        if (next == NULL) {
            free(vals);
            return -1;
        }
        vals = next;
        vals[count++] = v;
    }
    *out_units = vals;
    *out_count = count;
    return 0;
}

static char *quote_c_string(const char *raw) {
    const unsigned char *p;
    size_t n = 2;
    size_t i = 0;
    char *out;
    if (raw == NULL) {
        raw = "";
    }
    p = (const unsigned char *)raw;
    while (*p != '\0') {
        unsigned char c = *p++;
        switch (c) {
        case '\\':
        case '"':
        case '\n':
        case '\r':
        case '\t':
        case '\v':
        case '\f':
        case '\a':
        case '\b':
            n += 2;
            break;
        default:
            n += (c >= 32 && c <= 126) ? 1 : 4;
            break;
        }
    }
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    out[i++] = '"';
    p = (const unsigned char *)raw;
    while (*p != '\0') {
        unsigned char c = *p++;
        switch (c) {
        case '\\':
            out[i++] = '\\';
            out[i++] = '\\';
            break;
        case '"':
            out[i++] = '\\';
            out[i++] = '"';
            break;
        case '\n':
            out[i++] = '\\';
            out[i++] = 'n';
            break;
        case '\r':
            out[i++] = '\\';
            out[i++] = 'r';
            break;
        case '\t':
            out[i++] = '\\';
            out[i++] = 't';
            break;
        case '\v':
            out[i++] = '\\';
            out[i++] = 'v';
            break;
        case '\f':
            out[i++] = '\\';
            out[i++] = 'f';
            break;
        case '\a':
            out[i++] = '\\';
            out[i++] = 'a';
            break;
        case '\b':
            out[i++] = '\\';
            out[i++] = 'b';
            break;
        default:
            if (c >= 32 && c <= 126) {
                out[i++] = (char)c;
            } else {
                out[i++] = '\\';
                out[i++] = (char)('0' + ((c >> 6) & 7));
                out[i++] = (char)('0' + ((c >> 3) & 7));
                out[i++] = (char)('0' + (c & 7));
            }
            break;
        }
    }
    out[i++] = '"';
    out[i] = '\0';
    return out;
}

static void set_diag(cc_diag_t *d, const char *msg) {
    if (d == NULL || d->message[0] != '\0') {
        return;
    }
    d->path[0] = '\0';
    d->line = 0;
    d->col = 0;
    snprintf(d->message, sizeof(d->message), "%s", msg);
}

static cc_value_type_t type_to_val(cc_type_t t) {
    return (t == CC_TYPE_FLOAT || t == CC_TYPE_DOUBLE || t == CC_TYPE_LDOUBLE || t == CC_TYPE_COMPLEX ||
            t == CC_TYPE_IMAGINARY || t == CC_TYPE_DECIMAL32 || t == CC_TYPE_DECIMAL64 || t == CC_TYPE_DECIMAL128)
               ? CC_VAL_F64
               : CC_VAL_I64;
}

static int is_pointer_type(cc_type_t t) {
    return cc_type_is_pointer(t);
}

static cc_type_t ptr_base_type(cc_type_t t);

static int pointer_depth(cc_type_t t) {
    return (int)cc_type_pointer_depth(t);
}

static int is_array_object_decl(cc_type_t t, long array_len, int array_ndim) {
    if (!is_pointer_type(t) || array_len < 0 || array_ndim <= 0) {
        return 0;
    }
    return pointer_depth(t) >= array_ndim;
}

static int is_unsigned_integral_type(cc_type_t t) {
    return t == CC_TYPE_UCHAR || t == CC_TYPE_USHORT || t == CC_TYPE_UINT || t == CC_TYPE_ULONG ||
           t == CC_TYPE_ULONG_LONG;
}

static int is_integral_type(cc_type_t t) {
    return t == CC_TYPE_BOOL || t == CC_TYPE_CHAR || t == CC_TYPE_SCHAR || t == CC_TYPE_UCHAR || t == CC_TYPE_SHORT ||
           t == CC_TYPE_USHORT || t == CC_TYPE_INT || t == CC_TYPE_UINT || t == CC_TYPE_LONG || t == CC_TYPE_ULONG ||
           t == CC_TYPE_LONG_LONG || t == CC_TYPE_ULONG_LONG || t == CC_TYPE_ENUM || t == CC_TYPE_BITINT;
}

static int is_numeric_type(cc_type_t t) {
    return is_integral_type(t) || t == CC_TYPE_FLOAT || t == CC_TYPE_DOUBLE || t == CC_TYPE_LDOUBLE ||
           t == CC_TYPE_COMPLEX || t == CC_TYPE_IMAGINARY || t == CC_TYPE_DECIMAL32 || t == CC_TYPE_DECIMAL64 ||
           t == CC_TYPE_DECIMAL128 || t == CC_TYPE_ATOMIC;
}

static int is_unsigned_load_type(cc_type_t t) {
    return t == CC_TYPE_BOOL || is_unsigned_integral_type(t) || is_pointer_type(t);
}

static cc_type_t ptr_base_type(cc_type_t t);

static int can_convert_type(cc_type_t dst, cc_type_t src) {
    if (dst == src) {
        return 1;
    }
    if (is_numeric_type(dst) && is_numeric_type(src)) {
        return 1;
    }
    if (is_pointer_type(dst) && is_pointer_type(src)) {
        cc_type_t dbase = ptr_base_type(dst);
        cc_type_t sbase = ptr_base_type(src);
        if (dbase == CC_TYPE_VOID || sbase == CC_TYPE_VOID || dbase == sbase) {
            return 1;
        }
    }
    if (is_pointer_type(dst) && is_integral_type(src)) {
        return 1;
    }
    if (is_integral_type(dst) && is_pointer_type(src)) {
        return 1;
    }
    return 0;
}

static cc_type_t ptr_base_type(cc_type_t t) {
    return cc_type_deref_once(t);
}

static cc_type_t integral_promo_type(cc_type_t t) {
    if (t == CC_TYPE_BOOL || t == CC_TYPE_CHAR || t == CC_TYPE_SCHAR || t == CC_TYPE_UCHAR || t == CC_TYPE_SHORT ||
        t == CC_TYPE_USHORT) {
        return CC_TYPE_INT;
    }
    return t;
}

static cc_type_t common_integral_type(cc_type_t a, cc_type_t b) {
    cc_type_t ap = integral_promo_type(a);
    cc_type_t bp = integral_promo_type(b);

    if (ap == CC_TYPE_ULONG_LONG || bp == CC_TYPE_ULONG_LONG) {
        return CC_TYPE_ULONG_LONG;
    }
    if (ap == CC_TYPE_LONG_LONG || bp == CC_TYPE_LONG_LONG) {
        if (is_unsigned_integral_type(ap) || is_unsigned_integral_type(bp)) {
            return CC_TYPE_ULONG_LONG;
        }
        return CC_TYPE_LONG_LONG;
    }
    if (ap == CC_TYPE_ULONG || bp == CC_TYPE_ULONG) {
        return CC_TYPE_ULONG;
    }
    if (ap == CC_TYPE_LONG || bp == CC_TYPE_LONG) {
        if (is_unsigned_integral_type(ap) || is_unsigned_integral_type(bp)) {
            return CC_TYPE_ULONG;
        }
        return CC_TYPE_LONG;
    }
    if (ap == CC_TYPE_UINT || bp == CC_TYPE_UINT) {
        return CC_TYPE_UINT;
    }
    return CC_TYPE_INT;
}

static long type_size_bytes(cc_type_t t) {
    if (cc_type_is_pointer(t)) {
        return g_pointer_size_bytes;
    }
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_SCHAR:
    case CC_TYPE_UCHAR:
        return 1;
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
        return 2;
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
    case CC_TYPE_FLOAT:
        return 4;
    case CC_TYPE_LONG:
    case CC_TYPE_ULONG:
        return g_pointer_size_bytes;
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
    case CC_TYPE_DOUBLE:
        return 8;
    case CC_TYPE_LDOUBLE:
        return 16;
    case CC_TYPE_ENUM:
        return 4;
    case CC_TYPE_COMPLEX:
        return 16;
    case CC_TYPE_IMAGINARY:
        return 8;
    case CC_TYPE_BITINT:
        return g_pointer_size_bytes;
    case CC_TYPE_DECIMAL32:
        return 4;
    case CC_TYPE_DECIMAL64:
        return 8;
    case CC_TYPE_DECIMAL128:
        return 16;
    case CC_TYPE_ATOMIC:
        return g_pointer_size_bytes;
    case CC_TYPE_FUNC:
        return g_pointer_size_bytes;
    default:
        return -1;
    }
}

static long type_size_bytes_with_struct(const cc_translation_unit_t *tu, cc_type_t t, int struct_id) {
    if (t == CC_TYPE_ATOMIC) {
        if (struct_id <= 0) {
            return -1;
        }
        return type_size_bytes_with_struct(tu, (cc_type_t)struct_id, -1);
    }
    if (t == CC_TYPE_BITINT) {
        if (struct_id <= 0) {
            return -1;
        }
        return (struct_id + 7) / 8;
    }
    long n = type_size_bytes(t);
    if (n > 0) {
        return n;
    }
    if (t == CC_TYPE_VOID && tu != NULL && struct_id >= 0 && (size_t)struct_id < tu->struct_count &&
        tu->structs[struct_id].complete) {
        return tu->structs[struct_id].size;
    }
    return -1;
}

static long type_size_bytes_struct(const cc_translation_unit_t *tu, cc_type_t t, int struct_id) {
    if (t == CC_TYPE_ATOMIC) {
        if (struct_id <= 0) {
            return -1;
        }
        return type_size_bytes_struct(tu, (cc_type_t)struct_id, -1);
    }
    if (t == CC_TYPE_BITINT) {
        if (struct_id <= 0) {
            return -1;
        }
        return (struct_id + 7) / 8;
    }
    long n = type_size_bytes(t);
    if (n > 0) {
        return n;
    }
    if (t == CC_TYPE_VOID && tu != NULL && struct_id >= 0 && (size_t)struct_id < tu->struct_count &&
        tu->structs[struct_id].complete) {
        return tu->structs[struct_id].size;
    }
    return -1;
}

static long pointer_elem_size_bytes(const cc_translation_unit_t *tu, cc_type_t ptr_type, int ptr_struct_id) {
    cc_type_t pbase;
    long n;
    if (!is_pointer_type(ptr_type)) {
        return -1;
    }
    pbase = ptr_base_type(ptr_type);
    if (pbase == CC_TYPE_VOID) {
        n = type_size_bytes_struct(tu, CC_TYPE_VOID, ptr_struct_id);
        if (n > 0) {
            return n;
        }
        return 1;
    }
    n = type_size_bytes(pbase);
    if (n > 0) {
        return n;
    }
    return -1;
}

static long array_decl_scalar_size_bytes(const cc_translation_unit_t *tu, cc_type_t array_type, int struct_id,
                                         int array_ndim) {
    cc_type_t cur_type = array_type;
    int cur_sid = struct_id;
    int i;
    if (array_ndim <= 0) {
        return -1;
    }
    for (i = 0; i < array_ndim; ++i) {
        if (!is_pointer_type(cur_type)) {
            return -1;
        }
        cur_type = ptr_base_type(cur_type);
        if (cur_type != CC_TYPE_VOID) {
            cur_sid = -1;
        }
    }
    return type_size_bytes_with_struct(tu, cur_type, cur_sid);
}

static long expr_array_step_size_bytes(const cc_translation_unit_t *tu, const cc_expr_t *ptr_expr, long fallback) {
    long stride = 1;
    long scalar_size;
    int i;
    long dim;
    if (ptr_expr == NULL || ptr_expr->array_ndim <= 0) {
        return fallback;
    }
    scalar_size = array_decl_scalar_size_bytes(tu, ptr_expr->value_type, ptr_expr->struct_id, ptr_expr->array_ndim);
    if (scalar_size <= 0) {
        return fallback;
    }
    for (i = 1; i < ptr_expr->array_ndim; ++i) {
        dim = ptr_expr->array_dims[i];
        if (dim <= 0) {
            dim = 1;
        }
        if (stride > LONG_MAX / dim) {
            return fallback;
        }
        stride *= dim;
    }
    if (scalar_size > LONG_MAX / stride) {
        return fallback;
    }
    return scalar_size * stride;
}

static int is_cmp_op(cc_binop_t op) {
    return op == CC_BIN_EQ || op == CC_BIN_NE || op == CC_BIN_LT || op == CC_BIN_LE || op == CC_BIN_GT ||
           op == CC_BIN_GE;
}

static cc_cmp_kind_t bin_to_cmp(cc_binop_t op) {
    switch (op) {
    case CC_BIN_EQ:
        return CC_CMP_EQ;
    case CC_BIN_NE:
        return CC_CMP_NE;
    case CC_BIN_LT:
        return CC_CMP_LT;
    case CC_BIN_LE:
        return CC_CMP_LE;
    case CC_BIN_GT:
        return CC_CMP_GT;
    case CC_BIN_GE:
        return CC_CMP_GE;
    default:
        return CC_CMP_EQ;
    }
}

static int is_logical_op(cc_binop_t op) {
    return op == CC_BIN_LAND || op == CC_BIN_LOR;
}

static int new_label(cc_ssa_function_t *f);
static int emit_label_instr(cc_ssa_function_t *sf, int label);
static int emit_br_instr(cc_ssa_function_t *sf, int label);
static int emit_br_cond_instr(cc_ssa_function_t *sf, int cond, int label_true, int label_false);
static int emit_mov_instr(cc_ssa_function_t *sf, int dst, int src);
static int emit_const_i64_instr(cc_ssa_function_t *sf, long v);
static int emit_const_f64_instr(cc_ssa_function_t *sf, double v);
static int emit_memcpy_instr(cc_ssa_function_t *sf, int dst_ptr, int src_ptr, long size, cc_diag_t *diag);
static int normalize_integral_value(cc_ssa_function_t *sf, int v, cc_type_t t, cc_diag_t *diag);
static int normalize_float_value(cc_ssa_function_t *sf, int v, cc_type_t t, cc_diag_t *diag);
static int lower_truthy_value(cc_ssa_function_t *sf, int v, cc_diag_t *diag);
static int lower_expr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                      var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag);
static int lower_collect_labels_expr(cc_ssa_function_t *sf, const cc_expr_t *e, lower_ctx_t *ctx, cc_diag_t *diag);
static int lower_collect_hoisted_allocs(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, lower_ctx_t *ctx,
                                        const cc_stmt_t *s, cc_diag_t *diag);

static int lower_find_label(const lower_ctx_t *ctx, const char *name) {
    size_t i;
    for (i = 0; i < ctx->label_count; ++i) {
        if (strcmp(ctx->labels[i].name, name) == 0) {
            return ctx->labels[i].label;
        }
    }
    return -1;
}

static int lower_find_hoisted_alloc(const lower_ctx_t *ctx, const cc_stmt_t *decl) {
    size_t i;
    if (ctx == NULL || decl == NULL) {
        return -1;
    }
    for (i = 0; i < ctx->hoisted_alloc_count; ++i) {
        if (ctx->hoisted_allocs[i].decl == decl) {
            return ctx->hoisted_allocs[i].value;
        }
    }
    return -1;
}

static int append_hoisted_alloc(lower_ctx_t *ctx, const cc_stmt_t *decl, int value, cc_diag_t *diag) {
    hoisted_alloc_entry_t *next;
    if (ctx == NULL || decl == NULL || value < 0) {
        return -1;
    }
    next = (hoisted_alloc_entry_t *)realloc(ctx->hoisted_allocs, (ctx->hoisted_alloc_count + 1) * sizeof(*next));
    if (next == NULL) {
        set_diag(diag, "out of memory recording hoisted local storage");
        return -1;
    }
    ctx->hoisted_allocs = next;
    ctx->hoisted_allocs[ctx->hoisted_alloc_count].decl = decl;
    ctx->hoisted_allocs[ctx->hoisted_alloc_count].value = value;
    ctx->hoisted_alloc_count++;
    return 0;
}

static int lower_collect_labels(cc_ssa_function_t *sf, const cc_stmt_t *s, lower_ctx_t *ctx, cc_diag_t *diag) {
    size_t i;
    size_t ai;
    if (s == NULL) {
        return 0;
    }
    if (lower_collect_labels_expr(sf, s->expr, ctx, diag) != 0 ||
        lower_collect_labels_expr(sf, s->init_expr, ctx, diag) != 0 ||
        lower_collect_labels_expr(sf, s->post_expr, ctx, diag) != 0) {
        return -1;
    }
    if (s->kind == CC_STMT_ASM) {
        for (ai = 0; ai < s->asm_output_count; ++ai) {
            if (lower_collect_labels_expr(sf, s->asm_outputs[ai].expr, ctx, diag) != 0) {
                return -1;
            }
        }
        for (ai = 0; ai < s->asm_input_count; ++ai) {
            if (lower_collect_labels_expr(sf, s->asm_inputs[ai].expr, ctx, diag) != 0) {
                return -1;
            }
        }
    }
    if (s->kind == CC_STMT_LABEL) {
        label_entry_t *next;
        if (s->label_name == NULL || s->label_name[0] == '\0') {
            set_diag(diag, "malformed labeled statement in lowering");
            return -1;
        }
        if (lower_find_label(ctx, s->label_name) >= 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "duplicate label in lowering: %s", s->label_name);
            }
            return -1;
        }
        next = (label_entry_t *)realloc(ctx->labels, (ctx->label_count + 1) * sizeof(*next));
        if (next == NULL) {
            set_diag(diag, "out of memory collecting labels");
            return -1;
        }
        ctx->labels = next;
        ctx->labels[ctx->label_count].name = xstrdup(s->label_name);
        if (ctx->labels[ctx->label_count].name == NULL) {
            set_diag(diag, "out of memory duplicating label name");
            return -1;
        }
        ctx->labels[ctx->label_count].label = new_label(sf);
        if (ctx->labels[ctx->label_count].label < 0) {
            set_diag(diag, "out of memory assigning label id");
            return -1;
        }
        ctx->label_count++;
        return lower_collect_labels(sf, s->then_branch, ctx, diag);
    }
    if (s->kind == CC_STMT_IF) {
        if (lower_collect_labels(sf, s->then_branch, ctx, diag) != 0) {
            return -1;
        }
        return lower_collect_labels(sf, s->else_branch, ctx, diag);
    }
    if (s->kind == CC_STMT_WHILE || s->kind == CC_STMT_DO || s->kind == CC_STMT_SWITCH) {
        return lower_collect_labels(sf, s->then_branch, ctx, diag);
    }
    if (s->kind == CC_STMT_FOR) {
        if (lower_collect_labels(sf, s->init_stmt, ctx, diag) != 0) {
            return -1;
        }
        return lower_collect_labels(sf, s->then_branch, ctx, diag);
    }
    if (s->kind == CC_STMT_BLOCK) {
        for (i = 0; i < s->block_count; ++i) {
            if (lower_collect_labels(sf, &s->block_stmts[i], ctx, diag) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int lower_collect_labels_expr(cc_ssa_function_t *sf, const cc_expr_t *e, lower_ctx_t *ctx, cc_diag_t *diag) {
    size_t i;
    if (e == NULL) {
        return 0;
    }
    if (lower_collect_labels_expr(sf, e->lhs, ctx, diag) != 0 ||
        lower_collect_labels_expr(sf, e->rhs, ctx, diag) != 0 ||
        lower_collect_labels_expr(sf, e->third, ctx, diag) != 0) {
        return -1;
    }
    for (i = 0; i < e->arg_count; ++i) {
        if (lower_collect_labels_expr(sf, e->args[i], ctx, diag) != 0) {
            return -1;
        }
    }
    if (e->kind == CC_EXPR_STMT) {
        for (i = 0; i < e->stmt_expr_count; ++i) {
            if (lower_collect_labels(sf, &e->stmt_expr_stmts[i], ctx, diag) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int push_instr(cc_ssa_function_t *f, cc_ssa_instr_t in) {
    if (f->instr_count == f->instr_cap) {
        size_t ncap = f->instr_cap == 0 ? 16 : f->instr_cap * 2;
        cc_ssa_instr_t *next = (cc_ssa_instr_t *)realloc(f->instrs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        f->instrs = next;
        f->instr_cap = ncap;
    }
    f->instrs[f->instr_count++] = in;
    return 0;
}

static int new_value(cc_ssa_function_t *f, cc_value_type_t vt) {
    int id = f->value_count;
    f->value_count++;

    if ((size_t)f->value_count > f->value_cap) {
        size_t ncap = f->value_cap == 0 ? 32 : f->value_cap * 2;
        while (ncap < (size_t)f->value_count) {
            ncap *= 2;
        }
        cc_value_type_t *next = (cc_value_type_t *)realloc(f->value_types, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        f->value_types = next;
        f->value_cap = ncap;
    }

    f->value_types[id] = vt;
    return id;
}

static int new_label(cc_ssa_function_t *f) {
    return f->label_count++;
}

static int append_synth_global(cc_ssa_module_t *m, const cc_ssa_global_t *g) {
    cc_ssa_global_t *next;
    if (m == NULL || g == NULL) {
        return -1;
    }
    next = (cc_ssa_global_t *)realloc(m->globals, (m->global_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    m->globals = next;
    m->globals[m->global_count++] = *g;
    return 0;
}

static int make_local_static_symbol(char *out, size_t out_sz, const cc_function_t *fn, const char *name,
                                    size_t unique, const char *suffix) {
    const char *fn_name = (fn != NULL && fn->name != NULL) ? fn->name : "fn";
    const char *var_name = (name != NULL && name[0] != '\0') ? name : "var";
    if (suffix == NULL) {
        suffix = "";
    }
    if (snprintf(out, out_sz, "__cc_static_%s_%s_%zu%s", fn_name, var_name, unique, suffix) >= (int)out_sz) {
        return -1;
    }
    return 0;
}

static int emit_store_global_i64(cc_ssa_function_t *sf, const char *sym, int v, long size, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int addr = emit_global_addr(sf, sym, diag);
    if (addr < 0) {
        return -1;
    }
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_STORE;
    in.dst = -1;
    in.lhs = addr;
    in.rhs = v;
    in.imm = size;
    if (push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory appending static global store");
        return -1;
    }
    return 0;
}

static cc_value_type_t value_type(const cc_ssa_function_t *f, int v) {
    if (v < 0 || (size_t)v >= (size_t)f->value_count) {
        return CC_VAL_I64;
    }
    return f->value_types[v];
}

static int var_find_visible(var_entry_t *vars, size_t var_count, const char *name, int depth) {
    size_t i = var_count;
    while (i > 0) {
        i--;
        if (vars[i].depth <= depth && strcmp(vars[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int var_define(var_entry_t **vars, size_t *var_count, const char *name, cc_type_t type, int struct_id,
                      long array_len, int array_ndim, const long array_dims[CC_MAX_ARRAY_DIMS], int value, int depth,
                      int is_static_storage, const char *static_sym) {
    var_entry_t *next;
    char *dup;
    char *sym_dup = NULL;

    next = (var_entry_t *)realloc(*vars, (*var_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *vars = next;

    dup = xstrdup(name);
    if (dup == NULL) {
        return -1;
    }
    if (is_static_storage && static_sym != NULL) {
        sym_dup = xstrdup(static_sym);
        if (sym_dup == NULL) {
            free(dup);
            return -1;
        }
    }
    (*vars)[*var_count].name = dup;
    (*vars)[*var_count].type = type;
    (*vars)[*var_count].struct_id = struct_id;
    (*vars)[*var_count].array_len = array_len;
    (*vars)[*var_count].array_ndim = array_ndim;
    if (array_dims != NULL) {
        memcpy((*vars)[*var_count].array_dims, array_dims, sizeof((*vars)[*var_count].array_dims));
    } else {
        memset((*vars)[*var_count].array_dims, 0, sizeof((*vars)[*var_count].array_dims));
    }
    (*vars)[*var_count].value = value;
    (*vars)[*var_count].is_static_storage = is_static_storage ? 1 : 0;
    (*vars)[*var_count].static_sym = sym_dup;
    (*vars)[*var_count].depth = depth;
    (*var_count)++;
    return 0;
}

static const cc_function_t *find_fn(const cc_translation_unit_t *tu, const char *name) {
    size_t i;
    const cc_function_t *best = NULL;
    int best_score = -1;
    if (tu == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < tu->func_count; ++i) {
        const cc_function_t *f = &tu->funcs[i];
        int score;
        if (strcmp(f->name, name) != 0) {
            continue;
        }
        score = (f->has_prototype ? 2 : 0) + (f->has_body ? 1 : 0);
        if (best == NULL || score > best_score) {
            best = f;
            best_score = score;
        }
    }
    return best;
}

static const cc_global_t *find_global(const cc_translation_unit_t *tu, const char *name) {
    size_t i;
    if (tu == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < tu->global_count; ++i) {
        if (strcmp(tu->globals[i].name, name) == 0) {
            return &tu->globals[i];
        }
    }
    return NULL;
}

static int member_base_struct_id(const cc_expr_t *e) {
    if (e == NULL || e->kind != CC_EXPR_MEMBER || e->lhs == NULL) {
        return -1;
    }
    return e->lhs->struct_id;
}

static const cc_struct_member_t *find_struct_member(const cc_translation_unit_t *tu, int sid, const char *name) {
    size_t i;
    if (tu == NULL || name == NULL || sid < 0 || (size_t)sid >= tu->struct_count) {
        return NULL;
    }
    for (i = 0; i < tu->structs[sid].member_count; ++i) {
        if (strcmp(tu->structs[sid].members[i].name, name) == 0) {
            return &tu->structs[sid].members[i];
        }
    }
    return NULL;
}

static const cc_struct_member_t *find_union_cast_member(const cc_translation_unit_t *tu, int sid, cc_type_t src_type) {
    const cc_struct_def_t *sd;
    size_t i;
    if (tu == NULL || sid < 0 || (size_t)sid >= tu->struct_count) {
        return NULL;
    }
    sd = &tu->structs[sid];
    if (!sd->complete || !sd->is_union) {
        return NULL;
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        if (can_convert_type(m->type, src_type)) {
            return m;
        }
    }
    return NULL;
}

static int is_synthetic_struct_array_member(const cc_translation_unit_t *tu, const cc_expr_t *e);

static int is_hex_digit_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static long string_literal_size_bytes(const char *lit) {
    size_t i;
    long out_len = 0;
    if (lit == NULL) {
        return 1;
    }
    i = 0;
    while (lit[i] != '\0' && lit[i] != '"') {
        i++;
    }
    if (lit[i] != '"') {
        size_t n = strlen(lit);
        if (n >= (size_t)LONG_MAX) {
            return -1;
        }
        return (long)(n + 1);
    }
    i++;
    while (lit[i] != '\0') {
        if (lit[i] == '"') {
            return out_len + 1;
        }
        if (lit[i] == '\\') {
            i++;
            if (lit[i] == '\0') {
                return -1;
            }
            if (lit[i] == 'x') {
                i++;
                if (!is_hex_digit_char(lit[i])) {
                    return -1;
                }
                while (is_hex_digit_char(lit[i])) {
                    i++;
                }
                out_len++;
                continue;
            }
            if (lit[i] >= '0' && lit[i] <= '7') {
                int oct = 1;
                while (oct < 3 && lit[i + 1] >= '0' && lit[i + 1] <= '7') {
                    i++;
                    oct++;
                }
                i++;
                out_len++;
                continue;
            }
            i++;
            out_len++;
            continue;
        }
        i++;
        out_len++;
    }
    return -1;
}

static long array_size_bytes(const cc_translation_unit_t *tu, cc_type_t array_type, int struct_id, long array_len) {
    cc_type_t elem_type;
    long elem_size;
    long elem_count;
    if (!is_pointer_type(array_type) || array_len < 0) {
        return -1;
    }
    elem_type = ptr_base_type(array_type);
    elem_size = type_size_bytes_with_struct(tu, elem_type, struct_id);
    if (elem_size <= 0) {
        return -1;
    }
    elem_count = array_len > 0 ? array_len : 1;
    if (elem_count <= 0 || elem_size > LONG_MAX / elem_count) {
        return -1;
    }
    return elem_size * elem_count;
}

static long sizeof_expr_bytes(const cc_translation_unit_t *tu, var_entry_t *vars, size_t var_count, int depth,
                              const cc_expr_t *e) {
    if (e == NULL) {
        return -1;
    }
    if (e->kind == CC_EXPR_IDENT && e->ident != NULL) {
        int idx = var_find_visible(vars, var_count, e->ident, depth);
        if (idx >= 0) {
            long n = array_size_bytes(tu, vars[idx].type, vars[idx].struct_id, vars[idx].array_len);
            if (n > 0) {
                return n;
            }
        } else {
            const cc_global_t *g = find_global(tu, e->ident);
            if (g != NULL) {
                long n = array_size_bytes(tu, g->type, g->type_struct_id, g->array_len);
                if (n > 0) {
                    return n;
                }
            }
        }
    }
    if (e->kind == CC_EXPR_MEMBER && is_synthetic_struct_array_member(tu, e)) {
        const cc_struct_member_t *m = find_struct_member(tu, member_base_struct_id(e), e->ident);
        if (m != NULL && m->size > 0) {
            return m->size;
        }
    }
    if (e->kind == CC_EXPR_STR) {
        return string_literal_size_bytes(e->ident);
    }
    return type_size_bytes_struct(tu, e->value_type, e->struct_id);
}

/*
 * The frontend currently encodes fixed-size arrays in structs as pointer-typed
 * members with expanded storage size. Treat those as address-valued in rvalue
 * contexts so `arr[i]` indexes the inline storage, not an uninitialized pointer.
 */
static int is_synthetic_struct_array_member(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    const cc_struct_member_t *m;
    int sid;
    if (e == NULL || e->kind != CC_EXPR_MEMBER || e->ident == NULL) {
        return 0;
    }
    sid = member_base_struct_id(e);
    m = find_struct_member(tu, sid, e->ident);
    if (m == NULL || !is_pointer_type(m->type)) {
        return 0;
    }
    if (m->array_ndim > 0 || m->array_len >= 0) {
        return 1;
    }
    if (m->size > 0 && m->size != g_pointer_size_bytes) {
        return 1;
    }
    return 0;
}

static int expr_is_array_object_ref(const cc_translation_unit_t *tu, var_entry_t *vars, size_t var_count, int depth,
                                    const cc_expr_t *e) {
    int idx;
    const cc_global_t *g;
    if (e == NULL) {
        return 0;
    }
    if (e->kind == CC_EXPR_IDENT && e->ident != NULL) {
        idx = var_find_visible(vars, var_count, e->ident, depth);
        if (idx >= 0) {
            return (vars[idx].array_ndim > 0 && vars[idx].array_len >= 0) ||
                   is_array_object_decl(vars[idx].type, vars[idx].array_len, vars[idx].array_ndim);
        }
        g = find_global(tu, e->ident);
        if (g != NULL) {
            return (g->array_ndim > 0 && g->array_len >= 0) ||
                   is_array_object_decl(g->type, g->array_len, g->array_ndim);
        }
    }
    if (e->kind == CC_EXPR_MEMBER) {
        if (is_synthetic_struct_array_member(tu, e)) {
            return 1;
        }
        if (is_pointer_type(e->value_type) && e->array_ndim > 0) {
            return 1;
        }
    }
    if (e->kind == CC_EXPR_CAST && e->lhs != NULL) {
        return expr_is_array_object_ref(tu, vars, var_count, depth, e->lhs);
    }
    return 0;
}

static int expr_is_array_pointer_chain(const cc_translation_unit_t *tu, var_entry_t *vars, size_t var_count, int depth,
                                       const cc_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    if (expr_is_array_object_ref(tu, vars, var_count, depth, e)) {
        return 1;
    }
    if (e->kind == CC_EXPR_DEREF && e->lhs != NULL && is_pointer_type(e->value_type)) {
        return expr_is_array_pointer_chain(tu, vars, var_count, depth, e->lhs);
    }
    if (e->kind == CC_EXPR_BIN && (e->op == CC_BIN_ADD || e->op == CC_BIN_SUB) && is_pointer_type(e->value_type)) {
        if (expr_is_array_pointer_chain(tu, vars, var_count, depth, e->lhs)) {
            return 1;
        }
        return expr_is_array_pointer_chain(tu, vars, var_count, depth, e->rhs);
    }
    return 0;
}

static int builtin_bswap_bits(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, "__builtin_bswap16") == 0) {
        return 16;
    }
    if (strcmp(name, "__builtin_bswap32") == 0) {
        return 32;
    }
    if (strcmp(name, "__builtin_bswap64") == 0) {
        return 64;
    }
    return 0;
}

typedef enum {
    BUILTIN_NONE = 0,
    BUILTIN_VA_START,
    BUILTIN_VA_END,
    BUILTIN_VA_COPY,
    BUILTIN_VA_ARG,
    BUILTIN_ALLOCA,
    BUILTIN_EXPECT,
    BUILTIN_CONSTANT_P,
    BUILTIN_TRAP,
    BUILTIN_UNREACHABLE,
    BUILTIN_ASSUME,
    BUILTIN_ASSUME_ALIGNED,
    BUILTIN_UNPREDICTABLE,
    BUILTIN_CLZ,
    BUILTIN_CTZ,
    BUILTIN_POPCOUNT,
    BUILTIN_ADD_OVERFLOW,
    BUILTIN_SUB_OVERFLOW,
    BUILTIN_MUL_OVERFLOW,
    BUILTIN_ADD_OVERFLOW_P,
    BUILTIN_SUB_OVERFLOW_P,
    BUILTIN_MUL_OVERFLOW_P,
    BUILTIN_OBJECT_SIZE,
    BUILTIN_MEMCPY,
    BUILTIN_MEMMOVE,
    BUILTIN_MEMSET,
    BUILTIN_MEMCPY_CHK,
    BUILTIN_MEMMOVE_CHK,
    BUILTIN_MEMSET_CHK,
    BUILTIN_HUGE_VAL,
    BUILTIN_HUGE_VALF,
    BUILTIN_HUGE_VALL,
    BUILTIN_NANF,
    BUILTIN_NAN,
    BUILTIN_NANL,
    BUILTIN_SYNC_FETCH_ADD,
    BUILTIN_SYNC_FETCH_SUB,
    BUILTIN_SYNC_SUB_AND_FETCH,
    BUILTIN_SYNC_BOOL_CAS,
    BUILTIN_SYNC_LOCK_TEST_AND_SET,
    BUILTIN_SYNC_LOCK_RELEASE,
    BUILTIN_SYNC_SYNCHRONIZE,
    BUILTIN_ATOMIC_FETCH_ADD,
    BUILTIN_ATOMIC_FETCH_SUB,
    BUILTIN_ATOMIC_EXCHANGE_N,
    BUILTIN_ATOMIC_LOAD_N,
    BUILTIN_ATOMIC_STORE_N
} builtin_kind_t;

static builtin_kind_t builtin_kind(const char *name) {
    if (name == NULL) {
        return BUILTIN_NONE;
    }
    if (strcmp(name, "__builtin_va_start") == 0) {
        return BUILTIN_VA_START;
    }
    if (strcmp(name, "__builtin_va_end") == 0) {
        return BUILTIN_VA_END;
    }
    if (strcmp(name, "__builtin_va_copy") == 0) {
        return BUILTIN_VA_COPY;
    }
    if (strcmp(name, "__builtin_va_arg") == 0) {
        return BUILTIN_VA_ARG;
    }
    if (strcmp(name, "__builtin_alloca") == 0 || strcmp(name, "alloca") == 0) {
        return BUILTIN_ALLOCA;
    }
    if (strcmp(name, "__builtin_expect") == 0) {
        return BUILTIN_EXPECT;
    }
    if (strcmp(name, "__builtin_constant_p") == 0) {
        return BUILTIN_CONSTANT_P;
    }
    if (strcmp(name, "__builtin_trap") == 0) {
        return BUILTIN_TRAP;
    }
    if (strcmp(name, "__builtin_unreachable") == 0) {
        return BUILTIN_UNREACHABLE;
    }
    if (strcmp(name, "__builtin_assume") == 0) {
        return BUILTIN_ASSUME;
    }
    if (strcmp(name, "__builtin_assume_aligned") == 0) {
        return BUILTIN_ASSUME_ALIGNED;
    }
    if (strcmp(name, "__builtin_unpredictable") == 0) {
        return BUILTIN_UNPREDICTABLE;
    }
    if (strcmp(name, "__builtin_clz") == 0 || strcmp(name, "__builtin_clzl") == 0 ||
        strcmp(name, "__builtin_clzll") == 0) {
        return BUILTIN_CLZ;
    }
    if (strcmp(name, "__builtin_ctz") == 0 || strcmp(name, "__builtin_ctzl") == 0 ||
        strcmp(name, "__builtin_ctzll") == 0) {
        return BUILTIN_CTZ;
    }
    if (strcmp(name, "__builtin_popcount") == 0 || strcmp(name, "__builtin_popcountl") == 0 ||
        strcmp(name, "__builtin_popcountll") == 0) {
        return BUILTIN_POPCOUNT;
    }
    if (strcmp(name, "__builtin_add_overflow") == 0) {
        return BUILTIN_ADD_OVERFLOW;
    }
    if (strcmp(name, "__builtin_sub_overflow") == 0) {
        return BUILTIN_SUB_OVERFLOW;
    }
    if (strcmp(name, "__builtin_mul_overflow") == 0) {
        return BUILTIN_MUL_OVERFLOW;
    }
    if (strcmp(name, "__builtin_add_overflow_p") == 0) {
        return BUILTIN_ADD_OVERFLOW_P;
    }
    if (strcmp(name, "__builtin_sub_overflow_p") == 0) {
        return BUILTIN_SUB_OVERFLOW_P;
    }
    if (strcmp(name, "__builtin_mul_overflow_p") == 0) {
        return BUILTIN_MUL_OVERFLOW_P;
    }
    if (strcmp(name, "__builtin_object_size") == 0) {
        return BUILTIN_OBJECT_SIZE;
    }
    if (strcmp(name, "__builtin_memcpy") == 0) {
        return BUILTIN_MEMCPY;
    }
    if (strcmp(name, "__builtin_memmove") == 0) {
        return BUILTIN_MEMMOVE;
    }
    if (strcmp(name, "__builtin_memset") == 0) {
        return BUILTIN_MEMSET;
    }
    if (strcmp(name, "__builtin___memcpy_chk") == 0) {
        return BUILTIN_MEMCPY_CHK;
    }
    if (strcmp(name, "__builtin___memmove_chk") == 0) {
        return BUILTIN_MEMMOVE_CHK;
    }
    if (strcmp(name, "__builtin___memset_chk") == 0) {
        return BUILTIN_MEMSET_CHK;
    }
    if (strcmp(name, "__builtin_huge_val") == 0) {
        return BUILTIN_HUGE_VAL;
    }
    if (strcmp(name, "__builtin_huge_valf") == 0) {
        return BUILTIN_HUGE_VALF;
    }
    if (strcmp(name, "__builtin_huge_vall") == 0) {
        return BUILTIN_HUGE_VALL;
    }
    if (strcmp(name, "__builtin_nanf") == 0) {
        return BUILTIN_NANF;
    }
    if (strcmp(name, "__builtin_nan") == 0) {
        return BUILTIN_NAN;
    }
    if (strcmp(name, "__builtin_nanl") == 0) {
        return BUILTIN_NANL;
    }
    if (strcmp(name, "__sync_fetch_and_add") == 0) {
        return BUILTIN_SYNC_FETCH_ADD;
    }
    if (strcmp(name, "__sync_fetch_and_sub") == 0) {
        return BUILTIN_SYNC_FETCH_SUB;
    }
    if (strcmp(name, "__sync_sub_and_fetch") == 0) {
        return BUILTIN_SYNC_SUB_AND_FETCH;
    }
    if (strcmp(name, "__sync_bool_compare_and_swap") == 0) {
        return BUILTIN_SYNC_BOOL_CAS;
    }
    if (strcmp(name, "__sync_lock_test_and_set") == 0) {
        return BUILTIN_SYNC_LOCK_TEST_AND_SET;
    }
    if (strcmp(name, "__sync_lock_release") == 0) {
        return BUILTIN_SYNC_LOCK_RELEASE;
    }
    if (strcmp(name, "__sync_synchronize") == 0) {
        return BUILTIN_SYNC_SYNCHRONIZE;
    }
    if (strcmp(name, "__atomic_fetch_add") == 0) {
        return BUILTIN_ATOMIC_FETCH_ADD;
    }
    if (strcmp(name, "__atomic_fetch_sub") == 0) {
        return BUILTIN_ATOMIC_FETCH_SUB;
    }
    if (strcmp(name, "__atomic_exchange_n") == 0) {
        return BUILTIN_ATOMIC_EXCHANGE_N;
    }
    if (strcmp(name, "__atomic_load_n") == 0) {
        return BUILTIN_ATOMIC_LOAD_N;
    }
    if (strcmp(name, "__atomic_store_n") == 0) {
        return BUILTIN_ATOMIC_STORE_N;
    }
    return BUILTIN_NONE;
}

static int is_freestanding_mode(void) {
    const char *v = getenv("CC_FREESTANDING");
    if (v == NULL || v[0] == '\0' || strcmp(v, "0") == 0) {
        return 0;
    }
    return 1;
}

static int emit_local_storage_alloc(cc_ssa_function_t *sf, long total_size, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int dst;

    if (total_size <= 0) {
        set_diag(diag, "invalid local storage size");
        return -1;
    }
    dst = new_value(sf, CC_VAL_I64);
    if (dst < 0) {
        set_diag(diag, "out of memory allocating local storage value");
        return -1;
    }
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_STACKALLOC;
    in.dst = dst;
    in.imm = total_size;
    if (push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory appending local storage allocation");
        return -1;
    }
    return dst;
}

static int stmt_needs_hoisted_alloc(const cc_stmt_t *s) {
    if (s == NULL || s->kind != CC_STMT_DECL) {
        return 0;
    }
    if ((s->storage & CC_STORAGE_STATIC) != 0) {
        return 0;
    }
    if (s->type == CC_TYPE_VOID && s->type_struct_id >= 0) {
        return 1;
    }
    return is_array_object_decl(s->type, s->array_len, s->array_ndim);
}

static int lower_collect_hoisted_allocs(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, lower_ctx_t *ctx,
                                        const cc_stmt_t *s, cc_diag_t *diag) {
    long total_size;
    long elem_size;
    long elem_count;
    int value;
    size_t i;

    if (s == NULL || ctx == NULL || sf == NULL || tu == NULL) {
        return 0;
    }

    if (stmt_needs_hoisted_alloc(s) && lower_find_hoisted_alloc(ctx, s) < 0) {
        if (s->type == CC_TYPE_VOID && s->type_struct_id >= 0) {
            total_size = type_size_bytes_with_struct(tu, s->type, s->type_struct_id);
            if (total_size <= 0) {
                total_size = 1;
            }
        } else {
            cc_type_t elem_type = ptr_base_type(s->type);
            elem_size = array_decl_scalar_size_bytes(tu, s->type, s->type_struct_id, s->array_ndim);
            if (elem_size <= 0) {
                elem_size = type_size_bytes_with_struct(tu, elem_type, s->type_struct_id);
            }
            if (elem_size <= 0) {
                set_diag(diag, "unsupported hoisted local array element type");
                return -1;
            }
            elem_count = s->array_len > 0 ? s->array_len : 1;
            if (elem_count <= 0 || elem_size > LONG_MAX / elem_count) {
                set_diag(diag, "local array allocation size overflow");
                return -1;
            }
            total_size = elem_size * elem_count;
        }
        value = emit_local_storage_alloc(sf, total_size, diag);
        if (value < 0) {
            return -1;
        }
        if (append_hoisted_alloc(ctx, s, value, diag) != 0) {
            return -1;
        }
    }

    switch (s->kind) {
    case CC_STMT_LABEL:
        return lower_collect_hoisted_allocs(tu, sf, ctx, s->then_branch, diag);
    case CC_STMT_IF:
        if (lower_collect_hoisted_allocs(tu, sf, ctx, s->then_branch, diag) != 0) {
            return -1;
        }
        return lower_collect_hoisted_allocs(tu, sf, ctx, s->else_branch, diag);
    case CC_STMT_WHILE:
    case CC_STMT_DO:
    case CC_STMT_SWITCH:
        return lower_collect_hoisted_allocs(tu, sf, ctx, s->then_branch, diag);
    case CC_STMT_FOR:
        if (lower_collect_hoisted_allocs(tu, sf, ctx, s->init_stmt, diag) != 0) {
            return -1;
        }
        return lower_collect_hoisted_allocs(tu, sf, ctx, s->then_branch, diag);
    case CC_STMT_BLOCK:
        for (i = 0; i < s->block_count; ++i) {
            if (lower_collect_hoisted_allocs(tu, sf, ctx, &s->block_stmts[i], diag) != 0) {
                return -1;
            }
        }
        return 0;
    default:
        return 0;
    }
}

static long align_up_long(long n, long a) {
    if (a <= 1) {
        return n;
    }
    return ((n + a - 1) / a) * a;
}

static int function_param_index_by_name(const lower_ctx_t *ctx, const char *name) {
    size_t i;
    if (ctx == NULL || ctx->fn == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < ctx->fn->param_count; ++i) {
        if (ctx->fn->params[i].name != NULL && strcmp(ctx->fn->params[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int cast_value(cc_ssa_function_t *sf, int v, cc_value_type_t dst, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    cc_value_type_t src = value_type(sf, v);

    if (src == dst) {
        return v;
    }

    memset(&in, 0, sizeof(in));
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.param_index = -1;

    if (src == CC_VAL_I64 && dst == CC_VAL_F64) {
        in.op = CC_SSA_I2F;
    } else if (src == CC_VAL_F64 && dst == CC_VAL_I64) {
        in.op = CC_SSA_F2I;
    } else {
        set_diag(diag, "unsupported cast in lowering");
        return -1;
    }

    in.lhs = v;
    in.dst = new_value(sf, dst);
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        return -1;
    }
    return in.dst;
}

static int integral_type_bits(cc_type_t t) {
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_UCHAR:
        return 8;
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
        return 16;
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
        return 32;
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
        return 64;
    default:
        return 0;
    }
}

static int normalize_integral_value(cc_ssa_function_t *sf, int v, cc_type_t t, cc_diag_t *diag) {
    int bits;
    int sh;
    int c;
    cc_ssa_instr_t in;

    if (!is_integral_type(t)) {
        return v;
    }

    if (t == CC_TYPE_BOOL) {
        int z = emit_const_i64_instr(sf, 0);
        if (z < 0) {
            return -1;
        }
        memset(&in, 0, sizeof(in));
        in.op = CC_SSA_CMP;
        in.cmp_kind = CC_CMP_NE;
        in.is_unsigned = 1;
        in.dst = new_value(sf, CC_VAL_I64);
        in.lhs = v;
        in.rhs = z;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            set_diag(diag, "out of memory normalizing boolean value");
            return -1;
        }
        return in.dst;
    }

    bits = integral_type_bits(t);
    if (bits <= 0 || bits >= 64) {
        return v;
    }

    if (is_unsigned_integral_type(t)) {
        unsigned long long mask_u = (1ULL << bits) - 1ULL;
        c = emit_const_i64_instr(sf, (long)mask_u);
        if (c < 0) {
            return -1;
        }
        memset(&in, 0, sizeof(in));
        in.op = CC_SSA_AND;
        in.dst = new_value(sf, CC_VAL_I64);
        in.lhs = v;
        in.rhs = c;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            set_diag(diag, "out of memory normalizing unsigned integer value");
            return -1;
        }
        return in.dst;
    }

    sh = 64 - bits;
    c = emit_const_i64_instr(sf, sh);
    if (c < 0) {
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_SHL;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = v;
    in.rhs = c;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory normalizing signed integer value");
        return -1;
    }
    v = in.dst;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_SHR;
    in.is_unsigned = 0;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = v;
    in.rhs = c;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory normalizing signed integer value");
        return -1;
    }
    return in.dst;
}

static int normalize_float_value(cc_ssa_function_t *sf, int v, cc_type_t t, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    if (t != CC_TYPE_FLOAT) {
        return v;
    }
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_FROUND32;
    in.dst = new_value(sf, CC_VAL_F64);
    in.lhs = v;
    in.rhs = -1;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory normalizing float value");
        return -1;
    }
    return in.dst;
}

static int eval_const_i64_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long *out) {
    long a;
    long b;

    if (e == NULL || out == NULL) {
        return -1;
    }

    switch (e->kind) {
    case CC_EXPR_INT:
        *out = e->int_val;
        return 0;

    case CC_EXPR_BIN:
        if (eval_const_i64_expr(tu, e->lhs, &a) != 0 || eval_const_i64_expr(tu, e->rhs, &b) != 0) {
            return -1;
        }
        switch (e->op) {
        case CC_BIN_ADD:
            *out = a + b;
            return 0;
        case CC_BIN_SUB:
            *out = a - b;
            return 0;
        case CC_BIN_MUL:
            *out = a * b;
            return 0;
        case CC_BIN_DIV:
            if (b == 0) {
                return -1;
            }
            *out = a / b;
            return 0;
        case CC_BIN_MOD:
            if (b == 0) {
                return -1;
            }
            *out = a % b;
            return 0;
        case CC_BIN_SHL:
            *out = a << (b & 63);
            return 0;
        case CC_BIN_SHR:
            *out = a >> (b & 63);
            return 0;
        case CC_BIN_BAND:
            *out = a & b;
            return 0;
        case CC_BIN_BOR:
            *out = a | b;
            return 0;
        case CC_BIN_BXOR:
            *out = a ^ b;
            return 0;
        case CC_BIN_EQ:
            *out = (a == b) ? 1 : 0;
            return 0;
        case CC_BIN_NE:
            *out = (a != b) ? 1 : 0;
            return 0;
        case CC_BIN_LT:
            *out = (a < b) ? 1 : 0;
            return 0;
        case CC_BIN_LE:
            *out = (a <= b) ? 1 : 0;
            return 0;
        case CC_BIN_GT:
            *out = (a > b) ? 1 : 0;
            return 0;
        case CC_BIN_GE:
            *out = (a >= b) ? 1 : 0;
            return 0;
        case CC_BIN_LAND:
            *out = (a != 0 && b != 0) ? 1 : 0;
            return 0;
        case CC_BIN_LOR:
            *out = (a != 0 || b != 0) ? 1 : 0;
            return 0;
        case CC_BIN_COMMA:
            *out = b;
            return 0;
        default:
            return -1;
        }

    case CC_EXPR_CAST:
        if (e->aux_type == CC_TYPE_VOID) {
            return -1;
        }
        return eval_const_i64_expr(tu, e->lhs, out);

    case CC_EXPR_SIZEOF:
        if (e->lhs != NULL) {
            *out = type_size_bytes_with_struct(tu, e->lhs->value_type, e->lhs->struct_id);
        } else {
            *out = type_size_bytes_with_struct(tu, e->aux_type, e->aux_struct_id);
        }
        return *out < 0 ? -1 : 0;

    case CC_EXPR_TERNARY:
        if (eval_const_i64_expr(tu, e->lhs, &a) != 0) {
            return -1;
        }
        if (a != 0) {
            if (e->rhs == NULL) {
                *out = a;
                return 0;
            }
            return eval_const_i64_expr(tu, e->rhs, out);
        }
        return eval_const_i64_expr(tu, e->third, out);

    case CC_EXPR_CALL:
        if (e->ident != NULL && strcmp(e->ident, "__builtin_constant_p") == 0 && e->arg_count == 1 &&
            e->args[0] != NULL) {
            if (e->args[0]->kind == CC_EXPR_FLOAT || e->args[0]->kind == CC_EXPR_STR ||
                eval_const_i64_expr(tu, e->args[0], &a) == 0) {
                *out = 1;
            } else {
                *out = 0;
            }
            return 0;
        }
        return -1;

    default:
        return -1;
    }
}

static int emit_i64_binop_instr(cc_ssa_function_t *sf, cc_ssa_opcode_t op, int lhs, int rhs, int is_unsigned,
                                cc_diag_t *diag) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.is_unsigned = is_unsigned ? 1 : 0;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = lhs;
    in.rhs = rhs;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory lowering integer builtin");
        return -1;
    }
    return in.dst;
}

static int emit_const_u64_instr(cc_ssa_function_t *sf, unsigned long long v, cc_diag_t *diag) {
    unsigned long lo_u = (unsigned long)(v & 0xffffffffULL);
    unsigned long hi_u = (unsigned long)((v >> 32) & 0xffffffffULL);
    int lo;
    int hi;
    int c32;

    lo = emit_const_i64_instr(sf, (long)lo_u);
    if (lo < 0) {
        return -1;
    }
    if (hi_u == 0) {
        return lo;
    }
    hi = emit_const_i64_instr(sf, (long)hi_u);
    c32 = emit_const_i64_instr(sf, 32);
    if (hi < 0 || c32 < 0) {
        return -1;
    }
    hi = emit_i64_binop_instr(sf, CC_SSA_SHL, hi, c32, 0, diag);
    if (hi < 0) {
        return -1;
    }
    return emit_i64_binop_instr(sf, CC_SSA_OR, hi, lo, 0, diag);
}

static int builtin_bitop_width(const cc_expr_t *e) {
    int n;

    if (e == NULL || e->ident == NULL) {
        return 32;
    }
    n = (int)strlen(e->ident);
    if (n >= 2 && e->ident[n - 2] == 'l' && e->ident[n - 1] == 'l') {
        return 64;
    }
    if (n >= 1 && e->ident[n - 1] == 'l') {
        int bits = g_pointer_size_bytes * 8;
        if (bits > 0) {
            return bits > 64 ? 64 : bits;
        }
        return 64;
    }
    return 32;
}

static int lower_popcount64(cc_ssa_function_t *sf, int v, cc_diag_t *diag) {
    int m1;
    int m2;
    int m4;
    int h01;
    int c1;
    int c2;
    int c4;
    int c56;
    int t;

    m1 = emit_const_u64_instr(sf, 0x5555555555555555ULL, diag);
    m2 = emit_const_u64_instr(sf, 0x3333333333333333ULL, diag);
    m4 = emit_const_u64_instr(sf, 0x0f0f0f0f0f0f0f0fULL, diag);
    h01 = emit_const_u64_instr(sf, 0x0101010101010101ULL, diag);
    c1 = emit_const_i64_instr(sf, 1);
    c2 = emit_const_i64_instr(sf, 2);
    c4 = emit_const_i64_instr(sf, 4);
    c56 = emit_const_i64_instr(sf, 56);
    if (m1 < 0 || m2 < 0 || m4 < 0 || h01 < 0 || c1 < 0 || c2 < 0 || c4 < 0 || c56 < 0) {
        return -1;
    }

    t = emit_i64_binop_instr(sf, CC_SSA_SHR, v, c1, 1, diag);
    if (t < 0) {
        return -1;
    }
    t = emit_i64_binop_instr(sf, CC_SSA_AND, t, m1, 0, diag);
    if (t < 0) {
        return -1;
    }
    v = emit_i64_binop_instr(sf, CC_SSA_SUB, v, t, 0, diag);
    if (v < 0) {
        return -1;
    }

    t = emit_i64_binop_instr(sf, CC_SSA_SHR, v, c2, 1, diag);
    if (t < 0) {
        return -1;
    }
    t = emit_i64_binop_instr(sf, CC_SSA_AND, t, m2, 0, diag);
    if (t < 0) {
        return -1;
    }
    v = emit_i64_binop_instr(sf, CC_SSA_AND, v, m2, 0, diag);
    if (v < 0) {
        return -1;
    }
    v = emit_i64_binop_instr(sf, CC_SSA_ADD, v, t, 0, diag);
    if (v < 0) {
        return -1;
    }

    t = emit_i64_binop_instr(sf, CC_SSA_SHR, v, c4, 1, diag);
    if (t < 0) {
        return -1;
    }
    v = emit_i64_binop_instr(sf, CC_SSA_ADD, v, t, 0, diag);
    if (v < 0) {
        return -1;
    }
    v = emit_i64_binop_instr(sf, CC_SSA_AND, v, m4, 0, diag);
    if (v < 0) {
        return -1;
    }
    v = emit_i64_binop_instr(sf, CC_SSA_MUL, v, h01, 0, diag);
    if (v < 0) {
        return -1;
    }
    return emit_i64_binop_instr(sf, CC_SSA_SHR, v, c56, 1, diag);
}

static int lower_builtin_ctz(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                             var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    int bits;
    int x;
    int zero;
    int low;
    int v;
    int one;

    bits = builtin_bitop_width(e);
    if (bits <= 0 || bits > 64) {
        bits = 64;
    }

    x = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
    if (x < 0) {
        return -1;
    }
    x = cast_value(sf, x, CC_VAL_I64, diag);
    if (x < 0) {
        return -1;
    }
    x = normalize_integral_value(sf, x, e->args[0]->value_type, diag);
    if (x < 0) {
        return -1;
    }

    if (bits < 64) {
        unsigned long long mask = (1ULL << bits) - 1ULL;
        int cmask = emit_const_u64_instr(sf, mask, diag);
        if (cmask < 0) {
            return -1;
        }
        x = emit_i64_binop_instr(sf, CC_SSA_AND, x, cmask, 0, diag);
        if (x < 0) {
            return -1;
        }
    }

    zero = emit_const_i64_instr(sf, 0);
    one = emit_const_i64_instr(sf, 1);
    if (zero < 0 || one < 0) {
        return -1;
    }

    v = emit_i64_binop_instr(sf, CC_SSA_SUB, zero, x, 0, diag);
    if (v < 0) {
        return -1;
    }
    low = emit_i64_binop_instr(sf, CC_SSA_AND, x, v, 0, diag);
    if (low < 0) {
        return -1;
    }
    v = emit_i64_binop_instr(sf, CC_SSA_SUB, low, one, 0, diag);
    if (v < 0) {
        return -1;
    }

    if (bits < 64) {
        unsigned long long mask = (1ULL << bits) - 1ULL;
        int cmask = emit_const_u64_instr(sf, mask, diag);
        if (cmask < 0) {
            return -1;
        }
        v = emit_i64_binop_instr(sf, CC_SSA_AND, v, cmask, 0, diag);
        if (v < 0) {
            return -1;
        }
    }

    return lower_popcount64(sf, v, diag);
}

static int lower_builtin_clz(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                             var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    int bits;
    int x;
    int pc;
    int c1;
    int c2;
    int c4;
    int c8;
    int c16;
    int c32;
    int cbits;
    int t;

    bits = builtin_bitop_width(e);
    if (bits <= 0 || bits > 64) {
        bits = 64;
    }

    x = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
    if (x < 0) {
        return -1;
    }
    x = cast_value(sf, x, CC_VAL_I64, diag);
    if (x < 0) {
        return -1;
    }
    x = normalize_integral_value(sf, x, e->args[0]->value_type, diag);
    if (x < 0) {
        return -1;
    }

    if (bits < 64) {
        unsigned long long mask = (1ULL << bits) - 1ULL;
        int cmask = emit_const_u64_instr(sf, mask, diag);
        if (cmask < 0) {
            return -1;
        }
        x = emit_i64_binop_instr(sf, CC_SSA_AND, x, cmask, 0, diag);
        if (x < 0) {
            return -1;
        }
    }

    c1 = emit_const_i64_instr(sf, 1);
    c2 = emit_const_i64_instr(sf, 2);
    c4 = emit_const_i64_instr(sf, 4);
    c8 = emit_const_i64_instr(sf, 8);
    c16 = emit_const_i64_instr(sf, 16);
    c32 = emit_const_i64_instr(sf, 32);
    cbits = emit_const_i64_instr(sf, bits);
    if (c1 < 0 || c2 < 0 || c4 < 0 || c8 < 0 || c16 < 0 || c32 < 0 || cbits < 0) {
        return -1;
    }

    t = emit_i64_binop_instr(sf, CC_SSA_SHR, x, c1, 1, diag);
    if (t < 0) {
        return -1;
    }
    x = emit_i64_binop_instr(sf, CC_SSA_OR, x, t, 0, diag);
    if (x < 0) {
        return -1;
    }
    t = emit_i64_binop_instr(sf, CC_SSA_SHR, x, c2, 1, diag);
    if (t < 0) {
        return -1;
    }
    x = emit_i64_binop_instr(sf, CC_SSA_OR, x, t, 0, diag);
    if (x < 0) {
        return -1;
    }
    t = emit_i64_binop_instr(sf, CC_SSA_SHR, x, c4, 1, diag);
    if (t < 0) {
        return -1;
    }
    x = emit_i64_binop_instr(sf, CC_SSA_OR, x, t, 0, diag);
    if (x < 0) {
        return -1;
    }
    t = emit_i64_binop_instr(sf, CC_SSA_SHR, x, c8, 1, diag);
    if (t < 0) {
        return -1;
    }
    x = emit_i64_binop_instr(sf, CC_SSA_OR, x, t, 0, diag);
    if (x < 0) {
        return -1;
    }
    t = emit_i64_binop_instr(sf, CC_SSA_SHR, x, c16, 1, diag);
    if (t < 0) {
        return -1;
    }
    x = emit_i64_binop_instr(sf, CC_SSA_OR, x, t, 0, diag);
    if (x < 0) {
        return -1;
    }
    if (bits > 32) {
        t = emit_i64_binop_instr(sf, CC_SSA_SHR, x, c32, 1, diag);
        if (t < 0) {
            return -1;
        }
        x = emit_i64_binop_instr(sf, CC_SSA_OR, x, t, 0, diag);
        if (x < 0) {
            return -1;
        }
    }

    pc = lower_popcount64(sf, x, diag);
    if (pc < 0) {
        return -1;
    }
    return emit_i64_binop_instr(sf, CC_SSA_SUB, cbits, pc, 0, diag);
}

static int emit_global_addr(cc_ssa_function_t *sf, const char *name, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_GADDR;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = -1;
    in.rhs = -1;
    in.param_index = -1;
    in.sym = xstrdup(name);
    if (in.sym == NULL || in.dst < 0 || push_instr(sf, in) != 0) {
        free(in.sym);
        set_diag(diag, "out of memory emitting global address");
        return -1;
    }
    return in.dst;
}

static int lower_struct_init_to_ptr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                    var_entry_t *vars, size_t var_count, int depth, int base_ptr, int struct_id,
                                    const cc_expr_t *init, cc_diag_t *diag);
static const cc_expr_t *unwrap_scalar_initializer_expr(const cc_expr_t *e, cc_diag_t *diag);

static int lower_member_addr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                             var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int base_ptr;
    int offv;

    (void)tu;
    if (e == NULL || e->kind != CC_EXPR_MEMBER || e->lhs == NULL) {
        set_diag(diag, "malformed member expression in lowering");
        return -1;
    }

    if (e->member_is_arrow) {
        base_ptr = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
    } else if (e->lhs->kind == CC_EXPR_DEREF && e->lhs->lhs != NULL) {
        base_ptr = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs->lhs, diag);
    } else if (e->lhs->value_type == CC_TYPE_VOID && e->lhs->struct_id >= 0) {
        base_ptr = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
    } else if (e->lhs->kind == CC_EXPR_MEMBER) {
        base_ptr = lower_member_addr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
    } else if (e->lhs->kind == CC_EXPR_IDENT && e->lhs->ident != NULL) {
        int idx = var_find_visible(vars, var_count, e->lhs->ident, depth);
        if (idx >= 0) {
            if (vars[idx].type == CC_TYPE_VOID && vars[idx].struct_id >= 0) {
                base_ptr = vars[idx].value;
            } else {
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_ADDR;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = vars[idx].value;
                in.rhs = -1;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory emitting struct base address");
                    return -1;
                }
                base_ptr = in.dst;
            }
        } else {
            const cc_global_t *g = find_global(tu, e->lhs->ident);
            if (g == NULL) {
                set_diag(diag, "dot-member base identifier not found in lowering");
                return -1;
            }
            base_ptr = emit_global_addr(sf, g->name, diag);
        }
    } else {
        set_diag(diag, "dot-member lowering currently requires pointer-backed base");
        return -1;
    }
    if (base_ptr < 0) {
        return -1;
    }
    base_ptr = cast_value(sf, base_ptr, CC_VAL_I64, diag);
    if (base_ptr < 0) {
        return -1;
    }

    if (e->member_offset == 0) {
        return base_ptr;
    }
    offv = emit_const_i64_instr(sf, e->member_offset);
    if (offv < 0) {
        return -1;
    }
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_ADD;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = base_ptr;
    in.rhs = offv;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory computing member address");
        return -1;
    }
    return in.dst;
}

static int lower_expr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                      var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int lhs;
    int rhs;
    size_t i;

    if (e == NULL) {
        set_diag(diag, "null expression during AST->SSA lowering");
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.param_index = -1;
    in.label = -1;
    in.true_label = -1;
    in.false_label = -1;

    switch (e->kind) {
    case CC_EXPR_GENERIC: {
        const cc_expr_t *sel = selected_generic_expr(e);
        if (sel == NULL) {
            set_diag(diag, "unresolved _Generic expression in lowering");
            return -1;
        }
        return lower_expr(tu, sf, ctx, vars, var_count, depth, sel, diag);
    }
    case CC_EXPR_INT:
        in.op = CC_SSA_CONST;
        in.dst = new_value(sf, CC_VAL_I64);
        in.imm = e->int_val;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;

    case CC_EXPR_FLOAT:
        in.op = CC_SSA_CONST;
        in.dst = new_value(sf, CC_VAL_F64);
        in.fimm = e->float_val;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;

    case CC_EXPR_STR:
        in.op = CC_SSA_STR;
        in.dst = new_value(sf, CC_VAL_I64);
        in.sym = xstrdup(e->ident != NULL ? e->ident : "\"\"");
        if (in.sym == NULL) {
            return -1;
        }
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            free(in.sym);
            return -1;
        }
        return in.dst;

    case CC_EXPR_IDENT: {
        int idx = var_find_visible(vars, var_count, e->ident, depth);
        if (idx < 0) {
            const cc_global_t *g = find_global(tu, e->ident);
            const cc_function_t *fn;
            int gaddr;
            long mem_size;
            if (e->ident != NULL && strcmp(e->ident, "__func__") == 0) {
                char *lit = quote_c_string((ctx != NULL && ctx->fn != NULL && ctx->fn->name != NULL) ? ctx->fn->name : "");
                in.op = CC_SSA_STR;
                in.dst = new_value(sf, CC_VAL_I64);
                in.sym = lit;
                if (in.sym == NULL) {
                    return -1;
                }
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    free(in.sym);
                    return -1;
                }
                return in.dst;
            }
                if (g != NULL) {
                    if ((g->array_ndim > 0 && g->array_len >= 0) ||
                        is_array_object_decl(g->type, g->array_len, g->array_ndim)) {
                        return emit_global_addr(sf, g->name, diag);
                    }
                    if (g->type == CC_TYPE_VOID && g->type_struct_id >= 0) {
                        return emit_global_addr(sf, g->name, diag);
                    }
                    mem_size = type_size_bytes_with_struct(tu, g->type, g->type_struct_id);
                    if (mem_size <= 0) {
                        set_diag(diag, "unsupported global object type in lowering");
                        return -1;
                    }
                gaddr = emit_global_addr(sf, g->name, diag);
                if (gaddr < 0) {
                    return -1;
                }
                in.op = CC_SSA_LOAD;
                in.dst = new_value(sf, type_to_val(g->type));
                in.lhs = gaddr;
                in.rhs = -1;
                in.imm = mem_size;
                in.is_unsigned = is_unsigned_load_type(g->type) ? 1 : 0;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    return -1;
                }
                return in.dst;
            }
            fn = find_fn(tu, e->ident);
            if (fn != NULL) {
                return emit_global_addr(sf, fn->name, diag);
            }
            return emit_global_addr(sf, e->ident, diag);
        }
        if (vars[idx].is_static_storage) {
            int saddr;
            long mem_size;
            if (vars[idx].static_sym == NULL) {
                set_diag(diag, "malformed static local symbol in lowering");
                return -1;
            }
            if ((vars[idx].array_ndim > 0 && vars[idx].array_len >= 0) ||
                is_array_object_decl(vars[idx].type, vars[idx].array_len, vars[idx].array_ndim)) {
                return emit_global_addr(sf, vars[idx].static_sym, diag);
            }
            if (vars[idx].type == CC_TYPE_VOID && vars[idx].struct_id >= 0) {
                return emit_global_addr(sf, vars[idx].static_sym, diag);
            }
            mem_size = type_size_bytes_with_struct(tu, vars[idx].type, vars[idx].struct_id);
            if (mem_size <= 0) {
                set_diag(diag, "unsupported static local object type in lowering");
                return -1;
            }
            saddr = emit_global_addr(sf, vars[idx].static_sym, diag);
            if (saddr < 0) {
                return -1;
            }
            in.op = CC_SSA_LOAD;
            in.dst = new_value(sf, type_to_val(vars[idx].type));
            in.lhs = saddr;
            in.rhs = -1;
            in.imm = mem_size;
            in.is_unsigned = is_unsigned_load_type(vars[idx].type) ? 1 : 0;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                return -1;
            }
            return in.dst;
        }
        return vars[idx].value;
    }

    case CC_EXPR_ADDR: {
        int idx;
        if (e->lhs == NULL) {
            set_diag(diag, "malformed address-of expression in lowering");
            return -1;
        }
        if (e->lhs->kind == CC_EXPR_CAST && e->lhs->value_type == CC_TYPE_VOID && e->lhs->struct_id >= 0) {
            return lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        }
        if (e->lhs->kind == CC_EXPR_DEREF && e->lhs->lhs != NULL) {
            return lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs->lhs, diag);
        }
        if (e->lhs->kind == CC_EXPR_MEMBER) {
            return lower_member_addr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        }
        if (e->lhs->kind != CC_EXPR_IDENT || e->lhs->ident == NULL) {
            set_diag(diag, "address-of lowering currently requires identifier or member operand");
            return -1;
        }
        idx = var_find_visible(vars, var_count, e->lhs->ident, depth);
        if (idx < 0) {
            const cc_global_t *g = find_global(tu, e->lhs->ident);
            if (g == NULL) {
                const cc_function_t *fn = find_fn(tu, e->lhs->ident);
                if (fn != NULL) {
                    return emit_global_addr(sf, fn->name, diag);
                }
                return emit_global_addr(sf, e->lhs->ident, diag);
            }
            return emit_global_addr(sf, g->name, diag);
        }
        if (vars[idx].is_static_storage) {
            if (vars[idx].static_sym == NULL) {
                set_diag(diag, "malformed static local symbol in lowering");
                return -1;
            }
            return emit_global_addr(sf, vars[idx].static_sym, diag);
        }
        if (vars[idx].type == CC_TYPE_VOID && vars[idx].struct_id >= 0) {
            return cast_value(sf, vars[idx].value, CC_VAL_I64, diag);
        }
        in.op = CC_SSA_ADDR;
        in.dst = new_value(sf, CC_VAL_I64);
        in.lhs = vars[idx].value;
        in.rhs = -1;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;
    }

    case CC_EXPR_LABEL_ADDR: {
        int l;
        if (e->ident == NULL || e->ident[0] == '\0') {
            set_diag(diag, "malformed label-address expression in lowering");
            return -1;
        }
        l = lower_find_label(ctx, e->ident);
        if (l < 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "label address references unknown label: %s", e->ident);
            }
            return -1;
        }
        memset(&in, 0, sizeof(in));
        in.op = CC_SSA_LADDR;
        in.dst = new_value(sf, CC_VAL_I64);
        in.label = l;
        in.lhs = -1;
        in.rhs = -1;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            set_diag(diag, "out of memory emitting label address");
            return -1;
        }
        return in.dst;
    }

    case CC_EXPR_DEREF:
    {
        if (e->value_type == CC_TYPE_VOID && e->struct_id >= 0) {
            lhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
            if (lhs < 0) {
                return -1;
            }
            return cast_value(sf, lhs, CC_VAL_I64, diag);
        }
        if (is_pointer_type(e->value_type) && e->array_ndim > 0 &&
            (expr_is_array_pointer_chain(tu, vars, var_count, depth, e->lhs) ||
             (e->lhs != NULL && e->lhs->array_ndim > 0))) {
            lhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
            if (lhs < 0) {
                return -1;
            }
            return cast_value(sf, lhs, CC_VAL_I64, diag);
        }
        long mem_size = type_size_bytes(e->value_type);
        if (mem_size <= 0) {
            set_diag(diag, "unsupported dereference type size in lowering");
            return -1;
        }
        lhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        if (lhs < 0) {
            return -1;
        }
        lhs = cast_value(sf, lhs, CC_VAL_I64, diag);
        if (lhs < 0) {
            return -1;
        }
        in.op = CC_SSA_LOAD;
        in.dst = new_value(sf, type_to_val(e->value_type));
        in.lhs = lhs;
        in.rhs = -1;
        in.imm = mem_size;
        in.is_unsigned = is_unsigned_load_type(e->value_type) ? 1 : 0;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;
    }

    case CC_EXPR_MEMBER: {
        if (e->lhs == NULL && e->rhs != NULL && e->ident != NULL) {
            return lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
        }
        if (e->value_type == CC_TYPE_VOID && e->struct_id >= 0) {
            return lower_member_addr(tu, sf, ctx, vars, var_count, depth, e, diag);
        }
        long mem_size = type_size_bytes(e->value_type);
        int ptrv;
        if (expr_is_array_object_ref(tu, vars, var_count, depth, e)) {
            return lower_member_addr(tu, sf, ctx, vars, var_count, depth, e, diag);
        }
        if (mem_size <= 0) {
            set_diag(diag, "unsupported member type size in lowering");
            return -1;
        }
        ptrv = lower_member_addr(tu, sf, ctx, vars, var_count, depth, e, diag);
        if (ptrv < 0) {
            return -1;
        }
        in.op = CC_SSA_LOAD;
        in.dst = new_value(sf, type_to_val(e->value_type));
        in.lhs = ptrv;
        in.rhs = -1;
        in.imm = mem_size;
        in.is_unsigned = is_unsigned_load_type(e->value_type) ? 1 : 0;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;
    }

    case CC_EXPR_BIN: {
        cc_value_type_t vt;

        if (is_logical_op(e->op)) {
            int dst;
            int zero;
            int one;
            int rb;
            int l_rhs = new_label(sf);
            int l_end = new_label(sf);

            lhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
            if (lhs < 0) {
                return -1;
            }
            lhs = lower_truthy_value(sf, lhs, diag);
            if (lhs < 0) {
                return -1;
            }

            dst = new_value(sf, CC_VAL_I64);
            if (dst < 0) {
                set_diag(diag, "out of memory assigning logical temporary");
                return -1;
            }
            zero = emit_const_i64_instr(sf, 0);
            if (zero < 0 || emit_mov_instr(sf, dst, zero) != 0) {
                return -1;
            }

            if (e->op == CC_BIN_LAND) {
                if (emit_br_cond_instr(sf, lhs, l_rhs, l_end) != 0) {
                    return -1;
                }
                if (emit_label_instr(sf, l_rhs) != 0) {
                    return -1;
                }
                rhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
                if (rhs < 0) {
                    return -1;
                }
                rb = lower_truthy_value(sf, rhs, diag);
                if (rb < 0) {
                    return -1;
                }
                if (emit_mov_instr(sf, dst, rb) != 0) {
                    return -1;
                }
                if (emit_br_instr(sf, l_end) != 0) {
                    return -1;
                }
                if (emit_label_instr(sf, l_end) != 0) {
                    return -1;
                }
                return dst;
            }

            {
                int l_true = new_label(sf);

                if (emit_br_cond_instr(sf, lhs, l_true, l_rhs) != 0) {
                    return -1;
                }
                if (emit_label_instr(sf, l_true) != 0) {
                    return -1;
                }
            }
            one = emit_const_i64_instr(sf, 1);
            if (one < 0 || emit_mov_instr(sf, dst, one) != 0) {
                return -1;
            }
            if (emit_br_instr(sf, l_end) != 0) {
                return -1;
            }
            if (emit_label_instr(sf, l_rhs) != 0) {
                return -1;
            }
            rhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
            if (rhs < 0) {
                return -1;
            }
            rb = lower_truthy_value(sf, rhs, diag);
            if (rb < 0) {
                return -1;
            }
            if (emit_mov_instr(sf, dst, rb) != 0) {
                return -1;
            }
            if (emit_br_instr(sf, l_end) != 0) {
                return -1;
            }
            if (emit_label_instr(sf, l_end) != 0) {
                return -1;
            }
            return dst;
        }

        lhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        if (lhs < 0) {
            return -1;
        }
        rhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
        if (rhs < 0) {
            return -1;
        }

        if (e->op == CC_BIN_COMMA) {
            return rhs;
        }

        if (is_cmp_op(e->op)) {
            cc_value_type_t cmp_vt = (value_type(sf, lhs) == CC_VAL_F64 || value_type(sf, rhs) == CC_VAL_F64)
                                         ? CC_VAL_F64
                                         : CC_VAL_I64;
            cc_type_t cmp_it = CC_TYPE_INT;
            lhs = cast_value(sf, lhs, cmp_vt, diag);
            rhs = cast_value(sf, rhs, cmp_vt, diag);
            if (lhs < 0 || rhs < 0) {
                return -1;
            }
            if (cmp_vt == CC_VAL_I64) {
                if (is_pointer_type(e->lhs->value_type) || is_pointer_type(e->rhs->value_type)) {
                    cmp_it = g_pointer_size_bytes <= 4 ? CC_TYPE_UINT : CC_TYPE_ULONG_LONG;
                } else {
                    cmp_it = common_integral_type(e->lhs->value_type, e->rhs->value_type);
                }
                if (is_integral_type(cmp_it)) {
                    lhs = normalize_integral_value(sf, lhs, cmp_it, diag);
                    rhs = normalize_integral_value(sf, rhs, cmp_it, diag);
                    if (lhs < 0 || rhs < 0) {
                        return -1;
                    }
                }
            }
            in.op = CC_SSA_CMP;
            in.cmp_kind = bin_to_cmp(e->op);
            in.is_unsigned =
                (cmp_vt == CC_VAL_I64 &&
                 (is_unsigned_integral_type(cmp_it) || is_pointer_type(e->lhs->value_type) ||
                  is_pointer_type(e->rhs->value_type)))
                    ? 1
                    : 0;
            in.dst = new_value(sf, CC_VAL_I64);
            in.lhs = lhs;
            in.rhs = rhs;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                return -1;
            }
            return in.dst;
        }

        if (e->op == CC_BIN_SUB && is_pointer_type(e->lhs->value_type) && is_pointer_type(e->rhs->value_type)) {
            long elem_size = pointer_elem_size_bytes(tu, e->lhs->value_type, e->lhs->struct_id);
            int diffv;
            long lhs_step = expr_array_step_size_bytes(tu, e->lhs, -1);
            long rhs_step = expr_array_step_size_bytes(tu, e->rhs, -1);

            if (lhs_step > 0) {
                elem_size = lhs_step;
            } else if (rhs_step > 0) {
                elem_size = rhs_step;
            }

            lhs = cast_value(sf, lhs, CC_VAL_I64, diag);
            rhs = cast_value(sf, rhs, CC_VAL_I64, diag);
            if (lhs < 0 || rhs < 0) {
                return -1;
            }
            if (elem_size <= 0) {
                set_diag(diag, "unsupported pointer base type in subtraction lowering");
                return -1;
            }

            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_SUB;
            in.dst = new_value(sf, CC_VAL_I64);
            in.lhs = lhs;
            in.rhs = rhs;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                return -1;
            }
            diffv = in.dst;

            if (elem_size != 1) {
                int csz = emit_const_i64_instr(sf, elem_size);
                if (csz < 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_DIV;
                in.is_unsigned = 0;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = diffv;
                in.rhs = csz;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    return -1;
                }
                diffv = in.dst;
            }

            return cast_value(sf, diffv, type_to_val(e->value_type), diag);
        }

        if ((e->op == CC_BIN_ADD || e->op == CC_BIN_SUB) && is_pointer_type(e->value_type)) {
            int ptrv = -1;
            int idxv = -1;
            long elem_size;
            cc_ssa_instr_t mul;
            const cc_expr_t *ptr_expr = NULL;

            if (is_pointer_type(e->lhs->value_type)) {
                ptrv = lhs;
                idxv = rhs;
                ptr_expr = e->lhs;
            } else if (e->op == CC_BIN_ADD && is_pointer_type(e->rhs->value_type)) {
                ptrv = rhs;
                idxv = lhs;
                ptr_expr = e->rhs;
            } else {
                set_diag(diag, "unsupported pointer arithmetic form in lowering");
                return -1;
            }

            /*
             * Array indexing canonicalization can surface as pointer arithmetic
             * over a dereferenced array base. Use the underlying base address,
             * not the loaded first element value.
             */
            if (ptr_expr != NULL && ptr_expr->kind == CC_EXPR_DEREF && ptr_expr->lhs != NULL &&
                is_pointer_type(ptr_expr->value_type) &&
                expr_is_array_pointer_chain(tu, vars, var_count, depth, ptr_expr->lhs)) {
                int basev = lower_expr(tu, sf, ctx, vars, var_count, depth, ptr_expr->lhs, diag);
                if (basev < 0) {
                    return -1;
                }
                ptrv = basev;
            }

            ptrv = cast_value(sf, ptrv, CC_VAL_I64, diag);
            idxv = cast_value(sf, idxv, CC_VAL_I64, diag);
            if (ptrv < 0 || idxv < 0) {
                return -1;
            }

            elem_size = pointer_elem_size_bytes(tu, e->value_type, e->struct_id);
            elem_size = expr_array_step_size_bytes(tu, ptr_expr, elem_size);
            if (elem_size <= 0) {
                set_diag(diag, "unsupported pointer base type in lowering");
                return -1;
            }
            if (elem_size != 1) {
                int csz = emit_const_i64_instr(sf, elem_size);
                if (csz < 0) {
                    return -1;
                }
                memset(&mul, 0, sizeof(mul));
                mul.op = CC_SSA_MUL;
                mul.dst = new_value(sf, CC_VAL_I64);
                mul.lhs = idxv;
                mul.rhs = csz;
                if (mul.dst < 0 || push_instr(sf, mul) != 0) {
                    return -1;
                }
                idxv = mul.dst;
            }

            memset(&in, 0, sizeof(in));
            in.op = (e->op == CC_BIN_SUB) ? CC_SSA_SUB : CC_SSA_ADD;
            in.dst = new_value(sf, CC_VAL_I64);
            in.lhs = ptrv;
            in.rhs = idxv;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                return -1;
            }
            return in.dst;
        }

        vt = type_to_val(e->value_type);
        lhs = cast_value(sf, lhs, vt, diag);
        rhs = cast_value(sf, rhs, vt, diag);
        if (lhs < 0 || rhs < 0) {
            return -1;
        }
        if (vt == CC_VAL_I64 && is_integral_type(e->value_type) && !is_pointer_type(e->value_type)) {
            lhs = normalize_integral_value(sf, lhs, e->value_type, diag);
            rhs = normalize_integral_value(sf, rhs, e->value_type, diag);
            if (lhs < 0 || rhs < 0) {
                return -1;
            }
        }

        switch (e->op) {
        case CC_BIN_ADD:
            in.op = CC_SSA_ADD;
            break;
        case CC_BIN_SUB:
            in.op = CC_SSA_SUB;
            break;
        case CC_BIN_MUL:
            in.op = CC_SSA_MUL;
            break;
        case CC_BIN_DIV:
            in.op = CC_SSA_DIV;
            in.is_unsigned = is_unsigned_integral_type(e->value_type) ? 1 : 0;
            break;
        case CC_BIN_BAND:
            lhs = cast_value(sf, lhs, CC_VAL_I64, diag);
            rhs = cast_value(sf, rhs, CC_VAL_I64, diag);
            if (lhs < 0 || rhs < 0) {
                return -1;
            }
            in.op = CC_SSA_AND;
            vt = CC_VAL_I64;
            break;
        case CC_BIN_BOR:
            lhs = cast_value(sf, lhs, CC_VAL_I64, diag);
            rhs = cast_value(sf, rhs, CC_VAL_I64, diag);
            if (lhs < 0 || rhs < 0) {
                return -1;
            }
            in.op = CC_SSA_OR;
            vt = CC_VAL_I64;
            break;
        case CC_BIN_BXOR:
            lhs = cast_value(sf, lhs, CC_VAL_I64, diag);
            rhs = cast_value(sf, rhs, CC_VAL_I64, diag);
            if (lhs < 0 || rhs < 0) {
                return -1;
            }
            in.op = CC_SSA_XOR;
            vt = CC_VAL_I64;
            break;
        case CC_BIN_SHL:
            lhs = cast_value(sf, lhs, CC_VAL_I64, diag);
            rhs = cast_value(sf, rhs, CC_VAL_I64, diag);
            if (lhs < 0 || rhs < 0) {
                return -1;
            }
            in.op = CC_SSA_SHL;
            vt = CC_VAL_I64;
            break;
        case CC_BIN_SHR:
            lhs = cast_value(sf, lhs, CC_VAL_I64, diag);
            rhs = cast_value(sf, rhs, CC_VAL_I64, diag);
            if (lhs < 0 || rhs < 0) {
                return -1;
            }
            in.op = CC_SSA_SHR;
            in.is_unsigned = is_unsigned_integral_type(e->value_type) ? 1 : 0;
            vt = CC_VAL_I64;
            break;
        case CC_BIN_MOD: {
            cc_ssa_instr_t q;
            cc_ssa_instr_t p;
            cc_ssa_instr_t r;
            int lhs_i = cast_value(sf, lhs, CC_VAL_I64, diag);
            int rhs_i = cast_value(sf, rhs, CC_VAL_I64, diag);
            if (lhs_i < 0 || rhs_i < 0) {
                return -1;
            }
            memset(&q, 0, sizeof(q));
            q.op = CC_SSA_DIV;
            q.is_unsigned = is_unsigned_integral_type(e->value_type) ? 1 : 0;
            q.dst = new_value(sf, CC_VAL_I64);
            q.lhs = lhs_i;
            q.rhs = rhs_i;
            if (q.dst < 0 || push_instr(sf, q) != 0) {
                return -1;
            }
            memset(&p, 0, sizeof(p));
            p.op = CC_SSA_MUL;
            p.dst = new_value(sf, CC_VAL_I64);
            p.lhs = q.dst;
            p.rhs = rhs_i;
            if (p.dst < 0 || push_instr(sf, p) != 0) {
                return -1;
            }
            memset(&r, 0, sizeof(r));
            r.op = CC_SSA_SUB;
            r.dst = new_value(sf, CC_VAL_I64);
            r.lhs = lhs_i;
            r.rhs = p.dst;
            if (r.dst < 0 || push_instr(sf, r) != 0) {
                return -1;
            }
            if (is_integral_type(e->value_type) && !is_pointer_type(e->value_type)) {
                return normalize_integral_value(sf, r.dst, e->value_type, diag);
            }
            return r.dst;
        }
        default:
            set_diag(diag, "unsupported binary operation");
            return -1;
        }
        in.dst = new_value(sf, vt);
        in.lhs = lhs;
        in.rhs = rhs;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        if (e->value_type == CC_TYPE_FLOAT) {
            return normalize_float_value(sf, in.dst, e->value_type, diag);
        }
        if (vt == CC_VAL_I64 && is_integral_type(e->value_type) && !is_pointer_type(e->value_type)) {
            return normalize_integral_value(sf, in.dst, e->value_type, diag);
        }
        return in.dst;
    }

    case CC_EXPR_CALL: {
        const cc_function_t *callee = NULL;
        builtin_kind_t bk = builtin_kind(e->ident);
        int bswap_bits = builtin_bswap_bits(e->ident);
        int indirect_callee = -1;
        if (e->ident != NULL) {
            callee = find_fn(tu, e->ident);
        }
        if (bk == BUILTIN_VA_START) {
            int dst_idx;
            int last_idx;
            if (e->arg_count != 2 || e->args[0] == NULL || e->args[1] == NULL || e->args[0]->kind != CC_EXPR_IDENT ||
                e->args[1]->kind != CC_EXPR_IDENT) {
                set_diag(diag, "__builtin_va_start lowering expects (identifier, parameter-identifier)");
                return -1;
            }
            dst_idx = var_find_visible(vars, var_count, e->args[0]->ident, depth);
            if (dst_idx < 0) {
                set_diag(diag, "__builtin_va_start destination must be a local va_list variable");
                return -1;
            }
            last_idx = function_param_index_by_name(ctx, e->args[1]->ident);
            if (last_idx < 0) {
                set_diag(diag, "__builtin_va_start second argument must name a fixed parameter");
                return -1;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_VA_START;
            in.dst = vars[dst_idx].value;
            in.imm = (long)(last_idx + 1);
            if (push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory appending va_start");
                return -1;
            }
            return vars[dst_idx].value;
        }
        if (bk == BUILTIN_VA_END) {
            return emit_const_i64_instr(sf, 0);
        }
        if (bk == BUILTIN_VA_COPY) {
            int dst_idx;
            int srcv;
            if (e->arg_count != 2 || e->args[0] == NULL || e->args[1] == NULL || e->args[0]->kind != CC_EXPR_IDENT) {
                set_diag(diag, "__builtin_va_copy lowering expects destination identifier");
                return -1;
            }
            dst_idx = var_find_visible(vars, var_count, e->args[0]->ident, depth);
            if (dst_idx < 0) {
                set_diag(diag, "__builtin_va_copy destination must be a local va_list variable");
                return -1;
            }
            srcv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag);
            if (srcv < 0) {
                return -1;
            }
            srcv = cast_value(sf, srcv, CC_VAL_I64, diag);
            if (srcv < 0) {
                return -1;
            }
            if (emit_mov_instr(sf, vars[dst_idx].value, srcv) != 0) {
                set_diag(diag, "out of memory appending va_copy move");
                return -1;
            }
            return vars[dst_idx].value;
        }
        if (bk == BUILTIN_VA_ARG) {
            int ap_idx;
            int cur_ap;
            int load_v;
            int step_v;
            int next_ap;
            long mem_size;
            long step;
            if (e->arg_count != 1 || e->args[0] == NULL || e->args[0]->kind != CC_EXPR_IDENT) {
                set_diag(diag, "__builtin_va_arg lowering expects va_list identifier");
                return -1;
            }
            ap_idx = var_find_visible(vars, var_count, e->args[0]->ident, depth);
            if (ap_idx < 0) {
                set_diag(diag, "__builtin_va_arg operand must be a local va_list variable");
                return -1;
            }
            mem_size = type_size_bytes_with_struct(tu, e->aux_type, e->aux_struct_id);
            if (mem_size <= 0) {
                set_diag(diag, "unsupported __builtin_va_arg type size");
                return -1;
            }

            if (g_pointer_size_bytes == 8) {
                int ap_ptr;
                int gp_off_ptr;
                int fp_off_ptr;
                int overflow_ptr_ptr;
                int regsave_ptr_ptr;
                int gp_off;
                int fp_off;
                int overflow_ptr;
                int regsave_ptr;
                int use_fp = (e->aux_type == CC_TYPE_DOUBLE || e->aux_type == CC_TYPE_FLOAT) ? 1 : 0;
                int off_ptr;
                int offv;
                int off_limit;
                int cmpv;
                int dstv;
                int addrv;
                int reg_incv;
                int stack_incv;
                int next_off;
                int next_overflow;
                int reg_loaded;
                int stack_loaded;
                int l_reg;
                int l_stack;
                int l_end;
                int ap_idx_const;
                int field_off_const;
                long load_size = mem_size;
                long stack_step;
                int is_agg = (e->aux_type == CC_TYPE_VOID && e->aux_struct_id >= 0) ? 1 : 0;
                cc_value_type_t dst_ty = is_agg ? CC_VAL_I64 : type_to_val(e->aux_type);

                ap_ptr = cast_value(sf, vars[ap_idx].value, CC_VAL_I64, diag);
                if (ap_ptr < 0) {
                    return -1;
                }

                ap_idx_const = emit_const_i64_instr(sf, 8);
                if (ap_idx_const < 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_ADD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = ap_ptr;
                in.rhs = ap_idx_const;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory computing va_list overflow_arg_area field pointer");
                    return -1;
                }
                overflow_ptr_ptr = in.dst;

                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_LOAD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = overflow_ptr_ptr;
                in.imm = 8;
                in.is_unsigned = 1;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory loading va_list overflow_arg_area");
                    return -1;
                }
                overflow_ptr = in.dst;

                regsave_ptr_ptr = emit_const_i64_instr(sf, 16);
                if (regsave_ptr_ptr < 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_ADD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = ap_ptr;
                in.rhs = regsave_ptr_ptr;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory computing va_list reg_save_area field pointer");
                    return -1;
                }
                regsave_ptr_ptr = in.dst;

                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_LOAD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = regsave_ptr_ptr;
                in.imm = 8;
                in.is_unsigned = 1;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory loading va_list reg_save_area");
                    return -1;
                }
                regsave_ptr = in.dst;

                field_off_const = emit_const_i64_instr(sf, use_fp ? 4 : 0);
                if (field_off_const < 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_ADD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = ap_ptr;
                in.rhs = field_off_const;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory computing va_list offset field pointer");
                    return -1;
                }
                if (use_fp) {
                    fp_off_ptr = in.dst;
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_LOAD;
                    in.dst = new_value(sf, CC_VAL_I64);
                    in.lhs = fp_off_ptr;
                    in.imm = 4;
                    in.is_unsigned = 1;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory loading va_list fp_offset");
                        return -1;
                    }
                    fp_off = in.dst;
                    off_ptr = fp_off_ptr;
                    offv = fp_off;
                    off_limit = emit_const_i64_instr(sf, 160);
                    reg_incv = emit_const_i64_instr(sf, 16);
                } else {
                    gp_off_ptr = in.dst;
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_LOAD;
                    in.dst = new_value(sf, CC_VAL_I64);
                    in.lhs = gp_off_ptr;
                    in.imm = 4;
                    in.is_unsigned = 1;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory loading va_list gp_offset");
                        return -1;
                    }
                    gp_off = in.dst;
                    off_ptr = gp_off_ptr;
                    offv = gp_off;
                    off_limit = emit_const_i64_instr(sf, 40);
                    reg_incv = emit_const_i64_instr(sf, 8);
                }
                if (off_limit < 0 || reg_incv < 0) {
                    return -1;
                }

                stack_step = use_fp ? 8 : align_up_long(mem_size, 8);
                stack_incv = emit_const_i64_instr(sf, stack_step);
                if (stack_incv < 0) {
                    return -1;
                }

                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_CMP;
                in.cmp_kind = CC_CMP_LE;
                in.is_unsigned = 1;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = offv;
                in.rhs = off_limit;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory appending va_arg register-availability compare");
                    return -1;
                }
                cmpv = in.dst;

                dstv = new_value(sf, dst_ty);
                addrv = new_value(sf, CC_VAL_I64);
                if (dstv < 0 || addrv < 0) {
                    set_diag(diag, "out of memory creating va_arg temporaries");
                    return -1;
                }
                l_reg = new_label(sf);
                l_stack = new_label(sf);
                l_end = new_label(sf);
                if (emit_br_cond_instr(sf, cmpv, l_reg, l_stack) != 0) {
                    return -1;
                }

                if (emit_label_instr(sf, l_reg) != 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_ADD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = regsave_ptr;
                in.rhs = offv;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory computing va_arg register-slot address");
                    return -1;
                }
                if (emit_mov_instr(sf, addrv, in.dst) != 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_ADD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = offv;
                in.rhs = reg_incv;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory incrementing va_arg register offset");
                    return -1;
                }
                next_off = in.dst;
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_STORE;
                in.lhs = off_ptr;
                in.rhs = next_off;
                in.imm = 4;
                if (push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory storing va_arg offset update");
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_LOAD;
                in.dst = new_value(sf, dst_ty);
                in.lhs = addrv;
                in.imm = is_agg ? g_pointer_size_bytes : load_size;
                in.is_unsigned = is_agg ? 1 : (is_unsigned_load_type(e->aux_type) ? 1 : 0);
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory loading va_arg register value");
                    return -1;
                }
                reg_loaded = in.dst;
                if (emit_mov_instr(sf, dstv, reg_loaded) != 0 || emit_br_instr(sf, l_end) != 0) {
                    return -1;
                }

                if (emit_label_instr(sf, l_stack) != 0) {
                    return -1;
                }
                if (emit_mov_instr(sf, addrv, overflow_ptr) != 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_ADD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = overflow_ptr;
                in.rhs = stack_incv;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory incrementing va_arg overflow pointer");
                    return -1;
                }
                next_overflow = in.dst;
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_STORE;
                in.lhs = overflow_ptr_ptr;
                in.rhs = next_overflow;
                in.imm = 8;
                if (push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory storing va_arg overflow pointer update");
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_LOAD;
                in.dst = new_value(sf, dst_ty);
                in.lhs = addrv;
                in.imm = is_agg ? g_pointer_size_bytes : load_size;
                in.is_unsigned = is_agg ? 1 : (is_unsigned_load_type(e->aux_type) ? 1 : 0);
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory loading va_arg stack value");
                    return -1;
                }
                stack_loaded = in.dst;
                if (emit_mov_instr(sf, dstv, stack_loaded) != 0 || emit_br_instr(sf, l_end) != 0) {
                    return -1;
                }

                if (emit_label_instr(sf, l_end) != 0) {
                    return -1;
                }
                return dstv;
            }

            cur_ap = cast_value(sf, vars[ap_idx].value, CC_VAL_I64, diag);
            if (cur_ap < 0) {
                return -1;
            }
            if (e->aux_type == CC_TYPE_VOID && e->aux_struct_id >= 0) {
                /*
                 * Aggregate varargs are passed as pointers in this SSA ABI.
                 * va_arg(T struct) therefore reads one pointer-sized slot and
                 * yields the pointed object address.
                 */
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_LOAD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = cur_ap;
                in.rhs = -1;
                in.imm = g_pointer_size_bytes;
                in.is_unsigned = 1;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory appending va_arg aggregate pointer load");
                    return -1;
                }
                load_v = in.dst;
                step = g_pointer_size_bytes;
            } else {
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_LOAD;
                in.dst = new_value(sf, type_to_val(e->aux_type));
                in.lhs = cur_ap;
                in.rhs = -1;
                in.imm = mem_size;
                in.is_unsigned = is_unsigned_load_type(e->aux_type) ? 1 : 0;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory appending va_arg load");
                    return -1;
                }
                load_v = in.dst;
                step = align_up_long(mem_size, g_pointer_size_bytes);
            }
            step_v = emit_const_i64_instr(sf, step);
            if (step_v < 0) {
                return -1;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_ADD;
            in.dst = new_value(sf, CC_VAL_I64);
            in.lhs = cur_ap;
            in.rhs = step_v;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory appending va_arg pointer step");
                return -1;
            }
            next_ap = in.dst;
            if (emit_mov_instr(sf, vars[ap_idx].value, next_ap) != 0) {
                set_diag(diag, "out of memory appending va_arg pointer update");
                return -1;
            }
            return load_v;
        }
        if (bk == BUILTIN_ALLOCA) {
            cc_ssa_instr_t call_in;
            int sizev;
            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, "__builtin_alloca lowering expects 1 argument");
                return -1;
            }
            sizev = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (sizev < 0) {
                return -1;
            }
            sizev = cast_value(sf, sizev, CC_VAL_I64, diag);
            if (sizev < 0) {
                return -1;
            }
            memset(&call_in, 0, sizeof(call_in));
            call_in.op = CC_SSA_CALL;
            call_in.call_is_variadic = 0;
            call_in.sym = xstrdup(is_freestanding_mode() ? "kmalloc" : "malloc");
            call_in.arg_count = 1;
            call_in.args = (int *)calloc(1, sizeof(*call_in.args));
            call_in.dst = new_value(sf, CC_VAL_I64);
            if (call_in.sym == NULL || call_in.args == NULL || call_in.dst < 0) {
                free(call_in.sym);
                free(call_in.args);
                set_diag(diag, "out of memory lowering __builtin_alloca");
                return -1;
            }
            call_in.args[0] = sizev;
            if (push_instr(sf, call_in) != 0) {
                free(call_in.sym);
                free(call_in.args);
                set_diag(diag, "out of memory emitting __builtin_alloca call");
                return -1;
            }
            return call_in.dst;
        }
        if (bk == BUILTIN_EXPECT) {
            int v0;
            if (e->arg_count != 2) {
                set_diag(diag, "__builtin_expect lowering expects 2 arguments");
                return -1;
            }
            v0 = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (v0 < 0) {
                return -1;
            }
            if (lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag) < 0) {
                return -1;
            }
            return v0;
        }
        if (bk == BUILTIN_CONSTANT_P) {
            int is_const = 0;
            long dummy = 0;
            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, "__builtin_constant_p lowering expects 1 argument");
                return -1;
            }
            if (e->args[0]->kind == CC_EXPR_FLOAT || e->args[0]->kind == CC_EXPR_STR ||
                eval_const_i64_expr(tu, e->args[0], &dummy) == 0) {
                is_const = 1;
            }
            return emit_const_i64_instr(sf, is_const);
        }
        if (bk == BUILTIN_TRAP) {
            if (emit_trap_instr(sf) != 0) {
                set_diag(diag, "out of memory emitting __builtin_trap");
                return -1;
            }
            return emit_const_i64_instr(sf, 0);
        }
        if (bk == BUILTIN_UNREACHABLE) {
            if (emit_trap_instr(sf) != 0) {
                set_diag(diag, "out of memory emitting __builtin_unreachable");
                return -1;
            }
            return emit_const_i64_instr(sf, 0);
        }
        if (bk == BUILTIN_ASSUME) {
            if (e->arg_count != 1) {
                set_diag(diag, "__builtin_assume lowering expects 1 argument");
                return -1;
            }
            if (lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag) < 0) {
                return -1;
            }
            return emit_const_i64_instr(sf, 0);
        }
        if (bk == BUILTIN_ASSUME_ALIGNED) {
            size_t ai;
            int p0;
            if (e->arg_count < 2 || e->arg_count > 3) {
                set_diag(diag, "__builtin_assume_aligned lowering expects 2 or 3 arguments");
                return -1;
            }
            p0 = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (p0 < 0) {
                return -1;
            }
            for (ai = 1; ai < e->arg_count; ++ai) {
                if (lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[ai], diag) < 0) {
                    return -1;
                }
            }
            return p0;
        }
        if (bk == BUILTIN_UNPREDICTABLE) {
            if (e->arg_count != 1) {
                set_diag(diag, "__builtin_unpredictable lowering expects 1 argument");
                return -1;
            }
            return lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
        }
        if (bk == BUILTIN_HUGE_VAL || bk == BUILTIN_HUGE_VALF || bk == BUILTIN_HUGE_VALL) {
            (void)e;
            return emit_const_f64_instr(sf, HUGE_VAL);
        }
        if (bk == BUILTIN_NANF || bk == BUILTIN_NAN || bk == BUILTIN_NANL) {
            return emit_const_f64_instr(sf, NAN);
        }
        if (bk == BUILTIN_POPCOUNT) {
            int av;
            int v;
            long mem_size;
            cc_ssa_instr_t bin;

            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, "__builtin_popcount lowering expects 1 argument");
                return -1;
            }
            av = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (av < 0) {
                return -1;
            }
            av = cast_value(sf, av, CC_VAL_I64, diag);
            if (av < 0) {
                return -1;
            }
            mem_size = type_size_bytes_with_struct(tu, e->args[0]->value_type, e->args[0]->struct_id);
            if (mem_size <= 0 || mem_size > 8) {
                mem_size = 8;
            }
            v = av;
            if (mem_size < 8) {
                unsigned long long mask_u = (1ULL << (mem_size * 8)) - 1ULL;
                int mask = emit_const_i64_instr(sf, (long)mask_u);
                if (mask < 0) {
                    return -1;
                }
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_AND;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = mask;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                v = bin.dst;
            }
            {
                int c5555 = emit_const_i64_instr(sf, (long)0x5555555555555555ULL);
                int c3333 = emit_const_i64_instr(sf, (long)0x3333333333333333ULL);
                int c0f0f = emit_const_i64_instr(sf, (long)0x0F0F0F0F0F0F0F0FULL);
                int c7f = emit_const_i64_instr(sf, 0x7F);
                int t1, t2;
                if (c5555 < 0 || c3333 < 0 || c0f0f < 0 || c7f < 0) {
                    return -1;
                }
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_SHR;
                bin.is_unsigned = 1;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = emit_const_i64_instr(sf, 1);
                if (bin.rhs < 0 || bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                t1 = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_AND;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = t1;
                bin.rhs = c5555;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                t1 = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_SUB;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = t1;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                v = bin.dst;

                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_AND;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = c3333;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                t1 = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_SHR;
                bin.is_unsigned = 1;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = emit_const_i64_instr(sf, 2);
                if (bin.rhs < 0 || bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                t2 = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_AND;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = t2;
                bin.rhs = c3333;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                t2 = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_ADD;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = t1;
                bin.rhs = t2;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                v = bin.dst;

                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_SHR;
                bin.is_unsigned = 1;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = emit_const_i64_instr(sf, 4);
                if (bin.rhs < 0 || bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                t1 = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_ADD;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = t1;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                v = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_AND;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = c0f0f;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                v = bin.dst;

                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_SHR;
                bin.is_unsigned = 1;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = emit_const_i64_instr(sf, 8);
                if (bin.rhs < 0 || bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                t1 = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_ADD;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = t1;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                v = bin.dst;

                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_SHR;
                bin.is_unsigned = 1;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = emit_const_i64_instr(sf, 16);
                if (bin.rhs < 0 || bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                t1 = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_ADD;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = t1;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                v = bin.dst;

                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_SHR;
                bin.is_unsigned = 1;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = emit_const_i64_instr(sf, 32);
                if (bin.rhs < 0 || bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                t1 = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_ADD;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = t1;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                v = bin.dst;

                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_AND;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = v;
                bin.rhs = c7f;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                v = bin.dst;
            }
            return v;
        }
        if (bk == BUILTIN_ADD_OVERFLOW || bk == BUILTIN_SUB_OVERFLOW || bk == BUILTIN_MUL_OVERFLOW ||
            bk == BUILTIN_ADD_OVERFLOW_P || bk == BUILTIN_SUB_OVERFLOW_P || bk == BUILTIN_MUL_OVERFLOW_P) {
            int av;
            int bv;
            int pv = -1;
            int rv;
            int ov = -1;
            int overflow_op;
            int has_store;
            cc_type_t out_type;
            long mem_size;
            cc_ssa_instr_t bin;

            if (e->arg_count != 3 || e->args[0] == NULL || e->args[1] == NULL || e->args[2] == NULL) {
                set_diag(diag, "overflow builtin lowering expects 3 arguments");
                return -1;
            }
            has_store = (bk == BUILTIN_ADD_OVERFLOW || bk == BUILTIN_SUB_OVERFLOW || bk == BUILTIN_MUL_OVERFLOW);
            overflow_op = (bk == BUILTIN_ADD_OVERFLOW || bk == BUILTIN_ADD_OVERFLOW_P)
                              ? 0
                              : ((bk == BUILTIN_SUB_OVERFLOW || bk == BUILTIN_SUB_OVERFLOW_P) ? 1 : 2);
            av = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            bv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag);
            if (av < 0 || bv < 0) {
                return -1;
            }
            if (has_store) {
                pv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[2], diag);
                if (pv < 0) {
                    return -1;
                }
            }
            av = cast_value(sf, av, CC_VAL_I64, diag);
            bv = cast_value(sf, bv, CC_VAL_I64, diag);
            if (av < 0 || bv < 0) {
                return -1;
            }
            if (has_store) {
                pv = cast_value(sf, pv, CC_VAL_I64, diag);
                if (pv < 0) {
                    return -1;
                }
            }

            memset(&bin, 0, sizeof(bin));
            bin.op = (overflow_op == 0) ? CC_SSA_ADD : ((overflow_op == 1) ? CC_SSA_SUB : CC_SSA_MUL);
            bin.dst = new_value(sf, CC_VAL_I64);
            bin.lhs = av;
            bin.rhs = bv;
            if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                return -1;
            }
            rv = bin.dst;

            if (has_store) {
                out_type = ptr_base_type(e->args[2]->value_type);
                if (out_type == CC_TYPE_VOID) {
                    out_type = CC_TYPE_INT;
                }
                mem_size = pointer_elem_size_bytes(tu, e->args[2]->value_type, e->args[2]->struct_id);
                if (mem_size <= 0) {
                    mem_size = type_size_bytes_with_struct(tu, out_type, e->args[2]->struct_id);
                }
                if (mem_size <= 0) {
                    mem_size = g_pointer_size_bytes;
                }
            } else {
                out_type = e->args[2]->value_type;
                mem_size = type_size_bytes_with_struct(tu, out_type, e->args[2]->struct_id);
            }

            rv = cast_value(sf, rv, type_to_val(out_type), diag);
            if (rv < 0) {
                return -1;
            }
            if (has_store) {
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_STORE;
                in.lhs = pv;
                in.rhs = rv;
                in.imm = mem_size;
                if (push_instr(sf, in) != 0) {
                    return -1;
                }
            }

            if (is_unsigned_integral_type(out_type)) {
                if (overflow_op == 2) {
                    int z = emit_const_i64_instr(sf, 0);
                    int cmp_nz;
                    int qv;
                    int cmp_ne;
                    if (z < 0) {
                        return -1;
                    }
                    memset(&bin, 0, sizeof(bin));
                    bin.op = CC_SSA_CMP;
                    bin.cmp_kind = CC_CMP_NE;
                    bin.is_unsigned = 1;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = bv;
                    bin.rhs = z;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    cmp_nz = bin.dst;
                    memset(&bin, 0, sizeof(bin));
                    bin.op = CC_SSA_DIV;
                    bin.is_unsigned = 1;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = rv;
                    bin.rhs = bv;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    qv = bin.dst;
                    memset(&bin, 0, sizeof(bin));
                    bin.op = CC_SSA_CMP;
                    bin.cmp_kind = CC_CMP_NE;
                    bin.is_unsigned = 1;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = qv;
                    bin.rhs = av;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    cmp_ne = bin.dst;
                    memset(&bin, 0, sizeof(bin));
                    bin.op = CC_SSA_AND;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = cmp_nz;
                    bin.rhs = cmp_ne;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    ov = bin.dst;
                } else {
                    memset(&bin, 0, sizeof(bin));
                    bin.op = CC_SSA_CMP;
                    bin.cmp_kind = CC_CMP_LT;
                    bin.is_unsigned = 1;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = (overflow_op == 1) ? av : rv;
                    bin.rhs = (overflow_op == 1) ? bv : av;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    ov = bin.dst;
                }
            } else if (overflow_op == 2) {
                int z = emit_const_i64_instr(sf, 0);
                int cmp_nz;
                int qv;
                int cmp_eq;
                int not_eq;
                if (z < 0) {
                    return -1;
                }
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_NE;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = bv;
                bin.rhs = z;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                cmp_nz = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_DIV;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = rv;
                bin.rhs = bv;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                qv = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_NE;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = qv;
                bin.rhs = av;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                cmp_eq = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_AND;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = cmp_nz;
                bin.rhs = cmp_eq;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                not_eq = bin.dst;
                ov = not_eq;
            } else {
                int sign_r;
                int sign_a;
                int sign_b;
                int sa_sb_eq;
                int sa_sr_ne;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_LT;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = rv;
                bin.rhs = emit_const_i64_instr(sf, 0);
                if (bin.rhs < 0 || bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                sign_r = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_LT;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = av;
                bin.rhs = emit_const_i64_instr(sf, 0);
                if (bin.rhs < 0 || bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                sign_a = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_LT;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = bv;
                bin.rhs = emit_const_i64_instr(sf, 0);
                if (bin.rhs < 0 || bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                sign_b = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = (overflow_op == 0) ? CC_CMP_EQ : CC_CMP_NE;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = sign_a;
                bin.rhs = sign_b;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                sa_sb_eq = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_NE;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = sign_a;
                bin.rhs = sign_r;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                sa_sr_ne = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_AND;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = sa_sb_eq;
                bin.rhs = sa_sr_ne;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                ov = bin.dst;
            }
            return ov;
        }
        if (bk == BUILTIN_OBJECT_SIZE) {
            long n = -1;
            long mode = 0;
            if (e->arg_count != 2 || e->args[0] == NULL || e->args[1] == NULL) {
                set_diag(diag, "__builtin_object_size lowering expects 2 arguments");
                return -1;
            }
            if (e->args[1]->kind == CC_EXPR_INT) {
                mode = e->args[1]->int_val;
            } else {
                int mode_v = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag);
                if (mode_v < 0) {
                    return -1;
                }
            }
            n = sizeof_expr_bytes(tu, vars, var_count, depth, e->args[0]);
            if (n < 0) {
                n = (mode & 2L) ? 0 : -1;
            }
            return emit_const_i64_instr(sf, n);
        }
        if (bk == BUILTIN_MEMCPY || bk == BUILTIN_MEMMOVE || bk == BUILTIN_MEMSET || bk == BUILTIN_MEMCPY_CHK ||
            bk == BUILTIN_MEMMOVE_CHK || bk == BUILTIN_MEMSET_CHK) {
            int dstv;
            int nbytes;
            int objsz;
            int cmp_ok;
            int l_ok;
            int l_bad;
            int needs_chk = (bk == BUILTIN_MEMCPY_CHK || bk == BUILTIN_MEMMOVE_CHK || bk == BUILTIN_MEMSET_CHK);
            size_t argc = needs_chk ? 4 : 3;
            cc_ssa_instr_t call_in;

            if (e->arg_count < argc) {
                set_diag(diag, needs_chk ? "__builtin___mem*_chk lowering has wrong argument count"
                                         : "__builtin_mem* lowering has wrong argument count");
                return -1;
            }
            dstv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (dstv < 0) {
                return -1;
            }
            dstv = cast_value(sf, dstv, CC_VAL_I64, diag);
            if (dstv < 0) {
                return -1;
            }
            nbytes = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[2], diag);
            if (nbytes < 0) {
                return -1;
            }
            nbytes = cast_value(sf, nbytes, CC_VAL_I64, diag);
            if (nbytes < 0) {
                return -1;
            }
            if (needs_chk) {
                objsz = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[argc - 1], diag);
                if (objsz < 0) {
                    return -1;
                }
                objsz = cast_value(sf, objsz, CC_VAL_I64, diag);
                if (objsz < 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_CMP;
                in.cmp_kind = CC_CMP_LE;
                in.is_unsigned = 1;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = nbytes;
                in.rhs = objsz;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    return -1;
                }
                cmp_ok = in.dst;
                l_ok = new_label(sf);
                l_bad = new_label(sf);
                if (l_ok < 0 || l_bad < 0) {
                    return -1;
                }
                if (emit_br_cond_instr(sf, cmp_ok, l_ok, l_bad) != 0 || emit_label_instr(sf, l_bad) != 0 ||
                    emit_trap_instr(sf) != 0 || emit_label_instr(sf, l_ok) != 0) {
                    return -1;
                }
            }

            memset(&call_in, 0, sizeof(call_in));
            call_in.op = CC_SSA_CALL;
            call_in.call_is_variadic = 0;
            call_in.dst = new_value(sf, CC_VAL_I64);
            call_in.arg_count = (bk == BUILTIN_MEMSET_CHK) ? 3 : 3;
            call_in.args = (int *)calloc(3, sizeof(*call_in.args));
            if (call_in.dst < 0 || call_in.args == NULL) {
                free(call_in.args);
                return -1;
            }
            if (bk == BUILTIN_MEMCPY || bk == BUILTIN_MEMCPY_CHK) {
                call_in.sym = xstrdup("memcpy");
                call_in.args[0] = dstv;
                call_in.args[1] = cast_value(sf, lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag),
                                             CC_VAL_I64, diag);
                call_in.args[2] = nbytes;
            } else if (bk == BUILTIN_MEMMOVE || bk == BUILTIN_MEMMOVE_CHK) {
                call_in.sym = xstrdup("memmove");
                call_in.args[0] = dstv;
                call_in.args[1] = cast_value(sf, lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag),
                                             CC_VAL_I64, diag);
                call_in.args[2] = nbytes;
            } else {
                call_in.sym = xstrdup("memset");
                call_in.args[0] = dstv;
                call_in.args[1] = cast_value(sf, lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag),
                                             CC_VAL_I64, diag);
                call_in.args[2] = nbytes;
            }
            if (call_in.sym == NULL || call_in.args[1] < 0) {
                free(call_in.sym);
                free(call_in.args);
                return -1;
            }
            if (push_instr(sf, call_in) != 0) {
                free(call_in.sym);
                free(call_in.args);
                return -1;
            }
            return call_in.dst;
        }
        if (bk == BUILTIN_CLZ || bk == BUILTIN_CTZ) {
            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, bk == BUILTIN_CLZ ? "__builtin_clz lowering expects 1 argument"
                                                 : "__builtin_ctz lowering expects 1 argument");
                return -1;
            }
            if (bk == BUILTIN_CLZ) {
                return lower_builtin_clz(tu, sf, ctx, vars, var_count, depth, e, diag);
            }
            return lower_builtin_ctz(tu, sf, ctx, vars, var_count, depth, e, diag);
        }
        if (bk == BUILTIN_SYNC_SYNCHRONIZE) {
            return emit_const_i64_instr(sf, 0);
        }
        if (bk == BUILTIN_SYNC_LOCK_RELEASE) {
            int p;
            int z;
            long mem_size;
            cc_type_t elem_type;
            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, "__sync_lock_release lowering expects 1 argument");
                return -1;
            }
            p = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (p < 0) {
                return -1;
            }
            p = cast_value(sf, p, CC_VAL_I64, diag);
            if (p < 0) {
                return -1;
            }
            elem_type = ptr_base_type(e->args[0]->value_type);
            if (elem_type == CC_TYPE_VOID) {
                elem_type = CC_TYPE_INT;
            }
            mem_size = pointer_elem_size_bytes(tu, e->args[0]->value_type, e->args[0]->struct_id);
            if (mem_size <= 0) {
                mem_size = type_size_bytes_with_struct(tu, elem_type, e->args[0]->struct_id);
            }
            if (mem_size <= 0) {
                mem_size = g_pointer_size_bytes;
            }
            z = emit_const_i64_instr(sf, 0);
            if (z < 0) {
                return -1;
            }
            z = cast_value(sf, z, type_to_val(elem_type), diag);
            if (z < 0) {
                return -1;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_STORE;
            in.dst = -1;
            in.lhs = p;
            in.rhs = z;
            in.imm = mem_size;
            if (push_instr(sf, in) != 0) {
                return -1;
            }
            return z;
        }
        if (bk == BUILTIN_SYNC_FETCH_ADD || bk == BUILTIN_SYNC_FETCH_SUB || bk == BUILTIN_SYNC_SUB_AND_FETCH ||
            bk == BUILTIN_SYNC_BOOL_CAS || bk == BUILTIN_SYNC_LOCK_TEST_AND_SET || bk == BUILTIN_ATOMIC_FETCH_ADD ||
            bk == BUILTIN_ATOMIC_FETCH_SUB || bk == BUILTIN_ATOMIC_EXCHANGE_N || bk == BUILTIN_ATOMIC_LOAD_N ||
            bk == BUILTIN_ATOMIC_STORE_N) {
            int p;
            int oldv = -1;
            int newv = -1;
            int rhsv = -1;
            int cmpv = -1;
            int l_store = -1;
            int l_done = -1;
            long mem_size;
            cc_type_t elem_type;
            cc_ssa_instr_t bin;
            if (e->arg_count == 0 || e->args[0] == NULL) {
                set_diag(diag, "atomic builtin requires pointer argument");
                return -1;
            }
            p = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (p < 0) {
                return -1;
            }
            p = cast_value(sf, p, CC_VAL_I64, diag);
            if (p < 0) {
                return -1;
            }
            elem_type = ptr_base_type(e->args[0]->value_type);
            if (elem_type == CC_TYPE_VOID) {
                elem_type = CC_TYPE_INT;
            }
            mem_size = pointer_elem_size_bytes(tu, e->args[0]->value_type, e->args[0]->struct_id);
            if (mem_size <= 0) {
                mem_size = type_size_bytes_with_struct(tu, elem_type, e->args[0]->struct_id);
            }
            if (mem_size <= 0) {
                mem_size = g_pointer_size_bytes;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_LOAD;
            in.dst = new_value(sf, type_to_val(elem_type));
            in.lhs = p;
            in.rhs = -1;
            in.imm = mem_size;
            in.is_unsigned = is_unsigned_load_type(elem_type) ? 1 : 0;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                return -1;
            }
            oldv = in.dst;
            if (bk == BUILTIN_ATOMIC_LOAD_N) {
                if (e->arg_count >= 2 && e->args[1] != NULL &&
                    lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag) < 0) {
                    return -1;
                }
                return oldv;
            }
            if ((bk == BUILTIN_SYNC_FETCH_ADD || bk == BUILTIN_SYNC_FETCH_SUB || bk == BUILTIN_SYNC_SUB_AND_FETCH ||
                 bk == BUILTIN_SYNC_LOCK_TEST_AND_SET || bk == BUILTIN_ATOMIC_FETCH_ADD ||
                 bk == BUILTIN_ATOMIC_FETCH_SUB || bk == BUILTIN_ATOMIC_EXCHANGE_N || bk == BUILTIN_ATOMIC_STORE_N) &&
                e->arg_count >= 2 && e->args[1] != NULL) {
                rhsv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag);
                if (rhsv < 0) {
                    return -1;
                }
                rhsv = cast_value(sf, rhsv, type_to_val(elem_type), diag);
                if (rhsv < 0) {
                    return -1;
                }
            }
            if (bk == BUILTIN_ATOMIC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_SUB || bk == BUILTIN_ATOMIC_EXCHANGE_N ||
                bk == BUILTIN_ATOMIC_STORE_N) {
                if (e->arg_count >= 3 && e->args[2] != NULL &&
                    lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[2], diag) < 0) {
                    return -1;
                }
            }
            if (bk == BUILTIN_SYNC_FETCH_ADD || bk == BUILTIN_SYNC_FETCH_SUB || bk == BUILTIN_SYNC_SUB_AND_FETCH ||
                bk == BUILTIN_ATOMIC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_SUB) {
                memset(&bin, 0, sizeof(bin));
                bin.op = (bk == BUILTIN_SYNC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_ADD) ? CC_SSA_ADD : CC_SSA_SUB;
                bin.dst = new_value(sf, type_to_val(elem_type));
                bin.lhs = oldv;
                bin.rhs = rhsv;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                newv = bin.dst;
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_STORE;
                in.dst = -1;
                in.lhs = p;
                in.rhs = newv;
                in.imm = mem_size;
                if (push_instr(sf, in) != 0) {
                    return -1;
                }
                return bk == BUILTIN_SYNC_SUB_AND_FETCH ? newv : oldv;
            }
            if (bk == BUILTIN_SYNC_LOCK_TEST_AND_SET || bk == BUILTIN_ATOMIC_EXCHANGE_N) {
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_STORE;
                in.dst = -1;
                in.lhs = p;
                in.rhs = rhsv;
                in.imm = mem_size;
                if (push_instr(sf, in) != 0) {
                    return -1;
                }
                return oldv;
            }
            if (bk == BUILTIN_ATOMIC_STORE_N) {
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_STORE;
                in.dst = -1;
                in.lhs = p;
                in.rhs = rhsv;
                in.imm = mem_size;
                if (push_instr(sf, in) != 0) {
                    return -1;
                }
                return rhsv;
            }
            if (bk == BUILTIN_SYNC_BOOL_CAS) {
                int expectv;
                int desiredv;
                if (e->arg_count != 3 || e->args[1] == NULL || e->args[2] == NULL) {
                    set_diag(diag, "__sync_bool_compare_and_swap lowering expects 3 arguments");
                    return -1;
                }
                expectv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag);
                if (expectv < 0) {
                    return -1;
                }
                expectv = cast_value(sf, expectv, type_to_val(elem_type), diag);
                if (expectv < 0) {
                    return -1;
                }
                desiredv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[2], diag);
                if (desiredv < 0) {
                    return -1;
                }
                desiredv = cast_value(sf, desiredv, type_to_val(elem_type), diag);
                if (desiredv < 0) {
                    return -1;
                }
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_EQ;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = oldv;
                bin.rhs = expectv;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                cmpv = bin.dst;
                l_store = new_label(sf);
                l_done = new_label(sf);
                if (l_store < 0 || l_done < 0) {
                    return -1;
                }
                if (emit_br_cond_instr(sf, cmpv, l_store, l_done) != 0) {
                    return -1;
                }
                if (emit_label_instr(sf, l_store) != 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_STORE;
                in.dst = -1;
                in.lhs = p;
                in.rhs = desiredv;
                in.imm = mem_size;
                if (push_instr(sf, in) != 0) {
                    return -1;
                }
                if (emit_br_instr(sf, l_done) != 0) {
                    return -1;
                }
                if (emit_label_instr(sf, l_done) != 0) {
                    return -1;
                }
                return cmpv;
            }
        }
        if (e->ident != NULL && bswap_bits != 0) {
            int x;
            int res;
            int nbytes = bswap_bits / 8;
            int bi;
            if (e->arg_count != 1) {
                set_diag(diag, "byte-swap builtin lowering expects 1 argument");
                return -1;
            }
            x = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (x < 0) {
                return -1;
            }
            x = cast_value(sf, x, CC_VAL_I64, diag);
            if (x < 0) {
                return -1;
            }
            res = emit_const_i64_instr(sf, 0);
            if (res < 0) {
                return -1;
            }
            for (bi = 0; bi < nbytes; ++bi) {
                unsigned long long mask_u = 0xffULL << (bi * 8);
                int shift = ((nbytes - 1) - (2 * bi)) * 8;
                int cmask = emit_const_i64_instr(sf, (long)mask_u);
                int part;
                cc_ssa_instr_t bin;
                int cshift;
                if (cmask < 0) {
                    return -1;
                }
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_AND;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = x;
                bin.rhs = cmask;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                part = bin.dst;
                if (shift > 0) {
                    cshift = emit_const_i64_instr(sf, shift);
                    if (cshift < 0) {
                        return -1;
                    }
                    memset(&bin, 0, sizeof(bin));
                    bin.op = CC_SSA_SHL;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = part;
                    bin.rhs = cshift;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    part = bin.dst;
                } else if (shift < 0) {
                    cshift = emit_const_i64_instr(sf, -shift);
                    if (cshift < 0) {
                        return -1;
                    }
                    memset(&bin, 0, sizeof(bin));
                    bin.op = CC_SSA_SHR;
                    bin.is_unsigned = 1;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = part;
                    bin.rhs = cshift;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    part = bin.dst;
                }
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_OR;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = res;
                bin.rhs = part;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                res = bin.dst;
            }
            return res;
        }
        if (e->ident == NULL || (e->ident != NULL && callee == NULL && bk == BUILTIN_NONE && bswap_bits == 0 &&
                                 (var_find_visible(vars, var_count, e->ident, depth) >= 0 ||
                                  find_global(tu, e->ident) != NULL))) {
            if (e->ident == NULL) {
                const cc_expr_t *callee_expr = e->lhs;
                cc_expr_t callee_tmp;
                if (e->lhs == NULL) {
                    set_diag(diag, "malformed indirect call expression");
                    return -1;
                }
                if (callee_expr->kind == CC_EXPR_DEREF && callee_expr->lhs != NULL &&
                    is_pointer_type(callee_expr->lhs->value_type)) {
                    int keep_deref = 0;
                    const cc_expr_t *d = callee_expr->lhs;
                    if (expr_is_array_pointer_chain(tu, vars, var_count, depth, d)) {
                        keep_deref = 1;
                    }
                    if (d->kind == CC_EXPR_BIN && (d->op == CC_BIN_ADD || d->op == CC_BIN_SUB)) {
                        if (d->lhs != NULL && d->lhs->kind == CC_EXPR_DEREF && d->lhs->lhs != NULL &&
                            expr_is_array_pointer_chain(tu, vars, var_count, depth, d->lhs->lhs)) {
                            keep_deref = 1;
                        }
                        if (d->rhs != NULL && d->rhs->kind == CC_EXPR_DEREF && d->rhs->lhs != NULL &&
                            expr_is_array_pointer_chain(tu, vars, var_count, depth, d->rhs->lhs)) {
                            keep_deref = 1;
                        }
                    }
                    if (!keep_deref) {
                        callee_expr = callee_expr->lhs;
                    }
                }
                if (callee_expr->kind == CC_EXPR_BIN &&
                    (callee_expr->op == CC_BIN_ADD || callee_expr->op == CC_BIN_SUB) &&
                    callee_expr->lhs != NULL && callee_expr->lhs->kind == CC_EXPR_DEREF &&
                    callee_expr->lhs->lhs != NULL &&
                    expr_is_array_pointer_chain(tu, vars, var_count, depth, callee_expr->lhs->lhs)) {
                    callee_tmp = *callee_expr;
                    callee_tmp.lhs = callee_expr->lhs->lhs;
                    callee_expr = &callee_tmp;
                }
                indirect_callee = lower_expr(tu, sf, ctx, vars, var_count, depth, callee_expr, diag);
            } else {
                cc_expr_t callee_ref;
                memset(&callee_ref, 0, sizeof(callee_ref));
                callee_ref.kind = CC_EXPR_IDENT;
                callee_ref.ident = e->ident;
                indirect_callee = lower_expr(tu, sf, ctx, vars, var_count, depth, &callee_ref, diag);
            }
            if (indirect_callee < 0) {
                return -1;
            }
            indirect_callee = cast_value(sf, indirect_callee, CC_VAL_I64, diag);
            if (indirect_callee < 0) {
                return -1;
            }
            in.op = CC_SSA_CALLI;
            in.lhs = indirect_callee;
            /*
             * Function-pointer signatures are not fully represented in cc_type_t
             * yet; use variadic caller setup for CALLI so va_start-based callees
             * receive the copied argument area and XMM count consistently.
             */
            in.call_is_variadic = 1;
            in.call_fixed_count = 0;
            in.sym = NULL;
        } else {
            in.op = CC_SSA_CALL;
            in.call_is_variadic = (callee != NULL && callee->has_prototype) ? callee->is_variadic : 0;
            in.call_fixed_count =
                (callee != NULL && callee->has_prototype && callee->is_variadic) ? (int)callee->param_count : 0;
            in.sym = xstrdup(e->ident);
            if (in.sym == NULL) {
                return -1;
            }
        }
        in.arg_count = e->arg_count;
        if (in.arg_count > 0) {
            in.args = (int *)calloc(in.arg_count, sizeof(*in.args));
            if (in.args == NULL) {
                free(in.sym);
                return -1;
            }
        }

        for (i = 0; i < in.arg_count; ++i) {
            cc_value_type_t want;
            int av = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[i], diag);
            if (av < 0) {
                free(in.sym);
                free(in.args);
                return -1;
            }
            want = value_type(sf, av);
            if (e->ident != NULL && callee != NULL && callee->has_prototype && i < callee->param_count) {
                want = type_to_val(callee->params[i].type);
                av = cast_value(sf, av, want, diag);
                if (av < 0) {
                    free(in.sym);
                    free(in.args);
                    return -1;
                }
            }
            in.args[i] = av;
        }

        if (e->value_type == CC_TYPE_VOID) {
            in.dst = new_value(sf, CC_VAL_I64);
            in.imm = g_pointer_size_bytes;
            in.is_unsigned = 1;
        } else {
            in.dst = new_value(sf, type_to_val(e->value_type));
            in.imm = type_size_bytes_with_struct(tu, e->value_type, e->struct_id);
            if (in.imm <= 0) {
                in.imm = g_pointer_size_bytes;
            }
            in.is_unsigned = is_unsigned_load_type(e->value_type) ? 1 : 0;
        }
        if (in.dst < 0) {
            free(in.sym);
            free(in.args);
            return -1;
        }

        if (push_instr(sf, in) != 0) {
            free(in.sym);
            free(in.args);
            return -1;
        }
        return in.dst;
    }

    case CC_EXPR_ASSIGN: {
        cc_value_type_t want = type_to_val(e->value_type);
        rhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
        if (rhs < 0) {
            return -1;
        }
        rhs = cast_value(sf, rhs, want, diag);
        if (rhs < 0) {
            return -1;
        }

        if (e->ident != NULL) {
            int idx = var_find_visible(vars, var_count, e->ident, depth);
            if (idx >= 0) {
                if (vars[idx].is_static_storage) {
                    int saddr = emit_global_addr(sf, vars[idx].static_sym, diag);
                    long mem_size = type_size_bytes_with_struct(tu, vars[idx].type, vars[idx].struct_id);
                    if (saddr < 0) {
                        return -1;
                    }
                    if (mem_size <= 0) {
                        set_diag(diag, "unsupported static local assignment type");
                        return -1;
                    }
                    rhs = normalize_float_value(sf, rhs, vars[idx].type, diag);
                    if (rhs < 0) {
                        return -1;
                    }
                    rhs = normalize_integral_value(sf, rhs, vars[idx].type, diag);
                    if (rhs < 0) {
                        return -1;
                    }
                    if (vars[idx].type == CC_TYPE_VOID && vars[idx].struct_id >= 0) {
                        if (emit_memcpy_instr(sf, saddr, rhs, mem_size, diag) != 0) {
                            return -1;
                        }
                    } else {
                        memset(&in, 0, sizeof(in));
                        in.op = CC_SSA_STORE;
                        in.dst = -1;
                        in.lhs = saddr;
                        in.rhs = rhs;
                        in.imm = mem_size;
                        if (push_instr(sf, in) != 0) {
                            set_diag(diag, "out of memory appending static local assignment store");
                            return -1;
                        }
                    }
                    return rhs;
                }
                if (vars[idx].type == CC_TYPE_VOID && vars[idx].struct_id >= 0) {
                    long mem_size = type_size_bytes_with_struct(tu, vars[idx].type, vars[idx].struct_id);
                    if (mem_size <= 0) {
                        set_diag(diag, "unsupported local aggregate assignment type");
                        return -1;
                    }
                    if (emit_memcpy_instr(sf, vars[idx].value, rhs, mem_size, diag) != 0) {
                        return -1;
                    }
                } else {
                    rhs = normalize_float_value(sf, rhs, vars[idx].type, diag);
                    if (rhs < 0) {
                        return -1;
                    }
                    rhs = normalize_integral_value(sf, rhs, vars[idx].type, diag);
                    if (rhs < 0) {
                        return -1;
                    }
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_MOV;
                    in.dst = vars[idx].value;
                    in.lhs = rhs;
                    in.rhs = -1;
                    if (push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory appending assignment move");
                        return -1;
                    }
                }
                return vars[idx].value;
            }
            {
                const cc_global_t *g = find_global(tu, e->ident);
                long mem_size;
                int gaddr;
                if (g == NULL) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "assignment to unknown identifier during AST->SSA lowering: %s", e->ident);
                    }
                    return -1;
                }
                if (is_array_object_decl(g->type, g->array_len, g->array_ndim)) {
                    set_diag(diag, "assignment to array object is not supported");
                    return -1;
                }
                mem_size = type_size_bytes_with_struct(tu, g->type, g->type_struct_id);
                if (mem_size <= 0) {
                    set_diag(diag, "unsupported global assignment type in lowering");
                    return -1;
                }
                gaddr = emit_global_addr(sf, g->name, diag);
                if (gaddr < 0) {
                    return -1;
                }
                rhs = normalize_float_value(sf, rhs, g->type, diag);
                if (rhs < 0) {
                    return -1;
                }
                rhs = normalize_integral_value(sf, rhs, g->type, diag);
                if (rhs < 0) {
                    return -1;
                }
                if (g->type == CC_TYPE_VOID && g->type_struct_id >= 0) {
                    if (emit_memcpy_instr(sf, gaddr, rhs, mem_size, diag) != 0) {
                        return -1;
                    }
                } else {
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_STORE;
                    in.dst = -1;
                    in.lhs = gaddr;
                    in.rhs = rhs;
                    in.imm = mem_size;
                    if (push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory appending global assignment store");
                        return -1;
                    }
                }
                return rhs;
            }
        }

        if (e->lhs != NULL && (e->lhs->kind == CC_EXPR_DEREF || e->lhs->kind == CC_EXPR_MEMBER)) {
            long mem_size = type_size_bytes_with_struct(tu, e->lhs->value_type, e->lhs->struct_id);
            int ptrv;
            if (mem_size <= 0) {
                set_diag(diag, "unsupported pointer store type size in lowering");
                return -1;
            }
            if (e->lhs->kind == CC_EXPR_DEREF) {
                if (e->lhs->lhs == NULL) {
                    set_diag(diag, "malformed dereference assignment in lowering");
                    return -1;
                }
                ptrv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs->lhs, diag);
            } else {
                ptrv = lower_member_addr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
            }
            if (ptrv < 0) {
                return -1;
            }
            ptrv = cast_value(sf, ptrv, CC_VAL_I64, diag);
            if (ptrv < 0) {
                return -1;
            }
            rhs = normalize_float_value(sf, rhs, e->lhs->value_type, diag);
            if (rhs < 0) {
                return -1;
            }
            rhs = normalize_integral_value(sf, rhs, e->lhs->value_type, diag);
            if (rhs < 0) {
                return -1;
            }
            if (e->lhs->value_type == CC_TYPE_VOID && e->lhs->struct_id >= 0) {
                if (emit_memcpy_instr(sf, ptrv, rhs, mem_size, diag) != 0) {
                    return -1;
                }
                return rhs;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_STORE;
            in.dst = -1;
            in.lhs = ptrv;
            in.rhs = rhs;
            in.imm = mem_size;
            if (push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory appending pointer store");
                return -1;
            }
            return rhs;
        }

        set_diag(diag, "unsupported assignment target in lowering");
        return -1;
    }

    case CC_EXPR_UPDATE: {
        cc_value_type_t want = type_to_val(e->value_type);
        int one;
        int cur;
        int nextv;
        long step = 1;

        if (e->ident != NULL) {
            int idx = var_find_visible(vars, var_count, e->ident, depth);
            if (idx >= 0) {
                if (vars[idx].is_static_storage) {
                    int saddr;
                    long mem_size = type_size_bytes_with_struct(tu, vars[idx].type, vars[idx].struct_id);
                    if (vars[idx].static_sym == NULL) {
                        set_diag(diag, "malformed static local symbol in lowering");
                        return -1;
                    }
                    if (mem_size <= 0) {
                        set_diag(diag, "unsupported static local ++/-- type");
                        return -1;
                    }
                    saddr = emit_global_addr(sf, vars[idx].static_sym, diag);
                    if (saddr < 0) {
                        return -1;
                    }
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_LOAD;
                    in.dst = new_value(sf, want);
                    in.lhs = saddr;
                    in.rhs = -1;
                    in.imm = mem_size;
                    in.is_unsigned = is_unsigned_load_type(vars[idx].type) ? 1 : 0;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory loading static local for ++/--");
                        return -1;
                    }
                    cur = in.dst;
                } else {
                    cur = cast_value(sf, vars[idx].value, want, diag);
                    if (cur < 0) {
                        return -1;
                    }
                    if (e->update_postfix) {
                        int oldv = new_value(sf, want);
                        if (oldv < 0 || emit_mov_instr(sf, oldv, cur) != 0) {
                            set_diag(diag, "out of memory snapshotting postfix ++/-- value");
                            return -1;
                        }
                        cur = oldv;
                    }
                }

                if (is_pointer_type(vars[idx].type)) {
                    step = pointer_elem_size_bytes(tu, vars[idx].type, vars[idx].struct_id);
                    if (step <= 0) {
                        set_diag(diag, "unsupported pointer ++/-- type in lowering");
                        return -1;
                    }
                }

                one = emit_const_i64_instr(sf, step);
                if (one < 0) {
                    return -1;
                }
                if (!is_pointer_type(vars[idx].type)) {
                    one = cast_value(sf, one, want, diag);
                    if (one < 0) {
                        return -1;
                    }
                }

                memset(&in, 0, sizeof(in));
                in.op = (e->op == CC_BIN_SUB) ? CC_SSA_SUB : CC_SSA_ADD;
                in.dst = new_value(sf, want);
                in.lhs = cur;
                in.rhs = one;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    return -1;
                }
                nextv = in.dst;
                if (vars[idx].type == CC_TYPE_FLOAT) {
                    nextv = normalize_float_value(sf, nextv, vars[idx].type, diag);
                    if (nextv < 0) {
                        return -1;
                    }
                }
                if (is_integral_type(vars[idx].type) && !is_pointer_type(vars[idx].type)) {
                    nextv = normalize_integral_value(sf, nextv, vars[idx].type, diag);
                    if (nextv < 0) {
                        return -1;
                    }
                }

                if (vars[idx].is_static_storage) {
                    int saddr = emit_global_addr(sf, vars[idx].static_sym, diag);
                    long mem_size = type_size_bytes_with_struct(tu, vars[idx].type, vars[idx].struct_id);
                    if (saddr < 0) {
                        return -1;
                    }
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_STORE;
                    in.dst = -1;
                    in.lhs = saddr;
                    in.rhs = nextv;
                    in.imm = mem_size;
                    if (push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory storing static local ++/-- result");
                        return -1;
                    }
                } else if (emit_mov_instr(sf, vars[idx].value, nextv) != 0) {
                    return -1;
                }
                if (e->update_postfix) {
                    return cur;
                }
                if (vars[idx].is_static_storage) {
                    return nextv;
                }
                return vars[idx].value;
            }

            {
                const cc_global_t *g = find_global(tu, e->ident);
                long mem_size;
                int gaddr;
                cc_ssa_instr_t load_in;
                cc_ssa_instr_t store_in;
                if (g == NULL) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "update of unknown identifier during AST->SSA lowering: %s", e->ident);
                    }
                    return -1;
                }
                mem_size = type_size_bytes_with_struct(tu, g->type, g->type_struct_id);
                if (mem_size <= 0) {
                    set_diag(diag, "unsupported global update type in lowering");
                    return -1;
                }
                if (is_pointer_type(g->type)) {
                    step = pointer_elem_size_bytes(tu, g->type, g->type_struct_id);
                    if (step <= 0) {
                        set_diag(diag, "unsupported pointer ++/-- type in lowering");
                        return -1;
                    }
                }
                gaddr = emit_global_addr(sf, g->name, diag);
                if (gaddr < 0) {
                    return -1;
                }
                memset(&load_in, 0, sizeof(load_in));
                load_in.op = CC_SSA_LOAD;
                load_in.dst = new_value(sf, want);
                load_in.lhs = gaddr;
                load_in.rhs = -1;
                load_in.imm = mem_size;
                load_in.is_unsigned = is_unsigned_load_type(g->type) ? 1 : 0;
                if (load_in.dst < 0 || push_instr(sf, load_in) != 0) {
                    return -1;
                }
                cur = load_in.dst;
                one = emit_const_i64_instr(sf, step);
                if (one < 0) {
                    return -1;
                }
                if (!is_pointer_type(g->type)) {
                    one = cast_value(sf, one, want, diag);
                    if (one < 0) {
                        return -1;
                    }
                }
                memset(&in, 0, sizeof(in));
                in.op = (e->op == CC_BIN_SUB) ? CC_SSA_SUB : CC_SSA_ADD;
                in.dst = new_value(sf, want);
                in.lhs = cur;
                in.rhs = one;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    return -1;
                }
                nextv = in.dst;
                if (g->type == CC_TYPE_FLOAT) {
                    nextv = normalize_float_value(sf, nextv, g->type, diag);
                    if (nextv < 0) {
                        return -1;
                    }
                }
                if (is_integral_type(g->type) && !is_pointer_type(g->type)) {
                    nextv = normalize_integral_value(sf, nextv, g->type, diag);
                    if (nextv < 0) {
                        return -1;
                    }
                }
                memset(&store_in, 0, sizeof(store_in));
                store_in.op = CC_SSA_STORE;
                store_in.dst = -1;
                store_in.lhs = gaddr;
                store_in.rhs = nextv;
                store_in.imm = mem_size;
                if (push_instr(sf, store_in) != 0) {
                    return -1;
                }
                if (e->update_postfix) {
                    return cur;
                }
                return nextv;
            }
        }

        if (e->lhs != NULL && (e->lhs->kind == CC_EXPR_DEREF || e->lhs->kind == CC_EXPR_MEMBER)) {
            long mem_size = type_size_bytes(e->lhs->value_type);
            int ptrv;

            if (is_pointer_type(e->value_type)) {
                step = pointer_elem_size_bytes(tu, e->value_type, e->struct_id);
                if (step <= 0) {
                    set_diag(diag, "unsupported pointer ++/-- type in lowering");
                    return -1;
                }
            }

            if (e->lhs->kind == CC_EXPR_DEREF) {
                if (e->lhs->lhs == NULL) {
                    set_diag(diag, "malformed dereference update in lowering");
                    return -1;
                }
                ptrv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs->lhs, diag);
            } else {
                ptrv = lower_member_addr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
            }
            if (ptrv < 0) {
                return -1;
            }
            ptrv = cast_value(sf, ptrv, CC_VAL_I64, diag);
            if (ptrv < 0) {
                return -1;
            }
            if (mem_size <= 0) {
                set_diag(diag, "unsupported dereference update type size in lowering");
                return -1;
            }

            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_LOAD;
            in.dst = new_value(sf, want);
            in.lhs = ptrv;
            in.rhs = -1;
            in.imm = mem_size;
            in.is_unsigned = is_unsigned_load_type(e->value_type) ? 1 : 0;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                return -1;
            }
            cur = in.dst;

            one = emit_const_i64_instr(sf, step);
            if (one < 0) {
                return -1;
            }
            if (!is_pointer_type(e->value_type)) {
                one = cast_value(sf, one, want, diag);
                if (one < 0) {
                    return -1;
                }
            }

            memset(&in, 0, sizeof(in));
            in.op = (e->op == CC_BIN_SUB) ? CC_SSA_SUB : CC_SSA_ADD;
            in.dst = new_value(sf, want);
            in.lhs = cur;
            in.rhs = one;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                return -1;
            }
            nextv = in.dst;
            if (e->value_type == CC_TYPE_FLOAT) {
                nextv = normalize_float_value(sf, nextv, e->value_type, diag);
                if (nextv < 0) {
                    return -1;
                }
            }
            if (is_integral_type(e->value_type) && !is_pointer_type(e->value_type)) {
                nextv = normalize_integral_value(sf, nextv, e->value_type, diag);
                if (nextv < 0) {
                    return -1;
                }
            }

            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_STORE;
            in.dst = -1;
            in.lhs = ptrv;
            in.rhs = nextv;
            in.imm = mem_size;
            if (push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory appending pointer update store");
                return -1;
            }

            if (e->update_postfix) {
                return cur;
            }
            return nextv;
        }

        set_diag(diag, "unsupported update target in lowering");
        return -1;
    }

    case CC_EXPR_CAST: {
        int v;
        const cc_expr_t *cast_src = e->lhs;
        int aggregate_cast_target = (e->aux_type == CC_TYPE_VOID && e->aux_struct_id >= 0);
        if (e->lhs == NULL) {
            set_diag(diag, "malformed cast expression in lowering");
            return -1;
        }
        if (aggregate_cast_target) {
            int dst_ptr;
            int storesrc;
            long store_size;
            long sz = type_size_bytes_with_struct(tu, e->aux_type, e->aux_struct_id);
            if (sz <= 0) {
                sz = 1;
            }
            dst_ptr = emit_local_storage_alloc(sf, sz, diag);
            if (dst_ptr < 0) {
                return -1;
            }
            if (cast_src->kind == CC_EXPR_INIT_LIST) {
                if (lower_struct_init_to_ptr(tu, sf, ctx, vars, var_count, depth, dst_ptr, e->aux_struct_id, cast_src,
                                             diag) != 0) {
                    return -1;
                }
                return dst_ptr;
            }
            if ((size_t)e->aux_struct_id >= tu->struct_count || !tu->structs[e->aux_struct_id].is_union) {
                set_diag(diag, "aggregate cast lowering only supports union targets");
                return -1;
            }
            {
                const cc_struct_member_t *m = find_union_cast_member(tu, e->aux_struct_id, cast_src->value_type);
                cc_ssa_instr_t st;
                if (m == NULL) {
                    set_diag(diag, "union cast lowering found no compatible member");
                    return -1;
                }
                storesrc = lower_expr(tu, sf, ctx, vars, var_count, depth, cast_src, diag);
                if (storesrc < 0) {
                    return -1;
                }
                storesrc = cast_value(sf, storesrc, type_to_val(m->type), diag);
                if (storesrc < 0) {
                    return -1;
                }
                store_size = type_size_bytes_with_struct(tu, m->type, m->type_struct_id);
                if (store_size <= 0) {
                    set_diag(diag, "unsupported union cast member size in lowering");
                    return -1;
                }
                if (m->offset != 0) {
                    int off = emit_const_i64_instr(sf, m->offset);
                    cc_ssa_instr_t addi;
                    if (off < 0) {
                        return -1;
                    }
                    memset(&addi, 0, sizeof(addi));
                    addi.op = CC_SSA_ADD;
                    addi.dst = new_value(sf, CC_VAL_I64);
                    addi.lhs = dst_ptr;
                    addi.rhs = off;
                    if (addi.dst < 0 || push_instr(sf, addi) != 0) {
                        set_diag(diag, "out of memory computing union cast member address");
                        return -1;
                    }
                    dst_ptr = addi.dst;
                }
                memset(&st, 0, sizeof(st));
                st.op = CC_SSA_STORE;
                st.dst = -1;
                st.lhs = dst_ptr;
                st.rhs = storesrc;
                st.imm = store_size;
                if (push_instr(sf, st) != 0) {
                    set_diag(diag, "out of memory emitting union cast store");
                    return -1;
                }
            }
            return dst_ptr;
        }
        if (cast_src->kind == CC_EXPR_INIT_LIST) {
            cast_src = unwrap_scalar_initializer_expr(cast_src, diag);
            if (cast_src == NULL) {
                return -1;
            }
        }
        v = lower_expr(tu, sf, ctx, vars, var_count, depth, cast_src, diag);
        if (v < 0) {
            return -1;
        }
        if (e->aux_type == CC_TYPE_VOID) {
            return v;
        }
        v = cast_value(sf, v, type_to_val(e->aux_type), diag);
        if (v < 0) {
            return -1;
        }
        if (is_integral_type(e->aux_type) && !is_pointer_type(e->aux_type)) {
            v = normalize_integral_value(sf, v, e->aux_type, diag);
            if (v < 0) {
                return -1;
            }
        } else if (e->aux_type == CC_TYPE_FLOAT) {
            v = normalize_float_value(sf, v, e->aux_type, diag);
            if (v < 0) {
                return -1;
            }
        }
        return v;
    }

    case CC_EXPR_SIZEOF: {
        long n;
        if (e->lhs != NULL) {
            n = sizeof_expr_bytes(tu, vars, var_count, depth, e->lhs);
        } else {
            n = type_size_bytes_struct(tu, e->aux_type, e->aux_struct_id);
        }
        if (n < 0) {
            set_diag(diag, "unsupported sizeof operand in lowering");
            return -1;
        }
        in.op = CC_SSA_CONST;
        in.dst = new_value(sf, CC_VAL_I64);
        in.imm = n;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;
    }

    case CC_EXPR_INIT_LIST:
        if (getenv("CC_DEBUG_INIT_LIST") != NULL) {
            fprintf(stderr, "cc-debug: init-list reached lower_expr in %s (value_type=%d struct_id=%d arg_count=%zu)\n",
                    (sf != NULL && sf->name != NULL) ? sf->name : "<unknown>", (int)e->value_type, e->struct_id,
                    e->arg_count);
        }
        set_diag(diag, "initializer list cannot be used as runtime expression in lowering");
        return -1;

    case CC_EXPR_STMT: {
        var_entry_t *lvars = NULL;
        size_t lcount = var_count;
        size_t j;
        int saw_ret_dummy = 0;
        int outv = -1;

        if (var_count > 0) {
            lvars = (var_entry_t *)calloc(var_count, sizeof(*lvars));
            if (lvars == NULL) {
                set_diag(diag, "out of memory cloning scope for statement expression");
                return -1;
            }
            for (j = 0; j < var_count; ++j) {
                lvars[j] = vars[j];
                lvars[j].name = xstrdup(vars[j].name);
                if (lvars[j].name == NULL) {
                    size_t k;
                    for (k = 0; k < j; ++k) {
                        free(lvars[k].name);
                        free(lvars[k].static_sym);
                    }
                    free(lvars);
                    set_diag(diag, "out of memory cloning scope for statement expression");
                    return -1;
                }
                if (vars[j].static_sym != NULL) {
                    lvars[j].static_sym = xstrdup(vars[j].static_sym);
                    if (lvars[j].static_sym == NULL) {
                        size_t k;
                        free(lvars[j].name);
                        for (k = 0; k < j; ++k) {
                            free(lvars[k].name);
                            free(lvars[k].static_sym);
                        }
                        free(lvars);
                        set_diag(diag, "out of memory cloning scope for statement expression");
                        return -1;
                    }
                }
            }
        }
        for (j = 0; j < e->stmt_expr_count; ++j) {
            const cc_stmt_t *cur = &e->stmt_expr_stmts[j];
            int is_last = (j + 1 == e->stmt_expr_count);
            if (is_last && cur->kind == CC_STMT_EXPR && cur->expr != NULL) {
                outv = lower_expr(tu, sf, ctx, lvars, lcount, depth + 1, cur->expr, diag);
                if (outv < 0) {
                    while (lcount > 0) {
                        lcount--;
                        free(lvars[lcount].name);
                        free(lvars[lcount].static_sym);
                    }
                    free(lvars);
                    return -1;
                }
                continue;
            }
            if (lower_stmt(tu, sf, &lvars, &lcount, ctx, depth + 1, -1, -1, cur, &saw_ret_dummy,
                           diag) != 0) {
                while (lcount > 0) {
                    lcount--;
                    free(lvars[lcount].name);
                    free(lvars[lcount].static_sym);
                }
                free(lvars);
                return -1;
            }
        }
        if (outv >= 0) {
            outv = cast_value(sf, outv, type_to_val(e->value_type), diag);
        }
        while (lcount > 0) {
            lcount--;
            free(lvars[lcount].name);
            free(lvars[lcount].static_sym);
        }
        free(lvars);
        if (outv < 0) {
            return emit_const_i64_instr(sf, 0);
        }
        return outv;
    }

    case CC_EXPR_TERNARY: {
        long cond_const = 0;
        int cond;
        int cond_raw;
        int dst;
        int l_true = new_label(sf);
        int l_false = new_label(sf);
        int l_end = new_label(sf);
        cc_value_type_t want;
        int tv;
        int fv;

        if (e->lhs == NULL || e->third == NULL) {
            set_diag(diag, "malformed conditional expression in lowering");
            return -1;
        }

        if (eval_const_i64_expr(tu, e->lhs, &cond_const) == 0) {
            int sv;
            cc_value_type_t want = type_to_val(e->value_type);
            if (cond_const != 0) {
                if (e->rhs == NULL) {
                    sv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
                } else {
                    sv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
                }
            } else {
                sv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->third, diag);
            }
            if (sv < 0) {
                return -1;
            }
            return cast_value(sf, sv, want, diag);
        }

        cond_raw = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        if (cond_raw < 0) {
            return -1;
        }
        cond = lower_truthy_value(sf, cond_raw, diag);
        if (cond < 0) {
            return -1;
        }

        want = type_to_val(e->value_type);
        dst = new_value(sf, want);
        if (dst < 0) {
            set_diag(diag, "out of memory assigning ternary result");
            return -1;
        }

        if (emit_br_cond_instr(sf, cond, l_true, l_false) != 0) {
            return -1;
        }
        if (emit_label_instr(sf, l_true) != 0) {
            return -1;
        }
        if (e->rhs == NULL) {
            tv = cond_raw;
        } else {
            tv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
            if (tv < 0) {
                return -1;
            }
        }
        tv = cast_value(sf, tv, want, diag);
        if (tv < 0 || emit_mov_instr(sf, dst, tv) != 0 || emit_br_instr(sf, l_end) != 0) {
            return -1;
        }

        if (emit_label_instr(sf, l_false) != 0) {
            return -1;
        }
        fv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->third, diag);
        if (fv < 0) {
            return -1;
        }
        fv = cast_value(sf, fv, want, diag);
        if (fv < 0 || emit_mov_instr(sf, dst, fv) != 0 || emit_br_instr(sf, l_end) != 0) {
            return -1;
        }

        if (emit_label_instr(sf, l_end) != 0) {
            return -1;
        }
        return dst;
    }

    default:
        set_diag(diag, "unsupported expression kind in AST->SSA lowering");
        return -1;
    }
}

static int emit_label_instr(cc_ssa_function_t *sf, int label) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LABEL;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.label = label;
    return push_instr(sf, in);
}

static int emit_br_instr(cc_ssa_function_t *sf, int label) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_BR;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.label = label;
    return push_instr(sf, in);
}

static int emit_br_cond_instr(cc_ssa_function_t *sf, int cond, int label_true, int label_false) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_BR_COND;
    in.dst = -1;
    in.lhs = cond;
    in.rhs = -1;
    in.true_label = label_true;
    in.false_label = label_false;
    return push_instr(sf, in);
}

static int emit_trap_instr(cc_ssa_function_t *sf) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_TRAP;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    return push_instr(sf, in);
}

static int emit_mov_instr(cc_ssa_function_t *sf, int dst, int src) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_MOV;
    in.dst = dst;
    in.lhs = src;
    in.rhs = -1;
    return push_instr(sf, in);
}

static int emit_const_i64_instr(cc_ssa_function_t *sf, long v) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CONST;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = -1;
    in.rhs = -1;
    in.imm = v;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        return -1;
    }
    return in.dst;
}

static int emit_const_f64_instr(cc_ssa_function_t *sf, double v) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CONST;
    in.dst = new_value(sf, CC_VAL_F64);
    in.lhs = -1;
    in.rhs = -1;
    in.fimm = v;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        return -1;
    }
    return in.dst;
}

static int emit_memcpy_instr(cc_ssa_function_t *sf, int dst_ptr, int src_ptr, long size, cc_diag_t *diag) {
    cc_ssa_instr_t call_in;
    int sizev;

    if (size <= 0) {
        set_diag(diag, "invalid memcpy size in lowering");
        return -1;
    }
    sizev = emit_const_i64_instr(sf, size);
    if (sizev < 0) {
        return -1;
    }

    memset(&call_in, 0, sizeof(call_in));
    call_in.op = CC_SSA_CALL;
    call_in.call_is_variadic = 0;
    call_in.sym = xstrdup("memcpy");
    call_in.arg_count = 3;
    call_in.args = (int *)calloc(3, sizeof(*call_in.args));
    call_in.dst = new_value(sf, CC_VAL_I64);
    if (call_in.sym == NULL || call_in.args == NULL || call_in.dst < 0) {
        free(call_in.sym);
        free(call_in.args);
        set_diag(diag, "out of memory emitting memcpy call");
        return -1;
    }
    call_in.args[0] = dst_ptr;
    call_in.args[1] = src_ptr;
    call_in.args[2] = sizev;
    if (push_instr(sf, call_in) != 0) {
        free(call_in.sym);
        free(call_in.args);
        set_diag(diag, "out of memory appending memcpy call");
        return -1;
    }
    return 0;
}

static int lower_truthy_value(cc_ssa_function_t *sf, int v, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int zero;

    if (value_type(sf, v) == CC_VAL_F64) {
        zero = emit_const_f64_instr(sf, 0.0);
    } else {
        v = cast_value(sf, v, CC_VAL_I64, diag);
        if (v < 0) {
            return -1;
        }
        zero = emit_const_i64_instr(sf, 0);
    }
    if (zero < 0) {
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CMP;
    in.cmp_kind = CC_CMP_NE;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = v;
    in.rhs = zero;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        return -1;
    }
    return in.dst;
}

typedef struct {
    const cc_stmt_t *stmt;
    long value;
    long value_hi;
    int has_range;
    int label;
    int is_default;
} switch_case_site_t;

static const switch_case_site_t *g_switch_sites = NULL;
static size_t g_switch_site_count = 0;

static int append_switch_case_site(cc_ssa_function_t *sf, const cc_stmt_t *st, switch_case_site_t **sites,
                                   size_t *count, int *default_label, cc_diag_t *diag) {
    switch_case_site_t *next;
    switch_case_site_t site;
    if (st == NULL || (st->kind != CC_STMT_CASE && st->kind != CC_STMT_DEFAULT)) {
        return 0;
    }
    if (st->kind == CC_STMT_CASE && st->expr == NULL) {
        set_diag(diag, "malformed case label during lowering");
        return -1;
    }
    if (st->kind == CC_STMT_DEFAULT && *default_label >= 0) {
        set_diag(diag, "duplicate default labels in switch");
        return -1;
    }
    memset(&site, 0, sizeof(site));
    site.stmt = st;
    site.value = st->kind == CC_STMT_CASE ? st->expr->int_val : 0;
    site.value_hi = st->kind == CC_STMT_CASE ? (st->case_has_range ? st->case_hi : st->expr->int_val) : 0;
    site.has_range = st->kind == CC_STMT_CASE ? st->case_has_range : 0;
    site.label = new_label(sf);
    site.is_default = (st->kind == CC_STMT_DEFAULT);
    if (site.label < 0) {
        set_diag(diag, "out of memory assigning switch label");
        return -1;
    }
    next = (switch_case_site_t *)realloc(*sites, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        set_diag(diag, "out of memory collecting switch labels");
        return -1;
    }
    *sites = next;
    (*sites)[(*count)++] = site;
    if (site.is_default) {
        *default_label = site.label;
    }
    return 0;
}

static int collect_switch_case_sites_stmt(cc_ssa_function_t *sf, const cc_stmt_t *st, switch_case_site_t **sites,
                                          size_t *count, int *default_label, cc_diag_t *diag) {
    size_t i;
    if (st == NULL) {
        return 0;
    }
    if (append_switch_case_site(sf, st, sites, count, default_label, diag) != 0) {
        return -1;
    }
    switch (st->kind) {
    case CC_STMT_BLOCK:
        for (i = 0; i < st->block_count; ++i) {
            if (collect_switch_case_sites_stmt(sf, &st->block_stmts[i], sites, count, default_label, diag) != 0) {
                return -1;
            }
        }
        return 0;
    case CC_STMT_LABEL:
    case CC_STMT_CASE:
    case CC_STMT_DEFAULT:
        return collect_switch_case_sites_stmt(sf, st->then_branch, sites, count, default_label, diag);
    case CC_STMT_IF:
        if (collect_switch_case_sites_stmt(sf, st->then_branch, sites, count, default_label, diag) != 0) {
            return -1;
        }
        return collect_switch_case_sites_stmt(sf, st->else_branch, sites, count, default_label, diag);
    case CC_STMT_WHILE:
    case CC_STMT_DO:
        return collect_switch_case_sites_stmt(sf, st->then_branch, sites, count, default_label, diag);
    case CC_STMT_FOR:
        return collect_switch_case_sites_stmt(sf, st->then_branch, sites, count, default_label, diag);
    case CC_STMT_SWITCH:
        return 0;
    default:
        return 0;
    }
}

static int collect_switch_case_sites(cc_ssa_function_t *sf, const cc_stmt_t *body, switch_case_site_t **out_sites,
                                     size_t *out_count, int *out_default_label, cc_diag_t *diag) {
    switch_case_site_t *sites = NULL;
    size_t count = 0;
    int default_label = -1;
    if (body == NULL) {
        set_diag(diag, "switch lowering requires body");
        return -1;
    }
    if (collect_switch_case_sites_stmt(sf, body, &sites, &count, &default_label, diag) != 0) {
        free(sites);
        return -1;
    }
    *out_sites = sites;
    *out_count = count;
    *out_default_label = default_label;
    return 0;
}

static int find_switch_label_for_stmt(const switch_case_site_t *sites, size_t count, const cc_stmt_t *stmt) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (sites[i].stmt == stmt) {
            return sites[i].label;
        }
    }
    return -1;
}

static int lower_struct_init_to_ptr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                    var_entry_t *vars, size_t var_count, int depth, int base_ptr, int struct_id,
                                    const cc_expr_t *init, cc_diag_t *diag);
static int find_struct_member_index_by_name(const cc_struct_def_t *sd, const char *name);
static size_t struct_next_init_member_index(const cc_struct_def_t *sd, size_t member_idx);

static const cc_expr_t *unwrap_scalar_initializer_expr(const cc_expr_t *e, cc_diag_t *diag) {
    if (e == NULL) {
        set_diag(diag, "missing scalar initializer expression");
        return NULL;
    }
    if (e->kind != CC_EXPR_INIT_LIST) {
        return e;
    }
    if (e->arg_count >= 1 && e->args != NULL && e->args[0] != NULL) {
        return e->args[0];
    }
    set_diag(diag, "scalar initializer list is empty");
    return NULL;
}

static const cc_expr_t *selected_generic_expr(const cc_expr_t *e) {
    if (e == NULL || e->kind != CC_EXPR_GENERIC) {
        return NULL;
    }
    if (e->generic_selected < 0 || e->args == NULL || (size_t)e->generic_selected >= e->arg_count) {
        return NULL;
    }
    return e->args[e->generic_selected];
}

static int is_zero_initializer_expr(const cc_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    switch (e->kind) {
    case CC_EXPR_INT:
        return e->int_val == 0;
    case CC_EXPR_FLOAT:
        return e->float_val == 0.0;
    case CC_EXPR_CAST:
        return is_zero_initializer_expr(e->lhs);
    case CC_EXPR_BIN:
        return e->op == CC_BIN_COMMA ? is_zero_initializer_expr(e->rhs) : 0;
    case CC_EXPR_INIT_LIST:
        if (e->arg_count == 0) {
            return 1;
        }
        if (e->arg_count == 1) {
            return is_zero_initializer_expr(e->args[0]);
        }
        return 0;
    case CC_EXPR_GENERIC: {
        const cc_expr_t *sel = selected_generic_expr(e);
        return sel != NULL ? is_zero_initializer_expr(sel) : 0;
    }
    default:
        return 0;
    }
}

static int expr_matches_struct_value(const cc_expr_t *e, int struct_id) {
    if (e == NULL) {
        return 0;
    }
    if (e->value_type == CC_TYPE_VOID && e->struct_id == struct_id) {
        return 1;
    }
    if (e->kind == CC_EXPR_CAST && e->aux_type == CC_TYPE_VOID && e->aux_struct_id == struct_id && e->lhs != NULL &&
        e->lhs->value_type == CC_TYPE_VOID && e->lhs->struct_id == struct_id) {
        return 1;
    }
    return 0;
}

static size_t estimate_struct_init_consumed(const cc_translation_unit_t *tu, int struct_id, const cc_expr_t *list,
                                            size_t start, int depth) {
    const cc_struct_def_t *sd;
    size_t i = start;
    size_t next_member = 0;

    if (tu == NULL || list == NULL || list->kind != CC_EXPR_INIT_LIST || struct_id < 0 ||
        (size_t)struct_id >= tu->struct_count || depth > 32) {
        return 1;
    }
    sd = &tu->structs[struct_id];
    if (sd->is_union) {
        return (i < list->arg_count) ? 1 : 0;
    }
    while (next_member < sd->member_count && i < list->arg_count) {
        const cc_expr_t *raw = list->args[i];
        const cc_expr_t *item = raw;
        size_t member_idx = next_member;
        const cc_struct_member_t *m;
        size_t consumed;
        long max_items;
        long elem_size;

        if (raw != NULL && raw->kind == CC_EXPR_MEMBER && raw->lhs == NULL && raw->rhs != NULL && raw->ident != NULL) {
            int didx = find_struct_member_index_by_name(sd, raw->ident);
            if (didx < 0) {
                break;
            }
            member_idx = (size_t)didx;
            item = raw->rhs;
        } else if (raw != NULL && raw->kind == CC_EXPR_MEMBER && raw->lhs == NULL && raw->ident != NULL && i > start) {
            break;
        }
        if (member_idx >= sd->member_count) {
            break;
        }
        m = &sd->members[member_idx];
        next_member = struct_next_init_member_index(sd, member_idx);
        if (sd->has_flexible_array && member_idx + 1 == sd->member_count) {
            i++;
            continue;
        }
        if (m->type == CC_TYPE_VOID && m->type_struct_id >= 0) {
            if (item != NULL && item->kind == CC_EXPR_INIT_LIST) {
                i++;
                continue;
            }
            if (item != NULL && expr_matches_struct_value(item, m->type_struct_id)) {
                i++;
                continue;
            }
            consumed = estimate_struct_init_consumed(tu, m->type_struct_id, list, i, depth + 1);
            if (consumed == 0) {
                consumed = 1;
            }
            i += consumed;
            continue;
        }
        if (is_array_object_decl(m->type, m->array_len, m->array_ndim)) {
            if (item != NULL && (item->kind == CC_EXPR_INIT_LIST || item->kind == CC_EXPR_STR)) {
                i++;
                continue;
            }
            elem_size = array_decl_scalar_size_bytes(tu, m->type, m->type_struct_id, m->array_ndim);
            if (elem_size <= 0) {
                elem_size = pointer_elem_size_bytes(tu, m->type, m->type_struct_id);
            }
            max_items = m->array_len > 0 ? m->array_len : (elem_size > 0 ? (m->size / elem_size) : 1);
            if (max_items <= 0) {
                max_items = 1;
            }
            consumed = 1;
            while (consumed < (size_t)max_items && i + consumed < list->arg_count) {
                const cc_expr_t *cand = list->args[i + consumed];
                if (cand != NULL && cand->kind == CC_EXPR_INIT_LIST) {
                    break;
                }
                if (cand != NULL && cand->kind == CC_EXPR_MEMBER && cand->lhs == NULL && cand->ident != NULL) {
                    break;
                }
                consumed++;
            }
            i += consumed;
            continue;
        }
        i++;
    }
    if (i <= start) {
        return 1;
    }
    return i - start;
}

static int lower_store_string_to_fixed_array_ptr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf,
                                                 const lower_ctx_t *ctx, var_entry_t *vars, size_t var_count,
                                                 int depth, int field_ptr, const cc_struct_member_t *m,
                                                 const cc_expr_t *item, cc_diag_t *diag) {
    cc_type_t elem_type;
    long elem_size;
    long max_items;
    unsigned long *units = NULL;
    size_t unit_count = 0;
    size_t i;
    int wide;
    (void)ctx;
    (void)vars;
    (void)var_count;
    (void)depth;

    if (m == NULL || item == NULL || item->kind != CC_EXPR_STR) {
        return -1;
    }
    elem_type = ptr_base_type(m->type);
    elem_size = array_decl_scalar_size_bytes(tu, m->type, m->type_struct_id, m->array_ndim);
    if (elem_size <= 0) {
        elem_size = pointer_elem_size_bytes(tu, m->type, m->type_struct_id);
    }
    if (elem_size <= 0 || m->size <= 0) {
        return -1;
    }
    max_items = m->array_len > 0 ? m->array_len : (m->size / elem_size);
    if (max_items <= 0) {
        return -1;
    }
    wide = (item->aux_type == CC_TYPE_INT || item->aux_type == CC_TYPE_UINT || item->aux_type == CC_TYPE_LONG_LONG ||
            item->aux_type == CC_TYPE_ULONG_LONG);
    if (!wide && !(elem_type == CC_TYPE_CHAR || elem_type == CC_TYPE_UCHAR)) {
        return -1;
    }
    if (wide && !is_integral_type(elem_type)) {
        return -1;
    }
    if (decode_string_units(item, wide, &units, &unit_count) != 0) {
        set_diag(diag, "failed to decode string initializer");
        return -1;
    }
    if ((long)unit_count > max_items) {
        free(units);
        set_diag(diag, "string initializer exceeds fixed-size struct member array");
        return -1;
    }
    for (i = 0; i < unit_count + (unit_count < (size_t)max_items ? 1 : 0); ++i) {
        cc_ssa_instr_t in;
        int elem_ptr = field_ptr;
        int cval;
        if (i > 0) {
            int offv = emit_const_i64_instr(sf, (long)i * elem_size);
            if (offv < 0) {
                free(units);
                return -1;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_ADD;
            in.dst = new_value(sf, CC_VAL_I64);
            in.lhs = field_ptr;
            in.rhs = offv;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                free(units);
                set_diag(diag, "out of memory computing struct string element address");
                return -1;
            }
            elem_ptr = in.dst;
        }
                        cval = emit_const_i64_instr(sf, i < unit_count ? (long)units[i] : 0);
        if (cval < 0) {
            free(units);
            return -1;
        }
        cval = cast_value(sf, cval, type_to_val(elem_type), diag);
        if (cval < 0) {
            free(units);
            return -1;
        }
        memset(&in, 0, sizeof(in));
        in.op = CC_SSA_STORE;
        in.dst = -1;
        in.lhs = elem_ptr;
        in.rhs = cval;
        in.imm = elem_size;
        if (push_instr(sf, in) != 0) {
            free(units);
            set_diag(diag, "out of memory storing struct string initializer element");
            return -1;
        }
    }
    free(units);
    return 0;
}

static int lower_struct_array_member_init_to_ptr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf,
                                                 const lower_ctx_t *ctx, var_entry_t *vars, size_t var_count,
                                                 int depth, int field_ptr, const cc_struct_member_t *m,
                                                 const cc_expr_t *item, cc_diag_t *diag) {
    cc_type_t elem_type;
    long elem_size;
    long max_items;
    size_t ii;

    if (m == NULL || item == NULL || item->kind != CC_EXPR_INIT_LIST || !is_pointer_type(m->type)) {
        set_diag(diag, "invalid array-like struct member initializer");
        return -1;
    }

    elem_type = ptr_base_type(m->type);
    elem_size = array_decl_scalar_size_bytes(tu, m->type, m->type_struct_id, m->array_ndim);
    if (elem_size <= 0) {
        elem_size = pointer_elem_size_bytes(tu, m->type, m->type_struct_id);
    }
    if (elem_size <= 0 || m->size <= 0) {
        set_diag(diag, "unsupported array-like struct member size in initializer lowering");
        return -1;
    }
    max_items = m->array_len > 0 ? m->array_len : (m->size / elem_size);
    if (max_items <= 0) {
        set_diag(diag, "invalid array-like struct member extent in initializer lowering");
        return -1;
    }
    if (item->arg_count > (size_t)max_items) {
        set_diag(diag, "too many initializers for fixed-size struct member array");
        return -1;
    }

    for (ii = 0; ii < item->arg_count; ++ii) {
        int elem_ptr = field_ptr;
        cc_ssa_instr_t in;

        if (ii > 0) {
            int offv = emit_const_i64_instr(sf, (long)ii * elem_size);
            if (offv < 0) {
                return -1;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_ADD;
            in.dst = new_value(sf, CC_VAL_I64);
            in.lhs = field_ptr;
            in.rhs = offv;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory computing array-like struct member element address");
                return -1;
            }
            elem_ptr = in.dst;
        }

        if (elem_type == CC_TYPE_VOID && m->type_struct_id >= 0) {
            if (item->args[ii]->kind != CC_EXPR_INIT_LIST) {
                if (is_zero_initializer_expr(item->args[ii])) {
                    continue;
                }
                set_diag(diag, "struct array member initializer element requires braces");
                return -1;
            }
            if (lower_struct_init_to_ptr(tu, sf, ctx, vars, var_count, depth, elem_ptr, m->type_struct_id,
                                         item->args[ii], diag) != 0) {
                return -1;
            }
        } else {
            int v;
            const cc_expr_t *elem_expr = unwrap_scalar_initializer_expr(item->args[ii], diag);
            if (elem_expr == NULL) {
                return -1;
            }
            v = lower_expr(tu, sf, ctx, vars, var_count, depth, elem_expr, diag);
            if (v < 0) {
                return -1;
            }
            v = cast_value(sf, v, type_to_val(elem_type), diag);
            if (v < 0) {
                return -1;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_STORE;
            in.dst = -1;
            in.lhs = elem_ptr;
            in.rhs = v;
            in.imm = elem_size;
            if (push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory storing array-like struct member initializer element");
                return -1;
            }
        }
    }

    return 0;
}

static int lower_struct_init_to_ptr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                    var_entry_t *vars, size_t var_count, int depth, int base_ptr, int struct_id,
                                    const cc_expr_t *init, cc_diag_t *diag) {
    const cc_struct_def_t *sd;
    size_t i;
    size_t next_member = 0;

    if (tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count) {
        set_diag(diag, "invalid struct type during initializer lowering");
        return -1;
    }
    if (init == NULL || init->kind != CC_EXPR_INIT_LIST) {
        set_diag(diag, "struct initializer lowering requires initializer list");
        return -1;
    }

    sd = &tu->structs[struct_id];
    if (sd->is_union && init->arg_count > 1) {
        set_diag(diag, "too many initializers for union");
        return -1;
    }
    for (i = 0; i < init->arg_count; ++i) {
        const cc_expr_t *raw = init->args[i];
        const cc_expr_t *item = raw;
        size_t member_idx = next_member;
        const cc_struct_member_t *m;
        int field_ptr = base_ptr;
        cc_ssa_instr_t in;

        if (raw != NULL && raw->kind == CC_EXPR_MEMBER && raw->lhs == NULL && raw->rhs != NULL && raw->ident != NULL) {
            int didx = find_struct_member_index_by_name(sd, raw->ident);
            if (didx < 0) {
                set_diag(diag, "unknown designated struct member in local initializer");
                return -1;
            }
            member_idx = (size_t)didx;
            item = raw->rhs;
        }
        if (member_idx >= sd->member_count) {
            break;
        }
        m = &sd->members[member_idx];
        if (!sd->is_union) {
            next_member = struct_next_init_member_index(sd, member_idx);
        }
        if (sd->has_flexible_array && member_idx + 1 == sd->member_count) {
            /* GNU-compatible extension: accept and ignore flexible-array initializers. */
            continue;
        }
        if (m->offset != 0) {
            int offv = emit_const_i64_instr(sf, m->offset);
            if (offv < 0) {
                return -1;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_ADD;
            in.dst = new_value(sf, CC_VAL_I64);
            in.lhs = base_ptr;
            in.rhs = offv;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory computing struct member address");
                return -1;
            }
            field_ptr = in.dst;
        }

        if (m->type == CC_TYPE_VOID && m->type_struct_id >= 0) {
            const cc_expr_t *nested_item = item;
            if (item->kind == CC_EXPR_CAST && item->aux_type == CC_TYPE_VOID && item->aux_struct_id == m->type_struct_id &&
                item->lhs != NULL && item->lhs->kind == CC_EXPR_INIT_LIST) {
                nested_item = item->lhs;
            }
            nested_item = unwrap_self_designated_init_list(nested_item, m->name);
            if (nested_item->kind != CC_EXPR_INIT_LIST) {
                if (nested_item->value_type == CC_TYPE_VOID && nested_item->struct_id == m->type_struct_id) {
                    int rhsv;
                    long sub_sz = type_size_bytes_with_struct(tu, CC_TYPE_VOID, m->type_struct_id);
                    const cc_expr_t *copy_expr = nested_item;
                    if (copy_expr->kind == CC_EXPR_CAST && copy_expr->aux_type == CC_TYPE_VOID &&
                        copy_expr->aux_struct_id == m->type_struct_id && copy_expr->lhs != NULL &&
                        copy_expr->lhs->value_type == CC_TYPE_VOID && copy_expr->lhs->struct_id == m->type_struct_id) {
                        copy_expr = copy_expr->lhs;
                    }
                    if (sub_sz <= 0) {
                        sub_sz = 1;
                    }
                    rhsv = lower_expr(tu, sf, ctx, vars, var_count, depth, copy_expr, diag);
                    if (rhsv < 0) {
                        return -1;
                    }
                    if (emit_memcpy_instr(sf, field_ptr, rhsv, sub_sz, diag) != 0) {
                        return -1;
                    }
                    continue;
                }
                size_t consumed = 0;
                const cc_expr_t **tmp_args;
                cc_expr_t tmp_list;

                consumed = estimate_struct_init_consumed(tu, m->type_struct_id, init, i, 0);
                if (consumed == 0) {
                    consumed = 1;
                }
                tmp_args = (const cc_expr_t **)calloc(consumed, sizeof(*tmp_args));
                if (tmp_args == NULL) {
                    set_diag(diag, "out of memory collecting brace-elided aggregate initializers");
                    return -1;
                }
                {
                    size_t ci;
                    for (ci = 0; ci < consumed && i + ci < init->arg_count; ++ci) {
                        tmp_args[ci] = init->args[i + ci];
                    }
                }
                memset(&tmp_list, 0, sizeof(tmp_list));
                tmp_list.kind = CC_EXPR_INIT_LIST;
                tmp_list.arg_count = consumed;
                tmp_list.args = (cc_expr_t **)tmp_args;
                if (lower_struct_init_to_ptr(tu, sf, ctx, vars, var_count, depth, field_ptr, m->type_struct_id,
                                             &tmp_list, diag) != 0) {
                    free(tmp_args);
                    return -1;
                }
                free(tmp_args);
                i += consumed - 1;
                continue;
            }
            if (lower_struct_init_to_ptr(tu, sf, ctx, vars, var_count, depth, field_ptr, m->type_struct_id, nested_item,
                                         diag) != 0) {
                return -1;
            }
        } else {
            int v;
            const cc_expr_t *item_expr = item;
            long mem_size = type_size_bytes_with_struct(tu, m->type, m->type_struct_id);
            if (mem_size <= 0) {
                mem_size = m->size;
            }
            if (mem_size <= 0) {
                set_diag(diag, "unsupported struct member size in initializer lowering");
                return -1;
            }
            if (item->kind == CC_EXPR_INIT_LIST) {
                if (is_array_object_decl(m->type, m->array_len, m->array_ndim)) {
                    if (lower_struct_array_member_init_to_ptr(tu, sf, ctx, vars, var_count, depth, field_ptr, m, item,
                                                              diag) != 0) {
                        return -1;
                    }
                    continue;
                }
                item_expr = unwrap_scalar_initializer_expr(item, diag);
                if (item_expr == NULL) {
                    return -1;
                }
            } else if (is_array_object_decl(m->type, m->array_len, m->array_ndim) && item->kind == CC_EXPR_STR) {
                if (lower_store_string_to_fixed_array_ptr(tu, sf, ctx, vars, var_count, depth, field_ptr, m, item,
                                                          diag) != 0) {
                    return -1;
                }
                continue;
            } else if (is_array_object_decl(m->type, m->array_len, m->array_ndim)) {
                size_t consumed = 1;
                long elem_size = array_decl_scalar_size_bytes(tu, m->type, m->type_struct_id, m->array_ndim);
                long max_items;
                const cc_expr_t **tmp_args;
                cc_expr_t tmp_list;

                if (elem_size <= 0) {
                    elem_size = pointer_elem_size_bytes(tu, m->type, m->type_struct_id);
                }
                max_items = m->array_len > 0 ? m->array_len : (elem_size > 0 ? (m->size / elem_size) : 1);
                if (max_items <= 0) {
                    max_items = 1;
                }
                while (consumed < (size_t)max_items && i + consumed < init->arg_count) {
                    const cc_expr_t *cand = init->args[i + consumed];
                    if (cand != NULL && cand->kind == CC_EXPR_INIT_LIST) {
                        break;
                    }
                    if (cand != NULL && cand->kind == CC_EXPR_MEMBER && cand->lhs == NULL && cand->ident != NULL) {
                        break;
                    }
                    consumed++;
                }
                tmp_args = (const cc_expr_t **)calloc(consumed, sizeof(*tmp_args));
                if (tmp_args == NULL) {
                    set_diag(diag, "out of memory collecting brace-elided array member initializers");
                    return -1;
                }
                {
                    size_t ci;
                    for (ci = 0; ci < consumed; ++ci) {
                        tmp_args[ci] = init->args[i + ci];
                    }
                }
                memset(&tmp_list, 0, sizeof(tmp_list));
                tmp_list.kind = CC_EXPR_INIT_LIST;
                tmp_list.arg_count = consumed;
                tmp_list.args = (cc_expr_t **)tmp_args;
                if (lower_struct_array_member_init_to_ptr(tu, sf, ctx, vars, var_count, depth, field_ptr, m, &tmp_list,
                                                          diag) != 0) {
                    free(tmp_args);
                    return -1;
                }
                free(tmp_args);
                i += consumed - 1;
                continue;
            }
            v = lower_expr(tu, sf, ctx, vars, var_count, depth, item_expr, diag);
            if (v < 0) {
                return -1;
            }
            v = cast_value(sf, v, type_to_val(m->type), diag);
            if (v < 0) {
                return -1;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_STORE;
            in.dst = -1;
            in.lhs = field_ptr;
            in.rhs = v;
            in.imm = mem_size;
            if (push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory storing struct initializer member");
                return -1;
            }
        }
    }
    return 0;
}

static int lower_stmt(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, var_entry_t **vars, size_t *var_count,
                      const lower_ctx_t *ctx, int depth, int break_label, int continue_label, const cc_stmt_t *s,
                      int *saw_ret,
                      cc_diag_t *diag) {
    size_t j;

    if (s->kind == CC_STMT_DECL) {
        int v;
        int varv;
        if ((s->storage & CC_STORAGE_STATIC) != 0 && s->decl_name != NULL) {
            cc_ssa_global_t g;
            cc_ssa_instr_t in;
            char sym[256];
            int have_const_init = 0;
            int needs_runtime_init = 0;
            int is_array_obj = is_array_object_decl(s->type, s->array_len, s->array_ndim);
            int is_struct_obj = (s->type == CC_TYPE_VOID && s->type_struct_id >= 0);
            cc_type_t elem_type = CC_TYPE_VOID;
            long elem_size = 0;
            long arr_elems = 1;
            long init_i = 0;
            double init_f = 0.0;
            int init_is_float = 0;
            char *init_sym = NULL;
            long obj_size;
            if (ctx == NULL || ctx->mod == NULL) {
                set_diag(diag, "internal error: missing module context for static local");
                return -1;
            }
            if (make_local_static_symbol(sym, sizeof(sym), ctx->fn, s->decl_name, ctx->mod->global_count, "") != 0) {
                set_diag(diag, "failed to construct static local symbol");
                return -1;
            }
            if (is_array_obj) {
                elem_type = ptr_base_type(s->type);
                elem_size = array_decl_scalar_size_bytes(tu, s->type, s->type_struct_id, s->array_ndim);
                if (elem_size <= 0) {
                    elem_size = type_size_bytes_with_struct(tu, elem_type, s->type_struct_id);
                }
                if (elem_size <= 0 && elem_type == CC_TYPE_VOID && s->type_struct_id >= 0) {
                    elem_size = 1;
                }
                if (elem_size <= 0) {
                    set_diag(diag, "unsupported static local array element type");
                    return -1;
                }
                if (s->array_len > 0) {
                    arr_elems = s->array_len;
                }
                obj_size = elem_size * arr_elems;
            } else {
                obj_size = type_size_bytes_with_struct(tu, s->type, s->type_struct_id);
                if (obj_size <= 0 && is_struct_obj) {
                    obj_size = 1;
                }
            }
            if (obj_size <= 0) {
                set_diag(diag, "unsupported static local declaration type");
                return -1;
            }
            if (!is_array_obj && !is_struct_obj) {
                if (s->expr != NULL &&
                    eval_global_init_expr(tu, s->expr, &init_i, &init_f, &init_is_float, &init_sym) == 0) {
                    have_const_init = 1;
                } else if (s->expr != NULL) {
                    needs_runtime_init = 1;
                }
            } else if (s->expr != NULL && !is_zero_initializer_expr(s->expr)) {
                needs_runtime_init = 1;
            }

            memset(&g, 0, sizeof(g));
            g.name = xstrdup(sym);
            g.type = s->type;
            g.type_struct_id = s->type_struct_id;
            g.array_len = is_array_obj ? s->array_len : -1;
            g.size_bytes = obj_size;
            g.storage = CC_STORAGE_STATIC;
            g.attr_flags = s->attr_flags & (CC_ATTR_PACKED | CC_ATTR_ALIGNED | CC_ATTR_SECTION);
            g.attr_align = s->attr_align;
            if (s->attr_section != NULL) {
                g.attr_section = xstrdup(s->attr_section);
            }
            g.has_init = have_const_init;
            g.init_i = init_i;
            g.init_f = init_f;
            g.init_is_float = init_is_float;
            g.init_is_symbol = init_sym != NULL;
            g.init_sym = init_sym;
            if (g.name == NULL || (s->attr_section != NULL && g.attr_section == NULL)) {
                free(g.name);
                free(g.attr_section);
                free(g.init_sym);
                set_diag(diag, "out of memory creating static local symbol");
                return -1;
            }
            if (append_synth_global(ctx->mod, &g) != 0) {
                free(g.name);
                free(g.attr_section);
                free(g.init_sym);
                set_diag(diag, "out of memory appending static local global");
                return -1;
            }

            if (needs_runtime_init) {
                cc_ssa_global_t guard;
                char guard_sym[256];
                int dst_addr;
                int guard_addr;
                int guard_val;
                int one;
                int cond;
                int l_init = new_label(sf);
                int l_done = new_label(sf);
                if (make_local_static_symbol(guard_sym, sizeof(guard_sym), ctx->fn, s->decl_name, ctx->mod->global_count,
                                             "__guard") != 0) {
                    set_diag(diag, "failed to construct static local guard symbol");
                    return -1;
                }
                memset(&guard, 0, sizeof(guard));
                guard.name = xstrdup(guard_sym);
                guard.type = CC_TYPE_INT;
                guard.type_struct_id = -1;
                guard.array_len = -1;
                guard.size_bytes = 4;
                guard.storage = CC_STORAGE_STATIC;
                guard.has_init = 0;
                if (guard.name == NULL || append_synth_global(ctx->mod, &guard) != 0) {
                    free(guard.name);
                    set_diag(diag, "out of memory appending static local guard");
                    return -1;
                }
                guard_addr = emit_global_addr(sf, guard_sym, diag);
                if (guard_addr < 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_LOAD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = guard_addr;
                in.rhs = -1;
                in.imm = 4;
                in.is_unsigned = 1;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory loading static local guard");
                    return -1;
                }
                guard_val = in.dst;
                cond = lower_truthy_value(sf, guard_val, diag);
                if (cond < 0) {
                    return -1;
                }
                if (emit_br_cond_instr(sf, cond, l_done, l_init) != 0) {
                    return -1;
                }
                if (emit_label_instr(sf, l_init) != 0) {
                    return -1;
                }
                dst_addr = emit_global_addr(sf, sym, diag);
                if (dst_addr < 0) {
                    return -1;
                }
                if (is_struct_obj) {
                    if (s->expr != NULL && s->expr->kind == CC_EXPR_INIT_LIST) {
                        if (lower_struct_init_to_ptr(tu, sf, ctx, *vars, *var_count, depth, dst_addr,
                                                     s->type_struct_id, s->expr, diag) != 0) {
                            return -1;
                        }
                    } else {
                        v = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
                        if (v < 0) {
                            return -1;
                        }
                        if (emit_memcpy_instr(sf, dst_addr, v, obj_size, diag) != 0) {
                            return -1;
                        }
                    }
                } else if (is_array_obj) {
                    if (s->expr != NULL && s->expr->kind == CC_EXPR_INIT_LIST) {
                        size_t ii;
                        int handled_string_init = 0;
                        if (s->expr->arg_count > (size_t)arr_elems) {
                            set_diag(diag, "too many initializers for static local array");
                            return -1;
                        }
                        if (s->expr->arg_count == 1) {
                            const cc_expr_t *item_expr = unwrap_scalar_initializer_expr(s->expr->args[0], diag);
                            if (item_expr == NULL) {
                                return -1;
                            }
                            if (item_expr->kind == CC_EXPR_STR) {
                                unsigned long *units = NULL;
                                size_t unit_count = 0;
                                size_t si;
                                int wide = (item_expr->aux_type == CC_TYPE_INT || item_expr->aux_type == CC_TYPE_UINT ||
                                            item_expr->aux_type == CC_TYPE_LONG_LONG ||
                                            item_expr->aux_type == CC_TYPE_ULONG_LONG);
                                if (decode_string_units(item_expr, wide, &units, &unit_count) != 0) {
                                    set_diag(diag, "failed to decode static local array string initializer");
                                    return -1;
                                }
                                if ((long)unit_count > arr_elems) {
                                    free(units);
                                    set_diag(diag, "string initializer exceeds static local array size");
                                    return -1;
                                }
                                for (si = 0; si < unit_count + (unit_count < (size_t)arr_elems ? 1 : 0); ++si) {
                                    int item_ptr = dst_addr;
                                    cc_ssa_instr_t st_in;
                                    int cval;
                                    if (si > 0) {
                                        int offv = emit_const_i64_instr(sf, (long)si * elem_size);
                                        if (offv < 0) {
                                            free(units);
                                            return -1;
                                        }
                                        memset(&st_in, 0, sizeof(st_in));
                                        st_in.op = CC_SSA_ADD;
                                        st_in.dst = new_value(sf, CC_VAL_I64);
                                        st_in.lhs = dst_addr;
                                        st_in.rhs = offv;
                                        if (st_in.dst < 0 || push_instr(sf, st_in) != 0) {
                                            free(units);
                                            set_diag(diag,
                                                     "out of memory computing static local array string element address");
                                            return -1;
                                        }
                                        item_ptr = st_in.dst;
                                    }
                                    cval = emit_const_i64_instr(sf, si < unit_count ? (long)units[si] : 0);
                                    if (cval < 0) {
                                        free(units);
                                        return -1;
                                    }
                                    cval = cast_value(sf, cval, type_to_val(elem_type), diag);
                                    if (cval < 0) {
                                        free(units);
                                        return -1;
                                    }
                                    memset(&st_in, 0, sizeof(st_in));
                                    st_in.op = CC_SSA_STORE;
                                    st_in.dst = -1;
                                    st_in.lhs = item_ptr;
                                    st_in.rhs = cval;
                                    st_in.imm = elem_size;
                                    if (push_instr(sf, st_in) != 0) {
                                        free(units);
                                        set_diag(diag, "out of memory storing static local array string element");
                                        return -1;
                                    }
                                }
                                free(units);
                                handled_string_init = 1;
                            }
                        }
                        if (handled_string_init) {
                            /* nothing else to emit */
                        } else {
                        for (ii = 0; ii < s->expr->arg_count; ++ii) {
                            int item_ptr = dst_addr;
                            cc_ssa_instr_t st_in;
                            if (ii > 0) {
                                int offv = emit_const_i64_instr(sf, (long)ii * elem_size);
                                if (offv < 0) {
                                    return -1;
                                }
                                memset(&st_in, 0, sizeof(st_in));
                                st_in.op = CC_SSA_ADD;
                                st_in.dst = new_value(sf, CC_VAL_I64);
                                st_in.lhs = dst_addr;
                                st_in.rhs = offv;
                                if (st_in.dst < 0 || push_instr(sf, st_in) != 0) {
                                    set_diag(diag, "out of memory computing static local array initializer address");
                                    return -1;
                                }
                                item_ptr = st_in.dst;
                            }
                            if (elem_type == CC_TYPE_VOID && s->type_struct_id >= 0) {
                                const cc_expr_t *elem_init = s->expr->args[ii];
                                const cc_expr_t *tmp_args[1];
                                cc_expr_t tmp_list;
                                if (elem_init->kind == CC_EXPR_CAST && elem_init->aux_type == CC_TYPE_VOID &&
                                    elem_init->aux_struct_id == s->type_struct_id && elem_init->lhs != NULL &&
                                    elem_init->lhs->kind == CC_EXPR_INIT_LIST) {
                                    elem_init = elem_init->lhs;
                                }
                                if (elem_init->kind != CC_EXPR_INIT_LIST) {
                                    memset(&tmp_list, 0, sizeof(tmp_list));
                                    tmp_args[0] = elem_init;
                                    tmp_list.kind = CC_EXPR_INIT_LIST;
                                    tmp_list.arg_count = 1;
                                    tmp_list.args = (cc_expr_t **)tmp_args;
                                    elem_init = &tmp_list;
                                }
                                if (lower_struct_init_to_ptr(tu, sf, ctx, *vars, *var_count, depth, item_ptr,
                                                             s->type_struct_id, elem_init, diag) != 0) {
                                    return -1;
                                }
                            } else {
                                const cc_expr_t *item_expr = unwrap_scalar_initializer_expr(s->expr->args[ii], diag);
                                if (item_expr == NULL) {
                                    return -1;
                                }
                                v = lower_expr(tu, sf, ctx, *vars, *var_count, depth, item_expr, diag);
                                if (v < 0) {
                                    return -1;
                                }
                                v = cast_value(sf, v, type_to_val(elem_type), diag);
                                if (v < 0) {
                                    return -1;
                                }
                                memset(&st_in, 0, sizeof(st_in));
                                st_in.op = CC_SSA_STORE;
                                st_in.dst = -1;
                                st_in.lhs = item_ptr;
                                st_in.rhs = v;
                                st_in.imm = elem_size;
                                if (push_instr(sf, st_in) != 0) {
                                    set_diag(diag, "out of memory storing static local array initializer element");
                                    return -1;
                                }
                            }
                        }
                        }
                    } else if (s->expr->kind == CC_EXPR_STR) {
                        unsigned long *units = NULL;
                        size_t unit_count = 0;
                        size_t si;
                        int wide = (s->expr->aux_type == CC_TYPE_INT || s->expr->aux_type == CC_TYPE_UINT ||
                                    s->expr->aux_type == CC_TYPE_LONG_LONG ||
                                    s->expr->aux_type == CC_TYPE_ULONG_LONG);
                        if (decode_string_units(s->expr, wide, &units, &unit_count) != 0) {
                            set_diag(diag, "failed to decode static local array string initializer");
                            return -1;
                        }
                        if ((long)unit_count > arr_elems) {
                            free(units);
                            set_diag(diag, "string initializer exceeds static local array size");
                            return -1;
                        }
                        for (si = 0; si < unit_count + (unit_count < (size_t)arr_elems ? 1 : 0); ++si) {
                            int item_ptr = dst_addr;
                            cc_ssa_instr_t st_in;
                            int cval;
                            if (si > 0) {
                                int offv = emit_const_i64_instr(sf, (long)si * elem_size);
                                if (offv < 0) {
                                    free(units);
                                    return -1;
                                }
                                memset(&st_in, 0, sizeof(st_in));
                                st_in.op = CC_SSA_ADD;
                                st_in.dst = new_value(sf, CC_VAL_I64);
                                st_in.lhs = dst_addr;
                                st_in.rhs = offv;
                                if (st_in.dst < 0 || push_instr(sf, st_in) != 0) {
                                    free(units);
                                    set_diag(diag, "out of memory computing static local array string element address");
                                    return -1;
                                }
                                item_ptr = st_in.dst;
                            }
                            cval = emit_const_i64_instr(sf, si < unit_count ? (long)units[si] : 0);
                            if (cval < 0) {
                                free(units);
                                return -1;
                            }
                            cval = cast_value(sf, cval, type_to_val(elem_type), diag);
                            if (cval < 0) {
                                free(units);
                                return -1;
                            }
                            memset(&st_in, 0, sizeof(st_in));
                            st_in.op = CC_SSA_STORE;
                            st_in.dst = -1;
                            st_in.lhs = item_ptr;
                            st_in.rhs = cval;
                            st_in.imm = elem_size;
                            if (push_instr(sf, st_in) != 0) {
                                free(units);
                                set_diag(diag, "out of memory storing static local array string element");
                                return -1;
                            }
                        }
                        free(units);
                    } else {
                        v = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
                        if (v < 0) {
                            return -1;
                        }
                        v = cast_value(sf, v, type_to_val(elem_type), diag);
                        if (v < 0) {
                            return -1;
                        }
                        memset(&in, 0, sizeof(in));
                        in.op = CC_SSA_STORE;
                        in.dst = -1;
                        in.lhs = dst_addr;
                        in.rhs = v;
                        in.imm = elem_size;
                        if (push_instr(sf, in) != 0) {
                            set_diag(diag, "out of memory storing static local array initializer");
                            return -1;
                        }
                    }
                } else {
                    v = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
                    if (v < 0) {
                        return -1;
                    }
                    v = cast_value(sf, v, type_to_val(s->type), diag);
                    if (v < 0) {
                        return -1;
                    }
                    v = normalize_float_value(sf, v, s->type, diag);
                    if (v < 0) {
                        return -1;
                    }
                    if (emit_store_global_i64(sf, sym, v, obj_size, diag) != 0) {
                        return -1;
                    }
                }
                one = emit_const_i64_instr(sf, 1);
                if (one < 0) {
                    return -1;
                }
                if (emit_store_global_i64(sf, guard_sym, one, 4, diag) != 0) {
                    return -1;
                }
                if (emit_br_instr(sf, l_done) != 0) {
                    return -1;
                }
                if (emit_label_instr(sf, l_done) != 0) {
                    return -1;
                }
            }

            if (var_define(vars, var_count, s->decl_name, s->type, s->type_struct_id,
                           is_array_obj ? s->array_len : -1, is_array_obj ? s->array_ndim : 0,
                           is_array_obj ? s->array_dims : NULL, -1, depth, 1, sym) != 0) {
                set_diag(diag, "out of memory defining static local variable");
                return -1;
            }
            return 0;
        }
        if (s->type == CC_TYPE_VOID && s->type_struct_id >= 0) {
            long sz = type_size_bytes_with_struct(tu, s->type, s->type_struct_id);
            int prealloc = lower_find_hoisted_alloc(ctx, s);
            if (sz <= 0) {
                /* GNU empty-struct extension: materialize as 1-byte storage object. */
                sz = 1;
            }
            varv = prealloc >= 0 ? prealloc : emit_local_storage_alloc(sf, sz, diag);
            if (varv < 0) {
                return -1;
            }
            if (s->expr != NULL) {
                if (s->expr->kind == CC_EXPR_INIT_LIST) {
                    if (lower_struct_init_to_ptr(tu, sf, ctx, *vars, *var_count, depth, varv, s->type_struct_id,
                                                 s->expr, diag) != 0) {
                        return -1;
                    }
                } else {
                    int rhsv = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
                    if (rhsv < 0) {
                        return -1;
                    }
                    if (emit_memcpy_instr(sf, varv, rhsv, sz, diag) != 0) {
                        return -1;
                    }
                }
            }
            if (var_define(vars, var_count, s->decl_name, s->type, s->type_struct_id, -1, 0, NULL, varv, depth, 0,
                           NULL) != 0) {
                set_diag(diag, "out of memory defining local struct variable");
                return -1;
            }
            return 0;
        }
        if (is_array_object_decl(s->type, s->array_len, s->array_ndim)) {
            cc_ssa_instr_t st_in;
            cc_type_t elem_type = ptr_base_type(s->type);
            long elem_size = array_decl_scalar_size_bytes(tu, s->type, s->type_struct_id, s->array_ndim);
            long arr_elems = s->array_len > 0 ? s->array_len : 1;
            long total_size;
            int prealloc = lower_find_hoisted_alloc(ctx, s);

            if (elem_size <= 0) {
                elem_size = type_size_bytes_with_struct(tu, elem_type, s->type_struct_id);
            }
            if (elem_size <= 0) {
                set_diag(diag, "unsupported local array element type in lowering");
                return -1;
            }
            if (arr_elems <= 0) {
                arr_elems = 1;
            }
            if (elem_size > LONG_MAX / arr_elems) {
                set_diag(diag, "local array size overflow in lowering");
                return -1;
            }
            total_size = elem_size * arr_elems;
            if (total_size <= 0) {
                set_diag(diag, "invalid local array size in lowering");
                return -1;
            }
            varv = prealloc >= 0 ? prealloc : emit_local_storage_alloc(sf, total_size, diag);
            if (varv < 0) {
                return -1;
            }

            if (s->expr != NULL) {
                if (s->expr->kind == CC_EXPR_INIT_LIST) {
                    size_t ii;
                    int handled_string_init = 0;
                    if (s->expr->arg_count == 1) {
                        const cc_expr_t *item_expr = unwrap_scalar_initializer_expr(s->expr->args[0], diag);
                        if (item_expr == NULL) {
                            return -1;
                        }
                        if (item_expr->kind == CC_EXPR_STR) {
                            unsigned long *units = NULL;
                            size_t unit_count = 0;
                            size_t si;
                            int wide = (item_expr->aux_type == CC_TYPE_INT || item_expr->aux_type == CC_TYPE_UINT ||
                                        item_expr->aux_type == CC_TYPE_LONG_LONG ||
                                        item_expr->aux_type == CC_TYPE_ULONG_LONG);
                            if (decode_string_units(item_expr, wide, &units, &unit_count) != 0) {
                                set_diag(diag, "failed to decode local array string initializer");
                                return -1;
                            }
                            if ((long)unit_count > arr_elems) {
                                free(units);
                                set_diag(diag, "string initializer exceeds local array size");
                                return -1;
                            }
                            for (si = 0; si < unit_count + (unit_count < (size_t)arr_elems ? 1 : 0); ++si) {
                                int item_ptr = varv;
                                int cval;
                                if (si > 0) {
                                    int offv = emit_const_i64_instr(sf, (long)si * elem_size);
                                    if (offv < 0) {
                                        free(units);
                                        return -1;
                                    }
                                    memset(&st_in, 0, sizeof(st_in));
                                    st_in.op = CC_SSA_ADD;
                                    st_in.dst = new_value(sf, CC_VAL_I64);
                                    st_in.lhs = varv;
                                    st_in.rhs = offv;
                                    if (st_in.dst < 0 || push_instr(sf, st_in) != 0) {
                                        free(units);
                                        set_diag(diag, "out of memory computing local array string element address");
                                        return -1;
                                    }
                                    item_ptr = st_in.dst;
                                }
                                cval = emit_const_i64_instr(sf, si < unit_count ? (long)units[si] : 0);
                                if (cval < 0) {
                                    free(units);
                                    return -1;
                                }
                                cval = cast_value(sf, cval, type_to_val(elem_type), diag);
                                if (cval < 0) {
                                    free(units);
                                    return -1;
                                }
                                memset(&st_in, 0, sizeof(st_in));
                                st_in.op = CC_SSA_STORE;
                                st_in.dst = -1;
                                st_in.lhs = item_ptr;
                                st_in.rhs = cval;
                                st_in.imm = elem_size;
                                if (push_instr(sf, st_in) != 0) {
                                    free(units);
                                    set_diag(diag, "out of memory storing local array string element");
                                    return -1;
                                }
                            }
                            free(units);
                            handled_string_init = 1;
                        }
                    }
                    if (!handled_string_init) for (ii = 0; ii < s->expr->arg_count; ++ii) {
                        int item_ptr = varv;
                        if (ii > 0) {
                            int offv = emit_const_i64_instr(sf, (long)ii * elem_size);
                            if (offv < 0) {
                                return -1;
                            }
                            memset(&st_in, 0, sizeof(st_in));
                            st_in.op = CC_SSA_ADD;
                            st_in.dst = new_value(sf, CC_VAL_I64);
                            st_in.lhs = varv;
                            st_in.rhs = offv;
                            if (st_in.dst < 0 || push_instr(sf, st_in) != 0) {
                                set_diag(diag, "out of memory computing local array initializer element address");
                                return -1;
                            }
                            item_ptr = st_in.dst;
                        }
                        if (elem_type == CC_TYPE_VOID && s->type_struct_id >= 0) {
                            const cc_expr_t *elem_init = s->expr->args[ii];
                            const cc_expr_t *tmp_args[1];
                            cc_expr_t tmp_list;
                            if (elem_init->kind == CC_EXPR_CAST && elem_init->aux_type == CC_TYPE_VOID &&
                                elem_init->aux_struct_id == s->type_struct_id && elem_init->lhs != NULL &&
                                elem_init->lhs->kind == CC_EXPR_INIT_LIST) {
                                elem_init = elem_init->lhs;
                            }
                            if (elem_init->kind != CC_EXPR_INIT_LIST) {
                                memset(&tmp_list, 0, sizeof(tmp_list));
                                tmp_args[0] = elem_init;
                                tmp_list.kind = CC_EXPR_INIT_LIST;
                                tmp_list.arg_count = 1;
                                tmp_list.args = (cc_expr_t **)tmp_args;
                                elem_init = &tmp_list;
                            }
                            if (lower_struct_init_to_ptr(tu, sf, ctx, *vars, *var_count, depth, item_ptr,
                                                         s->type_struct_id, elem_init, diag) != 0) {
                                return -1;
                            }
                        } else {
                            v = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr->args[ii], diag);
                            if (v < 0) {
                                return -1;
                            }
                            v = cast_value(sf, v, type_to_val(elem_type), diag);
                            if (v < 0) {
                                return -1;
                            }
                            memset(&st_in, 0, sizeof(st_in));
                            st_in.op = CC_SSA_STORE;
                            st_in.dst = -1;
                            st_in.lhs = item_ptr;
                            st_in.rhs = v;
                            st_in.imm = elem_size;
                            if (push_instr(sf, st_in) != 0) {
                                set_diag(diag, "out of memory storing local array initializer element");
                                return -1;
                            }
                        }
                    }
                } else if (s->expr->kind == CC_EXPR_STR) {
                    unsigned long *units = NULL;
                    size_t unit_count = 0;
                    size_t ii;
                    int wide = (s->expr->aux_type == CC_TYPE_INT || s->expr->aux_type == CC_TYPE_UINT ||
                                s->expr->aux_type == CC_TYPE_LONG_LONG || s->expr->aux_type == CC_TYPE_ULONG_LONG);
                    if (decode_string_units(s->expr, wide, &units, &unit_count) != 0) {
                        set_diag(diag, "failed to decode string initializer");
                        return -1;
                    }
                    if ((long)unit_count > arr_elems) {
                        free(units);
                        set_diag(diag, "string initializer exceeds local array size");
                        return -1;
                    }
                    for (ii = 0; ii < unit_count + (unit_count < (size_t)arr_elems ? 1 : 0); ++ii) {
                        int item_ptr = varv;
                        int cval;
                        if (ii > 0) {
                            int offv = emit_const_i64_instr(sf, (long)ii * elem_size);
                            if (offv < 0) {
                                free(units);
                                return -1;
                            }
                            memset(&st_in, 0, sizeof(st_in));
                            st_in.op = CC_SSA_ADD;
                            st_in.dst = new_value(sf, CC_VAL_I64);
                            st_in.lhs = varv;
                            st_in.rhs = offv;
                            if (st_in.dst < 0 || push_instr(sf, st_in) != 0) {
                                free(units);
                                set_diag(diag, "out of memory computing local array string element address");
                                return -1;
                            }
                            item_ptr = st_in.dst;
                        }
                        cval = emit_const_i64_instr(sf, ii < unit_count ? (long)units[ii] : 0);
                        if (cval < 0) {
                            free(units);
                            return -1;
                        }
                        cval = cast_value(sf, cval, type_to_val(elem_type), diag);
                        if (cval < 0) {
                            free(units);
                            return -1;
                        }
                        memset(&st_in, 0, sizeof(st_in));
                        st_in.op = CC_SSA_STORE;
                        st_in.dst = -1;
                        st_in.lhs = item_ptr;
                        st_in.rhs = cval;
                        st_in.imm = elem_size;
                        if (push_instr(sf, st_in) != 0) {
                            free(units);
                            set_diag(diag, "out of memory storing local array string element");
                            return -1;
                        }
                    }
                    free(units);
                } else {
                    v = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
                    if (v < 0) {
                        return -1;
                    }
                    v = cast_value(sf, v, type_to_val(elem_type), diag);
                    if (v < 0) {
                        return -1;
                    }
                    memset(&st_in, 0, sizeof(st_in));
                    st_in.op = CC_SSA_STORE;
                    st_in.dst = -1;
                    st_in.lhs = varv;
                    st_in.rhs = v;
                    st_in.imm = elem_size;
                    if (push_instr(sf, st_in) != 0) {
                        set_diag(diag, "out of memory storing local array initializer");
                        return -1;
                    }
                }
            }

            if (var_define(vars, var_count, s->decl_name, s->type, s->type_struct_id, s->array_len, s->array_ndim,
                           s->array_dims, varv, depth, 0, NULL) != 0) {
                set_diag(diag, "out of memory defining local array variable");
                return -1;
            }
            return 0;
        }
        varv = new_value(sf, type_to_val(s->type));
        if (varv < 0) {
            set_diag(diag, "out of memory allocating local variable value");
            return -1;
        }
        if (var_define(vars, var_count, s->decl_name, s->type, s->type_struct_id, -1, 0, NULL, varv, depth, 0,
                       NULL) != 0) {
            set_diag(diag, "out of memory defining local variable");
            return -1;
        }
        if (s->expr != NULL) {
            v = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
            if (v < 0) {
                return -1;
            }
            v = cast_value(sf, v, type_to_val(s->type), diag);
            if (v < 0) {
                return -1;
            }
            v = normalize_float_value(sf, v, s->type, diag);
            if (v < 0) {
                return -1;
            }
            if (emit_mov_instr(sf, varv, v) != 0) {
                set_diag(diag, "out of memory appending declaration move");
                return -1;
            }
        } else {
            cc_ssa_instr_t in;
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_CONST;
            in.dst = varv;
            in.lhs = -1;
            in.rhs = -1;
            in.imm = 0;
            in.fimm = 0.0;
            if (push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory appending declaration default const");
                return -1;
            }
        }
        return 0;
    }

    if (s->kind == CC_STMT_EXPR) {
        if (s->expr != NULL && lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag) < 0) {
            return -1;
        }
        return 0;
    }

    if (s->kind == CC_STMT_ASM) {
        cc_ssa_instr_t in;
        size_t i4;
        memset(&in, 0, sizeof(in));
        in.op = CC_SSA_ASM;
        in.dst = -1;
        in.lhs = -1;
        in.rhs = -1;
        in.asm_volatile = s->asm_is_volatile ? 1 : 0;
        in.asm_is_goto = s->asm_is_goto ? 1 : 0;
        in.sym = xstrdup(s->asm_template != NULL ? s->asm_template : "");
        in.asm_out_count = s->asm_output_count;
        in.asm_in_count = s->asm_input_count;
        in.asm_clobber_count = s->asm_clobber_count;
        in.asm_goto_count = s->asm_goto_label_count;
        if (in.sym == NULL) {
            set_diag(diag, "out of memory storing asm template");
            return -1;
        }
        if (in.asm_out_count > 0) {
            in.asm_out_values = (int *)calloc(in.asm_out_count, sizeof(*in.asm_out_values));
            in.asm_out_constraints = (char **)calloc(in.asm_out_count, sizeof(*in.asm_out_constraints));
            in.asm_out_names = (char **)calloc(in.asm_out_count, sizeof(*in.asm_out_names));
            if (in.asm_out_values == NULL || in.asm_out_constraints == NULL || in.asm_out_names == NULL) {
                free_asm_instr_fields(&in);
                set_diag(diag, "out of memory storing asm outputs");
                return -1;
            }
        }
        if (in.asm_in_count > 0) {
            in.asm_in_values = (int *)calloc(in.asm_in_count, sizeof(*in.asm_in_values));
            in.asm_in_constraints = (char **)calloc(in.asm_in_count, sizeof(*in.asm_in_constraints));
            in.asm_in_names = (char **)calloc(in.asm_in_count, sizeof(*in.asm_in_names));
            if (in.asm_in_values == NULL || in.asm_in_constraints == NULL || in.asm_in_names == NULL) {
                free_asm_instr_fields(&in);
                set_diag(diag, "out of memory storing asm inputs");
                return -1;
            }
        }
        if (in.asm_clobber_count > 0) {
            in.asm_clobbers = (char **)calloc(in.asm_clobber_count, sizeof(*in.asm_clobbers));
            if (in.asm_clobbers == NULL) {
                free_asm_instr_fields(&in);
                set_diag(diag, "out of memory storing asm clobbers");
                return -1;
            }
        }
        if (in.asm_goto_count > 0) {
            in.asm_goto_labels = (int *)calloc(in.asm_goto_count, sizeof(*in.asm_goto_labels));
            in.asm_goto_names = (char **)calloc(in.asm_goto_count, sizeof(*in.asm_goto_names));
            if (in.asm_goto_labels == NULL || in.asm_goto_names == NULL) {
                free_asm_instr_fields(&in);
                set_diag(diag, "out of memory storing asm goto labels");
                return -1;
            }
        }
        for (i4 = 0; i4 < in.asm_out_count; ++i4) {
            int vidx = -1;
            const cc_asm_operand_t *op = &s->asm_outputs[i4];
            if (op->expr != NULL && op->expr->kind == CC_EXPR_IDENT && op->expr->ident != NULL) {
                vidx = var_find_visible(*vars, *var_count, op->expr->ident, depth);
                if (vidx >= 0) {
                    in.asm_out_values[i4] = (*vars)[vidx].value;
                } else if (asm_constraint_is_memory_only(op->constraint)) {
                    const cc_global_t *g = find_global(tu, op->expr->ident);
                    if (g != NULL) {
                        int addrv = emit_global_addr(sf, g->name, diag);
                        if (addrv < 0) {
                            free_asm_instr_fields(&in);
                            return -1;
                        }
                        in.asm_out_values[i4] = CC_SSA_ASM_MEM_INDIRECT_ENCODE(addrv);
                    } else {
                        free_asm_instr_fields(&in);
                        set_diag(diag, "asm output target must reference a local or global lvalue");
                        return -1;
                    }
                } else {
                    int av = lower_expr(tu, sf, ctx, *vars, *var_count, depth, op->expr, diag);
                    if (av < 0) {
                        free_asm_instr_fields(&in);
                        return -1;
                    }
                    in.asm_out_values[i4] = av;
                }
            } else if (op->expr != NULL && op->expr->kind == CC_EXPR_DEREF && op->expr->lhs != NULL &&
                       asm_constraint_is_memory_only(op->constraint)) {
                int addrv = lower_expr(tu, sf, ctx, *vars, *var_count, depth, op->expr->lhs, diag);
                if (addrv < 0) {
                    free_asm_instr_fields(&in);
                    return -1;
                }
                in.asm_out_values[i4] = CC_SSA_ASM_MEM_INDIRECT_ENCODE(addrv);
            } else {
                free_asm_instr_fields(&in);
                set_diag(diag, "unsupported asm output target (requires local identifier or memory dereference)");
                return -1;
            }
            in.asm_out_constraints[i4] = xstrdup(op->constraint != NULL ? op->constraint : "");
            if (op->name != NULL) {
                in.asm_out_names[i4] = xstrdup(op->name);
            }
            if (in.asm_out_constraints[i4] == NULL || (op->name != NULL && in.asm_out_names[i4] == NULL)) {
                free_asm_instr_fields(&in);
                set_diag(diag, "out of memory storing asm output operand");
                return -1;
            }
        }
        for (i4 = 0; i4 < in.asm_in_count; ++i4) {
            const cc_asm_operand_t *op = &s->asm_inputs[i4];
            int av = lower_expr(tu, sf, ctx, *vars, *var_count, depth, op->expr, diag);
            if (av < 0) {
                free_asm_instr_fields(&in);
                return -1;
            }
            in.asm_in_values[i4] = av;
            in.asm_in_constraints[i4] = xstrdup(op->constraint != NULL ? op->constraint : "");
            if (op->name != NULL) {
                in.asm_in_names[i4] = xstrdup(op->name);
            }
            if (in.asm_in_constraints[i4] == NULL || (op->name != NULL && in.asm_in_names[i4] == NULL)) {
                free_asm_instr_fields(&in);
                set_diag(diag, "out of memory storing asm input operand");
                return -1;
            }
        }
        for (i4 = 0; i4 < in.asm_clobber_count; ++i4) {
            in.asm_clobbers[i4] = xstrdup(s->asm_clobbers[i4] != NULL ? s->asm_clobbers[i4] : "");
            if (in.asm_clobbers[i4] == NULL) {
                free_asm_instr_fields(&in);
                set_diag(diag, "out of memory storing asm clobber");
                return -1;
            }
        }
        for (i4 = 0; i4 < in.asm_goto_count; ++i4) {
            int l;
            const char *lname = s->asm_goto_labels[i4];
            if (ctx == NULL || lname == NULL || lname[0] == '\0') {
                free_asm_instr_fields(&in);
                set_diag(diag, "asm goto label is malformed");
                return -1;
            }
            l = lower_find_label(ctx, lname);
            if (l < 0) {
                free_asm_instr_fields(&in);
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "asm goto references unknown label: %s", lname);
                }
                return -1;
            }
            in.asm_goto_labels[i4] = l;
            in.asm_goto_names[i4] = xstrdup(lname);
            if (in.asm_goto_names[i4] == NULL) {
                free_asm_instr_fields(&in);
                set_diag(diag, "out of memory storing asm goto label name");
                return -1;
            }
        }
        if (push_instr(sf, in) != 0) {
            free_asm_instr_fields(&in);
            set_diag(diag, "out of memory appending SSA asm instruction");
            return -1;
        }
        return 0;
    }

    if (s->kind == CC_STMT_RETURN) {
        cc_ssa_instr_t ret_in;
        int rv = -1;

        if (s->expr != NULL) {
            rv = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
            if (rv < 0) {
                return -1;
            }
            rv = cast_value(sf, rv, sf->ret_type, diag);
            if (rv < 0) {
                return -1;
            }
        }

        memset(&ret_in, 0, sizeof(ret_in));
        ret_in.op = CC_SSA_RET;
        ret_in.dst = -1;
        ret_in.lhs = rv;
        ret_in.rhs = -1;
        ret_in.param_index = -1;
        if (push_instr(sf, ret_in) != 0) {
            set_diag(diag, "out of memory appending SSA return instruction");
            return -1;
        }
        *saw_ret = 1;
        return 0;
    }

    if (s->kind == CC_STMT_IF) {
        int cond;
        int l_then = new_label(sf);
        int l_else = new_label(sf);
        int l_end = new_label(sf);

        cond = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
        if (cond < 0) {
            return -1;
        }
        cond = lower_truthy_value(sf, cond, diag);
        if (cond < 0) {
            return -1;
        }

        if (emit_br_cond_instr(sf, cond, l_then, s->else_branch != NULL ? l_else : l_end) != 0) {
            return -1;
        }
        if (emit_label_instr(sf, l_then) != 0) {
            return -1;
        }
        {
            size_t saved = *var_count;
            if (lower_stmt(tu, sf, vars, var_count, ctx, depth, break_label, continue_label, s->then_branch, saw_ret,
                           diag) != 0) {
                return -1;
            }
            while (*var_count > saved) {
                (*var_count)--;
                free((*vars)[*var_count].name);
            }
        }
        if (emit_br_instr(sf, l_end) != 0) {
            return -1;
        }

        if (s->else_branch != NULL) {
            if (emit_label_instr(sf, l_else) != 0) {
                return -1;
            }
            {
                size_t saved = *var_count;
                if (lower_stmt(tu, sf, vars, var_count, ctx, depth, break_label, continue_label, s->else_branch, saw_ret,
                               diag) != 0) {
                    return -1;
                }
                while (*var_count > saved) {
                    (*var_count)--;
                    free((*vars)[*var_count].name);
                }
            }
            if (emit_br_instr(sf, l_end) != 0) {
                return -1;
            }
        }

        if (emit_label_instr(sf, l_end) != 0) {
            return -1;
        }
        return 0;
    }

    if (s->kind == CC_STMT_WHILE) {
        int l_cond = new_label(sf);
        int l_body = new_label(sf);
        int l_end = new_label(sf);
        int cond;

        if (emit_label_instr(sf, l_cond) != 0) {
            return -1;
        }
        cond = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
        if (cond < 0) {
            return -1;
        }
        cond = lower_truthy_value(sf, cond, diag);
        if (cond < 0) {
            return -1;
        }
        if (emit_br_cond_instr(sf, cond, l_body, l_end) != 0) {
            return -1;
        }
        if (emit_label_instr(sf, l_body) != 0) {
            return -1;
        }
        {
            size_t saved = *var_count;
            if (lower_stmt(tu, sf, vars, var_count, ctx, depth, l_end, l_cond, s->then_branch, saw_ret, diag) != 0) {
                return -1;
            }
            while (*var_count > saved) {
                (*var_count)--;
                free((*vars)[*var_count].name);
            }
        }
        if (emit_br_instr(sf, l_cond) != 0) {
            return -1;
        }
        if (emit_label_instr(sf, l_end) != 0) {
            return -1;
        }
        return 0;
    }

    if (s->kind == CC_STMT_DO) {
        int l_body = new_label(sf);
        int l_cond = new_label(sf);
        int l_end = new_label(sf);
        int cond;

        if (emit_label_instr(sf, l_body) != 0) {
            return -1;
        }
        {
            size_t saved = *var_count;
            if (lower_stmt(tu, sf, vars, var_count, ctx, depth, l_end, l_cond, s->then_branch, saw_ret, diag) != 0) {
                return -1;
            }
            while (*var_count > saved) {
                (*var_count)--;
                free((*vars)[*var_count].name);
            }
        }
        if (emit_label_instr(sf, l_cond) != 0) {
            return -1;
        }
        cond = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
        if (cond < 0) {
            return -1;
        }
        cond = lower_truthy_value(sf, cond, diag);
        if (cond < 0) {
            return -1;
        }
        if (emit_br_cond_instr(sf, cond, l_body, l_end) != 0) {
            return -1;
        }
        if (emit_label_instr(sf, l_end) != 0) {
            return -1;
        }
        return 0;
    }

    if (s->kind == CC_STMT_FOR) {
        int l_cond = new_label(sf);
        int l_body = new_label(sf);
        int l_post = new_label(sf);
        int l_end = new_label(sf);
        size_t saved = *var_count;
        int for_depth = depth;

        if (s->init_stmt != NULL) {
            if (s->init_stmt->kind == CC_STMT_BLOCK) {
                size_t i3;
                for (i3 = 0; i3 < s->init_stmt->block_count; ++i3) {
                    if (lower_stmt(tu, sf, vars, var_count, ctx, depth + 1, break_label, continue_label,
                                   &s->init_stmt->block_stmts[i3], saw_ret, diag) != 0) {
                        return -1;
                    }
                }
            } else {
                if (lower_stmt(tu, sf, vars, var_count, ctx, depth + 1, break_label, continue_label, s->init_stmt,
                               saw_ret, diag) != 0) {
                    return -1;
                }
            }
            for_depth = depth + 1;
        } else if (s->init_expr != NULL && lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->init_expr, diag) < 0) {
            return -1;
        }
        if (emit_label_instr(sf, l_cond) != 0) {
            return -1;
        }
        if (s->expr != NULL) {
            int cond = lower_expr(tu, sf, ctx, *vars, *var_count, for_depth, s->expr, diag);
            if (cond < 0) {
                return -1;
            }
            cond = lower_truthy_value(sf, cond, diag);
            if (cond < 0) {
                return -1;
            }
            if (emit_br_cond_instr(sf, cond, l_body, l_end) != 0) {
                return -1;
            }
        } else {
            if (emit_br_instr(sf, l_body) != 0) {
                return -1;
            }
        }
        if (emit_label_instr(sf, l_body) != 0) {
            return -1;
        }
        {
            size_t body_saved = *var_count;
            if (lower_stmt(tu, sf, vars, var_count, ctx, for_depth, l_end, l_post, s->then_branch, saw_ret, diag) != 0) {
                return -1;
            }
            while (*var_count > body_saved) {
                (*var_count)--;
                free((*vars)[*var_count].name);
            }
        }
        if (emit_br_instr(sf, l_post) != 0) {
            return -1;
        }
        if (emit_label_instr(sf, l_post) != 0) {
            return -1;
        }
        if (s->post_expr != NULL && lower_expr(tu, sf, ctx, *vars, *var_count, for_depth, s->post_expr, diag) < 0) {
            return -1;
        }
        if (emit_br_instr(sf, l_cond) != 0) {
            return -1;
        }
        if (emit_label_instr(sf, l_end) != 0) {
            return -1;
        }
        while (*var_count > saved) {
            (*var_count)--;
            free((*vars)[*var_count].name);
        }
        return 0;
    }

    if (s->kind == CC_STMT_SWITCH) {
        switch_case_site_t *sites = NULL;
        size_t site_count = 0;
        int default_label = -1;
        int l_end = new_label(sf);
        size_t case_count = 0;
        size_t case_i = 0;
        size_t j2;
        int *cmp_labels = NULL;
        int *case_labels = NULL;
        long *case_values = NULL;
        long *case_hi_values = NULL;
        int *case_has_range = NULL;
        int cond;

        if (l_end < 0) {
            set_diag(diag, "out of memory assigning switch end label");
            return -1;
        }
        if (s->then_branch == NULL) {
            set_diag(diag, "switch lowering requires body");
            return -1;
        }

        cond = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
        if (cond < 0) {
            return -1;
        }
        cond = cast_value(sf, cond, CC_VAL_I64, diag);
        if (cond < 0) {
            return -1;
        }

        if (collect_switch_case_sites(sf, s->then_branch, &sites, &site_count, &default_label, diag) != 0) {
            return -1;
        }

        for (j2 = 0; j2 < site_count; ++j2) {
            if (!sites[j2].is_default) {
                case_count++;
            }
        }

        if (case_count == 0) {
            if (emit_br_instr(sf, default_label >= 0 ? default_label : l_end) != 0) {
                free(sites);
                return -1;
            }
        } else {
            cmp_labels = (int *)calloc(case_count, sizeof(*cmp_labels));
            case_labels = (int *)calloc(case_count, sizeof(*case_labels));
            case_values = (long *)calloc(case_count, sizeof(*case_values));
            case_hi_values = (long *)calloc(case_count, sizeof(*case_hi_values));
            case_has_range = (int *)calloc(case_count, sizeof(*case_has_range));
            if (cmp_labels == NULL || case_labels == NULL || case_values == NULL || case_hi_values == NULL ||
                case_has_range == NULL) {
                free(sites);
                free(cmp_labels);
                free(case_labels);
                free(case_values);
                free(case_hi_values);
                free(case_has_range);
                set_diag(diag, "out of memory lowering switch dispatch");
                return -1;
            }

            for (j2 = 0; j2 < site_count; ++j2) {
                if (!sites[j2].is_default) {
                    case_labels[case_i] = sites[j2].label;
                    case_values[case_i] = sites[j2].value;
                    case_hi_values[case_i] = sites[j2].value_hi;
                    case_has_range[case_i] = sites[j2].has_range;
                    cmp_labels[case_i] = new_label(sf);
                    if (cmp_labels[case_i] < 0) {
                        free(sites);
                        free(cmp_labels);
                        free(case_labels);
                        free(case_values);
                        free(case_hi_values);
                        free(case_has_range);
                        set_diag(diag, "out of memory assigning switch compare labels");
                        return -1;
                    }
                    case_i++;
                }
            }

            if (emit_label_instr(sf, cmp_labels[0]) != 0) {
                free(sites);
                free(cmp_labels);
                free(case_labels);
                free(case_values);
                free(case_hi_values);
                free(case_has_range);
                return -1;
            }
            for (case_i = 0; case_i < case_count; ++case_i) {
                int cv = emit_const_i64_instr(sf, case_values[case_i]);
                int cmpv;
                int false_label;
                cc_ssa_instr_t in;

                if (cv < 0) {
                    free(sites);
                    free(cmp_labels);
                    free(case_labels);
                    free(case_values);
                    free(case_hi_values);
                    free(case_has_range);
                    set_diag(diag, "out of memory appending switch case constant");
                    return -1;
                }

                if (case_has_range[case_i]) {
                    int chv = emit_const_i64_instr(sf, case_hi_values[case_i]);
                    int ge;
                    int le;
                    if (chv < 0) {
                        free(sites);
                        free(cmp_labels);
                        free(case_labels);
                        free(case_values);
                        free(case_hi_values);
                        free(case_has_range);
                        set_diag(diag, "out of memory appending switch case-range constant");
                        return -1;
                    }
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_CMP;
                    in.cmp_kind = CC_CMP_GE;
                    in.dst = new_value(sf, CC_VAL_I64);
                    in.lhs = cond;
                    in.rhs = cv;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        free(sites);
                        free(cmp_labels);
                        free(case_labels);
                        free(case_values);
                        free(case_hi_values);
                        free(case_has_range);
                        set_diag(diag, "out of memory appending switch range lower compare");
                        return -1;
                    }
                    ge = in.dst;
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_CMP;
                    in.cmp_kind = CC_CMP_LE;
                    in.dst = new_value(sf, CC_VAL_I64);
                    in.lhs = cond;
                    in.rhs = chv;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        free(sites);
                        free(cmp_labels);
                        free(case_labels);
                        free(case_values);
                        free(case_hi_values);
                        free(case_has_range);
                        set_diag(diag, "out of memory appending switch range upper compare");
                        return -1;
                    }
                    le = in.dst;
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_AND;
                    in.dst = new_value(sf, CC_VAL_I64);
                    in.lhs = ge;
                    in.rhs = le;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        free(sites);
                        free(cmp_labels);
                        free(case_labels);
                        free(case_values);
                        free(case_hi_values);
                        free(case_has_range);
                        set_diag(diag, "out of memory appending switch range combine");
                        return -1;
                    }
                    cmpv = in.dst;
                } else {
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_CMP;
                    in.cmp_kind = CC_CMP_EQ;
                    in.dst = new_value(sf, CC_VAL_I64);
                    in.lhs = cond;
                    in.rhs = cv;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        free(sites);
                        free(cmp_labels);
                        free(case_labels);
                        free(case_values);
                        free(case_hi_values);
                        free(case_has_range);
                        set_diag(diag, "out of memory appending switch compare");
                        return -1;
                    }
                    cmpv = in.dst;
                }
                false_label = (case_i + 1 < case_count) ? cmp_labels[case_i + 1]
                                                        : (default_label >= 0 ? default_label : l_end);
                if (emit_br_cond_instr(sf, cmpv, case_labels[case_i], false_label) != 0) {
                    free(sites);
                    free(cmp_labels);
                    free(case_labels);
                    free(case_values);
                    free(case_hi_values);
                    free(case_has_range);
                    return -1;
                }
                if (case_i + 1 < case_count && emit_label_instr(sf, cmp_labels[case_i + 1]) != 0) {
                    free(sites);
                    free(cmp_labels);
                    free(case_labels);
                    free(case_values);
                    free(case_hi_values);
                    free(case_has_range);
                    return -1;
                }
            }
        }

        {
            const switch_case_site_t *saved_sites = g_switch_sites;
            size_t saved_site_count = g_switch_site_count;
            g_switch_sites = sites;
            g_switch_site_count = site_count;
            if (lower_stmt(tu, sf, vars, var_count, ctx, depth + 1, l_end, continue_label, s->then_branch, saw_ret,
                           diag) != 0) {
                g_switch_sites = saved_sites;
                g_switch_site_count = saved_site_count;
                free(sites);
                free(cmp_labels);
                free(case_labels);
                free(case_values);
                free(case_hi_values);
                free(case_has_range);
                return -1;
            }
            g_switch_sites = saved_sites;
            g_switch_site_count = saved_site_count;
        }
        if (emit_label_instr(sf, l_end) != 0) {
            free(sites);
            free(cmp_labels);
            free(case_labels);
            free(case_values);
            free(case_hi_values);
            free(case_has_range);
            return -1;
        }
        free(sites);
        free(cmp_labels);
        free(case_labels);
        free(case_values);
        free(case_hi_values);
        free(case_has_range);
        return 0;
    }

    if (s->kind == CC_STMT_BREAK) {
        if (break_label < 0) {
            set_diag(diag, "break used outside loop/switch in lowering");
            return -1;
        }
        return emit_br_instr(sf, break_label);
    }

    if (s->kind == CC_STMT_CONTINUE) {
        if (continue_label < 0) {
            set_diag(diag, "continue used outside loop in lowering");
            return -1;
        }
        return emit_br_instr(sf, continue_label);
    }

    if (s->kind == CC_STMT_GOTO) {
        if (s->expr != NULL) {
            cc_ssa_instr_t in;
            int target;
            int l_bad;
            size_t i;

            if (ctx == NULL || ctx->label_count == 0) {
                set_diag(diag, "computed goto requires at least one local label");
                return -1;
            }
            target = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
            if (target < 0) {
                return -1;
            }
            target = cast_value(sf, target, CC_VAL_I64, diag);
            if (target < 0) {
                return -1;
            }
            l_bad = new_label(sf);
            if (l_bad < 0) {
                return -1;
            }
            for (i = 0; i < ctx->label_count; ++i) {
                int addrv;
                int cmpv;
                int l_next = (i + 1 < ctx->label_count) ? new_label(sf) : l_bad;

                if (l_next < 0) {
                    return -1;
                }

                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_LADDR;
                in.dst = new_value(sf, CC_VAL_I64);
                in.label = ctx->labels[i].label;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    return -1;
                }
                addrv = in.dst;

                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_CMP;
                in.cmp_kind = CC_CMP_EQ;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = target;
                in.rhs = addrv;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    return -1;
                }
                cmpv = in.dst;

                if (emit_br_cond_instr(sf, cmpv, ctx->labels[i].label, l_next) != 0) {
                    return -1;
                }
                if (l_next != l_bad && emit_label_instr(sf, l_next) != 0) {
                    return -1;
                }
            }
            if (emit_label_instr(sf, l_bad) != 0) {
                return -1;
            }
            return emit_br_instr(sf, l_bad);
        }
        int l = lower_find_label(ctx, s->label_name);
        if (l < 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "goto to unknown label in lowering: %s",
                         s->label_name ? s->label_name : "<null>");
            }
            return -1;
        }
        return emit_br_instr(sf, l);
    }

    if (s->kind == CC_STMT_LABEL) {
        int l = lower_find_label(ctx, s->label_name);
        if (l < 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "label not collected in lowering: %s",
                         s->label_name ? s->label_name : "<null>");
            }
            return -1;
        }
        if (emit_label_instr(sf, l) != 0) {
            return -1;
        }
        if (s->then_branch != NULL) {
            return lower_stmt(tu, sf, vars, var_count, ctx, depth, break_label, continue_label, s->then_branch,
                              saw_ret, diag);
        }
        return 0;
    }

    if (s->kind == CC_STMT_BLOCK) {
        size_t saved = *var_count;
        int child_depth = depth + 1;
        if (s->is_synthetic_block) {
            child_depth = depth;
        }
        for (j = 0; j < s->block_count; ++j) {
            if (lower_stmt(tu, sf, vars, var_count, ctx, child_depth, break_label, continue_label, &s->block_stmts[j],
                           saw_ret, diag) != 0) {
                return -1;
            }
        }
        if (!s->is_synthetic_block) {
            while (*var_count > saved) {
                (*var_count)--;
                free((*vars)[*var_count].name);
            }
        }
        return 0;
    }

    if (s->kind == CC_STMT_CASE || s->kind == CC_STMT_DEFAULT) {
        int case_label;
        if (g_switch_sites == NULL || g_switch_site_count == 0) {
            set_diag(diag, "case/default label used outside switch lowering context");
            return -1;
        }
        case_label = find_switch_label_for_stmt(g_switch_sites, g_switch_site_count, s);
        if (case_label < 0) {
            set_diag(diag, "switch label not collected during lowering");
            return -1;
        }
        if (emit_label_instr(sf, case_label) != 0) {
            return -1;
        }
        return 0;
    }

    set_diag(diag, "unsupported statement kind during AST->SSA lowering");
    return -1;
}

static int append_default_return(cc_ssa_function_t *sf) {
    cc_ssa_instr_t ret_in;

    if (sf->ret_type == CC_VAL_F64) {
        cc_ssa_instr_t c;
        memset(&c, 0, sizeof(c));
        c.op = CC_SSA_CONST;
        c.dst = new_value(sf, CC_VAL_F64);
        c.fimm = 0.0;
        if (c.dst < 0 || push_instr(sf, c) != 0) {
            return -1;
        }
        memset(&ret_in, 0, sizeof(ret_in));
        ret_in.op = CC_SSA_RET;
        ret_in.lhs = c.dst;
        ret_in.dst = -1;
        return push_instr(sf, ret_in);
    }

    if (sf->ret_type == CC_VAL_I64) {
        cc_ssa_instr_t c;
        memset(&c, 0, sizeof(c));
        c.op = CC_SSA_CONST;
        c.dst = new_value(sf, CC_VAL_I64);
        c.imm = 0;
        if (c.dst < 0 || push_instr(sf, c) != 0) {
            return -1;
        }
        memset(&ret_in, 0, sizeof(ret_in));
        ret_in.op = CC_SSA_RET;
        ret_in.lhs = c.dst;
        ret_in.dst = -1;
        return push_instr(sf, ret_in);
    }

    memset(&ret_in, 0, sizeof(ret_in));
    ret_in.op = CC_SSA_RET;
    ret_in.lhs = -1;
    ret_in.dst = -1;
    return push_instr(sf, ret_in);
}

static int append_noreturn_loop(cc_ssa_function_t *sf) {
    int l = new_label(sf);
    if (l < 0) {
        return -1;
    }
    if (emit_label_instr(sf, l) != 0) {
        return -1;
    }
    return emit_br_instr(sf, l);
}

static int apply_global_ptr_addend(long base, long idx, long step, int is_sub, long *out) {
    long delta;
    if (out == NULL) {
        return -1;
    }
    if (step <= 0) {
        step = 1;
    }
    if (idx > 0 && step > LONG_MAX / idx) {
        return -1;
    }
    if (idx < 0 && step > LONG_MAX / -idx) {
        return -1;
    }
    delta = idx * step;
    if (is_sub) {
        if (delta == LONG_MIN) {
            return -1;
        }
        delta = -delta;
    }
    if ((delta > 0 && base > LONG_MAX - delta) || (delta < 0 && base < LONG_MIN - delta)) {
        return -1;
    }
    *out = base + delta;
    return 0;
}

static int eval_global_addr_symbol_addend(const cc_translation_unit_t *tu, const cc_expr_t *e, char **out_sym,
                                          long *out_addend) {
    char *lsym = NULL;
    char *rsym = NULL;
    char *sym = NULL;
    long ladd = 0;
    long radd = 0;
    long iv = 0;
    double fv = 0.0;
    int isf = 0;
    long step = 1;
    int has_l = 0;
    int has_r = 0;

    if (out_sym == NULL || out_addend == NULL || e == NULL) {
        return -1;
    }
    *out_sym = NULL;
    *out_addend = 0;
    if (e->kind == CC_EXPR_CAST) {
        return eval_global_addr_symbol_addend(tu, e->lhs, out_sym, out_addend);
    }
    if (e->kind == CC_EXPR_MEMBER && e->lhs == NULL && e->rhs != NULL) {
        return eval_global_addr_symbol_addend(tu, e->rhs, out_sym, out_addend);
    }
    if (e->kind == CC_EXPR_IDENT && e->ident != NULL && e->ident[0] != '\0') {
        sym = xstrdup(e->ident);
        if (sym == NULL) {
            return -1;
        }
        *out_sym = sym;
        *out_addend = 0;
        return 0;
    }
    if (e->kind == CC_EXPR_MEMBER && e->lhs != NULL) {
        if (eval_global_addr_symbol_addend(tu, e->lhs, &sym, out_addend) != 0) {
            return -1;
        }
        if ((e->member_offset > 0 && *out_addend > LONG_MAX - e->member_offset) ||
            (e->member_offset < 0 && *out_addend < LONG_MIN - e->member_offset)) {
            free(sym);
            return -1;
        }
        *out_addend += e->member_offset;
        *out_sym = sym;
        return 0;
    }
    if (e->kind == CC_EXPR_DEREF && e->lhs != NULL) {
        return eval_global_addr_symbol_addend(tu, e->lhs, out_sym, out_addend);
    }
    if (e->kind == CC_EXPR_BIN && (e->op == CC_BIN_ADD || e->op == CC_BIN_SUB)) {
        if (eval_global_addr_symbol_addend(tu, e->lhs, &lsym, &ladd) == 0) {
            has_l = 1;
        }
        if (eval_global_addr_symbol_addend(tu, e->rhs, &rsym, &radd) == 0) {
            has_r = 1;
        }
        if (has_l && !has_r) {
            if (eval_global_init_expr(tu, e->rhs, &iv, &fv, &isf, &rsym) != 0 || rsym != NULL || isf) {
                free(lsym);
                free(rsym);
                return -1;
            }
            step = is_pointer_type(e->lhs->value_type) ? pointer_elem_size_bytes(tu, e->lhs->value_type, e->lhs->struct_id) : 1;
            step = expr_array_step_size_bytes(tu, e->lhs, step);
            if (apply_global_ptr_addend(ladd, iv, step, e->op == CC_BIN_SUB, &ladd) != 0) {
                free(lsym);
                return -1;
            }
            *out_sym = lsym;
            *out_addend = ladd;
            return 0;
        }
        if (!has_l && has_r && e->op == CC_BIN_ADD) {
            if (eval_global_init_expr(tu, e->lhs, &iv, &fv, &isf, &lsym) != 0 || lsym != NULL || isf) {
                free(rsym);
                free(lsym);
                return -1;
            }
            step = is_pointer_type(e->rhs->value_type) ? pointer_elem_size_bytes(tu, e->rhs->value_type, e->rhs->struct_id) : 1;
            step = expr_array_step_size_bytes(tu, e->rhs, step);
            if (apply_global_ptr_addend(radd, iv, step, 0, &radd) != 0) {
                free(rsym);
                return -1;
            }
            *out_sym = rsym;
            *out_addend = radd;
            return 0;
        }
        free(lsym);
        free(rsym);
        return -1;
    }
    return -1;
}

static int eval_global_init_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long *out_i, double *out_f,
                                 int *out_is_float, char **out_sym) {
    if (e == NULL) {
        *out_i = 0;
        *out_f = 0.0;
        *out_is_float = 0;
        if (out_sym != NULL) {
            *out_sym = NULL;
        }
        return 0;
    }
    if (e->kind == CC_EXPR_GENERIC) {
        const cc_expr_t *sel = selected_generic_expr(e);
        if (sel == NULL) {
            return -1;
        }
        return eval_global_init_expr(tu, sel, out_i, out_f, out_is_float, out_sym);
    }
    if (e->kind == CC_EXPR_MEMBER && e->lhs == NULL && e->rhs != NULL) {
        return eval_global_init_expr(tu, e->rhs, out_i, out_f, out_is_float, out_sym);
    }
    switch (e->kind) {
    case CC_EXPR_INT:
        *out_i = e->int_val;
        *out_f = (double)e->int_val;
        *out_is_float = 0;
        if (out_sym != NULL) {
            *out_sym = NULL;
        }
        return 0;
    case CC_EXPR_FLOAT:
        *out_i = (long)e->float_val;
        *out_f = e->float_val;
        *out_is_float = 1;
        if (out_sym != NULL) {
            *out_sym = NULL;
        }
        return 0;
    case CC_EXPR_IDENT:
        if (e->ident == NULL || e->ident[0] == '\0') {
            return -1;
        }
        *out_i = 0;
        *out_f = 0.0;
        *out_is_float = 0;
        if (out_sym != NULL) {
            *out_sym = xstrdup(e->ident);
            return *out_sym == NULL ? -1 : 0;
        }
        return 0;
    case CC_EXPR_ADDR:
        {
            char *sym = NULL;
            long addend = 0;
            *out_i = 0;
            *out_f = 0.0;
            *out_is_float = 0;
            if (eval_global_addr_symbol_addend(tu, e->lhs, &sym, &addend) != 0 || sym == NULL) {
                free(sym);
                return -1;
            }
            if (out_sym != NULL) {
                if (addend == 0) {
                    *out_sym = sym;
                } else {
                    char tmp[384];
                    snprintf(tmp, sizeof(tmp), "%s%+ld", sym, addend);
                    *out_sym = xstrdup(tmp);
                    free(sym);
                    return *out_sym == NULL ? -1 : 0;
                }
            } else {
                free(sym);
            }
            return 0;
        }
    case CC_EXPR_CAST:
        return eval_global_init_expr(tu, e->lhs, out_i, out_f, out_is_float, out_sym);
    case CC_EXPR_SIZEOF: {
        long n;
        if (e->lhs != NULL) {
            n = sizeof_expr_bytes(tu, NULL, 0, 0, e->lhs);
        } else {
            n = type_size_bytes_struct(tu, e->aux_type, e->aux_struct_id);
        }
        if (n < 0) {
            return -1;
        }
        *out_i = n;
        *out_f = (double)n;
        *out_is_float = 0;
        if (out_sym != NULL) {
            *out_sym = NULL;
        }
        return 0;
    }
    case CC_EXPR_TERNARY: {
        long cv = 0;
        double cf = 0.0;
        int cisf = 0;
        char *csym = NULL;
        if (e->lhs == NULL || e->third == NULL) {
            return -1;
        }
        if (eval_global_init_expr(tu, e->lhs, &cv, &cf, &cisf, &csym) != 0) {
            free(csym);
            return -1;
        }
        free(csym);
        if ((cisf ? cf : (double)cv) != 0.0) {
            if (e->rhs == NULL) {
                *out_i = cv;
                *out_f = cf;
                *out_is_float = cisf;
                if (out_sym != NULL) {
                    *out_sym = NULL;
                }
                return 0;
            }
            return eval_global_init_expr(tu, e->rhs, out_i, out_f, out_is_float, out_sym);
        }
        return eval_global_init_expr(tu, e->third, out_i, out_f, out_is_float, out_sym);
    }
    case CC_EXPR_BIN:
        if (e->op == CC_BIN_COMMA) {
            return eval_global_init_expr(tu, e->rhs, out_i, out_f, out_is_float, out_sym);
        }
        {
            long ai = 0;
            long bi = 0;
            double af = 0.0;
            double bf = 0.0;
            int aisf = 0;
            int bisf = 0;
            char *asym = NULL;
            char *bsym = NULL;

            if (eval_global_init_expr(tu, e->lhs, &ai, &af, &aisf, &asym) != 0 ||
                eval_global_init_expr(tu, e->rhs, &bi, &bf, &bisf, &bsym) != 0) {
                free(asym);
                free(bsym);
                return -1;
            }
            if (asym != NULL || bsym != NULL) {
                char *base = NULL;
                long addend = 0;
                char tmp[384];

                if (e->op == CC_BIN_ADD || e->op == CC_BIN_SUB) {
                    if (asym != NULL && bsym == NULL && !bisf) {
                        base = asym;
                        asym = NULL;
                        addend = (e->op == CC_BIN_ADD) ? bi : -bi;
                    } else if (asym == NULL && bsym != NULL && !aisf && e->op == CC_BIN_ADD) {
                        base = bsym;
                        bsym = NULL;
                        addend = ai;
                    }
                }
                free(asym);
                free(bsym);
                if (base == NULL) {
                    return -1;
                }
                *out_i = 0;
                *out_f = 0.0;
                *out_is_float = 0;
                if (out_sym != NULL) {
                    if (addend == 0) {
                        *out_sym = base;
                    } else {
                        snprintf(tmp, sizeof(tmp), "%s%+ld", base, addend);
                        *out_sym = xstrdup(tmp);
                        free(base);
                        if (*out_sym == NULL) {
                            return -1;
                        }
                    }
                } else {
                    free(base);
                }
                return 0;
            }

            if (aisf || bisf) {
                double a = aisf ? af : (double)ai;
                double b = bisf ? bf : (double)bi;
                switch (e->op) {
                case CC_BIN_ADD: *out_f = a + b; *out_is_float = 1; *out_i = (long)*out_f; return 0;
                case CC_BIN_SUB: *out_f = a - b; *out_is_float = 1; *out_i = (long)*out_f; return 0;
                case CC_BIN_MUL: *out_f = a * b; *out_is_float = 1; *out_i = (long)*out_f; return 0;
                case CC_BIN_DIV:
                    if (b == 0.0) return -1;
                    *out_f = a / b;
                    *out_is_float = 1;
                    *out_i = (long)*out_f;
                    return 0;
                case CC_BIN_EQ: *out_i = (a == b) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_NE: *out_i = (a != b) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_LT: *out_i = (a < b) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_LE: *out_i = (a <= b) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_GT: *out_i = (a > b) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_GE: *out_i = (a >= b) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_LAND: *out_i = (a != 0.0 && b != 0.0) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_LOR: *out_i = (a != 0.0 || b != 0.0) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                default:
                    return -1;
                }
            } else {
                switch (e->op) {
                case CC_BIN_ADD: *out_i = ai + bi; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_SUB: *out_i = ai - bi; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_MUL: *out_i = ai * bi; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_DIV:
                    if (bi == 0) return -1;
                    *out_i = ai / bi;
                    *out_f = (double)*out_i;
                    *out_is_float = 0;
                    return 0;
                case CC_BIN_MOD:
                    if (bi == 0) return -1;
                    *out_i = ai % bi;
                    *out_f = (double)*out_i;
                    *out_is_float = 0;
                    return 0;
                case CC_BIN_SHL: *out_i = ai << (bi & 63); *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_SHR: *out_i = ai >> (bi & 63); *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_BAND: *out_i = ai & bi; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_BXOR: *out_i = ai ^ bi; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_BOR: *out_i = ai | bi; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_EQ: *out_i = (ai == bi) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_NE: *out_i = (ai != bi) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_LT: *out_i = (ai < bi) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_LE: *out_i = (ai <= bi) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_GT: *out_i = (ai > bi) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_GE: *out_i = (ai >= bi) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_LAND: *out_i = (ai != 0 && bi != 0) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                case CC_BIN_LOR: *out_i = (ai != 0 || bi != 0) ? 1 : 0; *out_f = (double)*out_i; *out_is_float = 0; return 0;
                default:
                    return -1;
                }
            }
        }
    default:
        return -1;
    }
}

static int eval_global_init_item(const cc_translation_unit_t *tu, const cc_expr_t *e, cc_ssa_global_init_item_t *out) {
    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (e == NULL) {
        return 0;
    }
    if (e->kind == CC_EXPR_GENERIC) {
        const cc_expr_t *sel = selected_generic_expr(e);
        if (sel == NULL) {
            return -1;
        }
        return eval_global_init_item(tu, sel, out);
    }
    if (e->kind == CC_EXPR_MEMBER && e->lhs == NULL && e->rhs != NULL) {
        return eval_global_init_item(tu, e->rhs, out);
    }
    switch (e->kind) {
    case CC_EXPR_INT:
        out->init_i = e->int_val;
        out->init_f = (double)e->int_val;
        out->init_is_float = 0;
        return 0;
    case CC_EXPR_FLOAT:
        out->init_i = (long)e->float_val;
        out->init_f = e->float_val;
        out->init_is_float = 1;
        return 0;
    case CC_EXPR_STR:
        out->init_is_string = 1;
        out->init_str = xstrdup(e->ident != NULL ? e->ident : "\"\"");
        return out->init_str == NULL ? -1 : 0;
    case CC_EXPR_CAST:
        return eval_global_init_item(tu, e->lhs, out);
    case CC_EXPR_BIN:
        if (e->op == CC_BIN_COMMA) {
            return eval_global_init_item(tu, e->rhs, out);
        }
        break;
    default:
        break;
    }

    {
        long iv = 0;
        double fv = 0.0;
        int isf = 0;
        char *sym = NULL;
        if (eval_global_init_expr(tu, e, &iv, &fv, &isf, &sym) != 0) {
            free(sym);
            return -1;
        }
        out->init_i = iv;
        out->init_f = fv;
        out->init_is_float = isf;
        out->init_is_symbol = sym != NULL;
        out->init_sym = sym;
        return 0;
    }
}

typedef struct {
    long offset;
    long size;
    int is_string;
    char *payload;
} global_reloc_t;

static int find_struct_member_index_by_name(const cc_struct_def_t *sd, const char *name) {
    size_t i;
    if (sd == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < sd->member_count; ++i) {
        if (sd->members[i].name != NULL && strcmp(sd->members[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_struct_member_recursive(const cc_translation_unit_t *tu, int struct_id, const char *name,
                                        const cc_struct_member_t **out_member, long *out_off) {
    const cc_struct_def_t *sd;
    size_t i;

    if (tu == NULL || name == NULL || out_member == NULL || out_off == NULL ||
        struct_id < 0 || (size_t)struct_id >= tu->struct_count) {
        return -1;
    }

    sd = &tu->structs[struct_id];
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        if (m->name != NULL && strcmp(m->name, name) == 0) {
            *out_member = m;
            *out_off = m->offset;
            return 0;
        }
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        const cc_struct_member_t *sub_m = NULL;
        long sub_off = 0;
        if (m->name != NULL && m->name[0] != '\0') {
            continue;
        }
        if (m->type != CC_TYPE_VOID || m->type_struct_id < 0 ||
            (size_t)m->type_struct_id >= tu->struct_count) {
            continue;
        }
        if (find_struct_member_recursive(tu, m->type_struct_id, name, &sub_m, &sub_off) == 0) {
            *out_member = sub_m;
            *out_off = m->offset + sub_off;
            return 0;
        }
    }
    return -1;
}

static size_t struct_next_init_member_index(const cc_struct_def_t *sd, size_t member_idx) {
    size_t next = member_idx + 1;
    long off;
    if (sd == NULL || member_idx >= sd->member_count) {
        return next;
    }
    if (sd->members[member_idx].size == 0) {
        return next;
    }
    off = sd->members[member_idx].offset;
    while (next < sd->member_count && sd->members[next].offset == off) {
        next++;
    }
    return next;
}

static int struct_has_flexible_tail(const cc_struct_def_t *sd) {
    const cc_struct_member_t *m;
    if (sd == NULL || sd->member_count == 0) {
        return 0;
    }
    if (sd->has_flexible_array) {
        return 1;
    }
    m = &sd->members[sd->member_count - 1];
    return m->array_ndim > 0 && m->array_dims[m->array_ndim - 1] == 0;
}

static int append_global_reloc(global_reloc_t **arr, size_t *count, long offset, long size, const char *sym,
                               cc_diag_t *diag) {
    global_reloc_t *next;
    char *dup;

    if (arr == NULL || count == NULL || sym == NULL || size <= 0) {
        set_diag(diag, "invalid global relocation entry");
        return -1;
    }
    next = (global_reloc_t *)realloc(*arr, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        set_diag(diag, "out of memory allocating global relocation entries");
        return -1;
    }
    *arr = next;
    dup = xstrdup(sym);
    if (dup == NULL) {
        set_diag(diag, "out of memory duplicating global relocation symbol");
        return -1;
    }
    (*arr)[*count].offset = offset;
    (*arr)[*count].size = size;
    (*arr)[*count].is_string = 0;
    (*arr)[*count].payload = dup;
    (*count)++;
    return 0;
}

static int append_global_string_reloc(global_reloc_t **arr, size_t *count, long offset, long size, const char *str,
                                      cc_diag_t *diag) {
    global_reloc_t *next;
    char *dup;

    if (arr == NULL || count == NULL || str == NULL || size <= 0) {
        set_diag(diag, "invalid global string relocation entry");
        return -1;
    }
    next = (global_reloc_t *)realloc(*arr, (*count + 1) * sizeof(*next));
    if (next == NULL) {
        set_diag(diag, "out of memory allocating global string relocation entries");
        return -1;
    }
    *arr = next;
    dup = xstrdup(str);
    if (dup == NULL) {
        set_diag(diag, "out of memory duplicating global string relocation payload");
        return -1;
    }
    (*arr)[*count].offset = offset;
    (*arr)[*count].size = size;
    (*arr)[*count].is_string = 1;
    (*arr)[*count].payload = dup;
    (*count)++;
    return 0;
}

static int append_global_init_item(cc_ssa_global_t *g, const cc_ssa_global_init_item_t *it, cc_diag_t *diag) {
    cc_ssa_global_init_item_t *next;
    if (g == NULL || it == NULL) {
        return -1;
    }
    next = (cc_ssa_global_init_item_t *)realloc(g->init_items, (g->init_item_count + 1) * sizeof(*next));
    if (next == NULL) {
        set_diag(diag, "out of memory appending global initializer stream");
        return -1;
    }
    g->init_items = next;
    g->init_items[g->init_item_count++] = *it;
    return 0;
}

static void store_const_bytes(unsigned char *buf, long off, long size, unsigned long long v) {
    long j;
    for (j = 0; j < size; ++j) {
        buf[off + j] = (unsigned char)((v >> (8 * j)) & 0xFFu);
    }
}

static char *global_char_list_to_quoted_string(const cc_translation_unit_t *tu, const cc_expr_t *list) {
    size_t cap;
    size_t len = 0;
    size_t i;
    char *out;

    if (list == NULL || list->kind != CC_EXPR_INIT_LIST) {
        return NULL;
    }
    cap = list->arg_count * 4 + 8;
    out = (char *)malloc(cap);
    if (out == NULL) {
        return NULL;
    }
    out[len++] = '"';
    for (i = 0; i < list->arg_count; ++i) {
        long iv = 0;
        double fv = 0.0;
        int isf = 0;
        char *sym = NULL;
        unsigned char ch;
        const cc_expr_t *item = list->args[i];
        if (item != NULL && item->kind == CC_EXPR_INIT_LIST) {
            item = unwrap_scalar_initializer_expr(item, NULL);
            if (item == NULL) {
                free(out);
                return NULL;
            }
        }
        if (eval_global_init_expr(tu, item, &iv, &fv, &isf, &sym) != 0 || isf || sym != NULL) {
            free(sym);
            free(out);
            return NULL;
        }
        free(sym);
        if (iv < 0 || iv > 255) {
            free(out);
            return NULL;
        }
        ch = (unsigned char)iv;
        if (len + 5 >= cap) {
            size_t ncap = cap * 2;
            char *tmp = (char *)realloc(out, ncap);
            if (tmp == NULL) {
                free(out);
                return NULL;
            }
            out = tmp;
            cap = ncap;
        }
        switch (ch) {
        case '\\':
            out[len++] = '\\';
            out[len++] = '\\';
            break;
        case '"':
            out[len++] = '\\';
            out[len++] = '"';
            break;
        case '\n':
            out[len++] = '\\';
            out[len++] = 'n';
            break;
        case '\r':
            out[len++] = '\\';
            out[len++] = 'r';
            break;
        case '\t':
            out[len++] = '\\';
            out[len++] = 't';
            break;
        case '\0':
            out[len++] = '\\';
            out[len++] = '0';
            out[len++] = '0';
            out[len++] = '0';
            break;
        default:
            if (ch < 32 || ch >= 127) {
                static const char hex[] = "0123456789ABCDEF";
                out[len++] = '\\';
                out[len++] = 'x';
                out[len++] = hex[(ch >> 4) & 0xF];
                out[len++] = hex[ch & 0xF];
            } else {
                out[len++] = (char)ch;
            }
            break;
        }
    }
    out[len++] = '"';
    out[len] = '\0';
    return out;
}

static int store_scalar_global_init(const cc_translation_unit_t *tu, cc_type_t type, int struct_id, long field_size,
                                    const cc_expr_t *expr, unsigned char *buf, long buf_size, long off,
                                    global_reloc_t **relocs, size_t *reloc_count, cc_diag_t *diag) {
    long iv = 0;
    double fv = 0.0;
    int isf = 0;
    char *sym = NULL;

    (void)tu;
    (void)struct_id;

    if (expr != NULL && expr->kind == CC_EXPR_INIT_LIST) {
        expr = unwrap_scalar_initializer_expr(expr, diag);
        if (expr == NULL) {
            return -1;
        }
    }
    if (off < 0 || field_size <= 0 || off + field_size > buf_size) {
        set_diag(diag, "global initializer write exceeds destination object size");
        return -1;
    }

    if (expr != NULL && expr->kind == CC_EXPR_STR) {
        if (!is_pointer_type(type) || field_size != g_pointer_size_bytes) {
            set_diag(diag, "string scalar initializer requires pointer-sized pointer field");
            return -1;
        }
        if (append_global_string_reloc(relocs, reloc_count, off, field_size,
                                       expr->ident != NULL ? expr->ident : "\"\"", diag) != 0) {
            free(sym);
            return -1;
        }
        return 0;
    }
    if (expr != NULL && expr->kind == CC_EXPR_CAST && expr->lhs != NULL && expr->lhs->kind == CC_EXPR_INIT_LIST &&
        (expr->aux_type == CC_TYPE_PTR_CHAR || expr->aux_type == CC_TYPE_PTR_UCHAR)) {
        char *quoted = global_char_list_to_quoted_string(tu, expr->lhs);
        if (quoted != NULL) {
            if (is_pointer_type(type) && field_size == g_pointer_size_bytes) {
                if (append_global_string_reloc(relocs, reloc_count, off, field_size, quoted, diag) != 0) {
                    free(quoted);
                    return -1;
                }
                free(quoted);
                return 0;
            }
            free(quoted);
        }
    }

    if (eval_global_init_expr(tu, expr, &iv, &fv, &isf, &sym) != 0) {
        free(sym);
        if (diag != NULL && diag->message[0] == '\0') {
            if (expr != NULL) {
                snprintf(diag->message, sizeof(diag->message),
                         "unsupported scalar in global initializer (expr kind %d at %zu:%zu)", (int)expr->kind,
                         expr->line, expr->col);
            } else {
                snprintf(diag->message, sizeof(diag->message), "unsupported scalar in global initializer");
            }
        }
        return -1;
    }

    if (sym != NULL) {
        int ptr_sized_integral = is_integral_type(type) && field_size == g_pointer_size_bytes;
        if ((!is_pointer_type(type) && !ptr_sized_integral) || field_size != g_pointer_size_bytes) {
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message),
                         "symbol initializer requires pointer-sized field (sym=%s type=%d size=%ld expr_kind=%d at %zu:%zu)",
                         sym, (int)type, field_size, expr != NULL ? (int)expr->kind : -1, expr != NULL ? expr->line : 0,
                         expr != NULL ? expr->col : 0);
            }
            free(sym);
            return -1;
        }
        if (append_global_reloc(relocs, reloc_count, off, field_size, sym, diag) != 0) {
            free(sym);
            return -1;
        }
        free(sym);
        return 0;
    }

    if (isf && (type == CC_TYPE_FLOAT || type == CC_TYPE_DOUBLE)) {
        if (field_size == 4) {
            union {
                float f;
                uint32_t u;
            } cvt;
            cvt.f = (float)fv;
            store_const_bytes(buf, off, field_size, cvt.u);
            return 0;
        }
        if (field_size == 8) {
            union {
                double f;
                uint64_t u;
            } cvt;
            cvt.f = fv;
            store_const_bytes(buf, off, field_size, cvt.u);
            return 0;
        }
        set_diag(diag, "unsupported floating field size in global initializer");
        return -1;
    }

    store_const_bytes(buf, off, field_size, (unsigned long long)iv);
    return 0;
}

static int is_member_designator_expr(const cc_expr_t *e) {
    return e != NULL && e->kind == CC_EXPR_MEMBER && e->lhs == NULL && e->rhs != NULL && e->ident != NULL;
}

static const cc_expr_t *extract_struct_init_list_expr(const cc_expr_t *e, int struct_id) {
    if (e == NULL) {
        return NULL;
    }
    if (e->kind == CC_EXPR_INIT_LIST) {
        return e;
    }
    if (e->kind == CC_EXPR_CAST && e->aux_type == CC_TYPE_VOID && e->aux_struct_id == struct_id && e->lhs != NULL &&
        e->lhs->kind == CC_EXPR_INIT_LIST) {
        return e->lhs;
    }
    return NULL;
}

static const cc_expr_t *unwrap_self_designated_init_list(const cc_expr_t *init_list, const char *member_name) {
    const cc_expr_t *raw;
    if (init_list == NULL || init_list->kind != CC_EXPR_INIT_LIST || member_name == NULL || member_name[0] == '\0') {
        return init_list;
    }
    if (init_list->arg_count != 1 || init_list->args == NULL || init_list->args[0] == NULL) {
        return init_list;
    }
    raw = init_list->args[0];
    if (!is_member_designator_expr(raw) || raw->ident == NULL || raw->rhs == NULL) {
        return init_list;
    }
    if (strcmp(raw->ident, member_name) != 0) {
        return init_list;
    }
    return raw->rhs;
}

static int fill_fixed_char_array_from_string(const cc_expr_t *expr, cc_type_t elem_type, long elem_size, long max_elems,
                                             long field_off, unsigned char *buf, long buf_size, cc_diag_t *diag) {
    unsigned long *units = NULL;
    size_t unit_count = 0;
    size_t i;
    size_t ncopy;
    int wide;

    if (expr == NULL || expr->kind != CC_EXPR_STR || max_elems < 0 || elem_size <= 0) {
        return -1;
    }
    wide = (expr->aux_type == CC_TYPE_INT || expr->aux_type == CC_TYPE_UINT || expr->aux_type == CC_TYPE_LONG_LONG ||
            expr->aux_type == CC_TYPE_ULONG_LONG);
    if (!wide && !(elem_type == CC_TYPE_CHAR || elem_type == CC_TYPE_UCHAR)) {
        return -1;
    }
    if (wide && !is_integral_type(elem_type)) {
        return -1;
    }
    if (decode_string_units(expr, wide, &units, &unit_count) != 0) {
        set_diag(diag, "failed to decode string initializer");
        return -1;
    }
    ncopy = unit_count;
    if ((long)ncopy > max_elems) {
        ncopy = (size_t)max_elems;
    }
    for (i = 0; i < ncopy; ++i) {
        long off = field_off + (long)i * elem_size;
        if (off < 0 || off + elem_size > buf_size) {
            free(units);
            set_diag(diag, "string initializer write exceeds destination object size");
            return -1;
        }
        store_const_bytes(buf, off, elem_size, units[i]);
    }
    if ((long)unit_count < max_elems) {
        long off = field_off + (long)unit_count * elem_size;
        if (off < 0 || off + elem_size > buf_size) {
            free(units);
            set_diag(diag, "string terminator write exceeds destination object size");
            return -1;
        }
        store_const_bytes(buf, off, elem_size, 0);
    }
    free(units);
    return 0;
}

static int flatten_struct_init_bytes_cursor(const cc_translation_unit_t *tu, int struct_id, const cc_expr_t *init_list,
                                            size_t *cursor, long base, unsigned char *buf, long buf_size,
                                            global_reloc_t **relocs, size_t *reloc_count, cc_diag_t *diag);

static int flatten_arraylike_struct_member_from_cursor(const cc_translation_unit_t *tu, const cc_struct_member_t *m,
                                                       const cc_expr_t *src_list, size_t *cursor, long field_off,
                                                       unsigned char *buf, long buf_size, global_reloc_t **relocs,
                                                       size_t *reloc_count, cc_diag_t *diag) {
    cc_type_t elem_type;
    long elem_size;
    long max_elems;
    long j;

    if (m == NULL || src_list == NULL || src_list->kind != CC_EXPR_INIT_LIST || cursor == NULL) {
        set_diag(diag, "invalid array-like struct member initializer");
        return -1;
    }
    elem_type = ptr_base_type(m->type);
    elem_size = array_decl_scalar_size_bytes(tu, m->type, m->type_struct_id, m->array_ndim);
    if (elem_size <= 0) {
        elem_size = pointer_elem_size_bytes(tu, m->type, m->type_struct_id);
    }
    if (elem_size <= 0 || m->size <= 0) {
        set_diag(diag, "invalid array-like struct member extent");
        return -1;
    }
    max_elems = m->array_len > 0 ? m->array_len : (m->size / elem_size);
    for (j = 0; j < max_elems && *cursor < src_list->arg_count; ++j) {
        const cc_expr_t *raw = src_list->args[*cursor];
        long elem_off = field_off + j * elem_size;
        const cc_expr_t *elem_expr = raw;

        if (is_member_designator_expr(raw)) {
            break;
        }

        if (raw != NULL && raw->kind == CC_EXPR_STR) {
            if (fill_fixed_char_array_from_string(raw, elem_type, elem_size, max_elems, field_off, buf, buf_size,
                                                  diag) != 0) {
                return -1;
            }
            (*cursor)++;
            return 0;
        }

        if (elem_type == CC_TYPE_VOID && m->type_struct_id >= 0) {
            const cc_expr_t *sl = extract_struct_init_list_expr(raw, m->type_struct_id);
            if (sl != NULL) {
                size_t sub = 0;
                if (flatten_struct_init_bytes_cursor(tu, m->type_struct_id, sl, &sub, elem_off, buf, buf_size, relocs,
                                                     reloc_count, diag) != 0) {
                    return -1;
                }
                if (sub < sl->arg_count) {
                    set_diag(diag, "too many nested struct initializers in array-like member");
                    return -1;
                }
                (*cursor)++;
                continue;
            }
            if (flatten_struct_init_bytes_cursor(tu, m->type_struct_id, src_list, cursor, elem_off, buf, buf_size,
                                                 relocs, reloc_count, diag) != 0) {
                return -1;
            }
            continue;
        }

        if (raw != NULL && raw->kind == CC_EXPR_INIT_LIST) {
            elem_expr = unwrap_scalar_initializer_expr(raw, diag);
            if (elem_expr == NULL) {
                return -1;
            }
        }
        if (store_scalar_global_init(tu, elem_type, m->type_struct_id, elem_size, elem_expr, buf, buf_size, elem_off,
                                     relocs, reloc_count, diag) != 0) {
            return -1;
        }
        (*cursor)++;
    }
    return 0;
}

static int flatten_struct_init_bytes_cursor(const cc_translation_unit_t *tu, int struct_id, const cc_expr_t *init_list,
                                            size_t *cursor, long base, unsigned char *buf, long buf_size,
                                            global_reloc_t **relocs, size_t *reloc_count, cc_diag_t *diag) {
    const cc_struct_def_t *sd;
    size_t i;
    size_t next_member = 0;

    if (tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count) {
        set_diag(diag, "invalid struct type in global initializer flattening");
        return -1;
    }
    if (init_list == NULL || init_list->kind != CC_EXPR_INIT_LIST || cursor == NULL) {
        set_diag(diag, "struct global initializer must use braces");
        return -1;
    }

    sd = &tu->structs[struct_id];
    i = *cursor;
    while (i < init_list->arg_count) {
        const cc_expr_t *raw = init_list->args[i];
        const cc_expr_t *item = raw;
        const cc_struct_member_t *m = NULL;
        const cc_struct_member_t *nested_m = NULL;
        size_t member_idx = next_member;
        long field_off;
        long nested_off = 0;
        long scalar_size;
        int is_designator = 0;
        int is_nested_designator = 0;
        int consumed_item = 0;
        long natural_size;

        if (is_member_designator_expr(raw)) {
            int didx = find_struct_member_index_by_name(sd, raw->ident);
            if (didx < 0) {
                if (find_struct_member_recursive(tu, struct_id, raw->ident, &nested_m, &nested_off) != 0) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "unknown designated struct member '%s' in global initializer for struct %s",
                                 raw->ident != NULL ? raw->ident : "<null>",
                                 sd->tag != NULL ? sd->tag : "<anonymous>");
                    }
                    return -1;
                }
                is_nested_designator = 1;
            } else {
                member_idx = (size_t)didx;
            }
            is_designator = 1;
            item = raw->rhs;
            consumed_item = 1;
            i++;
        }
        if (!is_designator && member_idx >= sd->member_count) {
            break;
        }
        if (!is_nested_designator && member_idx >= sd->member_count) {
            i = init_list->arg_count;
            break;
        }

        m = is_nested_designator ? nested_m : &sd->members[member_idx];
        if (!is_nested_designator && !sd->is_union) {
            next_member = struct_next_init_member_index(sd, member_idx);
        } else if (!is_nested_designator && !is_designator) {
            /* A plain union initializer initializes a single member. */
            next_member = sd->member_count;
        }
        if (!is_nested_designator && struct_has_flexible_tail(sd) && member_idx + 1 == sd->member_count) {
            /* GNU-compatible extension: accept and ignore flexible-array initializers. */
            if (!consumed_item) {
                i++;
            }
            continue;
        }
        field_off = base + (is_nested_designator ? nested_off : m->offset);
        if (field_off < 0 || field_off + m->size > buf_size) {
            set_diag(diag, "struct member offset exceeds global initializer object");
            return -1;
        }

        natural_size = type_size_bytes_with_struct(tu, m->type, m->type_struct_id);

        if (sd->is_union && item != NULL && item->kind == CC_EXPR_INIT_LIST && m->type != CC_TYPE_VOID &&
            !is_array_object_decl(m->type, m->array_len, m->array_ndim)) {
            size_t ui;
            size_t pos = 0;
            for (ui = 0; ui < item->arg_count; ++ui) {
                const cc_expr_t *u_raw = item->args[ui];
                const cc_expr_t *u_item = u_raw;
                const cc_struct_member_t *um;
                int u_idx;
                long u_size;
                long u_off;
                if (is_member_designator_expr(u_raw)) {
                    u_idx = find_struct_member_index_by_name(sd, u_raw->ident);
                    if (u_idx < 0) {
                        set_diag(diag, "unknown designated union submember in global initializer");
                        return -1;
                    }
                    u_item = u_raw->rhs;
                } else {
                    u_idx = (int)pos++;
                }
                if (u_idx < 0 || (size_t)u_idx >= sd->member_count) {
                    break;
                }
                um = &sd->members[u_idx];
                if (um->type == CC_TYPE_VOID && um->type_struct_id >= 0) {
                    const cc_expr_t *u_nested = extract_struct_init_list_expr(u_item, um->type_struct_id);
                    if (u_nested != NULL) {
                        size_t sub = 0;
                        u_off = base + um->offset;
                        if (flatten_struct_init_bytes_cursor(tu, um->type_struct_id, u_nested, &sub, u_off, buf, buf_size,
                                                             relocs, reloc_count, diag) != 0) {
                            return -1;
                        }
                        continue;
                    }
                }
                if (u_item != NULL && u_item->kind == CC_EXPR_INIT_LIST) {
                    u_item = unwrap_scalar_initializer_expr(u_item, diag);
                    if (u_item == NULL) {
                        return -1;
                    }
                }
                u_size = type_size_bytes_with_struct(tu, um->type, um->type_struct_id);
                if (u_size <= 0) {
                    u_size = um->size;
                }
                if (u_size <= 0) {
                    continue;
                }
                u_off = base + um->offset;
                if (store_scalar_global_init(tu, um->type, um->type_struct_id, u_size, u_item, buf, buf_size, u_off,
                                             relocs, reloc_count, diag) != 0) {
                    return -1;
                }
            }
            if (!consumed_item) {
                i++;
            }
            continue;
        }

        if (m->type == CC_TYPE_VOID && m->type_struct_id >= 0) {
            const cc_expr_t *nested = extract_struct_init_list_expr(item, m->type_struct_id);
            nested = unwrap_self_designated_init_list(nested, m->name);
            if (nested != NULL) {
                size_t sub = 0;
                if (!consumed_item) {
                    i++;
                    consumed_item = 1;
                }
                if (flatten_struct_init_bytes_cursor(tu, m->type_struct_id, nested, &sub, field_off, buf, buf_size,
                                                     relocs, reloc_count, diag) != 0) {
                    return -1;
                }
                if (sub < nested->arg_count) {
                    set_diag(diag, "too many nested struct initializers");
                    return -1;
                }
                continue;
            }
            if (is_designator) {
                long z_i = 0;
                double z_f = 0.0;
                int z_is_float = 0;
                char *z_sym = NULL;
                if (eval_global_init_expr(tu, item, &z_i, &z_f, &z_is_float, &z_sym) == 0 && z_sym == NULL &&
                    (z_is_float ? (z_f == 0.0) : (z_i == 0))) {
                    free(z_sym);
                    continue;
                }
                free(z_sym);
                set_diag(diag, "nested struct global initializer requires braces");
                return -1;
            }
            if (flatten_struct_init_bytes_cursor(tu, m->type_struct_id, init_list, &i, field_off, buf, buf_size,
                                                 relocs, reloc_count, diag) != 0) {
                return -1;
            }
            continue;
        }

        if (is_array_object_decl(m->type, m->array_len, m->array_ndim)) {
            if (item != NULL && item->kind == CC_EXPR_INIT_LIST) {
                size_t sub = 0;
                if (!consumed_item) {
                    i++;
                    consumed_item = 1;
                }
                if (flatten_arraylike_struct_member_from_cursor(tu, m, item, &sub, field_off, buf, buf_size, relocs,
                                                                reloc_count, diag) != 0) {
                    return -1;
                }
                if (sub < item->arg_count) {
                    set_diag(diag, "too many elements for array-like struct member initializer");
                    return -1;
                }
                continue;
            }
            if (item != NULL && item->kind == CC_EXPR_STR) {
                cc_type_t elem_type = ptr_base_type(m->type);
                long elem_size = array_decl_scalar_size_bytes(tu, m->type, m->type_struct_id, m->array_ndim);
                long max_elems;
                if (elem_size <= 0) {
                    elem_size = pointer_elem_size_bytes(tu, m->type, m->type_struct_id);
                }
                max_elems = elem_size > 0 ? (m->array_len > 0 ? m->array_len : (m->size / elem_size)) : 0;
                if (fill_fixed_char_array_from_string(item, elem_type, elem_size, max_elems, field_off, buf, buf_size,
                                                      diag) != 0) {
                    set_diag(diag, "invalid string initializer for fixed-size array struct member");
                    return -1;
                }
                if (!consumed_item) {
                    i++;
                    consumed_item = 1;
                }
                continue;
            }
            if (flatten_arraylike_struct_member_from_cursor(tu, m, init_list, &i, field_off, buf, buf_size, relocs,
                                                            reloc_count, diag) != 0) {
                return -1;
            }
            continue;
        }

        if (!consumed_item) {
            i++;
            consumed_item = 1;
        }
        if (item != NULL && item->kind == CC_EXPR_INIT_LIST) {
            item = unwrap_scalar_initializer_expr(item, diag);
            if (item == NULL) {
                return -1;
            }
        }
        scalar_size = natural_size;
        if (scalar_size <= 0) {
            scalar_size = m->size;
        }
        if (m->size > 0 && scalar_size > m->size) {
            scalar_size = m->size;
        }
        if (scalar_size <= 0) {
            set_diag(diag, "unsupported scalar struct member size in global initializer");
            return -1;
        }
        if (store_scalar_global_init(tu, m->type, m->type_struct_id, scalar_size, item, buf, buf_size, field_off,
                                     relocs, reloc_count, diag) != 0) {
            return -1;
        }
    }
    *cursor = i;
    return 0;
}

static int flatten_struct_init_bytes(const cc_translation_unit_t *tu, int struct_id, const cc_expr_t *init, long base,
                                     unsigned char *buf, long buf_size, global_reloc_t **relocs, size_t *reloc_count,
                                     cc_diag_t *diag) {
    size_t cur = 0;
    const cc_struct_def_t *sd = NULL;
    if (tu != NULL && struct_id >= 0 && (size_t)struct_id < tu->struct_count) {
        sd = &tu->structs[struct_id];
    }
    if (flatten_struct_init_bytes_cursor(tu, struct_id, init, &cur, base, buf, buf_size, relocs, reloc_count, diag) !=
        0) {
        return -1;
    }
    (void)sd;
    return 0;
}

static int flatten_scalar_array_init_cursor(const cc_translation_unit_t *tu, cc_type_t scalar_type, int scalar_struct_id,
                                            const long *dims, int ndim, const cc_expr_t *list, size_t *cursor,
                                            long base, unsigned char *buf, long buf_size, global_reloc_t **relocs,
                                            size_t *reloc_count, cc_diag_t *diag) {
    long scalar_size;
    long i;

    if (tu == NULL || dims == NULL || ndim <= 0 || list == NULL || list->kind != CC_EXPR_INIT_LIST || cursor == NULL) {
        set_diag(diag, "invalid scalar array initializer");
        return -1;
    }
    scalar_size = type_size_bytes_with_struct(tu, scalar_type, scalar_struct_id);
    if (scalar_size <= 0) {
        set_diag(diag, "unsupported scalar array element type in global initializer");
        return -1;
    }
    if (dims[0] < 0) {
        set_diag(diag, "invalid array dimension in global initializer");
        return -1;
    }

    if (ndim == 1) {
        for (i = 0; i < dims[0] && *cursor < list->arg_count; ++i) {
            const cc_expr_t *item = list->args[*cursor];
            long elem_off = base + i * scalar_size;
            if (item != NULL && item->kind == CC_EXPR_INIT_LIST) {
                item = unwrap_scalar_initializer_expr(item, diag);
                if (item == NULL) {
                    return -1;
                }
            }
            if (store_scalar_global_init(tu, scalar_type, scalar_struct_id, scalar_size, item, buf, buf_size, elem_off,
                                         relocs, reloc_count, diag) != 0) {
                return -1;
            }
            (*cursor)++;
        }
        return 0;
    }

    {
        long sub_count = 1;
        long sub_size;
        int d;
        for (d = 1; d < ndim; ++d) {
            long dim = dims[d] > 0 ? dims[d] : 1;
            if (sub_count > LONG_MAX / dim) {
                set_diag(diag, "array initializer dimension overflow");
                return -1;
            }
            sub_count *= dim;
        }
        if (scalar_size > LONG_MAX / sub_count) {
            set_diag(diag, "array initializer byte-size overflow");
            return -1;
        }
        sub_size = scalar_size * sub_count;
        for (i = 0; i < dims[0] && *cursor < list->arg_count; ++i) {
            const cc_expr_t *raw = list->args[*cursor];
            long sub_base = base + i * sub_size;
            if (raw != NULL && raw->kind == CC_EXPR_STR && ndim == 2 && dims[1] >= 0) {
                if (fill_fixed_char_array_from_string(raw, scalar_type, scalar_size, dims[1], sub_base, buf, buf_size,
                                                      diag) != 0) {
                    return -1;
                }
                (*cursor)++;
                continue;
            }
            if (raw != NULL && raw->kind == CC_EXPR_INIT_LIST) {
                size_t sub_cur = 0;
                (*cursor)++;
                if (flatten_scalar_array_init_cursor(tu, scalar_type, scalar_struct_id, dims + 1, ndim - 1, raw,
                                                     &sub_cur, sub_base, buf, buf_size, relocs, reloc_count, diag) != 0) {
                    return -1;
                }
                if (sub_cur < raw->arg_count) {
                    set_diag(diag, "too many nested array initializers");
                    return -1;
                }
            } else {
                if (flatten_scalar_array_init_cursor(tu, scalar_type, scalar_struct_id, dims + 1, ndim - 1, list,
                                                     cursor, sub_base, buf, buf_size, relocs, reloc_count, diag) != 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int cmp_global_reloc(const void *a, const void *b) {
    const global_reloc_t *ra = (const global_reloc_t *)a;
    const global_reloc_t *rb = (const global_reloc_t *)b;
    if (ra->offset < rb->offset) {
        return -1;
    }
    if (ra->offset > rb->offset) {
        return 1;
    }
    if (ra->size < rb->size) {
        return -1;
    }
    if (ra->size > rb->size) {
        return 1;
    }
    return 0;
}

static int build_global_stream_from_bytes(cc_ssa_global_t *g, const unsigned char *buf, long total_size,
                                          global_reloc_t *relocs, size_t reloc_count, cc_diag_t *diag) {
    long off = 0;
    size_t ri = 0;

    if (reloc_count > 1) {
        qsort(relocs, reloc_count, sizeof(*relocs), cmp_global_reloc);
    }

    while (off < total_size) {
        cc_ssa_global_init_item_t it;
        memset(&it, 0, sizeof(it));
        if (ri < reloc_count && relocs[ri].offset == off) {
            if (relocs[ri].size != 4 && relocs[ri].size != 8) {
                set_diag(diag, "unsupported relocation size in global initializer stream");
                return -1;
            }
            it.init_size = relocs[ri].size;
            if (relocs[ri].is_string) {
                it.init_is_string = 1;
                it.init_str = xstrdup(relocs[ri].payload);
                if (it.init_str == NULL) {
                    set_diag(diag, "out of memory duplicating relocation string in global initializer stream");
                    return -1;
                }
            } else {
                it.init_is_symbol = 1;
                it.init_sym = xstrdup(relocs[ri].payload);
                if (it.init_sym == NULL) {
                    set_diag(diag, "out of memory duplicating relocation symbol in global initializer stream");
                    return -1;
                }
            }
            if (append_global_init_item(g, &it, diag) != 0) {
                free(it.init_sym);
                free(it.init_str);
                return -1;
            }
            off += relocs[ri].size;
            ri++;
            continue;
        }
        if (ri < reloc_count && relocs[ri].offset < off) {
            set_diag(diag, "overlapping relocation entries in global initializer stream");
            return -1;
        }
        it.init_size = 1;
        it.init_i = (long)buf[off];
        if (append_global_init_item(g, &it, diag) != 0) {
            return -1;
        }
        off++;
    }
    return 0;
}

static void free_global_relocs(global_reloc_t *relocs, size_t count) {
    size_t i;
    for (i = 0; i < count; ++i) {
        free(relocs[i].payload);
    }
    free(relocs);
}

static int __attribute__((unused)) expr_calls_named_fn(const cc_expr_t *e, const char *name);

static int __attribute__((unused)) stmt_calls_named_fn(const cc_stmt_t *s, const char *name) {
    size_t i;

    if (s == NULL || name == NULL || name[0] == '\0') {
        return 0;
    }
    if (expr_calls_named_fn(s->expr, name)) {
        return 1;
    }
    if (expr_calls_named_fn(s->init_expr, name) || expr_calls_named_fn(s->post_expr, name)) {
        return 1;
    }
    if (stmt_calls_named_fn(s->init_stmt, name) || stmt_calls_named_fn(s->then_branch, name) ||
        stmt_calls_named_fn(s->else_branch, name)) {
        return 1;
    }
    for (i = 0; i < s->block_count; ++i) {
        if (stmt_calls_named_fn(&s->block_stmts[i], name)) {
            return 1;
        }
    }
    for (i = 0; i < s->asm_output_count; ++i) {
        if (expr_calls_named_fn(s->asm_outputs[i].expr, name)) {
            return 1;
        }
    }
    for (i = 0; i < s->asm_input_count; ++i) {
        if (expr_calls_named_fn(s->asm_inputs[i].expr, name)) {
            return 1;
        }
    }
    return 0;
}

static int __attribute__((unused)) expr_calls_named_fn(const cc_expr_t *e, const char *name) {
    size_t i;

    if (e == NULL || name == NULL || name[0] == '\0') {
        return 0;
    }
    if (e->kind == CC_EXPR_CALL && e->ident != NULL && strcmp(e->ident, name) == 0) {
        return 1;
    }
    if (expr_calls_named_fn(e->lhs, name) || expr_calls_named_fn(e->rhs, name) || expr_calls_named_fn(e->third, name)) {
        return 1;
    }
    for (i = 0; i < e->arg_count; ++i) {
        if (expr_calls_named_fn(e->args[i], name)) {
            return 1;
        }
    }
    for (i = 0; i < e->stmt_expr_count; ++i) {
        if (stmt_calls_named_fn(&e->stmt_expr_stmts[i], name)) {
            return 1;
        }
    }
    return 0;
}

static int __attribute__((unused)) tu_has_direct_call_to(const cc_translation_unit_t *tu, const char *name) {
    size_t i;
    size_t j;

    if (tu == NULL || name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < tu->func_count; ++i) {
        const cc_function_t *f = &tu->funcs[i];
        if (!f->has_body) {
            continue;
        }
        for (j = 0; j < f->stmt_count; ++j) {
            if (stmt_calls_named_fn(&f->stmts[j], name)) {
                return 1;
            }
        }
    }
    return 0;
}

static int should_skip_fn_body_for_codegen(const cc_translation_unit_t *tu, const cc_function_t *f) {
    size_t i;
    (void)tu;
    if (f == NULL || !f->has_body) {
        return 1;
    }
    /*
     * Keep system GMP extern-inline helper bodies header-only.
     * gmp.h emits extern-inline wrappers that should not become TU-local
     * out-of-line definitions.
     */
    if ((f->storage & CC_STORAGE_INLINE) != 0 && (f->storage & CC_STORAGE_EXTERN) != 0 && f->name != NULL &&
        strncmp(f->name, "__gmp", 5) == 0) {
        return 1;
    }
    for (i = 0; i < f->stmt_count; ++i) {
        if (stmt_calls_named_fn(&f->stmts[i], "__builtin_va_arg_pack")) {
            return 1;
        }
    }
    if ((f->storage & CC_STORAGE_STATIC) == 0) {
        return 0;
    }
    if ((f->storage & CC_STORAGE_INLINE) == 0) {
        return 0;
    }
    if ((f->attr_flags & CC_ATTR_UNUSED) == 0) {
        return 0;
    }
    return 1;
}

int cc_ast_to_ssa(const cc_translation_unit_t *tu, cc_ssa_module_t *out, cc_diag_t *diag) {
    size_t i;
    size_t def_count = 0;
    size_t out_i = 0;

    cc_ssa_module_init(out);
    if (diag != NULL) {
        diag->path[0] = '\0';
        diag->line = 0;
        diag->col = 0;
        diag->error_count = 0;
        diag->message[0] = '\0';
    }

    if (tu->global_count > 0) {
        out->globals = (cc_ssa_global_t *)calloc(tu->global_count, sizeof(*out->globals));
        if (out->globals == NULL) {
            set_diag(diag, "out of memory allocating SSA globals");
            return -1;
        }
        out->global_count = tu->global_count;
        for (i = 0; i < tu->global_count; ++i) {
            long init_i = 0;
            double init_f = 0.0;
            int init_is_float = 0;
            int init_is_string = 0;
            int force_bss_zero = 0;
            int is_struct_global = 0;
            int struct_id = -1;
            long struct_size = 0;
            long struct_elems = 0;
            char *init_sym = NULL;
            const cc_expr_t *init = tu->globals[i].init;
            out->globals[i].name = xstrdup(tu->globals[i].name);
            out->globals[i].type = tu->globals[i].type;
            out->globals[i].type_struct_id = tu->globals[i].type_struct_id;
            out->globals[i].array_len = tu->globals[i].array_len;
            out->globals[i].storage = tu->globals[i].storage;
            out->globals[i].attr_flags = tu->globals[i].attr_flags;
            out->globals[i].attr_align = tu->globals[i].attr_align;
            if (getenv("CC_DEBUG_GLOBAL_TYPE") != NULL && tu->globals[i].name != NULL &&
                strcmp(tu->globals[i].name, "sort_functions") == 0) {
                fprintf(stderr, "cc-debug: sort_functions type=%d array_ndim=%d dims=%ld,%ld,%ld,%ld len=%ld\n",
                        (int)tu->globals[i].type, tu->globals[i].array_ndim, tu->globals[i].array_dims[0],
                        tu->globals[i].array_dims[1], tu->globals[i].array_dims[2], tu->globals[i].array_dims[3],
                        tu->globals[i].array_len);
            }
            if (tu->globals[i].attr_section != NULL) {
                out->globals[i].attr_section = xstrdup(tu->globals[i].attr_section);
                if (out->globals[i].attr_section == NULL) {
                    set_diag(diag, "out of memory duplicating global section attribute");
                    cc_ssa_module_free(out);
                    return -1;
                }
            }
            if (tu->globals[i].attr_alias != NULL) {
                out->globals[i].attr_alias = xstrdup(tu->globals[i].attr_alias);
                if (out->globals[i].attr_alias == NULL) {
                    set_diag(diag, "out of memory duplicating global alias attribute");
                    cc_ssa_module_free(out);
                    return -1;
                }
            }
            if (out->globals[i].name == NULL) {
                set_diag(diag, "out of memory duplicating global name");
                cc_ssa_module_free(out);
                return -1;
            }

            if ((tu->globals[i].storage & CC_STORAGE_EXTERN) != 0 && init == NULL) {
                out->globals[i].has_init = 0;
                out->globals[i].init_i = 0;
                out->globals[i].init_f = 0.0;
                out->globals[i].init_is_float = 0;
                out->globals[i].init_is_string = 0;
                out->globals[i].init_is_symbol = 0;
                out->globals[i].init_sym = NULL;
                continue;
            }

            if (tu->globals[i].type == CC_TYPE_VOID && tu->globals[i].type_struct_id >= 0) {
                is_struct_global = 1;
                struct_id = tu->globals[i].type_struct_id;
                struct_elems = 1;
            } else if (is_array_object_decl(tu->globals[i].type, tu->globals[i].array_len, tu->globals[i].array_ndim) &&
                       ptr_base_type(tu->globals[i].type) == CC_TYPE_VOID && tu->globals[i].type_struct_id >= 0) {
                is_struct_global = 1;
                struct_id = tu->globals[i].type_struct_id;
                struct_elems = tu->globals[i].array_len;
            }

            if (is_struct_global) {
                global_reloc_t *relocs = NULL;
                size_t reloc_count = 0;
                unsigned char *buf = NULL;
                long total_size;
                int any_nonzero = 0;
                size_t z;
                const cc_expr_t *struct_init = init;

                if (tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count ||
                    !tu->structs[struct_id].complete || tu->structs[struct_id].size <= 0) {
                    if (diag != NULL) {
                        snprintf(diag->message, sizeof(diag->message),
                                 "invalid struct type in global lowering: %s (sid=%d count=%zu complete=%d size=%ld)",
                                 tu->globals[i].name != NULL ? tu->globals[i].name : "<anon>", struct_id,
                                 tu != NULL ? tu->struct_count : 0,
                                 (tu != NULL && struct_id >= 0 && (size_t)struct_id < tu->struct_count)
                                     ? tu->structs[struct_id].complete
                                     : 0,
                                 (tu != NULL && struct_id >= 0 && (size_t)struct_id < tu->struct_count)
                                     ? tu->structs[struct_id].size
                                     : 0L);
                    } else {
                        set_diag(diag, "invalid struct type in global lowering");
                    }
                    cc_ssa_module_free(out);
                    return -1;
                }

                struct_size = tu->structs[struct_id].size;
                if (struct_elems <= 0) {
                    if (init != NULL && init->kind == CC_EXPR_INIT_LIST && init->arg_count > 0 &&
                        ptr_base_type(tu->globals[i].type) == CC_TYPE_VOID) {
                        struct_elems = (long)init->arg_count;
                    } else {
                        struct_elems = 1;
                    }
                }
                total_size = struct_size * struct_elems;
                if (total_size <= 0) {
                    set_diag(diag, "invalid struct global object size");
                    cc_ssa_module_free(out);
                    return -1;
                }

                out->globals[i].type = CC_TYPE_PTR_UCHAR;
                out->globals[i].type_struct_id = -1;
                out->globals[i].array_len = total_size;

                if (init == NULL) {
                    out->globals[i].has_init = 0;
                    out->globals[i].init_i = 0;
                    out->globals[i].init_f = 0.0;
                    out->globals[i].init_is_float = 0;
                    out->globals[i].init_is_string = 0;
                    out->globals[i].init_is_symbol = 0;
                    out->globals[i].init_sym = NULL;
                    continue;
                }
                if (tu->globals[i].type == CC_TYPE_VOID && init->kind == CC_EXPR_CAST && init->aux_type == CC_TYPE_VOID &&
                    init->aux_struct_id == struct_id && init->lhs != NULL && init->lhs->kind == CC_EXPR_INIT_LIST) {
                    struct_init = init->lhs;
                }
                if (struct_init->kind != CC_EXPR_INIT_LIST) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "struct global initializer for %s must use braces", tu->globals[i].name);
                    }
                    cc_ssa_module_free(out);
                    return -1;
                }

                buf = (unsigned char *)calloc((size_t)total_size, 1);
                if (buf == NULL) {
                    set_diag(diag, "out of memory allocating struct global init buffer");
                    cc_ssa_module_free(out);
                    return -1;
                }

                if (tu->globals[i].type == CC_TYPE_VOID) {
                    if (flatten_struct_init_bytes(tu, struct_id, struct_init, 0, buf, total_size, &relocs, &reloc_count,
                                                  diag) != 0) {
                        if (diag != NULL && strcmp(diag->message, "too many items in struct global initializer") == 0) {
                            snprintf(diag->message, sizeof(diag->message), "too many items in struct global initializer for %s",
                                     tu->globals[i].name);
                        }
                        free(buf);
                        free_global_relocs(relocs, reloc_count);
                        cc_ssa_module_free(out);
                        return -1;
                    }
                } else {
                    size_t cur = 0;
                    long j;
                    for (j = 0; j < struct_elems && cur < struct_init->arg_count; ++j) {
                        const cc_expr_t *raw = struct_init->args[cur];
                        const cc_expr_t *elem_list = extract_struct_init_list_expr(raw, struct_id);
                        if (elem_list != NULL) {
                            size_t sub = 0;
                            cur++;
                            if (flatten_struct_init_bytes_cursor(tu, struct_id, elem_list, &sub, j * struct_size, buf,
                                                                 total_size, &relocs, &reloc_count, diag) != 0) {
                                free(buf);
                                free_global_relocs(relocs, reloc_count);
                                cc_ssa_module_free(out);
                                return -1;
                            }
                            if (sub < elem_list->arg_count) {
                                set_diag(diag, "too many initializers in struct array element");
                                free(buf);
                                free_global_relocs(relocs, reloc_count);
                                cc_ssa_module_free(out);
                                return -1;
                            }
                            continue;
                        }
                        if (flatten_struct_init_bytes_cursor(tu, struct_id, struct_init, &cur, j * struct_size, buf, total_size,
                                                             &relocs, &reloc_count, diag) != 0) {
                            free(buf);
                            free_global_relocs(relocs, reloc_count);
                            cc_ssa_module_free(out);
                            return -1;
                        }
                    }
                    if (cur < struct_init->arg_count) {
                        set_diag(diag, "too many struct elements in array initializer");
                        free(buf);
                        free_global_relocs(relocs, reloc_count);
                        cc_ssa_module_free(out);
                        return -1;
                    }
                }

                for (z = 0; z < (size_t)total_size; ++z) {
                    if (buf[z] != 0) {
                        any_nonzero = 1;
                        break;
                    }
                }
                if (!any_nonzero && reloc_count == 0) {
                    out->globals[i].has_init = 0;
                    out->globals[i].init_i = 0;
                    out->globals[i].init_f = 0.0;
                    out->globals[i].init_is_float = 0;
                    out->globals[i].init_is_string = 0;
                    out->globals[i].init_is_symbol = 0;
                    out->globals[i].init_sym = NULL;
                    free(buf);
                    free_global_relocs(relocs, reloc_count);
                    continue;
                }

                if (build_global_stream_from_bytes(&out->globals[i], buf, total_size, relocs, reloc_count, diag) !=
                    0) {
                    free(buf);
                    free_global_relocs(relocs, reloc_count);
                    cc_ssa_module_free(out);
                    return -1;
                }
                out->globals[i].has_init = 1;
                out->globals[i].init_i = 0;
                out->globals[i].init_f = 0.0;
                out->globals[i].init_is_float = 0;
                out->globals[i].init_is_string = 0;
                out->globals[i].init_is_symbol = 0;
                out->globals[i].init_sym = NULL;
                free(buf);
                free_global_relocs(relocs, reloc_count);
                continue;
            }

            if (init != NULL && init->kind == CC_EXPR_ADDR && init->lhs != NULL && init->lhs->kind == CC_EXPR_CAST &&
                init->lhs->aux_type == CC_TYPE_VOID && init->lhs->aux_struct_id >= 0 && init->lhs->lhs != NULL &&
                init->lhs->lhs->kind == CC_EXPR_INIT_LIST) {
                cc_ssa_global_t lit;
                global_reloc_t *relocs = NULL;
                size_t reloc_count = 0;
                unsigned char *buf = NULL;
                long lit_sid = init->lhs->aux_struct_id;
                long lit_size;
                int any_nonzero = 0;
                size_t z;
                char symbuf[256];

                if ((size_t)lit_sid >= tu->struct_count || !tu->structs[lit_sid].complete || tu->structs[lit_sid].size <= 0) {
                    set_diag(diag, "invalid compound literal struct type in global initializer");
                    cc_ssa_module_free(out);
                    return -1;
                }
                if (snprintf(symbuf, sizeof(symbuf), "__cc_clit_%s_%zu", tu->globals[i].name, i) >= (int)sizeof(symbuf)) {
                    set_diag(diag, "compound literal symbol name too long");
                    cc_ssa_module_free(out);
                    return -1;
                }
                lit_size = tu->structs[lit_sid].size;
                buf = (unsigned char *)calloc((size_t)lit_size, 1);
                if (buf == NULL) {
                    set_diag(diag, "out of memory allocating compound literal storage");
                    cc_ssa_module_free(out);
                    return -1;
                }
                if (flatten_struct_init_bytes(tu, lit_sid, init->lhs->lhs, 0, buf, lit_size, &relocs, &reloc_count,
                                              diag) != 0) {
                    if (diag != NULL && strcmp(diag->message, "too many items in struct global initializer") == 0) {
                        snprintf(diag->message, sizeof(diag->message),
                                 "too many items in compound-literal struct initializer for %s", tu->globals[i].name);
                    }
                    free(buf);
                    free_global_relocs(relocs, reloc_count);
                    cc_ssa_module_free(out);
                    return -1;
                }
                memset(&lit, 0, sizeof(lit));
                lit.name = xstrdup(symbuf);
                lit.type = CC_TYPE_PTR_UCHAR;
                lit.type_struct_id = -1;
                lit.array_len = lit_size;
                lit.storage = CC_STORAGE_STATIC;
                if (lit.name == NULL) {
                    set_diag(diag, "out of memory duplicating compound literal symbol name");
                    free(buf);
                    free_global_relocs(relocs, reloc_count);
                    cc_ssa_module_free(out);
                    return -1;
                }
                for (z = 0; z < (size_t)lit_size; ++z) {
                    if (buf[z] != 0) {
                        any_nonzero = 1;
                        break;
                    }
                }
                if (any_nonzero || reloc_count > 0) {
                    if (build_global_stream_from_bytes(&lit, buf, lit_size, relocs, reloc_count, diag) != 0) {
                        free(buf);
                        free_global_relocs(relocs, reloc_count);
                        free(lit.name);
                        cc_ssa_module_free(out);
                        return -1;
                    }
                    lit.has_init = 1;
                } else {
                    lit.has_init = 0;
                }
                free(buf);
                free_global_relocs(relocs, reloc_count);
                if (append_synth_global(out, &lit) != 0) {
                    free(lit.name);
                    set_diag(diag, "out of memory appending compound literal global");
                    cc_ssa_module_free(out);
                    return -1;
                }
                init_sym = xstrdup(symbuf);
                if (init_sym == NULL) {
                    set_diag(diag, "out of memory duplicating compound literal symbol reference");
                    cc_ssa_module_free(out);
                    return -1;
                }
                out->globals[i].has_init = 1;
                out->globals[i].init_i = 0;
                out->globals[i].init_f = 0.0;
                out->globals[i].init_is_float = 0;
                out->globals[i].init_is_string = 0;
                out->globals[i].init_is_symbol = 1;
                out->globals[i].init_sym = init_sym;
                continue;
            }

            if (init != NULL && init->kind == CC_EXPR_INIT_LIST) {
                size_t j;
                if (!is_pointer_type(out->globals[i].type) || out->globals[i].array_len < 0) {
                    if (out->globals[i].type == CC_TYPE_VOID && out->globals[i].type_struct_id >= 0) {
                        if (init->arg_count == 0) {
                            force_bss_zero = 1;
                        } else if (init->arg_count == 1) {
                            long z_i = 0;
                            double z_f = 0.0;
                            int z_is_float = 0;
                            char *z_sym = NULL;
                            if (eval_global_init_expr(tu, init->args[0], &z_i, &z_f, &z_is_float, &z_sym) != 0 ||
                                z_sym != NULL ||
                                (z_is_float ? (z_f != 0.0) : (z_i != 0))) {
                                free(z_sym);
                                if (diag != NULL && diag->message[0] == '\0') {
                                    snprintf(diag->message, sizeof(diag->message),
                                             "unsupported struct initializer for global %s", tu->globals[i].name);
                                }
                                cc_ssa_module_free(out);
                                return -1;
                            }
                            free(z_sym);
                            force_bss_zero = 1;
                        } else {
                            if (diag != NULL && diag->message[0] == '\0') {
                                snprintf(diag->message, sizeof(diag->message),
                                         "unsupported struct initializer for global %s", tu->globals[i].name);
                            }
                            cc_ssa_module_free(out);
                            return -1;
                        }
                    } else {
                        if (diag != NULL && diag->message[0] == '\0') {
                            snprintf(diag->message, sizeof(diag->message),
                                     "unsupported list initializer for non-array global %s", tu->globals[i].name);
                        }
                        cc_ssa_module_free(out);
                        return -1;
                    }
                }
                if (force_bss_zero) {
                    out->globals[i].has_init = 0;
                    out->globals[i].init_i = 0;
                    out->globals[i].init_f = 0.0;
                    out->globals[i].init_is_float = 0;
                    out->globals[i].init_is_string = 0;
                    out->globals[i].init_is_symbol = 0;
                    out->globals[i].init_sym = NULL;
                    continue;
                }
                if (is_pointer_type(out->globals[i].type) && out->globals[i].array_len >= 0 &&
                    tu->globals[i].array_ndim > 0 && ptr_base_type(tu->globals[i].type) != CC_TYPE_VOID) {
                    int needs_flatten = tu->globals[i].array_ndim > 1;
                    size_t jj;
                    for (jj = 0; jj < init->arg_count; ++jj) {
                        if (init->args[jj] != NULL && init->args[jj]->kind == CC_EXPR_INIT_LIST) {
                            needs_flatten = 1;
                            break;
                        }
                    }
                    if (needs_flatten) {
                        cc_type_t scalar_type = tu->globals[i].type;
                        int scalar_sid = tu->globals[i].type_struct_id;
                        long dims[CC_MAX_ARRAY_DIMS];
                        int nd = tu->globals[i].array_ndim;
                        long scalar_size;
                        long elem_count = 1;
                        long total_size;
                        unsigned char *buf = NULL;
                        global_reloc_t *relocs = NULL;
                        size_t reloc_count = 0;
                        int any_nonzero = 0;
                        size_t cur = 0;
                        size_t z;
                        int d;

                        if (nd > CC_MAX_ARRAY_DIMS) {
                            set_diag(diag, "array dimension count exceeds compiler limit");
                            cc_ssa_module_free(out);
                            return -1;
                        }
                        for (d = 0; d < nd; ++d) {
                            dims[d] = tu->globals[i].array_dims[d];
                        }
                        if (dims[0] <= 0) {
                            dims[0] = tu->globals[i].array_len > 0 ? tu->globals[i].array_len : (long)init->arg_count;
                        }
                        for (d = 0; d < nd; ++d) {
                            if (dims[d] <= 0) {
                                set_diag(diag, "invalid array dimension in global initializer");
                                cc_ssa_module_free(out);
                                return -1;
                            }
                        }
                        for (d = 0; d < nd; ++d) {
                            if (!is_pointer_type(scalar_type)) {
                                set_diag(diag, "malformed array type in global initializer");
                                cc_ssa_module_free(out);
                                return -1;
                            }
                            scalar_type = ptr_base_type(scalar_type);
                            if (scalar_type != CC_TYPE_VOID) {
                                scalar_sid = -1;
                            }
                        }
                        scalar_size = type_size_bytes_with_struct(tu, scalar_type, scalar_sid);
                        if (scalar_size <= 0) {
                            set_diag(diag, "unsupported scalar array element type in global initializer");
                            cc_ssa_module_free(out);
                            return -1;
                        }
                        for (d = 0; d < nd; ++d) {
                            if (elem_count > LONG_MAX / dims[d]) {
                                set_diag(diag, "array element count overflow in global initializer");
                                cc_ssa_module_free(out);
                                return -1;
                            }
                            elem_count *= dims[d];
                        }
                        if (scalar_size > LONG_MAX / elem_count) {
                            set_diag(diag, "array byte-size overflow in global initializer");
                            cc_ssa_module_free(out);
                            return -1;
                        }
                        total_size = scalar_size * elem_count;
                        buf = (unsigned char *)calloc((size_t)total_size, 1);
                        if (buf == NULL) {
                            set_diag(diag, "out of memory allocating global array initializer buffer");
                            cc_ssa_module_free(out);
                            return -1;
                        }
                        if (flatten_scalar_array_init_cursor(tu, scalar_type, scalar_sid, dims, nd, init, &cur, 0, buf,
                                                             total_size, &relocs, &reloc_count, diag) != 0) {
                            free(buf);
                            free_global_relocs(relocs, reloc_count);
                            cc_ssa_module_free(out);
                            return -1;
                        }
                        if (cur < init->arg_count) {
                            set_diag(diag, "too many elements in global array initializer");
                            free(buf);
                            free_global_relocs(relocs, reloc_count);
                            cc_ssa_module_free(out);
                            return -1;
                        }
                        for (z = 0; z < (size_t)total_size; ++z) {
                            if (buf[z] != 0) {
                                any_nonzero = 1;
                                break;
                            }
                        }
                        out->globals[i].type = CC_TYPE_PTR_UCHAR;
                        out->globals[i].type_struct_id = -1;
                        out->globals[i].array_len = total_size;
                        if (!any_nonzero && reloc_count == 0) {
                            out->globals[i].has_init = 0;
                            out->globals[i].init_i = 0;
                            out->globals[i].init_f = 0.0;
                            out->globals[i].init_is_float = 0;
                            out->globals[i].init_is_string = 0;
                            out->globals[i].init_is_symbol = 0;
                            out->globals[i].init_sym = NULL;
                            free(buf);
                            free_global_relocs(relocs, reloc_count);
                            continue;
                        }
                        if (build_global_stream_from_bytes(&out->globals[i], buf, total_size, relocs, reloc_count,
                                                           diag) != 0) {
                            free(buf);
                            free_global_relocs(relocs, reloc_count);
                            cc_ssa_module_free(out);
                            return -1;
                        }
                        out->globals[i].has_init = 1;
                        out->globals[i].init_i = 0;
                        out->globals[i].init_f = 0.0;
                        out->globals[i].init_is_float = 0;
                        out->globals[i].init_is_string = 0;
                        out->globals[i].init_is_symbol = 0;
                        out->globals[i].init_sym = NULL;
                        free(buf);
                        free_global_relocs(relocs, reloc_count);
                        continue;
                    }
                }
                if (init->arg_count == 0) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message), "empty initializer list for global %s",
                                 tu->globals[i].name);
                    }
                    cc_ssa_module_free(out);
                    return -1;
                }
                out->globals[i].init_items =
                    (cc_ssa_global_init_item_t *)calloc(init->arg_count, sizeof(*out->globals[i].init_items));
                if (out->globals[i].init_items == NULL) {
                    set_diag(diag, "out of memory allocating global initializer list");
                    cc_ssa_module_free(out);
                    return -1;
                }
                out->globals[i].init_item_count = init->arg_count;
                for (j = 0; j < init->arg_count; ++j) {
                    if (eval_global_init_item(tu, init->args[j], &out->globals[i].init_items[j]) != 0) {
                        if (diag != NULL && diag->message[0] == '\0') {
                            snprintf(diag->message, sizeof(diag->message),
                                     "unsupported global initializer item %zu for %s", j, tu->globals[i].name);
                        }
                        cc_ssa_module_free(out);
                        return -1;
                    }
                }
                {
                    cc_type_t base_t = ptr_base_type(tu->globals[i].type);
                    if (tu->globals[i].array_ndim == 1 && (base_t == CC_TYPE_CHAR || base_t == CC_TYPE_UCHAR)) {
                        long inferred_len = 0;
                        for (j = 0; j < init->arg_count; ++j) {
                            const cc_expr_t *it = init->args[j];
                            if (it != NULL && it->kind == CC_EXPR_STR) {
                                unsigned long *units = NULL;
                                size_t unit_count = 0;
                                if (decode_string_units(it, 0, &units, &unit_count) == 0) {
                                    inferred_len += (long)(unit_count + 1);
                                } else {
                                    inferred_len += 1;
                                }
                                free(units);
                            } else {
                                inferred_len += 1;
                            }
                        }
                        if (inferred_len <= 0) {
                            inferred_len = (long)init->arg_count;
                        }
                        if (out->globals[i].array_len <= 0 || inferred_len > out->globals[i].array_len) {
                            out->globals[i].array_len = inferred_len;
                        }
                    } else if (out->globals[i].array_len <= 0) {
                        out->globals[i].array_len = (long)init->arg_count;
                    }
                }
            } else if (init != NULL && init->kind == CC_EXPR_STR) {
                init_is_string = 1;
                out->globals[i].init_str = xstrdup(init->ident != NULL ? init->ident : "\"\"");
                if (out->globals[i].init_str == NULL) {
                    set_diag(diag, "out of memory duplicating global string initializer");
                    cc_ssa_module_free(out);
                    return -1;
                }
            } else if (init != NULL && init->kind == CC_EXPR_CAST && init->lhs != NULL &&
                       init->lhs->kind == CC_EXPR_STR && is_pointer_type(tu->globals[i].type)) {
                init_is_string = 1;
                out->globals[i].init_str = xstrdup(init->lhs->ident != NULL ? init->lhs->ident : "\"\"");
                if (out->globals[i].init_str == NULL) {
                    set_diag(diag, "out of memory duplicating global string initializer");
                    cc_ssa_module_free(out);
                    return -1;
                }
            } else if (eval_global_init_expr(tu, init, &init_i, &init_f, &init_is_float, &init_sym) != 0) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "unsupported global initializer for %s",
                             tu->globals[i].name);
                }
                free(init_sym);
                cc_ssa_module_free(out);
                return -1;
            }
            out->globals[i].has_init = (init != NULL) && !force_bss_zero;
            if (out->globals[i].has_init && !out->globals[i].init_is_string && !out->globals[i].init_is_symbol) {
                if ((out->globals[i].type == CC_TYPE_FLOAT || out->globals[i].type == CC_TYPE_DOUBLE) &&
                    !init_is_float) {
                    init_f = (double)init_i;
                    init_is_float = 1;
                } else if (is_integral_type(out->globals[i].type) && init_is_float) {
                    init_i = (long)init_f;
                    init_is_float = 0;
                }
            }
            out->globals[i].init_i = init_i;
            out->globals[i].init_f = init_f;
            out->globals[i].init_is_float = init_is_float;
            out->globals[i].init_is_string = init_is_string;
            out->globals[i].init_is_symbol = init_sym != NULL;
            out->globals[i].init_sym = init_sym;
        }
    }

    for (i = 0; i < tu->func_count; ++i) {
        const cc_function_t *fdecl = &tu->funcs[i];
        cc_ssa_global_t gdecl;
        if (fdecl->has_body || (fdecl->attr_flags & CC_ATTR_ALIAS) == 0 || fdecl->attr_alias == NULL ||
            fdecl->attr_alias[0] == '\0') {
            continue;
        }
        memset(&gdecl, 0, sizeof(gdecl));
        gdecl.name = xstrdup(fdecl->name);
        gdecl.type = CC_TYPE_PTR_VOID;
        gdecl.type_struct_id = -1;
        gdecl.array_len = -1;
        gdecl.storage = fdecl->storage | CC_STORAGE_EXTERN;
        gdecl.attr_flags = fdecl->attr_flags;
        gdecl.attr_align = fdecl->attr_align;
        if (fdecl->attr_section != NULL) {
            gdecl.attr_section = xstrdup(fdecl->attr_section);
        }
        gdecl.attr_alias = xstrdup(fdecl->attr_alias);
        if (gdecl.name == NULL || gdecl.attr_alias == NULL ||
            (fdecl->attr_section != NULL && gdecl.attr_section == NULL)) {
            free(gdecl.name);
            free(gdecl.attr_section);
            free(gdecl.attr_alias);
            set_diag(diag, "out of memory lowering function alias declaration");
            cc_ssa_module_free(out);
            return -1;
        }
        if (append_synth_global(out, &gdecl) != 0) {
            free(gdecl.name);
            free(gdecl.attr_section);
            free(gdecl.attr_alias);
            set_diag(diag, "out of memory appending function alias declaration");
            cc_ssa_module_free(out);
            return -1;
        }
    }

    for (i = 0; i < tu->func_count; ++i) {
        if (tu->funcs[i].has_body && !should_skip_fn_body_for_codegen(tu, &tu->funcs[i])) {
            def_count++;
        }
    }
    if (def_count == 0) {
        out->funcs = NULL;
        out->func_count = 0;
        return 0;
    }

    out->funcs = (cc_ssa_function_t *)calloc(def_count, sizeof(*out->funcs));
    if (out->funcs == NULL) {
        set_diag(diag, "out of memory allocating SSA functions");
        return -1;
    }
    out->func_count = def_count;

    for (i = 0; i < tu->func_count; ++i) {
        const cc_function_t *af = &tu->funcs[i];
        cc_ssa_function_t *sf;
        var_entry_t *vars = NULL;
        lower_ctx_t lctx;
        size_t var_count = 0;
        size_t j;
        size_t k;
        int saw_ret = 0;
        int eff_attr_flags;
        long eff_attr_align;
        const char *eff_attr_section;
        const char *eff_attr_alias = NULL;

        if (!af->has_body || should_skip_fn_body_for_codegen(tu, af)) {
            continue;
        }
        sf = &out->funcs[out_i++];

        memset(&lctx, 0, sizeof(lctx));
        lctx.fn = af;
        lctx.mod = out;

        eff_attr_flags = af->attr_flags;
        eff_attr_align = af->attr_align;
        eff_attr_section = af->attr_section;
        eff_attr_alias = af->attr_alias;
        for (k = 0; k < tu->func_count; ++k) {
            const cc_function_t *cand = &tu->funcs[k];
            if (strcmp(cand->name, af->name) != 0) {
                continue;
            }
            eff_attr_flags |= cand->attr_flags;
            if (cand->attr_align > eff_attr_align) {
                eff_attr_align = cand->attr_align;
            }
            if ((eff_attr_section == NULL || eff_attr_section[0] == '\0') && cand->attr_section != NULL &&
                cand->attr_section[0] != '\0') {
                eff_attr_section = cand->attr_section;
            }
            if ((eff_attr_alias == NULL || eff_attr_alias[0] == '\0') && cand->attr_alias != NULL &&
                cand->attr_alias[0] != '\0') {
                eff_attr_alias = cand->attr_alias;
            }
        }
        if ((eff_attr_section == NULL || eff_attr_section[0] == '\0') && (eff_attr_flags & CC_ATTR_HOT) != 0) {
            eff_attr_section = ".text.hot";
        } else if ((eff_attr_section == NULL || eff_attr_section[0] == '\0') &&
                   (eff_attr_flags & CC_ATTR_COLD) != 0) {
            eff_attr_section = ".text.unlikely";
        }

        sf->name = xstrdup(af->name);
        if (sf->name == NULL) {
            set_diag(diag, "out of memory duplicating function name");
            cc_ssa_module_free(out);
            return -1;
        }
        sf->ret_type = (af->ret_type == CC_TYPE_FLOAT || af->ret_type == CC_TYPE_DOUBLE ||
                        af->ret_type == CC_TYPE_LDOUBLE || af->ret_type == CC_TYPE_DECIMAL32 ||
                        af->ret_type == CC_TYPE_DECIMAL64 || af->ret_type == CC_TYPE_DECIMAL128)
                           ? CC_VAL_F64
                           : CC_VAL_I64;
        sf->storage = af->storage;
        sf->attr_flags = eff_attr_flags;
        sf->attr_align = eff_attr_align;
        if (eff_attr_section != NULL) {
            sf->attr_section = xstrdup(eff_attr_section);
            if (sf->attr_section == NULL) {
                set_diag(diag, "out of memory duplicating function section attribute");
                cc_ssa_module_free(out);
                return -1;
            }
        }
        if (eff_attr_alias != NULL) {
            sf->attr_alias = xstrdup(eff_attr_alias);
            if (sf->attr_alias == NULL) {
                set_diag(diag, "out of memory duplicating function alias attribute");
                cc_ssa_module_free(out);
                return -1;
            }
        }
        sf->is_variadic = af->is_variadic;
        sf->param_count = af->param_count;
        sf->param_values = (int *)calloc(af->param_count == 0 ? 1 : af->param_count, sizeof(*sf->param_values));
        sf->param_types =
            (cc_value_type_t *)calloc(af->param_count == 0 ? 1 : af->param_count, sizeof(*sf->param_types));
        if (sf->param_values == NULL || sf->param_types == NULL) {
            set_diag(diag, "out of memory allocating parameter map");
            cc_ssa_module_free(out);
            return -1;
        }

        for (j = 0; j < af->param_count; ++j) {
            cc_ssa_instr_t in;
            int v;

            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_PARAM;
            in.dst = new_value(sf, type_to_val(af->params[j].type));
            in.lhs = -1;
            in.rhs = -1;
            in.param_index = (int)j;
            if (in.dst < 0) {
                set_diag(diag, "out of memory assigning param SSA value");
                cc_ssa_module_free(out);
                return -1;
            }
            sf->param_values[j] = in.dst;
            sf->param_types[j] = type_to_val(af->params[j].type);
            if (push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory appending SSA parameter instruction");
                cc_ssa_module_free(out);
                return -1;
            }

            v = in.dst;
            if (var_define(&vars, &var_count, af->params[j].name, af->params[j].type, af->params[j].type_struct_id,
                           -1, 0, NULL, v, 0, 0, NULL) != 0) {
                set_diag(diag, "out of memory defining parameter variable");
                cc_ssa_module_free(out);
                return -1;
            }
        }

        for (j = 0; j < af->stmt_count; ++j) {
            if (lower_collect_labels(sf, &af->stmts[j], &lctx, diag) != 0) {
                cc_ssa_module_free(out);
                return -1;
            }
        }
        for (j = 0; j < af->stmt_count; ++j) {
            if (lower_collect_hoisted_allocs(tu, sf, &lctx, &af->stmts[j], diag) != 0) {
                cc_ssa_module_free(out);
                return -1;
            }
        }

        for (j = 0; j < af->stmt_count; ++j) {
            if (lower_stmt(tu, sf, &vars, &var_count, &lctx, 0, -1, -1, &af->stmts[j], &saw_ret, diag) != 0) {
                cc_ssa_module_free(out);
                return -1;
            }
        }

        if (sf->instr_count == 0 || sf->instrs[sf->instr_count - 1].op != CC_SSA_RET) {
            if ((sf->attr_flags & CC_ATTR_NORETURN) != 0) {
                if (append_noreturn_loop(sf) != 0) {
                    set_diag(diag, "out of memory appending noreturn terminator");
                    cc_ssa_module_free(out);
                    return -1;
                }
            } else {
                if (append_default_return(sf) != 0) {
                    set_diag(diag, "out of memory appending default return");
                    cc_ssa_module_free(out);
                    return -1;
                }
            }
        }

        for (j = 0; j < var_count; ++j) {
            free(vars[j].name);
            free(vars[j].static_sym);
        }
        free(vars);
        for (j = 0; j < lctx.label_count; ++j) {
            free(lctx.labels[j].name);
        }
        free(lctx.labels);
        free(lctx.hoisted_allocs);
    }

    return 0;
}

void cc_ssa_set_pointer_size(int bytes) {
    if (bytes == 4 || bytes == 8) {
        g_pointer_size_bytes = bytes;
    }
}
