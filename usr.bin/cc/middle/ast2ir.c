#include "cc_frontend.h"
#include "cc_ssa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pointer_size_bytes = 8;

typedef struct {
    char *name;
    cc_type_t type;
    int struct_id;
    int value;
    int depth;
} var_entry_t;

typedef struct {
    char *name;
    int label;
} label_entry_t;

typedef struct {
    label_entry_t *labels;
    size_t label_count;
    const cc_function_t *fn;
} lower_ctx_t;

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

static void set_diag(cc_diag_t *d, const char *msg) {
    if (d == NULL || d->message[0] != '\0') {
        return;
    }
    d->line = 0;
    d->col = 0;
    snprintf(d->message, sizeof(d->message), "%s", msg);
}

static cc_value_type_t type_to_val(cc_type_t t) {
    return (t == CC_TYPE_FLOAT || t == CC_TYPE_DOUBLE) ? CC_VAL_F64 : CC_VAL_I64;
}

static int is_pointer_type(cc_type_t t) {
    return t >= CC_TYPE_PTR_VOID && t <= CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE;
}

static int is_unsigned_integral_type(cc_type_t t) {
    return t == CC_TYPE_UCHAR || t == CC_TYPE_USHORT || t == CC_TYPE_UINT || t == CC_TYPE_ULONG_LONG;
}

static int is_unsigned_load_type(cc_type_t t) {
    return t == CC_TYPE_BOOL || is_unsigned_integral_type(t) || is_pointer_type(t);
}

static cc_type_t ptr_base_type(cc_type_t t) {
    switch (t) {
    case CC_TYPE_PTR_VOID:
        return CC_TYPE_VOID;
    case CC_TYPE_PTR_BOOL:
        return CC_TYPE_BOOL;
    case CC_TYPE_PTR_CHAR:
        return CC_TYPE_CHAR;
    case CC_TYPE_PTR_UCHAR:
        return CC_TYPE_UCHAR;
    case CC_TYPE_PTR_SHORT:
        return CC_TYPE_SHORT;
    case CC_TYPE_PTR_USHORT:
        return CC_TYPE_USHORT;
    case CC_TYPE_PTR_INT:
        return CC_TYPE_INT;
    case CC_TYPE_PTR_UINT:
        return CC_TYPE_UINT;
    case CC_TYPE_PTR_LONG_LONG:
        return CC_TYPE_LONG_LONG;
    case CC_TYPE_PTR_ULONG_LONG:
        return CC_TYPE_ULONG_LONG;
    case CC_TYPE_PTR_FLOAT:
        return CC_TYPE_FLOAT;
    case CC_TYPE_PTR_DOUBLE:
        return CC_TYPE_DOUBLE;
    case CC_TYPE_PTR_PTR_VOID:
        return CC_TYPE_PTR_VOID;
    case CC_TYPE_PTR_PTR_BOOL:
        return CC_TYPE_PTR_BOOL;
    case CC_TYPE_PTR_PTR_CHAR:
        return CC_TYPE_PTR_CHAR;
    case CC_TYPE_PTR_PTR_UCHAR:
        return CC_TYPE_PTR_UCHAR;
    case CC_TYPE_PTR_PTR_SHORT:
        return CC_TYPE_PTR_SHORT;
    case CC_TYPE_PTR_PTR_USHORT:
        return CC_TYPE_PTR_USHORT;
    case CC_TYPE_PTR_PTR_INT:
        return CC_TYPE_PTR_INT;
    case CC_TYPE_PTR_PTR_UINT:
        return CC_TYPE_PTR_UINT;
    case CC_TYPE_PTR_PTR_LONG_LONG:
        return CC_TYPE_PTR_LONG_LONG;
    case CC_TYPE_PTR_PTR_ULONG_LONG:
        return CC_TYPE_PTR_ULONG_LONG;
    case CC_TYPE_PTR_PTR_FLOAT:
        return CC_TYPE_PTR_FLOAT;
    case CC_TYPE_PTR_PTR_DOUBLE:
        return CC_TYPE_PTR_DOUBLE;
    case CC_TYPE_PTR_PTR_PTR_VOID:
        return CC_TYPE_PTR_PTR_VOID;
    case CC_TYPE_PTR_PTR_PTR_BOOL:
        return CC_TYPE_PTR_PTR_BOOL;
    case CC_TYPE_PTR_PTR_PTR_CHAR:
        return CC_TYPE_PTR_PTR_CHAR;
    case CC_TYPE_PTR_PTR_PTR_UCHAR:
        return CC_TYPE_PTR_PTR_UCHAR;
    case CC_TYPE_PTR_PTR_PTR_SHORT:
        return CC_TYPE_PTR_PTR_SHORT;
    case CC_TYPE_PTR_PTR_PTR_USHORT:
        return CC_TYPE_PTR_PTR_USHORT;
    case CC_TYPE_PTR_PTR_PTR_INT:
        return CC_TYPE_PTR_PTR_INT;
    case CC_TYPE_PTR_PTR_PTR_UINT:
        return CC_TYPE_PTR_PTR_UINT;
    case CC_TYPE_PTR_PTR_PTR_LONG_LONG:
        return CC_TYPE_PTR_PTR_LONG_LONG;
    case CC_TYPE_PTR_PTR_PTR_ULONG_LONG:
        return CC_TYPE_PTR_PTR_ULONG_LONG;
    case CC_TYPE_PTR_PTR_PTR_FLOAT:
        return CC_TYPE_PTR_PTR_FLOAT;
    case CC_TYPE_PTR_PTR_PTR_DOUBLE:
        return CC_TYPE_PTR_PTR_DOUBLE;
    case CC_TYPE_PTR_PTR_PTR_PTR_VOID:
        return CC_TYPE_PTR_PTR_PTR_VOID;
    case CC_TYPE_PTR_PTR_PTR_PTR_BOOL:
        return CC_TYPE_PTR_PTR_PTR_BOOL;
    case CC_TYPE_PTR_PTR_PTR_PTR_CHAR:
        return CC_TYPE_PTR_PTR_PTR_CHAR;
    case CC_TYPE_PTR_PTR_PTR_PTR_UCHAR:
        return CC_TYPE_PTR_PTR_PTR_UCHAR;
    case CC_TYPE_PTR_PTR_PTR_PTR_SHORT:
        return CC_TYPE_PTR_PTR_PTR_SHORT;
    case CC_TYPE_PTR_PTR_PTR_PTR_USHORT:
        return CC_TYPE_PTR_PTR_PTR_USHORT;
    case CC_TYPE_PTR_PTR_PTR_PTR_INT:
        return CC_TYPE_PTR_PTR_PTR_INT;
    case CC_TYPE_PTR_PTR_PTR_PTR_UINT:
        return CC_TYPE_PTR_PTR_PTR_UINT;
    case CC_TYPE_PTR_PTR_PTR_PTR_LONG_LONG:
        return CC_TYPE_PTR_PTR_PTR_LONG_LONG;
    case CC_TYPE_PTR_PTR_PTR_PTR_ULONG_LONG:
        return CC_TYPE_PTR_PTR_PTR_ULONG_LONG;
    case CC_TYPE_PTR_PTR_PTR_PTR_FLOAT:
        return CC_TYPE_PTR_PTR_PTR_FLOAT;
    case CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE:
        return CC_TYPE_PTR_PTR_PTR_DOUBLE;
    default:
        return CC_TYPE_VOID;
    }
}

