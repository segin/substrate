#include "cc_frontend.h"
#include "cc_asm.h"
#include "cc_consteval.h"
#include "cc_ctype.h"
#include "cc_diag.h"
#include "cc_ssa.h"
#include "cc_types.h"
#include "cc_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <math.h>


typedef struct {
    char *name;
    cc_type_t type;
    int struct_id;
    long array_len;
    int array_ndim;
    long array_dims[CC_MAX_ARRAY_DIMS];
    int value;
    int addr_value;
    long addr_size;
    int is_static_storage;
    int is_parameter;
    char *static_sym;
    int depth;
    int vla_bytes; /**< a variable-length array's size in bytes, as the value computed for its allocation; else -1 */
    /** a variable-length array's strides: the size in bytes of one element of each dimension, outermost
     *  first, as values computed at its declaration; -1 past its rank, and every entry -1 if not a VLA.
     *  For a pointer to one, the first is the size of the array it points to, the rest the array's. */
    int vla_strides[CC_MAX_ARRAY_DIMS];
    int vla_rank;       /**< the number of strides; 0 if neither a VLA nor a pointer to one */
    int vla_is_pointer; /**< a pointer to a VLA, whose own size is a pointer's */
    const cc_ctype_t *ctype; /**< the variable's declared type; NULL where the declaration recorded none */
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
    size_t label_cap;
    const cc_function_t *fn;
    cc_ssa_module_t *mod;
    hoisted_alloc_entry_t *hoisted_allocs;
    size_t hoisted_alloc_count;
    size_t hoisted_alloc_cap;
} lower_ctx_t;

enum {
    CC_ATOMIC_RELAXED = 0,
    CC_ATOMIC_CONSUME = 1,
    CC_ATOMIC_ACQUIRE = 2,
    CC_ATOMIC_RELEASE = 3,
    CC_ATOMIC_ACQ_REL = 4,
    CC_ATOMIC_SEQ_CST = 5
};

static int emit_trap_instr(cc_ssa_function_t *sf);
static int emit_global_addr(cc_ssa_function_t *sf, const char *name, cc_diag_t *diag);
static int lower_stmt(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, var_entry_t **vars, size_t *var_count,
                      const lower_ctx_t *ctx, int depth, int break_label, int continue_label, const cc_stmt_t *s,
                      int *saw_ret, cc_diag_t *diag);
static int emit_local_storage_alloc(cc_ssa_function_t *sf, long total_size, cc_diag_t *diag);
static int lower_nested_env(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, var_entry_t *vars,
                            size_t var_count, const cc_function_t *callee, cc_diag_t *diag);
static int lower_nested_trampoline(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                   var_entry_t *vars, size_t var_count, const cc_function_t *callee,
                                   cc_diag_t *diag);
static int eval_global_init_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long long *out_i, double *out_f,
                                 int *out_is_float, char **out_sym);
static int eval_global_init_ld_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long double *out);
static int eval_global_addr_symbol_addend(const cc_translation_unit_t *tu, const cc_expr_t *e, char **out_sym,
                                          long *out_addend);
static const cc_expr_t *selected_generic_expr(const cc_expr_t *e);
static int var_find_visible(var_entry_t *vars, size_t var_count, const char *name, int depth);
static const cc_global_t *find_global(const cc_translation_unit_t *tu, const char *name);
static int member_base_struct_id(const cc_expr_t *e);
static const cc_struct_member_t *find_struct_member(const cc_translation_unit_t *tu, int sid, const char *name);
static const cc_expr_t *unwrap_self_designated_init_list(const cc_expr_t *init_list, const char *member_name);
static int expr_is_array_object_ref(const cc_translation_unit_t *tu, var_entry_t *vars, size_t var_count, int depth,
                                    const cc_expr_t *e);
static long array_type_size_bytes(const cc_translation_unit_t *tu, cc_type_t t, int struct_id, int array_ndim,
                                  const long dims[CC_MAX_ARRAY_DIMS]);
static long sizeof_expr_bytes(const cc_translation_unit_t *tu, var_entry_t *vars, size_t var_count, int depth,
                              const cc_expr_t *e);
static long sizeof_type_name_bytes(const cc_translation_unit_t *tu, const cc_expr_t *e);
static int static_local_array_init_data(const cc_translation_unit_t *tu, const cc_stmt_t *s, cc_ssa_global_t *g);

static void free_asm_instr_fields(cc_ssa_instr_t *in) {
    size_t i;
    if (in == NULL) {
        return;
    }
    free(in->sym);
    free(in->asm_out_values);
    free(in->asm_out_sizes);
    free(in->asm_in_values);
    free(in->asm_in_sizes);
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
    if (s[0] == 'u' || s[0] == 'U') {
        /* a universal character name, \uXXXX or \UXXXXXXXX: the code point
         * (narrow literals reach here already in UTF-8) */
        size_t digits = s[0] == 'u' ? 4 : 8;
        i = 1;
        while (i < n && i <= digits) {
            unsigned char c = s[i];
            unsigned d;
            if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') d = 10u + (unsigned)(c - 'a');
            else if (c >= 'A' && c <= 'F') d = 10u + (unsigned)(c - 'A');
            else break;
            v = (v << 4) | d;
            i++;
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
    size_t cap = 0;

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
        if (count >= cap) {
            size_t ncap = cap == 0 ? 16 : cap * 2;
            next = (unsigned long *)realloc(vals, ncap * sizeof(*next));
            if (next == NULL) {
                free(vals);
                return -1;
            }
            vals = next;
            cap = ncap;
        }
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

/**
 * Record a lowering error, which has no source position; the first one
 * recorded is kept.
 *
 * @param d    the diagnostic
 * @param msg  the message
 */
static void set_diag(cc_diag_t *d, const char *msg) {
    cc_diag_set(d, "", 0, 0, msg);
}

static cc_value_type_t type_to_val(cc_type_t t) {
    return (t == CC_TYPE_FLOAT16 || t == CC_TYPE_FLOAT || t == CC_TYPE_DOUBLE || t == CC_TYPE_LDOUBLE || t == CC_TYPE_COMPLEX ||
            t == CC_TYPE_IMAGINARY || t == CC_TYPE_DECIMAL32 || t == CC_TYPE_DECIMAL64 || t == CC_TYPE_DECIMAL128)
               ? CC_VAL_F64
               : CC_VAL_I64;
}

static int is_pointer_type(cc_type_t t);

typedef cc_const_t const_integral_value_t;

static int is_pointer_type(cc_type_t t) {
    return cc_type_is_pointer(t);
}

/**
 * The type of an expression's value: its interned type with an array or
 * a function decayed to a pointer, qualifiers dropped.  A node sema did
 * not check -- one lowering made itself -- has none recorded; its legacy
 * fields stand in.
 *
 * @param e  the expression
 * @return   the type, or NULL if not known
 */
static const cc_ctype_t *lower_value_ctype(const cc_expr_t *e) {
    const cc_ctype_t *t = e->ctype;

    if (t == NULL) {
        t = cc_ct_from_legacy(e->value_type, e->struct_id, e->array_ndim, e->array_dims);
    }
    return cc_ct_decay(t);
}

/**
 * Whether an expression's value is a pointer: a pointer, or an array or
 * function used as a value.
 *
 * @param e  the expression
 * @return   1 or 0
 */
static int lower_is_pointer(const cc_expr_t *e) {
    const cc_ctype_t *t = lower_value_ctype(e);

    return t != NULL && t->kind == CC_CT_POINTER;
}

/**
 * Whether an expression's value is of an integer type, _Bool, an
 * enumeration and the bit-precise types included.
 *
 * @param e  the expression
 * @return   1 or 0
 */
static int lower_is_integer(const cc_expr_t *e) {
    return cc_ct_is_integer(lower_value_ctype(e));
}

/**
 * Whether an expression's value is of an unsigned integer type other
 * than _Bool (whose one value bit never needs extending).
 *
 * @param e  the expression
 * @return   1 or 0
 */
static int lower_is_unsigned(const cc_expr_t *e) {
    const cc_ctype_t *t = lower_value_ctype(e);

    return cc_ct_is_unsigned(t) && t->kind != CC_CT_BOOL;
}

/**
 * Whether loading an expression's value from memory zero-extends it: an
 * unsigned integer, _Bool or a pointer.
 *
 * @param e  the expression
 * @return   1 or 0
 */
static int lower_loads_unsigned(const cc_expr_t *e);

/**
 * Whether loading a value of type @p t from memory zero-extends it: an
 * unsigned integer, _Bool or a pointer.
 *
 * @param t  the type, decayed; may be NULL
 * @return   1 or 0
 */
static int ct_loads_unsigned(const cc_ctype_t *t) {
    return cc_ct_is_unsigned(t) || (t != NULL && t->kind == CC_CT_POINTER);
}

static int lower_loads_unsigned(const cc_expr_t *e) {
    return ct_loads_unsigned(lower_value_ctype(e));
}

/**
 * The type of a variable as a value: its declared type decayed, or, where
 * the declaration recorded none, the type its legacy fields describe.
 *
 * @param v  the variable
 * @return   the type, or NULL if not known
 */
static const cc_ctype_t *lower_var_ctype(const var_entry_t *v) {
    const cc_ctype_t *t = v->ctype;

    if (t == NULL) {
        t = cc_ct_from_legacy(v->type, v->struct_id, v->array_ndim, v->array_dims);
    }
    return cc_ct_decay(t);
}

/**
 * The type of a function parameter as a value (see lower_var_ctype()).
 *
 * @param p  the parameter
 * @return   the type, or NULL if not known
 */
static const cc_ctype_t *lower_param_ctype(const cc_param_t *p) {
    const cc_ctype_t *t = p->ctype;

    if (t == NULL) {
        t = cc_ct_from_legacy(p->type, p->type_struct_id, p->array_ndim, p->array_dims);
    }
    return cc_ct_decay(t);
}

/**
 * The type a declaration statement declares, as a value (see
 * lower_var_ctype()).
 *
 * @param s  the declaration
 * @return   the type, or NULL if not known
 */
static const cc_ctype_t *lower_decl_ctype(const cc_stmt_t *s) {
    const cc_ctype_t *t = s->ctype;

    if (t == NULL) {
        t = cc_ct_from_legacy(s->type, s->type_struct_id, s->array_ndim, s->array_dims);
    }
    return cc_ct_decay(t);
}

/**
 * The type of a structure member as a value (see lower_var_ctype()).
 *
 * @param m  the member
 * @return   the type, or NULL if not known
 */
static const cc_ctype_t *lower_member_ctype(const cc_struct_member_t *m) {
    const cc_ctype_t *t = m->ctype;

    if (t == NULL) {
        t = cc_ct_from_legacy(m->type, m->type_struct_id, m->array_ndim, m->array_dims);
    }
    return cc_ct_decay(t);
}

/**
 * The type of a file-scope variable as a value (see lower_var_ctype()).
 *
 * @param g  the variable
 * @return   the type, or NULL if not known
 */
static const cc_ctype_t *lower_global_ctype(const cc_global_t *g) {
    const cc_ctype_t *t = g->ctype;

    if (t == NULL) {
        t = cc_ct_from_legacy(g->type, g->type_struct_id, g->array_ndim, g->array_dims);
    }
    return cc_ct_decay(t);
}

/**
 * The kind of IR value a value of type @p t is held in: F64 for a real
 * binary floating type or an imaginary one (held as its one part), I64
 * for everything else -- integers, pointers, and the structures that
 * stand for complex, decimal, wide and vector values.
 *
 * @param t  the type, decayed; may be NULL
 * @return   CC_VAL_F64 or CC_VAL_I64
 */
static cc_value_type_t ct_value_kind(const cc_ctype_t *t) {
    if (t == NULL) {
        return CC_VAL_I64;
    }
    switch (t->kind) {
    case CC_CT_FLOAT16:
    case CC_CT_FLOAT:
    case CC_CT_DOUBLE:
    case CC_CT_LDOUBLE:
    case CC_CT_IMAGINARY:
        return CC_VAL_F64;
    default:
        return CC_VAL_I64;
    }
}

/**
 * Whether a value of type @p t lives in memory and is moved by copying
 * its bytes: a structure, union or array, and the types lowering keeps
 * in a hidden structure -- complex, decimal floating, and integers wider
 * than 64 bits.
 *
 * @param t  the type, or NULL
 * @return   1 or 0
 */
static int ct_in_memory(const cc_ctype_t *t) {
    if (t == NULL) {
        return 0;
    }
    switch (t->kind) {
    case CC_CT_RECORD:
    case CC_CT_ARRAY:
    case CC_CT_COMPLEX:
    case CC_CT_DECIMAL32:
    case CC_CT_DECIMAL64:
    case CC_CT_DECIMAL128:
        return 1;
    default:
        return cc_ct_is_integer(t) && cc_ct_int_width(t) > 64;
    }
}

/**
 * The kind of IR value an expression's value is held in (ct_value_kind()).
 *
 * @param e  the expression
 * @return   CC_VAL_F64 or CC_VAL_I64
 */
static cc_value_type_t lower_value_kind(const cc_expr_t *e) {
    return ct_value_kind(lower_value_ctype(e));
}

/**
 * The structure behind a type: the record it is, or the hidden structure
 * that stands for a complex type (as for the wide, vector and decimal
 * types, which are records already).
 *
 * @param t  the type, or NULL
 * @return   the structure's index, or -1 if the type is not one
 */
static int ct_record_id(const cc_ctype_t *t) {
    if (t != NULL && t->kind == CC_CT_RECORD) {
        return t->id;
    }
    if (t != NULL && t->kind == CC_CT_COMPLEX) {
        return cc_ct_complex_record(t);
    }
    return -1;
}

/**
 * The structure behind an expression's value (ct_record_id()).
 *
 * @param e  the expression
 * @return   the structure's index, or -1 if the value is not one
 */
static int lower_record_id(const cc_expr_t *e) {
    return ct_record_id(lower_value_ctype(e));
}

/**
 * The type a cast (or a compound literal, which is one) converts to, as
 * written: not decayed, so `(int[]){...}` is an array.
 *
 * @param e  the CC_EXPR_CAST
 * @return   the type
 */
static const cc_ctype_t *lower_cast_ctype(const cc_expr_t *e) {
    return e->ctype != NULL ? e->ctype : cc_ct_from_legacy(e->aux_type, e->aux_struct_id, e->array_ndim, e->array_dims);
}

/**
 * The structure a cast or compound literal converts to (ct_record_id()).
 *
 * @param e  an expression, or NULL
 * @return   the structure's index, or -1 if @p e is not a cast to one
 */
static int lower_cast_record_id(const cc_expr_t *e) {
    return (e != NULL && e->kind == CC_EXPR_CAST) ? ct_record_id(lower_cast_ctype(e)) : -1;
}

/**
 * Whether a cast or compound literal converts to an array of (or a
 * pointer to) plain or unsigned char, as `(char[]){"..."}` does.
 *
 * @param e  the CC_EXPR_CAST
 * @return   1 or 0
 */
static int lower_cast_is_char_array(const cc_expr_t *e) {
    const cc_ctype_t *t = lower_cast_ctype(e);

    return t != NULL && (t->kind == CC_CT_ARRAY || t->kind == CC_CT_POINTER) && cc_ct_is_character(t->of);
}

/**
 * Whether an expression's value is of structure @p sid; with @p sid
 * negative, whether it is void.
 *
 * @param e    the expression
 * @param sid  the structure's index, or -1
 * @return     1 or 0
 */
static int lower_is_record(const cc_expr_t *e, int sid) {
    const cc_ctype_t *t;

    if (sid >= 0) {
        return lower_record_id(e) == sid;
    }
    t = lower_value_ctype(e);
    return t != NULL && t->kind == CC_CT_VOID;
}

/**
 * Whether an expression's value is of type kind @p k.
 *
 * @param e  the expression
 * @param k  the kind
 * @return   1 or 0
 */
static int lower_has_kind(const cc_expr_t *e, cc_ctype_kind_t k) {
    const cc_ctype_t *t = lower_value_ctype(e);

    return t != NULL && t->kind == k;
}

static long type_size_bytes_with_struct(const cc_translation_unit_t *tu, cc_type_t t, int struct_id);

/**
 * The size in bytes of an expression's value (an array or function used
 * as a value is a pointer).
 *
 * @param tu  the translation unit, for structure sizes
 * @param e   the expression
 * @return    the size, or what the legacy encoding gives where the type
 *            has none (void, an incomplete structure)
 */
static long lower_value_size(const cc_translation_unit_t *tu, const cc_expr_t *e);

/**
 * The size of what an expression's pointer value points to: the step of
 * pointer arithmetic on it, an array pointee's whole size included.
 *
 * @param tu  the translation unit, for structure sizes
 * @param e   an expression whose value is a pointer
 * @return    the size, or -1 where the type does not give one (void, a
 *            function, an incomplete type, a variable-length array)
 */
static long lower_pointee_size(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    const cc_ctype_t *t = lower_value_ctype(e);
    long n;

    if (t == NULL || t->kind != CC_CT_POINTER || t->of == NULL || t->of->kind == CC_CT_FUNCTION) {
        return -1;
    }
    n = cc_ct_size(t->of, tu);
    return n > 0 ? n : -1;
}

/**
 * The step of pointer arithmetic on @p e: the size of what it points to,
 * or 1 for a `void *` or a pointer to a function, as GNU C counts them.
 *
 * @param tu  the translation unit
 * @param e   the pointer operand
 * @return    the step, or -1 if @p e is not a pointer to a complete type
 */
static long lower_pointer_step(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    const cc_ctype_t *t = lower_value_ctype(e);

    if (t == NULL || t->kind != CC_CT_POINTER || t->of == NULL) {
        return -1;
    }
    if (t->of->kind == CC_CT_VOID || t->of->kind == CC_CT_FUNCTION) {
        return 1;
    }
    return lower_pointee_size(tu, e);
}

/**
 * The type of the object an atomic builtin's pointer operand points to,
 * unqualified; `int` for a `void *` or an operand that is not a pointer,
 * as the builtins take them.
 *
 * @param e  the pointer operand
 * @return   the type
 */
static const cc_ctype_t *lower_atomic_elem_ctype(const cc_expr_t *e) {
    const cc_ctype_t *t = lower_value_ctype(e);

    if (t == NULL || t->kind != CC_CT_POINTER || t->of == NULL || t->of->kind == CC_CT_VOID) {
        return cc_ct_basic(CC_CT_INT);
    }
    return t->of->unqual;
}

static long lower_value_size(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    long n = cc_ct_size(lower_value_ctype(e), tu);

    /* the legacy fields speak only for a node lowering built, which has no type recorded */
    return (n > 0 || e->ctype != NULL) ? n : type_size_bytes_with_struct(tu, e->value_type, e->struct_id);
}

static cc_type_t ptr_base_type(cc_type_t t);

static int pointer_depth(cc_type_t t) {
    return (int)cc_type_pointer_depth(t);
}

/**
 * Whether the operand of a `*` is a cast, or a cast plus or minus an
 * integer (a subscript of one): `*(T)p`, `((T)p)[i]`.
 *
 * @param x  the operand
 * @return   1 if so, else 0
 */
static int deref_operand_is_cast(const cc_expr_t *x) {
    if (x != NULL && x->kind == CC_EXPR_BIN && (x->op == CC_BIN_ADD || x->op == CC_BIN_SUB) && x->lhs != NULL &&
        x->rhs != NULL) {
        x = lower_is_pointer(x->lhs) ? x->lhs : x->rhs;
    }
    return x != NULL && x->kind == CC_EXPR_CAST;
}

/**
 * Whether an initializer list for a multi-dimensional array initializes
 * rows as wholes -- a braced sub-list, `{{1, 2}, {3, 4}}`, or a string
 * for a row of characters, `{"ab", "cd"}` -- rather than listing every
 * element flat, `{1, 2, 3, 4}`.
 *
 * @param list  the initializer list
 * @return      1 if any element is a braced list or a string, else 0
 */
static int init_list_has_row_initializer(const cc_expr_t *list) {
    size_t i;

    if (list == NULL || list->kind != CC_EXPR_INIT_LIST) {
        return 0;
    }
    for (i = 0; i < list->arg_count; ++i) {
        if (list->args[i] != NULL &&
            (list->args[i]->kind == CC_EXPR_INIT_LIST || list->args[i]->kind == CC_EXPR_STR)) {
            return 1;
        }
    }
    return 0;
}

static int is_array_object_decl(cc_type_t t, long array_len, int array_ndim) {
    if (!is_pointer_type(t) || array_len < 0 || array_ndim <= 0) {
        return 0;
    }
    return pointer_depth(t) >= array_ndim;
}

static cc_type_t ptr_base_type(cc_type_t t);

static cc_type_t ptr_base_type(cc_type_t t) {
    return cc_type_deref_once(t);
}

/**
 * Size of a type known from its kind alone.
 *
 * @param t  the type
 * @return   the size in bytes of a scalar type; the pointer size for
 *           `_BitInt`, `_Atomic` and function types, whose real size
 *           this kind does not carry; -1 for anything else
 */
static long type_size_bytes(cc_type_t t) {
    long n = cc_type_scalar_size(t);

    if (n >= 0) {
        return n;
    }
    if (t == CC_TYPE_BITINT || t == CC_TYPE_ATOMIC || t == CC_TYPE_FUNC) {
        return cc_target_pointer_size();
    }
    return -1;
}

static long type_size_bytes_with_struct(const cc_translation_unit_t *tu, cc_type_t t, int struct_id) {
    if (t == CC_TYPE_ATOMIC) {
        if (struct_id <= 0) {
            return -1;
        }
        return type_size_bytes_with_struct(tu, (cc_type_t)struct_id, -1);
    }
    if (t == CC_TYPE_BITINT) {
        return cc_bitint_size(struct_id);
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

/**
 * A floating constant converted to integer type @p t, as an initializer's
 * bits: through unsigned long long for an unsigned type, since converting
 * a value of 2^63 or more to long long is undefined (and gives 2^63).
 */
static long long fp_const_to_int(double v, cc_type_t t) {
    if (t == CC_TYPE_BOOL) {
        return v != 0.0;
    }
    if (cc_type_is_unsigned_integral(t) && v >= 0.0) {
        return (long long)(unsigned long long)v;
    }
    return (long long)v;
}

/** An eightbyte's x86-64 psABI class (System V psABI 3.2.3), in merge order. */
enum { ABI64_NONE = 0, ABI64_SSE, ABI64_INTEGER, ABI64_X87, ABI64_MEMORY };

/**
 * Merge class @p c into an eightbyte's class: INTEGER beats SSE, and x87
 * with anything else, or MEMORY anywhere, makes it MEMORY.
 */
static void abi64_merge(int *cls, int c) {
    if (*cls == c || c == ABI64_NONE) {
        return;
    }
    if (*cls == ABI64_NONE) {
        *cls = c;
    } else if (*cls == ABI64_MEMORY || c == ABI64_MEMORY || *cls == ABI64_X87 || c == ABI64_X87) {
        *cls = ABI64_MEMORY;
    } else {
        *cls = ABI64_INTEGER;
    }
}

/** Give the eightbytes @p size bytes at @p off cover class @p c. */
static void abi64_cover(int cls[2], long off, long size, int c) {
    long first = off / 8;
    long last = (off + (size > 0 ? size : 1) - 1) / 8;
    long i;

    for (i = first; i <= last; ++i) {
        if (i > 1) {
            abi64_merge(&cls[1], ABI64_MEMORY);
            return;
        }
        abi64_merge(&cls[i], c);
    }
}

static int abi64_classify_record(const cc_translation_unit_t *tu, int sid, long off, int cls[2], int depth);

/** The class of a scalar of legacy type @p t. */
static int abi64_scalar_class(cc_type_t t) {
    if (t == CC_TYPE_FLOAT || t == CC_TYPE_DOUBLE || t == CC_TYPE_FLOAT16 || t == CC_TYPE_DECIMAL32 ||
        t == CC_TYPE_DECIMAL64 || t == CC_TYPE_DECIMAL128) {
        return ABI64_SSE;
    }
    return t == CC_TYPE_LDOUBLE ? ABI64_X87 : ABI64_INTEGER;
}

/**
 * Classify an object of interned type @p ct at offset @p off into the
 * eightbytes @p cls.
 *
 * @return 0, or -1 if the type cannot be classified (then the caller keeps
 *         the INTEGER default)
 */
static int abi64_classify_ctype(const cc_translation_unit_t *tu, const cc_ctype_t *ct, long off, int cls[2],
                                int depth) {
    long size = cc_ct_size(ct, tu);
    long align = cc_ct_align(ct, tu);

    if (depth > 32 || size < 0) {
        return -1;
    }
    if (align > 0 && off % align != 0) {
        /* an unaligned member (a packed structure) puts the whole in memory */
        abi64_cover(cls, off, size, ABI64_MEMORY);
        return 0;
    }
    switch (ct->kind) {
    case CC_CT_FLOAT16:
    case CC_CT_FLOAT:
    case CC_CT_DOUBLE:
    case CC_CT_DECIMAL32:
    case CC_CT_DECIMAL64:
    case CC_CT_DECIMAL128:
        abi64_cover(cls, off, size, ABI64_SSE);
        return 0;
    case CC_CT_LDOUBLE:
        abi64_cover(cls, off, size, ABI64_X87);
        return 0;
    case CC_CT_COMPLEX:
    case CC_CT_IMAGINARY: {
        long es = cc_ct_size(ct->of, tu);

        if (es <= 0 || abi64_classify_ctype(tu, ct->of, off, cls, depth + 1) != 0) {
            return -1;
        }
        return ct->kind == CC_CT_COMPLEX ? abi64_classify_ctype(tu, ct->of, off + es, cls, depth + 1) : 0;
    }
    case CC_CT_RECORD:
        return abi64_classify_record(tu, ct->id, off, cls, depth + 1);
    case CC_CT_ARRAY: {
        long es = cc_ct_size(ct->of, tu);
        long i;

        if (ct->count < 0 || es <= 0) {
            return ct->count <= 0 ? 0 : -1;
        }
        for (i = 0; i < ct->count && off + i * es < 16; ++i) {
            if (abi64_classify_ctype(tu, ct->of, off + i * es, cls, depth + 1) != 0) {
                return -1;
            }
        }
        return 0;
    }
    case CC_CT_FUNCTION:
    case CC_CT_VOID:
        return -1;
    default:
        abi64_cover(cls, off, size, ABI64_INTEGER);
        return 0;
    }
}

/** Classify structure or union @p sid at offset @p off into @p cls; -1 if it cannot be. */
static int abi64_classify_record(const cc_translation_unit_t *tu, int sid, long off, int cls[2], int depth) {
    const cc_struct_def_t *sd;
    size_t i;

    if (tu == NULL || sid < 0 || (size_t)sid >= tu->struct_count || depth > 32) {
        return -1;
    }
    sd = &tu->structs[sid];
    if (sd->complex_elem != CC_TYPE_VOID) {
        long es = type_size_bytes(sd->complex_elem);

        abi64_cover(cls, off, es, abi64_scalar_class(sd->complex_elem));
        abi64_cover(cls, off + es, es, abi64_scalar_class(sd->complex_elem));
        return 0;
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        long moff = off + m->offset;

        if (m->is_bitfield) {
            if (m->bit_width > 0) {
                abi64_cover(cls, moff, m->bit_storage_size > 0 ? m->bit_storage_size : 1, ABI64_INTEGER);
            }
            continue;
        }
        if (m->ctype != NULL) {
            if (abi64_classify_ctype(tu, m->ctype, moff, cls, depth) != 0) {
                return -1;
            }
        } else if (m->array_ndim == 0 && m->type == CC_TYPE_VOID && m->type_struct_id >= 0) {
            if (abi64_classify_record(tu, m->type_struct_id, moff, cls, depth + 1) != 0) {
                return -1;
            }
        } else if (m->array_ndim == 0 && m->size > 0) {
            abi64_cover(cls, moff, m->size, abi64_scalar_class(m->type));
        } else {
            return -1;
        }
    }
    return 0;
}

/**
 * How an x86-64 aggregate of @p size bytes travels: for one of at most 16
 * bytes that goes in registers, CC_ABI_SSE0 / CC_ABI_SSE1 for its
 * eightbytes that are SSE (none set: all INTEGER, the old behaviour); for
 * a returned one that goes in x87 registers CC_ABI_RET_X87 or
 * CC_ABI_RET_X87_COMPLEX; -1 for one passed in memory, as anything over
 * 16 bytes, containing a long double (as an argument), or packed is.
 *
 * @param tu         the translation unit
 * @param sid        the structure, union or complex type's structure
 * @param size       its size
 * @param is_return  nonzero for a return value
 */
static int abi64_aggregate_class(const cc_translation_unit_t *tu, int sid, long size, int is_return) {
    int cls[2] = {ABI64_NONE, ABI64_NONE};
    int flags = 0;

    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count &&
        tu->structs[sid].complex_elem == CC_TYPE_LDOUBLE) {
        return is_return ? CC_ABI_RET_X87_COMPLEX : -1;
    }
    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].decimal_bits > 0) {
        /* a decimal floating value is SSE (psABI 3.2.3): _Decimal128 SSE and SSEUP, whole in one xmm register */
        if (tu->structs[sid].decimal_bits == 128) {
            return CC_ABI_SSE0 | CC_ABI_SSE1 | (is_return ? CC_ABI_RET_VEC128 : CC_ABI_VEC128);
        }
        return CC_ABI_SSE0;
    }
    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].vector_count > 0) {
        /*
         * A vector, by its size as gcc classes it: 16 bytes SSE and SSEUP,
         * whole in one xmm register; 8 bytes SSE; 4 or fewer INTEGER;
         * wider (an AVX vector without AVX) in memory.
         */
        if (size == 16) {
            return CC_ABI_SSE0 | CC_ABI_SSE1 | (is_return ? CC_ABI_RET_VEC128 : CC_ABI_VEC128);
        }
        return size == 8 ? CC_ABI_SSE0 : size < 8 ? 0 : -1;
    }
    if (size > 16 || size <= 0) {
        return -1;
    }
    if (abi64_classify_record(tu, sid, 0, cls, 0) != 0) {
        return 0;
    }
    if (cls[0] == ABI64_MEMORY || cls[1] == ABI64_MEMORY) {
        return -1;
    }
    if (cls[0] == ABI64_X87 || cls[1] == ABI64_X87) {
        /* one long double (X87 then X87UP) is returned in %st0; as an argument it is memory */
        return (is_return && cls[0] == ABI64_X87 && size == 16) ? CC_ABI_RET_X87 : -1;
    }
    if (cls[0] == ABI64_SSE) {
        flags |= CC_ABI_SSE0;
    }
    if (cls[1] == ABI64_SSE && size > 8) {
        flags |= CC_ABI_SSE1;
    }
    return flags;
}

/** Whether structure @p sid stands for a vector type. */
static int sid_is_vector(const cc_translation_unit_t *tu, int sid) {
    return tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].vector_count > 0;
}

/**
 * How an i386 vector argument or parameter of structure @p sid and
 * @p size bytes travels, as gcc passes one: the first three 16-byte
 * vectors in %xmm0-2 and the first three 8-byte ones in %mm0-2
 * (CC_CALL_ARG_ABI_AGG_REG with CC_ABI_VEC128 or CC_ABI_VEC64); a wider
 * one, or one the registers ran out for, on the stack aligned to its size
 * (CC_ABI_STACK_VEC).  *@p n16 and *@p n8 count the registers taken.
 *
 * @return the ABI byte, or -1 if it travels as any aggregate of its size
 */
static int i386_vector_abi(const cc_translation_unit_t *tu, int sid, long size, int *n16, int *n8) {
    if (!sid_is_vector(tu, sid) || size <= 4) {
        return -1;
    }
    if (size == 16 && *n16 < 3) {
        ++*n16;
        return CC_CALL_ARG_ABI_AGG_REG | CC_ABI_VEC128;
    }
    if (size == 8 && *n8 < 3) {
        ++*n8;
        return CC_CALL_ARG_ABI_AGG_REG | CC_ABI_VEC64;
    }
    return CC_CALL_ARG_ABI_AGG_MEM | (size >= 16 ? CC_ABI_STACK_VEC : 0);
}

/**
 * CC_ABI_STACK_VEC for a vector of 16 bytes or more, or a _Decimal128,
 * passed in memory (aligned to its size, as gcc places them), else 0.
 */
static int vector_mem_flag(const cc_translation_unit_t *tu, int sid, long size) {
    int dec128 = tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].decimal_bits == 128;

    return (sid_is_vector(tu, sid) || dec128) && size >= 16 ? CC_ABI_STACK_VEC : 0;
}

/**
 * The agg_class of a CALL or RET of an aggregate: how the target returns
 * one of structure @p sid and @p size bytes, where that is not the
 * default (x86-64: in %rax/%rdx if 16 bytes or fewer, else by hidden
 * pointer; i386: by hidden pointer).
 */
static int agg_return_class(const cc_translation_unit_t *tu, int sid, long size) {
    if (cc_target_pointer_size() == 8) {
        int c = abi64_aggregate_class(tu, sid, size, 1);

        return c >= 0 ? c : (size <= 16 ? CC_ABI_RET_MEMORY : 0);
    }
    /* i386 psABI: a float _Complex comes back in %eax:%edx, as gcc has it; any other aggregate in memory */
    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].complex_elem == CC_TYPE_FLOAT) {
        return CC_ABI_RET_EAX_EDX;
    }
    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].decimal_bits > 0) {
        /* _Decimal32 in %eax, _Decimal64 in %eax:%edx, _Decimal128 in memory, as gcc returns them */
        return tu->structs[sid].decimal_bits <= 64 ? CC_ABI_RET_EAX_EDX : 0;
    }
    if (sid_is_vector(tu, sid)) {
        /* a vector of 16 bytes in %xmm0, of 8 in %mm0, of 4 or fewer in %eax, as gcc returns one */
        long vsize = tu->structs[sid].size;

        return vsize == 16 ? CC_ABI_RET_VEC128 : vsize == 8 ? CC_ABI_RET_VEC64 : vsize <= 4 ? CC_ABI_RET_EAX_EDX : 0;
    }
    return 0;
}

/**
 * The size in bytes of a sizeof expression, for the constant evaluator:
 * of a string literal, an array, or any other expression or type.
 *
 * @param ctx  the translation unit; may be NULL
 * @param e    the CC_EXPR_SIZEOF node
 * @return     the size, or -1 if it is not known
 */
static long lower_const_sizeof(void *ctx, const cc_expr_t *e) {
    const cc_translation_unit_t *tu = (const cc_translation_unit_t *)ctx;

    if (e->lhs == NULL) {
        return sizeof_type_name_bytes(tu, e);
    }
    return sizeof_expr_bytes(tu, NULL, 0, 0, e->lhs);
}


/**
 * Evaluate an integer constant expression in lowering's context.
 *
 * @param tu   the translation unit; may be NULL
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression
 */
static int eval_const_integral_expr_value(const cc_translation_unit_t *tu, const cc_expr_t *e,
                                          const_integral_value_t *out) {
    cc_const_env_t env;

    env.sizeof_bytes = lower_const_sizeof;
    env.ctx = (void *)tu;
    return cc_const_eval(&env, e, out);
}

static long type_size_bytes_struct(const cc_translation_unit_t *tu, cc_type_t t, int struct_id) {
    if (t == CC_TYPE_ATOMIC) {
        if (struct_id <= 0) {
            return -1;
        }
        return type_size_bytes_struct(tu, (cc_type_t)struct_id, -1);
    }
    if (t == CC_TYPE_BITINT) {
        return cc_bitint_size(struct_id);
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
        if (cur_type != CC_TYPE_VOID && !is_pointer_type(cur_type)) {
            cur_sid = -1;
        }
    }
    return type_size_bytes_with_struct(tu, cur_type, cur_sid);
}

static long array_dims_step_size_bytes(const cc_translation_unit_t *tu, cc_type_t array_type, int struct_id,
                                       int array_ndim, const long array_dims[CC_MAX_ARRAY_DIMS], int dim_start,
                                       long fallback) {
    long stride = 1;
    long scalar_size;
    int i;

    if (array_ndim <= 0) {
        return fallback;
    }
    if (dim_start < 0) {
        dim_start = 0;
    }
    if (dim_start > array_ndim) {
        return fallback;
    }
    scalar_size = array_decl_scalar_size_bytes(tu, array_type, struct_id, array_ndim);
    if (scalar_size <= 0) {
        return fallback;
    }
    for (i = dim_start; i < array_ndim; ++i) {
        long dim = array_dims != NULL ? array_dims[i] : 0;
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

/**
 * The step of pointer arithmetic on an operand whose real type is known
 * (see cc_ct_of_expr()): the size of what it points to, an array or a
 * function decaying first.
 *
 * @param tu        the translation unit, for structure sizes
 * @param ptr_expr  the pointer operand
 * @return          the step in bytes, or -1 if the type is not known or
 *                  what it points to has no size
 */
static long expr_real_step_size_bytes(const cc_translation_unit_t *tu, const cc_expr_t *ptr_expr) {
    const cc_ctype_t *t = ptr_expr != NULL ? lower_value_ctype(ptr_expr) : NULL;
    long n;

    if (t == NULL || t->kind != CC_CT_POINTER) {
        return -1;
    }
    n = cc_ct_size(t->of, tu);
    return n > 0 ? n : -1;
}

/**
 * The step of pointer arithmetic on @p ptr_expr: the size of what its
 * interned type points to, an array pointee's whole size included.
 *
 * The legacy walks over array dimensions this replaced are gone: every
 * expression sema checked carries its type, and one that points to an
 * array says so.  A pointee with no size (a variable-length array, void)
 * gives @p fallback; a VLA's runtime stride is found elsewhere
 * (vla_subscript_level()).
 *
 * @param tu        the translation unit
 * @param vars      unused
 * @param var_count unused
 * @param depth     unused
 * @param ptr_expr  the pointer expression
 * @param fallback  what to return when the type gives no step
 * @return          the step in bytes, or @p fallback
 */
static long expr_array_step_size_bytes(const cc_translation_unit_t *tu, var_entry_t *vars, size_t var_count, int depth,
                                       const cc_expr_t *ptr_expr, long fallback) {
    long real_step = expr_real_step_size_bytes(tu, ptr_expr);

    (void)vars;
    (void)var_count;
    (void)depth;
    return real_step > 0 ? real_step : fallback;
}

/**
 * The step of pointer arithmetic on a variable named by @p ptr_expr; the
 * same as expr_array_step_size_bytes() now that the variable's type is
 * the expression's.
 */
static long expr_decl_array_step_size_bytes(const cc_translation_unit_t *tu, var_entry_t *vars, size_t var_count,
                                            int depth, const cc_expr_t *ptr_expr, long fallback) {
    return expr_array_step_size_bytes(tu, vars, var_count, depth, ptr_expr, fallback);
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
static int emit_const_i64_instr(cc_ssa_function_t *sf, long long v);
static int emit_const_f64_instr(cc_ssa_function_t *sf, double v);
static int emit_const_ldouble_special_instr(cc_ssa_function_t *sf, long code);
static void set_value_size(cc_ssa_function_t *f, int v, long size);
static long value_size(const cc_ssa_function_t *f, int v);
static int emit_cmp_instr(cc_ssa_function_t *sf, cc_cmp_kind_t cmp_kind, int lhs, int rhs, int is_unsigned,
                          cc_diag_t *diag);
static int emit_memcpy_instr(cc_ssa_function_t *sf, int dst_ptr, int src_ptr, long size, cc_diag_t *diag);
static int emit_memset_instr(cc_ssa_function_t *sf, int dst_ptr, int fill_byte, long size, cc_diag_t *diag);
static int eval_const_int_expr_simple(const cc_expr_t *e, long long *out);
static int emit_atomic_compiler_barrier(cc_ssa_function_t *sf, cc_diag_t *diag);
static int emit_atomic_full_barrier(cc_ssa_function_t *sf, cc_diag_t *diag);
static int normalize_integral_value(cc_ssa_function_t *sf, int v, cc_type_t t, cc_diag_t *diag);
static int normalize_float_value(cc_ssa_function_t *sf, int v, cc_type_t t, cc_diag_t *diag);
static int normalize_float_ct(cc_ssa_function_t *sf, int v, const cc_ctype_t *ct, cc_diag_t *diag);
static int normalize_int_ct(cc_ssa_function_t *sf, int v, const cc_ctype_t *ct, cc_diag_t *diag);
static int lower_truthy_value(cc_ssa_function_t *sf, int v, cc_diag_t *diag);
static cc_type_t fp_builtin_storage_type(const cc_expr_t *e);
static int lower_fp_builtin_arg(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *arg,
                                cc_type_t *out_storage_type, cc_diag_t *diag);
static int emit_fp_negate_from_value(cc_ssa_function_t *sf, int v, cc_type_t store_t, cc_diag_t *diag);
static int emit_fp_signbit_from_value(cc_ssa_function_t *sf, int v, cc_type_t store_t, cc_diag_t *diag);
static int emit_fp_isinf_sign_from_value(cc_ssa_function_t *sf, int v, cc_type_t store_t, cc_diag_t *diag);
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
    if (ctx->hoisted_alloc_count >= ctx->hoisted_alloc_cap) {
        size_t ncap = ctx->hoisted_alloc_cap == 0 ? 16 : ctx->hoisted_alloc_cap * 2;
        next = (hoisted_alloc_entry_t *)realloc(ctx->hoisted_allocs, ncap * sizeof(*next));
        if (next == NULL) {
            set_diag(diag, "out of memory recording hoisted local storage");
            return -1;
        }
        ctx->hoisted_allocs = next;
        ctx->hoisted_alloc_cap = ncap;
    }
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
        if (ctx->label_count >= ctx->label_cap) {
            size_t ncap = ctx->label_cap == 0 ? 16 : ctx->label_cap * 2;
            next = (label_entry_t *)realloc(ctx->labels, ncap * sizeof(*next));
            if (next == NULL) {
                set_diag(diag, "out of memory collecting labels");
                return -1;
            }
            ctx->labels = next;
            ctx->label_cap = ncap;
        }
        ctx->labels[ctx->label_count].name = cc_strdup(s->label_name);
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
    if (in.dst >= 0 && in.dst < f->value_count && f->value_types[in.dst] == CC_VAL_I64 && f->value_is_unsigned != NULL) {
        switch (in.op) {
        case CC_SSA_CONST:
        case CC_SSA_PARAM:
        case CC_SSA_LOAD:
        case CC_SSA_ADD:
        case CC_SSA_SUB:
        case CC_SSA_MUL:
        case CC_SSA_DIV:
        case CC_SSA_AND:
        case CC_SSA_OR:
        case CC_SSA_XOR:
        case CC_SSA_SHL:
        case CC_SSA_SHR:
        case CC_SSA_CALL:
        case CC_SSA_CALLI:
        case CC_SSA_F2I:
            f->value_is_unsigned[in.dst] = in.is_unsigned ? 1 : 0;
            break;
        case CC_SSA_MOV:
            if (in.lhs >= 0 && in.lhs < f->value_count) {
                f->value_is_unsigned[in.dst] = f->value_is_unsigned[in.lhs];
            }
            break;
        case CC_SSA_CMP:
        case CC_SSA_GADDR:
        case CC_SSA_LADDR:
        case CC_SSA_ADDR:
        case CC_SSA_STR:
        case CC_SSA_STACKALLOC:
        case CC_SSA_DYNALLOC:
        case CC_SSA_VA_START:
            f->value_is_unsigned[in.dst] = 1;
            break;
        default:
            f->value_is_unsigned[in.dst] = 0;
            break;
        }
    }
    if (in.dst >= 0 && in.dst < f->value_count && f->value_sizes != NULL) {
        switch (in.op) {
        case CC_SSA_PARAM:
        case CC_SSA_LOAD:
        case CC_SSA_CALL:
        case CC_SSA_CALLI:
            set_value_size(f, in.dst, in.imm > 0 ? in.imm : cc_target_pointer_size());
            break;
        case CC_SSA_MOV:
            if (in.lhs >= 0 && in.lhs < f->value_count) {
                set_value_size(f, in.dst, f->value_sizes[in.lhs]);
            }
            break;
        case CC_SSA_CMP:
            set_value_size(f, in.dst, 4);
            break;
        case CC_SSA_GADDR:
        case CC_SSA_LADDR:
        case CC_SSA_ADDR:
        case CC_SSA_STR:
        case CC_SSA_STACKALLOC:
        case CC_SSA_DYNALLOC:
        case CC_SSA_VA_START:
            set_value_size(f, in.dst, cc_target_pointer_size());
            break;
        case CC_SSA_CONST:
        case CC_SSA_ADD:
        case CC_SSA_SUB:
        case CC_SSA_MUL:
        case CC_SSA_DIV:
        case CC_SSA_AND:
        case CC_SSA_OR:
        case CC_SSA_XOR:
        case CC_SSA_SHL:
        case CC_SSA_SHR:
        case CC_SSA_F2I:
            break;
        default:
            if (f->value_types[in.dst] == CC_VAL_F64) {
                set_value_size(f, in.dst, 8);
            }
            break;
        }
    }
    return 0;
}

static int eval_const_int_expr_simple(const cc_expr_t *e, long long *out) {
    const_integral_value_t value;

    if (e == NULL || out == NULL) {
        return -1;
    }
    if (eval_const_integral_expr_value(NULL, e, &value) != 0) {
        return -1;
    }
    *out = (long long)value.bits;
    return 0;
}

static int emit_atomic_compiler_barrier(cc_ssa_function_t *sf, cc_diag_t *diag) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_ASM;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.asm_volatile = 1;
    in.sym = cc_strdup("");
    in.asm_clobber_count = 1;
    in.asm_clobbers = (char **)calloc(1, sizeof(*in.asm_clobbers));
    if (in.sym == NULL || in.asm_clobbers == NULL) {
        free_asm_instr_fields(&in);
        set_diag(diag, "out of memory emitting atomic compiler barrier");
        return -1;
    }
    in.asm_clobbers[0] = cc_strdup("memory");
    if (in.asm_clobbers[0] == NULL) {
        free_asm_instr_fields(&in);
        set_diag(diag, "out of memory emitting atomic compiler barrier clobber");
        return -1;
    }
    if (push_instr(sf, in) != 0) {
        free_asm_instr_fields(&in);
        set_diag(diag, "out of memory appending atomic compiler barrier");
        return -1;
    }
    return 0;
}

static int emit_atomic_full_barrier(cc_ssa_function_t *sf, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    const char *tmpl = cc_target_pointer_size() == 8 ? "lock; orl $0, (%%rsp)" : "lock; orl $0, (%%esp)";

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_ASM;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.asm_volatile = 1;
    in.sym = cc_strdup(tmpl);
    in.asm_clobber_count = 1;
    in.asm_clobbers = (char **)calloc(1, sizeof(*in.asm_clobbers));
    if (in.sym == NULL || in.asm_clobbers == NULL) {
        free_asm_instr_fields(&in);
        set_diag(diag, "out of memory emitting atomic full barrier");
        return -1;
    }
    in.asm_clobbers[0] = cc_strdup("memory");
    if (in.asm_clobbers[0] == NULL) {
        free_asm_instr_fields(&in);
        set_diag(diag, "out of memory emitting atomic full barrier clobber");
        return -1;
    }
    if (push_instr(sf, in) != 0) {
        free_asm_instr_fields(&in);
        set_diag(diag, "out of memory appending atomic full barrier");
        return -1;
    }
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
        unsigned char *next_unsigned;
        uint32_t *next_sizes;
        if (next == NULL) {
            return -1;
        }
        next_unsigned = (unsigned char *)realloc(f->value_is_unsigned, ncap * sizeof(*next_unsigned));
        if (next_unsigned == NULL) {
            return -1;
        }
        next_sizes = (uint32_t *)realloc(f->value_sizes, ncap * sizeof(*next_sizes));
        if (next_sizes == NULL) {
            return -1;
        }
        f->value_types = next;
        f->value_is_unsigned = next_unsigned;
        f->value_sizes = next_sizes;
        f->value_cap = ncap;
    }

    f->value_types[id] = vt;
    f->value_is_unsigned[id] = 0;
    f->value_sizes[id] = (uint32_t)(vt == CC_VAL_F64 ? 8 : cc_target_pointer_size());
    return id;
}

/**
 * Record how many bytes a value holds.
 *
 * @param f     the function the value belongs to
 * @param v     the value
 * @param size  its size in bytes -- a structure value's whole size; 0 or
 *              less means "a word", the pointer size
 */
static void set_value_size(cc_ssa_function_t *f, int v, long size) {
    if (f == NULL || f->value_sizes == NULL || v < 0 || v >= f->value_count) {
        return;
    }
    if (size <= 0) {
        size = cc_target_pointer_size();
    }
    /* compared unsigned: on a 32-bit host (long)UINT32_MAX is -1, and every size would be clamped */
    if ((unsigned long)size > UINT32_MAX) {
        size = (long)(UINT32_MAX >> 1);
    }
    f->value_sizes[v] = (uint32_t)size;
}

static long value_size(const cc_ssa_function_t *f, int v) {
    if (f == NULL || f->value_sizes == NULL || v < 0 || v >= f->value_count || f->value_sizes[v] == 0) {
        return 0;
    }
    return f->value_sizes[v];
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

static int value_is_unsigned(const cc_ssa_function_t *f, int v) {
    if (f == NULL || v < 0 || (size_t)v >= (size_t)f->value_count || f->value_is_unsigned == NULL) {
        return 0;
    }
    return f->value_is_unsigned[v] ? 1 : 0;
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

/**
 * Whether an expression is a variable-length array variable, or a pointer
 * to one, with any subscripts applied -- `a`, `a[i]`, `a[i] + 1`, `*a` --
 * and if so which, and how many subscripts deep.  Subscripts reach
 * lowering as `*(p + i)`, so each dereference is one level and pointer
 * arithmetic keeps the level of its pointer operand.
 *
 * At level L the expression steps, as a pointer, by stride L, and for
 * L >= 1 its size is stride L - 1.  At level 0 an array's size is the
 * array's (vla_bytes) and a pointer's a pointer's.
 *
 * @param vars       the variables in scope
 * @param var_count  their number
 * @param depth      the block depth
 * @param e          the expression
 * @param level      receives the number of subscripts
 * @return           the array's index in @p vars, or -1 if @p e is not one
 */
static int vla_subscript_level(var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, int *level) {
    int idx;

    if (e == NULL || vars == NULL) {
        return -1;
    }
    switch (e->kind) {
    case CC_EXPR_IDENT:
        if (e->ident == NULL) {
            return -1;
        }
        idx = var_find_visible(vars, var_count, e->ident, depth);
        if (idx < 0 || vars[idx].vla_rank <= 0) {
            return -1;
        }
        *level = 0;
        return idx;
    case CC_EXPR_DEREF:
        idx = vla_subscript_level(vars, var_count, depth, e->lhs, level);
        if (idx < 0 || *level + 1 > vars[idx].vla_rank) {
            return -1;
        }
        ++*level;
        return idx;
    case CC_EXPR_BIN:
        if (e->op != CC_BIN_ADD && e->op != CC_BIN_SUB) {
            return -1;
        }
        if (e->lhs != NULL && lower_is_pointer(e->lhs)) {
            return vla_subscript_level(vars, var_count, depth, e->lhs, level);
        }
        if (e->op == CC_BIN_ADD && e->rhs != NULL && lower_is_pointer(e->rhs)) {
            return vla_subscript_level(vars, var_count, depth, e->rhs, level);
        }
        return -1;
    default:
        return -1;
    }
}

static int var_define(var_entry_t **vars, size_t *var_count, const char *name, cc_type_t type, int struct_id,
                      long array_len, int array_ndim, const long array_dims[CC_MAX_ARRAY_DIMS], int value, int depth,
                      int is_static_storage, int is_parameter, const char *static_sym, const cc_ctype_t *ctype) {
    var_entry_t *next;
    char *dup;
    char *sym_dup = NULL;

    next = (var_entry_t *)realloc(*vars, (*var_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *vars = next;

    dup = cc_strdup(name);
    if (dup == NULL) {
        return -1;
    }
    if (is_static_storage && static_sym != NULL) {
        sym_dup = cc_strdup(static_sym);
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
    (*vars)[*var_count].addr_value = -1;
    (*vars)[*var_count].addr_size = 0;
    (*vars)[*var_count].is_static_storage = is_static_storage ? 1 : 0;
    (*vars)[*var_count].is_parameter = is_parameter ? 1 : 0;
    (*vars)[*var_count].static_sym = sym_dup;
    (*vars)[*var_count].depth = depth;
    (*vars)[*var_count].vla_bytes = -1;
    for (int k = 0; k < CC_MAX_ARRAY_DIMS; ++k) {
        (*vars)[*var_count].vla_strides[k] = -1;
    }
    (*vars)[*var_count].vla_rank = 0;
    (*vars)[*var_count].vla_is_pointer = 0;
    (*vars)[*var_count].ctype = ctype;
    (*var_count)++;
    return 0;
}

static int is_complete_struct_object_type(const cc_translation_unit_t *tu, cc_type_t t, int struct_id) {
    if (tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count) {
        return 0;
    }
    if (!tu->structs[struct_id].complete) {
        return 0;
    }
    if (is_pointer_type(t)) {
        return 0;
    }
    if (t == CC_TYPE_ATOMIC || t == CC_TYPE_BITINT) {
        return 0;
    }
    return 1;
}

static int var_is_aggregate_or_array(const cc_translation_unit_t *tu, const var_entry_t *v) {
    if (v == NULL) {
        return 0;
    }
    if (is_complete_struct_object_type(tu, v->type, v->struct_id)) {
        return 1;
    }
    if ((v->array_ndim > 0 && v->array_len >= 0) ||
        is_array_object_decl(v->type, v->array_len, v->array_ndim)) {
        return 1;
    }
    return 0;
}

static long var_object_size_bytes(const cc_translation_unit_t *tu, const var_entry_t *v) {
    long sz;
    if (v == NULL) {
        return -1;
    }
    if (is_complete_struct_object_type(tu, v->type, v->struct_id)) {
        sz = tu->structs[v->struct_id].size;
        if (sz <= 0) {
            sz = 1;
        }
        return sz;
    }
    sz = type_size_bytes_with_struct(tu, v->type, v->struct_id);
    if (sz <= 0) {
        sz = cc_target_pointer_size();
    }
    return sz;
}

/**
 * The size of a load or store of a scalar local.  It is the object's
 * size except for a complex, imaginary or `_Decimal128` object: cc
 * carries one of those as a double, so it accesses that double and not
 * the whole 16-byte object (which a 16-byte access would treat as a long
 * double).
 *
 * @param tu  the translation unit
 * @param v   the local
 * @return    the access size in bytes, or <= 0 if unknown
 */
static long var_scalar_access_size(const cc_translation_unit_t *tu, const var_entry_t *v) {
    if (v != NULL && (cc_type_is_complex(v->type) || v->type == CC_TYPE_DECIMAL128)) {
        return type_size_bytes_with_struct(tu, CC_TYPE_DOUBLE, -1);
    }
    return var_object_size_bytes(tu, v);
}

/**
 * Whether an initializer is certainly not a constant expression: it
 * calls a function (a `__builtin_` aside), assigns, increments or
 * decrements, or is a statement expression.  The operand of sizeof is not
 * evaluated and is not looked at.  An initializer this says no to may
 * still not fold; this finds only what no constant expression contains.
 *
 * @param e  the initializer
 * @return   1 if it is certainly not constant, else 0
 */
static int expr_is_certainly_nonconstant(const cc_expr_t *e) {
    size_t i;

    if (e == NULL) {
        return 0;
    }
    switch (e->kind) {
    case CC_EXPR_SIZEOF:
        return 0;
    case CC_EXPR_ASSIGN:
    case CC_EXPR_UPDATE:
    case CC_EXPR_STMT:
        return 1;
    case CC_EXPR_CALL:
        if (e->ident == NULL || strncmp(e->ident, "__builtin_", 10) != 0) {
            return 1;
        }
        break;
    default:
        break;
    }
    if (expr_is_certainly_nonconstant(e->lhs) || expr_is_certainly_nonconstant(e->rhs) ||
        expr_is_certainly_nonconstant(e->third)) {
        return 1;
    }
    for (i = 0; i < e->arg_count; ++i) {
        if (expr_is_certainly_nonconstant(e->args[i])) {
            return 1;
        }
    }
    return 0;
}

static int ensure_local_var_addressable(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, var_entry_t *v, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int rhs;

    if (v == NULL) {
        set_diag(diag, "internal error: null local variable");
        return -1;
    }
    if (v->addr_value >= 0) {
        return v->addr_value;
    }
    if (var_is_aggregate_or_array(tu, v)) {
        v->addr_value = v->value;
        v->addr_size = var_object_size_bytes(tu, v);
        return v->addr_value;
    }

    v->addr_size = var_scalar_access_size(tu, v);
    if (v->addr_size <= 0) {
        set_diag(diag, "unsupported address-taken local type");
        return -1;
    }
    v->addr_value = emit_local_storage_alloc(sf, var_object_size_bytes(tu, v), diag);
    if (v->addr_value < 0) {
        return -1;
    }

    rhs = v->value;
    rhs = normalize_float_ct(sf, rhs, lower_var_ctype(v), diag);
    if (rhs < 0) {
        return -1;
    }
    rhs = normalize_int_ct(sf, rhs, lower_var_ctype(v), diag);
    if (rhs < 0) {
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_STORE;
    in.dst = -1;
    in.lhs = v->addr_value;
    in.rhs = rhs;
    in.imm = v->addr_size;
    if (push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory materializing address-taken local variable");
        return -1;
    }
    return v->addr_value;
}

static int emit_local_var_scalar_load(cc_ssa_function_t *sf, const var_entry_t *v, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    cc_value_type_t vt;
    long size;

    if (v == NULL) {
        set_diag(diag, "internal error: null local variable in scalar load");
        return -1;
    }
    if (v->addr_value < 0) {
        return v->value;
    }

    vt = ct_value_kind(lower_var_ctype(v));
    size = v->addr_size > 0 ? v->addr_size : cc_target_pointer_size();

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LOAD;
    in.dst = new_value(sf, vt);
    in.lhs = v->addr_value;
    in.rhs = -1;
    in.imm = size;
    in.is_unsigned = ct_loads_unsigned(lower_var_ctype(v)) ? 1 : 0;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory loading local scalar variable");
        return -1;
    }
    return in.dst;
}

static int emit_local_var_scalar_store(cc_ssa_function_t *sf, const var_entry_t *v, int rhs, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    long size;

    if (v == NULL) {
        set_diag(diag, "internal error: null local variable in scalar store");
        return -1;
    }
    if (v->addr_value < 0) {
        if (emit_mov_instr(sf, v->value, rhs) != 0) {
            set_diag(diag, "out of memory moving local scalar variable");
            return -1;
        }
        return 0;
    }

    size = v->addr_size > 0 ? v->addr_size : cc_target_pointer_size();
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_STORE;
    in.dst = -1;
    in.lhs = v->addr_value;
    in.rhs = rhs;
    in.imm = size;
    in.is_unsigned = ct_loads_unsigned(lower_var_ctype(v)) ? 1 : 0;
    if (push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory storing local scalar variable");
        return -1;
    }
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
    size_t best_idx = 0;
    const cc_global_t *best = NULL;
    int best_score = -1;
    if (tu == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < tu->global_count; ++i) {
        if (strcmp(tu->globals[i].name, name) == 0) {
            const cc_global_t *g = &tu->globals[i];
            int score = 0;
            if (g->init != NULL) {
                score += 8;
            }
            if (g->array_len > 0) {
                score += 4;
            }
            if (g->array_ndim > 0) {
                score += 2;
            }
            if ((g->storage & CC_STORAGE_EXTERN) == 0) {
                score += 1;
            }
            if (best == NULL || score > best_score || (score == best_score && i > best_idx)) {
                best = g;
                best_idx = i;
                best_score = score;
            }
        }
    }
    return best;
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
        const char *mname = tu->structs[sid].members[i].name;
        if (mname != NULL && strcmp(mname, name) == 0) {
            return &tu->structs[sid].members[i];
        }
    }
    return NULL;
}

static int is_synthetic_struct_array_member(const cc_translation_unit_t *tu, const cc_expr_t *e);

/**
 * A wide string literal (`L"..."`, `u"..."`, `U"..."`) as the narrow
 * literal of its bytes, for the emitter, which writes a literal with
 * `.asciz`: each unit little-endian in the element's size, as octal
 * escapes, then the terminating unit's bytes but the last (which `.asciz`
 * adds).
 *
 * @param tu  the translation unit
 * @param e   the literal
 * @return    the quoted text (caller frees), or NULL when @p e is not a
 *            wide literal or on error
 */
static char *wide_string_as_bytes(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    unsigned long *units = NULL;
    size_t count = 0;
    long elem_size;
    size_t nbytes;
    size_t i;
    char *out;
    char *p;

    if (e == NULL || e->kind != CC_EXPR_STR) {
        return NULL;
    }
    elem_size = lower_pointee_size(tu, e);
    if (elem_size <= 1 || elem_size > 8) {
        return NULL;
    }
    if (decode_string_units(e, 1, &units, &count) != 0) {
        free(units);
        return NULL;
    }
    nbytes = (count + 1) * (size_t)elem_size - 1;
    out = (char *)malloc(nbytes * 4 + 3);
    if (out == NULL) {
        free(units);
        return NULL;
    }
    p = out;
    *p++ = '"';
    for (i = 0; i < nbytes; ++i) {
        size_t unit = i / (size_t)elem_size;
        unsigned v = unit < count ? (unsigned)((units[unit] >> (8 * (i % (size_t)elem_size))) & 0xffu) : 0u;
        p[0] = '\\';
        p[1] = (char)('0' + ((v >> 6) & 7u));
        p[2] = (char)('0' + ((v >> 3) & 7u));
        p[3] = (char)('0' + (v & 7u));
        p += 4;
    }
    *p++ = '"';
    *p = '\0';
    free(units);
    return out;
}

/**
 * The text the emitter writes for a string literal: a wide literal's
 * bytes (wide_string_as_bytes()), or a narrow literal as it is.
 *
 * @param tu  the translation unit
 * @param e   the literal
 * @return    the quoted text (caller frees), or NULL out of memory
 */
static char *string_literal_emit_text(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    char *wide = wide_string_as_bytes(tu, e);
    if (wide != NULL) {
        return wide;
    }
    return cc_strdup(e != NULL && e->ident != NULL ? e->ident : "\"\"");
}

static long array_type_size_bytes(const cc_translation_unit_t *tu, cc_type_t t, int struct_id, int array_ndim,
                                  const long array_dims[CC_MAX_ARRAY_DIMS]) {
    cc_type_t elem_type;
    int elem_struct_id;
    long elem_size;
    long count = 1;
    int i;

    if (array_ndim <= 0) {
        return type_size_bytes_struct(tu, t, struct_id);
    }
    elem_type = t;
    elem_struct_id = struct_id;
    if (is_pointer_type(t)) {
        for (i = 0; i < array_ndim; ++i) {
            elem_type = ptr_base_type(elem_type);
            if (elem_struct_id >= 0 && elem_type != CC_TYPE_VOID && !is_pointer_type(elem_type)) {
                elem_struct_id = -1;
            }
        }
    }
    elem_size = type_size_bytes_with_struct(tu, elem_type, elem_struct_id);
    if (elem_size <= 0) {
        return -1;
    }
    for (i = 0; i < array_ndim; ++i) {
        long d = array_dims != NULL ? array_dims[i] : 0;
        if (d <= 0 || count > LONG_MAX / d) {
            return -1;
        }
        count *= d;
    }
    if (elem_size > LONG_MAX / count) {
        return -1;
    }
    return elem_size * count;
}

/**
 * Find the object a pointer expression points into and how far into it,
 * from real types: an array (which decays to its start), `&x`, and
 * pointer + or - an integer constant, `&a[i]` included.
 *
 * @param tu          the translation unit, for structure sizes
 * @param e           the pointer expression
 * @param out_size    receives the object's size in bytes
 * @param out_offset  receives the offset into it in bytes
 * @return            1 if found, 0 if not
 */
static int pointed_object_extent(const cc_translation_unit_t *tu, const cc_expr_t *e, long *out_size,
                                 long *out_offset) {
    const cc_ctype_t *t;
    long step;

    if (e == NULL) {
        return 0;
    }
    if (e->kind == CC_EXPR_ADDR && e->lhs != NULL) {
        const cc_expr_t *x = e->lhs;
        if (x->kind == CC_EXPR_DEREF && x->lhs != NULL) {
            return pointed_object_extent(tu, x->lhs, out_size, out_offset);  /* &*p, &a[i] */
        }
        t = cc_ct_of_expr(x);
        *out_size = cc_ct_size(t, tu);
        *out_offset = 0;
        return *out_size > 0;
    }
    if (e->kind == CC_EXPR_BIN && (e->op == CC_BIN_ADD || e->op == CC_BIN_SUB) && e->lhs != NULL &&
        e->rhs != NULL && e->rhs->kind == CC_EXPR_INT) {
        t = cc_ct_decay(cc_ct_of_expr(e->lhs));
        if (t == NULL || t->kind != CC_CT_POINTER) {
            return 0;
        }
        step = cc_ct_size(t->of, tu);
        if (step <= 0 || !pointed_object_extent(tu, e->lhs, out_size, out_offset)) {
            return 0;
        }
        *out_offset += (e->op == CC_BIN_ADD ? 1 : -1) * (long)e->rhs->int_val * step;
        return 1;
    }
    t = cc_ct_of_expr(e);
    if (t != NULL && t->kind == CC_CT_ARRAY) {
        if (e->kind == CC_EXPR_DEREF && e->lhs != NULL) {
            /* a row, `a[i]`: part of the array it was taken from */
            return pointed_object_extent(tu, e->lhs, out_size, out_offset);
        }
        *out_size = cc_ct_size(t, tu);
        *out_offset = 0;
        return *out_size > 0;
    }
    return 0;
}

/**
 * The value of `sizeof (T)` the parser could not fold (T a structure not
 * yet laid out then, or void): the size of the type it recorded, and 1
 * for void, as gcc has it.
 *
 * @param tu  the translation unit
 * @param e   the CC_EXPR_SIZEOF with no operand expression
 * @return    the size, or -1 if it is not known
 */
static long sizeof_type_name_bytes(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    return cc_ct_sizeof_type(e->aux_ctype, tu);
}

/**
 * The value of `sizeof e`: the size of its operand's interned type, an
 * array's whole size included (not decayed).  A node lowering built
 * itself has no type recorded; its legacy fields stand in.  The legacy
 * walks over variables, globals, members and string literals this used to
 * make never decided a size once every checked expression had a type.
 *
 * @param tu         the translation unit
 * @param vars       unused
 * @param var_count  unused
 * @param depth      unused
 * @param e          the operand
 * @return           the size, or -1 if it is not known
 */
static long sizeof_expr_bytes(const cc_translation_unit_t *tu, var_entry_t *vars, size_t var_count, int depth,
                              const cc_expr_t *e) {
    const cc_ctype_t *t;

    (void)vars;
    (void)var_count;
    (void)depth;
    if (e == NULL) {
        return -1;
    }
    t = e->ctype != NULL ? e->ctype : cc_ct_from_legacy(e->value_type, e->struct_id, e->array_ndim, e->array_dims);
    return cc_ct_size(t, tu);
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
    if (m->array_len >= 0) {
        return 1;
    }
    if (m->array_ndim > 0) {
        /* bounds and no element count: a pointer to an array, not an array */
        return 0;
    }
    if (m->size > 0 && m->size != cc_target_pointer_size()) {
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
        const cc_struct_member_t *m =
            e->ident != NULL ? find_struct_member(tu, member_base_struct_id(e), e->ident) : NULL;
        if (is_synthetic_struct_array_member(tu, e)) {
            return 1;
        }
        if (m != NULL && m->array_len < 0 && m->array_ndim > 0) {
            /* a pointer to an array: its value is the pointer */
            return 0;
        }
        if (lower_is_pointer(e) && e->array_ndim > 0) {
            return 1;
        }
    }
    if (e->kind == CC_EXPR_DEREF && lower_is_pointer(e) && e->array_ndim > 0) {
        return 1;
    }
    if (e->kind == CC_EXPR_ADDR && e->lhs != NULL) {
        return expr_is_array_object_ref(tu, vars, var_count, depth, e->lhs);
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
    if (e->kind == CC_EXPR_DEREF && e->lhs != NULL && lower_is_pointer(e)) {
        return expr_is_array_pointer_chain(tu, vars, var_count, depth, e->lhs);
    }
    if (e->kind == CC_EXPR_BIN && (e->op == CC_BIN_ADD || e->op == CC_BIN_SUB) && lower_is_pointer(e)) {
        if (expr_is_array_pointer_chain(tu, vars, var_count, depth, e->lhs)) {
            return 1;
        }
        return expr_is_array_pointer_chain(tu, vars, var_count, depth, e->rhs);
    }
    return 0;
}

static int builtin_bswap_bits(const char *name) {
    return(cc_builtin_bswap_bits(name));
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
    BUILTIN_FFS,
    BUILTIN_POPCOUNT,
    BUILTIN_PARITY,
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
    BUILTIN_PREFETCH,
    BUILTIN_CPU_SUPPORTS,
    BUILTIN_STRLEN,
    BUILTIN_MEMCPY_CHK,
    BUILTIN_MEMMOVE_CHK,
    BUILTIN_MEMSET_CHK,
    BUILTIN_HUGE_VAL,
    BUILTIN_HUGE_VALF,
    BUILTIN_HUGE_VALL,
    BUILTIN_INF,
    BUILTIN_INFF,
    BUILTIN_INFL,
    BUILTIN_LDBL_MIN,
    BUILTIN_LDBL_DENORM_MIN,
    BUILTIN_LDBL_MAX,
    BUILTIN_ISFINITE,
    BUILTIN_ISINF,
    BUILTIN_ISINF_SIGN,
    BUILTIN_ISNAN,
    BUILTIN_SIGNBIT,
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
    BUILTIN_ATOMIC_STORE_N,
    BUILTIN_ATOMIC_THREAD_FENCE,
    BUILTIN_ATOMIC_SIGNAL_FENCE
} builtin_kind_t;

static builtin_kind_t builtin_kind(const char *name) {
    if (name == NULL) {
        return BUILTIN_NONE;
    }
    if (strcmp(name, "__builtin_va_start") == 0 || strcmp(name, "__builtin_c23_va_start") == 0) {
        return BUILTIN_VA_START;
    }
    if (strcmp(name, "__builtin_va_end") == 0 || strcmp(name, "__builtin_c23_va_end") == 0) {
        return BUILTIN_VA_END;
    }
    if (strcmp(name, "__builtin_va_copy") == 0 || strcmp(name, "__builtin_c23_va_copy") == 0) {
        return BUILTIN_VA_COPY;
    }
    if (strcmp(name, "__builtin_va_arg") == 0 || strcmp(name, "__builtin_c23_va_arg") == 0) {
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
    if (strcmp(name, "__builtin_ffs") == 0 || strcmp(name, "__builtin_ffsl") == 0 ||
        strcmp(name, "__builtin_ffsll") == 0) {
        return BUILTIN_FFS;
    }
    if (strcmp(name, "__builtin_popcount") == 0 || strcmp(name, "__builtin_popcountl") == 0 ||
        strcmp(name, "__builtin_popcountll") == 0) {
        return BUILTIN_POPCOUNT;
    }
    if (strcmp(name, "__builtin_parity") == 0 || strcmp(name, "__builtin_parityl") == 0 ||
        strcmp(name, "__builtin_parityll") == 0) {
        return BUILTIN_PARITY;
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
    if (strcmp(name, "__builtin_prefetch") == 0) {
        return BUILTIN_PREFETCH;
    }
    if (strcmp(name, "__builtin_cpu_supports") == 0) {
        return BUILTIN_CPU_SUPPORTS;
    }
    if (strcmp(name, "__builtin_strlen") == 0) {
        return BUILTIN_STRLEN;
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
    if (strcmp(name, "__builtin_inf") == 0) {
        return BUILTIN_INF;
    }
    if (strcmp(name, "__builtin_inff") == 0) {
        return BUILTIN_INFF;
    }
    if (strcmp(name, "__builtin_infl") == 0) {
        return BUILTIN_INFL;
    }
    if (strcmp(name, "__builtin_ldbl_min") == 0) {
        return BUILTIN_LDBL_MIN;
    }
    if (strcmp(name, "__builtin_ldbl_denorm_min") == 0) {
        return BUILTIN_LDBL_DENORM_MIN;
    }
    if (strcmp(name, "__builtin_ldbl_max") == 0) {
        return BUILTIN_LDBL_MAX;
    }
    if (strcmp(name, "__builtin_isfinite") == 0) {
        return BUILTIN_ISFINITE;
    }
    if (strcmp(name, "__builtin_isinf") == 0 || strcmp(name, "__builtin_isinff") == 0 ||
        strcmp(name, "__builtin_isinfl") == 0) {
        return BUILTIN_ISINF;
    }
    if (strcmp(name, "__builtin_isinf_sign") == 0) {
        return BUILTIN_ISINF_SIGN;
    }
    if (strcmp(name, "__builtin_isnan") == 0 || strcmp(name, "__builtin_isnanf") == 0 ||
        strcmp(name, "__builtin_isnanl") == 0) {
        return BUILTIN_ISNAN;
    }
    if (strcmp(name, "__builtin_signbit") == 0 || strcmp(name, "__builtin_signbitf") == 0 ||
        strcmp(name, "__builtin_signbitl") == 0) {
        return BUILTIN_SIGNBIT;
    }
    if (strcmp(name, "__builtin_nanf") == 0 || strcmp(name, "__builtin_nansf") == 0) {
        return BUILTIN_NANF;
    }
    if (strcmp(name, "__builtin_nan") == 0 || strcmp(name, "__builtin_nans") == 0) {
        return BUILTIN_NAN;
    }
    if (strcmp(name, "__builtin_nanl") == 0 || strcmp(name, "__builtin_nansl") == 0) {
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
    if (strcmp(name, "__atomic_thread_fence") == 0) {
        return BUILTIN_ATOMIC_THREAD_FENCE;
    }
    if (strcmp(name, "__atomic_signal_fence") == 0) {
        return BUILTIN_ATOMIC_SIGNAL_FENCE;
    }
    return BUILTIN_NONE;
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

/**
 * Take @p bytes_v bytes off the stack at run time (a DYNALLOC).
 *
 * A variable-length array passes @p reuse = 1: the allocation gets a save
 * slot, so each execution of its declaration first gives back what the
 * previous one took, and a VLA declared in a loop does not grow the stack.
 * alloca passes 0, and its storage lasts until the function returns.
 *
 * @param sf       the function
 * @param bytes_v  the byte count, an integer value
 * @param reuse    nonzero for a variable-length array
 * @param diag     receives an error
 * @return         the block's address, or -1 on error
 */
static int emit_dynalloc_instr(cc_ssa_function_t *sf, int bytes_v, int reuse, cc_diag_t *diag) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_DYNALLOC;
    in.lhs = bytes_v;
    in.rhs = -1;
    if (reuse) {
        in.rhs = emit_local_storage_alloc(sf, cc_target_pointer_size(), diag);
        if (in.rhs < 0) {
            return -1;
        }
    }
    in.dst = new_value(sf, CC_VAL_I64);
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory lowering a dynamic stack allocation");
        return -1;
    }
    return in.dst;
}

/**
 * Whether a declaration is of a variable-length array: one of its bounds
 * is not a constant.
 *
 * @param s  the declaration
 * @return   1 or 0
 */
static int stmt_is_vla_decl(const cc_stmt_t *s) {
    int k;

    for (k = 0; k < CC_MAX_ARRAY_DIMS; ++k) {
        if (s->array_bound_exprs[k] != NULL) {
            return 1;
        }
    }
    return 0;
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
    if (stmt_is_vla_decl(s)) {
        return 0;
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
        in.is_unsigned = value_is_unsigned(sf, v);
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

static int promote_f64_to_ldouble_value(cc_ssa_function_t *sf, int v, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    size_t i;

    if (value_size(sf, v) >= 16) {
        return v;
    }
    if (value_type(sf, v) != CC_VAL_F64) {
        v = cast_value(sf, v, CC_VAL_F64, diag);
        if (v < 0) {
            return -1;
        }
    }

    for (i = sf->instr_count; i > 0; --i) {
        cc_ssa_instr_t *def = &sf->instrs[i - 1];
        if (def->dst != v) {
            continue;
        }
        if (def->op == CC_SSA_I2F) {
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_I2F;
            in.dst = new_value(sf, CC_VAL_F64);
            in.lhs = def->lhs;
            in.rhs = -1;
            in.is_unsigned = def->is_unsigned;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory promoting integer to long double");
                return -1;
            }
            set_value_size(sf, in.dst, 16);
            return in.dst;
        }
        break;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_MOV;
    in.dst = new_value(sf, CC_VAL_F64);
    in.lhs = v;
    in.rhs = -1;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory promoting long double value");
        return -1;
    }
    set_value_size(sf, in.dst, 16);
    return in.dst;
}

/**
 * Convert a long double value to double, the inverse of
 * promote_f64_to_ldouble_value(): a long double value is 16 bytes of
 * x87 extended precision, and reading it as a double without the
 * conversion reads its low 8 bytes, part of the mantissa.
 *
 * @param sf    the function being lowered
 * @param v     the value
 * @param diag  receives a message if memory runs out
 * @return      a double value holding @p v rounded to double (@p v
 *              itself if it is not a long double), or -1 on error
 */
static int demote_ldouble_to_f64_value(cc_ssa_function_t *sf, int v, cc_diag_t *diag) {
    cc_ssa_instr_t in;

    if (value_type(sf, v) != CC_VAL_F64 || value_size(sf, v) < 16) {
        return v;
    }
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_FROUND64;
    in.dst = new_value(sf, CC_VAL_F64);
    in.lhs = v;
    in.rhs = -1;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory converting long double value");
        return -1;
    }
    set_value_size(sf, in.dst, 8);
    return in.dst;
}

static int integral_type_bits(cc_type_t t) {
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_SCHAR:
    case CC_TYPE_UCHAR:
        return 8;
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
        return 16;
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
        return 32;
    case CC_TYPE_LONG:
    case CC_TYPE_ULONG:
        return (int)(cc_target_pointer_size() * 8);
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
    int want_unsigned;

    if (!cc_type_is_integral(t)) {
        return v;
    }

    if (t == CC_TYPE_BOOL) {
        int z;
        size_t k;

        /*
         * A floating value has already been truncated to an integer;
         * _Bool is whether the floating value itself is non-zero, so
         * 0.5 converts to 1.
         */
        for (k = sf->instr_count; k > 0; --k) {
            const cc_ssa_instr_t *def = &sf->instrs[k - 1];
            if (def->dst == v) {
                if (def->op == CC_SSA_F2I && def->lhs >= 0) {
                    return lower_truthy_value(sf, def->lhs, diag);
                }
                break;
            }
        }
        z = emit_const_i64_instr(sf, 0);
        if (z < 0) {
            return -1;
        }
        if (value_size(sf, v) >= 8) {
            set_value_size(sf, z, 8);
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
    want_unsigned = cc_type_is_unsigned_integral(t) ? 1 : 0;
    if (bits <= 0) {
        return v;
    }
    if (bits >= 64) {
        long cur_size = (sf->value_sizes != NULL && v >= 0 && v < sf->value_count) ? sf->value_sizes[v] : 0;
        int src_unsigned = value_is_unsigned(sf, v);
        size_t k;

        /*
         * A floating value converts straight to 64 bits; nothing to
         * extend.  Whether the conversion is to unsigned goes on the
         * conversion itself: a later cast may relabel its value signed.
         */
        for (k = sf->instr_count; k > 0; --k) {
            if (sf->instrs[k - 1].dst == v) {
                if (sf->instrs[k - 1].op == CC_SSA_F2I) {
                    cur_size = 8;
                    if (want_unsigned) {
                        sf->instrs[k - 1].is_unsigned = 1;
                    }
                }
                break;
            }
        }
        if (cur_size <= 0) {
            cur_size = cc_target_pointer_size();
        }
        if (cur_size > 0 && cur_size < 8) {
            int src_bits = (int)(cur_size * 8);
            if (src_unsigned) {
                unsigned long long mask = (src_bits >= 64) ? ~0ULL : ((1ULL << src_bits) - 1ULL);
                c = emit_const_i64_instr(sf, (long long)mask);
                if (c < 0) {
                    return -1;
                }
                set_value_size(sf, c, 8);
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_AND;
                in.is_unsigned = 1;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = v;
                in.rhs = c;
                set_value_size(sf, in.dst, 8);
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory zero-extending integral value");
                    return -1;
                }
                v = in.dst;
            } else {
                int ext_sh = 64 - src_bits;
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_SHL;
                in.is_unsigned = 0;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = v;
                in.rhs = -1;
                in.imm = ext_sh;
                set_value_size(sf, in.dst, 8);
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory sign-extending integral value");
                    return -1;
                }
                v = in.dst;
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_SHR;
                in.is_unsigned = 0;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = v;
                in.rhs = -1;
                in.imm = ext_sh;
                set_value_size(sf, in.dst, 8);
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory sign-extending integral value");
                    return -1;
                }
                v = in.dst;
            }
        } else {
            set_value_size(sf, v, 8);
        }
        if (sf->value_is_unsigned != NULL && v >= 0 && v < sf->value_count) {
            sf->value_is_unsigned[v] = want_unsigned ? 1 : 0;
        }
        return v;
    }

    if (want_unsigned) {
        unsigned long long mask_u = (1ULL << bits) - 1ULL;
        c = emit_const_i64_instr(sf, (long long)mask_u); /* not (long): a 32-bit host makes 0xffffffff -1 */
        if (c < 0) {
            return -1;
        }
        memset(&in, 0, sizeof(in));
        in.op = CC_SSA_AND;
        in.is_unsigned = 1;
        in.dst = new_value(sf, CC_VAL_I64);
        in.lhs = v;
        in.rhs = c;
        set_value_size(sf, in.dst, (bits + 7) / 8);
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            set_diag(diag, "out of memory normalizing unsigned integer value");
            return -1;
        }
        return in.dst;
    }

    if (cc_target_pointer_size() <= 4 && bits <= 32) {
        if (bits == 32) {
            if (value_size(sf, v) > 4) {
                /*
                 * Truncate into a new value: relabelling v would also
                 * make the instruction that computes it 32-bit.
                 */
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_MOV;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = v;
                in.rhs = -1;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory truncating integer value");
                    return -1;
                }
                v = in.dst;
            }
            set_value_size(sf, v, 4);
            if (sf->value_is_unsigned != NULL && v >= 0 && v < sf->value_count) {
                sf->value_is_unsigned[v] = 0;
            }
            return v;
        }
        sh = 32 - bits;
    } else {
        sh = 64 - bits;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_SHL;
    in.is_unsigned = 0;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = v;
    in.rhs = -1;
    in.imm = sh;
    set_value_size(sf, in.dst, (cc_target_pointer_size() <= 4 && bits <= 32) ? 4 : 8);
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
    in.rhs = -1;
    in.imm = sh;
    set_value_size(sf, in.dst, (bits + 7) / 8);
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory normalizing signed integer value");
        return -1;
    }
    return in.dst;
}

/**
 * Convert an integer value to integer type @p ct: as
 * normalize_integral_value() does for legacy type @p t, and for a
 * bit-precise type, which the legacy encoding cannot say the sign of,
 * keep the low N bits, sign-extended for `_BitInt(N)` and zero-extended
 * for `unsigned _BitInt(N)`, in a value of the size it already has.
 *
 * @param sf    the function
 * @param v     the value
 * @param ct    the type converted to, or NULL if not known
 * @param t     its legacy encoding
 * @param diag  receives an error
 * @return      the converted value, or -1
 */
static int normalize_integral_ct(cc_ssa_function_t *sf, int v, const cc_ctype_t *ct, cc_type_t t, cc_diag_t *diag);

/**
 * Convert an integer value to integer type @p ct (normalize_integral_ct()),
 * the legacy kind taken from the type itself; a value of any other type
 * is left as it is.
 *
 * @param sf    the function
 * @param v     the value
 * @param ct    the type converted to; may be NULL
 * @param diag  receives an error
 * @return      the converted value, or -1
 */
static int normalize_int_ct(cc_ssa_function_t *sf, int v, const cc_ctype_t *ct, cc_diag_t *diag) {
    if (!cc_ct_is_integer(ct)) {
        return v;
    }
    return normalize_integral_ct(sf, v, ct, cc_ct_legacy_kind(ct->kind), diag);
}

static int normalize_integral_ct(cc_ssa_function_t *sf, int v, const cc_ctype_t *ct, cc_type_t t, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    long size;
    int vbits;
    int n;
    int uns;

    if (ct == NULL || (ct->kind != CC_CT_BITINT && ct->kind != CC_CT_UBITINT)) {
        return normalize_integral_value(sf, v, t, diag);
    }
    n = (int)ct->count;
    uns = ct->kind == CC_CT_UBITINT;
    /* the register the value lives in, as normalize_integral_value() shifts it: a word, or a pair on i386 */
    size = (cc_target_pointer_size() <= 4 && n <= 32) ? 4 : 8;
    vbits = (int)size * 8;
    if (n <= 0 || n >= vbits) {
        if (sf->value_is_unsigned != NULL && v >= 0 && v < sf->value_count) {
            sf->value_is_unsigned[v] = (unsigned char)uns;
        }
        return v;
    }
    memset(&in, 0, sizeof(in));
    in.rhs = -1;
    if (uns) {
        int c = emit_const_i64_instr(sf, (long long)((1ULL << n) - 1ULL));

        if (c < 0) {
            return -1;
        }
        set_value_size(sf, c, size);
        in.op = CC_SSA_AND;
        in.is_unsigned = 1;
        in.lhs = v;
        in.rhs = c;
    } else {
        in.op = CC_SSA_SHL;
        in.lhs = v;
        in.imm = vbits - n;
    }
    in.dst = new_value(sf, CC_VAL_I64);
    if (in.dst < 0) {
        return -1;
    }
    set_value_size(sf, in.dst, size);
    if (push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory wrapping a bit-precise value");
        return -1;
    }
    v = in.dst;
    if (!uns) {
        memset(&in, 0, sizeof(in));
        in.op = CC_SSA_SHR;
        in.lhs = v;
        in.rhs = -1;
        in.imm = vbits - n;
        in.dst = new_value(sf, CC_VAL_I64);
        if (in.dst < 0) {
            return -1;
        }
        set_value_size(sf, in.dst, size);
        if (push_instr(sf, in) != 0) {
            set_diag(diag, "out of memory wrapping a bit-precise value");
            return -1;
        }
        v = in.dst;
    }
    if (sf->value_is_unsigned != NULL && v >= 0 && v < sf->value_count) {
        sf->value_is_unsigned[v] = (unsigned char)uns;
    }
    return v;
}

/**
 * Convert a floating value to type @p ct: round it to single precision
 * when @p ct is `float` (it is held as a double), else leave it.
 *
 * @param sf    the function
 * @param v     the value
 * @param ct    the type converted to; may be NULL
 * @param diag  receives an error
 * @return      the converted value, or -1
 */
static int normalize_float_ct(cc_ssa_function_t *sf, int v, const cc_ctype_t *ct, cc_diag_t *diag) {
    return (ct != NULL && ct->kind == CC_CT_FLOAT) ? normalize_float_value(sf, v, CC_TYPE_FLOAT, diag) : v;
}

static int normalize_float_value(cc_ssa_function_t *sf, int v, cc_type_t t, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    if (t != CC_TYPE_FLOAT) {
        return v;
    }
    v = demote_ldouble_to_f64_value(sf, v, diag);
    if (v < 0) {
        return -1;
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

static unsigned short float16_pack_bits(double value) {
    union {
        double d;
        uint64_t u;
    } cvt;
    uint64_t sign;
    uint64_t exp;
    uint64_t frac;

    cvt.d = value;
    sign = (cvt.u >> 63) & 1u;
    exp = (cvt.u >> 52) & 0x7ffu;
    frac = cvt.u & ((1ULL << 52) - 1ULL);

    if (exp == 0x7ffu) {
        unsigned short out = (unsigned short)((sign << 15) | 0x7c00u | (unsigned short)(frac >> 42));
        if (frac != 0 && (out & 0x03ffu) == 0) {
            out |= 1u;
        }
        return out;
    }

    if (exp == 0) {
        return (unsigned short)(sign << 15);
    }

    {
        int32_t exp_unbiased = (int32_t)exp - 1023;
        int32_t half_exp = exp_unbiased + 15;
        uint64_t mant = frac;

        if (half_exp >= 0x1f) {
            return (unsigned short)((sign << 15) | 0x7c00u);
        }

        if (half_exp <= 0) {
            int shift = 1 - half_exp;
            uint64_t mantissa = mant | (1ULL << 52);
            uint64_t rounded;
            uint64_t rem_mask;
            uint64_t rem;
            uint64_t halfway;

            if (shift > 24) {
                return (unsigned short)(sign << 15);
            }
            rounded = mantissa >> (42 + shift);
            rem_mask = (1ULL << (42 + shift)) - 1ULL;
            rem = mantissa & rem_mask;
            halfway = 1ULL << (41 + shift);
            if (rem > halfway || (rem == halfway && (rounded & 1ULL) != 0)) {
                rounded++;
            }
            return (unsigned short)((sign << 15) | (rounded & 0x03ffu));
        }

        {
            uint64_t rounded = mant >> 42;
            uint64_t rem = mant & ((1ULL << 42) - 1ULL);
            uint64_t halfway = 1ULL << 41;

            if (rem > halfway || (rem == halfway && (rounded & 1ULL) != 0)) {
                rounded++;
                if (rounded == 0x400u) {
                    rounded = 0;
                    half_exp++;
                    if (half_exp >= 0x1f) {
                        return (unsigned short)((sign << 15) | 0x7c00u);
                    }
                }
            }
            return (unsigned short)((sign << 15) | ((unsigned short)half_exp << 10) | (unsigned short)rounded);
        }
    }
}

static int eval_const_i64_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long long *out) {
    const_integral_value_t value;

    if (e == NULL || out == NULL) {
        return -1;
    }
    if (eval_const_integral_expr_value(tu, e, &value) != 0) {
        return -1;
    }
    *out = (long long)value.bits;
    return 0;
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
    if (in.dst >= 0 && sf->value_sizes != NULL) {
        long lsz = (lhs >= 0 && lhs < sf->value_count) ? (long)sf->value_sizes[lhs] : (long)cc_target_pointer_size();
        long rsz = (rhs >= 0 && rhs < sf->value_count) ? (long)sf->value_sizes[rhs] : (long)cc_target_pointer_size();
        set_value_size(sf, in.dst, lsz > rsz ? lsz : rsz);
    }
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory lowering integer builtin");
        return -1;
    }
    return in.dst;
}

/*
 * One 8-byte constant.  Building it as hi << 32 | lo does not work on
 * i386, where the pieces are 32-bit values and a shift by 32 shifts by 0.
 */
static int emit_const_u64_instr(cc_ssa_function_t *sf, unsigned long long v, cc_diag_t *diag) {
    int c = emit_const_i64_instr(sf, (long long)v);

    if (c < 0) {
        set_diag(diag, "out of memory lowering 64-bit constant");
        return -1;
    }
    set_value_size(sf, c, 8);
    return c;
}

static int emit_bitfield_mask(cc_ssa_function_t *sf, int width, cc_diag_t *diag) {
    unsigned long long mask_u;
    if (width <= 0) {
        return emit_const_i64_instr(sf, 0);
    }
    if (width >= 64) {
        return emit_const_u64_instr(sf, ~0ULL, diag);
    }
    mask_u = (1ULL << width) - 1ULL;
    return emit_const_u64_instr(sf, mask_u, diag);
}

static int lower_bitfield_load_from_addr(cc_ssa_function_t *sf, const cc_expr_t *e, int ptrv, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    long storage_size;
    int v;
    int c;
    int maskv;

    if (e == NULL || !e->member_is_bitfield) {
        set_diag(diag, "malformed bit-field read in lowering");
        return -1;
    }
    storage_size = e->member_bit_storage_size;
    if (storage_size <= 0) {
        /* a bit-field's type is scalar: its size needs no translation unit */
        storage_size = cc_ct_size(lower_value_ctype(e), NULL);
    }
    if (storage_size <= 0) {
        set_diag(diag, "unsupported bit-field storage size");
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LOAD;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = ptrv;
    in.rhs = -1;
    in.imm = storage_size;
    in.is_unsigned = 1;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory loading bit-field storage");
        return -1;
    }
    v = in.dst;

    if (e->member_bit_offset > 0) {
        c = emit_const_i64_instr(sf, e->member_bit_offset);
        if (c < 0) {
            return -1;
        }
        v = emit_i64_binop_instr(sf, CC_SSA_SHR, v, c, 1, diag);
        if (v < 0) {
            return -1;
        }
    }
    maskv = emit_bitfield_mask(sf, e->member_bit_width, diag);
    if (maskv < 0) {
        return -1;
    }
    v = emit_i64_binop_instr(sf, CC_SSA_AND, v, maskv, 0, diag);
    if (v < 0) {
        return -1;
    }

    if (e->member_bit_signed && e->member_bit_width > 0 && e->member_bit_width < 64) {
        int sh = 64 - e->member_bit_width;
        c = emit_const_i64_instr(sf, sh);
        if (c < 0) {
            return -1;
        }
        v = emit_i64_binop_instr(sf, CC_SSA_SHL, v, c, 0, diag);
        if (v < 0) {
            return -1;
        }
        v = emit_i64_binop_instr(sf, CC_SSA_SHR, v, c, 0, diag);
        if (v < 0) {
            return -1;
        }
    }

    v = cast_value(sf, v, lower_value_kind(e), diag);
    if (v < 0) {
        return -1;
    }
    if (lower_is_integer(e) && !lower_is_pointer(e)) {
        v = normalize_int_ct(sf, v, lower_value_ctype(e), diag);
    }
    return v;
}

static int lower_bitfield_store_to_addr(cc_ssa_function_t *sf, const cc_expr_t *e, int ptrv, int rhs, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    long storage_size;
    int maskv;
    int rhsv;
    int oldv;
    int new_bits;
    int clear_mask;
    int old_cleared;
    int merged;
    unsigned long long mask_u;

    if (e == NULL || !e->member_is_bitfield) {
        set_diag(diag, "malformed bit-field write in lowering");
        return -1;
    }
    if (e->member_bit_width <= 0) {
        set_diag(diag, "cannot assign to zero-width bit-field");
        return -1;
    }

    storage_size = e->member_bit_storage_size;
    if (storage_size <= 0) {
        /* a bit-field's type is scalar: its size needs no translation unit */
        storage_size = cc_ct_size(lower_value_ctype(e), NULL);
    }
    if (storage_size <= 0) {
        set_diag(diag, "unsupported bit-field storage size");
        return -1;
    }

    rhsv = cast_value(sf, rhs, CC_VAL_I64, diag);
    if (rhsv < 0) {
        return -1;
    }
    if (lower_is_integer(e) && !lower_is_pointer(e)) {
        rhsv = normalize_int_ct(sf, rhsv, lower_value_ctype(e), diag);
        if (rhsv < 0) {
            return -1;
        }
    }
    maskv = emit_bitfield_mask(sf, e->member_bit_width, diag);
    if (maskv < 0) {
        return -1;
    }
    rhsv = emit_i64_binop_instr(sf, CC_SSA_AND, rhsv, maskv, 0, diag);
    if (rhsv < 0) {
        return -1;
    }

    if (e->member_bit_offset > 0) {
        int c = emit_const_i64_instr(sf, e->member_bit_offset);
        if (c < 0) {
            return -1;
        }
        rhsv = emit_i64_binop_instr(sf, CC_SSA_SHL, rhsv, c, 0, diag);
        if (rhsv < 0) {
            return -1;
        }
    }
    new_bits = rhsv;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LOAD;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = ptrv;
    in.rhs = -1;
    in.imm = storage_size;
    in.is_unsigned = 1;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory loading bit-field storage for write");
        return -1;
    }
    oldv = in.dst;

    if (e->member_bit_width >= 64) {
        mask_u = ~0ULL;
    } else {
        mask_u = ((1ULL << e->member_bit_width) - 1ULL) << e->member_bit_offset;
    }
    clear_mask = emit_const_u64_instr(sf, ~mask_u, diag);
    if (clear_mask < 0) {
        return -1;
    }
    old_cleared = emit_i64_binop_instr(sf, CC_SSA_AND, oldv, clear_mask, 0, diag);
    if (old_cleared < 0) {
        return -1;
    }
    merged = emit_i64_binop_instr(sf, CC_SSA_OR, old_cleared, new_bits, 0, diag);
    if (merged < 0) {
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_STORE;
    in.dst = -1;
    in.lhs = ptrv;
    in.rhs = merged;
    in.imm = storage_size;
    if (push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory storing bit-field");
        return -1;
    }
    return lower_bitfield_load_from_addr(sf, e, ptrv, diag);
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
        int bits = cc_target_pointer_size() * 8;
        if (bits > 0) {
            return bits > 64 ? 64 : bits;
        }
        return 64;
    }
    return 32;
}

/**
 * Count the set bits of a value, in the usual SWAR steps: pairs, nibbles,
 * bytes, then a multiply that sums the bytes into the top one.
 *
 * @param sf    the function being lowered
 * @param v     the value; no bits may be set above @p bits
 * @param bits  its width: up to 32 counts with 32-bit masks (which is
 *              what an i386 register holds), else with 64-bit ones
 * @param diag  receives an error
 * @return      the value holding the count, or -1
 */
static int lower_popcount(cc_ssa_function_t *sf, int v, int bits, cc_diag_t *diag) {
    int wide = bits > 32;
    int m1;
    int m2;
    int m4;
    int h01;
    int c1;
    int c2;
    int c4;
    int ctop;
    int cff;
    int t;

    if (wide) {
        m1 = emit_const_u64_instr(sf, 0x5555555555555555ULL, diag);
        m2 = emit_const_u64_instr(sf, 0x3333333333333333ULL, diag);
        m4 = emit_const_u64_instr(sf, 0x0f0f0f0f0f0f0f0fULL, diag);
        h01 = emit_const_u64_instr(sf, 0x0101010101010101ULL, diag);
    } else {
        m1 = emit_const_i64_instr(sf, 0x55555555);
        m2 = emit_const_i64_instr(sf, 0x33333333);
        m4 = emit_const_i64_instr(sf, 0x0f0f0f0f);
        h01 = emit_const_i64_instr(sf, 0x01010101);
    }
    c1 = emit_const_i64_instr(sf, 1);
    c2 = emit_const_i64_instr(sf, 2);
    c4 = emit_const_i64_instr(sf, 4);
    ctop = emit_const_i64_instr(sf, wide ? 56 : 24);
    cff = wide ? emit_const_u64_instr(sf, 0xff, diag) : emit_const_i64_instr(sf, 0xff);
    if (m1 < 0 || m2 < 0 || m4 < 0 || h01 < 0 || c1 < 0 || c2 < 0 || c4 < 0 || ctop < 0 || cff < 0) {
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
    v = emit_i64_binop_instr(sf, CC_SSA_SHR, v, ctop, 1, diag);
    if (v < 0) {
        return -1;
    }
    /* in 64-bit arithmetic a 32-bit count leaves partial sums above the top byte */
    return emit_i64_binop_instr(sf, CC_SSA_AND, v, cff, 0, diag);
}

static int lower_builtin_ctz_from_normalized(cc_ssa_function_t *sf, int x, int bits, cc_diag_t *diag) {
    int zero;
    int low;
    int v;
    int one;

    if (bits <= 0 || bits > 64) {
        bits = 64;
    }

    /*
     * The constants must be as wide as x: on i386 a 64-bit x is a register
     * pair, and a 32-bit constant would leave the high half undefined.
     */
    zero = bits > 32 ? emit_const_u64_instr(sf, 0, diag) : emit_const_i64_instr(sf, 0);
    one = bits > 32 ? emit_const_u64_instr(sf, 1, diag) : emit_const_i64_instr(sf, 1);
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

    return lower_popcount(sf, v, bits, diag);
}

static int lower_builtin_ctz(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                             var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    int bits;
    int x;

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
    x = normalize_int_ct(sf, x, lower_value_ctype(e->args[0]), diag);
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
    return lower_builtin_ctz_from_normalized(sf, x, bits, diag);
}

static int lower_builtin_ffs(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                             var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int bits;
    int x;
    int z;
    int one;
    int cmp;
    int ctz;
    int nz;
    int dst;
    int l_nz;
    int l_z;
    int l_end;

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
    x = normalize_int_ct(sf, x, lower_value_ctype(e->args[0]), diag);
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

    /* as wide as x, or i386 compares a 64-bit x against half a zero */
    z = bits > 32 ? emit_const_u64_instr(sf, 0, diag) : emit_const_i64_instr(sf, 0);
    one = emit_const_i64_instr(sf, 1);
    if (z < 0 || one < 0) {
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CMP;
    in.cmp_kind = CC_CMP_NE;
    in.is_unsigned = 1;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = x;
    in.rhs = z;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory lowering __builtin_ffs compare");
        return -1;
    }
    cmp = in.dst;

    dst = new_value(sf, CC_VAL_I64);
    l_nz = new_label(sf);
    l_z = new_label(sf);
    l_end = new_label(sf);
    if (dst < 0 || emit_br_cond_instr(sf, cmp, l_nz, l_z) != 0 || emit_label_instr(sf, l_nz) != 0) {
        set_diag(diag, "out of memory lowering __builtin_ffs control flow");
        return -1;
    }

    ctz = lower_builtin_ctz_from_normalized(sf, x, bits, diag);
    if (ctz < 0) {
        return -1;
    }
    nz = emit_i64_binop_instr(sf, CC_SSA_ADD, ctz, one, 0, diag);
    if (nz < 0 || emit_mov_instr(sf, dst, nz) != 0 || emit_br_instr(sf, l_end) != 0 || emit_label_instr(sf, l_z) != 0 ||
        emit_mov_instr(sf, dst, z) != 0 || emit_br_instr(sf, l_end) != 0 || emit_label_instr(sf, l_end) != 0) {
        set_diag(diag, "out of memory lowering __builtin_ffs value");
        return -1;
    }

    return dst;
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
    x = normalize_int_ct(sf, x, lower_value_ctype(e->args[0]), diag);
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

    pc = lower_popcount(sf, x, bits, diag);
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
    in.sym = cc_strdup(name);
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
static int lower_array_init_to_ptr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                   var_entry_t *vars, size_t var_count, int depth, cc_type_t array_type, int struct_id,
                                   int array_ndim, const long array_dims[CC_MAX_ARRAY_DIMS], const cc_expr_t *init,
                                   cc_diag_t *diag);

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
    } else if (lower_record_id(e->lhs) >= 0) {
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
    set_value_size(sf, offv, cc_target_pointer_size());
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_ADD;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = base_ptr;
    in.rhs = offv;
    set_value_size(sf, in.dst, cc_target_pointer_size());
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory computing member address");
        return -1;
    }
    return in.dst;
}

/**
 * The element type of a complex type -- given in the legacy encoding as the
 * structure `{ T __re, __im; }` standing for it -- or CC_TYPE_VOID.
 */
static cc_type_t complex_elem_of(const cc_translation_unit_t *tu, cc_type_t t, int sid) {
    if (t != CC_TYPE_VOID || sid < 0 || (size_t)sid >= tu->struct_count) {
        return CC_TYPE_VOID;
    }
    return tu->structs[sid].complex_elem;
}

/** A floating value of type @p from as one of type @p to (float, double or long double). */
static int complex_fp_convert(cc_ssa_function_t *sf, int v, cc_type_t from, cc_type_t to, cc_diag_t *diag) {
    if (v < 0 || from == to) {
        return v;
    }
    if (to == CC_TYPE_LDOUBLE) {
        return promote_f64_to_ldouble_value(sf, v, diag);
    }
    v = demote_ldouble_to_f64_value(sf, v, diag);
    return to == CC_TYPE_FLOAT ? normalize_float_value(sf, v, CC_TYPE_FLOAT, diag) : v;
}

/** @p addr + @p off, as a pointer (pointer-sized, as a member address is). */
static int complex_part_addr(cc_ssa_function_t *sf, int addr, long off, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int offv = emit_const_i64_instr(sf, off);

    if (offv < 0) {
        return -1;
    }
    set_value_size(sf, offv, cc_target_pointer_size());
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_ADD;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = addr;
    in.rhs = offv;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory computing a complex part's address");
        return -1;
    }
    set_value_size(sf, in.dst, cc_target_pointer_size());
    return in.dst;
}

/** Load the real (@p imag 0) or imaginary part of the complex value at @p addr. */
static int complex_load_part(cc_ssa_function_t *sf, int addr, cc_type_t elem, int imag, cc_diag_t *diag) {
    long es = type_size_bytes(elem);
    cc_ssa_instr_t ld;
    int p = addr;

    if (imag) {
        p = complex_part_addr(sf, addr, es, diag);
        if (p < 0) {
            return -1;
        }
    }
    memset(&ld, 0, sizeof(ld));
    ld.op = CC_SSA_LOAD;
    ld.dst = new_value(sf, CC_VAL_F64);
    ld.lhs = p;
    ld.rhs = -1;
    ld.imm = es;
    if (ld.dst < 0 || push_instr(sf, ld) != 0) {
        set_diag(diag, "out of memory loading a complex part");
        return -1;
    }
    return ld.dst;
}

/** Store @p re and @p im, of type @p elem, as a new complex temporary; return its address. */
static int complex_store_new(cc_ssa_function_t *sf, int re, int im, cc_type_t elem, cc_diag_t *diag) {
    long es = type_size_bytes(elem);
    int addr = emit_local_storage_alloc(sf, 2 * es, diag);
    int ip = addr < 0 ? -1 : complex_part_addr(sf, addr, es, diag);
    cc_ssa_instr_t st;

    if (ip < 0) {
        return -1;
    }
    memset(&st, 0, sizeof(st));
    st.op = CC_SSA_STORE;
    st.dst = -1;
    st.lhs = addr;
    st.rhs = re;
    st.imm = es;
    if (push_instr(sf, st) != 0) {
        set_diag(diag, "out of memory storing a complex part");
        return -1;
    }
    st.lhs = ip;
    st.rhs = im;
    if (push_instr(sf, st) != 0) {
        set_diag(diag, "out of memory storing a complex part");
        return -1;
    }
    return addr;
}

/** A floating zero of type @p elem. */
static int complex_fp_zero(cc_ssa_function_t *sf, cc_type_t elem, cc_diag_t *diag) {
    int z = emit_const_f64_instr(sf, 0.0);

    return z < 0 ? -1 : complex_fp_convert(sf, z, CC_TYPE_DOUBLE, elem, diag);
}

/** @p a @p op @p b in the floating type @p elem, rounded to it. */
static int complex_fop(cc_ssa_function_t *sf, cc_ssa_opcode_t op, int a, int b, cc_type_t elem, cc_diag_t *diag) {
    cc_ssa_instr_t in;

    if (a < 0 || b < 0) {
        return -1;
    }
    memset(&in, 0, sizeof(in));
    in.op = op;
    in.dst = new_value(sf, CC_VAL_F64);
    in.lhs = a;
    in.rhs = b;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory in complex arithmetic");
        return -1;
    }
    if (elem == CC_TYPE_LDOUBLE) {
        set_value_size(sf, in.dst, 16);
        return in.dst;
    }
    return elem == CC_TYPE_FLOAT ? normalize_float_value(sf, in.dst, CC_TYPE_FLOAT, diag) : in.dst;
}

/** -@p a, exactly (a zero's sign flips). */
static int complex_fneg(cc_ssa_function_t *sf, int a, cc_type_t elem, cc_diag_t *diag) {
    int m1 = emit_const_f64_instr(sf, -1.0);

    m1 = m1 < 0 ? -1 : complex_fp_convert(sf, m1, CC_TYPE_DOUBLE, elem, diag);
    return complex_fop(sf, CC_SSA_MUL, a, m1, elem, diag);
}

/**
 * Read an operand of a complex operation as parts of type @p elem: a complex
 * one's two parts, or a real one's value with *@p im set to -1 (there is no
 * imaginary part to add: C11 Annex G.5 keeps a real operand real, so that
 * signed zeros come out as they should).
 */
static int complex_operand(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                           var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_type_t elem,
                           int *re, int *im, cc_diag_t *diag) {
    cc_type_t ce = complex_elem_of(tu, CC_TYPE_VOID, lower_record_id(e));

    if (ce != CC_TYPE_VOID) {
        int addr = lower_expr(tu, sf, ctx, vars, var_count, depth, e, diag);

        *re = addr < 0 ? -1 : complex_fp_convert(sf, complex_load_part(sf, addr, ce, 0, diag), ce, elem, diag);
        *im = addr < 0 ? -1 : complex_fp_convert(sf, complex_load_part(sf, addr, ce, 1, diag), ce, elem, diag);
        return (*re < 0 || *im < 0) ? -1 : 0;
    } else {
        cc_expr_t c;

        memset(&c, 0, sizeof(c));
        c.kind = CC_EXPR_CAST;
        c.aux_type = elem;
        c.aux_struct_id = -1;
        c.value_type = elem;
        c.struct_id = -1;
        c.ctype = cc_ct_from_scalar(elem);
        c.lhs = (cc_expr_t *)e;
        *re = lower_expr(tu, sf, ctx, vars, var_count, depth, &c, diag);
        *im = -1;
        return *re < 0 ? -1 : 0;
    }
}

/**
 * (a + bi) / (c + di) as libgcc's __divdc3 computes it for finite operands
 * (Smith's method, dividing by the larger of |c| and |d|), so that results
 * are gcc's to the bit; its rescaling of huge or tiny operands and its
 * recovery of infinities from NaN are not done.
 */
static int complex_div(cc_ssa_function_t *sf, int a, int b, int c, int d, cc_type_t elem, int *x, int *y,
                       cc_diag_t *diag) {
    int c2 = complex_fop(sf, CC_SSA_MUL, c, c, elem, diag);
    int d2 = complex_fop(sf, CC_SSA_MUL, d, d, elem, diag);
    int lt = (c2 < 0 || d2 < 0) ? -1 : emit_cmp_instr(sf, CC_CMP_LT, c2, d2, 0, diag);
    int l_d = new_label(sf);
    int l_c = new_label(sf);
    int l_end = new_label(sf);
    int ratio;
    int denom;
    int xv;
    int yv;

    *x = new_value(sf, CC_VAL_F64);
    *y = new_value(sf, CC_VAL_F64);
    if (lt < 0 || l_d < 0 || l_c < 0 || l_end < 0 || *x < 0 || *y < 0 ||
        emit_br_cond_instr(sf, lt, l_d, l_c) != 0 || emit_label_instr(sf, l_d) != 0) {
        return -1;
    }
    if (elem == CC_TYPE_LDOUBLE) {
        set_value_size(sf, *x, 16);
        set_value_size(sf, *y, 16);
    }
    /* |c| < |d|: ratio = c/d; denom = c*ratio + d; x = (a*ratio + b)/denom; y = (b*ratio - a)/denom */
    ratio = complex_fop(sf, CC_SSA_DIV, c, d, elem, diag);
    denom = complex_fop(sf, CC_SSA_ADD, complex_fop(sf, CC_SSA_MUL, c, ratio, elem, diag), d, elem, diag);
    xv = complex_fop(sf, CC_SSA_DIV, complex_fop(sf, CC_SSA_ADD, complex_fop(sf, CC_SSA_MUL, a, ratio, elem, diag), b,
                                                 elem, diag),
                     denom, elem, diag);
    yv = complex_fop(sf, CC_SSA_DIV, complex_fop(sf, CC_SSA_SUB, complex_fop(sf, CC_SSA_MUL, b, ratio, elem, diag), a,
                                                 elem, diag),
                     denom, elem, diag);
    if (xv < 0 || yv < 0 || emit_mov_instr(sf, *x, xv) != 0 || emit_mov_instr(sf, *y, yv) != 0 ||
        emit_br_instr(sf, l_end) != 0 || emit_label_instr(sf, l_c) != 0) {
        return -1;
    }
    /* else: ratio = d/c; denom = d*ratio + c; x = (b*ratio + a)/denom; y = (b - a*ratio)/denom */
    ratio = complex_fop(sf, CC_SSA_DIV, d, c, elem, diag);
    denom = complex_fop(sf, CC_SSA_ADD, complex_fop(sf, CC_SSA_MUL, d, ratio, elem, diag), c, elem, diag);
    xv = complex_fop(sf, CC_SSA_DIV, complex_fop(sf, CC_SSA_ADD, complex_fop(sf, CC_SSA_MUL, b, ratio, elem, diag), a,
                                                 elem, diag),
                     denom, elem, diag);
    yv = complex_fop(sf, CC_SSA_DIV, complex_fop(sf, CC_SSA_SUB, b, complex_fop(sf, CC_SSA_MUL, a, ratio, elem, diag),
                                                 elem, diag),
                     denom, elem, diag);
    if (xv < 0 || yv < 0 || emit_mov_instr(sf, *x, xv) != 0 || emit_mov_instr(sf, *y, yv) != 0 ||
        emit_br_instr(sf, l_end) != 0 || emit_label_instr(sf, l_end) != 0) {
        return -1;
    }
    return 0;
}

/**
 * A binary operator with a complex operand (see sema_complex_binop()): +,
 * -, *, / give a new complex temporary's address; == and != an int; `z ^ -1`
 * (GNU `~z`) the conjugate.  A real operand stays real (Annex G.5.1).
 */
static int lower_complex_binop(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                               var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    cc_type_t elem = complex_elem_of(tu, CC_TYPE_VOID, lower_record_id(e));
    int a, b, c, d;
    int x = -1;
    int y = -1;

    if (elem == CC_TYPE_VOID) {
        /* == or !=: compare in the wider of the operands' types */
        cc_type_t le = complex_elem_of(tu, CC_TYPE_VOID, lower_record_id(e->lhs));
        cc_type_t re = complex_elem_of(tu, CC_TYPE_VOID, lower_record_id(e->rhs));
        cc_type_t lt = le != CC_TYPE_VOID ? le : (lower_has_kind(e->lhs, CC_CT_LDOUBLE) ? CC_TYPE_LDOUBLE : CC_TYPE_DOUBLE);
        cc_type_t rt = re != CC_TYPE_VOID ? re : (lower_has_kind(e->rhs, CC_CT_LDOUBLE) ? CC_TYPE_LDOUBLE : CC_TYPE_DOUBLE);
        int eq_re;
        int eq_im;
        int both;

        elem = (lt == CC_TYPE_LDOUBLE || rt == CC_TYPE_LDOUBLE) ? CC_TYPE_LDOUBLE
               : (lt == CC_TYPE_FLOAT && rt == CC_TYPE_FLOAT) ? CC_TYPE_FLOAT : CC_TYPE_DOUBLE;
        if (complex_operand(tu, sf, ctx, vars, var_count, depth, e->lhs, elem, &a, &b, diag) != 0 ||
            complex_operand(tu, sf, ctx, vars, var_count, depth, e->rhs, elem, &c, &d, diag) != 0) {
            return -1;
        }
        if (b < 0) {
            b = complex_fp_zero(sf, elem, diag);
        }
        if (d < 0) {
            d = complex_fp_zero(sf, elem, diag);
        }
        eq_re = emit_cmp_instr(sf, CC_CMP_EQ, a, c, 0, diag);
        eq_im = emit_cmp_instr(sf, CC_CMP_EQ, b, d, 0, diag);
        both = (eq_re < 0 || eq_im < 0) ? -1 : emit_i64_binop_instr(sf, CC_SSA_AND, eq_re, eq_im, 1, diag);
        if (both < 0 || e->op == CC_BIN_EQ) {
            return both;
        }
        return emit_i64_binop_instr(sf, CC_SSA_XOR, both, emit_const_i64_instr(sf, 1), 1, diag);
    }
    if (e->op == CC_BIN_BXOR) {
        /* the conjugate */
        if (complex_operand(tu, sf, ctx, vars, var_count, depth, e->lhs, elem, &a, &b, diag) != 0) {
            return -1;
        }
        return complex_store_new(sf, a, complex_fneg(sf, b, elem, diag), elem, diag);
    }
    if (complex_operand(tu, sf, ctx, vars, var_count, depth, e->lhs, elem, &a, &b, diag) != 0 ||
        complex_operand(tu, sf, ctx, vars, var_count, depth, e->rhs, elem, &c, &d, diag) != 0) {
        return -1;
    }
    switch (e->op) {
    case CC_BIN_ADD:
        x = complex_fop(sf, CC_SSA_ADD, a, c, elem, diag);
        y = b < 0 ? d : (d < 0 ? b : complex_fop(sf, CC_SSA_ADD, b, d, elem, diag));
        break;
    case CC_BIN_SUB:
        x = complex_fop(sf, CC_SSA_SUB, a, c, elem, diag);
        y = d < 0 ? b : (b < 0 ? complex_fneg(sf, d, elem, diag) : complex_fop(sf, CC_SSA_SUB, b, d, elem, diag));
        break;
    case CC_BIN_MUL:
        if (b < 0) {
            x = complex_fop(sf, CC_SSA_MUL, a, c, elem, diag);
            y = complex_fop(sf, CC_SSA_MUL, a, d, elem, diag);
        } else if (d < 0) {
            x = complex_fop(sf, CC_SSA_MUL, a, c, elem, diag);
            y = complex_fop(sf, CC_SSA_MUL, b, c, elem, diag);
        } else {
            int ac = complex_fop(sf, CC_SSA_MUL, a, c, elem, diag);
            int bd = complex_fop(sf, CC_SSA_MUL, b, d, elem, diag);
            int ad = complex_fop(sf, CC_SSA_MUL, a, d, elem, diag);
            int bc = complex_fop(sf, CC_SSA_MUL, b, c, elem, diag);

            x = complex_fop(sf, CC_SSA_SUB, ac, bd, elem, diag);
            y = complex_fop(sf, CC_SSA_ADD, ad, bc, elem, diag);
        }
        break;
    case CC_BIN_DIV:
        if (d < 0) {
            x = complex_fop(sf, CC_SSA_DIV, a, c, elem, diag);
            y = complex_fop(sf, CC_SSA_DIV, b, c, elem, diag);
        } else {
            /* libgcc's __divsc3 divides float parts in double and rounds the results once */
            cc_type_t work = elem == CC_TYPE_FLOAT ? CC_TYPE_DOUBLE : elem;

            if (b < 0) {
                b = complex_fp_zero(sf, work, diag);
            }
            if (complex_div(sf, a, b, c, d, work, &x, &y, diag) != 0) {
                return -1;
            }
            if (work != elem) {
                x = complex_fp_convert(sf, x, work, elem, diag);
                y = complex_fp_convert(sf, y, work, elem, diag);
            }
        }
        break;
    default:
        set_diag(diag, "invalid complex operation in lowering");
        return -1;
    }
    if (x < 0 || y < 0) {
        return -1;
    }
    return complex_store_new(sf, x, y, elem, diag);
}

/**
 * A cast to a complex type: a new temporary holding the source converted --
 * a complex source's two parts, read once, or a real source as the real
 * part with a zero imaginary part.
 */
static int lower_complex_cast(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                              var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    cc_type_t elem = complex_elem_of(tu, CC_TYPE_VOID, ct_record_id(lower_cast_ctype(e)));
    int re;
    int im;

    if (complex_operand(tu, sf, ctx, vars, var_count, depth, e->lhs, elem, &re, &im, diag) != 0) {
        return -1;
    }
    if (im < 0) {
        im = complex_fp_zero(sf, elem, diag);
    }
    return im < 0 ? -1 : complex_store_new(sf, re, im, elem, diag);
}

/*
 * Integers wider than 64 bits.  A value of one is the address of its
 * limbs (the structure parser_wide_struct() makes), least significant
 * first, each a target word -- a limb is the pointer's size on both
 * targets.  Every operation reads its operands' limbs and writes a new
 * temporary's; the bits of the top limb above the width are kept as the
 * sign (or zero) extension of the value, and are not trusted on input:
 * reading the top limb extends it again (wide_ld()).  Multiplication,
 * division and the floating conversions call libgcc's _BitInt helpers, as
 * gcc does.
 */

/** A wide integer type's shape. */
typedef struct {
    long bits;       /**< its width N */
    long n;          /**< limbs */
    long limb;       /**< bytes in a limb (the pointer size) */
    int is_unsigned; /**< unsigned */
} wide_t;

/** Fill @p w for (@p t, @p sid) if it is a wide integer type; return whether it is. */
static int wide_of(const cc_translation_unit_t *tu, cc_type_t t, int sid, wide_t *w) {
    const cc_struct_def_t *sd;

    if (t != CC_TYPE_VOID || tu == NULL || sid < 0 || (size_t)sid >= tu->struct_count) {
        return 0;
    }
    sd = &tu->structs[sid];
    if (sd->wide_bits <= 0) {
        return 0;
    }
    w->bits = sd->wide_bits;
    w->limb = cc_target_pointer_size();
    w->n = (w->bits + w->limb * 8 - 1) / (w->limb * 8);
    w->is_unsigned = sd->wide_unsigned;
    return 1;
}

/** A limb-sized constant. */
static int wide_k(cc_ssa_function_t *sf, long long v, const wide_t *w) {
    int c = emit_const_i64_instr(sf, v);

    if (c >= 0) {
        set_value_size(sf, c, w->limb);
    }
    return c;
}

/** @p a @p op @p b on limbs (limb-sized: 32-bit operations on i386). */
static int wide_op(cc_ssa_function_t *sf, cc_ssa_opcode_t op, int a, int b, int is_unsigned, const wide_t *w,
                   cc_diag_t *diag) {
    int r = (a < 0 || b < 0) ? -1 : emit_i64_binop_instr(sf, op, a, b, is_unsigned, diag);

    if (r >= 0) {
        set_value_size(sf, r, w->limb);
    }
    return r;
}

/** A limb comparison, 1 or 0, limb-sized. */
static int wide_cmp(cc_ssa_function_t *sf, cc_cmp_kind_t kind, int a, int b, int is_unsigned, const wide_t *w,
                    cc_diag_t *diag) {
    int r = (a < 0 || b < 0) ? -1 : emit_cmp_instr(sf, kind, a, b, is_unsigned, diag);

    if (r >= 0) {
        set_value_size(sf, r, w->limb);
    }
    return r;
}

/** The address of limb @p i of the wide value at @p base. */
static int wide_addr(cc_ssa_function_t *sf, int base, long i, const wide_t *w, cc_diag_t *diag) {
    return (base < 0 || i == 0) ? base : complex_part_addr(sf, base, i * w->limb, diag);
}

/**
 * Limb @p i of the wide value at @p base.  The top limb comes back
 * extended from the value's width -- sign-extended if the type is signed --
 * whatever its stored padding bits.
 */
static int wide_ld(cc_ssa_function_t *sf, int base, long i, const wide_t *w, cc_diag_t *diag) {
    cc_ssa_instr_t ld;
    int p = wide_addr(sf, base, i, w, diag);
    long top_bits = w->bits - (w->n - 1) * w->limb * 8;

    if (p < 0) {
        return -1;
    }
    memset(&ld, 0, sizeof(ld));
    ld.op = CC_SSA_LOAD;
    ld.dst = new_value(sf, CC_VAL_I64);
    ld.lhs = p;
    ld.rhs = -1;
    ld.imm = w->limb;
    ld.is_unsigned = 1;
    if (ld.dst < 0 || push_instr(sf, ld) != 0) {
        set_diag(diag, "out of memory loading a limb");
        return -1;
    }
    set_value_size(sf, ld.dst, w->limb);
    if (i == w->n - 1 && top_bits < w->limb * 8) {
        int sh = wide_k(sf, w->limb * 8 - top_bits, w);
        int v = wide_op(sf, CC_SSA_SHL, ld.dst, sh, 1, w, diag);

        return wide_op(sf, CC_SSA_SHR, v, sh, w->is_unsigned, w, diag);
    }
    return ld.dst;
}

/** Store limb value @p v as limb @p i of the wide value at @p base. */
static int wide_st(cc_ssa_function_t *sf, int base, long i, int v, const wide_t *w, cc_diag_t *diag) {
    cc_ssa_instr_t st;
    int p = wide_addr(sf, base, i, w, diag);

    if (p < 0 || v < 0) {
        return -1;
    }
    memset(&st, 0, sizeof(st));
    st.op = CC_SSA_STORE;
    st.dst = -1;
    st.lhs = p;
    st.rhs = v;
    st.imm = w->limb;
    if (push_instr(sf, st) != 0) {
        set_diag(diag, "out of memory storing a limb");
        return -1;
    }
    return 0;
}

/** Re-extend the top limb of the wide value at @p addr from its width (after an operation that wrote it). */
static int wide_fix_top(cc_ssa_function_t *sf, int addr, const wide_t *w, cc_diag_t *diag) {
    if (w->bits % (w->limb * 8) == 0) {
        return 0;
    }
    return wide_st(sf, addr, w->n - 1, wide_ld(sf, addr, w->n - 1, w, diag), w, diag);
}

/** The limb filling a widened value: all ones if signed and negative, else zero. */
static int wide_fill(cc_ssa_function_t *sf, int addr, const wide_t *w, cc_diag_t *diag) {
    if (w->is_unsigned) {
        return wide_k(sf, 0, w);
    }
    return wide_op(sf, CC_SSA_SHR, wide_ld(sf, addr, w->n - 1, w, diag), wide_k(sf, w->limb * 8 - 1, w), 0, w,
                   diag);
}

/**
 * A call to a libgcc helper, `void`/scalar `name(args...)`.  @p abi gives
 * each argument's ABI kind (NULL: all default); the returned value, if
 * @p ret_vt is not -1, is a new value of that kind.
 */
static int wide_libcall(cc_ssa_function_t *sf, const char *name, const int *args, size_t nargs,
                        const unsigned char *abi, int ret_vt, int ret_agg_class, int ret_x87, cc_diag_t *diag) {
    cc_ssa_instr_t call_in;
    size_t i;

    memset(&call_in, 0, sizeof(call_in));
    call_in.op = CC_SSA_CALL;
    call_in.sym = cc_strdup(name);
    call_in.arg_count = nargs;
    call_in.args = (int *)calloc(nargs, sizeof(*call_in.args));
    call_in.call_arg_abi = (unsigned char *)calloc(nargs, sizeof(*call_in.call_arg_abi));
    call_in.call_arg_sizes = (uint32_t *)calloc(nargs, sizeof(*call_in.call_arg_sizes));
    call_in.dst = new_value(sf, ret_vt >= 0 ? (cc_value_type_t)ret_vt : CC_VAL_I64);
    call_in.rhs = -1;
    call_in.agg_class = ret_agg_class;
    call_in.call_ret_x87 = ret_x87;
    if (call_in.sym == NULL || call_in.args == NULL || call_in.call_arg_abi == NULL || call_in.call_arg_sizes == NULL ||
        call_in.dst < 0) {
        free(call_in.sym);
        free(call_in.args);
        free(call_in.call_arg_abi);
        free(call_in.call_arg_sizes);
        set_diag(diag, "out of memory emitting a helper call");
        return -1;
    }
    for (i = 0; i < nargs; ++i) {
        if (args[i] < 0) {
            free(call_in.sym);
            free(call_in.args);
            free(call_in.call_arg_abi);
            free(call_in.call_arg_sizes);
            return -1;
        }
        call_in.args[i] = args[i];
        call_in.call_arg_abi[i] = abi != NULL ? abi[i] : CC_CALL_ARG_ABI_DEFAULT;
        call_in.call_arg_sizes[i] = (uint32_t)(abi != NULL && abi[i] == CC_CALL_ARG_ABI_FLOAT ? 4
                                               : abi != NULL && abi[i] == CC_CALL_ARG_ABI_LDOUBLE
                                                   ? 12
                                                   : value_size(sf, args[i]));
    }
    if (ret_x87) {
        set_value_size(sf, call_in.dst, 16);
    }
    if (push_instr(sf, call_in) != 0) {
        free(call_in.sym);
        free(call_in.args);
        free(call_in.call_arg_abi);
        free(call_in.call_arg_sizes);
        set_diag(diag, "out of memory emitting a helper call");
        return -1;
    }
    return call_in.dst;
}

/** An int-sized argument to a libgcc helper (a precision): 4 bytes on both targets. */
static int wide_int_arg(cc_ssa_function_t *sf, long v) {
    int c = emit_const_i64_instr(sf, v);

    if (c >= 0) {
        set_value_size(sf, c, 4);
    }
    return c;
}

/** A new wide temporary of shape @p w. */
static int wide_new(cc_ssa_function_t *sf, const wide_t *w, cc_diag_t *diag) {
    return emit_local_storage_alloc(sf, w->n * w->limb, diag);
}

/**
 * @p a + - & | ^ @p b, limb by limb with the carry or borrow, into a new
 * temporary.
 */
static int wide_arith(cc_ssa_function_t *sf, cc_binop_t op, int a, int b, const wide_t *w, cc_diag_t *diag) {
    int r = wide_new(sf, w, diag);
    int carry = wide_k(sf, 0, w);
    long i;

    for (i = 0; r >= 0 && i < w->n; ++i) {
        int x = wide_ld(sf, a, i, w, diag);
        int y = wide_ld(sf, b, i, w, diag);
        int v;

        switch (op) {
        case CC_BIN_ADD: {
            int s = wide_op(sf, CC_SSA_ADD, x, y, 1, w, diag);
            int c1 = wide_cmp(sf, CC_CMP_LT, s, x, 1, w, diag);
            int s2 = wide_op(sf, CC_SSA_ADD, s, carry, 1, w, diag);
            int c2 = wide_cmp(sf, CC_CMP_LT, s2, s, 1, w, diag);

            carry = wide_op(sf, CC_SSA_OR, c1, c2, 1, w, diag);
            v = s2;
            break;
        }
        case CC_BIN_SUB: {
            int d = wide_op(sf, CC_SSA_SUB, x, y, 1, w, diag);
            int b1 = wide_cmp(sf, CC_CMP_LT, x, y, 1, w, diag);
            int d2 = wide_op(sf, CC_SSA_SUB, d, carry, 1, w, diag);
            int b2 = wide_cmp(sf, CC_CMP_LT, d, carry, 1, w, diag);

            carry = wide_op(sf, CC_SSA_OR, b1, b2, 1, w, diag);
            v = d2;
            break;
        }
        case CC_BIN_BAND:
            v = wide_op(sf, CC_SSA_AND, x, y, 1, w, diag);
            break;
        case CC_BIN_BOR:
            v = wide_op(sf, CC_SSA_OR, x, y, 1, w, diag);
            break;
        default:
            v = wide_op(sf, CC_SSA_XOR, x, y, 1, w, diag);
            break;
        }
        if (wide_st(sf, r, i, v, w, diag) != 0) {
            return -1;
        }
    }
    return (r < 0 || wide_fix_top(sf, r, w, diag) != 0) ? -1 : r;
}

/** A wide zero, as a new temporary. */
static int wide_zero(cc_ssa_function_t *sf, const wide_t *w, cc_diag_t *diag) {
    int r = wide_new(sf, w, diag);
    long i;

    for (i = 0; r >= 0 && i < w->n; ++i) {
        if (wide_st(sf, r, i, wide_k(sf, 0, w), w, diag) != 0) {
            return -1;
        }
    }
    return r;
}

/**
 * @p a << or >> @p count (a long long value): the limbs are copied into
 * the middle of a scratch area three times their length -- zeros below,
 * the sign fill above -- so each result limb is two loads at a run-time
 * offset, branch-free.  A count of the width or more is undefined in C; it
 * is clamped to keep the loads inside the scratch area.
 */
static int wide_shift(cc_ssa_function_t *sf, int is_left, int a, int count, const wide_t *w, cc_diag_t *diag) {
    long W = w->limb * 8;
    cc_type_t word = w->limb == 8 ? CC_TYPE_ULONG_LONG : CC_TYPE_UINT;
    wide_t s3 = *w;
    int scratch;
    int fill;
    int q;
    int r;
    int qoff;
    int wm1r;
    int res;
    long i;
    int lt;
    int l_clamp = new_label(sf);
    int l_go = new_label(sf);

    s3.n = 3 * w->n;
    s3.bits = s3.n * W;
    scratch = emit_local_storage_alloc(sf, s3.n * w->limb, diag);
    fill = wide_fill(sf, a, w, diag);
    for (i = 0; scratch >= 0 && fill >= 0 && i < w->n; ++i) {
        if (wide_st(sf, scratch, i, wide_k(sf, 0, w), &s3, diag) != 0 ||
            wide_st(sf, scratch, w->n + i, wide_ld(sf, a, i, w, diag), &s3, diag) != 0 ||
            wide_st(sf, scratch, 2 * w->n + i, is_left ? wide_k(sf, 0, w) : fill, &s3, diag) != 0) {
            return -1;
        }
    }
    /* q whole limbs and r bits, as pointer-sized words */
    q = normalize_integral_value(sf, emit_i64_binop_instr(sf, CC_SSA_SHR, count, emit_const_i64_instr(sf, W == 64 ? 6 : 5),
                                                          1, diag),
                                 word, diag);
    r = normalize_integral_value(sf, emit_i64_binop_instr(sf, CC_SSA_AND, count, emit_const_i64_instr(sf, W - 1), 1, diag),
                                 word, diag);
    if (q < 0 || r < 0 || l_clamp < 0 || l_go < 0) {
        return -1;
    }
    set_value_size(sf, q, w->limb);
    set_value_size(sf, r, w->limb);
    lt = wide_cmp(sf, CC_CMP_GT, q, wide_k(sf, w->n, w), 1, w, diag);
    if (lt < 0 || emit_br_cond_instr(sf, lt, l_clamp, l_go) != 0 || emit_label_instr(sf, l_clamp) != 0 ||
        emit_mov_instr(sf, q, wide_k(sf, w->n, w)) != 0 || emit_br_instr(sf, l_go) != 0 ||
        emit_label_instr(sf, l_go) != 0) {
        return -1;
    }
    qoff = wide_op(sf, CC_SSA_MUL, q, wide_k(sf, w->limb, w), 1, w, diag);
    wm1r = wide_op(sf, CC_SSA_SUB, wide_k(sf, W - 1, w), r, 1, w, diag);
    res = wide_new(sf, w, diag);
    for (i = 0; res >= 0 && qoff >= 0 && wm1r >= 0 && i < w->n; ++i) {
        int mid = complex_part_addr(sf, scratch, (w->n + i) * w->limb, diag);
        int p0 = wide_op(sf, is_left ? CC_SSA_SUB : CC_SSA_ADD, mid, qoff, 1, w, diag);
        int p1 = p0 < 0 ? -1 : complex_part_addr(sf, p0, is_left ? -w->limb : w->limb, diag);
        int x = wide_ld(sf, p0, 0, &s3, diag);
        int y = wide_ld(sf, p1, 0, &s3, diag);
        int v;

        if (is_left) {
            /* x << r | (y >> 1) >> (W-1-r): no shift by W when r is 0 */
            v = wide_op(sf, CC_SSA_OR, wide_op(sf, CC_SSA_SHL, x, r, 1, w, diag),
                        wide_op(sf, CC_SSA_SHR, wide_op(sf, CC_SSA_SHR, y, wide_k(sf, 1, w), 1, w, diag), wm1r, 1, w,
                                diag),
                        1, w, diag);
        } else {
            v = wide_op(sf, CC_SSA_OR, wide_op(sf, CC_SSA_SHR, x, r, 1, w, diag),
                        wide_op(sf, CC_SSA_SHL, wide_op(sf, CC_SSA_SHL, y, wide_k(sf, 1, w), 1, w, diag), wm1r, 1, w,
                                diag),
                        1, w, diag);
        }
        if (wide_st(sf, res, i, v, w, diag) != 0) {
            return -1;
        }
    }
    return (res < 0 || wide_fix_top(sf, res, w, diag) != 0) ? -1 : res;
}

/** @p a == != < <= > >= @p b, an int: equality on all limbs, order from the top limb down. */
static int wide_compare(cc_ssa_function_t *sf, cc_binop_t op, int a, int b, const wide_t *w, cc_diag_t *diag) {
    long i;

    if (op == CC_BIN_EQ || op == CC_BIN_NE) {
        int acc = wide_k(sf, 0, w);

        for (i = 0; acc >= 0 && i < w->n; ++i) {
            acc = wide_op(sf, CC_SSA_OR, acc,
                          wide_op(sf, CC_SSA_XOR, wide_ld(sf, a, i, w, diag), wide_ld(sf, b, i, w, diag), 1, w, diag),
                          1, w, diag);
        }
        return acc < 0 ? -1 : emit_cmp_instr(sf, op == CC_BIN_EQ ? CC_CMP_EQ : CC_CMP_NE, acc, wide_k(sf, 0, w), 1,
                                             diag);
    } else {
        int res = new_value(sf, CC_VAL_I64);
        int l_lt = new_label(sf);
        int l_gt = new_label(sf);
        int l_end = new_label(sf);
        int when_lt = op == CC_BIN_LT || op == CC_BIN_LE;
        int when_eq = op == CC_BIN_LE || op == CC_BIN_GE;

        if (res < 0 || l_lt < 0 || l_gt < 0 || l_end < 0) {
            return -1;
        }
        for (i = w->n - 1; i >= 0; --i) {
            /* the top limb compares in the type's signedness, the rest unsigned */
            int uns = i == w->n - 1 ? w->is_unsigned : 1;
            int x = wide_ld(sf, a, i, w, diag);
            int y = wide_ld(sf, b, i, w, diag);
            int l1 = new_label(sf);
            int l2 = new_label(sf);
            int c1 = wide_cmp(sf, CC_CMP_LT, x, y, uns, w, diag);

            if (c1 < 0 || l1 < 0 || l2 < 0 || emit_br_cond_instr(sf, c1, l_lt, l1) != 0 ||
                emit_label_instr(sf, l1) != 0) {
                return -1;
            }
            c1 = wide_cmp(sf, CC_CMP_GT, x, y, uns, w, diag);
            if (c1 < 0 || emit_br_cond_instr(sf, c1, l_gt, l2) != 0 || emit_label_instr(sf, l2) != 0) {
                return -1;
            }
        }
        if (emit_mov_instr(sf, res, emit_const_i64_instr(sf, when_eq)) != 0 || emit_br_instr(sf, l_end) != 0 ||
            emit_label_instr(sf, l_lt) != 0 || emit_mov_instr(sf, res, emit_const_i64_instr(sf, when_lt)) != 0 ||
            emit_br_instr(sf, l_end) != 0 || emit_label_instr(sf, l_gt) != 0 ||
            emit_mov_instr(sf, res, emit_const_i64_instr(sf, !when_lt)) != 0 || emit_br_instr(sf, l_end) != 0 ||
            emit_label_instr(sf, l_end) != 0) {
            return -1;
        }
        return res;
    }
}

/** The precision a libgcc _BitInt helper takes for an operand of shape @p w: negative when signed. */
static long wide_prec(const wide_t *w) {
    return w->is_unsigned ? w->bits : -w->bits;
}

/** @p a * / % @p b by libgcc's __mulbitint3 and __divmodbitint4, into a new temporary. */
static int wide_muldiv(cc_ssa_function_t *sf, cc_binop_t op, int a, int b, const wide_t *w, cc_diag_t *diag) {
    int r = wide_new(sf, w, diag);
    int null_ptr = emit_const_i64_instr(sf, 0);

    if (r < 0 || null_ptr < 0) {
        return -1;
    }
    set_value_size(sf, null_ptr, cc_target_pointer_size());
    if (op == CC_BIN_MUL) {
        int args[6];

        args[0] = r;
        args[1] = wide_int_arg(sf, w->bits);
        args[2] = a;
        args[3] = wide_int_arg(sf, wide_prec(w));
        args[4] = b;
        args[5] = wide_int_arg(sf, wide_prec(w));
        if (wide_libcall(sf, "__mulbitint3", args, 6, NULL, -1, 0, 0, diag) < 0) {
            return -1;
        }
    } else {
        int args[8];

        args[0] = op == CC_BIN_DIV ? r : null_ptr;
        args[1] = wide_int_arg(sf, op == CC_BIN_DIV ? w->bits : 0);
        args[2] = op == CC_BIN_MOD ? r : null_ptr;
        args[3] = wide_int_arg(sf, op == CC_BIN_MOD ? w->bits : 0);
        args[4] = a;
        args[5] = wide_int_arg(sf, wide_prec(w));
        args[6] = b;
        args[7] = wide_int_arg(sf, wide_prec(w));
        if (wide_libcall(sf, "__divmodbitint4", args, 8, NULL, -1, 0, 0, diag) < 0) {
            return -1;
        }
    }
    return wide_fix_top(sf, r, w, diag) != 0 ? -1 : r;
}

/** Whether @p e is the constant -1 converted (how `-x` reaches lowering: `-1 * x`). */
static int wide_is_minus_one(const cc_expr_t *e) {
    while (e != NULL && e->kind == CC_EXPR_CAST) {
        e = e->lhs;
    }
    return e != NULL && e->kind == CC_EXPR_INT && e->int_val == -1;
}

/**
 * A binary operator on wide integers (sema cast both operands to one wide
 * type, or for a shift gave the count as a long long): a new temporary's
 * address, or an int for a comparison.
 */
static int lower_wide_binop(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                            var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    wide_t w;
    int a;
    int b;

    if (!wide_of(tu, CC_TYPE_VOID, lower_record_id(e->lhs), &w)) {
        set_diag(diag, "malformed wide integer operation in lowering");
        return -1;
    }
    if (e->op == CC_BIN_MUL && wide_is_minus_one(e->lhs)) {
        /* -x */
        b = lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
        return b < 0 ? -1 : wide_arith(sf, CC_BIN_SUB, wide_zero(sf, &w, diag), b, &w, diag);
    }
    a = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
    b = a < 0 ? -1 : lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
    if (b < 0) {
        return -1;
    }
    switch (e->op) {
    case CC_BIN_ADD:
    case CC_BIN_SUB:
    case CC_BIN_BAND:
    case CC_BIN_BOR:
    case CC_BIN_BXOR:
        return wide_arith(sf, e->op, a, b, &w, diag);
    case CC_BIN_MUL:
    case CC_BIN_DIV:
    case CC_BIN_MOD:
        return wide_muldiv(sf, e->op, a, b, &w, diag);
    case CC_BIN_SHL:
    case CC_BIN_SHR:
        return wide_shift(sf, e->op == CC_BIN_SHL, a, b, &w, diag);
    case CC_BIN_EQ:
    case CC_BIN_NE:
    case CC_BIN_LT:
    case CC_BIN_LE:
    case CC_BIN_GT:
    case CC_BIN_GE:
        return wide_compare(sf, e->op, a, b, &w, diag);
    default:
        set_diag(diag, "invalid wide integer operation in lowering");
        return -1;
    }
}

/** The shape of a vector type: see vec_of(). */
typedef struct {
    long n;          /**< its element count */
    long es;         /**< an element's size in bytes */
    cc_type_t elem;  /**< the element type */
    int is_fp;       /**< the elements are floating */
    int is_unsigned; /**< the elements are unsigned integers */
} vec_t;

/** Whether (@p t, @p sid) is a vector type; if so its shape in *@p v. */
static int vec_of(const cc_translation_unit_t *tu, cc_type_t t, int sid, vec_t *v) {
    const cc_struct_def_t *sd;

    if (t != CC_TYPE_VOID || sid < 0 || (size_t)sid >= tu->struct_count) {
        return 0;
    }
    sd = &tu->structs[sid];
    if (sd->vector_count <= 0) {
        return 0;
    }
    v->n = sd->vector_count;
    v->elem = sd->vector_elem;
    v->es = type_size_bytes(v->elem);
    v->is_fp = v->elem == CC_TYPE_FLOAT || v->elem == CC_TYPE_DOUBLE;
    v->is_unsigned = cc_type_is_unsigned_integral(v->elem);
    return 1;
}

/** Element @p i of the vector of shape @p v at @p base: a double for a floating one, else an integer. */
static int vec_ld(cc_ssa_function_t *sf, int base, long i, const vec_t *v, cc_diag_t *diag) {
    cc_ssa_instr_t ld;
    int p = (base < 0 || i == 0) ? base : complex_part_addr(sf, base, i * v->es, diag);

    if (p < 0) {
        return -1;
    }
    memset(&ld, 0, sizeof(ld));
    ld.op = CC_SSA_LOAD;
    ld.dst = new_value(sf, v->is_fp ? CC_VAL_F64 : CC_VAL_I64);
    ld.lhs = p;
    ld.rhs = -1;
    ld.imm = v->es;
    ld.is_unsigned = v->is_unsigned;
    if (ld.dst < 0 || push_instr(sf, ld) != 0) {
        set_diag(diag, "out of memory loading a vector element");
        return -1;
    }
    if (!v->is_fp) {
        set_value_size(sf, ld.dst, v->es == 8 ? 8 : 4);
    }
    return ld.dst;
}

/** Store @p val as element @p i (@p es bytes) of the vector at @p base. */
static int vec_st(cc_ssa_function_t *sf, int base, long i, long es, int val, cc_diag_t *diag) {
    cc_ssa_instr_t st;
    int p = (base < 0 || i == 0) ? base : complex_part_addr(sf, base, i * es, diag);

    if (p < 0 || val < 0) {
        return -1;
    }
    memset(&st, 0, sizeof(st));
    st.op = CC_SSA_STORE;
    st.dst = -1;
    st.lhs = p;
    st.rhs = val;
    st.imm = es;
    if (push_instr(sf, st) != 0) {
        set_diag(diag, "out of memory storing a vector element");
        return -1;
    }
    return 0;
}

/** @p a @p op @p b on two integer elements of shape @p v (`%` as `a - a / b * b`). */
static int vec_int_op(cc_ssa_function_t *sf, cc_binop_t op, int a, int b, const vec_t *v, cc_diag_t *diag) {
    static const struct {
        cc_binop_t op;
        cc_ssa_opcode_t ssa;
    } map[] = {{CC_BIN_ADD, CC_SSA_ADD}, {CC_BIN_SUB, CC_SSA_SUB}, {CC_BIN_MUL, CC_SSA_MUL},
               {CC_BIN_DIV, CC_SSA_DIV}, {CC_BIN_BAND, CC_SSA_AND}, {CC_BIN_BOR, CC_SSA_OR},
               {CC_BIN_BXOR, CC_SSA_XOR}, {CC_BIN_SHL, CC_SSA_SHL}, {CC_BIN_SHR, CC_SSA_SHR}};
    long vs = v->es == 8 ? 8 : 4;
    int r = -1;
    size_t k;

    if (a < 0 || b < 0) {
        return -1;
    }
    if (op == CC_BIN_MOD) {
        int q = vec_int_op(sf, CC_BIN_DIV, a, b, v, diag);
        int m = vec_int_op(sf, CC_BIN_MUL, q, b, v, diag);

        return vec_int_op(sf, CC_BIN_SUB, a, m, v, diag);
    }
    for (k = 0; k < sizeof(map) / sizeof(map[0]); ++k) {
        if (map[k].op == op) {
            r = emit_i64_binop_instr(sf, map[k].ssa, a, b, v->is_unsigned, diag);
            break;
        }
    }
    if (k == sizeof(map) / sizeof(map[0])) {
        set_diag(diag, "invalid vector operation in lowering");
        return -1;
    }
    if (r >= 0) {
        set_value_size(sf, r, vs);
    }
    return r;
}

/** The comparison kind of comparison operator @p op. */
static cc_cmp_kind_t vec_cmp_kind(cc_binop_t op) {
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
    default:
        return CC_CMP_GE;
    }
}

/**
 * A vector operand of an element-wise operation: a vector's address
 * (*@p is_vec 1), or a scalar's value (0), already of the element type,
 * which every element is then combined with.
 */
static int vec_operand(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                       var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, int *is_vec,
                       cc_diag_t *diag) {
    vec_t dummy;
    int v = lower_expr(tu, sf, ctx, vars, var_count, depth, e, diag);

    *is_vec = vec_of(tu, CC_TYPE_VOID, lower_record_id(e), &dummy);
    if (v >= 0 && !*is_vec && value_type(sf, v) == CC_VAL_I64 && value_size(sf, v) < 4) {
        set_value_size(sf, v, 4);
    }
    return v;
}

/**
 * A binary operator on vectors (GCC's vector extension), element by
 * element into a new temporary: arithmetic in the element type (floating
 * elements rounded to it), a scalar operand used for every element, and a
 * comparison giving -1 or 0 in signed integers of the element's size.
 */
static int lower_vector_binop(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                              var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    vec_t v;
    int lv;
    int rv;
    int a;
    int b;
    int r;
    long i;
    int is_cmp = e->op == CC_BIN_EQ || e->op == CC_BIN_NE || e->op == CC_BIN_LT || e->op == CC_BIN_LE ||
                 e->op == CC_BIN_GT || e->op == CC_BIN_GE;

    if (!vec_of(tu, CC_TYPE_VOID, lower_record_id(e->lhs), &v) &&
        !vec_of(tu, CC_TYPE_VOID, lower_record_id(e->rhs), &v)) {
        set_diag(diag, "malformed vector operation in lowering");
        return -1;
    }
    a = vec_operand(tu, sf, ctx, vars, var_count, depth, e->lhs, &lv, diag);
    b = a < 0 ? -1 : vec_operand(tu, sf, ctx, vars, var_count, depth, e->rhs, &rv, diag);
    r = b < 0 ? -1 : emit_local_storage_alloc(sf, v.n * v.es, diag);
    for (i = 0; r >= 0 && i < v.n; ++i) {
        int x = lv ? vec_ld(sf, a, i, &v, diag) : a;
        int y = rv ? vec_ld(sf, b, i, &v, diag) : b;
        int val;

        if (x < 0 || y < 0) {
            return -1;
        }
        if (is_cmp) {
            /* 1 or 0, made -1 or 0 and widened to the element's size */
            int c = emit_cmp_instr(sf, vec_cmp_kind(e->op), x, y, v.is_unsigned, diag);
            int z = c < 0 ? -1 : emit_const_i64_instr(sf, 0);

            if (z >= 0) {
                set_value_size(sf, c, 4);
                set_value_size(sf, z, 4);
            }
            val = z < 0 ? -1 : emit_i64_binop_instr(sf, CC_SSA_SUB, z, c, 0, diag);
            if (val >= 0) {
                set_value_size(sf, val, 4);
                if (v.es == 8) {
                    val = normalize_integral_value(sf, val, CC_TYPE_LONG_LONG, diag);
                }
            }
        } else if (v.is_fp) {
            static const cc_ssa_opcode_t fops[] = {CC_SSA_ADD, CC_SSA_SUB, CC_SSA_MUL, CC_SSA_DIV};
            int k = e->op == CC_BIN_ADD ? 0 : e->op == CC_BIN_SUB ? 1 : e->op == CC_BIN_MUL ? 2 : 3;

            val = complex_fop(sf, fops[k], x, y, v.elem, diag);
        } else {
            val = vec_int_op(sf, e->op, x, y, &v, diag);
        }
        if (vec_st(sf, r, i, v.es, val, diag) != 0) {
            return -1;
        }
    }
    return r;
}

/**
 * A conversion to or from a vector type (CC_EXPR_CAST), keeping the bits:
 * a vector copied into a new temporary of the target type, an integer of
 * its size stored into one, or a vector read as an integer.
 */
static int lower_vector_cast(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                             var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    vec_t vt;
    vec_t vs;
    const cc_ctype_t *to = lower_cast_ctype(e);
    int to_vec = vec_of(tu, CC_TYPE_VOID, ct_record_id(to), &vt);
    int from_vec = vec_of(tu, CC_TYPE_VOID, lower_record_id(e->lhs), &vs);
    int src = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
    cc_ssa_instr_t ld;
    int r;

    if (src < 0) {
        return -1;
    }
    if (to == NULL || to->kind == CC_CT_VOID) {
        return src;
    }
    if (to_vec && from_vec) {
        r = emit_local_storage_alloc(sf, vt.n * vt.es, diag);
        return (r < 0 || emit_memcpy_instr(sf, r, src, vt.n * vt.es, diag) != 0) ? -1 : r;
    }
    if (to_vec) {
        r = emit_local_storage_alloc(sf, vt.n * vt.es, diag);
        return (r < 0 || vec_st(sf, r, 0, vt.n * vt.es, src, diag) != 0) ? -1 : r;
    }
    memset(&ld, 0, sizeof(ld));
    ld.op = CC_SSA_LOAD;
    ld.dst = new_value(sf, CC_VAL_I64);
    ld.lhs = src;
    ld.rhs = -1;
    ld.imm = vs.n * vs.es;
    ld.is_unsigned = lower_is_unsigned(e);
    if (ld.dst < 0 || push_instr(sf, ld) != 0) {
        set_diag(diag, "out of memory converting a vector");
        return -1;
    }
    set_value_size(sf, ld.dst, vs.n * vs.es >= 8 ? 8 : 4);
    return ld.dst;
}

/** The width of the decimal floating type (@p t, @p sid): 32, 64 or 128; 0 if it is not one. */
static int dec_bits_of(const cc_translation_unit_t *tu, cc_type_t t, int sid) {
    if (t != CC_TYPE_VOID || sid < 0 || (size_t)sid >= tu->struct_count) {
        return 0;
    }
    return tu->structs[sid].decimal_bits;
}

/** libgcc's mode suffix of a decimal type: sd, dd or td. */
static const char *dec_mode(int bits) {
    return bits == 32 ? "sd" : bits == 64 ? "dd" : "td";
}

/** How a dec_libcall() argument or result travels. */
typedef enum {
    DEC_ARG_DEC,    /**< a decimal value, by the address of its storage */
    DEC_ARG_INT,    /**< a 4-byte integer */
    DEC_ARG_LLONG,  /**< an 8-byte integer */
    DEC_ARG_FLOAT,  /**< a float (held as a double) */
    DEC_ARG_DOUBLE, /**< a double */
    DEC_ARG_LDOUBLE /**< a long double */
} dec_arg_t;

/**
 * A call to libgcc's decimal helper @p name: arguments @p args of kinds
 * @p kinds (a decimal's of type @p arg_sids), passed as gcc passes those
 * types -- a decimal in an xmm register on x86-64, on i386 a _Decimal32
 * as an int, the others in memory (_Decimal128 16-aligned) -- and a
 * result of kind @p ret, a decimal one of structure @p ret_sid in new
 * storage (%xmm0 on x86-64; %eax, %eax:%edx or a hidden pointer on i386).
 *
 * @return the result: a decimal's address, else its value; -1 on error
 */
static int dec_libcall(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const char *name, const int *args,
                       const dec_arg_t *kinds, const int *arg_sids, size_t nargs, dec_arg_t ret, int ret_sid,
                       cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int is64 = cc_target_pointer_size() == 8;
    size_t i;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CALL;
    in.sym = cc_strdup(name);
    in.arg_count = nargs;
    in.args = (int *)calloc(nargs, sizeof(*in.args));
    in.call_arg_abi = (unsigned char *)calloc(nargs, sizeof(*in.call_arg_abi));
    in.call_arg_sizes = (uint32_t *)calloc(nargs, sizeof(*in.call_arg_sizes));
    in.rhs = -1;
    if (in.sym == NULL || in.args == NULL || in.call_arg_abi == NULL || in.call_arg_sizes == NULL) {
        free(in.sym);
        free(in.args);
        free(in.call_arg_abi);
        free(in.call_arg_sizes);
        set_diag(diag, "out of memory emitting a decimal helper call");
        return -1;
    }
    for (i = 0; i < nargs; ++i) {
        int v = args[i];
        unsigned char abi = CC_CALL_ARG_ABI_DEFAULT;
        uint32_t size = 0;

        switch (kinds[i]) {
        case DEC_ARG_DEC: {
            long dsize = tu->structs[arg_sids[i]].decimal_bits / 8;

            size = (uint32_t)dsize;
            if (is64) {
                abi = (unsigned char)(CC_CALL_ARG_ABI_AGG_REG | abi64_aggregate_class(tu, arg_sids[i], dsize, 0));
            } else if (dsize == 4) {
                cc_ssa_instr_t ld;

                memset(&ld, 0, sizeof(ld));
                ld.op = CC_SSA_LOAD;
                ld.dst = new_value(sf, CC_VAL_I64);
                ld.lhs = v;
                ld.rhs = -1;
                ld.imm = 4;
                ld.is_unsigned = 1;
                if (ld.dst < 0 || push_instr(sf, ld) != 0) {
                    v = -1;
                } else {
                    set_value_size(sf, ld.dst, 4);
                    v = ld.dst;
                }
            } else {
                abi = (unsigned char)(CC_CALL_ARG_ABI_AGG_MEM | vector_mem_flag(tu, arg_sids[i], dsize));
            }
            break;
        }
        case DEC_ARG_INT:
            size = 4;
            break;
        case DEC_ARG_LLONG:
            size = 8;
            break;
        case DEC_ARG_FLOAT:
            abi = CC_CALL_ARG_ABI_FLOAT;
            size = 4;
            break;
        case DEC_ARG_DOUBLE:
            size = 8;
            break;
        case DEC_ARG_LDOUBLE:
            abi = CC_CALL_ARG_ABI_LDOUBLE;
            size = 12;
            break;
        }
        if (v < 0) {
            free(in.sym);
            free(in.args);
            free(in.call_arg_abi);
            free(in.call_arg_sizes);
            return -1;
        }
        in.args[i] = v;
        in.call_arg_abi[i] = abi;
        in.call_arg_sizes[i] = size;
    }
    if (ret == DEC_ARG_DEC) {
        long rsize = tu->structs[ret_sid].decimal_bits / 8;
        int storage = emit_local_storage_alloc(sf, rsize, diag);

        in.dst = storage;
        in.rhs = storage;
        in.imm = rsize;
        in.is_unsigned = 1;
        in.agg_class = agg_return_class(tu, ret_sid, rsize);
    } else if (ret == DEC_ARG_FLOAT || ret == DEC_ARG_DOUBLE || ret == DEC_ARG_LDOUBLE) {
        in.dst = new_value(sf, CC_VAL_F64);
        in.imm = 8;
        if (ret == DEC_ARG_FLOAT) {
            in.agg_class = CC_ABI_RET_FLOAT;
        }
        if (ret == DEC_ARG_LDOUBLE) {
            in.call_ret_x87 = 1;
            in.imm = 16;
        }
    } else {
        in.dst = new_value(sf, CC_VAL_I64);
        in.imm = ret == DEC_ARG_LLONG ? 8 : 4;
    }
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        free(in.sym);
        free(in.args);
        free(in.call_arg_abi);
        free(in.call_arg_sizes);
        set_diag(diag, "out of memory emitting a decimal helper call");
        return -1;
    }
    if (ret == DEC_ARG_LDOUBLE) {
        set_value_size(sf, in.dst, 16);
    } else if (ret == DEC_ARG_INT) {
        set_value_size(sf, in.dst, 4);
    } else if (ret == DEC_ARG_LLONG) {
        set_value_size(sf, in.dst, 8);
    }
    return in.dst;
}

/**
 * A binary operator on decimal operands of one type (sema converts
 * them): `+ - * /` by __bid_{add,sub,mul,div}<mode>3, a comparison by the
 * matching __bid_<cmp><mode>2, whose result compares with 0 as the
 * operator does (libgcc's convention, unordered operands failing).
 */
static int lower_decimal_binop(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                               var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    static const struct {
        cc_binop_t op;
        const char *name;
        int arity;
    } ops[] = {{CC_BIN_ADD, "add", 3}, {CC_BIN_SUB, "sub", 3}, {CC_BIN_MUL, "mul", 3}, {CC_BIN_DIV, "div", 3},
               {CC_BIN_EQ, "eq", 2},   {CC_BIN_NE, "ne", 2},   {CC_BIN_LT, "lt", 2},   {CC_BIN_LE, "le", 2},
               {CC_BIN_GT, "gt", 2},   {CC_BIN_GE, "ge", 2}};
    int sid = lower_record_id(e->lhs);
    int bits = dec_bits_of(tu, CC_TYPE_VOID, sid);
    dec_arg_t kinds[2] = {DEC_ARG_DEC, DEC_ARG_DEC};
    int sids[2];
    int args[2];
    char name[32];
    size_t k;

    for (k = 0; k < sizeof(ops) / sizeof(ops[0]) && ops[k].op != e->op; ++k) {
    }
    if (bits == 0 || k == sizeof(ops) / sizeof(ops[0])) {
        set_diag(diag, "malformed decimal floating operation in lowering");
        return -1;
    }
    sids[0] = sid;
    sids[1] = sid;
    args[0] = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
    args[1] = args[0] < 0 ? -1 : lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
    if (args[1] < 0) {
        return -1;
    }
    snprintf(name, sizeof(name), "__bid_%s%s%d", ops[k].name, dec_mode(bits), ops[k].arity);
    if (ops[k].arity == 3) {
        return dec_libcall(tu, sf, name, args, kinds, sids, 2, DEC_ARG_DEC, sid, diag);
    }
    {
        int r = dec_libcall(tu, sf, name, args, kinds, sids, 2, DEC_ARG_INT, -1, diag);
        int z = r < 0 ? -1 : emit_const_i64_instr(sf, 0);
        int c;

        if (z < 0) {
            return -1;
        }
        set_value_size(sf, z, 4);
        c = emit_cmp_instr(sf, vec_cmp_kind(e->op), r, z, 0, diag);
        if (c >= 0) {
            set_value_size(sf, c, 4);
        }
        return c;
    }
}

/**
 * A conversion to or from a decimal floating type (CC_EXPR_CAST), by
 * libgcc: between decimal types __bid_extend/trunc<from><to>2, from an
 * integer __bid_float[uns]{si,di}<mode>, to one __bid_fix[uns]<mode>{si,di},
 * and to or from a binary floating type __bid_extend/trunc (sf, df, xf).
 */
static int lower_decimal_cast(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                              var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    int to_sid = ct_record_id(lower_cast_ctype(e));
    int to = dec_bits_of(tu, CC_TYPE_VOID, to_sid);
    int from = dec_bits_of(tu, CC_TYPE_VOID, lower_record_id(e->lhs));
    const cc_ctype_t *st = lower_value_ctype(e->lhs);
    const cc_ctype_t *dt = lower_value_ctype(e);
    cc_ctype_kind_t sk = st != NULL ? st->kind : CC_CT_VOID;
    cc_ctype_kind_t dk = dt != NULL ? dt->kind : CC_CT_VOID;
    int src = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
    dec_arg_t kind;
    int sids[1];
    char name[40];

    if (src < 0) {
        return -1;
    }
    if (dk == CC_CT_VOID) {
        return src;
    }
    if (to != 0 && from != 0) {
        if (to == from) {
            return src;
        }
        snprintf(name, sizeof(name), "__bid_%s%s%s2", to > from ? "extend" : "trunc", dec_mode(from), dec_mode(to));
        sids[0] = lower_record_id(e->lhs);
        kind = DEC_ARG_DEC;
        return dec_libcall(tu, sf, name, &src, &kind, sids, 1, DEC_ARG_DEC, to_sid, diag);
    }
    if (to != 0) {
        /* to a decimal type: from an integer or a binary floating type */
        sids[0] = -1;
        if (sk == CC_CT_FLOAT || sk == CC_CT_DOUBLE || sk == CC_CT_LDOUBLE) {
            const char *bm = sk == CC_CT_FLOAT ? "sf" : sk == CC_CT_DOUBLE ? "df" : "xf";
            int bw = sk == CC_CT_FLOAT ? 32 : sk == CC_CT_DOUBLE ? 64 : 80;

            kind = sk == CC_CT_FLOAT ? DEC_ARG_FLOAT : sk == CC_CT_DOUBLE ? DEC_ARG_DOUBLE : DEC_ARG_LDOUBLE;
            snprintf(name, sizeof(name), "__bid_%s%s%s", bw <= to ? "extend" : "trunc", bm, dec_mode(to));
        } else {
            int wide = cc_ct_int_width(st) > 32;
            int uns = lower_is_unsigned(e->lhs);

            src = cast_value(sf, src, CC_VAL_I64, diag);
            src = src < 0 ? -1 : normalize_integral_value(sf, src, wide ? (uns ? CC_TYPE_ULONG_LONG : CC_TYPE_LONG_LONG)
                                                                        : (uns ? CC_TYPE_UINT : CC_TYPE_INT),
                                                       diag);
            if (src < 0) {
                return -1;
            }
            set_value_size(sf, src, wide ? 8 : 4);
            kind = wide ? DEC_ARG_LLONG : DEC_ARG_INT;
            snprintf(name, sizeof(name), "__bid_float%s%s%s", uns ? "uns" : "", wide ? "di" : "si", dec_mode(to));
        }
        return dec_libcall(tu, sf, name, &src, &kind, sids, 1, DEC_ARG_DEC, to_sid, diag);
    }
    /* from a decimal type: to an integer or a binary floating type */
    sids[0] = lower_record_id(e->lhs);
    kind = DEC_ARG_DEC;
    if (dk == CC_CT_FLOAT || dk == CC_CT_DOUBLE || dk == CC_CT_LDOUBLE) {
        const char *bm = dk == CC_CT_FLOAT ? "sf" : dk == CC_CT_DOUBLE ? "df" : "xf";
        int bw = dk == CC_CT_FLOAT ? 32 : dk == CC_CT_DOUBLE ? 64 : 80;
        dec_arg_t rk = dk == CC_CT_FLOAT ? DEC_ARG_FLOAT : dk == CC_CT_DOUBLE ? DEC_ARG_DOUBLE : DEC_ARG_LDOUBLE;
        int r;

        /* libgcc's names: trunc unless the binary type is the wider (sd to df and xf, dd to xf) */
        snprintf(name, sizeof(name), "__bid_%s%s%s", bw <= from ? "trunc" : "extend", dec_mode(from), bm);
        r = dec_libcall(tu, sf, name, &src, &kind, sids, 1, rk, -1, diag);
        if (r >= 0 && dk == CC_CT_FLOAT) {
            r = normalize_float_value(sf, r, CC_TYPE_FLOAT, diag);
        }
        return r;
    }
    {
        int wide = cc_ct_int_width(dt) > 32;
        int uns = lower_is_unsigned(e);
        int r;

        snprintf(name, sizeof(name), "__bid_fix%s%s%s", uns ? "uns" : "", dec_mode(from), wide ? "di" : "si");
        r = dec_libcall(tu, sf, name, &src, &kind, sids, 1, wide ? DEC_ARG_LLONG : DEC_ARG_INT, -1, diag);
        return r < 0 ? -1 : normalize_int_ct(sf, r, dt, diag);
    }
}

/**
 * A conversion to or from a wide integer type (CC_EXPR_CAST): between wide
 * types limb by limb, from a narrower integer through its long long value,
 * to one by its low 64 bits, and to or from a floating type by libgcc's
 * __floatbitint* and __fix*bitint.
 */
static int lower_wide_cast(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                           var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    wide_t wd;
    wide_t ws;
    const cc_ctype_t *tct = lower_cast_ctype(e);
    int to_wide = wide_of(tu, CC_TYPE_VOID, ct_record_id(tct), &wd);
    int from_wide = wide_of(tu, CC_TYPE_VOID, lower_record_id(e->lhs), &ws);
    const cc_ctype_t *sct = lower_value_ctype(e->lhs);
    cc_ctype_kind_t st = sct != NULL ? sct->kind : CC_CT_VOID;
    long i;

    if (to_wide && from_wide) {
        int src = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        int r = src < 0 ? -1 : wide_new(sf, &wd, diag);
        int fill = r < 0 ? -1 : wide_fill(sf, src, &ws, diag);

        for (i = 0; fill >= 0 && i < wd.n; ++i) {
            if (wide_st(sf, r, i, i < ws.n ? wide_ld(sf, src, i, &ws, diag) : fill, &wd, diag) != 0) {
                return -1;
            }
        }
        return (fill < 0 || wide_fix_top(sf, r, &wd, diag) != 0) ? -1 : r;
    }
    if (to_wide && (st == CC_CT_FLOAT || st == CC_CT_DOUBLE || st == CC_CT_LDOUBLE)) {
        int r = wide_new(sf, &wd, diag);
        int v = r < 0 ? -1 : lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        int args[3];
        unsigned char abi[3] = {CC_CALL_ARG_ABI_DEFAULT, CC_CALL_ARG_ABI_DEFAULT, CC_CALL_ARG_ABI_DEFAULT};

        if (v < 0) {
            return -1;
        }
        if (st == CC_CT_LDOUBLE) {
            v = promote_f64_to_ldouble_value(sf, v, diag);
            abi[2] = CC_CALL_ARG_ABI_LDOUBLE;
        } else {
            v = demote_ldouble_to_f64_value(sf, v, diag);
            abi[2] = st == CC_CT_FLOAT ? CC_CALL_ARG_ABI_FLOAT : CC_CALL_ARG_ABI_DEFAULT;
        }
        args[0] = r;
        args[1] = wide_int_arg(sf, wide_prec(&wd));
        args[2] = v;
        if (wide_libcall(sf, st == CC_CT_FLOAT ? "__fixsfbitint" : st == CC_CT_DOUBLE ? "__fixdfbitint"
                                                                                       : "__fixxfbitint",
                         args, 3, abi, -1, 0, 0, diag) < 0) {
            return -1;
        }
        return wide_fix_top(sf, r, &wd, diag) != 0 ? -1 : r;
    }
    if (to_wide) {
        /* an integer: its value as a (unsigned) long long, sign- or zero-extended into the limbs */
        int is_uns = cc_ct_is_unsigned(sct);
        cc_expr_t c;
        int v;
        int fill;
        int r;

        memset(&c, 0, sizeof(c));
        c.kind = CC_EXPR_CAST;
        c.aux_type = is_uns ? CC_TYPE_ULONG_LONG : CC_TYPE_LONG_LONG;
        c.aux_struct_id = -1;
        c.value_type = c.aux_type;
        c.struct_id = -1;
        c.ctype = cc_ct_basic(is_uns ? CC_CT_ULLONG : CC_CT_LLONG);
        c.lhs = (cc_expr_t *)e->lhs;
        v = lower_expr(tu, sf, ctx, vars, var_count, depth, &c, diag);
        fill = v < 0 ? -1 : (is_uns ? emit_const_i64_instr(sf, 0)
                                    : emit_i64_binop_instr(sf, CC_SSA_SHR, v, emit_const_i64_instr(sf, 63), 0, diag));
        r = fill < 0 ? -1 : wide_new(sf, &wd, diag);
        if (r < 0) {
            return -1;
        }
        set_value_size(sf, fill, 8);
        for (i = 0; i < wd.n; ++i) {
            int piece;

            if (i * wd.limb * 8 >= 64) {
                piece = fill;
            } else if (i == 0) {
                piece = v;
            } else {
                piece = emit_i64_binop_instr(sf, CC_SSA_SHR, v, emit_const_i64_instr(sf, i * wd.limb * 8), is_uns, diag);
            }
            if (wide_st(sf, r, i, piece, &wd, diag) != 0) {
                return -1;
            }
        }
        return wide_fix_top(sf, r, &wd, diag) != 0 ? -1 : r;
    }
    /* from a wide integer */
    {
        cc_ctype_kind_t to = tct != NULL ? tct->kind : CC_CT_VOID;
        int src = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);

        if (src < 0) {
            return -1;
        }
        if (to == CC_CT_FLOAT || to == CC_CT_DOUBLE || to == CC_CT_LDOUBLE) {
            int args[2];
            int v;

            args[0] = src;
            args[1] = wide_int_arg(sf, wide_prec(&ws));
            v = wide_libcall(sf, to == CC_CT_FLOAT ? "__floatbitintsf" : to == CC_CT_DOUBLE ? "__floatbitintdf"
                                                                                             : "__floatbitintxf",
                             args, 2, NULL, CC_VAL_F64, to == CC_CT_FLOAT ? CC_ABI_RET_FLOAT : 0,
                             to == CC_CT_LDOUBLE, diag);
            return v;
        }
        if (to == CC_CT_VOID) {
            return src;
        }
        {
            /* an integer: the low 64 bits, then the target type's own conversion */
            cc_ssa_instr_t ld;

            memset(&ld, 0, sizeof(ld));
            ld.op = CC_SSA_LOAD;
            ld.dst = new_value(sf, CC_VAL_I64);
            ld.lhs = src;
            ld.rhs = -1;
            ld.imm = 8;
            ld.is_unsigned = 1;
            if (ld.dst < 0 || push_instr(sf, ld) != 0) {
                set_diag(diag, "out of memory converting a wide integer");
                return -1;
            }
            set_value_size(sf, ld.dst, 8);
            return normalize_int_ct(sf, cast_value(sf, ld.dst, ct_value_kind(tct), diag), tct, diag);
        }
    }
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
        in.is_unsigned = lower_is_unsigned(e) ? 1 : 0;
        set_value_size(sf, in.dst, lower_value_size(tu, e));
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;

    case CC_EXPR_FLOAT:
        in.op = CC_SSA_CONST;
        in.dst = new_value(sf, CC_VAL_F64);
        in.fimm = e->float_val;
        in.fimm_ld = e->float_val_ld;
        if (lower_has_kind(e, CC_CT_LDOUBLE)) {
            set_value_size(sf, in.dst, 16);
        }
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;

    case CC_EXPR_STR:
        in.op = CC_SSA_STR;
        in.dst = new_value(sf, CC_VAL_I64);
        in.sym = string_literal_emit_text(tu, e);
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
            int expr_is_array_obj = (e->array_ndim > 0);
            int expr_is_struct_obj = (lower_record_id(e) >= 0);
            if (e->ident != NULL &&
                (strcmp(e->ident, "__func__") == 0 || strcmp(e->ident, "__FUNCTION__") == 0 ||
                 strcmp(e->ident, "__PRETTY_FUNCTION__") == 0)) {
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
                /*
                 * Use expression-level array metadata first. This keeps
                 * lowering consistent with semantic analysis when a symbol has
                 * multiple declarations and only some carry explicit array
                 * bounds (e.g. typedef-backed extern declarations).
                 */
                if (expr_is_array_obj ||
                    (g->array_ndim > 0 && g->array_len >= 0) ||
                    is_array_object_decl(g->type, g->array_len, g->array_ndim)) {
                    return emit_global_addr(sf, g->name, diag);
                }
                if (expr_is_struct_obj || (g->type == CC_TYPE_VOID && g->type_struct_id >= 0)) {
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
                in.dst = new_value(sf, lower_value_kind(e));
                in.lhs = gaddr;
                in.rhs = -1;
                in.imm = mem_size;
                in.is_unsigned = lower_loads_unsigned(e) ? 1 : 0;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    return -1;
                }
                return in.dst;
            }
            fn = find_fn(tu, e->ident);
            if (cc_nested_uses_outer(tu, cc_find_function_def(tu, e->ident))) {
                return lower_nested_trampoline(tu, sf, ctx, vars, var_count, cc_find_function_def(tu, e->ident),
                                               diag);
            }
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
            in.dst = new_value(sf, lower_value_kind(e));
            in.lhs = saddr;
            in.rhs = -1;
            in.imm = mem_size;
            in.is_unsigned = lower_loads_unsigned(e) ? 1 : 0;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                return -1;
            }
            return in.dst;
        }
        if (vars[idx].addr_value >= 0 && !var_is_aggregate_or_array(tu, &vars[idx])) {
            long mem_size = vars[idx].addr_size > 0 ? vars[idx].addr_size : var_object_size_bytes(tu, &vars[idx]);
            if (mem_size <= 0) {
                set_diag(diag, "unsupported address-taken local object type in lowering");
                return -1;
            }
            in.op = CC_SSA_LOAD;
            in.dst = new_value(sf, lower_value_kind(e));
            in.lhs = vars[idx].addr_value;
            in.rhs = -1;
            in.imm = mem_size;
            in.is_unsigned = lower_loads_unsigned(e) ? 1 : 0;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory loading address-taken local");
                return -1;
            }
            return in.dst;
        }
        if (vars[idx].type == CC_TYPE_LDOUBLE || lower_has_kind(e, CC_CT_LDOUBLE)) {
            set_value_size(sf, vars[idx].value, 16);
        }
        return vars[idx].value;
    }

    case CC_EXPR_ADDR: {
        int idx;
        if (e->lhs == NULL) {
            set_diag(diag, "malformed address-of expression in lowering");
            return -1;
        }
        if (e->lhs->kind == CC_EXPR_CAST && lower_record_id(e->lhs) >= 0) {
            return lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        }
        if (e->lhs->kind == CC_EXPR_CAST) {
            /*
             * A compound literal, the only cast sema lets `&` apply to (a
             * scalar one checked into a cast of its value): an array's value
             * is its storage; a scalar's is put in some.
             */
            const cc_ctype_t *lt = lower_cast_ctype(e->lhs);
            long sz = cc_ct_size(lt, tu);
            int v = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
            int p;

            if (v < 0 || (lt != NULL && lt->kind == CC_CT_ARRAY)) {
                return v;
            }
            if (sz <= 0) {
                set_diag(diag, "compound literal of unknown size in address-of lowering");
                return -1;
            }
            p = emit_local_storage_alloc(sf, sz, diag);
            if (p < 0) {
                return -1;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_STORE;
            in.dst = -1;
            in.lhs = p;
            in.rhs = v;
            in.imm = sz;
            if (push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory storing a compound literal");
                return -1;
            }
            return p;
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
                if (cc_nested_uses_outer(tu, cc_find_function_def(tu, e->lhs->ident))) {
                    return lower_nested_trampoline(tu, sf, ctx, vars, var_count,
                                                   cc_find_function_def(tu, e->lhs->ident), diag);
                }
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
        if (ensure_local_var_addressable(tu, sf, &vars[idx], diag) < 0) {
            return -1;
        }
        return cast_value(sf, vars[idx].addr_value, CC_VAL_I64, diag);
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
        if (cc_ct_expr_designates_function(e->lhs)) {
            /* `*fp` is the function fp points to; as a value it is fp again */
            return lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        }
        if (deref_operand_is_cast(e->lhs)) {
            /*
             * `*(T)p` or `((T)p)[i]`, T a pointer to an array: the result is
             * that array, and as a value its address -- the operand again.
             * Only through a cast: elsewhere the legacy array bounds carry
             * this, and the paths that read them take the address themselves.
             */
            const cc_ctype_t *t = cc_ct_of_expr(e);
            if (t != NULL && t->kind == CC_CT_ARRAY) {
                return lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
            }
        }
        if (lower_record_id(e) >= 0) {
            lhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
            if (lhs < 0) {
                return -1;
            }
            return cast_value(sf, lhs, CC_VAL_I64, diag);
        }
        if (lower_is_pointer(e) && e->array_ndim > 0) {
            lhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
            if (lhs < 0) {
                return -1;
            }
            return cast_value(sf, lhs, CC_VAL_I64, diag);
        }
        long mem_size = lower_value_size(tu, e);
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
        in.dst = new_value(sf, lower_value_kind(e));
        in.lhs = lhs;
        in.rhs = -1;
        in.imm = mem_size;
        in.is_unsigned = lower_loads_unsigned(e) ? 1 : 0;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;
    }

    case CC_EXPR_MEMBER: {
        if (e->lhs == NULL && e->rhs != NULL && e->ident != NULL) {
            return lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
        }
        if (lower_record_id(e) >= 0) {
            return lower_member_addr(tu, sf, ctx, vars, var_count, depth, e, diag);
        }
        if (e->member_is_bitfield) {
            int ptrv = lower_member_addr(tu, sf, ctx, vars, var_count, depth, e, diag);
            if (ptrv < 0) {
                return -1;
            }
            return lower_bitfield_load_from_addr(sf, e, ptrv, diag);
        }
        long mem_size = lower_value_size(tu, e);
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
        in.dst = new_value(sf, lower_value_kind(e));
        in.lhs = ptrv;
        in.rhs = -1;
        in.imm = mem_size;
        in.is_unsigned = lower_loads_unsigned(e) ? 1 : 0;
        if (in.dst < 0 || push_instr(sf, in) != 0) {
            return -1;
        }
        return in.dst;
    }

    case CC_EXPR_COMPLEX: {
        /* sema converted both parts to the element type */
        cc_type_t elem = complex_elem_of(tu, CC_TYPE_VOID, lower_record_id(e));
        int re = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        int im = re < 0 ? -1 : lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);

        if (im < 0 || elem == CC_TYPE_VOID) {
            if (diag != NULL && diag->message[0] == '\0') {
                set_diag(diag, "malformed complex value in lowering");
            }
            return -1;
        }
        return complex_store_new(sf, re, im, elem, diag);
    }

    case CC_EXPR_BIN: {
        cc_value_type_t vt;

        if (e->op != CC_BIN_COMMA && !is_logical_op(e->op) && e->lhs != NULL) {
            wide_t ww;
            vec_t vv;

            if (vec_of(tu, CC_TYPE_VOID, lower_record_id(e), &vv) &&
                (vec_of(tu, CC_TYPE_VOID, lower_record_id(e->lhs), &vv) ||
                 (e->rhs != NULL && vec_of(tu, CC_TYPE_VOID, lower_record_id(e->rhs), &vv)))) {
                return lower_vector_binop(tu, sf, ctx, vars, var_count, depth, e, diag);
            }
            if (dec_bits_of(tu, CC_TYPE_VOID, lower_record_id(e->lhs)) != 0) {
                return lower_decimal_binop(tu, sf, ctx, vars, var_count, depth, e, diag);
            }
            if (wide_of(tu, CC_TYPE_VOID, lower_record_id(e->lhs), &ww)) {
                return lower_wide_binop(tu, sf, ctx, vars, var_count, depth, e, diag);
            }
        }
        if (e->op != CC_BIN_COMMA && !is_logical_op(e->op) &&
            (complex_elem_of(tu, CC_TYPE_VOID, lower_record_id(e)) != CC_TYPE_VOID ||
             (e->lhs != NULL && complex_elem_of(tu, CC_TYPE_VOID, lower_record_id(e->lhs)) != CC_TYPE_VOID) ||
             (e->rhs != NULL && complex_elem_of(tu, CC_TYPE_VOID, lower_record_id(e->rhs)) != CC_TYPE_VOID))) {
            return lower_complex_binop(tu, sf, ctx, vars, var_count, depth, e, diag);
        }
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

        if (e->op == CC_BIN_MUL && e->lhs != NULL && e->rhs != NULL && e->lhs->kind == CC_EXPR_INT &&
            e->lhs->int_val == -1 &&
            (lower_has_kind(e->rhs, CC_CT_FLOAT) || lower_has_kind(e->rhs, CC_CT_DOUBLE) ||
             lower_has_kind(e->rhs, CC_CT_LDOUBLE))) {
            cc_type_t store_t = fp_builtin_storage_type(e->rhs);
            rhs = lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
            if (rhs < 0) {
                return -1;
            }
            if (value_type(sf, rhs) != CC_VAL_F64) {
                rhs = cast_value(sf, rhs, CC_VAL_F64, diag);
                if (rhs < 0) {
                    return -1;
                }
            }
            if (store_t == CC_TYPE_LDOUBLE) {
                rhs = promote_f64_to_ldouble_value(sf, rhs, diag);
                if (rhs < 0) {
                    return -1;
                }
            }
            return emit_fp_negate_from_value(sf, rhs, store_t, diag);
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
            int cmp_ldouble = (lower_has_kind(e->lhs, CC_CT_LDOUBLE) || lower_has_kind(e->rhs, CC_CT_LDOUBLE));
            cc_value_type_t cmp_vt = (cmp_ldouble || value_type(sf, lhs) == CC_VAL_F64 || value_type(sf, rhs) == CC_VAL_F64)
                                         ? CC_VAL_F64
                                         : CC_VAL_I64;
            const cc_ctype_t *cmp_ct = cc_ct_basic(CC_CT_INT);
            lhs = cast_value(sf, lhs, cmp_vt, diag);
            rhs = cast_value(sf, rhs, cmp_vt, diag);
            if (lhs < 0 || rhs < 0) {
                return -1;
            }
            if (cmp_ldouble) {
                lhs = promote_f64_to_ldouble_value(sf, lhs, diag);
                rhs = promote_f64_to_ldouble_value(sf, rhs, diag);
                if (lhs < 0 || rhs < 0) {
                    return -1;
                }
            }
            if (cmp_vt == CC_VAL_I64) {
                /* pointers compare as the unsigned integer of their width; integers in their common type */
                if (lower_is_pointer(e->lhs) || lower_is_pointer(e->rhs)) {
                    cmp_ct = cc_ct_basic(cc_target_pointer_size() <= 4 ? CC_CT_UINT : CC_CT_ULLONG);
                } else {
                    cmp_ct = cc_ct_common_int(lower_value_ctype(e->lhs), lower_value_ctype(e->rhs));
                }
                lhs = normalize_int_ct(sf, lhs, cmp_ct, diag);
                rhs = normalize_int_ct(sf, rhs, cmp_ct, diag);
                if (lhs < 0 || rhs < 0) {
                    return -1;
                }
            }
            in.op = CC_SSA_CMP;
            in.cmp_kind = bin_to_cmp(e->op);
            in.is_unsigned =
                (cmp_vt == CC_VAL_I64 &&
                 (cc_ct_is_unsigned(cmp_ct) || lower_is_pointer(e->lhs) || lower_is_pointer(e->rhs)))
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

        if (e->op == CC_BIN_SUB && lower_is_pointer(e->lhs) && lower_is_pointer(e->rhs)) {
            long elem_size = lower_pointee_size(tu, e->lhs);
            int diffv;

            if (elem_size <= 0) {
                long lhs_step = expr_array_step_size_bytes(tu, vars, var_count, depth, e->lhs, -1);
                long rhs_step = expr_array_step_size_bytes(tu, vars, var_count, depth, e->rhs, -1);

                elem_size = lower_pointer_step(tu, e->lhs);
                if (lhs_step > 0) {
                    elem_size = lhs_step;
                } else if (rhs_step > 0) {
                    elem_size = rhs_step;
                }
            }

            lhs = cast_value(sf, lhs, CC_VAL_I64, diag);
            rhs = cast_value(sf, rhs, CC_VAL_I64, diag);
            if (lhs < 0 || rhs < 0) {
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

            {
                /* into a variable-length array: the element size is a stride computed at its declaration */
                int level;
                int vidx = vla_subscript_level(vars, var_count, depth, e->lhs, &level);

                if (vidx < 0) {
                    vidx = vla_subscript_level(vars, var_count, depth, e->rhs, &level);
                }
                if (vidx >= 0 && level < vars[vidx].vla_rank) {
                    diffv = emit_i64_binop_instr(sf, CC_SSA_DIV, diffv, vars[vidx].vla_strides[level], 0, diag);
                    if (diffv < 0) {
                        return -1;
                    }
                    return cast_value(sf, diffv, lower_value_kind(e), diag);
                }
            }

            /* otherwise the element size is a constant */
            if (elem_size <= 0) {
                set_diag(diag, "unsupported pointer base type in subtraction lowering");
                return -1;
            }
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

            return cast_value(sf, diffv, lower_value_kind(e), diag);
        }

        if ((e->op == CC_BIN_ADD || e->op == CC_BIN_SUB) && lower_is_pointer(e)) {
            int ptrv = -1;
            int idxv = -1;
            long elem_size;
            cc_ssa_instr_t mul;
            const cc_expr_t *ptr_expr = NULL;

            if (lower_is_pointer(e->lhs)) {
                ptrv = lhs;
                idxv = rhs;
                ptr_expr = e->lhs;
            } else if (e->op == CC_BIN_ADD && lower_is_pointer(e->rhs)) {
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
                lower_is_pointer(ptr_expr) && ptr_expr->array_ndim > 0 &&
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

            {
                /* into a variable-length array: the step is a stride computed at its declaration */
                int level;
                int vidx = vla_subscript_level(vars, var_count, depth, ptr_expr, &level);

                if (vidx >= 0 && level < vars[vidx].vla_rank) {
                    idxv = emit_i64_binop_instr(sf, CC_SSA_MUL, idxv, vars[vidx].vla_strides[level], 0, diag);
                    if (idxv < 0) {
                        return -1;
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
            }

            elem_size = expr_real_step_size_bytes(tu, ptr_expr);
            if (elem_size <= 0) {
                elem_size = lower_pointee_size(tu, e);
            }
            if (elem_size <= 0) {
                elem_size = lower_pointer_step(tu, e);
                elem_size = expr_array_step_size_bytes(tu, vars, var_count, depth, ptr_expr, elem_size);
                elem_size = expr_decl_array_step_size_bytes(tu, vars, var_count, depth, ptr_expr, elem_size);
            }
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

        vt = lower_value_kind(e);
        lhs = cast_value(sf, lhs, vt, diag);
        rhs = cast_value(sf, rhs, vt, diag);
        if (lhs < 0 || rhs < 0) {
            return -1;
        }
        if (lower_has_kind(e, CC_CT_LDOUBLE)) {
            lhs = promote_f64_to_ldouble_value(sf, lhs, diag);
            rhs = promote_f64_to_ldouble_value(sf, rhs, diag);
            if (lhs < 0 || rhs < 0) {
                return -1;
            }
        }
        if (vt == CC_VAL_I64 && lower_is_integer(e) && !lower_is_pointer(e)) {
            lhs = normalize_int_ct(sf, lhs, lower_value_ctype(e), diag);
            rhs = normalize_int_ct(sf, rhs, lower_value_ctype(e), diag);
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
            in.is_unsigned = lower_is_unsigned(e) ? 1 : 0;
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
            in.is_unsigned = lower_is_unsigned(e) ? 1 : 0;
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
            q.is_unsigned = lower_is_unsigned(e) ? 1 : 0;
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
            if (cc_ct_int_width(lower_value_ctype(e)) >= 64) {
                set_value_size(sf, q.dst, 8);
                set_value_size(sf, p.dst, 8);
                set_value_size(sf, r.dst, 8);
            }
            if (lower_is_integer(e) && !lower_is_pointer(e)) {
                return normalize_int_ct(sf, r.dst, lower_value_ctype(e), diag);
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
        if (lower_has_kind(e, CC_CT_LDOUBLE)) {
            set_value_size(sf, in.dst, 16);
        }
        if (lower_has_kind(e, CC_CT_FLOAT)) {
            return normalize_float_ct(sf, in.dst, lower_value_ctype(e), diag);
        }
        if (vt == CC_VAL_I64 && lower_is_integer(e) && cc_ct_int_width(lower_value_ctype(e)) >= 64) {
            set_value_size(sf, in.dst, 8);
        }
        if (vt == CC_VAL_I64 && lower_is_integer(e) && !lower_is_pointer(e)) {
            return normalize_int_ct(sf, in.dst, lower_value_ctype(e), diag);
        }
        return in.dst;
    }

    case CC_EXPR_CALL: {
        const cc_function_t *callee = NULL;
        builtin_kind_t bk = builtin_kind(e->ident);
        int bswap_bits = builtin_bswap_bits(e->ident);
        int indirect_callee = -1;
        int ident_var_idx = -1;
        const cc_global_t *ident_global = NULL;
        if (e->ident != NULL) {
            callee = find_fn(tu, e->ident);
            ident_var_idx = var_find_visible(vars, var_count, e->ident, depth);
            ident_global = find_global(tu, e->ident);
        }
        if (bk == BUILTIN_VA_START) {
            int dst_idx;
            int last_idx;
            int va_start_dst;
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
            in.dst = new_value(sf, CC_VAL_I64);
            in.imm = (long)(last_idx + 1);
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory appending va_start");
                return -1;
            }
            va_start_dst = in.dst;
            if (emit_local_var_scalar_store(sf, &vars[dst_idx], va_start_dst, diag) != 0) {
                return -1;
            }
            return va_start_dst;
        }
        if (bk == BUILTIN_VA_END) {
            return emit_const_i64_instr(sf, 0);
        }
        if (bk == BUILTIN_VA_COPY) {
            int dst_idx;
            int srcv;
            int copy_src;
            int copy_dst;
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

            /*
             * On SysV x86-64 we model va_list as a pointer to the ABI state
             * record ({gp_offset, fp_offset, overflow_arg_area, reg_save_area}).
             * va_copy must duplicate that record so advancing one list does not
             * mutate the other.
             */
            if (cc_target_pointer_size() == 8) {
                copy_src = srcv;
                copy_dst = emit_local_storage_alloc(sf, 24, diag);
                if (copy_dst < 0) {
                    return -1;
                }
                if (emit_memcpy_instr(sf, copy_dst, copy_src, 24, diag) != 0) {
                    return -1;
                }
                srcv = copy_dst;
            }
            if (emit_local_var_scalar_store(sf, &vars[dst_idx], srcv, diag) != 0) {
                set_diag(diag, "out of memory appending va_copy");
                return -1;
            }
            return srcv;
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
            /* the value is of the type named, which is the expression's */
            mem_size = lower_value_size(tu, e);
            if (mem_size <= 0) {
                set_diag(diag, "unsupported __builtin_va_arg type size");
                return -1;
            }

            if (cc_target_pointer_size() == 8) {
                int ap_ptr;
                int gp_off_ptr;
                int fp_off_ptr;
                int overflow_ptr_ptr;
                int regsave_ptr_ptr;
                int gp_off;
                int fp_off;
                int overflow_ptr;
                int regsave_ptr;
                int use_fp = lower_has_kind(e, CC_CT_DOUBLE) || lower_has_kind(e, CC_CT_FLOAT);
                int use_ldouble = lower_has_kind(e, CC_CT_LDOUBLE);
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
                int is_agg = lower_record_id(e) >= 0;
                cc_value_type_t dst_ty = is_agg ? CC_VAL_I64 : lower_value_kind(e);

                ap_ptr = emit_local_var_scalar_load(sf, &vars[ap_idx], diag);
                if (ap_ptr < 0) {
                    return -1;
                }
                ap_ptr = cast_value(sf, ap_ptr, CC_VAL_I64, diag);
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

                if (use_ldouble) {
                    int align_add;
                    int align_mask;
                    int aligned_tmp;
                    int aligned_ptr;
                    int next_overflow_ld;
                    int dst_ld;

                    align_add = emit_const_i64_instr(sf, 15);
                    align_mask = emit_const_i64_instr(sf, -16);
                    if (align_add < 0 || align_mask < 0) {
                        return -1;
                    }
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_ADD;
                    in.dst = new_value(sf, CC_VAL_I64);
                    in.lhs = overflow_ptr;
                    in.rhs = align_add;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory aligning va_arg long double pointer");
                        return -1;
                    }
                    aligned_tmp = in.dst;
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_AND;
                    in.dst = new_value(sf, CC_VAL_I64);
                    in.lhs = aligned_tmp;
                    in.rhs = align_mask;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory masking va_arg long double pointer alignment");
                        return -1;
                    }
                    aligned_ptr = in.dst;

                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_LOAD;
                    in.dst = new_value(sf, lower_value_kind(e));
                    in.lhs = aligned_ptr;
                    in.imm = mem_size;
                    in.is_unsigned = 0;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory loading va_arg long double value");
                        return -1;
                    }
                    dst_ld = in.dst;

                    stack_incv = emit_const_i64_instr(sf, align_up_long(mem_size, 8));
                    if (stack_incv < 0) {
                        return -1;
                    }
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_ADD;
                    in.dst = new_value(sf, CC_VAL_I64);
                    in.lhs = aligned_ptr;
                    in.rhs = stack_incv;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory incrementing va_arg long double overflow pointer");
                        return -1;
                    }
                    next_overflow_ld = in.dst;
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_STORE;
                    in.lhs = overflow_ptr_ptr;
                    in.rhs = next_overflow_ld;
                    in.imm = 8;
                    if (push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory storing va_arg long double overflow pointer update");
                        return -1;
                    }
                    return dst_ld;
                }

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
                in.imm = is_agg ? cc_target_pointer_size() : load_size;
                in.is_unsigned = is_agg ? 1 : (lower_loads_unsigned(e) ? 1 : 0);
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
                in.imm = is_agg ? cc_target_pointer_size() : load_size;
                in.is_unsigned = is_agg ? 1 : (lower_loads_unsigned(e) ? 1 : 0);
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

            cur_ap = emit_local_var_scalar_load(sf, &vars[ap_idx], diag);
            if (cur_ap < 0) {
                return -1;
            }
            cur_ap = cast_value(sf, cur_ap, CC_VAL_I64, diag);
            if (cur_ap < 0) {
                return -1;
            }
            if (lower_record_id(e) >= 0) {
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
                in.imm = cc_target_pointer_size();
                in.is_unsigned = 1;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory appending va_arg aggregate pointer load");
                    return -1;
                }
                load_v = in.dst;
                step = cc_target_pointer_size();
            } else {
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_LOAD;
                in.dst = new_value(sf, lower_value_kind(e));
                in.lhs = cur_ap;
                in.rhs = -1;
                in.imm = mem_size;
                in.is_unsigned = lower_loads_unsigned(e) ? 1 : 0;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory appending va_arg load");
                    return -1;
                }
                load_v = in.dst;
                step = align_up_long(mem_size, cc_target_pointer_size());
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
            if (emit_local_var_scalar_store(sf, &vars[ap_idx], next_ap, diag) != 0) {
                set_diag(diag, "out of memory appending va_arg pointer update");
                return -1;
            }
            return load_v;
        }
        if (bk == BUILTIN_ALLOCA) {
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
            return emit_dynalloc_instr(sf, sizev, 0, diag);
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
            long long dummy = 0;
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
        if (bk == BUILTIN_HUGE_VAL || bk == BUILTIN_HUGE_VALF || bk == BUILTIN_HUGE_VALL ||
            bk == BUILTIN_INF || bk == BUILTIN_INFF || bk == BUILTIN_INFL) {
            (void)e;
            return emit_const_f64_instr(sf, INFINITY);
        }
        if (bk == BUILTIN_LDBL_MIN) {
            return emit_const_ldouble_special_instr(sf, 1);
        }
        if (bk == BUILTIN_LDBL_DENORM_MIN) {
            return emit_const_ldouble_special_instr(sf, 2);
        }
        if (bk == BUILTIN_LDBL_MAX) {
            return emit_const_ldouble_special_instr(sf, 3);
        }
        if (bk == BUILTIN_ISFINITE || bk == BUILTIN_ISINF || bk == BUILTIN_ISINF_SIGN || bk == BUILTIN_ISNAN ||
            bk == BUILTIN_SIGNBIT) {
            int arg_v;
            cc_type_t store_t;

            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, "floating classification builtin lowering expects 1 argument");
                return -1;
            }
            arg_v = lower_fp_builtin_arg(tu, sf, ctx, vars, var_count, depth, e->args[0], &store_t, diag);
            if (arg_v < 0) {
                return -1;
            }
            if (bk == BUILTIN_ISNAN) {
                return emit_cmp_instr(sf, CC_CMP_NE, arg_v, arg_v, 0, diag);
            }
            if (bk == BUILTIN_SIGNBIT) {
                return emit_fp_signbit_from_value(sf, arg_v, store_t, diag);
            }
            {
                int inf_sign = emit_fp_isinf_sign_from_value(sf, arg_v, store_t, diag);
                int zero = emit_const_i64_instr(sf, 0);
                if (inf_sign < 0 || zero < 0) {
                    return -1;
                }
                if (bk == BUILTIN_ISINF_SIGN) {
                    return inf_sign;
                }
                if (bk == BUILTIN_ISINF) {
                    return emit_cmp_instr(sf, CC_CMP_NE, inf_sign, zero, 1, diag);
                }
                {
                    int self_eq = emit_cmp_instr(sf, CC_CMP_EQ, arg_v, arg_v, 0, diag);
                    int not_inf = emit_cmp_instr(sf, CC_CMP_EQ, inf_sign, zero, 1, diag);
                    if (self_eq < 0 || not_inf < 0) {
                        return -1;
                    }
                    return emit_i64_binop_instr(sf, CC_SSA_AND, self_eq, not_inf, 0, diag);
                }
            }
        }
        if (bk == BUILTIN_NANF || bk == BUILTIN_NAN || bk == BUILTIN_NANL) {
            return emit_const_f64_instr(sf, NAN);
        }
        if (bk == BUILTIN_POPCOUNT) {
            /*
             * The set bits of the argument converted to the builtin's width
             * (unsigned int, unsigned long, unsigned long long): a wider one
             * truncated, a narrower one sign- or zero-extended as its type is,
             * so (signed char)-1 counts 32.  A narrower argument's own bits
             * are counted, plus, when it is signed and negative, the bits the
             * sign extension sets -- which needs no value wider than the
             * argument (i386 has no 64-bit register to extend an int into).
             */
            int bits = builtin_bitop_width(e);
            long arg_size;
            int arg_bits;
            int sign_extends;
            int v;
            int cnt;

            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, "__builtin_popcount lowering expects 1 argument");
                return -1;
            }
            v = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (v < 0 || (v = cast_value(sf, v, CC_VAL_I64, diag)) < 0) {
                return -1;
            }
            arg_size = lower_value_size(tu, e->args[0]);
            arg_bits = (arg_size > 0 && arg_size < 8) ? (int)arg_size * 8 : 64;
            sign_extends = arg_bits < bits && !cc_ct_is_unsigned(lower_value_ctype(e->args[0]));
            if (arg_bits < bits) {
                bits = arg_bits;
            }
            cnt = v;
            if (bits < 64) {
                int mask = emit_const_i64_instr(sf, (long long)((1ULL << bits) - 1ULL));
                if (mask < 0 || (cnt = emit_i64_binop_instr(sf, CC_SSA_AND, v, mask, 1, diag)) < 0) {
                    return -1;
                }
            }
            cnt = lower_popcount(sf, cnt, bits, diag);
            if (cnt >= 0 && sign_extends) {
                int zero = emit_const_i64_instr(sf, 0);
                int ext = emit_const_i64_instr(sf, builtin_bitop_width(e) - arg_bits);
                int neg = zero < 0 ? -1 : emit_cmp_instr(sf, CC_CMP_LT, v, zero, 0, diag);
                if (ext < 0 || neg < 0 || (neg = cast_value(sf, neg, CC_VAL_I64, diag)) < 0 ||
                    (ext = emit_i64_binop_instr(sf, CC_SSA_MUL, neg, ext, 0, diag)) < 0) {
                    return -1;
                }
                cnt = emit_i64_binop_instr(sf, CC_SSA_ADD, cnt, ext, 0, diag);
            }
            return cnt;
        }
        if (bk == BUILTIN_PARITY) {
            /* the low bit of the set-bit count, of the argument converted to the builtin's width */
            int bits = builtin_bitop_width(e);
            long arg_size;
            int v;
            int cnt;
            int one;

            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, "__builtin_parity lowering expects 1 argument");
                return -1;
            }
            v = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (v < 0 || (v = cast_value(sf, v, CC_VAL_I64, diag)) < 0) {
                return -1;
            }
            arg_size = lower_value_size(tu, e->args[0]);
            if (arg_size <= 0 || arg_size > 8) {
                arg_size = 8;
            }
            /*
             * A wider argument is truncated to the builtin's width.  A narrower
             * one is extended to it, which adds either no set bits or a multiple
             * of 8 of them -- never changing the parity -- so its own bits are
             * enough, and masking to them drops whatever the extension put above.
             */
            if (arg_size * 8 < bits) {
                bits = (int)arg_size * 8;
            }
            if (bits < 64) {
                int mask = emit_const_i64_instr(sf, (long long)((1ULL << bits) - 1ULL));
                if (mask < 0 || (v = emit_i64_binop_instr(sf, CC_SSA_AND, v, mask, 1, diag)) < 0) {
                    return -1;
                }
            }
            cnt = lower_popcount(sf, v, bits, diag);
            one = emit_const_i64_instr(sf, 1);
            if (cnt < 0 || one < 0) {
                return -1;
            }
            return emit_i64_binop_instr(sf, CC_SSA_AND, cnt, one, 1, diag);
        }
        if (bk == BUILTIN_ADD_OVERFLOW || bk == BUILTIN_SUB_OVERFLOW || bk == BUILTIN_MUL_OVERFLOW ||
            bk == BUILTIN_ADD_OVERFLOW_P || bk == BUILTIN_SUB_OVERFLOW_P || bk == BUILTIN_MUL_OVERFLOW_P) {
            int av;
            int bv;
            int pv = -1;
            int rv;
            int rv_exact;
            int ov = -1;
            int overflow_op;
            int has_store;
            const cc_ctype_t *out_ct;
            int out_unsigned;
            int out_bits = 0;
            long mem_size;
            size_t first_instr;
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
            /*
             * The exact result is computed in 64 bits on both targets: each
             * operand widened from its own type (on i386 an int is 32 bits,
             * and a sum of two would wrap before it is compared).
             */
            av = normalize_integral_value(sf, av,
                                          lower_is_unsigned(e->args[0]) ? CC_TYPE_ULONG_LONG
                                                                                               : CC_TYPE_LONG_LONG,
                                          diag);
            bv = normalize_integral_value(sf, bv,
                                          lower_is_unsigned(e->args[1]) ? CC_TYPE_ULONG_LONG
                                                                                               : CC_TYPE_LONG_LONG,
                                          diag);
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
            set_value_size(sf, bin.dst, 8);
            rv_exact = bin.dst;
            rv = rv_exact;

            /* the result's type: what the third operand points to, or for the _p forms the operand's own */
            if (has_store) {
                out_ct = lower_atomic_elem_ctype(e->args[2]);
                mem_size = lower_pointee_size(tu, e->args[2]);
                if (mem_size <= 0) {
                    mem_size = cc_target_pointer_size();
                }
            } else {
                out_ct = lower_value_ctype(e->args[2]);
                mem_size = cc_ct_size(out_ct, tu);
            }
            out_unsigned = cc_ct_is_unsigned(out_ct) && out_ct->kind != CC_CT_BOOL;

            rv = cast_value(sf, rv, ct_value_kind(out_ct), diag);
            if (rv < 0) {
                return -1;
            }
            if (cc_ct_is_integer(out_ct)) {
                rv = normalize_int_ct(sf, rv, out_ct, diag);
                if (rv < 0) {
                    return -1;
                }
                out_bits = integral_type_bits(cc_ct_legacy_kind(out_ct->kind));
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

            /* the overflow check: values from here on are 64-bit (see the end) */
            first_instr = sf->instr_count;
            if (out_bits > 0 && out_bits < 64) {
                /* overflowed if the result, widened back from its type, is not the exact one */
                int rv_wide = normalize_integral_value(
                    sf, rv, out_unsigned ? CC_TYPE_ULONG_LONG : CC_TYPE_LONG_LONG, diag);
                if (rv_wide < 0) {
                    return -1;
                }
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_NE;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = rv_exact;
                bin.rhs = rv_wide;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                ov = bin.dst;
            } else if (out_unsigned) {
                if (overflow_op == 2) {
                    int z = emit_const_i64_instr(sf, 0);
                    int cmp_nz;
                    int cmp_z;
                    int safe_bv;
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
                    bin.op = CC_SSA_CMP;
                    bin.cmp_kind = CC_CMP_EQ;
                    bin.is_unsigned = 1;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = bv;
                    bin.rhs = z;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    cmp_z = normalize_integral_value(sf, bin.dst, CC_TYPE_ULONG_LONG, diag);
                    if (cmp_z < 0) {
                        return -1;
                    }
                    memset(&bin, 0, sizeof(bin));
                    bin.op = CC_SSA_OR;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = bv;
                    bin.rhs = cmp_z;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    safe_bv = bin.dst;
                    set_value_size(sf, safe_bv, 8);
                    memset(&bin, 0, sizeof(bin));
                    bin.op = CC_SSA_DIV;
                    bin.is_unsigned = 1;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = rv;
                    bin.rhs = safe_bv;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    qv = bin.dst;
                    set_value_size(sf, qv, 8);
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
                {
                    int z = emit_const_i64_instr(sf, 0);
                    int exact_neg;
                    if (z < 0) {
                        return -1;
                    }
                    memset(&bin, 0, sizeof(bin));
                    bin.op = CC_SSA_CMP;
                    bin.cmp_kind = CC_CMP_LT;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = rv_exact;
                    bin.rhs = z;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    exact_neg = bin.dst;
                    memset(&bin, 0, sizeof(bin));
                    bin.op = CC_SSA_OR;
                    bin.dst = new_value(sf, CC_VAL_I64);
                    bin.lhs = ov;
                    bin.rhs = exact_neg;
                    if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                        return -1;
                    }
                    ov = bin.dst;
                }
            } else if (overflow_op == 2) {
                int z = emit_const_i64_instr(sf, 0);
                int cmp_nz;
                int cmp_z;
                int safe_bv;
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
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_EQ;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = bv;
                bin.rhs = z;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                cmp_z = normalize_integral_value(sf, bin.dst, CC_TYPE_ULONG_LONG, diag);
                if (cmp_z < 0) {
                    return -1;
                }
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_OR;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = bv;
                bin.rhs = cmp_z;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                safe_bv = bin.dst;
                set_value_size(sf, safe_bv, 8);
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_DIV;
                bin.dst = new_value(sf, CC_VAL_I64);
                bin.lhs = rv;
                bin.rhs = safe_bv;
                if (bin.dst < 0 || push_instr(sf, bin) != 0) {
                    return -1;
                }
                qv = bin.dst;
                set_value_size(sf, qv, 8);
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
            {
                /* the check's constants are compared with 64-bit values; on
                 * i386 a 4-byte constant would compare only the low words */
                size_t k;
                for (k = first_instr; k < sf->instr_count; ++k) {
                    if (sf->instrs[k].op == CC_SSA_CONST && sf->instrs[k].dst >= 0) {
                        set_value_size(sf, sf->instrs[k].dst, 8);
                    }
                }
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
            {
                long obj_size;
                long obj_off;
                if (pointed_object_extent(tu, e->args[0], &obj_size, &obj_off)) {
                    /* what is left of the object past the pointer */
                    n = (obj_off >= 0 && obj_off <= obj_size) ? obj_size - obj_off : 0;
                }
                /* otherwise the object is not known (a pointer from malloc, say): leave n unknown */
            }
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
                call_in.sym = cc_strdup("memcpy");
                call_in.args[0] = dstv;
                call_in.args[1] = cast_value(sf, lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag),
                                             CC_VAL_I64, diag);
                call_in.args[2] = nbytes;
            } else if (bk == BUILTIN_MEMMOVE || bk == BUILTIN_MEMMOVE_CHK) {
                call_in.sym = cc_strdup("memmove");
                call_in.args[0] = dstv;
                call_in.args[1] = cast_value(sf, lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag),
                                             CC_VAL_I64, diag);
                call_in.args[2] = nbytes;
            } else {
                call_in.sym = cc_strdup("memset");
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
        if (bk == BUILTIN_STRLEN) {
            cc_ssa_instr_t call_in;
            int arg0;
            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, "__builtin_strlen lowering expects 1 argument");
                return -1;
            }
            arg0 = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (arg0 < 0) {
                return -1;
            }
            arg0 = cast_value(sf, arg0, CC_VAL_I64, diag);
            if (arg0 < 0) {
                return -1;
            }
            memset(&call_in, 0, sizeof(call_in));
            call_in.op = CC_SSA_CALL;
            call_in.call_is_variadic = 0;
            call_in.sym = cc_strdup("strlen");
            call_in.arg_count = 1;
            call_in.args = (int *)calloc(1, sizeof(*call_in.args));
            call_in.dst = new_value(sf, CC_VAL_I64);
            if (call_in.sym == NULL || call_in.args == NULL || call_in.dst < 0) {
                free(call_in.sym);
                free(call_in.args);
                return -1;
            }
            call_in.args[0] = arg0;
            if (push_instr(sf, call_in) != 0) {
                free(call_in.sym);
                free(call_in.args);
                return -1;
            }
            return call_in.dst;
        }
        if (bk == BUILTIN_PREFETCH) {
            size_t ai;
            if (e->arg_count < 1 || e->arg_count > 3) {
                set_diag(diag, "__builtin_prefetch lowering expects 1 to 3 arguments");
                return -1;
            }
            for (ai = 0; ai < e->arg_count; ++ai) {
                int v = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[ai], diag);
                if (v < 0) {
                    return -1;
                }
                (void)v;
            }
            return emit_const_i64_instr(sf, 0);
        }
        if (bk == BUILTIN_CPU_SUPPORTS) {
            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, "__builtin_cpu_supports lowering expects 1 argument");
                return -1;
            }
            if (e->args[0]->kind != CC_EXPR_STR) {
                set_diag(diag, "__builtin_cpu_supports lowering expects a string literal");
                return -1;
            }
            /*
             * Conservative fallback until full runtime CPU dispatch is wired:
             * report feature probes as unavailable rather than miscompile by
             * claiming unsupported ISA at runtime.
             */
            return emit_const_i64_instr(sf, 0);
        }
        if (bk == BUILTIN_CLZ || bk == BUILTIN_CTZ || bk == BUILTIN_FFS) {
            const char *builtin_name = "__builtin_clz";
            if (bk == BUILTIN_CTZ) {
                builtin_name = "__builtin_ctz";
            } else if (bk == BUILTIN_FFS) {
                builtin_name = "__builtin_ffs";
            }
            if (e->arg_count != 1 || e->args[0] == NULL) {
                char msg[112];
                snprintf(msg, sizeof(msg), "%s lowering expects 1 argument", builtin_name);
                set_diag(diag, msg);
                return -1;
            }
            if (bk == BUILTIN_CLZ) {
                return lower_builtin_clz(tu, sf, ctx, vars, var_count, depth, e, diag);
            }
            if (bk == BUILTIN_CTZ) {
                return lower_builtin_ctz(tu, sf, ctx, vars, var_count, depth, e, diag);
            }
            return lower_builtin_ffs(tu, sf, ctx, vars, var_count, depth, e, diag);
        }
        if (bk == BUILTIN_SYNC_SYNCHRONIZE) {
            if (emit_atomic_full_barrier(sf, diag) != 0) {
                return -1;
            }
            return emit_const_i64_instr(sf, 0);
        }
        if (bk == BUILTIN_ATOMIC_THREAD_FENCE || bk == BUILTIN_ATOMIC_SIGNAL_FENCE) {
            long long mo = CC_ATOMIC_SEQ_CST;
            int mov;

            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, bk == BUILTIN_ATOMIC_THREAD_FENCE ? "__atomic_thread_fence lowering expects 1 argument"
                                                                  : "__atomic_signal_fence lowering expects 1 argument");
                return -1;
            }
            mov = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
            if (mov < 0) {
                return -1;
            }
            if (eval_const_int_expr_simple(e->args[0], &mo) != 0) {
                mo = CC_ATOMIC_SEQ_CST;
            }
            if (mo != CC_ATOMIC_RELAXED) {
                if (bk == BUILTIN_ATOMIC_THREAD_FENCE) {
                    if (emit_atomic_full_barrier(sf, diag) != 0) {
                        return -1;
                    }
                } else {
                    if (emit_atomic_compiler_barrier(sf, diag) != 0) {
                        return -1;
                    }
                }
            }
            return emit_const_i64_instr(sf, 0);
        }
        if (bk == BUILTIN_SYNC_LOCK_RELEASE) {
            int p;
            int z;
            long mem_size;
            const cc_ctype_t *elem_ct;
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
            elem_ct = lower_atomic_elem_ctype(e->args[0]);
            mem_size = lower_pointee_size(tu, e->args[0]);
            if (mem_size <= 0) {
                mem_size = lower_pointer_step(tu, e->args[0]);
            }
            if (mem_size <= 0) {
                mem_size = cc_target_pointer_size();
            }
            if (emit_atomic_compiler_barrier(sf, diag) != 0) {
                return -1;
            }
            z = emit_const_i64_instr(sf, 0);
            if (z < 0) {
                return -1;
            }
            z = cast_value(sf, z, ct_value_kind(elem_ct), diag);
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
            long long memorder = CC_ATOMIC_SEQ_CST;
            int need_pre_compiler = 0;
            int need_post_compiler = 0;
            int need_pre_full = 0;
            int need_post_full = 0;
            const cc_ctype_t *elem_ct;
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
            elem_ct = lower_atomic_elem_ctype(e->args[0]);
            mem_size = lower_pointee_size(tu, e->args[0]);
            if (mem_size <= 0) {
                mem_size = lower_pointer_step(tu, e->args[0]);
            }
            if (mem_size <= 0) {
                mem_size = cc_target_pointer_size();
            }
            if (bk == BUILTIN_ATOMIC_LOAD_N) {
                if (e->arg_count >= 2 && e->args[1] != NULL) {
                    if (lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag) < 0) {
                        return -1;
                    }
                    if (eval_const_int_expr_simple(e->args[1], &memorder) != 0) {
                        memorder = CC_ATOMIC_SEQ_CST;
                    }
                }
            } else if (bk == BUILTIN_ATOMIC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_SUB || bk == BUILTIN_ATOMIC_EXCHANGE_N ||
                       bk == BUILTIN_ATOMIC_STORE_N) {
                if (e->arg_count >= 3 && e->args[2] != NULL) {
                    if (lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[2], diag) < 0) {
                        return -1;
                    }
                    if (eval_const_int_expr_simple(e->args[2], &memorder) != 0) {
                        memorder = CC_ATOMIC_SEQ_CST;
                    }
                }
            }
            if (bk == BUILTIN_ATOMIC_LOAD_N) {
                if (memorder == CC_ATOMIC_SEQ_CST) {
                    need_pre_full = 1;
                    need_post_full = 1;
                } else if (memorder != CC_ATOMIC_RELAXED) {
                    need_post_compiler = 1;
                }
            } else if (bk == BUILTIN_ATOMIC_STORE_N) {
                if (memorder == CC_ATOMIC_SEQ_CST) {
                    need_pre_full = 1;
                    need_post_full = 1;
                } else if (memorder != CC_ATOMIC_RELAXED) {
                    need_pre_compiler = 1;
                }
            } else {
                if (memorder == CC_ATOMIC_SEQ_CST) {
                    need_pre_full = 1;
                    need_post_full = 1;
                } else if (memorder != CC_ATOMIC_RELAXED) {
                    need_pre_compiler = 1;
                    need_post_compiler = 1;
                }
            }
            if (need_pre_full) {
                if (emit_atomic_full_barrier(sf, diag) != 0) {
                    return -1;
                }
            } else if (need_pre_compiler) {
                if (emit_atomic_compiler_barrier(sf, diag) != 0) {
                    return -1;
                }
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_LOAD;
            in.dst = new_value(sf, ct_value_kind(elem_ct));
            in.lhs = p;
            in.rhs = -1;
            in.imm = mem_size;
            in.is_unsigned = ct_loads_unsigned(elem_ct) ? 1 : 0;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                return -1;
            }
            oldv = in.dst;
            if (bk == BUILTIN_ATOMIC_LOAD_N) {
                if (need_post_full) {
                    if (emit_atomic_full_barrier(sf, diag) != 0) {
                        return -1;
                    }
                } else if (need_post_compiler) {
                    if (emit_atomic_compiler_barrier(sf, diag) != 0) {
                        return -1;
                    }
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
                rhsv = cast_value(sf, rhsv, ct_value_kind(elem_ct), diag);
                if (rhsv < 0) {
                    return -1;
                }
            }
            if (bk == BUILTIN_SYNC_FETCH_ADD || bk == BUILTIN_SYNC_FETCH_SUB || bk == BUILTIN_SYNC_SUB_AND_FETCH ||
                bk == BUILTIN_ATOMIC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_SUB) {
                memset(&bin, 0, sizeof(bin));
                bin.op = (bk == BUILTIN_SYNC_FETCH_ADD || bk == BUILTIN_ATOMIC_FETCH_ADD) ? CC_SSA_ADD : CC_SSA_SUB;
                bin.dst = new_value(sf, ct_value_kind(elem_ct));
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
                if (need_post_full) {
                    if (emit_atomic_full_barrier(sf, diag) != 0) {
                        return -1;
                    }
                } else if (need_post_compiler) {
                    if (emit_atomic_compiler_barrier(sf, diag) != 0) {
                        return -1;
                    }
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
                if (need_post_full) {
                    if (emit_atomic_full_barrier(sf, diag) != 0) {
                        return -1;
                    }
                } else if (need_post_compiler) {
                    if (emit_atomic_compiler_barrier(sf, diag) != 0) {
                        return -1;
                    }
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
                if (need_post_full) {
                    if (emit_atomic_full_barrier(sf, diag) != 0) {
                        return -1;
                    }
                } else if (need_post_compiler) {
                    if (emit_atomic_compiler_barrier(sf, diag) != 0) {
                        return -1;
                    }
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
                expectv = cast_value(sf, expectv, ct_value_kind(elem_ct), diag);
                if (expectv < 0) {
                    return -1;
                }
                desiredv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[2], diag);
                if (desiredv < 0) {
                    return -1;
                }
                desiredv = cast_value(sf, desiredv, ct_value_kind(elem_ct), diag);
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
                if (need_post_full) {
                    if (emit_atomic_full_barrier(sf, diag) != 0) {
                        return -1;
                    }
                } else if (need_post_compiler) {
                    if (emit_atomic_compiler_barrier(sf, diag) != 0) {
                        return -1;
                    }
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
                int cmask = emit_const_i64_instr(sf, (long long)mask_u);
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
        in.rhs = -1;
        if (e->ident == NULL ||
            (e->ident != NULL && callee == NULL && bk == BUILTIN_NONE && bswap_bits == 0 &&
             (ident_var_idx >= 0 || (ident_global != NULL && ident_global->array_ndim <= 0 &&
                                      cc_type_is_pointer(ident_global->type))))) {
            if (e->ident == NULL) {
                const cc_expr_t *callee_expr = e->lhs;
                cc_expr_t callee_tmp;
                if (e->lhs == NULL) {
                    set_diag(diag, "malformed indirect call expression");
                    return -1;
                }
                if (callee_expr->kind == CC_EXPR_DEREF && cc_ct_expr_designates_function(callee_expr->lhs)) {
                    /*
                     * Where the operand's type is known, a `*` whose operand is a
                     * pointer to a function (or is the function) changes nothing:
                     * `(*fp)()` and `(**fp)()` call fp.
                     */
                    while (callee_expr->kind == CC_EXPR_DEREF && cc_ct_expr_designates_function(callee_expr->lhs)) {
                        callee_expr = callee_expr->lhs;
                    }
                } else if (callee_expr->kind == CC_EXPR_DEREF && callee_expr->lhs != NULL &&
                           cc_ct_of_expr(callee_expr->lhs) == NULL && lower_is_pointer(callee_expr->lhs)) {
                    int keep_deref = 0;
                    const cc_expr_t *d = callee_expr->lhs;
                    const cc_ctype_t *dt = lower_value_ctype(callee_expr->lhs);

                    if (dt != NULL && dt->kind == CC_CT_POINTER && dt->of != NULL && dt->of->kind == CC_CT_POINTER) {
                        /*
                         * Explicit indirect calls through pointer-to-function-pointer
                         * chains need one real load before the final CALLI target is
                         * available. Only elide the dereference for plain function
                         * pointers, where `(*fp)()` is semantically just `fp()`.
                         */
                        keep_deref = 1;
                    }
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
                if (ident_global != NULL && ident_var_idx < 0 && ident_global->array_ndim <= 0 &&
                    cc_type_is_pointer(ident_global->type)) {
                    cc_ssa_instr_t load_fp;
                    long mem_size = type_size_bytes_with_struct(tu, ident_global->type, ident_global->type_struct_id);
                    int gaddr = emit_global_addr(sf, ident_global->name, diag);
                    if (gaddr < 0) {
                        return -1;
                    }
                    if (mem_size <= 0) {
                        mem_size = cc_target_pointer_size();
                    }
                    memset(&load_fp, 0, sizeof(load_fp));
                    load_fp.op = CC_SSA_LOAD;
                    load_fp.dst = new_value(sf, CC_VAL_I64);
                    load_fp.lhs = gaddr;
                    load_fp.rhs = -1;
                    load_fp.imm = mem_size;
                    load_fp.is_unsigned = 1;
                    if (load_fp.dst < 0 || push_instr(sf, load_fp) != 0) {
                        return -1;
                    }
                    indirect_callee = load_fp.dst;
                } else {
                    cc_expr_t callee_ref;
                    memset(&callee_ref, 0, sizeof(callee_ref));
                    callee_ref.kind = CC_EXPR_IDENT;
                    callee_ref.ident = e->ident;
                    callee_ref.ctype = ident_var_idx >= 0 ? lower_var_ctype(&vars[ident_var_idx])
                                                          : lower_global_ctype(ident_global);
                    indirect_callee = lower_expr(tu, sf, ctx, vars, var_count, depth, &callee_ref, diag);
                }
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
            /*
             * For calls without a visible prototype, C applies default argument
             * promotions and the real callee may be variadic. On SysV AMD64
             * the caller must set %al to the number of vector register args for
             * variadic-compatible calls; treating unknown/non-prototype calls as
             * variadic-safe avoids ABI breakage in mixed K&R/prototype code.
             */
            in.call_is_variadic = (callee != NULL && callee->has_prototype) ? callee->is_variadic : 1;
            in.call_fixed_count =
                (callee != NULL && callee->has_prototype && callee->is_variadic) ? (int)callee->param_count : 0;
            in.sym = cc_strdup(e->ident);
            if (in.sym == NULL) {
                return -1;
            }
        }
        if (in.op == CC_SSA_CALL && ident_var_idx < 0) {
            /* a GNU nested function using the caller's variables: their addresses in the static chain */
            const cc_function_t *nest_callee = cc_find_function_def(tu, e->ident);

            if (cc_nested_uses_outer(tu, nest_callee)) {
                int nest_envv = lower_nested_env(tu, sf, vars, var_count, nest_callee, diag);

                if (nest_envv < 0) {
                    free(in.sym);
                    return -1;
                }
                in.call_chain = nest_envv + 1;
            }
        }
        in.arg_count = e->arg_count;
        if (in.arg_count > 0) {
            in.args = (int *)calloc(in.arg_count, sizeof(*in.args));
            in.call_arg_abi = (unsigned char *)calloc(in.arg_count, sizeof(*in.call_arg_abi));
            in.call_arg_sizes = (uint32_t *)calloc(in.arg_count, sizeof(*in.call_arg_sizes));
            if (in.args == NULL || in.call_arg_abi == NULL || in.call_arg_sizes == NULL) {
                free(in.sym);
                free(in.args);
                free(in.call_arg_abi);
                free(in.call_arg_sizes);
                return -1;
            }
        }

        /* i386: the vector arguments in %xmm0-2 and %mm0-2 so far */
        int vec_n16 = 0;
        int vec_n8 = 0;

        for (i = 0; i < e->arg_count; ++i) {
            cc_value_type_t want;
            int av = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[i], diag);
            int arg_needs_ldouble_abi = 0;
            int arg_needs_float_abi = 0;
            int arg_is_aggregate =
                (e->args[i] != NULL && lower_record_id(e->args[i]) >= 0);
            int arg_is_variadic =
                (in.call_is_variadic && (int)i >= in.call_fixed_count) ? 1 : 0;
            long arg_size = e->args[i] != NULL ? lower_value_size(tu, e->args[i]) : cc_target_pointer_size();
            if (arg_size <= 0) {
                arg_size = cc_target_pointer_size();
            }
            if (av < 0) {
                free(in.sym);
                free(in.args);
                free(in.call_arg_abi);
                free(in.call_arg_sizes);
                return -1;
            }
            want = value_type(sf, av);
            if (e->ident != NULL && callee != NULL && callee->has_prototype && i < callee->param_count) {
                if (callee->params[i].type == CC_TYPE_VOID && callee->params[i].type_struct_id >= 0) {
                    long agg_size = type_size_bytes_with_struct(tu, callee->params[i].type, callee->params[i].type_struct_id);
                    int is_transparent_union = 0;
                    int pass_union_scalar = 0;
                    int arg_is_same_aggregate = 0;
                    int pass_agg_mem = 0;
                    int pass_agg_reg = 0;
                    int agg_cls = 0;
                    arg_size = cc_target_pointer_size();
                    if (callee->params[i].type_struct_id >= 0 &&
                        (size_t)callee->params[i].type_struct_id < tu->struct_count) {
                        const cc_struct_def_t *sd = &tu->structs[callee->params[i].type_struct_id];
                        if ((sd->attr_flags & CC_ATTR_TRANSPARENT_UNION) != 0) {
                            is_transparent_union = 1;
                        }
                        if (e->args[i] != NULL && lower_record_id(e->args[i]) >= 0 &&
                            lower_record_id(e->args[i]) == callee->params[i].type_struct_id) {
                            arg_is_same_aggregate = 1;
                        }
                        if (sd->is_union && !arg_is_same_aggregate) {
                            pass_union_scalar = 1;
                        }
                    }
                    int vec_abi = cc_target_pointer_size() == 4
                                      ? i386_vector_abi(tu, callee->params[i].type_struct_id, agg_size, &vec_n16,
                                                        &vec_n8)
                                      : -1;

                    if (vec_abi >= 0) {
                        /* an i386 vector: in an xmm or MMX register, or on the stack aligned to its size */
                        if (CC_ABI_KIND(vec_abi) == CC_CALL_ARG_ABI_AGG_REG) {
                            pass_agg_reg = 1;
                            agg_cls = vec_abi & ~0x0f;
                        } else {
                            pass_agg_mem = 1;
                        }
                    } else if (!is_transparent_union && !pass_union_scalar && agg_size == 0) {
                        in.call_arg_abi[i] = CC_CALL_ARG_ABI_ZERO;
                        arg_size = 0;
                    } else if (!is_transparent_union && !pass_union_scalar && cc_target_pointer_size() == 8 && agg_size > 16) {
                        pass_agg_mem = 1;
                    } else if (!is_transparent_union && !pass_union_scalar && cc_target_pointer_size() == 4 && agg_size > 4) {
                        /* i386 psABI: a struct argument is copied onto the stack */
                        pass_agg_mem = 1;
                    } else if (!is_transparent_union && !pass_union_scalar && cc_target_pointer_size() == 8 &&
                               agg_size > 0 && agg_size <= 16) {
                        /* x86-64: by its eightbytes' classes, in general and xmm registers or memory */
                        agg_cls = abi64_aggregate_class(tu, callee->params[i].type_struct_id, agg_size, 0);
                        if (agg_cls < 0) {
                            pass_agg_mem = 1;
                        } else if (agg_size > 8 || agg_cls != 0) {
                            pass_agg_reg = 1;
                        }
                    }
                    av = cast_value(sf, av, CC_VAL_I64, diag);
                    if (av < 0) {
                        free(in.sym);
                        free(in.args);
                        free(in.call_arg_abi);
                        free(in.call_arg_sizes);
                        return -1;
                    }
                    if (!is_transparent_union && !pass_union_scalar && !pass_agg_mem && !pass_agg_reg && agg_size > 0 &&
                        agg_size <= cc_target_pointer_size()) {
                        cc_ssa_instr_t load_in;
                        memset(&load_in, 0, sizeof(load_in));
                        load_in.op = CC_SSA_LOAD;
                        load_in.dst = new_value(sf, CC_VAL_I64);
                        load_in.lhs = av;
                        load_in.rhs = -1;
                        load_in.imm = agg_size;
                        load_in.is_unsigned = 1;
                        if (load_in.dst < 0 || push_instr(sf, load_in) != 0) {
                            free(in.sym);
                            free(in.args);
                            free(in.call_arg_abi);
                            free(in.call_arg_sizes);
                            set_diag(diag, "out of memory lowering small aggregate call argument");
                            return -1;
                        }
                        av = load_in.dst;
                        arg_size = agg_size;
                    }
                    want = CC_VAL_I64;
                    if (pass_agg_mem) {
                        in.call_arg_abi[i] = (unsigned char)(CC_CALL_ARG_ABI_AGG_MEM |
                                                             vector_mem_flag(tu, callee->params[i].type_struct_id,
                                                                             agg_size));
                        arg_size = agg_size;
                    } else if (pass_agg_reg) {
                        in.call_arg_abi[i] = (unsigned char)(CC_CALL_ARG_ABI_AGG_REG | agg_cls);
                        arg_size = agg_size;
                    }
                } else {
                    if (callee->params[i].type == CC_TYPE_LDOUBLE) {
                        arg_needs_ldouble_abi = 1;
                    }
                    if (callee->params[i].type == CC_TYPE_FLOAT && callee->has_prototype) {
                        /* a prototyped float parameter takes a float, not the double it is held in */
                        arg_needs_float_abi = 1;
                    }
                    arg_size =
                        type_size_bytes_with_struct(tu, callee->params[i].type, callee->params[i].type_struct_id);
                    if (arg_size <= 0) {
                        arg_size = cc_target_pointer_size();
                    }
                    want = ct_value_kind(lower_param_ctype(&callee->params[i]));
                    av = cast_value(sf, av, want, diag);
                    if (av < 0) {
                        free(in.sym);
                        free(in.args);
                        free(in.call_arg_abi);
                        free(in.call_arg_sizes);
                        return -1;
                    }
                    if (callee->params[i].type == CC_TYPE_LDOUBLE) {
                        av = promote_f64_to_ldouble_value(sf, av, diag);
                    } else if (callee->params[i].type == CC_TYPE_DOUBLE) {
                        av = demote_ldouble_to_f64_value(sf, av, diag);
                    } else if (callee->params[i].type == CC_TYPE_FLOAT) {
                        av = normalize_float_value(sf, av, CC_TYPE_FLOAT, diag);
                    }
                    if (callee->params[i].type == CC_TYPE_LDOUBLE || callee->params[i].type == CC_TYPE_DOUBLE ||
                        callee->params[i].type == CC_TYPE_FLOAT) {
                        if (av < 0) {
                            free(in.sym);
                            free(in.args);
                            free(in.call_arg_abi);
                            free(in.call_arg_sizes);
                            return -1;
                        }
                    }
                    av = normalize_integral_ct(sf, av, callee->params[i].ctype, callee->params[i].type, diag);
                    if (av < 0) {
                        free(in.sym);
                        free(in.args);
                        free(in.call_arg_abi);
                        free(in.call_arg_sizes);
                        return -1;
                    }
                }
            } else if (arg_is_aggregate && arg_is_variadic && in.op != CC_SSA_CALLI) {
                /*
                 * The i386 target ABI used by this compiler passes aggregate
                 * varargs as one pointer-sized slot.  __builtin_va_arg lowers
                 * aggregate extraction by reading that slot and copying from
                 * the pointed object, so the caller metadata must not describe
                 * the full aggregate width here.
                 */
                av = cast_value(sf, av, CC_VAL_I64, diag);
                if (av < 0) {
                    free(in.sym);
                    free(in.args);
                    free(in.call_arg_abi);
                    free(in.call_arg_sizes);
                    return -1;
                }
                want = CC_VAL_I64;
                arg_size = cc_target_pointer_size();
            } else if (in.op == CC_SSA_CALLI && arg_is_aggregate) {
                long agg_size = lower_value_size(tu, e->args[i]);
                arg_size = cc_target_pointer_size();
                if (agg_size > 0 && agg_size <= cc_target_pointer_size()) {
                    cc_ssa_instr_t load_in;
                    av = cast_value(sf, av, CC_VAL_I64, diag);
                    if (av < 0) {
                        free(in.sym);
                        free(in.args);
                        free(in.call_arg_abi);
                        free(in.call_arg_sizes);
                        return -1;
                    }
                    memset(&load_in, 0, sizeof(load_in));
                    load_in.op = CC_SSA_LOAD;
                    load_in.dst = new_value(sf, CC_VAL_I64);
                    load_in.lhs = av;
                    load_in.rhs = -1;
                    load_in.imm = agg_size;
                    load_in.is_unsigned = 1;
                    if (load_in.dst < 0 || push_instr(sf, load_in) != 0) {
                        free(in.sym);
                        free(in.args);
                        free(in.call_arg_abi);
                        free(in.call_arg_sizes);
                        set_diag(diag, "out of memory lowering indirect small aggregate call argument");
                        return -1;
                    }
                    av = load_in.dst;
                    want = CC_VAL_I64;
                    arg_size = agg_size;
                }
            } else if (e->args[i] != NULL && lower_has_kind(e->args[i], CC_CT_LDOUBLE)) {
                arg_needs_ldouble_abi = 1;
            }
            in.args[i] = av;
            if (CC_ABI_KIND(in.call_arg_abi[i]) != CC_CALL_ARG_ABI_AGG_MEM &&
                CC_ABI_KIND(in.call_arg_abi[i]) != CC_CALL_ARG_ABI_AGG_REG) {
                in.call_arg_abi[i] = (unsigned char)(arg_needs_ldouble_abi ? CC_CALL_ARG_ABI_LDOUBLE
                                                     : arg_needs_float_abi ? CC_CALL_ARG_ABI_FLOAT
                                                                           : CC_CALL_ARG_ABI_DEFAULT);
            }
            in.call_arg_sizes[i] = (uint32_t)(arg_needs_ldouble_abi ? 12 : arg_needs_float_abi ? 4 : arg_size);
        }
        in.call_ret_x87 = (lower_has_kind(e, CC_CT_LDOUBLE)) ? 1 : 0;
        if (lower_has_kind(e, CC_CT_FLOAT)) {
            /* a float comes back in single precision */
            in.agg_class = CC_ABI_RET_FLOAT;
        }
        if (lower_record_id(e) >= 0) {
            long agg_size = lower_value_size(tu, e);
            int agg_addr;
            if (agg_size <= 0) {
                free(in.sym);
                free(in.args);
                free(in.call_arg_abi);
                free(in.call_arg_sizes);
                set_diag(diag, "unsupported aggregate return size in call lowering");
                return -1;
            }
            agg_addr = emit_local_storage_alloc(sf, agg_size, diag);
            if (agg_addr < 0) {
                free(in.sym);
                free(in.args);
                free(in.call_arg_abi);
                free(in.call_arg_sizes);
                return -1;
            }
            in.dst = agg_addr;
            in.rhs = agg_addr;
            in.imm = agg_size;
            in.is_unsigned = 1;
            in.agg_class = agg_return_class(tu, e->struct_id, agg_size);
        } else if (lower_has_kind(e, CC_CT_VOID)) {
            in.dst = new_value(sf, CC_VAL_I64);
            in.imm = cc_target_pointer_size();
            in.is_unsigned = 1;
        } else {
            in.dst = new_value(sf, lower_value_kind(e));
            in.imm = lower_value_size(tu, e);
            if (in.imm <= 0) {
                in.imm = cc_target_pointer_size();
            }
            in.is_unsigned = lower_loads_unsigned(e) ? 1 : 0;
        }
        if (in.dst < 0) {
            free(in.sym);
            free(in.args);
            free(in.call_arg_abi);
            free(in.call_arg_sizes);
            return -1;
        }

        if (push_instr(sf, in) != 0) {
            free(in.sym);
            free(in.args);
            free(in.call_arg_abi);
            free(in.call_arg_sizes);
            return -1;
        }
        if (lower_has_kind(e, CC_CT_LDOUBLE)) {
            set_value_size(sf, in.dst, 16);
        }
        return in.dst;
    }

    case CC_EXPR_ASSIGN: {
        cc_value_type_t want = lower_value_kind(e);
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
                    rhs = normalize_float_ct(sf, rhs, lower_var_ctype(&vars[idx]), diag);
                    if (rhs < 0) {
                        return -1;
                    }
                    rhs = normalize_integral_ct(sf, rhs, e->ctype, vars[idx].type, diag);
                    if (rhs < 0) {
                        return -1;
                    }
                    if (vars[idx].type == CC_TYPE_VOID && vars[idx].struct_id >= 0) {
                        if (emit_memcpy_instr(sf, saddr, rhs, mem_size, diag) != 0) {
                            return -1;
                        }
                        return saddr;
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
                if (vars[idx].addr_value >= 0 && !var_is_aggregate_or_array(tu, &vars[idx])) {
                    long mem_size = vars[idx].addr_size > 0 ? vars[idx].addr_size : var_object_size_bytes(tu, &vars[idx]);
                    if (mem_size <= 0) {
                        set_diag(diag, "unsupported address-taken local assignment type");
                        return -1;
                    }
                    rhs = normalize_float_ct(sf, rhs, lower_var_ctype(&vars[idx]), diag);
                    if (rhs < 0) {
                        return -1;
                    }
                    rhs = normalize_integral_ct(sf, rhs, e->ctype, vars[idx].type, diag);
                    if (rhs < 0) {
                        return -1;
                    }
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_STORE;
                    in.dst = -1;
                    in.lhs = vars[idx].addr_value;
                    in.rhs = rhs;
                    in.imm = mem_size;
                    if (push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory appending address-taken assignment store");
                        return -1;
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
                    return vars[idx].value;
                } else {
                    rhs = normalize_float_ct(sf, rhs, lower_var_ctype(&vars[idx]), diag);
                    if (rhs < 0) {
                        return -1;
                    }
                    rhs = normalize_integral_ct(sf, rhs, e->ctype, vars[idx].type, diag);
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
                rhs = normalize_float_ct(sf, rhs, lower_global_ctype(g), diag);
                if (rhs < 0) {
                    return -1;
                }
                rhs = normalize_integral_ct(sf, rhs, e->ctype, g->type, diag);
                if (rhs < 0) {
                    return -1;
                }
                if (g->type == CC_TYPE_VOID && g->type_struct_id >= 0) {
                    if (emit_memcpy_instr(sf, gaddr, rhs, mem_size, diag) != 0) {
                        return -1;
                    }
                    return gaddr;
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
            long mem_size = lower_value_size(tu, e->lhs);
            int ptrv;
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
            if (e->lhs->kind == CC_EXPR_MEMBER && e->lhs->member_is_bitfield) {
                return lower_bitfield_store_to_addr(sf, e->lhs, ptrv, rhs, diag);
            }
            if (mem_size == 0) {
                /* Empty/opaque struct store: skip — there are no bytes to
                 * write. Linux's vDSO has zero-sized helper structs guarded
                 * by typeof macros that produce one of these. */
                return rhs;
            }
            if (mem_size < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "unsupported pointer store type size in lowering "
                         "(value_type=%d struct_id=%d kind=%d ms=%ld)",
                         (int)e->lhs->value_type, e->lhs->struct_id, (int)e->lhs->kind, mem_size);
                set_diag(diag, msg);
                return -1;
            }
            rhs = normalize_float_ct(sf, rhs, lower_value_ctype(e->lhs), diag);
            if (rhs < 0) {
                return -1;
            }
            if (lower_has_kind(e->lhs, CC_CT_LDOUBLE) && e->rhs != NULL && !lower_has_kind(e->rhs, CC_CT_LDOUBLE)) {
                /* stored through a pointer or member as a long double: an int or double is widened first */
                rhs = promote_f64_to_ldouble_value(sf, cast_value(sf, rhs, CC_VAL_F64, diag), diag);
                if (rhs < 0) {
                    return -1;
                }
            }
            rhs = normalize_int_ct(sf, rhs, lower_value_ctype(e), diag);
            if (rhs < 0) {
                return -1;
            }
            if (lower_record_id(e->lhs) >= 0) {
                if (emit_memcpy_instr(sf, ptrv, rhs, mem_size, diag) != 0) {
                    return -1;
                }
                return ptrv;
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
        cc_value_type_t want = lower_value_kind(e);
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
                    in.is_unsigned = ct_loads_unsigned(lower_var_ctype(&vars[idx])) ? 1 : 0;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory loading static local for ++/--");
                        return -1;
                    }
                    cur = in.dst;
                } else if (vars[idx].addr_value >= 0 && !var_is_aggregate_or_array(tu, &vars[idx])) {
                    long mem_size = vars[idx].addr_size > 0 ? vars[idx].addr_size : var_object_size_bytes(tu, &vars[idx]);
                    if (mem_size <= 0) {
                        set_diag(diag, "unsupported address-taken local ++/-- type");
                        return -1;
                    }
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_LOAD;
                    in.dst = new_value(sf, want);
                    in.lhs = vars[idx].addr_value;
                    in.rhs = -1;
                    in.imm = mem_size;
                    in.is_unsigned = ct_loads_unsigned(lower_var_ctype(&vars[idx])) ? 1 : 0;
                    if (in.dst < 0 || push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory loading address-taken local for ++/--");
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

                if (vars[idx].vla_is_pointer) {
                    /* a pointer to a variable-length array steps by the array's run-time size */
                    one = vars[idx].vla_strides[0];
                } else {
                    one = emit_const_i64_instr(sf, step);
                }
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
                    nextv = normalize_float_ct(sf, nextv, lower_var_ctype(&vars[idx]), diag);
                    if (nextv < 0) {
                        return -1;
                    }
                }
                if (cc_type_is_integral(vars[idx].type) && !is_pointer_type(vars[idx].type)) {
                    nextv = normalize_integral_ct(sf, nextv, e->ctype, vars[idx].type, diag);
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
                } else if (vars[idx].addr_value >= 0 && !var_is_aggregate_or_array(tu, &vars[idx])) {
                    long mem_size = vars[idx].addr_size > 0 ? vars[idx].addr_size : var_object_size_bytes(tu, &vars[idx]);
                    if (mem_size <= 0) {
                        set_diag(diag, "unsupported address-taken local ++/-- store type");
                        return -1;
                    }
                    memset(&in, 0, sizeof(in));
                    in.op = CC_SSA_STORE;
                    in.dst = -1;
                    in.lhs = vars[idx].addr_value;
                    in.rhs = nextv;
                    in.imm = mem_size;
                    if (push_instr(sf, in) != 0) {
                        set_diag(diag, "out of memory storing address-taken local ++/-- result");
                        return -1;
                    }
                } else if (emit_mov_instr(sf, vars[idx].value, nextv) != 0) {
                    return -1;
                }
                if (e->update_postfix) {
                    return cur;
                }
                if (vars[idx].is_static_storage || vars[idx].addr_value >= 0) {
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
                load_in.is_unsigned = ct_loads_unsigned(lower_global_ctype(g)) ? 1 : 0;
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
                    nextv = normalize_float_ct(sf, nextv, lower_global_ctype(g), diag);
                    if (nextv < 0) {
                        return -1;
                    }
                }
                if (cc_type_is_integral(g->type) && !is_pointer_type(g->type)) {
                    nextv = normalize_integral_ct(sf, nextv, e->ctype, g->type, diag);
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
            long mem_size = lower_value_size(tu, e->lhs);
            int ptrv;

            if (lower_is_pointer(e)) {
                step = lower_pointee_size(tu, e);
                if (step <= 0) {
                    step = lower_pointer_step(tu, e);
                }
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
            if (e->lhs->kind == CC_EXPR_MEMBER && e->lhs->member_is_bitfield) {
                cur = lower_bitfield_load_from_addr(sf, e->lhs, ptrv, diag);
                if (cur < 0) {
                    return -1;
                }
                one = emit_const_i64_instr(sf, 1);
                if (one < 0) {
                    return -1;
                }
                one = cast_value(sf, one, want, diag);
                if (one < 0) {
                    return -1;
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
                nextv = normalize_int_ct(sf, nextv, lower_value_ctype(e), diag);
                if (nextv < 0) {
                    return -1;
                }
                if (lower_bitfield_store_to_addr(sf, e->lhs, ptrv, nextv, diag) < 0) {
                    return -1;
                }
                if (e->update_postfix) {
                    return cur;
                }
                return nextv;
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
            in.is_unsigned = lower_loads_unsigned(e) ? 1 : 0;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                return -1;
            }
            cur = in.dst;

            one = emit_const_i64_instr(sf, step);
            if (one < 0) {
                return -1;
            }
            if (!lower_is_pointer(e)) {
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
            if (lower_has_kind(e, CC_CT_FLOAT)) {
                nextv = normalize_float_ct(sf, nextv, lower_value_ctype(e), diag);
                if (nextv < 0) {
                    return -1;
                }
            }
            if (lower_is_integer(e) && !lower_is_pointer(e)) {
                nextv = normalize_int_ct(sf, nextv, lower_value_ctype(e), diag);
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
        const cc_ctype_t *cast_ct = lower_cast_ctype(e);
        /* the structure the target is, or the hidden one behind a complex, wide, decimal or vector type */
        int to_sid = ct_record_id(cast_ct);
        int aggregate_cast_target = to_sid >= 0;
        if (e->lhs == NULL) {
            set_diag(diag, "malformed cast expression in lowering");
            return -1;
        }
        if (cast_src->kind == CC_EXPR_INIT_LIST && cast_ct != NULL && cast_ct->kind == CC_CT_ARRAY) {
            return lower_array_init_to_ptr(tu, sf, ctx, vars, var_count, depth, e->aux_type, e->aux_struct_id,
                                           e->array_ndim, e->array_dims, cast_src, diag);
        }
        if (complex_elem_of(tu, CC_TYPE_VOID, to_sid) != CC_TYPE_VOID && cast_src->kind != CC_EXPR_INIT_LIST) {
            return lower_complex_cast(tu, sf, ctx, vars, var_count, depth, e, diag);
        }
        {
            wide_t wd;
            wide_t ws;
            int to_wide = wide_of(tu, CC_TYPE_VOID, to_sid, &wd);
            int from_wide = cast_src->kind != CC_EXPR_INIT_LIST &&
                            wide_of(tu, CC_TYPE_VOID, lower_record_id(cast_src), &ws);

            if (from_wide && to_wide && to_sid == lower_record_id(cast_src)) {
                return lower_expr(tu, sf, ctx, vars, var_count, depth, cast_src, diag);
            }
            if ((to_wide && cast_src->kind != CC_EXPR_INIT_LIST) || from_wide) {
                return lower_wide_cast(tu, sf, ctx, vars, var_count, depth, e, diag);
            }
        }
        if (cast_src->kind != CC_EXPR_INIT_LIST && (dec_bits_of(tu, CC_TYPE_VOID, to_sid) != 0 ||
                                                    dec_bits_of(tu, CC_TYPE_VOID, lower_record_id(cast_src)) != 0)) {
            return lower_decimal_cast(tu, sf, ctx, vars, var_count, depth, e, diag);
        }
        if (cast_src->kind != CC_EXPR_INIT_LIST) {
            vec_t vd;
            vec_t vs;
            int to_vec = vec_of(tu, CC_TYPE_VOID, to_sid, &vd);
            int from_vec = vec_of(tu, CC_TYPE_VOID, lower_record_id(cast_src), &vs);

            if (to_vec && from_vec && to_sid == lower_record_id(cast_src)) {
                return lower_expr(tu, sf, ctx, vars, var_count, depth, cast_src, diag);
            }
            if (to_vec || from_vec) {
                return lower_vector_cast(tu, sf, ctx, vars, var_count, depth, e, diag);
            }
        }
        if (aggregate_cast_target) {
            const cc_struct_def_t *target_sd;
            int dst_ptr;
            int storesrc;
            long store_size;
            long sz = cc_ct_size(cast_ct, tu);
            if ((size_t)to_sid >= tu->struct_count) {
                set_diag(diag, "invalid aggregate cast target in lowering");
                return -1;
            }
            target_sd = &tu->structs[to_sid];
            if (sz <= 0) {
                sz = 1;
            }
            dst_ptr = emit_local_storage_alloc(sf, sz, diag);
            if (dst_ptr < 0) {
                return -1;
            }
            if (cast_src->kind == CC_EXPR_INIT_LIST) {
                if (emit_memset_instr(sf, dst_ptr, 0, sz, diag) != 0) {
                    return -1;
                }
                if (lower_struct_init_to_ptr(tu, sf, ctx, vars, var_count, depth, dst_ptr, to_sid, cast_src,
                                             diag) != 0) {
                    return -1;
                }
                return dst_ptr;
            }
            if (!target_sd->is_union) {
                if (lower_record_id(cast_src) < 0) {
                    set_diag(diag, "struct cast lowering requires aggregate source");
                    return -1;
                }
                storesrc = lower_expr(tu, sf, ctx, vars, var_count, depth, cast_src, diag);
                if (storesrc < 0) {
                    return -1;
                }
                if (emit_memcpy_instr(sf, dst_ptr, storesrc, sz, diag) != 0) {
                    return -1;
                }
                return dst_ptr;
            }
            {
                const cc_struct_member_t *m = cc_ct_union_cast_member(tu, to_sid, lower_value_ctype(cast_src));
                cc_ssa_instr_t st;
                if (m == NULL) {
                    set_diag(diag, "union cast lowering found no compatible member");
                    return -1;
                }
                storesrc = lower_expr(tu, sf, ctx, vars, var_count, depth, cast_src, diag);
                if (storesrc < 0) {
                    return -1;
                }
                storesrc = cast_value(sf, storesrc, ct_value_kind(lower_member_ctype(m)), diag);
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
        if (cast_ct == NULL || cast_ct->kind == CC_CT_VOID) {
            return v;
        }
        v = cast_value(sf, v, lower_value_kind(e), diag);
        if (v < 0) {
            return -1;
        }
        if (cast_ct->kind == CC_CT_DOUBLE || cast_ct->kind == CC_CT_FLOAT) {
            v = demote_ldouble_to_f64_value(sf, v, diag);
            if (v < 0) {
                return -1;
            }
        }
        if (cast_ct->kind == CC_CT_LDOUBLE) {
            if (lower_is_integer(cast_src) && !lower_is_pointer(cast_src)) {
                set_value_size(sf, v, 16);
            } else {
                v = promote_f64_to_ldouble_value(sf, v, diag);
                if (v < 0) {
                    return -1;
                }
            }
        }
        if (cc_ct_is_integer(cast_ct)) {
            v = normalize_int_ct(sf, v, cast_ct, diag);
            if (v < 0) {
                return -1;
            }
        } else if (cast_ct->kind == CC_CT_FLOAT) {
            v = normalize_float_ct(sf, v, cast_ct, diag);
            if (v < 0) {
                return -1;
            }
        }
        return v;
    }

    case CC_EXPR_SIZEOF: {
        long n;
        {
            /* a variable-length array, or an element of one: its size was computed at the declaration */
            int level;
            int vidx = vla_subscript_level(vars, var_count, depth, e->lhs, &level);

            if (vidx >= 0 && !(level == 0 && vars[vidx].vla_is_pointer)) {
                int size_v = level == 0 ? vars[vidx].vla_bytes : vars[vidx].vla_strides[level - 1];

                return cast_value(sf, size_v, lower_value_kind(e), diag);
            }
        }
        if (e->lhs != NULL) {
            n = sizeof_expr_bytes(tu, vars, var_count, depth, e->lhs);
        } else {
            n = sizeof_type_name_bytes(tu, e);
        }
        if (n < 0) {
            set_diag(diag, "unsupported sizeof operand in lowering");
            return -1;
        }
        in.op = CC_SSA_CONST;
        in.dst = new_value(sf, CC_VAL_I64);
        in.imm = n;
        in.is_unsigned = 1;
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
                lvars[j].name = cc_strdup(vars[j].name);
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
                    lvars[j].static_sym = cc_strdup(vars[j].static_sym);
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
            outv = cast_value(sf, outv, lower_value_kind(e), diag);
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
        long long cond_const = 0;
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
            cc_value_type_t want = lower_value_kind(e);
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
            sv = cast_value(sf, sv, want, diag);
            if (sv >= 0 && lower_is_integer(e) && !lower_is_pointer(e)) {
                sv = normalize_int_ct(sf, sv, lower_value_ctype(e), diag);
            }
            return sv;
        }

        cond_raw = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        if (cond_raw < 0) {
            return -1;
        }
        cond = lower_truthy_value(sf, cond_raw, diag);
        if (cond < 0) {
            return -1;
        }

        want = lower_value_kind(e);
        dst = new_value(sf, want);
        if (dst < 0) {
            set_diag(diag, "out of memory assigning ternary result");
            return -1;
        }
        if (lower_has_kind(e, CC_CT_LDOUBLE)) {
            set_value_size(sf, dst, 16);
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
        /* each arm is converted to the result type: (c ? s : ull) widens s */
        if (tv >= 0 && lower_is_integer(e) && !lower_is_pointer(e)) {
            tv = normalize_int_ct(sf, tv, lower_value_ctype(e), diag);
        }
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
        if (fv >= 0 && lower_is_integer(e) && !lower_is_pointer(e)) {
            fv = normalize_int_ct(sf, fv, lower_value_ctype(e), diag);
        }
        if (fv < 0 || emit_mov_instr(sf, dst, fv) != 0 || emit_br_instr(sf, l_end) != 0) {
            return -1;
        }

        if (emit_label_instr(sf, l_end) != 0) {
            return -1;
        }
        if (want == CC_VAL_I64) {
            long result_size = lower_value_size(tu, e);
            if ((result_size <= 0 && lower_is_pointer(e)) ||
                (lower_record_id(e) >= 0)) {
                /* a pointer, or an aggregate's value: its address */
                result_size = cc_target_pointer_size();
            }
            set_value_size(sf, dst, result_size > 0 ? result_size : cc_target_pointer_size());
            if (sf->value_is_unsigned != NULL && dst >= 0 && dst < sf->value_count) {
                sf->value_is_unsigned[dst] = lower_loads_unsigned(e) ? 1 : 0;
            }
        } else if (lower_has_kind(e, CC_CT_LDOUBLE)) {
            set_value_size(sf, dst, 16);
        } else {
            set_value_size(sf, dst, 8);
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

static int emit_const_i64_instr(cc_ssa_function_t *sf, long long v) {
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
    in.fimm_ld = (long double)v;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        return -1;
    }
    return in.dst;
}

static int emit_const_ldouble_special_instr(cc_ssa_function_t *sf, long code) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CONST;
    in.dst = new_value(sf, CC_VAL_F64);
    in.lhs = -1;
    in.rhs = -1;
    in.is_unsigned = 1;
    in.imm = code;
    if (code == 1) {
        in.fimm_ld = LDBL_MIN;
    } else if (code == 2) {
#ifdef __LDBL_DENORM_MIN__
        in.fimm_ld = __LDBL_DENORM_MIN__;
#else
        in.fimm_ld = 0.0L;
#endif
    } else if (code == 3) {
        in.fimm_ld = LDBL_MAX;
    }
    in.fimm = (double)in.fimm_ld;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        return -1;
    }
    set_value_size(sf, in.dst, 16);
    return in.dst;
}

static int emit_cmp_instr(cc_ssa_function_t *sf, cc_cmp_kind_t cmp_kind, int lhs, int rhs, int is_unsigned,
                          cc_diag_t *diag) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CMP;
    in.cmp_kind = cmp_kind;
    in.is_unsigned = is_unsigned ? 1 : 0;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = lhs;
    in.rhs = rhs;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory lowering compare");
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
    call_in.sym = cc_strdup("memcpy");
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

static int emit_memset_instr(cc_ssa_function_t *sf, int dst_ptr, int fill_byte, long size, cc_diag_t *diag) {
    cc_ssa_instr_t call_in;
    int bytev;
    int sizev;

    if (size <= 0) {
        set_diag(diag, "invalid memset size in lowering");
        return -1;
    }
    bytev = emit_const_i64_instr(sf, fill_byte);
    if (bytev < 0) {
        return -1;
    }
    sizev = emit_const_i64_instr(sf, size);
    if (sizev < 0) {
        return -1;
    }

    memset(&call_in, 0, sizeof(call_in));
    call_in.op = CC_SSA_CALL;
    call_in.call_is_variadic = 0;
    call_in.sym = cc_strdup("memset");
    call_in.arg_count = 3;
    call_in.args = (int *)calloc(3, sizeof(*call_in.args));
    call_in.dst = new_value(sf, CC_VAL_I64);
    if (call_in.sym == NULL || call_in.args == NULL || call_in.dst < 0) {
        free(call_in.sym);
        free(call_in.args);
        set_diag(diag, "out of memory emitting memset call");
        return -1;
    }
    call_in.args[0] = dst_ptr;
    call_in.args[1] = bytev;
    call_in.args[2] = sizev;
    if (push_instr(sf, call_in) != 0) {
        free(call_in.sym);
        free(call_in.args);
        set_diag(diag, "out of memory appending memset call");
        return -1;
    }
    return 0;
}

/*
 * A floating value converted to _Bool is whether it is non-zero (0.5
 * is true); truncating it to an integer first would make it false.
 */
static int bool_from_floating(cc_ssa_function_t *sf, int v, cc_type_t t, cc_diag_t *diag) {
    if (t != CC_TYPE_BOOL || value_type(sf, v) != CC_VAL_F64) {
        return v;
    }
    return lower_truthy_value(sf, v, diag);
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
        if (zero >= 0 && value_size(sf, v) >= 8) {
            set_value_size(sf, zero, 8);
        }
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

/**
 * The floating type a floating builtin's operand is stored and inspected
 * as: float, long double, or double for anything else (the integer and
 * double operands, held as doubles).
 *
 * @param e  the operand
 * @return   CC_TYPE_FLOAT, CC_TYPE_LDOUBLE or CC_TYPE_DOUBLE
 */
static cc_type_t fp_builtin_storage_type(const cc_expr_t *e) {
    if (lower_has_kind(e, CC_CT_FLOAT)) {
        return CC_TYPE_FLOAT;
    }
    if (lower_has_kind(e, CC_CT_LDOUBLE)) {
        return CC_TYPE_LDOUBLE;
    }
    return CC_TYPE_DOUBLE;
}

static int lower_fp_builtin_arg(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *arg,
                                cc_type_t *out_storage_type, cc_diag_t *diag) {
    int v;
    cc_type_t store_t;

    if (arg == NULL) {
        set_diag(diag, "floating builtin missing operand");
        return -1;
    }
    v = lower_expr(tu, sf, ctx, vars, var_count, depth, arg, diag);
    if (v < 0) {
        return -1;
    }
    store_t = fp_builtin_storage_type(arg);
    if (value_type(sf, v) != CC_VAL_F64) {
        v = cast_value(sf, v, CC_VAL_F64, diag);
        if (v < 0) {
            return -1;
        }
    }
    if (store_t == CC_TYPE_LDOUBLE) {
        v = promote_f64_to_ldouble_value(sf, v, diag);
        if (v < 0) {
            return -1;
        }
    }
    v = normalize_float_value(sf, v, store_t, diag);
    if (v < 0) {
        return -1;
    }
    if (out_storage_type != NULL) {
        *out_storage_type = store_t;
    }
    return v;
}

static int emit_fp_signbit_from_value(cc_ssa_function_t *sf, int v, cc_type_t store_t, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    long store_size;
    int tmp_ptr;
    int bits;
    int mask;
    int masked;
    int zero;

    store_size = store_t == CC_TYPE_FLOAT ? 4 : (store_t == CC_TYPE_LDOUBLE ? 16 : 8);
    tmp_ptr = emit_local_storage_alloc(sf, store_size, diag);
    if (tmp_ptr < 0) {
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_STORE;
    in.lhs = tmp_ptr;
    in.rhs = v;
    in.imm = store_size;
    if (push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory storing floating signbit operand");
        return -1;
    }

    {
        /* the sign is the top bit of the last byte: float 3, double 7, long double 9 (80-bit) */
        int off = emit_const_i64_instr(sf, store_size == 4 ? 3 : (store_size == 16 ? 9 : 7));
        if (off < 0) {
            return -1;
        }
        tmp_ptr = emit_i64_binop_instr(sf, CC_SSA_ADD, tmp_ptr, off, 1, diag);
        if (tmp_ptr < 0) {
            return -1;
        }
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LOAD;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = tmp_ptr;
    in.imm = 1;
    in.is_unsigned = 1;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory loading floating signbit bits");
        return -1;
    }
    bits = in.dst;

    mask = emit_const_u64_instr(sf, 0x80ULL, diag);
    if (mask < 0) {
        return -1;
    }
    masked = emit_i64_binop_instr(sf, CC_SSA_AND, bits, mask, 0, diag);
    if (masked < 0) {
        return -1;
    }
    zero = emit_const_i64_instr(sf, 0);
    if (zero < 0) {
        return -1;
    }
    return emit_cmp_instr(sf, CC_CMP_NE, masked, zero, 1, diag);
}

static int emit_fp_negate_from_value(cc_ssa_function_t *sf, int v, cc_type_t store_t, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    long store_size = store_t == CC_TYPE_FLOAT ? 4 : (store_t == CC_TYPE_LDOUBLE ? 16 : 8);
    int tmp_ptr;
    int bit_ptr;
    int bits;
    int mask;
    int xored;

    tmp_ptr = emit_local_storage_alloc(sf, store_size, diag);
    if (tmp_ptr < 0) {
        return -1;
    }
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_STORE;
    in.lhs = tmp_ptr;
    in.rhs = v;
    in.imm = store_size;
    if (push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory storing floating negation operand");
        return -1;
    }

    bit_ptr = tmp_ptr;
    if (store_size == 16) {
        int off = emit_const_i64_instr(sf, 9);
        if (off < 0) {
            return -1;
        }
        bit_ptr = emit_i64_binop_instr(sf, CC_SSA_ADD, tmp_ptr, off, 1, diag);
        if (bit_ptr < 0) {
            return -1;
        }
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LOAD;
    in.dst = new_value(sf, CC_VAL_I64);
    in.lhs = bit_ptr;
    in.imm = store_size == 16 ? 1 : store_size;
    in.is_unsigned = 1;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory loading floating negation bits");
        return -1;
    }
    bits = in.dst;
    mask = emit_const_u64_instr(
        sf, store_size == 4 ? 0x80000000ULL : (store_size == 16 ? 0x80ULL : 0x8000000000000000ULL), diag);
    if (mask < 0) {
        return -1;
    }
    xored = emit_i64_binop_instr(sf, CC_SSA_XOR, bits, mask, 1, diag);
    if (xored < 0) {
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_STORE;
    in.lhs = bit_ptr;
    in.rhs = xored;
    in.imm = store_size == 16 ? 1 : store_size;
    if (push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory storing floating negation bits");
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LOAD;
    in.dst = new_value(sf, CC_VAL_F64);
    in.lhs = tmp_ptr;
    in.imm = store_size;
    if (in.dst < 0 || push_instr(sf, in) != 0) {
        set_diag(diag, "out of memory loading floating negation result");
        return -1;
    }
    if (store_size == 16) {
        set_value_size(sf, in.dst, 16);
    }
    return normalize_float_value(sf, in.dst, store_t, diag);
}

static int emit_fp_isinf_sign_from_value(cc_ssa_function_t *sf, int v, cc_type_t store_t, cc_diag_t *diag) {
    int pos_inf;
    int neg_inf;
    int pos_cmp;
    int neg_cmp;

    pos_inf = emit_const_f64_instr(sf, INFINITY);
    neg_inf = emit_const_f64_instr(sf, -INFINITY);
    if (pos_inf < 0 || neg_inf < 0) {
        return -1;
    }
    pos_inf = normalize_float_value(sf, pos_inf, store_t, diag);
    neg_inf = normalize_float_value(sf, neg_inf, store_t, diag);
    if (pos_inf < 0 || neg_inf < 0) {
        return -1;
    }
    pos_cmp = emit_cmp_instr(sf, CC_CMP_EQ, v, pos_inf, 0, diag);
    neg_cmp = emit_cmp_instr(sf, CC_CMP_EQ, v, neg_inf, 0, diag);
    if (pos_cmp < 0 || neg_cmp < 0) {
        return -1;
    }
    return emit_i64_binop_instr(sf, CC_SSA_SUB, pos_cmp, neg_cmp, 0, diag);
}

typedef struct {
    const cc_stmt_t *stmt;
    long long value;
    long long value_hi;
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
static int find_struct_member_recursive(const cc_translation_unit_t *tu, int struct_id, const char *name,
                                        const cc_struct_member_t **out_member, long *out_off);
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
    size_t i;

    if (e == NULL || e->kind != CC_EXPR_GENERIC) {
        return NULL;
    }
    if (e->args == NULL || e->arg_count == 0) {
        return NULL;
    }
    if (e->generic_selected >= 0 && (size_t)e->generic_selected < e->arg_count) {
        return e->args[e->generic_selected];
    }
    if (e->generic_is_default != NULL && e->generic_count == e->arg_count) {
        for (i = 0; i < e->generic_count; ++i) {
            if (e->generic_is_default[i]) {
                return e->args[i];
            }
        }
    }
    return e->args[0];
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
    if (lower_is_record(e, struct_id)) {
        return 1;
    }
    if (struct_id >= 0 && lower_cast_record_id(e) == struct_id && e->lhs != NULL && lower_is_record(e->lhs, struct_id)) {
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
    wide = cc_string_literal_is_wide(item);
    if (!wide && !cc_ct_is_character(cc_ct_from_scalar(elem_type))) {
        return -1;
    }
    if (wide && !cc_type_is_integral(elem_type)) {
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
            if (item->args[ii]->kind != CC_EXPR_INIT_LIST && lower_is_record(item->args[ii], m->type_struct_id)) {
                /* a value of the element's structure type (a decimal constant, say): copied in */
                int src = lower_expr(tu, sf, ctx, vars, var_count, depth, item->args[ii], diag);

                if (src < 0 || emit_memcpy_instr(sf, elem_ptr, src, elem_size, diag) != 0) {
                    return -1;
                }
                continue;
            }
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

static int lower_store_string_to_array_ptr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, cc_type_t array_type,
                                           int struct_id, int array_ndim, const long array_dims[CC_MAX_ARRAY_DIMS],
                                           int base_ptr, const cc_expr_t *item, cc_diag_t *diag) {
    cc_type_t elem_type;
    long elem_size;
    long max_items;
    unsigned long *units = NULL;
    size_t unit_count = 0;
    size_t i;
    int wide;

    if (item == NULL || item->kind != CC_EXPR_STR || !is_pointer_type(array_type) || array_ndim != 1) {
        set_diag(diag, "invalid string compound-literal array initializer");
        return -1;
    }

    elem_type = ptr_base_type(array_type);
    elem_size = array_decl_scalar_size_bytes(tu, array_type, struct_id, array_ndim);
    if (elem_size <= 0) {
        elem_size = pointer_elem_size_bytes(tu, array_type, struct_id);
    }
    max_items = array_dims != NULL && array_dims[0] > 0 ? array_dims[0] : 1;
    if (elem_size <= 0 || max_items <= 0) {
        set_diag(diag, "unsupported string compound-literal array initializer");
        return -1;
    }

    wide = cc_string_literal_is_wide(item);
    if (!wide && !cc_ct_is_character(cc_ct_from_scalar(elem_type))) {
        set_diag(diag, "narrow string initializer requires character array compound literal");
        return -1;
    }
    if (wide && !cc_type_is_integral(elem_type)) {
        set_diag(diag, "wide string initializer requires integral array compound literal");
        return -1;
    }

    if (decode_string_units(item, wide, &units, &unit_count) != 0) {
        set_diag(diag, "failed to decode compound-literal string initializer");
        return -1;
    }
    if ((long)unit_count > max_items) {
        free(units);
        set_diag(diag, "string initializer exceeds compound-literal array size");
        return -1;
    }

    for (i = 0; i < unit_count + (unit_count < (size_t)max_items ? 1 : 0); ++i) {
        cc_ssa_instr_t in;
        int elem_ptr = base_ptr;
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
            in.lhs = base_ptr;
            in.rhs = offv;
            if (in.dst < 0 || push_instr(sf, in) != 0) {
                free(units);
                set_diag(diag, "out of memory computing compound-literal string element address");
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
            set_diag(diag, "out of memory storing compound-literal string element");
            return -1;
        }
    }

    free(units);
    return 0;
}

static int lower_array_init_to_ptr_cursor(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                          var_entry_t *vars, size_t var_count, int depth, cc_type_t array_type,
                                          int struct_id, int array_ndim, const long *array_dims, const cc_expr_t *list,
                                          size_t *cursor, int base_ptr, cc_diag_t *diag) {
    cc_type_t elem_type;
    long elem_size;
    long max_items;
    long i;

    if (tu == NULL || sf == NULL || array_dims == NULL || list == NULL || list->kind != CC_EXPR_INIT_LIST ||
        cursor == NULL || !is_pointer_type(array_type) || array_ndim <= 0) {
        set_diag(diag, "invalid compound-literal array initializer");
        return -1;
    }

    elem_type = ptr_base_type(array_type);
    elem_size = array_decl_scalar_size_bytes(tu, array_type, struct_id, array_ndim);
    if (elem_size <= 0) {
        elem_size = pointer_elem_size_bytes(tu, array_type, struct_id);
    }
    max_items = array_dims[0] > 0 ? array_dims[0] : 1;
    if (elem_size <= 0 || max_items <= 0) {
        set_diag(diag, "unsupported compound-literal array element type");
        return -1;
    }

    if (array_ndim == 1) {
        if (*cursor == 0 && list->arg_count == 1) {
            const cc_expr_t *only = unwrap_scalar_initializer_expr(list->args[0], diag);
            if (only == NULL) {
                return -1;
            }
            if (only->kind == CC_EXPR_STR) {
                /* `{"text"}`: the string fills the array, and is the list's one element, now consumed */
                if (lower_store_string_to_array_ptr(tu, sf, array_type, struct_id, array_ndim, array_dims, base_ptr,
                                                    only, diag) != 0) {
                    return -1;
                }
                *cursor = 1;
                return 0;
            }
        }

        for (i = 0; i < max_items && *cursor < list->arg_count; ++i) {
            const cc_expr_t *raw = list->args[*cursor];
            int elem_ptr = base_ptr;
            cc_ssa_instr_t in;

            if (i > 0) {
                int offv = emit_const_i64_instr(sf, i * elem_size);
                if (offv < 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_ADD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = base_ptr;
                in.rhs = offv;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory computing compound-literal array element address");
                    return -1;
                }
                elem_ptr = in.dst;
            }

            if (elem_type == CC_TYPE_VOID && struct_id >= 0) {
                const cc_expr_t *elem_init = raw;
                if (struct_id >= 0 && lower_cast_record_id(elem_init) == struct_id && elem_init->lhs != NULL &&
                    elem_init->lhs->kind == CC_EXPR_INIT_LIST) {
                    elem_init = elem_init->lhs;
                }
                if (elem_init == NULL || elem_init->kind != CC_EXPR_INIT_LIST) {
                    if (is_zero_initializer_expr(raw)) {
                        (*cursor)++;
                        continue;
                    }
                    set_diag(diag, "struct compound-literal array element requires braces");
                    return -1;
                }
                if (lower_struct_init_to_ptr(tu, sf, ctx, vars, var_count, depth, elem_ptr, struct_id, elem_init,
                                             diag) != 0) {
                    return -1;
                }
            } else {
                const cc_expr_t *elem_expr = unwrap_scalar_initializer_expr(raw, diag);
                int v;
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
                    set_diag(diag, "out of memory storing compound-literal array element");
                    return -1;
                }
            }

            (*cursor)++;
        }
        return 0;
    }

    {
        cc_type_t sub_type = ptr_base_type(array_type);
        int sub_sid = (sub_type == CC_TYPE_VOID || is_pointer_type(sub_type)) ? struct_id : -1;
        long sub_size = array_dims_step_size_bytes(tu, sub_type, sub_sid, array_ndim - 1, array_dims + 1, 0, elem_size);

        if (sub_size <= 0) {
            set_diag(diag, "unsupported nested compound-literal array stride");
            return -1;
        }

        for (i = 0; i < max_items && *cursor < list->arg_count; ++i) {
            const cc_expr_t *raw = list->args[*cursor];
            int sub_ptr = base_ptr;
            cc_ssa_instr_t in;

            if (i > 0) {
                int offv = emit_const_i64_instr(sf, i * sub_size);
                if (offv < 0) {
                    return -1;
                }
                memset(&in, 0, sizeof(in));
                in.op = CC_SSA_ADD;
                in.dst = new_value(sf, CC_VAL_I64);
                in.lhs = base_ptr;
                in.rhs = offv;
                if (in.dst < 0 || push_instr(sf, in) != 0) {
                    set_diag(diag, "out of memory computing nested compound-literal array address");
                    return -1;
                }
                sub_ptr = in.dst;
            }

            if (raw != NULL && raw->kind == CC_EXPR_STR && array_ndim == 2) {
                cc_type_t row_scalar_type = sub_type;
                int rd;
                for (rd = 0; rd < array_ndim - 1 && is_pointer_type(row_scalar_type); ++rd) {
                    row_scalar_type = ptr_base_type(row_scalar_type);
                }
                if (!is_pointer_type(row_scalar_type)) {
                    if (lower_store_string_to_array_ptr(tu, sf, sub_type, sub_sid, array_ndim - 1, array_dims + 1,
                                                       sub_ptr, raw, diag) != 0) {
                        return -1;
                    }
                    (*cursor)++;
                    continue;
                }
            }

            if (raw != NULL && raw->kind == CC_EXPR_INIT_LIST) {
                size_t sub_cur = 0;
                (*cursor)++;
                if (lower_array_init_to_ptr_cursor(tu, sf, ctx, vars, var_count, depth, sub_type, sub_sid,
                                                   array_ndim - 1, array_dims + 1, raw, &sub_cur, sub_ptr, diag) != 0) {
                    return -1;
                }
                if (sub_cur < raw->arg_count) {
                    set_diag(diag, "too many nested compound-literal array initializers");
                    return -1;
                }
            } else {
                if (lower_array_init_to_ptr_cursor(tu, sf, ctx, vars, var_count, depth, sub_type, sub_sid,
                                                   array_ndim - 1, array_dims + 1, list, cursor, sub_ptr, diag) != 0) {
                    return -1;
                }
            }
        }
    }

    return 0;
}

static cc_type_t array_scalar_type_after_dims(cc_type_t type, int ndim) {
    while (ndim-- > 0 && is_pointer_type(type)) {
        type = ptr_base_type(type);
    }
    return type;
}

static int lower_array_init_to_ptr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                   var_entry_t *vars, size_t var_count, int depth, cc_type_t array_type, int struct_id,
                                   int array_ndim, const long array_dims[CC_MAX_ARRAY_DIMS], const cc_expr_t *init,
                                   cc_diag_t *diag) {
    long total_size;
    int base_ptr;
    size_t cursor = 0;

    if (tu == NULL || sf == NULL || init == NULL || init->kind != CC_EXPR_INIT_LIST || !is_pointer_type(array_type) ||
        array_ndim <= 0) {
        set_diag(diag, "invalid compound-literal array expression");
        return -1;
    }

    total_size = array_type_size_bytes(tu, array_type, struct_id, array_ndim, array_dims);
    if (total_size <= 0) {
        set_diag(diag, "unsupported compound-literal array size in lowering");
        return -1;
    }

    base_ptr = emit_local_storage_alloc(sf, total_size, diag);
    if (base_ptr < 0) {
        return -1;
    }
    if (emit_memset_instr(sf, base_ptr, 0, total_size, diag) != 0) {
        return -1;
    }
    if (lower_array_init_to_ptr_cursor(tu, sf, ctx, vars, var_count, depth, array_type, struct_id, array_ndim,
                                       array_dims, init, &cursor, base_ptr, diag) != 0) {
        return -1;
    }
    if (cursor < init->arg_count) {
        set_diag(diag, "too many compound-literal array initializers");
        return -1;
    }

    return base_ptr;
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
        const cc_struct_member_t *nested_m = NULL;
        int is_nested_designator = 0;
        long nested_off = 0;
        long field_off = 0;
        int field_ptr = base_ptr;
        cc_ssa_instr_t in;

        if (raw != NULL && raw->kind == CC_EXPR_MEMBER && raw->lhs == NULL && raw->rhs != NULL && raw->ident != NULL) {
            int didx = find_struct_member_index_by_name(sd, raw->ident);
            if (didx < 0) {
                if (find_struct_member_recursive(tu, struct_id, raw->ident, &nested_m, &nested_off) == 0) {
                    is_nested_designator = 1;
                }
            }
            if (didx < 0 && !is_nested_designator) {
                char msg[160];
                snprintf(msg, sizeof(msg), "unknown designated struct member '%s' in local initializer",
                         raw->ident);
                set_diag(diag, msg);
                return -1;
            }
            if (!is_nested_designator) {
                member_idx = (size_t)didx;
            }
            item = raw->rhs;
        }
        if (!is_nested_designator && member_idx >= sd->member_count) {
            break;
        }
        m = is_nested_designator ? nested_m : &sd->members[member_idx];
        if (!is_nested_designator && !sd->is_union) {
            next_member = struct_next_init_member_index(sd, member_idx);
        }
        if (!is_nested_designator && sd->has_flexible_array && member_idx + 1 == sd->member_count) {
            /* GNU-compatible extension: accept and ignore flexible-array initializers. */
            continue;
        }
        field_off = is_nested_designator ? nested_off : m->offset;
        if (field_off != 0) {
            int offv = emit_const_i64_instr(sf, field_off);
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
            if (lower_cast_record_id(item) == m->type_struct_id && item->lhs != NULL &&
                item->lhs->kind == CC_EXPR_INIT_LIST) {
                nested_item = item->lhs;
            }
            nested_item = unwrap_self_designated_init_list(nested_item, m->name);
            if (nested_item->kind != CC_EXPR_INIT_LIST) {
                if (lower_is_record(nested_item, m->type_struct_id)) {
                    int rhsv;
                    long sub_sz = type_size_bytes_with_struct(tu, CC_TYPE_VOID, m->type_struct_id);
                    const cc_expr_t *copy_expr = nested_item;
                    if (lower_cast_record_id(copy_expr) == m->type_struct_id && copy_expr->lhs != NULL &&
                        lower_is_record(copy_expr->lhs, m->type_struct_id)) {
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
            if (m->is_bitfield) {
                cc_expr_t bf;
                memset(&bf, 0, sizeof(bf));
                bf.value_type = m->type;
                bf.ctype = lower_member_ctype(m);
                bf.member_is_bitfield = 1;
                bf.member_bit_width = m->bit_width;
                bf.member_bit_offset = m->bit_offset;
                bf.member_bit_storage_bits = m->bit_storage_bits;
                bf.member_bit_storage_size = m->bit_storage_size;
                bf.member_bit_signed = m->bit_signed;
                if (lower_bitfield_store_to_addr(sf, &bf, field_ptr, v, diag) < 0) {
                    return -1;
                }
                continue;
            }
            v = cast_value(sf, v, ct_value_kind(lower_member_ctype(m)), diag);
            if (v >= 0 && m->type == CC_TYPE_LDOUBLE && !lower_has_kind(item_expr, CC_CT_LDOUBLE)) {
                /* a long double member from an int or double: widened, not stored as the double's bytes */
                v = promote_f64_to_ldouble_value(sf, v, diag);
            }
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

/** The shape of a variable-length array, or of the one a pointer points to. */
typedef struct {
    cc_type_t type;                          /**< the declared type, legacy encoding */
    int struct_id;                           /**< its struct id */
    int ndim;                                /**< the array's rank */
    const long *dims;                        /**< its constant bounds, outermost first; 0 where run-time */
    cc_expr_t *const *bounds;                /**< its run-time bounds; NULL where constant */
    int is_pointer;                          /**< the declaration is a pointer to the array */
} vla_shape_t;

/**
 * Compute a variable-length array's size and strides where it is declared
 * (a pointer parameter's, at function entry).
 *
 * Each bound is evaluated, outermost first, and each constant bound taken
 * from the declaration; then, from the innermost dimension outward, the
 * stride of dimension k (the size of one of its elements) is the element
 * size times the bounds inside it.  For a pointer to the array the strides
 * are the array's size followed by the array's strides, a pointer being
 * one level further out.  The strides are what indexing and sizeof use
 * (see vla_subscript_level()).
 *
 * @param tu         the translation unit
 * @param sf         the function
 * @param ctx        the lowering context
 * @param vars       the variables in scope
 * @param var_count  their number
 * @param depth      the block depth
 * @param shape      the array
 * @param strides    receives a value per level, -1 past the rank
 * @param out_rank   receives the number of strides
 * @param out_bytes  receives the array's size in bytes, a value
 * @param diag       receives an error
 * @return           0, or -1 on error
 */
static int lower_vla_layout(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                            var_entry_t *vars, size_t var_count, int depth, const vla_shape_t *shape,
                            int strides[CC_MAX_ARRAY_DIMS], int *out_rank, int *out_bytes, cc_diag_t *diag) {
    int dims[CC_MAX_ARRAY_DIMS];
    int own[CC_MAX_ARRAY_DIMS];
    int ndim = shape->ndim;
    int shift = shape->is_pointer ? 1 : 0;
    long elem_size;
    int k;

    for (k = 0; k < CC_MAX_ARRAY_DIMS; ++k) {
        strides[k] = -1;
    }
    if (ndim <= 0 || ndim + shift > CC_MAX_ARRAY_DIMS) {
        set_diag(diag, "unsupported variable-length array rank in lowering");
        return -1;
    }
    elem_size = array_decl_scalar_size_bytes(tu, shape->type, shape->struct_id, ndim + shift);
    if (elem_size <= 0) {
        set_diag(diag, "unsupported variable-length array element type in lowering");
        return -1;
    }
    for (k = 0; k < ndim; ++k) {
        if (shape->bounds[k] != NULL) {
            dims[k] = lower_expr(tu, sf, ctx, vars, var_count, depth, shape->bounds[k], diag);
            if (dims[k] >= 0) {
                dims[k] = cast_value(sf, dims[k], CC_VAL_I64, diag);
            }
        } else if (shape->dims[k] > 0) {
            dims[k] = emit_const_i64_instr(sf, shape->dims[k]);
        } else {
            set_diag(diag, "variable-length array dimension has no bound");
            return -1;
        }
        if (dims[k] < 0) {
            return -1;
        }
    }
    own[ndim - 1] = emit_const_i64_instr(sf, elem_size);
    if (own[ndim - 1] < 0) {
        return -1;
    }
    for (k = ndim - 2; k >= 0; --k) {
        own[k] = emit_i64_binop_instr(sf, CC_SSA_MUL, own[k + 1], dims[k + 1], 1, diag);
        if (own[k] < 0) {
            return -1;
        }
    }
    *out_bytes = emit_i64_binop_instr(sf, CC_SSA_MUL, own[0], dims[0], 1, diag);
    if (*out_bytes < 0) {
        return -1;
    }
    if (shape->is_pointer) {
        strides[0] = *out_bytes;
    }
    for (k = 0; k < ndim; ++k) {
        strides[k + shift] = own[k];
    }
    *out_rank = ndim + shift;
    return 0;
}

/**
 * Give a variable its variable-length array shape: compute the strides of
 * @p shape and record them, the rank and the size in the variable's entry.
 *
 * @param v      the variable
 * @param shape  the array it is, or points to
 * @return       0, or -1 on error
 * (the other parameters are lower_vla_layout()'s)
 */
static int lower_vla_attach(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                            var_entry_t *vars, size_t var_count, int depth, size_t v, const vla_shape_t *shape,
                            cc_diag_t *diag) {
    int strides[CC_MAX_ARRAY_DIMS];
    int rank;
    int bytes;

    if (lower_vla_layout(tu, sf, ctx, vars, var_count, depth, shape, strides, &rank, &bytes, diag) != 0) {
        return -1;
    }
    memcpy(vars[v].vla_strides, strides, sizeof(strides));
    vars[v].vla_rank = rank;
    vars[v].vla_is_pointer = shape->is_pointer;
    if (!shape->is_pointer) {
        vars[v].vla_bytes = bytes;
    }
    return 0;
}

/**
 * Build the environment a call passes to a GNU nested function that uses
 * the enclosing function's variables: an array holding the address of each
 * one cc_nested_outer_vars() lists, in that order, taken from the caller's
 * scope -- the enclosing function's own variables, or, in another nested
 * function, the addresses it was given.
 *
 * @param tu         the translation unit
 * @param sf         the calling function
 * @param vars       the caller's variables
 * @param var_count  their number
 * @param callee     the nested function
 * @param diag       receives an error
 * @return           the array's address, or -1 on error
 */
static int lower_nested_env(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, var_entry_t *vars,
                            size_t var_count, const cc_function_t *callee, cc_diag_t *diag) {
    cc_outer_var_t *outer;
    size_t n;
    size_t i;
    long ps = cc_target_pointer_size();
    int envv;

    if (cc_nested_outer_vars(tu, callee, &outer, &n) != 0) {
        set_diag(diag, "nested function's enclosing function is missing");
        return -1;
    }
    envv = emit_local_storage_alloc(sf, n > 0 ? (long)n * ps : ps, diag);
    for (i = 0; envv >= 0 && i < n; ++i) {
        size_t k = var_count;
        int idx = -1;
        int addr;
        int slot;
        cc_ssa_instr_t st;

        /* the enclosing function's top-level scope, not a block inside it */
        while (k > 0) {
            --k;
            if (vars[k].depth == 0 && strcmp(vars[k].name, outer[i].name) == 0) {
                idx = (int)k;
                break;
            }
        }
        if (idx < 0) {
            set_diag(diag, "nested function is called where a variable it may use is not in scope");
            envv = -1;
            break;
        }
        if (vars[idx].vla_rank > 0) {
            set_diag(diag, "a nested function cannot use a variable-length array of the function it is in");
            envv = -1;
            break;
        }
        if (vars[idx].is_static_storage) {
            addr = emit_global_addr(sf, vars[idx].static_sym, diag);
        } else {
            addr = ensure_local_var_addressable(tu, sf, &vars[idx], diag);
        }
        slot = addr < 0 ? -1 : emit_const_i64_instr(sf, (long long)i * ps);
        slot = slot < 0 ? -1 : emit_i64_binop_instr(sf, CC_SSA_ADD, envv, slot, 1, diag);
        if (slot < 0) {
            envv = -1;
            break;
        }
        memset(&st, 0, sizeof(st));
        st.op = CC_SSA_STORE;
        st.dst = -1;
        st.lhs = slot;
        st.rhs = addr;
        st.imm = ps;
        if (push_instr(sf, st) != 0) {
            set_diag(diag, "out of memory building a nested function's environment");
            envv = -1;
            break;
        }
    }
    free(outer);
    return envv;
}

/** Store the low @p size bytes of @p v at @p base + @p off. */
static int emit_store_at(cc_ssa_function_t *sf, int base, long off, int v, long size, cc_diag_t *diag) {
    cc_ssa_instr_t st;
    int addr = emit_const_i64_instr(sf, off);

    addr = addr < 0 ? -1 : emit_i64_binop_instr(sf, CC_SSA_ADD, base, addr, 1, diag);
    if (addr < 0) {
        return -1;
    }
    memset(&st, 0, sizeof(st));
    st.op = CC_SSA_STORE;
    st.dst = -1;
    st.lhs = addr;
    st.rhs = v;
    st.imm = size;
    if (push_instr(sf, st) != 0) {
        set_diag(diag, "out of memory writing a trampoline");
        return -1;
    }
    return 0;
}

/** Store the constant @p c, @p size bytes of it, at @p base + @p off. */
static int emit_store_const_at(cc_ssa_function_t *sf, int base, long off, long long c, long size, cc_diag_t *diag) {
    int v = emit_const_i64_instr(sf, c);

    return v < 0 ? -1 : emit_store_at(sf, base, off, v, size, diag);
}

/**
 * The address of a GNU nested function that uses the enclosing function's
 * variables: a trampoline in the caller's frame that loads the static chain
 * register with the environment (see lower_nested_env()) and jumps to the
 * function, as gcc builds one.  It lives as long as the caller's frame, and
 * the stack must be executable to run it (the module is marked so).
 *
 *   x86-64:  movabs $env, %r10; movabs $fn, %r11; jmp *%r11
 *   i386:    movl $env, %ecx; movl $fn, %eax; jmp *%eax
 *
 * @param tu         the translation unit
 * @param sf         the function taking the address
 * @param ctx        the lowering context (its module is marked)
 * @param vars       its variables
 * @param var_count  their number
 * @param callee     the nested function
 * @param diag       receives an error
 * @return           the trampoline's address, or -1 on error
 */
static int lower_nested_trampoline(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                   var_entry_t *vars, size_t var_count, const cc_function_t *callee,
                                   cc_diag_t *diag) {
    int envv = lower_nested_env(tu, sf, vars, var_count, callee, diag);
    int fnv = envv < 0 ? -1 : emit_global_addr(sf, callee->name, diag);
    int tramp = fnv < 0 ? -1 : emit_local_storage_alloc(sf, 32, diag);

    if (tramp < 0) {
        return -1;
    }
    if (cc_target_pointer_size() == 8) {
        if (emit_store_const_at(sf, tramp, 0, 0xba49, 2, diag) != 0 ||  /* movabs $imm64, %r10 */
            emit_store_at(sf, tramp, 2, envv, 8, diag) != 0 ||
            emit_store_const_at(sf, tramp, 10, 0xbb49, 2, diag) != 0 || /* movabs $imm64, %r11 */
            emit_store_at(sf, tramp, 12, fnv, 8, diag) != 0 ||
            emit_store_const_at(sf, tramp, 20, 0xff41, 2, diag) != 0 || /* jmp *%r11 */
            emit_store_const_at(sf, tramp, 22, 0xe3, 1, diag) != 0) {
            return -1;
        }
    } else if (emit_store_const_at(sf, tramp, 0, 0xb9, 1, diag) != 0 || /* movl $imm32, %ecx */
               emit_store_at(sf, tramp, 1, envv, 4, diag) != 0 ||
               emit_store_const_at(sf, tramp, 5, 0xb8, 1, diag) != 0 || /* movl $imm32, %eax */
               emit_store_at(sf, tramp, 6, fnv, 4, diag) != 0 ||
               emit_store_const_at(sf, tramp, 10, 0xe0ff, 2, diag) != 0) { /* jmp *%eax */
        return -1;
    }
    if (ctx != NULL && ctx->mod != NULL) {
        ctx->mod->needs_exec_stack = 1;
    }
    return tramp;
}

/**
 * At the entry of a GNU nested function that takes an environment (see
 * lower_nested_env()), make each of the enclosing function's variables a
 * variable here whose storage is the address the environment holds, so
 * reads and writes reach the enclosing function's object.  A parameter of
 * the nested function hides an outer variable of its name.
 *
 * @param tu         the translation unit
 * @param sf         the nested function
 * @param vars       its variables, the parameters defined
 * @param var_count  their number
 * @param af         the nested function
 * @param envv       the environment, as the static chain gave it
 * @param diag       receives an error
 * @return           0, or -1 on error
 */
static int lower_nested_bind_outer(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, var_entry_t **vars,
                                   size_t *var_count, const cc_function_t *af, int envv, cc_diag_t *diag) {
    cc_outer_var_t *outer;
    size_t n;
    size_t i;
    long ps = cc_target_pointer_size();
    int rc = 0;

    if (cc_nested_outer_vars(tu, af, &outer, &n) != 0) {
        set_diag(diag, "nested function's enclosing function is missing in lowering");
        return -1;
    }
    for (i = 0; envv >= 0 && i < n; ++i) {
        size_t j;
        int hidden = 0;
        int slot;
        int addr;
        var_entry_t *v;
        cc_ssa_instr_t ld;

        for (j = 0; j < af->param_count; ++j) {
            if (af->params[j].name != NULL && strcmp(af->params[j].name, outer[i].name) == 0) {
                hidden = 1;
            }
        }
        if (hidden) {
            continue;
        }
        slot = emit_const_i64_instr(sf, (long long)i * ps);
        slot = slot < 0 ? -1 : emit_i64_binop_instr(sf, CC_SSA_ADD, envv, slot, 1, diag);
        if (slot < 0) {
            rc = -1;
            break;
        }
        memset(&ld, 0, sizeof(ld));
        ld.op = CC_SSA_LOAD;
        ld.dst = new_value(sf, CC_VAL_I64);
        ld.lhs = slot;
        ld.rhs = -1;
        ld.imm = ps;
        ld.is_unsigned = 1;
        if (ld.dst < 0 || push_instr(sf, ld) != 0) {
            set_diag(diag, "out of memory reading a nested function's environment");
            rc = -1;
            break;
        }
        addr = ld.dst;
        if (var_define(vars, var_count, outer[i].name, outer[i].type, outer[i].struct_id, outer[i].array_len,
                       outer[i].array_ndim, outer[i].array_dims, addr, 0, 0, 0, NULL, outer[i].ctype) != 0) {
            set_diag(diag, "out of memory defining an enclosing function's variable");
            rc = -1;
            break;
        }
        v = &(*vars)[*var_count - 1];
        v->addr_value = addr;
        if (var_is_aggregate_or_array(tu, v)) {
            v->addr_size = var_object_size_bytes(tu, v);
        } else {
            v->addr_size = var_scalar_access_size(tu, v);
            v->value = emit_const_i64_instr(sf, 0);
            if (v->value >= 0 && ct_value_kind(lower_var_ctype(v)) != CC_VAL_I64) {
                v->value = cast_value(sf, v->value, ct_value_kind(lower_var_ctype(v)), diag);
            }
            if (v->value < 0) {
                rc = -1;
                break;
            }
        }
    }
    free(outer);
    return envv < 0 ? -1 : rc;
}

static int lower_stmt(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, var_entry_t **vars, size_t *var_count,
                      const lower_ctx_t *ctx, int depth, int break_label, int continue_label, const cc_stmt_t *s,
                      int *saw_ret,
                      cc_diag_t *diag) {
    size_t j;

    if (s->kind == CC_STMT_DECL) {
        int v;
        int varv;
        if ((s->storage & CC_STORAGE_EXTERN) != 0 && s->decl_name != NULL) {
            if (s->expr != NULL) {
                set_diag(diag, "extern local declaration cannot have an initializer");
                return -1;
            }
            if (var_define(vars, var_count, s->decl_name, s->type, s->type_struct_id, s->array_len, s->array_ndim,
                           s->array_dims, -1, depth, 1, 0, s->decl_name, s->ctype) != 0) {
                set_diag(diag, "out of memory defining extern local declaration");
                return -1;
            }
            return 0;
        }
        if ((s->storage & CC_STORAGE_STATIC) != 0 && s->decl_name != NULL) {
            cc_ssa_global_t g;
            cc_ssa_instr_t in;
            char sym[256];
            int have_const_init = 0;
            int needs_runtime_init = 0;
            int is_array_obj = is_array_object_decl(s->type, s->array_len, s->array_ndim);
            int is_struct_obj = is_complete_struct_object_type(tu, s->type, s->type_struct_id);
            cc_type_t elem_type = CC_TYPE_VOID;
            long elem_size = 0;
            long arr_elems = 1;
            long long init_i = 0;
            double init_f = 0.0;
            long double init_f_ld = 0.0L;
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
                long total_size = array_type_size_bytes(tu, s->type, s->type_struct_id, s->array_ndim, s->array_dims);
                elem_type = ptr_base_type(s->type);
                elem_size = array_dims_step_size_bytes(tu, s->type, s->type_struct_id, s->array_ndim, s->array_dims, 1,
                                                       -1);
                if (elem_size <= 0) {
                    elem_size = array_decl_scalar_size_bytes(tu, s->type, s->type_struct_id, s->array_ndim);
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
                obj_size = total_size > 0 ? total_size : (elem_size * arr_elems);
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
            if (expr_is_certainly_nonconstant(s->expr)) {
                /* a static object is initialized once, before the program starts (C11 6.7.9p4) */
                set_diag(diag, "initializer element is not constant");
                return -1;
            }
            if (!is_array_obj && !is_struct_obj) {
                if (s->expr != NULL &&
                    eval_global_init_expr(tu, s->expr, &init_i, &init_f, &init_is_float, &init_sym) == 0) {
                    if (init_is_float && eval_global_init_ld_expr(tu, s->expr, &init_f_ld) != 0) {
                        init_f_ld = (long double)init_f;
                    }
                    have_const_init = 1;
                } else if (s->expr != NULL) {
                    needs_runtime_init = 1;
                }
            } else if (s->expr != NULL && !is_zero_initializer_expr(s->expr)) {
                needs_runtime_init = 1;
            }

            memset(&g, 0, sizeof(g));
            g.name = cc_strdup(sym);
            g.type = s->type;
            g.type_struct_id = s->type_struct_id;
            g.array_len = is_array_obj ? s->array_len : -1;
            g.size_bytes = obj_size;
            g.storage = CC_STORAGE_STATIC;
            g.attr_flags = s->attr_flags & (CC_ATTR_PACKED | CC_ATTR_ALIGNED | CC_ATTR_SECTION);
            g.attr_align = s->attr_align;
            if (s->attr_section != NULL) {
                g.attr_section = cc_strdup(s->attr_section);
            }
            g.has_init = have_const_init;
            if (g.has_init && (g.type == CC_TYPE_FLOAT || g.type == CC_TYPE_DOUBLE || g.type == CC_TYPE_LDOUBLE) &&
                !init_is_float) {
                init_f = (double)init_i;
                init_f_ld = (long double)init_i;
                init_is_float = 1;
            } else if (g.has_init && cc_type_is_integral(g.type) && init_is_float) {
                init_i = fp_const_to_int(init_f, g.type);
                init_is_float = 0;
            }
            g.init_i = init_i;
            g.init_f = init_f;
            g.init_f_ld = init_f_ld;
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
            if (is_array_obj && needs_runtime_init) {
                int rc = static_local_array_init_data(tu, s, &g);
                if (rc < 0) {
                    free(g.name);
                    free(g.attr_section);
                    set_diag(diag, "out of memory laying out a static local array's initializer");
                    return -1;
                }
                needs_runtime_init = rc == 0;
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
                guard.name = cc_strdup(guard_sym);
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
                        if (s->array_ndim > 0 && is_pointer_type(array_scalar_type_after_dims(s->type, s->array_ndim))) {
                            size_t cur = 0;
                            if (lower_array_init_to_ptr_cursor(tu, sf, ctx, *vars, *var_count, depth, s->type,
                                                               s->type_struct_id, s->array_ndim, s->array_dims,
                                                               s->expr, &cur, dst_addr, diag) != 0) {
                                return -1;
                            }
                            if (cur < s->expr->arg_count) {
                                set_diag(diag, "too many initializers for static local pointer array");
                                return -1;
                            }
                            handled_string_init = 1;
                        }
                        if (!handled_string_init && s->expr->arg_count > (size_t)arr_elems) {
                            set_diag(diag, "too many initializers for static local array");
                            return -1;
                        }
                        if (!handled_string_init && s->expr->arg_count == 1) {
                            const cc_expr_t *item_expr = unwrap_scalar_initializer_expr(s->expr->args[0], diag);
                            if (item_expr == NULL) {
                                return -1;
                            }
                            if (item_expr->kind == CC_EXPR_STR) {
                                unsigned long *units = NULL;
                                size_t unit_count = 0;
                                size_t si;
                                int wide = cc_string_literal_is_wide(item_expr);
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
                                if (s->type_struct_id >= 0 && lower_cast_record_id(elem_init) == s->type_struct_id &&
                                    elem_init->lhs != NULL && elem_init->lhs->kind == CC_EXPR_INIT_LIST) {
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
                                if (s->array_ndim > 1 && item_expr->kind == CC_EXPR_STR) {
                                    cc_type_t scalar_type = s->type;
                                    long scalar_size;
                                    size_t si;
                                    size_t unit_count = 0;
                                    unsigned long *units = NULL;
                                    int wide = cc_string_literal_is_wide(item_expr);
                                    int di;
                                    for (di = 0; di < s->array_ndim; ++di) {
                                        if (!is_pointer_type(scalar_type)) {
                                            set_diag(diag, "unsupported multidimensional static local string array type");
                                            return -1;
                                        }
                                        scalar_type = ptr_base_type(scalar_type);
                                    }
                                    scalar_size = type_size_bytes_with_struct(tu, scalar_type, s->type_struct_id);
                                    if (scalar_size <= 0) {
                                        scalar_size = 1;
                                    }
                                    if (decode_string_units(item_expr, wide, &units, &unit_count) != 0) {
                                        set_diag(diag, "failed to decode static local multidimensional string initializer");
                                        return -1;
                                    }
                                    if ((long)(unit_count * (size_t)scalar_size) > elem_size) {
                                        free(units);
                                        set_diag(diag, "string initializer exceeds static local array row size");
                                        return -1;
                                    }
                                    for (si = 0; si < unit_count + ((long)(unit_count * (size_t)scalar_size) < elem_size ? 1 : 0);
                                         ++si) {
                                        int char_ptr = item_ptr;
                                        int cval;
                                        if (si > 0) {
                                            int offv = emit_const_i64_instr(sf, (long)si * scalar_size);
                                            if (offv < 0) {
                                                free(units);
                                                return -1;
                                            }
                                            memset(&st_in, 0, sizeof(st_in));
                                            st_in.op = CC_SSA_ADD;
                                            st_in.dst = new_value(sf, CC_VAL_I64);
                                            st_in.lhs = item_ptr;
                                            st_in.rhs = offv;
                                            if (st_in.dst < 0 || push_instr(sf, st_in) != 0) {
                                                free(units);
                                                set_diag(diag, "out of memory computing static local string row address");
                                                return -1;
                                            }
                                            char_ptr = st_in.dst;
                                        }
                                        cval = emit_const_i64_instr(sf, si < unit_count ? (long)units[si] : 0);
                                        if (cval < 0) {
                                            free(units);
                                            return -1;
                                        }
                                        cval = cast_value(sf, cval, type_to_val(scalar_type), diag);
                                        if (cval < 0) {
                                            free(units);
                                            return -1;
                                        }
                                        memset(&st_in, 0, sizeof(st_in));
                                        st_in.op = CC_SSA_STORE;
                                        st_in.dst = -1;
                                        st_in.lhs = char_ptr;
                                        st_in.rhs = cval;
                                        st_in.imm = scalar_size;
                                        if (push_instr(sf, st_in) != 0) {
                                            free(units);
                                            set_diag(diag, "out of memory storing static local string row element");
                                            return -1;
                                        }
                                    }
                                    free(units);
                                    continue;
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
                        int wide = cc_string_literal_is_wide(s->expr);
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
                    v = bool_from_floating(sf, v, s->type, diag);
                    if (v < 0) {
                        return -1;
                    }
                    v = cast_value(sf, v, ct_value_kind(lower_decl_ctype(s)), diag);
                    if (v < 0) {
                        return -1;
                    }
                    v = normalize_float_ct(sf, v, lower_decl_ctype(s), diag);
                    if (v >= 0 && cc_type_is_integral(s->type) && !is_pointer_type(s->type)) {
                        v = normalize_integral_ct(sf, v, s->ctype, s->type, diag);
                    }
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
                           is_array_obj ? s->array_dims : NULL, -1, depth, 1, 0, sym, s->ctype) != 0) {
                set_diag(diag, "out of memory defining static local variable");
                return -1;
            }
            return 0;
        }
        if (is_complete_struct_object_type(tu, s->type, s->type_struct_id)) {
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
                    if (emit_memset_instr(sf, varv, 0, sz, diag) != 0) {
                        return -1;
                    }
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
            if (var_define(vars, var_count, s->decl_name, s->type, s->type_struct_id, -1, 0, NULL, varv, depth, 0, 0,
                           NULL, s->ctype) != 0) {
                set_diag(diag, "out of memory defining local struct variable");
                return -1;
            }
            if (ensure_local_var_addressable(tu, sf, &(*vars)[*var_count - 1], diag) < 0) {
                return -1;
            }
            return 0;
        }
        if (is_array_object_decl(s->type, s->array_len, s->array_ndim)) {
            cc_ssa_instr_t st_in;
            cc_type_t elem_type = ptr_base_type(s->type);
            long elem_size;
            long arr_elems = s->array_len > 0 ? s->array_len : 1;
            long total_size = array_type_size_bytes(tu, s->type, s->type_struct_id, s->array_ndim, s->array_dims);
            int prealloc = lower_find_hoisted_alloc(ctx, s);
            int is_runtime_vla = 0;
            int vla_bytes_v = -1;
            int vla_strides[CC_MAX_ARRAY_DIMS];
            int vla_rank = 0;

            if (arr_elems <= 0) {
                arr_elems = 1;
            }
            if (total_size <= 0 || stmt_is_vla_decl(s)) {
                if (stmt_is_vla_decl(s)) {
                    vla_shape_t shape = {s->type, s->type_struct_id, s->array_ndim, s->array_dims, s->array_bound_exprs,
                                         0};

                    if (lower_vla_layout(tu, sf, ctx, *vars, *var_count, depth, &shape, vla_strides, &vla_rank,
                                         &vla_bytes_v, diag) != 0) {
                        return -1;
                    }
                    elem_size = array_decl_scalar_size_bytes(tu, s->type, s->type_struct_id, s->array_ndim);
                    varv = emit_dynalloc_instr(sf, vla_bytes_v, 1, diag);
                    if (varv < 0) {
                        return -1;
                    }
                    is_runtime_vla = 1;
                } else {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "invalid local array size in lowering (type=%d struct_id=%d ndim=%d dims=%ld,%ld,%ld,%ld len=%ld)",
                             (int)s->type, s->type_struct_id, s->array_ndim, s->array_dims[0], s->array_dims[1],
                             s->array_dims[2], s->array_dims[3], s->array_len);
                    set_diag(diag, msg);
                    return -1;
                }
            }
            if (!is_runtime_vla && total_size % arr_elems != 0) {
                set_diag(diag, "unsupported local array element stride in lowering");
                return -1;
            }
            if (!is_runtime_vla) {
                elem_size = total_size / arr_elems;
            }
            if (elem_size <= 0) {
                elem_size = type_size_bytes_with_struct(tu, elem_type, s->type_struct_id);
            }
            if (elem_size <= 0) {
                set_diag(diag, "unsupported local array element type in lowering");
                return -1;
            }
            if (!is_runtime_vla) {
                varv = prealloc >= 0 ? prealloc : emit_local_storage_alloc(sf, total_size, diag);
                if (varv < 0) {
                    return -1;
                }
            }

            if (s->expr != NULL) {
                if (emit_memset_instr(sf, varv, 0, total_size, diag) != 0) {
                    return -1;
                }
                if (s->expr->kind == CC_EXPR_INIT_LIST) {
                    size_t ii;
                    int handled_string_init = 0;
                    if (s->array_ndim > 1 && !is_runtime_vla && init_list_has_row_initializer(s->expr)) {
                        /*
                         * Rows initialized whole -- `{{1, 2}, {3, 4}}`, `{"ab", "cd"}`: the
                         * compound-literal walk takes them, brace elision and all.
                         */
                        size_t cur = 0;
                        if (lower_array_init_to_ptr_cursor(tu, sf, ctx, *vars, *var_count, depth, s->type,
                                                           s->type_struct_id, s->array_ndim, s->array_dims, s->expr,
                                                           &cur, varv, diag) != 0) {
                            return -1;
                        }
                        if (cur < s->expr->arg_count) {
                            set_diag(diag, "too many initializers for local array");
                            return -1;
                        }
                        handled_string_init = 1;
                    }
                    if (!handled_string_init && s->expr->arg_count == 1) {
                        const cc_expr_t *item_expr = unwrap_scalar_initializer_expr(s->expr->args[0], diag);
                        if (item_expr == NULL) {
                            return -1;
                        }
                        if (item_expr->kind == CC_EXPR_STR) {
                            unsigned long *units = NULL;
                            size_t unit_count = 0;
                            size_t si;
                            int wide = cc_string_literal_is_wide(item_expr);
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
                            if (s->type_struct_id >= 0 && lower_cast_record_id(elem_init) == s->type_struct_id &&
                                elem_init->lhs != NULL && elem_init->lhs->kind == CC_EXPR_INIT_LIST) {
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
                    int wide = cc_string_literal_is_wide(s->expr);
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
                           s->array_dims, varv, depth, 0, 0, NULL, s->ctype) != 0) {
                set_diag(diag, "out of memory defining local array variable");
                return -1;
            }
            (*vars)[*var_count - 1].vla_bytes = vla_bytes_v;
            if (is_runtime_vla) {
                memcpy((*vars)[*var_count - 1].vla_strides, vla_strides, sizeof(vla_strides));
                (*vars)[*var_count - 1].vla_rank = vla_rank;
            }
            if (ensure_local_var_addressable(tu, sf, &(*vars)[*var_count - 1], diag) < 0) {
                return -1;
            }
            return 0;
        }
        varv = new_value(sf, ct_value_kind(lower_decl_ctype(s)));
        if (varv < 0) {
            set_diag(diag, "out of memory allocating local variable value");
            return -1;
        }
        if (s->type == CC_TYPE_LDOUBLE) {
            set_value_size(sf, varv, 16);
        }
        if (var_define(vars, var_count, s->decl_name, s->type, s->type_struct_id, -1, 0, NULL, varv, depth, 0, 0,
                       NULL, s->ctype) != 0) {
            set_diag(diag, "out of memory defining local variable");
            return -1;
        }
        if (stmt_is_vla_decl(s) && s->array_len == -1) {
            /* a pointer to a variable-length array (array_len -1): it steps by the array's run-time size */
            vla_shape_t shape = {s->type, s->type_struct_id, s->array_ndim, s->array_dims, s->array_bound_exprs, 1};

            if (lower_vla_attach(tu, sf, ctx, *vars, *var_count, depth, *var_count - 1, &shape, diag) != 0) {
                return -1;
            }
        }
        if (s->expr != NULL) {
            v = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
            if (v < 0) {
                return -1;
            }
            v = bool_from_floating(sf, v, s->type, diag);
            if (v < 0) {
                return -1;
            }
            v = cast_value(sf, v, ct_value_kind(lower_decl_ctype(s)), diag);
            if (v < 0) {
                return -1;
            }
            if (s->type == CC_TYPE_LDOUBLE) {
                v = promote_f64_to_ldouble_value(sf, v, diag);
                if (v < 0) {
                    return -1;
                }
            }
            v = normalize_float_ct(sf, v, lower_decl_ctype(s), diag);
            if (v >= 0 && cc_type_is_integral(s->type) && !is_pointer_type(s->type)) {
                v = normalize_integral_ct(sf, v, s->ctype, s->type, diag);
            }
            if (v < 0) {
                return -1;
            }
            if (emit_mov_instr(sf, varv, v) != 0) {
                set_diag(diag, "out of memory appending declaration move");
                return -1;
            }
            if (s->type == CC_TYPE_LDOUBLE) {
                set_value_size(sf, varv, 16);
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
            in.fimm_ld = 0.0L;
            if (push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory appending declaration default const");
                return -1;
            }
        }
        if (ensure_local_var_addressable(tu, sf, &(*vars)[*var_count - 1], diag) < 0) {
            return -1;
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
        int pushed = 0;
        size_t i4;
        int *asm_out_store_ptr = NULL;
        long *asm_out_store_size = NULL;
        const cc_ctype_t **asm_out_store_type = NULL;
        memset(&in, 0, sizeof(in));
        in.op = CC_SSA_ASM;
        in.dst = -1;
        in.lhs = -1;
        in.rhs = -1;
        in.asm_volatile = s->asm_is_volatile ? 1 : 0;
        in.asm_is_goto = s->asm_is_goto ? 1 : 0;
        in.sym = cc_strdup(s->asm_template != NULL ? s->asm_template : "");
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
            in.asm_out_sizes = (unsigned char *)calloc(in.asm_out_count, sizeof(*in.asm_out_sizes));
            in.asm_out_constraints = (char **)calloc(in.asm_out_count, sizeof(*in.asm_out_constraints));
            in.asm_out_names = (char **)calloc(in.asm_out_count, sizeof(*in.asm_out_names));
            asm_out_store_ptr = (int *)malloc(in.asm_out_count * sizeof(*asm_out_store_ptr));
            asm_out_store_size = (long *)calloc(in.asm_out_count, sizeof(*asm_out_store_size));
            asm_out_store_type = (const cc_ctype_t **)calloc(in.asm_out_count, sizeof(*asm_out_store_type));
            if (in.asm_out_values == NULL || in.asm_out_sizes == NULL || in.asm_out_constraints == NULL ||
                in.asm_out_names == NULL || asm_out_store_ptr == NULL || asm_out_store_size == NULL ||
                asm_out_store_type == NULL) {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                set_diag(diag, "out of memory storing asm outputs");
                return -1;
            }
            for (i4 = 0; i4 < in.asm_out_count; ++i4) {
                asm_out_store_ptr[i4] = -1;
            }
        }
        if (in.asm_in_count > 0) {
            in.asm_in_values = (int *)calloc(in.asm_in_count, sizeof(*in.asm_in_values));
            in.asm_in_sizes = (unsigned char *)calloc(in.asm_in_count, sizeof(*in.asm_in_sizes));
            in.asm_in_constraints = (char **)calloc(in.asm_in_count, sizeof(*in.asm_in_constraints));
            in.asm_in_names = (char **)calloc(in.asm_in_count, sizeof(*in.asm_in_names));
            if (in.asm_in_values == NULL || in.asm_in_sizes == NULL || in.asm_in_constraints == NULL ||
                in.asm_in_names == NULL) {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                set_diag(diag, "out of memory storing asm inputs");
                return -1;
            }
        }
        if (in.asm_clobber_count > 0) {
            in.asm_clobbers = (char **)calloc(in.asm_clobber_count, sizeof(*in.asm_clobbers));
            if (in.asm_clobbers == NULL) {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                set_diag(diag, "out of memory storing asm clobbers");
                return -1;
            }
        }
        if (in.asm_goto_count > 0) {
            in.asm_goto_labels = (int *)calloc(in.asm_goto_count, sizeof(*in.asm_goto_labels));
            in.asm_goto_names = (char **)calloc(in.asm_goto_count, sizeof(*in.asm_goto_names));
            if (in.asm_goto_labels == NULL || in.asm_goto_names == NULL) {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                set_diag(diag, "out of memory storing asm goto labels");
                return -1;
            }
        }
        for (i4 = 0; i4 < in.asm_out_count; ++i4) {
            int vidx = -1;
            int out_v = -1;
            const cc_asm_operand_t *op = &s->asm_outputs[i4];
            long out_sz = 0;
            int is_mem_only = cc_asm_constraint_is_memory_only(op->constraint);
            if (op->expr == NULL) {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                set_diag(diag, "asm output is missing expression");
                return -1;
            }
            out_sz = lower_value_size(tu, op->expr);
            if (out_sz <= 0) {
                out_sz = cc_target_pointer_size();
            }
            if (out_sz > 255) {
                out_sz = 255;
            }
            in.asm_out_sizes[i4] = (unsigned char)out_sz;
            if (op->expr != NULL && op->expr->kind == CC_EXPR_IDENT && op->expr->ident != NULL) {
                vidx = var_find_visible(*vars, *var_count, op->expr->ident, depth);
                if (vidx >= 0) {
                    if (is_mem_only) {
                        int ptrv = ensure_local_var_addressable(tu, sf, &(*vars)[vidx], diag);
                        if (ptrv < 0) {
                            free_asm_instr_fields(&in);
                            free(asm_out_store_ptr);
                            free(asm_out_store_size);
                            free(asm_out_store_type);
                            return -1;
                        }
                        out_v = CC_SSA_ASM_MEM_INDIRECT_ENCODE(ptrv);
                    } else {
                        out_v = (*vars)[vidx].value;
                        if ((*vars)[vidx].addr_value >= 0) {
                            asm_out_store_ptr[i4] = (*vars)[vidx].addr_value;
                            asm_out_store_size[i4] = out_sz;
                            asm_out_store_type[i4] = lower_value_ctype(op->expr);
                        }
                    }
                } else if (is_mem_only) {
                    const cc_global_t *g = find_global(tu, op->expr->ident);
                    if (g != NULL) {
                        int addrv = emit_global_addr(sf, g->name, diag);
                        if (addrv < 0) {
                            free_asm_instr_fields(&in);
                            free(asm_out_store_ptr);
                            free(asm_out_store_size);
                            free(asm_out_store_type);
                            return -1;
                        }
                        out_v = CC_SSA_ASM_MEM_INDIRECT_ENCODE(addrv);
                    } else {
                        free_asm_instr_fields(&in);
                        free(asm_out_store_ptr);
                        free(asm_out_store_size);
                        free(asm_out_store_type);
                        set_diag(diag, "asm output target must reference a local or global lvalue");
                        return -1;
                    }
                } else {
                    int av = lower_expr(tu, sf, ctx, *vars, *var_count, depth, op->expr, diag);
                    if (av < 0) {
                        free_asm_instr_fields(&in);
                        free(asm_out_store_ptr);
                        free(asm_out_store_size);
                        free(asm_out_store_type);
                        return -1;
                    }
                    out_v = av;
                }
            } else if (op->expr->kind == CC_EXPR_DEREF || op->expr->kind == CC_EXPR_MEMBER) {
                int ptrv = -1;
                if (op->expr->kind == CC_EXPR_DEREF) {
                    if (op->expr->lhs == NULL) {
                        free_asm_instr_fields(&in);
                        free(asm_out_store_ptr);
                        free(asm_out_store_size);
                        free(asm_out_store_type);
                        set_diag(diag, "malformed asm output dereference target");
                        return -1;
                    }
                    ptrv = lower_expr(tu, sf, ctx, *vars, *var_count, depth, op->expr->lhs, diag);
                } else {
                    ptrv = lower_member_addr(tu, sf, ctx, *vars, *var_count, depth, op->expr, diag);
                }
                if (ptrv < 0) {
                    free_asm_instr_fields(&in);
                    free(asm_out_store_ptr);
                    free(asm_out_store_size);
                    free(asm_out_store_type);
                    return -1;
                }
                ptrv = cast_value(sf, ptrv, CC_VAL_I64, diag);
                if (ptrv < 0) {
                    free_asm_instr_fields(&in);
                    free(asm_out_store_ptr);
                    free(asm_out_store_size);
                    free(asm_out_store_type);
                    return -1;
                }
                if (is_mem_only) {
                    out_v = CC_SSA_ASM_MEM_INDIRECT_ENCODE(ptrv);
                } else {
                    int tmpv = new_value(sf, lower_value_kind(op->expr));
                    if (tmpv < 0) {
                        free_asm_instr_fields(&in);
                        free(asm_out_store_ptr);
                        free(asm_out_store_size);
                        free(asm_out_store_type);
                        set_diag(diag, "out of memory allocating asm output temporary");
                        return -1;
                    }
                    out_v = tmpv;
                    asm_out_store_ptr[i4] = ptrv;
                    asm_out_store_size[i4] = out_sz;
                    asm_out_store_type[i4] = lower_value_ctype(op->expr);
                }
            } else {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                set_diag(diag, "unsupported asm output target (requires identifier, dereference, or member lvalue)");
                return -1;
            }
            in.asm_out_values[i4] = out_v;
            in.asm_out_constraints[i4] = cc_strdup(op->constraint != NULL ? op->constraint : "");
            if (op->name != NULL) {
                in.asm_out_names[i4] = cc_strdup(op->name);
            }
            if (in.asm_out_constraints[i4] == NULL || (op->name != NULL && in.asm_out_names[i4] == NULL)) {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                set_diag(diag, "out of memory storing asm output operand");
                return -1;
            }
        }
        for (i4 = 0; i4 < in.asm_in_count; ++i4) {
            const cc_asm_operand_t *op = &s->asm_inputs[i4];
            long in_sz = 0;
            int av = lower_expr(tu, sf, ctx, *vars, *var_count, depth, op->expr, diag);
            if (av < 0) {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                return -1;
            }
            in.asm_in_values[i4] = av;
            in_sz = lower_value_size(tu, op->expr);
            if (in_sz <= 0) {
                in_sz = cc_target_pointer_size();
            }
            if (in_sz > 255) {
                in_sz = 255;
            }
            in.asm_in_sizes[i4] = (unsigned char)in_sz;
            in.asm_in_constraints[i4] = cc_strdup(op->constraint != NULL ? op->constraint : "");
            if (op->name != NULL) {
                in.asm_in_names[i4] = cc_strdup(op->name);
            }
            if (in.asm_in_constraints[i4] == NULL || (op->name != NULL && in.asm_in_names[i4] == NULL)) {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                set_diag(diag, "out of memory storing asm input operand");
                return -1;
            }
        }
        for (i4 = 0; i4 < in.asm_clobber_count; ++i4) {
            in.asm_clobbers[i4] = cc_strdup(s->asm_clobbers[i4] != NULL ? s->asm_clobbers[i4] : "");
            if (in.asm_clobbers[i4] == NULL) {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                set_diag(diag, "out of memory storing asm clobber");
                return -1;
            }
        }
        for (i4 = 0; i4 < in.asm_goto_count; ++i4) {
            int l;
            const char *lname = s->asm_goto_labels[i4];
            if (ctx == NULL || lname == NULL || lname[0] == '\0') {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                set_diag(diag, "asm goto label is malformed");
                return -1;
            }
            l = lower_find_label(ctx, lname);
            if (l < 0) {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "asm goto references unknown label: %s", lname);
                }
                return -1;
            }
            in.asm_goto_labels[i4] = l;
            in.asm_goto_names[i4] = cc_strdup(lname);
            if (in.asm_goto_names[i4] == NULL) {
                free_asm_instr_fields(&in);
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                set_diag(diag, "out of memory storing asm goto label name");
                return -1;
            }
        }
        if (push_instr(sf, in) != 0) {
            free_asm_instr_fields(&in);
            free(asm_out_store_ptr);
            free(asm_out_store_size);
            free(asm_out_store_type);
            set_diag(diag, "out of memory appending SSA asm instruction");
            return -1;
        }
        pushed = 1;
        for (i4 = 0; i4 < in.asm_out_count; ++i4) {
            cc_ssa_instr_t st;
            int rhs;
            if (asm_out_store_ptr == NULL || asm_out_store_ptr[i4] < 0) {
                continue;
            }
            if (asm_out_store_size[i4] <= 0) {
                set_diag(diag, "asm output storeback has invalid size");
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                return -1;
            }
            if (ct_in_memory(asm_out_store_type[i4])) {
                /* Aggregate output: memcpy the asm result into the
                 * storeback pointer. Linux's perf perf_event_open uses
                 * an asm that returns a struct {u32, u32}; refusing
                 * blocks the build. */
                if (emit_memcpy_instr(sf, asm_out_store_ptr[i4], in.asm_out_values[i4],
                                      asm_out_store_size[i4], diag) != 0) {
                    free(asm_out_store_ptr);
                    free(asm_out_store_size);
                    free(asm_out_store_type);
                    return -1;
                }
                continue;
            }
            rhs = in.asm_out_values[i4];
            rhs = normalize_float_ct(sf, rhs, asm_out_store_type[i4], diag);
            if (rhs < 0) {
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                return -1;
            }
            rhs = normalize_int_ct(sf, rhs, asm_out_store_type[i4], diag);
            if (rhs < 0) {
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                return -1;
            }
            memset(&st, 0, sizeof(st));
            st.op = CC_SSA_STORE;
            st.dst = -1;
            st.lhs = asm_out_store_ptr[i4];
            st.rhs = rhs;
            st.imm = asm_out_store_size[i4];
            if (push_instr(sf, st) != 0) {
                set_diag(diag, "out of memory appending asm output storeback");
                free(asm_out_store_ptr);
                free(asm_out_store_size);
                free(asm_out_store_type);
                return -1;
            }
        }
        (void)pushed;
        free(asm_out_store_ptr);
        free(asm_out_store_size);
        free(asm_out_store_type);
        return 0;
    }

    if (s->kind == CC_STMT_RETURN) {
        cc_ssa_instr_t ret_in;
        int rv = -1;
        int is_void_return = 0;
        long ret_size_hint = 0;
        int ret_agg_class = 0;

        if (ctx != NULL && ctx->fn != NULL && ctx->fn->ret_type == CC_TYPE_VOID && ctx->fn->ret_struct_id < 0) {
            is_void_return = 1;
        }

        if (s->expr != NULL) {
            rv = lower_expr(tu, sf, ctx, *vars, *var_count, depth, s->expr, diag);
            if (rv < 0) {
                return -1;
            }
            if (!is_void_return && ctx != NULL && ctx->fn != NULL &&
                ctx->fn->ret_type == CC_TYPE_VOID && ctx->fn->ret_struct_id >= 0) {
                long ret_size = type_size_bytes_with_struct(tu, ctx->fn->ret_type, ctx->fn->ret_struct_id);
                long reg_agg_limit = cc_target_pointer_size() <= 4 ? 8 : 16;
                if (ret_size <= 0) {
                    set_diag(diag, "unsupported aggregate return size in return lowering");
                    return -1;
                }
                rv = cast_value(sf, rv, CC_VAL_I64, diag);
                if (rv < 0) {
                    return -1;
                }
                ret_agg_class = agg_return_class(tu, ctx->fn->ret_struct_id, ret_size);
                if ((ret_agg_class & (CC_ABI_RET_X87 | CC_ABI_RET_X87_COMPLEX | CC_ABI_RET_MEMORY)) != 0 ||
                    (cc_target_pointer_size() == 4 && (ret_agg_class & (CC_ABI_RET_VEC128 | CC_ABI_RET_VEC64)) != 0)) {
                    /* the backend reads the value from the structure rv points at */
                    ret_size_hint = ret_size;
                    goto append_return;
                }
                if (sf->ret_abi == CC_CALL_ARG_ABI_AGG_MEM) {
                    /*
                     * i386: every struct is returned through the caller's
                     * hidden pointer, whatever its size; the backend copies
                     * the struct rv points at there.
                     */
                    ret_size_hint = ret_size;
                    goto append_return;
                }
                if (ret_size <= cc_target_pointer_size()) {
                    cc_ssa_instr_t load_in;
                    memset(&load_in, 0, sizeof(load_in));
                    load_in.op = CC_SSA_LOAD;
                    load_in.dst = new_value(sf, CC_VAL_I64);
                    load_in.lhs = rv;
                    load_in.rhs = -1;
                    load_in.imm = ret_size;
                    load_in.is_unsigned = 1;
                    if (load_in.dst < 0 || push_instr(sf, load_in) != 0) {
                        set_diag(diag, "out of memory lowering small aggregate return");
                        return -1;
                    }
                    rv = load_in.dst;
                    memset(&ret_in, 0, sizeof(ret_in));
                    ret_in.op = CC_SSA_RET;
                    ret_in.dst = -1;
                    ret_in.lhs = rv;
                    ret_in.rhs = -1;
                    ret_in.imm = ret_size;
                    ret_in.param_index = -1;
                    ret_in.agg_class = ret_agg_class;
                    if (push_instr(sf, ret_in) != 0) {
                        set_diag(diag, "out of memory appending aggregate return");
                        return -1;
                    }
                    *saw_ret = 1;
                    return 0;
                }
                if (ret_size <= 16) {
                    if (ret_size > reg_agg_limit) {
                        rv = cast_value(sf, rv, sf->ret_type, diag);
                        if (rv < 0) {
                            return -1;
                        }
                        ret_size_hint = cc_target_pointer_size();
                        goto append_return;
                    }
                    cc_ssa_instr_t csplit;
                    cc_ssa_instr_t add_split;
                    cc_ssa_instr_t lo;
                    cc_ssa_instr_t hi;
                    long low_size = cc_target_pointer_size();
                    long hi_size = ret_size - low_size;

                    memset(&lo, 0, sizeof(lo));
                    lo.op = CC_SSA_LOAD;
                    lo.dst = new_value(sf, CC_VAL_I64);
                    lo.lhs = rv;
                    lo.rhs = -1;
                    lo.imm = low_size;
                    lo.is_unsigned = 1;
                    if (lo.dst < 0 || push_instr(sf, lo) != 0) {
                        set_diag(diag, "out of memory lowering aggregate return low part");
                        return -1;
                    }

                    memset(&csplit, 0, sizeof(csplit));
                    csplit.op = CC_SSA_CONST;
                    csplit.dst = new_value(sf, CC_VAL_I64);
                    csplit.imm = low_size;
                    if (csplit.dst < 0 || push_instr(sf, csplit) != 0) {
                        set_diag(diag, "out of memory lowering aggregate return offset");
                        return -1;
                    }

                    memset(&add_split, 0, sizeof(add_split));
                    add_split.op = CC_SSA_ADD;
                    add_split.dst = new_value(sf, CC_VAL_I64);
                    add_split.lhs = rv;
                    add_split.rhs = csplit.dst;
                    if (add_split.dst < 0 || push_instr(sf, add_split) != 0) {
                        set_diag(diag, "out of memory lowering aggregate return high address");
                        return -1;
                    }

                    memset(&hi, 0, sizeof(hi));
                    hi.op = CC_SSA_LOAD;
                    hi.dst = new_value(sf, CC_VAL_I64);
                    hi.lhs = add_split.dst;
                    hi.rhs = -1;
                    hi.imm = hi_size;
                    hi.is_unsigned = 1;
                    if (hi.dst < 0 || push_instr(sf, hi) != 0) {
                        set_diag(diag, "out of memory lowering aggregate return high part");
                        return -1;
                    }

                    memset(&ret_in, 0, sizeof(ret_in));
                    ret_in.op = CC_SSA_RET;
                    ret_in.dst = -1;
                    ret_in.lhs = lo.dst;
                    ret_in.rhs = hi.dst;
                    ret_in.imm = ret_size;
                    ret_in.param_index = -1;
                    ret_in.agg_class = ret_agg_class;
                    if (push_instr(sf, ret_in) != 0) {
                        set_diag(diag, "out of memory appending aggregate return");
                        return -1;
                    }
                    *saw_ret = 1;
                    return 0;
                }
                /*
                 * >16-byte aggregate return still uses the older pointer-return
                 * convention in this lowering path.
                 */
                rv = cast_value(sf, rv, sf->ret_type, diag);
                if (rv < 0) {
                    return -1;
                }
                ret_size_hint = ret_size;
            } else if (!is_void_return) {
                rv = cast_value(sf, rv, sf->ret_type, diag);
                if (rv < 0) {
                    return -1;
                }
                if (ctx != NULL && ctx->fn != NULL && cc_type_is_integral(ctx->fn->ret_type)) {
                    /* `return 7;` from a long long function: the int is widened (on i386, its high word set) */
                    rv = normalize_integral_ct(sf, rv, ctx->fn->ret_ctype, ctx->fn->ret_type, diag);
                    if (rv < 0) {
                        return -1;
                    }
                }
                if (ctx != NULL && ctx->fn != NULL) {
                    ret_size_hint = type_size_bytes_with_struct(tu, ctx->fn->ret_type, ctx->fn->ret_struct_id);
                    if (ret_size_hint <= 0) {
                        ret_size_hint = cc_target_pointer_size();
                    }
                    /* a long double returned from a double or float function is rounded to it */
                    if (ctx->fn->ret_type == CC_TYPE_DOUBLE || ctx->fn->ret_type == CC_TYPE_FLOAT) {
                        rv = ctx->fn->ret_type == CC_TYPE_FLOAT ? normalize_float_value(sf, rv, CC_TYPE_FLOAT, diag)
                                                                : demote_ldouble_to_f64_value(sf, rv, diag);
                        if (rv < 0) {
                            return -1;
                        }
                    }
                }
            } else {
                rv = -1;
            }
        }

append_return:
        memset(&ret_in, 0, sizeof(ret_in));
        ret_in.op = CC_SSA_RET;
        ret_in.dst = -1;
        ret_in.lhs = rv;
        ret_in.rhs = -1;
        ret_in.imm = ret_size_hint;
        ret_in.param_index = -1;
        ret_in.agg_class = ret_agg_class;
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
        long long *case_values = NULL;
        long long *case_hi_values = NULL;
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
            case_values = (long long *)calloc(case_count, sizeof(*case_values));
            case_hi_values = (long long *)calloc(case_count, sizeof(*case_hi_values));
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
    long long iv = 0;
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
        sym = cc_strdup(e->ident);
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
            step = lower_is_pointer(e->lhs) ? lower_pointer_step(tu, e->lhs) : 1;
            step = expr_array_step_size_bytes(tu, NULL, 0, 0, e->lhs, step);
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
            step = lower_is_pointer(e->rhs) ? lower_pointer_step(tu, e->rhs) : 1;
            step = expr_array_step_size_bytes(tu, NULL, 0, 0, e->rhs, step);
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

static int eval_global_init_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long long *out_i, double *out_f,
                                 int *out_is_float, char **out_sym) {
    const_integral_value_t ivalue;

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
    if ((cc_ct_is_integer(cc_const_type_of(e)) || cc_const_type_of(e)->kind == CC_CT_POINTER) &&
        eval_const_integral_expr_value(tu, e, &ivalue) == 0) {
        *out_i = (long long)ivalue.bits;
        *out_f = (double)*out_i;
        *out_is_float = 0;
        if (out_sym != NULL) {
            *out_sym = NULL;
        }
        return 0;
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
        *out_i = (long long)e->float_val;
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
            *out_sym = cc_strdup(e->ident);
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
                    *out_sym = cc_strdup(tmp);
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
    case CC_EXPR_CALL:
        if (e->ident != NULL) {
            if ((strcmp(e->ident, "__builtin_huge_val") == 0 || strcmp(e->ident, "__builtin_huge_valf") == 0 ||
                 strcmp(e->ident, "__builtin_huge_vall") == 0 || strcmp(e->ident, "__builtin_inf") == 0 ||
                 strcmp(e->ident, "__builtin_inff") == 0 || strcmp(e->ident, "__builtin_infl") == 0) &&
                e->arg_count == 0) {
                *out_i = 0;
                *out_f = INFINITY;
                *out_is_float = 1;
                if (out_sym != NULL) {
                    *out_sym = NULL;
                }
                return 0;
            }
            if ((strcmp(e->ident, "__builtin_nanf") == 0 || strcmp(e->ident, "__builtin_nan") == 0 ||
                 strcmp(e->ident, "__builtin_nanl") == 0 || strcmp(e->ident, "__builtin_nansf") == 0 ||
                 strcmp(e->ident, "__builtin_nans") == 0 || strcmp(e->ident, "__builtin_nansl") == 0) &&
                e->arg_count == 1) {
                *out_i = 0;
                *out_f = NAN;
                *out_is_float = 1;
                if (out_sym != NULL) {
                    *out_sym = NULL;
                }
                return 0;
            }
            if ((strcmp(e->ident, "__builtin_ldbl_min") == 0 || strcmp(e->ident, "__builtin_ldbl_denorm_min") == 0 ||
                 strcmp(e->ident, "__builtin_ldbl_max") == 0) &&
                e->arg_count == 0) {
                long double v;
                if (strcmp(e->ident, "__builtin_ldbl_max") == 0) {
                    v = LDBL_MAX;
                } else if (strcmp(e->ident, "__builtin_ldbl_denorm_min") == 0) {
#ifdef __LDBL_DENORM_MIN__
                    v = __LDBL_DENORM_MIN__;
#else
                    v = 0.0L;
#endif
                } else {
                    v = LDBL_MIN;
                }
                *out_i = 0;
                *out_f = (double)v;
                *out_is_float = 1;
                if (out_sym != NULL) {
                    *out_sym = NULL;
                }
                return 0;
            }
        }
        return -1;
    case CC_EXPR_SIZEOF: {
        long n;
        if (e->lhs != NULL) {
            n = sizeof_expr_bytes(tu, NULL, 0, 0, e->lhs);
        } else {
            n = sizeof_type_name_bytes(tu, e);
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
        long long cv = 0;
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
        if (e->op == CC_BIN_ADD || e->op == CC_BIN_SUB) {
            char *addr_sym = NULL;
            long addr_addend = 0;
            if (eval_global_addr_symbol_addend(tu, e, &addr_sym, &addr_addend) == 0 && addr_sym != NULL) {
                *out_i = 0;
                *out_f = 0.0;
                *out_is_float = 0;
                if (out_sym != NULL) {
                    if (addr_addend == 0) {
                        *out_sym = addr_sym;
                    } else {
                        char tmp[384];
                        snprintf(tmp, sizeof(tmp), "%s%+ld", addr_sym, addr_addend);
                        *out_sym = cc_strdup(tmp);
                        free(addr_sym);
                        if (*out_sym == NULL) {
                            return -1;
                        }
                    }
                } else {
                    free(addr_sym);
                }
                return 0;
            }
            free(addr_sym);
        }
        {
            long long ai = 0;
            long long bi = 0;
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
                        *out_sym = cc_strdup(tmp);
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

static int eval_global_init_ld_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long double *out) {
    long long iv = 0;
    double fv = 0.0;
    int isf = 0;
    char *sym = NULL;

    if (out == NULL) {
        return -1;
    }
    if (e == NULL) {
        *out = 0.0L;
        return 0;
    }
    if (e->kind == CC_EXPR_GENERIC) {
        const cc_expr_t *sel = selected_generic_expr(e);
        return sel != NULL ? eval_global_init_ld_expr(tu, sel, out) : -1;
    }
    if (e->kind == CC_EXPR_MEMBER && e->lhs == NULL && e->rhs != NULL) {
        return eval_global_init_ld_expr(tu, e->rhs, out);
    }
    switch (e->kind) {
    case CC_EXPR_INT:
        *out = (long double)e->int_val;
        return 0;
    case CC_EXPR_FLOAT:
        *out = lower_has_kind(e, CC_CT_LDOUBLE) ? e->float_val_ld : (long double)e->float_val;
        return 0;
    case CC_EXPR_CAST:
        return eval_global_init_ld_expr(tu, e->lhs, out);
    case CC_EXPR_CALL:
        if (e->ident != NULL && e->arg_count == 0) {
            if (strcmp(e->ident, "__builtin_ldbl_min") == 0) {
                *out = LDBL_MIN;
                return 0;
            }
            if (strcmp(e->ident, "__builtin_ldbl_denorm_min") == 0) {
#ifdef __LDBL_DENORM_MIN__
                *out = __LDBL_DENORM_MIN__;
#else
                *out = 0.0L;
#endif
                return 0;
            }
            if (strcmp(e->ident, "__builtin_ldbl_max") == 0) {
                *out = LDBL_MAX;
                return 0;
            }
        }
        break;
    case CC_EXPR_TERNARY: {
        long double cv = 0.0L;
        if (e->lhs == NULL || e->third == NULL) {
            return -1;
        }
        if (eval_global_init_ld_expr(tu, e->lhs, &cv) != 0) {
            return -1;
        }
        if (cv != 0.0L) {
            return e->rhs != NULL ? eval_global_init_ld_expr(tu, e->rhs, out) : ((*out = cv), 0);
        }
        return eval_global_init_ld_expr(tu, e->third, out);
    }
    case CC_EXPR_BIN:
        if (e->op == CC_BIN_COMMA) {
            return eval_global_init_ld_expr(tu, e->rhs, out);
        }
        {
            long double a = 0.0L;
            long double b = 0.0L;
            if (eval_global_init_ld_expr(tu, e->lhs, &a) != 0 || eval_global_init_ld_expr(tu, e->rhs, &b) != 0) {
                return -1;
            }
            switch (e->op) {
            case CC_BIN_ADD: *out = a + b; return 0;
            case CC_BIN_SUB: *out = a - b; return 0;
            case CC_BIN_MUL: *out = a * b; return 0;
            case CC_BIN_DIV:
                if (b == 0.0L) return -1;
                *out = a / b;
                return 0;
            default:
                break;
            }
        }
        break;
    default:
        break;
    }

    if (eval_global_init_expr(tu, e, &iv, &fv, &isf, &sym) != 0 || sym != NULL) {
        free(sym);
        return -1;
    }
    free(sym);
    *out = isf ? (long double)fv : (long double)iv;
    return 0;
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
        out->init_f_ld = (long double)e->int_val;
        out->init_is_float = 0;
        return 0;
    case CC_EXPR_FLOAT:
        out->init_i = (long long)e->float_val;
        out->init_f = e->float_val;
        out->init_f_ld = lower_has_kind(e, CC_CT_LDOUBLE) ? e->float_val_ld : (long double)e->float_val;
        out->init_is_float = 1;
        return 0;
    case CC_EXPR_STR:
        out->init_is_string = 1;
        out->init_str = string_literal_emit_text(tu, e);
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
        long long iv = 0;
        double fv = 0.0;
        int isf = 0;
        char *sym = NULL;
        if (eval_global_init_expr(tu, e, &iv, &fv, &isf, &sym) != 0) {
            free(sym);
            return -1;
        }
        out->init_i = iv;
        out->init_f = fv;
        out->init_f_ld = isf && eval_global_init_ld_expr(tu, e, &out->init_f_ld) == 0 ? out->init_f_ld
                                                                                         : (long double)(isf ? fv : iv);
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
    if (sd->members[member_idx].is_bitfield && sd->members[member_idx].bit_width > 0) {
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

static long struct_scalar_slots_for_init(const cc_translation_unit_t *tu, int struct_id, int depth) {
    const cc_struct_def_t *sd;
    long slots = 0;
    size_t i;

    if (tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count || depth > 32) {
        return -1;
    }
    sd = &tu->structs[struct_id];
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        if (sd->has_flexible_array && i + 1 == sd->member_count) {
            continue;
        }
        if (is_array_object_decl(m->type, m->array_len, m->array_ndim)) {
            long cnt = m->array_len > 0 ? m->array_len : 1;
            cc_type_t elem = ptr_base_type(m->type);
            if (elem == CC_TYPE_VOID && m->type_struct_id >= 0) {
                long sub = struct_scalar_slots_for_init(tu, m->type_struct_id, depth + 1);
                if (sub < 0 || cnt > LONG_MAX / sub || slots > LONG_MAX - cnt * sub) {
                    return -1;
                }
                slots += cnt * sub;
            } else {
                if (slots > LONG_MAX - cnt) {
                    return -1;
                }
                slots += cnt;
            }
            continue;
        }
        if (m->type == CC_TYPE_VOID && m->type_struct_id >= 0) {
            long sub = struct_scalar_slots_for_init(tu, m->type_struct_id, depth + 1);
            if (sub < 0 || slots > LONG_MAX - sub) {
                return -1;
            }
            slots += sub;
            continue;
        }
        if (slots == LONG_MAX) {
            return -1;
        }
        slots++;
    }
    return slots > 0 ? slots : 1;
}

static long infer_struct_array_len_from_init_list(const cc_translation_unit_t *tu, int struct_id, const cc_expr_t *init) {
    long slots;
    size_t cur = 0;
    long elems = 0;

    if (tu == NULL || init == NULL || init->kind != CC_EXPR_INIT_LIST) {
        return -1;
    }
    slots = struct_scalar_slots_for_init(tu, struct_id, 0);
    if (slots <= 0) {
        return -1;
    }
    while (cur < init->arg_count) {
        const cc_expr_t *raw = init->args[cur];
        if (raw != NULL && raw->kind == CC_EXPR_INIT_LIST) {
            elems++;
            cur++;
            continue;
        }
        if (struct_id >= 0 && lower_cast_record_id(raw) == struct_id && raw->lhs != NULL &&
            raw->lhs->kind == CC_EXPR_INIT_LIST) {
            elems++;
            cur++;
            continue;
        }
        {
            long consumed = 0;
            while (cur < init->arg_count && consumed < slots) {
                const cc_expr_t *it = init->args[cur];
                if (consumed > 0 && it != NULL && it->kind == CC_EXPR_INIT_LIST) {
                    break;
                }
                consumed++;
                cur++;
            }
            if (consumed == 0) {
                cur++;
            }
            elems++;
        }
    }
    return elems > 0 ? elems : 1;
}

/**
 * The string literal a flexible array member's initializer gives it,
 * written bare (`"abc"`) or as the only item of a braced list
 * (`{"abc"}`).
 *
 * @param item  the member's initializer
 * @return      the literal, or NULL if it is not one
 */
static const cc_expr_t *flexible_init_string(const cc_expr_t *item) {
    if (item != NULL && item->kind == CC_EXPR_INIT_LIST && item->arg_count == 1) {
        item = item->args[0];
    }
    return item != NULL && item->kind == CC_EXPR_STR ? item : NULL;
}

static long infer_flexible_array_member_elems(const cc_translation_unit_t *tu, const cc_struct_member_t *m,
                                              const cc_expr_t *item) {
    const cc_expr_t *str = flexible_init_string(item);
    cc_type_t elem_type;
    long elem_size;
    unsigned long *units = NULL;
    size_t unit_count = 0;
    int wide;
    long elems = -1;

    if (tu == NULL || m == NULL || item == NULL) {
        return -1;
    }
    elem_type = ptr_base_type(m->type);
    elem_size = array_decl_scalar_size_bytes(tu, m->type, m->type_struct_id, m->array_ndim);
    if (elem_size <= 0) {
        elem_size = pointer_elem_size_bytes(tu, m->type, m->type_struct_id);
    }
    if (elem_size <= 0) {
        return -1;
    }
    if (str != NULL && elem_type != CC_TYPE_VOID) {
        wide = cc_string_literal_is_wide(str);
        if (decode_string_units(str, wide, &units, &unit_count) != 0) {
            free(units);
            return -1;
        }
        free(units);
        elems = (long)unit_count + 1; /* the terminating null */
    } else if (elem_type == CC_TYPE_VOID && m->type_struct_id >= 0 && item->kind == CC_EXPR_INIT_LIST) {
        elems = infer_struct_array_len_from_init_list(tu, m->type_struct_id, item);
    } else if (item->kind == CC_EXPR_INIT_LIST) {
        elems = (long)item->arg_count;
    } else {
        elems = 1;
    }
    return elems > 0 ? elems : -1;
}

static long infer_flexible_struct_tail_extra_bytes(const cc_translation_unit_t *tu, int struct_id, const cc_expr_t *init_list) {
    const cc_struct_def_t *sd;
    size_t i;
    size_t next_member = 0;

    if (tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count || init_list == NULL ||
        init_list->kind != CC_EXPR_INIT_LIST) {
        return 0;
    }
    sd = &tu->structs[struct_id];
    if (!struct_has_flexible_tail(sd) || sd->member_count == 0) {
        return 0;
    }
    for (i = 0; i < init_list->arg_count; ++i) {
        const cc_expr_t *raw = init_list->args[i];
        const cc_expr_t *item = raw;
        size_t member_idx = next_member;
        const cc_struct_member_t *m;
        int raw_is_designator =
            (raw != NULL && raw->kind == CC_EXPR_MEMBER && raw->lhs == NULL && raw->rhs != NULL && raw->ident != NULL);
        if (raw_is_designator) {
            int didx = find_struct_member_index_by_name(sd, raw->ident);
            if (didx < 0) {
                continue;
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
        if (member_idx + 1 == sd->member_count && struct_has_flexible_tail(sd)) {
            long elem_size = array_decl_scalar_size_bytes(tu, m->type, m->type_struct_id, m->array_ndim);
            long elems;
            if (elem_size <= 0) {
                elem_size = pointer_elem_size_bytes(tu, m->type, m->type_struct_id);
            }
            elems = infer_flexible_array_member_elems(tu, m, item);
            if (elem_size <= 0 || elems <= 0 || elems > LONG_MAX / elem_size) {
                return -1;
            }
            return elems * elem_size;
        }
    }
    return 0;
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
    dup = cc_strdup(sym);
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
    dup = cc_strdup(str);
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
        if (j < 8) {
            buf[off + j] = (unsigned char)((v >> (8 * j)) & 0xFFu);
        } else {
            buf[off + j] = 0;
        }
    }
}

static unsigned long long load_const_bytes(const unsigned char *buf, long off, long size) {
    unsigned long long v = 0;
    long j;
    for (j = 0; j < size && j < 8; ++j) {
        v |= ((unsigned long long)buf[off + j]) << (8 * j);
    }
    return v;
}

static int store_bitfield_global_init(const cc_translation_unit_t *tu, const cc_struct_member_t *m,
                                      const cc_expr_t *expr, unsigned char *buf, long buf_size, long off,
                                      cc_diag_t *diag) {
    long long iv = 0;
    double fv = 0.0;
    int isf = 0;
    char *sym = NULL;
    unsigned long long oldv;
    unsigned long long field_mask;
    unsigned long long clear_mask;
    unsigned long long newv;
    long storage_size;

    if (m == NULL || !m->is_bitfield || m->bit_width <= 0) {
        set_diag(diag, "invalid bit-field global initializer");
        return -1;
    }
    storage_size = m->bit_storage_size > 0 ? m->bit_storage_size : m->size;
    if (storage_size <= 0 || off < 0 || off + storage_size > buf_size) {
        set_diag(diag, "bit-field initializer exceeds object size");
        return -1;
    }
    if (eval_global_init_expr(tu, expr, &iv, &fv, &isf, &sym) != 0 || sym != NULL) {
        free(sym);
        set_diag(diag, "bit-field global initializer must be an integer constant");
        return -1;
    }
    free(sym);
    if (m->bit_width >= 64) {
        field_mask = ~0ULL;
    } else {
        field_mask = (1ULL << m->bit_width) - 1ULL;
    }
    oldv = load_const_bytes(buf, off, storage_size);
    clear_mask = ~(field_mask << m->bit_offset);
    newv = (oldv & clear_mask) | ((((unsigned long long)(isf ? (long long)fv : iv)) & field_mask) << m->bit_offset);
    store_const_bytes(buf, off, storage_size, newv);
    return 0;
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
        long long iv = 0;
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

/**
 * Write the constant value of a scalar initializer into a global's bytes:
 * an integer or floating value in the field's size, or a relocation for
 * an address (a string literal, a symbol plus addend).
 *
 * @param tu           the translation unit
 * @param ct           the field's type
 * @param field_size   its size in bytes
 * @param expr         the initializer (a braced scalar is unwrapped)
 * @param buf          the global's bytes
 * @param buf_size     their size
 * @param off          the field's offset in them
 * @param relocs       receives relocations
 * @param reloc_count  their count
 * @param diag         receives an error
 * @return             0, or -1 with @p diag set
 */
static int store_scalar_global_init(const cc_translation_unit_t *tu, const cc_ctype_t *ct, long field_size,
                                    const cc_expr_t *expr, unsigned char *buf, long buf_size, long off,
                                    global_reloc_t **relocs, size_t *reloc_count, cc_diag_t *diag) {
    long long iv = 0;
    double fv = 0.0;
    int isf = 0;
    char *sym = NULL;
    cc_ctype_kind_t tk = ct != NULL ? ct->kind : CC_CT_VOID;
    int is_ptr = tk == CC_CT_POINTER;

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
        if (!is_ptr || field_size != cc_target_pointer_size()) {
            set_diag(diag, "string scalar initializer requires pointer-sized pointer field");
            return -1;
        }
        char *text = string_literal_emit_text(tu, expr);
        if (text == NULL ||
            append_global_string_reloc(relocs, reloc_count, off, field_size, text, diag) != 0) {
            free(text);
            free(sym);
            return -1;
        }
        free(text);
        return 0;
    }
    if (expr != NULL && expr->kind == CC_EXPR_CAST && expr->lhs != NULL && expr->lhs->kind == CC_EXPR_INIT_LIST &&
        lower_cast_is_char_array(expr)) {
        char *quoted = global_char_list_to_quoted_string(tu, expr->lhs);
        if (quoted != NULL) {
            if (is_ptr && field_size == cc_target_pointer_size()) {
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
        int ptr_sized_integral = cc_ct_is_integer(ct) && field_size == cc_target_pointer_size();
        if ((!is_ptr && !ptr_sized_integral) || field_size != cc_target_pointer_size()) {
            if (diag != NULL && diag->message[0] == '\0') {
                char spelled[96];

                snprintf(diag->message, sizeof(diag->message),
                         "symbol initializer requires pointer-sized field (sym=%s type=%s size=%ld at %zu:%zu)", sym,
                         ct != NULL ? cc_ct_format(ct, spelled, sizeof(spelled)) : "?", field_size,
                         expr != NULL ? expr->line : 0, expr != NULL ? expr->col : 0);
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

    if (tk == CC_CT_FLOAT16 || tk == CC_CT_FLOAT || tk == CC_CT_DOUBLE || tk == CC_CT_LDOUBLE) {
        if (field_size == 2 && tk == CC_CT_FLOAT16) {
            unsigned short raw = float16_pack_bits(isf ? fv : (double)iv);
            store_const_bytes(buf, off, field_size, raw);
            return 0;
        }
        if (field_size == 4) {
            union {
                float f;
                uint32_t u;
            } cvt;
            cvt.f = isf ? (float)fv : (float)iv;
            store_const_bytes(buf, off, field_size, cvt.u);
            return 0;
        }
        if (field_size == 8) {
            union {
                double f;
                uint64_t u;
            } cvt;
            cvt.f = isf ? fv : (double)iv;
            store_const_bytes(buf, off, field_size, cvt.u);
            return 0;
        }
        if (tk == CC_CT_LDOUBLE) {
            long double cvt = 0.0L;
            unsigned char raw[sizeof(long double)];
            long ncopy = field_size;
            if (isf && eval_global_init_ld_expr(tu, expr, &cvt) != 0) {
                cvt = (long double)fv;
            } else if (!isf) {
                cvt = (long double)iv;
            }
            memset(raw, 0, sizeof(raw));
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
            if (ncopy > 10) {
                ncopy = 10;
            }
#endif
            if (ncopy > (long)sizeof(raw)) {
                ncopy = (long)sizeof(raw);
            }
            memcpy(raw, &cvt, (size_t)ncopy);
            ncopy = field_size;
            if (ncopy > (long)sizeof(raw)) {
                ncopy = (long)sizeof(raw);
            }
            if (ncopy > 0) {
                memcpy(buf + off, raw, (size_t)ncopy);
            }
            while (ncopy < field_size) {
                buf[off + ncopy] = 0;
                ncopy++;
            }
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
    if (struct_id >= 0 && lower_cast_record_id(e) == struct_id && e->lhs != NULL && e->lhs->kind == CC_EXPR_INIT_LIST) {
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

/**
 * Write a string literal into a global's character array: its units, the
 * terminating null if there is room, at most @p max_elems of them.
 *
 * @param expr       the literal
 * @param elem       the array's element type (a character type, or an
 *                   integer type for a wide literal)
 * @param elem_size  its size
 * @param max_elems  the array's length
 * @param field_off  the array's offset in @p buf
 * @param buf        the global's bytes
 * @param buf_size   their size
 * @param diag       receives an error
 * @return           0, or -1 (with @p diag set on a real error)
 */
static int fill_fixed_char_array_from_string(const cc_expr_t *expr, const cc_ctype_t *elem, long elem_size,
                                             long max_elems, long field_off, unsigned char *buf, long buf_size,
                                             cc_diag_t *diag) {
    unsigned long *units = NULL;
    size_t unit_count = 0;
    size_t i;
    size_t ncopy;
    int wide;

    if (expr == NULL || expr->kind != CC_EXPR_STR || max_elems < 0 || elem_size <= 0 || elem == NULL) {
        return -1;
    }
    wide = cc_string_literal_is_wide(expr);
    if (!wide && !cc_ct_is_character(elem)) {
        return -1;
    }
    if (wide && !cc_ct_is_integer(elem)) {
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
static int flatten_scalar_array_init_cursor(const cc_translation_unit_t *tu, cc_type_t scalar_type, int scalar_struct_id,
                                            const long *dims, int ndim, const cc_expr_t *list, size_t *cursor,
                                            long base, unsigned char *buf, long buf_size, global_reloc_t **relocs,
                                            size_t *reloc_count, cc_diag_t *diag);

/**
 * Write the initializer of a structure's flexible array member -- the
 * GNU extension of initializing one in a static object -- into the
 * object's bytes.  The object was sized for the elements given
 * (infer_flexible_struct_tail_extra_bytes()).
 *
 * @param tu           the translation unit
 * @param m            the flexible array member
 * @param item         its initializer: a braced list (brace elision
 *                     allowed for structure elements)
 * @param elems        how many elements it gives
 * @param base         the member's offset in the buffer
 * @param buf          the object's bytes
 * @param buf_size     their size
 * @param relocs       receives relocations for address constants
 * @param reloc_count  their count
 * @param diag         receives an error
 * @return             0, or -1 on error
 */
static int flatten_flexible_tail_init(const cc_translation_unit_t *tu, const cc_struct_member_t *m,
                                      const cc_expr_t *item, long elems, long base, unsigned char *buf, long buf_size,
                                      global_reloc_t **relocs, size_t *reloc_count, cc_diag_t *diag) {
    cc_type_t elem_type = ptr_base_type(m->type);
    long elem_size = array_decl_scalar_size_bytes(tu, m->type, m->type_struct_id, m->array_ndim);
    const cc_expr_t *str = flexible_init_string(item);
    size_t cur = 0;
    long k;

    if (elem_size <= 0) {
        elem_size = pointer_elem_size_bytes(tu, m->type, m->type_struct_id);
    }
    if (elem_size <= 0 || elems <= 0 || elems > (buf_size - base) / elem_size) {
        set_diag(diag, "flexible array initializer exceeds its object");
        return -1;
    }
    if (str != NULL && elem_type != CC_TYPE_VOID) {
        unsigned long *units = NULL;
        size_t count = 0;
        size_t u;
        if (decode_string_units(str, cc_string_literal_is_wide(str), &units, &count) != 0) {
            free(units);
            set_diag(diag, "invalid string initializer for flexible array member");
            return -1;
        }
        for (u = 0; u < count && (long)u < elems; ++u) {
            long b;
            for (b = 0; b < elem_size && b < (long)sizeof(units[u]); ++b) {
                buf[base + (long)u * elem_size + b] = (unsigned char)(units[u] >> (8 * b));
            }
        }
        free(units);
        return 0; /* the terminating null is already zero */
    }
    if (item == NULL) {
        return 0;
    }
    if (item->kind != CC_EXPR_INIT_LIST) {
        /* `{1, 5}` for `int v[]`: brace elision gives the array one element */
        if (elem_type == CC_TYPE_VOID) {
            set_diag(diag, "unsupported flexible array member initializer");
            return -1;
        }
        return store_scalar_global_init(tu, cc_ct_from_legacy(elem_type, m->type_struct_id, 0, NULL), elem_size, item,
                                        buf, buf_size, base, relocs,
                                        reloc_count, diag);
    }
    if (elem_type == CC_TYPE_VOID && m->type_struct_id >= 0) {
        for (k = 0; k < elems && cur < item->arg_count; ++k) {
            const cc_expr_t *raw = item->args[cur];
            const cc_expr_t *braced = extract_struct_init_list_expr(raw, m->type_struct_id);
            long off = base + k * elem_size;
            if (braced != NULL) {
                size_t sub = 0;
                if (flatten_struct_init_bytes_cursor(tu, m->type_struct_id, braced, &sub, off, buf, buf_size, relocs,
                                                     reloc_count, diag) != 0) {
                    return -1;
                }
                cur++;
            } else if (flatten_struct_init_bytes_cursor(tu, m->type_struct_id, item, &cur, off, buf, buf_size, relocs,
                                                        reloc_count, diag) != 0) {
                /* brace elision: the element takes its members from the list itself */
                return -1;
            }
        }
        return 0;
    }
    {
        long dims[1];
        dims[0] = elems;
        return flatten_scalar_array_init_cursor(tu, elem_type, -1, dims, 1, item, &cur, base, buf, buf_size, relocs,
                                                reloc_count, diag);
    }
}

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
    if (elem_size <= 0) {
        set_diag(diag, "invalid array-like struct member extent");
        return -1;
    }
    if (m->array_len > 0) {
        max_elems = m->array_len;
    } else if (m->size > 0) {
        max_elems = m->size / elem_size;
    } else if (buf_size > field_off) {
        max_elems = (buf_size - field_off) / elem_size;
    } else {
        max_elems = 0;
    }
    if (max_elems <= 0) {
        set_diag(diag, "invalid array-like struct member extent");
        return -1;
    }
    for (j = 0; j < max_elems && *cursor < src_list->arg_count; ++j) {
        const cc_expr_t *raw = src_list->args[*cursor];
        long elem_off = field_off + j * elem_size;
        const cc_expr_t *elem_expr = raw;

        if (is_member_designator_expr(raw)) {
            break;
        }

        if (raw != NULL && raw->kind == CC_EXPR_STR) {
            if (fill_fixed_char_array_from_string(raw, cc_ct_from_scalar(elem_type), elem_size, max_elems, field_off,
                                                  buf, buf_size,
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
        if (store_scalar_global_init(tu, cc_ct_from_legacy(elem_type, m->type_struct_id, 0, NULL), elem_size, elem_expr,
                                     buf, buf_size, elem_off,
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
            /* GNU extension: a static object's flexible array member takes the elements its initializer gives */
            long elems = infer_flexible_array_member_elems(tu, m, item);
            if (elems > 0 && flatten_flexible_tail_init(tu, m, item, elems, base + m->offset, buf, buf_size, relocs,
                                                        reloc_count, diag) != 0) {
                return -1;
            }
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
                if (store_scalar_global_init(tu, cc_ct_from_legacy(um->type, um->type_struct_id, 0, NULL), u_size, u_item,
                                             buf, buf_size, u_off,
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
            if (m->name == NULL || m->type_struct_id < 0 || (size_t)m->type_struct_id >= tu->struct_count ||
                find_struct_member_index_by_name(&tu->structs[m->type_struct_id], m->name) < 0) {
                nested = unwrap_self_designated_init_list(nested, m->name);
            }
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
                long long z_i = 0;
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
                if (fill_fixed_char_array_from_string(item, cc_ct_from_scalar(elem_type), elem_size, max_elems,
                                                      field_off, buf, buf_size,
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
        if (m->is_bitfield) {
            if (store_bitfield_global_init(tu, m, item, buf, buf_size, field_off, diag) != 0) {
                return -1;
            }
            continue;
        }
        if (store_scalar_global_init(tu, cc_ct_from_legacy(m->type, m->type_struct_id, 0, NULL), scalar_size, item, buf,
                                     buf_size, field_off,
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
            if (store_scalar_global_init(tu, cc_ct_from_legacy(scalar_type, scalar_struct_id, 0, NULL), scalar_size, item,
                                         buf, buf_size, elem_off,
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
                if (fill_fixed_char_array_from_string(raw, cc_ct_from_scalar(scalar_type), scalar_size, dims[1],
                                                      sub_base, buf, buf_size,
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
                it.init_str = cc_strdup(relocs[ri].payload);
                if (it.init_str == NULL) {
                    set_diag(diag, "out of memory duplicating relocation string in global initializer stream");
                    return -1;
                }
            } else {
                it.init_is_symbol = 1;
                it.init_sym = cc_strdup(relocs[ri].payload);
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

/**
 * Give a static local array of scalars its initializer as data, the way
 * a file-scope array gets it, instead of code that fills it on the
 * function's first call.  The initializer of an object with static
 * storage is constant (C11 6.7.9p4), so its bytes are known now, in any
 * brace form: `{1, 2, 3, 4}` and `{{1, 2}, {3, 4}}` alike.
 *
 * @param tu  the translation unit
 * @param s   the declaration; an array with an initializer list
 * @param g   the array's global, which receives the data and has_init
 * @return    1 if it was given its data; 0 if the initializer is not a
 *            constant array of scalars this handles, so the caller falls
 *            back to filling the array at run time; -1 when out of memory
 */
static int static_local_array_init_data(const cc_translation_unit_t *tu, const cc_stmt_t *s, cc_ssa_global_t *g) {
    cc_type_t scalar_type = s->type;
    int scalar_sid = s->type_struct_id;
    long dims[CC_MAX_ARRAY_DIMS];
    int nd = s->array_ndim;
    long scalar_size;
    long elem_count = 1;
    unsigned char *buf;
    global_reloc_t *relocs = NULL;
    size_t reloc_count = 0;
    size_t cur = 0;
    cc_diag_t quiet;
    int d;

    if (s->expr == NULL || s->expr->kind != CC_EXPR_INIT_LIST || nd <= 0 || nd > CC_MAX_ARRAY_DIMS) {
        return 0;
    }
    for (d = 0; d < nd; ++d) {
        if (!is_pointer_type(scalar_type)) {
            return 0;
        }
        scalar_type = ptr_base_type(scalar_type);
        dims[d] = s->array_dims[d];
    }
    if (dims[0] <= 0 && s->array_len > 0 && nd == 1) {
        dims[0] = s->array_len;
    }
    if (scalar_type == CC_TYPE_VOID || is_pointer_type(scalar_type)) {
        /* aggregates and pointers (string tables) keep their own initialization code */
        return 0;
    }
    scalar_sid = -1;
    scalar_size = type_size_bytes_with_struct(tu, scalar_type, scalar_sid);
    if (scalar_size <= 0) {
        return 0;
    }
    for (d = 0; d < nd; ++d) {
        if (dims[d] <= 0 || elem_count > LONG_MAX / dims[d]) {
            return 0;
        }
        elem_count *= dims[d];
    }
    if (scalar_size > LONG_MAX / elem_count) {
        return 0;
    }
    buf = (unsigned char *)calloc((size_t)(scalar_size * elem_count), 1);
    if (buf == NULL) {
        return -1;
    }
    memset(&quiet, 0, sizeof(quiet));
    if (flatten_scalar_array_init_cursor(tu, scalar_type, scalar_sid, dims, nd, s->expr, &cur, 0, buf,
                                         scalar_size * elem_count, &relocs, &reloc_count, &quiet) != 0 ||
        cur < s->expr->arg_count) {
        /* not a constant this can lay out: leave it to the run-time path and its diagnostics */
        free(buf);
        free_global_relocs(relocs, reloc_count);
        return 0;
    }
    if (build_global_stream_from_bytes(g, buf, scalar_size * elem_count, relocs, reloc_count, &quiet) != 0) {
        free(buf);
        free_global_relocs(relocs, reloc_count);
        return -1;
    }
    g->type = CC_TYPE_PTR_UCHAR;
    g->type_struct_id = -1;
    g->array_len = scalar_size * elem_count;
    g->has_init = 1;
    free(buf);
    free_global_relocs(relocs, reloc_count);
    return 1;
}

static int expr_uses_va_arg_pack(const cc_expr_t *e);

static int stmt_uses_va_arg_pack(const cc_stmt_t *s) {
    size_t i;

    if (s == NULL) {
        return 0;
    }
    if (expr_uses_va_arg_pack(s->expr) || expr_uses_va_arg_pack(s->init_expr) || expr_uses_va_arg_pack(s->post_expr)) {
        return 1;
    }
    if (stmt_uses_va_arg_pack(s->init_stmt) || stmt_uses_va_arg_pack(s->then_branch) ||
        stmt_uses_va_arg_pack(s->else_branch)) {
        return 1;
    }
    for (i = 0; i < s->block_count; ++i) {
        if (stmt_uses_va_arg_pack(&s->block_stmts[i])) {
            return 1;
        }
    }
    for (i = 0; i < s->asm_output_count; ++i) {
        if (expr_uses_va_arg_pack(s->asm_outputs[i].expr)) {
            return 1;
        }
    }
    for (i = 0; i < s->asm_input_count; ++i) {
        if (expr_uses_va_arg_pack(s->asm_inputs[i].expr)) {
            return 1;
        }
    }
    return 0;
}

static int expr_uses_va_arg_pack(const cc_expr_t *e) {
    size_t i;

    if (e == NULL) {
        return 0;
    }
    if (e->kind == CC_EXPR_CALL && e->ident != NULL &&
        (strcmp(e->ident, "__builtin_va_arg_pack") == 0 || strcmp(e->ident, "__builtin_va_arg_pack_len") == 0)) {
        return 1;
    }
    if (expr_uses_va_arg_pack(e->lhs) || expr_uses_va_arg_pack(e->rhs) || expr_uses_va_arg_pack(e->third)) {
        return 1;
    }
    for (i = 0; i < e->arg_count; ++i) {
        if (expr_uses_va_arg_pack(e->args[i])) {
            return 1;
        }
    }
    for (i = 0; i < e->stmt_expr_count; ++i) {
        if (stmt_uses_va_arg_pack(&e->stmt_expr_stmts[i])) {
            return 1;
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
        if (stmt_uses_va_arg_pack(&f->stmts[i])) {
            return 1;
        }
    }
    if ((f->storage & CC_STORAGE_STATIC) == 0) {
        return 0;
    }
    if ((f->storage & CC_STORAGE_INLINE) == 0) {
        return 0;
    }
    /* Always emit static-inline bodies. Linux's `inline` macro pulls in
     * __maybe_unused (which cc parses as __unused__), so without this we
     * skipped the bodies of every static inline -- including referenced
     * helpers like fls / __ffs / arch_*_bit / __arch_hweight* / etc. --
     * leaving the vDSO link with a wall of undefined references. cc has
     * no inliner, so these have to come out-of-line. Bad-form bodies
     * from x86_64-only helpers like rip_rel_ptr produce dead-code asm
     * that the assembler now tolerates (rip in 32-bit, oversized reloc,
     * etc.); the linker drops them via section gc. */
    return 0;
}

int cc_ast_to_ssa(const cc_translation_unit_t *tu, cc_ssa_module_t *out, cc_diag_t *diag) {
    if (!tu || !out) return -1;

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
            long long init_i = 0;
            double init_f = 0.0;
            long double init_f_ld = 0.0L;
            int init_is_float = 0;
            int init_is_string = 0;
            int force_bss_zero = 0;
            int is_struct_global = 0;
            int struct_id = -1;
            long struct_size = 0;
            long struct_elems = 0;
            char *init_sym = NULL;
            const cc_expr_t *init = tu->globals[i].init;
            out->globals[i].name = cc_strdup(tu->globals[i].name);
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
                out->globals[i].attr_section = cc_strdup(tu->globals[i].attr_section);
                if (out->globals[i].attr_section == NULL) {
                    set_diag(diag, "out of memory duplicating global section attribute");
                    cc_ssa_module_free(out);
                    return -1;
                }
            }
            if (tu->globals[i].attr_alias != NULL) {
                out->globals[i].attr_alias = cc_strdup(tu->globals[i].attr_alias);
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
                if (struct_elems == 1 && struct_has_flexible_tail(&tu->structs[struct_id])) {
                    long flex_extra = infer_flexible_struct_tail_extra_bytes(tu, struct_id, struct_init);
                    if (flex_extra < 0 || total_size > LONG_MAX - flex_extra) {
                        set_diag(diag, "invalid flexible-array struct global object size");
                        cc_ssa_module_free(out);
                        return -1;
                    }
                    total_size += flex_extra;
                }
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
                if (tu->globals[i].type == CC_TYPE_VOID && struct_id >= 0 && lower_cast_record_id(init) == struct_id &&
                    init->lhs != NULL && init->lhs->kind == CC_EXPR_INIT_LIST) {
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

            if (init != NULL && init->kind == CC_EXPR_ADDR && init->lhs != NULL &&
                lower_cast_record_id(init->lhs) >= 0 && init->lhs->lhs != NULL &&
                init->lhs->lhs->kind == CC_EXPR_INIT_LIST) {
                cc_ssa_global_t lit;
                global_reloc_t *relocs = NULL;
                size_t reloc_count = 0;
                unsigned char *buf = NULL;
                long lit_sid = lower_cast_record_id(init->lhs);
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
                lit.name = cc_strdup(symbuf);
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
                init_sym = cc_strdup(symbuf);
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
                            long long z_i = 0;
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
                        if (init->arg_count == 0) {
                            force_bss_zero = 1;
                            out->globals[i].has_init = 0;
                            out->globals[i].init_i = 0;
                            out->globals[i].init_f = 0.0;
                            out->globals[i].init_is_float = 0;
                            out->globals[i].init_is_string = 0;
                            out->globals[i].init_is_symbol = 0;
                            out->globals[i].init_sym = NULL;
                            continue;
                        } else if (init->arg_count == 1) {
                            long long sv = 0;
                            double sf = 0.0;
                            int s_is_float = 0;
                            char *ssym = NULL;
                            if (eval_global_init_expr(tu, init->args[0], &sv, &sf, &s_is_float, &ssym) != 0) {
                                free(ssym);
                                if (diag != NULL && diag->message[0] == '\0') {
                                    snprintf(diag->message, sizeof(diag->message),
                                             "unsupported scalar list initializer for global %s", tu->globals[i].name);
                                }
                                cc_ssa_module_free(out);
                                return -1;
                            }
                            if (ssym != NULL && out->globals[i].type == CC_TYPE_BOOL) {
                                free(ssym);
                                ssym = NULL;
                                sv = 1;
                                sf = 1.0;
                                s_is_float = 0;
                            }
                            if (s_is_float && out->globals[i].type == CC_TYPE_BOOL) {
                                sv = sf != 0.0;
                                sf = (double)sv;
                                s_is_float = 0;
                            }
                            out->globals[i].init_i = sv;
                            out->globals[i].init_f = sf;
                            out->globals[i].init_f_ld =
                                s_is_float && eval_global_init_ld_expr(tu, init->args[0], &init_f_ld) == 0
                                    ? init_f_ld
                                    : (long double)(s_is_float ? sf : sv);
                            out->globals[i].init_is_float = s_is_float;
                            out->globals[i].init_is_string = 0;
                            out->globals[i].init_is_symbol = ssym != NULL ? 1 : 0;
                            out->globals[i].init_sym = ssym;
                            out->globals[i].has_init =
                                (ssym != NULL) || (s_is_float ? (sf != 0.0 || signbit(sf)) : (sv != 0));
                            continue;
                        }
                        if (diag != NULL && diag->message[0] == '\0') {
                            snprintf(diag->message, sizeof(diag->message),
                                     "unsupported scalar list initializer (arity %zu) for global %s",
                                     init->arg_count, tu->globals[i].name);
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
                    if (tu->globals[i].array_ndim == 1 && cc_ct_is_character(cc_ct_from_scalar(base_t))) {
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
                out->globals[i].init_str = string_literal_emit_text(tu, init);
                if (out->globals[i].init_str == NULL) {
                    set_diag(diag, "out of memory duplicating global string initializer");
                    cc_ssa_module_free(out);
                    return -1;
                }
            } else if (init != NULL && init->kind == CC_EXPR_CAST && init->lhs != NULL &&
                       init->lhs->kind == CC_EXPR_STR && is_pointer_type(tu->globals[i].type)) {
                init_is_string = 1;
                out->globals[i].init_str = string_literal_emit_text(tu, init->lhs);
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
            if (init_sym != NULL && out->globals[i].type == CC_TYPE_BOOL) {
                free(init_sym);
                init_sym = NULL;
                init_i = 1;
                init_f = 1.0;
                init_f_ld = 1.0L;
                init_is_float = 0;
            }
            if (init_is_float && out->globals[i].type == CC_TYPE_BOOL) {
                init_i = init_f != 0.0;
                init_f = (double)init_i;
                init_f_ld = (long double)init_i;
                init_is_float = 0;
            }
            if (init_is_float && eval_global_init_ld_expr(tu, init, &init_f_ld) != 0) {
                init_f_ld = (long double)init_f;
            }
            out->globals[i].has_init = (init != NULL) && !force_bss_zero;
            if (out->globals[i].has_init && !out->globals[i].init_is_string && !out->globals[i].init_is_symbol) {
                if ((out->globals[i].type == CC_TYPE_FLOAT || out->globals[i].type == CC_TYPE_DOUBLE ||
                     out->globals[i].type == CC_TYPE_LDOUBLE) &&
                    !init_is_float) {
                    init_f = (double)init_i;
                    init_f_ld = (long double)init_i;
                    init_is_float = 1;
                } else if (cc_type_is_integral(out->globals[i].type) && init_is_float) {
                    init_i = fp_const_to_int(init_f, out->globals[i].type);
                    init_is_float = 0;
                }
            }
            out->globals[i].init_i = init_i;
            out->globals[i].init_f = init_f;
            out->globals[i].init_f_ld = init_f_ld;
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
        gdecl.name = cc_strdup(fdecl->name);
        gdecl.type = CC_TYPE_PTR_VOID;
        gdecl.type_struct_id = -1;
        gdecl.array_len = -1;
        gdecl.storage = fdecl->storage | CC_STORAGE_EXTERN;
        gdecl.attr_flags = fdecl->attr_flags;
        gdecl.attr_align = fdecl->attr_align;
        if (fdecl->attr_section != NULL) {
            gdecl.attr_section = cc_strdup(fdecl->attr_section);
        }
        gdecl.attr_alias = cc_strdup(fdecl->attr_alias);
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
        cc_attr_flags_t eff_attr_flags;
        long eff_attr_align;
        const char *eff_attr_section;
        const char *eff_attr_alias = NULL;
        int nest_chain = -1;

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

        sf->name = cc_strdup(af->name);
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
        sf->ret_abi = (unsigned char)(af->ret_type == CC_TYPE_LDOUBLE ? CC_CALL_ARG_ABI_LDOUBLE
                                      : af->ret_type == CC_TYPE_FLOAT ? CC_CALL_ARG_ABI_FLOAT
                                                                      : CC_CALL_ARG_ABI_DEFAULT);
        if (cc_target_pointer_size() <= 4 && af->ret_type == CC_TYPE_VOID && af->ret_struct_id >= 0 &&
            agg_return_class(tu, af->ret_struct_id, 8) == 0) {
            /*
             * the i386 psABI returns every struct in memory; a float
             * _Complex comes back in %eax:%edx, and a vector of 4, 8 or
             * 16 bytes in a register
             */
            sf->ret_abi = CC_CALL_ARG_ABI_AGG_MEM;
        }
        sf->storage = af->storage;
        sf->attr_flags = eff_attr_flags;
        sf->attr_align = eff_attr_align;
        if (eff_attr_section != NULL) {
            sf->attr_section = cc_strdup(eff_attr_section);
            if (sf->attr_section == NULL) {
                set_diag(diag, "out of memory duplicating function section attribute");
                cc_ssa_module_free(out);
                return -1;
            }
        }
        if (eff_attr_alias != NULL) {
            sf->attr_alias = cc_strdup(eff_attr_alias);
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
        sf->param_abi = (unsigned char *)calloc(af->param_count == 0 ? 1 : af->param_count, sizeof(*sf->param_abi));
        if (sf->param_values == NULL || sf->param_types == NULL || sf->param_abi == NULL) {
            set_diag(diag, "out of memory allocating parameter map");
            cc_ssa_module_free(out);
            return -1;
        }
        if (cc_nested_uses_outer(tu, af)) {
            /*
             * A nested function that uses the enclosing function's variables
             * is given their addresses, an array, in the static chain
             * register; it is read before anything else can clobber it.
             */
            cc_ssa_instr_t ch;

            memset(&ch, 0, sizeof(ch));
            ch.op = CC_SSA_CHAIN;
            ch.dst = new_value(sf, CC_VAL_I64);
            ch.lhs = -1;
            ch.rhs = -1;
            if (ch.dst < 0 || push_instr(sf, ch) != 0) {
                set_diag(diag, "out of memory reading a nested function's static chain");
                cc_ssa_module_free(out);
                return -1;
            }
            set_value_size(sf, ch.dst, cc_target_pointer_size());
            nest_chain = ch.dst;
        }

        /* i386: the vector parameters that came in %xmm0-2 and %mm0-2 so far */
        int vec_n16 = 0;
        int vec_n8 = 0;

        for (j = 0; j < af->param_count; ++j) {
            cc_ssa_instr_t in;
            long param_size;
            long aggregate_size = -1;
            int agg_cls = 0;
            int is_aggregate_param = (af->params[j].type == CC_TYPE_VOID && af->params[j].type_struct_id >= 0);
            int use_agg_mem_abi = 0;
            int use_agg_reg_abi = 0;
            int agg_reg_storage = -1;
            int vec_abi;

            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_PARAM;
            in.dst = new_value(sf, ct_value_kind(lower_param_ctype(&af->params[j])));
            in.lhs = -1;
            in.rhs = -1;
            in.param_index = (int)j;
            param_size = type_size_bytes_with_struct(tu, af->params[j].type, af->params[j].type_struct_id);
            if (is_aggregate_param) {
                aggregate_size = param_size;
            }
            if (param_size <= 0) {
                param_size = is_aggregate_param && aggregate_size == 0 ? 0 : cc_target_pointer_size();
            }
            vec_abi = (is_aggregate_param && cc_target_pointer_size() == 4)
                          ? i386_vector_abi(tu, af->params[j].type_struct_id, param_size, &vec_n16, &vec_n8)
                          : -1;
            if (vec_abi >= 0) {
                /* an i386 vector: from an xmm or MMX register, or the stack aligned to its size */
                if (CC_ABI_KIND(vec_abi) == CC_CALL_ARG_ABI_AGG_REG) {
                    use_agg_reg_abi = 1;
                    agg_cls = vec_abi & ~0x0f;
                } else {
                    use_agg_mem_abi = 1;
                }
            } else if (is_aggregate_param && aggregate_size == 0) {
                /* GNU C empty structs are zero-width ABI parameters. */
            } else if (is_aggregate_param && cc_target_pointer_size() == 8 && param_size > 16) {
                use_agg_mem_abi = 1;
            } else if (is_aggregate_param && cc_target_pointer_size() == 4 && param_size > 4) {
                use_agg_mem_abi = 1;
            } else if (is_aggregate_param && cc_target_pointer_size() == 8 && param_size <= 16 &&
                       (agg_cls = abi64_aggregate_class(tu, af->params[j].type_struct_id, param_size, 0)) < 0) {
                /* x86-64: an aggregate of 16 bytes or fewer in memory (a long double in it, packed) */
                use_agg_mem_abi = 1;
            } else if (is_aggregate_param && cc_target_pointer_size() == 8 && param_size <= 16 &&
                       (param_size > cc_target_pointer_size() || agg_cls != 0)) {
                use_agg_reg_abi = 1;
            } else if (is_aggregate_param && param_size > cc_target_pointer_size()) {
                param_size = cc_target_pointer_size();
            }
            if (use_agg_reg_abi) {
                agg_reg_storage = emit_local_storage_alloc(sf, param_size, diag);
                if (agg_reg_storage < 0) {
                    cc_ssa_module_free(out);
                    return -1;
                }
                in.lhs = agg_reg_storage;
            }
            in.imm = param_size;
            in.is_unsigned = ct_loads_unsigned(lower_param_ctype(&af->params[j])) ? 1 : 0;
            if (in.dst < 0) {
                set_diag(diag, "out of memory assigning param SSA value");
                cc_ssa_module_free(out);
                return -1;
            }
            if (af->params[j].type == CC_TYPE_LDOUBLE) {
                set_value_size(sf, in.dst, 16);
            }
            sf->param_values[j] = in.dst;
            sf->param_types[j] = ct_value_kind(lower_param_ctype(&af->params[j]));
            if (use_agg_mem_abi) {
                sf->param_abi[j] = (unsigned char)(CC_CALL_ARG_ABI_AGG_MEM |
                                                   vector_mem_flag(tu, af->params[j].type_struct_id, param_size));
            } else if (use_agg_reg_abi) {
                sf->param_abi[j] = (unsigned char)(CC_CALL_ARG_ABI_AGG_REG | (agg_cls > 0 ? agg_cls : 0));
            } else if (is_aggregate_param && aggregate_size == 0) {
                sf->param_abi[j] = CC_CALL_ARG_ABI_ZERO;
            } else {
                /* a float parameter arrives in single precision -- unless the definition is old-style (promoted) */
                sf->param_abi[j] = (unsigned char)(af->params[j].type == CC_TYPE_LDOUBLE ? CC_CALL_ARG_ABI_LDOUBLE
                                                   : (af->params[j].type == CC_TYPE_FLOAT && af->has_prototype)
                                                       ? CC_CALL_ARG_ABI_FLOAT
                                                       : CC_CALL_ARG_ABI_DEFAULT);
            }
            if (push_instr(sf, in) != 0) {
                set_diag(diag, "out of memory appending SSA parameter instruction");
                cc_ssa_module_free(out);
                return -1;
            }
            if (use_agg_mem_abi || use_agg_reg_abi) {
                /* the value is the address of the struct's stack copy or storage, not the struct */
                set_value_size(sf, in.dst, cc_target_pointer_size());
            }
        }
        for (j = 0; j < af->param_count; ++j) {
            int v = sf->param_values[j];
            int is_aggregate_param = (af->params[j].type == CC_TYPE_VOID && af->params[j].type_struct_id >= 0);
            if (is_aggregate_param) {
                long agg_size = type_size_bytes_with_struct(tu, af->params[j].type, af->params[j].type_struct_id);
                int agg_addr;
                if (agg_size < 0) {
                    char msg[192];
                    snprintf(msg, sizeof(msg), "unsupported aggregate parameter size for %s.%s (struct_id=%d size=%ld)",
                             af->name != NULL ? af->name : "<function>",
                             af->params[j].name != NULL ? af->params[j].name : "<anonymous>",
                             af->params[j].type_struct_id, agg_size);
                    set_diag(diag, msg);
                    cc_ssa_module_free(out);
                    return -1;
                }
                agg_addr = emit_local_storage_alloc(sf, agg_size > 0 ? agg_size : 1, diag);
                if (agg_addr < 0) {
                    cc_ssa_module_free(out);
                    return -1;
                }
                if (agg_size == 0) {
                    /* No incoming ABI bytes exist for a GNU empty struct. */
                } else if (agg_size <= cc_target_pointer_size() &&
                           CC_ABI_KIND(sf->param_abi[j]) != CC_CALL_ARG_ABI_AGG_MEM &&
                           CC_ABI_KIND(sf->param_abi[j]) != CC_CALL_ARG_ABI_AGG_REG) {
                    /* its bytes came in one integer register; otherwise v is the address of a copy */
                    cc_ssa_instr_t st;
                    memset(&st, 0, sizeof(st));
                    st.op = CC_SSA_STORE;
                    st.dst = -1;
                    st.lhs = agg_addr;
                    st.rhs = v;
                    st.imm = agg_size;
                    if (push_instr(sf, st) != 0) {
                        set_diag(diag, "out of memory materializing aggregate parameter");
                        cc_ssa_module_free(out);
                        return -1;
                    }
                } else if (emit_memcpy_instr(sf, agg_addr, v, agg_size, diag) != 0) {
                    cc_ssa_module_free(out);
                    return -1;
                }
                v = agg_addr;
                sf->param_values[j] = v;
            }
            if (var_define(&vars, &var_count, af->params[j].name, af->params[j].type, af->params[j].type_struct_id,
                           af->params[j].array_len, af->params[j].array_ndim, af->params[j].array_dims, v, 0, 0, 1,
                           NULL, af->params[j].ctype) != 0) {
                set_diag(diag, "out of memory defining parameter variable");
                cc_ssa_module_free(out);
                return -1;
            }
            if (ensure_local_var_addressable(tu, sf, &vars[var_count - 1], diag) < 0) {
                cc_ssa_module_free(out);
                return -1;
            }
        }
        /* a parameter that points to a variable-length array: its strides, from the parameters before it */
        for (j = 0; j < af->param_count; ++j) {
            const cc_param_t *pa = &af->params[j];
            vla_shape_t shape = {pa->type, pa->type_struct_id, pa->array_ndim, pa->array_dims, pa->vla_bounds, 1};
            int k;
            int vidx;

            for (k = 0; k < CC_MAX_ARRAY_DIMS && pa->vla_bounds[k] == NULL; ++k) {
            }
            if (k == CC_MAX_ARRAY_DIMS || pa->name == NULL) {
                continue;
            }
            vidx = var_find_visible(vars, var_count, pa->name, 0);
            if (vidx < 0 ||
                lower_vla_attach(tu, sf, &lctx, vars, var_count, 0, (size_t)vidx, &shape, diag) != 0) {
                if (vidx < 0) {
                    set_diag(diag, "variable-length array parameter is not defined in lowering");
                }
                cc_ssa_module_free(out);
                return -1;
            }
        }
        if (nest_chain >= 0 && lower_nested_bind_outer(tu, sf, &vars, &var_count, af, nest_chain, diag) != 0) {
            cc_ssa_module_free(out);
            return -1;
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