static cc_type_t integral_promo_type(cc_type_t t) {
    if (t == CC_TYPE_BOOL || t == CC_TYPE_CHAR || t == CC_TYPE_UCHAR || t == CC_TYPE_SHORT ||
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
    if (ap == CC_TYPE_UINT || bp == CC_TYPE_UINT) {
        return CC_TYPE_UINT;
    }
    return CC_TYPE_INT;
}

static long type_size_bytes(cc_type_t t) {
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_UCHAR:
        return 1;
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
        return 2;
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
    case CC_TYPE_FLOAT:
        return 4;
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
    case CC_TYPE_DOUBLE:
        return 8;
    case CC_TYPE_PTR_VOID:
    case CC_TYPE_PTR_BOOL:
    case CC_TYPE_PTR_CHAR:
    case CC_TYPE_PTR_UCHAR:
    case CC_TYPE_PTR_SHORT:
    case CC_TYPE_PTR_USHORT:
    case CC_TYPE_PTR_INT:
    case CC_TYPE_PTR_UINT:
    case CC_TYPE_PTR_LONG_LONG:
    case CC_TYPE_PTR_ULONG_LONG:
    case CC_TYPE_PTR_FLOAT:
    case CC_TYPE_PTR_DOUBLE:
    case CC_TYPE_PTR_PTR_VOID:
    case CC_TYPE_PTR_PTR_BOOL:
    case CC_TYPE_PTR_PTR_CHAR:
    case CC_TYPE_PTR_PTR_UCHAR:
    case CC_TYPE_PTR_PTR_SHORT:
    case CC_TYPE_PTR_PTR_USHORT:
    case CC_TYPE_PTR_PTR_INT:
    case CC_TYPE_PTR_PTR_UINT:
    case CC_TYPE_PTR_PTR_LONG_LONG:
    case CC_TYPE_PTR_PTR_ULONG_LONG:
    case CC_TYPE_PTR_PTR_FLOAT:
    case CC_TYPE_PTR_PTR_DOUBLE:
    case CC_TYPE_PTR_PTR_PTR_VOID:
    case CC_TYPE_PTR_PTR_PTR_BOOL:
    case CC_TYPE_PTR_PTR_PTR_CHAR:
    case CC_TYPE_PTR_PTR_PTR_UCHAR:
    case CC_TYPE_PTR_PTR_PTR_SHORT:
    case CC_TYPE_PTR_PTR_PTR_USHORT:
    case CC_TYPE_PTR_PTR_PTR_INT:
    case CC_TYPE_PTR_PTR_PTR_UINT:
    case CC_TYPE_PTR_PTR_PTR_LONG_LONG:
    case CC_TYPE_PTR_PTR_PTR_ULONG_LONG:
    case CC_TYPE_PTR_PTR_PTR_FLOAT:
    case CC_TYPE_PTR_PTR_PTR_DOUBLE:
    case CC_TYPE_PTR_PTR_PTR_PTR_VOID:
    case CC_TYPE_PTR_PTR_PTR_PTR_BOOL:
    case CC_TYPE_PTR_PTR_PTR_PTR_CHAR:
    case CC_TYPE_PTR_PTR_PTR_PTR_UCHAR:
    case CC_TYPE_PTR_PTR_PTR_PTR_SHORT:
    case CC_TYPE_PTR_PTR_PTR_PTR_USHORT:
    case CC_TYPE_PTR_PTR_PTR_PTR_INT:
    case CC_TYPE_PTR_PTR_PTR_PTR_UINT:
    case CC_TYPE_PTR_PTR_PTR_PTR_LONG_LONG:
    case CC_TYPE_PTR_PTR_PTR_PTR_ULONG_LONG:
    case CC_TYPE_PTR_PTR_PTR_PTR_FLOAT:
    case CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE:
        return g_pointer_size_bytes;
    default:
        return -1;
    }
}

static long type_size_bytes_with_struct(const cc_translation_unit_t *tu, cc_type_t t, int struct_id) {
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
static int lower_truthy_value(cc_ssa_function_t *sf, int v, cc_diag_t *diag);
static int lower_expr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                      var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag);

static int lower_find_label(const lower_ctx_t *ctx, const char *name) {
    size_t i;
    for (i = 0; i < ctx->label_count; ++i) {
        if (strcmp(ctx->labels[i].name, name) == 0) {
            return ctx->labels[i].label;
        }
    }
    return -1;
}

static int lower_collect_labels(cc_ssa_function_t *sf, const cc_stmt_t *s, lower_ctx_t *ctx, cc_diag_t *diag) {
    size_t i;
    if (s == NULL) {
        return 0;
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
                      int value, int depth) {
    var_entry_t *next;
    char *dup;

    next = (var_entry_t *)realloc(*vars, (*var_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    *vars = next;

    dup = xstrdup(name);
    if (dup == NULL) {
        return -1;
    }
    (*vars)[*var_count].name = dup;
    (*vars)[*var_count].type = type;
    (*vars)[*var_count].struct_id = struct_id;
    (*vars)[*var_count].value = value;
    (*vars)[*var_count].depth = depth;
    (*var_count)++;
    return 0;
}

static const cc_function_t *find_fn(const cc_translation_unit_t *tu, const char *name) {
    size_t i;
    if (tu == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < tu->func_count; ++i) {
        if (strcmp(tu->funcs[i].name, name) == 0) {
            return &tu->funcs[i];
        }
    }
    return NULL;
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

/*
 * The frontend currently encodes fixed-size arrays in structs as pointer-typed
 * members with expanded storage size. Treat those as address-valued in rvalue
 * contexts so `arr[i]` indexes the inline storage, not an uninitialized pointer.
 */
static int is_synthetic_struct_array_member(const cc_translation_unit_t *tu, const cc_expr_t *e) {
    const cc_struct_member_t *m;
    long logical_size;
    int sid;
    if (e == NULL || e->kind != CC_EXPR_MEMBER || e->ident == NULL) {
        return 0;
    }
    sid = member_base_struct_id(e);
    m = find_struct_member(tu, sid, e->ident);
    if (m == NULL || !is_pointer_type(m->type)) {
        return 0;
    }
    logical_size = type_size_bytes(m->type);
    if (logical_size <= 0) {
        return 0;
    }
    return m->size > logical_size;
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
    BUILTIN_EXPECT,
    BUILTIN_CONSTANT_P
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
    if (strcmp(name, "__builtin_expect") == 0) {
        return BUILTIN_EXPECT;
    }
    if (strcmp(name, "__builtin_constant_p") == 0) {
        return BUILTIN_CONSTANT_P;
    }
    return BUILTIN_NONE;
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
            if (g != NULL) {
                if (g->array_len >= 0 && is_pointer_type(g->type)) {
                    return emit_global_addr(sf, g->name, diag);
                }
                mem_size = type_size_bytes(g->type);
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
            if (diag != NULL && diag->message[0] == '\0') {
                snprintf(diag->message, sizeof(diag->message), "unknown identifier during AST->SSA lowering: %s",
                         e->ident);
            }
            return -1;
        }
        return vars[idx].value;
    }

    case CC_EXPR_ADDR: {
        int idx;
        if (e->lhs == NULL) {
            set_diag(diag, "malformed address-of expression in lowering");
            return -1;
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
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "unknown identifier during address-of lowering: %s",
                             e->lhs->ident);
                }
                return -1;
            }
            return emit_global_addr(sf, g->name, diag);
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

    case CC_EXPR_DEREF:
    {
        if (e->value_type == CC_TYPE_VOID && e->struct_id >= 0) {
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
        if (e->value_type == CC_TYPE_VOID && e->struct_id >= 0) {
            return lower_member_addr(tu, sf, ctx, vars, var_count, depth, e, diag);
        }
        long mem_size = type_size_bytes(e->value_type);
        int ptrv;
        if (is_synthetic_struct_array_member(tu, e)) {
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
                cmp_it = common_integral_type(e->lhs->value_type, e->rhs->value_type);
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

            if (is_pointer_type(e->lhs->value_type)) {
                ptrv = lhs;
                idxv = rhs;
            } else if (e->op == CC_BIN_ADD && is_pointer_type(e->rhs->value_type)) {
                ptrv = rhs;
                idxv = lhs;
            } else {
                set_diag(diag, "unsupported pointer arithmetic form in lowering");
                return -1;
            }

            ptrv = cast_value(sf, ptrv, CC_VAL_I64, diag);
            idxv = cast_value(sf, idxv, CC_VAL_I64, diag);
            if (ptrv < 0 || idxv < 0) {
                return -1;
            }

            {
                elem_size = pointer_elem_size_bytes(tu, e->value_type, e->struct_id);
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

        vt = type_to_val(e->value_type);
        lhs = cast_value(sf, lhs, vt, diag);
        rhs = cast_value(sf, rhs, vt, diag);
        if (lhs < 0 || rhs < 0) {
            return -1;
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
            cur_ap = cast_value(sf, vars[ap_idx].value, CC_VAL_I64, diag);
            if (cur_ap < 0) {
                return -1;
            }
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
            if (e->arg_count != 1 || e->args[0] == NULL) {
                set_diag(diag, "__builtin_constant_p lowering expects 1 argument");
                return -1;
            }
            if (e->args[0]->kind == CC_EXPR_INT || e->args[0]->kind == CC_EXPR_FLOAT || e->args[0]->kind == CC_EXPR_STR) {
                is_const = 1;
            }
            return emit_const_i64_instr(sf, is_const);
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
        if (e->ident == NULL) {
            if (e->lhs == NULL) {
                set_diag(diag, "malformed indirect call expression");
                return -1;
            }
            indirect_callee = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
            if (indirect_callee < 0) {
                return -1;
            }
            indirect_callee = cast_value(sf, indirect_callee, CC_VAL_I64, diag);
            if (indirect_callee < 0) {
                return -1;
            }
            in.op = CC_SSA_CALLI;
            in.lhs = indirect_callee;
            in.call_is_variadic = 0;
            in.call_fixed_count = 0;
            in.sym = NULL;
        } else {
            in.op = CC_SSA_CALL;
            in.call_is_variadic = callee != NULL ? callee->is_variadic : 0;
            in.call_fixed_count = (callee != NULL && callee->is_variadic) ? (int)callee->param_count : 0;
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
            if (e->ident != NULL && callee != NULL && i < callee->param_count) {
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
        } else {
            in.dst = new_value(sf, type_to_val(e->value_type));
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
                if (g->array_len >= 0 && is_pointer_type(g->type)) {
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
                cur = cast_value(sf, vars[idx].value, want, diag);
                if (cur < 0) {
                    return -1;
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

                if (e->update_postfix) {
                    int retv = new_value(sf, want);
                    if (retv < 0 || emit_mov_instr(sf, retv, vars[idx].value) != 0) {
                        return -1;
                    }
                    if (emit_mov_instr(sf, vars[idx].value, nextv) != 0) {
                        return -1;
                    }
                    return retv;
                }

                if (emit_mov_instr(sf, vars[idx].value, nextv) != 0) {
                    return -1;
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
                mem_size = type_size_bytes(g->type);
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
        if (e->lhs == NULL) {
            set_diag(diag, "malformed cast expression in lowering");
            return -1;
        }
        v = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        if (v < 0) {
            return -1;
        }
        if (e->aux_type == CC_TYPE_VOID) {
            return v;
        }
        return cast_value(sf, v, type_to_val(e->aux_type), diag);
    }

    case CC_EXPR_SIZEOF: {
        long n;
        if (e->lhs != NULL) {
            n = type_size_bytes_struct(tu, e->lhs->value_type, e->lhs->struct_id);
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
        set_diag(diag, "initializer list cannot be used as runtime expression in lowering");
        return -1;

    case CC_EXPR_TERNARY: {
        int cond;
        int dst;
        int l_true = new_label(sf);
        int l_false = new_label(sf);
        int l_end = new_label(sf);
        cc_value_type_t want;
        int tv;
        int fv;

        if (e->lhs == NULL || e->rhs == NULL || e->third == NULL) {
            set_diag(diag, "malformed conditional expression in lowering");
            return -1;
        }

        cond = lower_expr(tu, sf, ctx, vars, var_count, depth, e->lhs, diag);
        if (cond < 0) {
            return -1;
        }
        cond = lower_truthy_value(sf, cond, diag);
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
        tv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->rhs, diag);
        if (tv < 0) {
            return -1;
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
    size_t stmt_index;
    long value;
    int label;
    int is_default;
} switch_case_site_t;

static int collect_switch_case_sites(cc_ssa_function_t *sf, const cc_stmt_t *body, switch_case_site_t **out_sites,
                                     size_t *out_count, int *out_default_label, cc_diag_t *diag) {
    size_t i;
    switch_case_site_t *sites = NULL;
    size_t count = 0;
    int default_label = -1;

    if (body == NULL || body->kind != CC_STMT_BLOCK) {
        set_diag(diag, "switch lowering requires block body");
        return -1;
    }

    for (i = 0; i < body->block_count; ++i) {
        const cc_stmt_t *st = &body->block_stmts[i];
        switch_case_site_t *next;
        switch_case_site_t site;

        if (st->kind != CC_STMT_CASE && st->kind != CC_STMT_DEFAULT) {
            continue;
        }
        if (st->kind == CC_STMT_CASE && st->expr == NULL) {
            set_diag(diag, "malformed case label during lowering");
            free(sites);
            return -1;
        }
        if (st->kind == CC_STMT_DEFAULT && default_label >= 0) {
            set_diag(diag, "duplicate default labels in switch");
            free(sites);
            return -1;
        }

        memset(&site, 0, sizeof(site));
        site.stmt_index = i;
        site.value = st->kind == CC_STMT_CASE ? st->expr->int_val : 0;
        site.label = new_label(sf);
        site.is_default = (st->kind == CC_STMT_DEFAULT);
        if (site.label < 0) {
            set_diag(diag, "out of memory assigning switch label");
            free(sites);
            return -1;
        }

        next = (switch_case_site_t *)realloc(sites, (count + 1) * sizeof(*next));
        if (next == NULL) {
            set_diag(diag, "out of memory collecting switch labels");
            free(sites);
            return -1;
        }
        sites = next;
        sites[count++] = site;

        if (site.is_default) {
            default_label = site.label;
        }
    }

    *out_sites = sites;
    *out_count = count;
    *out_default_label = default_label;
    return 0;
}

static int find_switch_label_for_stmt(const switch_case_site_t *sites, size_t count, size_t stmt_index) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (sites[i].stmt_index == stmt_index) {
            return sites[i].label;
        }
    }
    return -1;
}

static int lower_struct_init_to_ptr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                    var_entry_t *vars, size_t var_count, int depth, int base_ptr, int struct_id,
                                    const cc_expr_t *init, cc_diag_t *diag) {
    const cc_struct_def_t *sd;
    size_t i;

    if (tu == NULL || struct_id < 0 || (size_t)struct_id >= tu->struct_count) {
        set_diag(diag, "invalid struct type during initializer lowering");
        return -1;
    }
    if (init == NULL || init->kind != CC_EXPR_INIT_LIST) {
        set_diag(diag, "struct initializer lowering requires initializer list");
        return -1;
    }

    sd = &tu->structs[struct_id];
    for (i = 0; i < init->arg_count && i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        const cc_expr_t *item = init->args[i];
        int field_ptr = base_ptr;
        cc_ssa_instr_t in;

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
            if (item->kind != CC_EXPR_INIT_LIST) {
                set_diag(diag, "nested struct initializer requires braces");
                return -1;
            }
            if (lower_struct_init_to_ptr(tu, sf, ctx, vars, var_count, depth, field_ptr, m->type_struct_id, item,
                                         diag) != 0) {
                return -1;
            }
        } else {
            int v;
            long mem_size = type_size_bytes_with_struct(tu, m->type, m->type_struct_id);
            if (mem_size <= 0) {
                mem_size = m->size;
            }
            if (mem_size <= 0) {
                set_diag(diag, "unsupported struct member size in initializer lowering");
                return -1;
            }
            v = lower_expr(tu, sf, ctx, vars, var_count, depth, item, diag);
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
        if (s->type == CC_TYPE_VOID && s->type_struct_id >= 0) {
            cc_ssa_instr_t call_in;
            int onev;
            int sizev;
            long sz = type_size_bytes_with_struct(tu, s->type, s->type_struct_id);
            if (sz <= 0) {
                set_diag(diag, "unsupported local struct declaration in lowering");
                return -1;
            }
            onev = emit_const_i64_instr(sf, 1);
            if (onev < 0) {
                return -1;
            }
            sizev = emit_const_i64_instr(sf, sz);
            if (sizev < 0) {
                return -1;
            }
            memset(&call_in, 0, sizeof(call_in));
            call_in.op = CC_SSA_CALL;
            call_in.call_is_variadic = 0;
            call_in.sym = xstrdup("calloc");
            call_in.arg_count = 2;
            call_in.args = (int *)calloc(2, sizeof(*call_in.args));
            call_in.dst = new_value(sf, CC_VAL_I64);
            if (call_in.sym == NULL || call_in.args == NULL || call_in.dst < 0) {
                free(call_in.sym);
                free(call_in.args);
                set_diag(diag, "out of memory allocating local struct storage call");
                return -1;
            }
            call_in.args[0] = onev;
            call_in.args[1] = sizev;
            if (push_instr(sf, call_in) != 0) {
                free(call_in.sym);
                free(call_in.args);
                set_diag(diag, "out of memory appending local struct storage call");
                return -1;
            }
            varv = call_in.dst;
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
            if (var_define(vars, var_count, s->decl_name, s->type, s->type_struct_id, varv, depth) != 0) {
                set_diag(diag, "out of memory defining local struct variable");
                return -1;
            }
            return 0;
        }
        if (s->array_len >= 0 && is_pointer_type(s->type)) {
            cc_ssa_instr_t call_in;
            cc_ssa_instr_t st_in;
            cc_type_t elem_type = ptr_base_type(s->type);
            long elem_size = type_size_bytes_with_struct(tu, elem_type, s->type_struct_id);
            long arr_elems = s->array_len > 0 ? s->array_len : 1;
            long total_size;
            int onev;
            int bytesv;

            if (elem_size <= 0) {
                set_diag(diag, "unsupported local array element type in lowering");
                return -1;
            }
            if (arr_elems <= 0) {
                arr_elems = 1;
            }
            total_size = elem_size * arr_elems;
            if (total_size <= 0) {
                set_diag(diag, "invalid local array size in lowering");
                return -1;
            }

            onev = emit_const_i64_instr(sf, 1);
            if (onev < 0) {
                return -1;
            }
            bytesv = emit_const_i64_instr(sf, total_size);
            if (bytesv < 0) {
                return -1;
            }

            memset(&call_in, 0, sizeof(call_in));
            call_in.op = CC_SSA_CALL;
            call_in.call_is_variadic = 0;
            call_in.sym = xstrdup("calloc");
            call_in.arg_count = 2;
            call_in.args = (int *)calloc(2, sizeof(*call_in.args));
            call_in.dst = new_value(sf, CC_VAL_I64);
            if (call_in.sym == NULL || call_in.args == NULL || call_in.dst < 0) {
                free(call_in.sym);
                free(call_in.args);
                set_diag(diag, "out of memory allocating local array storage call");
                return -1;
            }
            call_in.args[0] = onev;
            call_in.args[1] = bytesv;
            if (push_instr(sf, call_in) != 0) {
                free(call_in.sym);
                free(call_in.args);
                set_diag(diag, "out of memory appending local array storage call");
                return -1;
            }
            varv = call_in.dst;

            if (s->expr != NULL) {
                if (s->expr->kind == CC_EXPR_INIT_LIST) {
                    size_t ii;
                    for (ii = 0; ii < s->expr->arg_count; ++ii) {
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
                            if (s->expr->args[ii]->kind != CC_EXPR_INIT_LIST) {
                                set_diag(diag, "struct array initializer element requires braces");
                                return -1;
                            }
                            if (lower_struct_init_to_ptr(tu, sf, ctx, *vars, *var_count, depth, item_ptr,
                                                         s->type_struct_id, s->expr->args[ii], diag) != 0) {
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

            if (var_define(vars, var_count, s->decl_name, s->type, s->type_struct_id, varv, depth) != 0) {
                set_diag(diag, "out of memory defining local array variable");
                return -1;
            }
            return 0;
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
            varv = new_value(sf, type_to_val(s->type));
            if (varv < 0) {
                set_diag(diag, "out of memory allocating local variable value");
                return -1;
            }
            if (emit_mov_instr(sf, varv, v) != 0) {
                set_diag(diag, "out of memory appending declaration move");
                return -1;
            }
        } else {
            cc_ssa_instr_t in;
            varv = new_value(sf, type_to_val(s->type));
            if (varv < 0) {
                set_diag(diag, "out of memory allocating local variable value");
                return -1;
            }
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
        if (var_define(vars, var_count, s->decl_name, s->type, s->type_struct_id, varv, depth) != 0) {
            set_diag(diag, "out of memory defining local variable");
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
        int cond;

        if (l_end < 0) {
            set_diag(diag, "out of memory assigning switch end label");
            return -1;
        }
        if (s->then_branch == NULL || s->then_branch->kind != CC_STMT_BLOCK) {
            set_diag(diag, "switch lowering requires block body");
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
            if (cmp_labels == NULL || case_labels == NULL || case_values == NULL) {
                free(sites);
                free(cmp_labels);
                free(case_labels);
                free(case_values);
                set_diag(diag, "out of memory lowering switch dispatch");
                return -1;
            }

            for (j2 = 0; j2 < site_count; ++j2) {
                if (!sites[j2].is_default) {
                    case_labels[case_i] = sites[j2].label;
                    case_values[case_i] = sites[j2].value;
                    cmp_labels[case_i] = new_label(sf);
                    if (cmp_labels[case_i] < 0) {
                        free(sites);
                        free(cmp_labels);
                        free(case_labels);
                        free(case_values);
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
                    set_diag(diag, "out of memory appending switch case constant");
                    return -1;
                }

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
                    set_diag(diag, "out of memory appending switch compare");
                    return -1;
                }
                cmpv = in.dst;
                false_label = (case_i + 1 < case_count) ? cmp_labels[case_i + 1]
                                                        : (default_label >= 0 ? default_label : l_end);
                if (emit_br_cond_instr(sf, cmpv, case_labels[case_i], false_label) != 0) {
                    free(sites);
                    free(cmp_labels);
                    free(case_labels);
                    free(case_values);
                    return -1;
                }
                if (case_i + 1 < case_count && emit_label_instr(sf, cmp_labels[case_i + 1]) != 0) {
                    free(sites);
                    free(cmp_labels);
                    free(case_labels);
                    free(case_values);
                    return -1;
                }
            }
        }

        for (j2 = 0; j2 < s->then_branch->block_count; ++j2) {
            const cc_stmt_t *bst = &s->then_branch->block_stmts[j2];
            int case_label = find_switch_label_for_stmt(sites, site_count, j2);
            if (case_label >= 0) {
                if (emit_label_instr(sf, case_label) != 0) {
                    free(sites);
                    free(cmp_labels);
                    free(case_labels);
                    free(case_values);
                    return -1;
                }
                continue;
            }
            if (lower_stmt(tu, sf, vars, var_count, ctx, depth + 1, l_end, continue_label, bst, saw_ret, diag) != 0) {
                free(sites);
                free(cmp_labels);
                free(case_labels);
                free(case_values);
                return -1;
            }
        }
        if (emit_label_instr(sf, l_end) != 0) {
            free(sites);
            free(cmp_labels);
            free(case_labels);
            free(case_values);
            return -1;
        }
        free(sites);
        free(cmp_labels);
        free(case_labels);
        free(case_values);
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
        for (j = 0; j < s->block_count; ++j) {
            if (lower_stmt(tu, sf, vars, var_count, ctx, depth + 1, break_label, continue_label, &s->block_stmts[j],
                           saw_ret, diag) != 0) {
                return -1;
            }
        }
        while (*var_count > saved) {
            (*var_count)--;
            free((*vars)[*var_count].name);
        }
        return 0;
    }

    if (s->kind == CC_STMT_CASE || s->kind == CC_STMT_DEFAULT) {
        set_diag(diag, "case/default label used outside switch lowering context");
        return -1;
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

static int eval_global_init_expr(const cc_expr_t *e, long *out_i, double *out_f, int *out_is_float) {
    if (e == NULL) {
        *out_i = 0;
        *out_f = 0.0;
        *out_is_float = 0;
        return 0;
    }
    switch (e->kind) {
    case CC_EXPR_INT:
        *out_i = e->int_val;
        *out_f = (double)e->int_val;
        *out_is_float = 0;
        return 0;
    case CC_EXPR_FLOAT:
        *out_i = (long)e->float_val;
        *out_f = e->float_val;
        *out_is_float = 1;
        return 0;
    case CC_EXPR_CAST:
        return eval_global_init_expr(e->lhs, out_i, out_f, out_is_float);
    case CC_EXPR_BIN:
        if (e->op == CC_BIN_COMMA) {
            return eval_global_init_expr(e->rhs, out_i, out_f, out_is_float);
        }
        return -1;
    default:
        return -1;
    }
}

static int eval_global_init_item(const cc_expr_t *e, cc_ssa_global_init_item_t *out) {
    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (e == NULL) {
        return 0;
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
        return eval_global_init_item(e->lhs, out);
    case CC_EXPR_BIN:
        if (e->op == CC_BIN_COMMA) {
            return eval_global_init_item(e->rhs, out);
        }
        return -1;
    default:
        return -1;
    }
}

int cc_ast_to_ssa(const cc_translation_unit_t *tu, cc_ssa_module_t *out, cc_diag_t *diag) {
    size_t i;
    size_t def_count = 0;
    size_t out_i = 0;

    cc_ssa_module_init(out);
    if (diag != NULL) {
        diag->line = 0;
        diag->col = 0;
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
            const cc_expr_t *init = tu->globals[i].init;
            out->globals[i].name = xstrdup(tu->globals[i].name);
            out->globals[i].type = tu->globals[i].type;
            out->globals[i].type_struct_id = tu->globals[i].type_struct_id;
            out->globals[i].array_len = tu->globals[i].array_len;
            out->globals[i].storage = tu->globals[i].storage;
            if (out->globals[i].name == NULL) {
                set_diag(diag, "out of memory duplicating global name");
                cc_ssa_module_free(out);
                return -1;
            }
            if (init != NULL && init->kind == CC_EXPR_INIT_LIST) {
                size_t j;
                if (!is_pointer_type(out->globals[i].type) || out->globals[i].array_len < 0) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "unsupported list initializer for non-array global %s", tu->globals[i].name);
                    }
                    cc_ssa_module_free(out);
                    return -1;
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
                    if (eval_global_init_item(init->args[j], &out->globals[i].init_items[j]) != 0) {
                        if (diag != NULL && diag->message[0] == '\0') {
                            snprintf(diag->message, sizeof(diag->message),
                                     "unsupported global initializer item %zu for %s", j, tu->globals[i].name);
                        }
                        cc_ssa_module_free(out);
                        return -1;
                    }
                }
                if (out->globals[i].array_len <= 0) {
                    out->globals[i].array_len = (long)init->arg_count;
                }
            } else if (init != NULL && init->kind == CC_EXPR_STR) {
                init_is_string = 1;
                out->globals[i].init_str = xstrdup(init->ident != NULL ? init->ident : "\"\"");
                if (out->globals[i].init_str == NULL) {
                    set_diag(diag, "out of memory duplicating global string initializer");
                    cc_ssa_module_free(out);
                    return -1;
                }
            } else if (eval_global_init_expr(init, &init_i, &init_f, &init_is_float) != 0) {
                if (diag != NULL && diag->message[0] == '\0') {
                    snprintf(diag->message, sizeof(diag->message), "unsupported global initializer for %s",
                             tu->globals[i].name);
                }
                cc_ssa_module_free(out);
                return -1;
            }
            out->globals[i].has_init = (init != NULL);
            out->globals[i].init_i = init_i;
            out->globals[i].init_f = init_f;
            out->globals[i].init_is_float = init_is_float;
            out->globals[i].init_is_string = init_is_string;
        }
    }

    for (i = 0; i < tu->func_count; ++i) {
        if (tu->funcs[i].has_body) {
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
        int saw_ret = 0;

        if (!af->has_body) {
            continue;
        }
        sf = &out->funcs[out_i++];

        memset(&lctx, 0, sizeof(lctx));
        lctx.fn = af;

        sf->name = xstrdup(af->name);
        if (sf->name == NULL) {
            set_diag(diag, "out of memory duplicating function name");
            cc_ssa_module_free(out);
            return -1;
        }
        sf->ret_type = (af->ret_type == CC_TYPE_FLOAT || af->ret_type == CC_TYPE_DOUBLE) ? CC_VAL_F64 : CC_VAL_I64;
        sf->storage = af->storage;
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
                           v, 0) != 0) {
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
            if (lower_stmt(tu, sf, &vars, &var_count, &lctx, 0, -1, -1, &af->stmts[j], &saw_ret, diag) != 0) {
                cc_ssa_module_free(out);
                return -1;
            }
        }

        if (sf->instr_count == 0 || sf->instrs[sf->instr_count - 1].op != CC_SSA_RET) {
            if (append_default_return(sf) != 0) {
                set_diag(diag, "out of memory appending default return");
                cc_ssa_module_free(out);
                return -1;
            }
        }

        for (j = 0; j < var_count; ++j) {
            free(vars[j].name);
        }
        free(vars);
        for (j = 0; j < lctx.label_count; ++j) {
            free(lctx.labels[j].name);
        }
        free(lctx.labels);
    }

    return 0;
}

void cc_ssa_set_pointer_size(int bytes) {
    if (bytes == 4 || bytes == 8) {
        g_pointer_size_bytes = bytes;
    }
}
