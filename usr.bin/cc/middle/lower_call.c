/*
 * lower_call.c: lowering a call -- the builtins the compiler expands itself
 * (lower_builtin_call()) and the ordinary call, its arguments passed as the
 * target's ABI says (lower_call_expr()).
 */

#include "cc_frontend.h"
#include "cc_abi.h"
#include "cc_irbuild.h"
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
#include "cc_lower.h"


/** lower_builtin_call() made no builtin of the call: lower it as an ordinary call. */
#define LOWER_NOT_BUILTIN (-2)

static int global_is_pointer_object(const cc_global_t *g);
static const cc_ctype_t *lower_atomic_elem_ctype(const cc_expr_t *e);
static int eval_const_int_expr_simple(const cc_expr_t *e, long long *out);
static int emit_atomic_compiler_barrier(cc_ssa_function_t *sf, cc_diag_t *diag);
static int emit_atomic_full_barrier(cc_ssa_function_t *sf, cc_diag_t *diag);
static int emit_local_var_scalar_load(cc_ssa_function_t *sf, const var_entry_t *v, cc_diag_t *diag);
static int emit_local_var_scalar_store(cc_ssa_function_t *sf, const var_entry_t *v, int rhs, cc_diag_t *diag);
static int pointed_object_extent(const cc_translation_unit_t *tu, const cc_expr_t *e, long *out_size,
                                 long *out_offset);
static int builtin_bswap_bits(const char *name);
static builtin_kind_t builtin_kind(const char *name);
static int function_param_index_by_name(const lower_ctx_t *ctx, const char *name);
static int builtin_bitop_width(const cc_expr_t *e);
static int lower_popcount(cc_ssa_function_t *sf, int v, int bits, cc_diag_t *diag);
static int lower_builtin_ctz_from_normalized(cc_ssa_function_t *sf, int x, int bits, cc_diag_t *diag);
static int lower_builtin_ctz(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                             var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag);
static int lower_builtin_ffs(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                             var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag);
static int lower_builtin_clz(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                             var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag);
static int lower_builtin_call(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                              var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e,
                              builtin_kind_t bk, int bswap_bits, cc_ssa_instr_t *in_io, cc_diag_t *diag);
static int emit_const_ldouble_special_instr(cc_ssa_function_t *sf, long code);
static int lower_fp_builtin_arg(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *arg,
                                const cc_ctype_t **out_storage_type, cc_diag_t *diag);
static int emit_fp_signbit_from_value(cc_ssa_function_t *sf, int v, const cc_ctype_t *store_t, cc_diag_t *diag);
static int emit_fp_isinf_sign_from_value(cc_ssa_function_t *sf, int v, const cc_ctype_t *store_t, cc_diag_t *diag);

/**
 * Whether a file-scope variable is a pointer object: its declared type,
 * not decayed, is a pointer (an array is not one).
 *
 * @param g  the variable
 * @return   1 or 0
 */
static int global_is_pointer_object(const cc_global_t *g) {
    const cc_ctype_t *t = global_decl_ctype(g);

    return t != NULL && t->kind == CC_CT_POINTER;
}

/**
 * The structure a parameter is passed as (ct_record_id()).
 *
 * @param p  the parameter
 * @return   the structure's index, or -1 if the parameter is not one
 */
int param_record_id(const cc_param_t *p) {
    return ct_record_id(lower_param_ctype(p));
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
    if (cc_ir_push(sf, in) != 0) {
        free_asm_instr_fields(&in);
        set_diag(diag, "out of memory appending atomic compiler barrier");
        return -1;
    }
    return 0;
}

static int emit_atomic_full_barrier(cc_ssa_function_t *sf, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    const char *tmpl = cc_target_current()->full_barrier_asm;

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
    if (cc_ir_push(sf, in) != 0) {
        free_asm_instr_fields(&in);
        set_diag(diag, "out of memory appending atomic full barrier");
        return -1;
    }
    return 0;
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
    in.dst = cc_ir_new_value(sf, vt);
    in.lhs = v->addr_value;
    in.rhs = -1;
    in.imm = size;
    in.is_unsigned = ct_loads_unsigned(lower_var_ctype(v)) ? 1 : 0;
    if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
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
        if (cc_ir_mov(sf, v->value, rhs) != 0) {
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
    if (cc_ir_push(sf, in) != 0) {
        set_diag(diag, "out of memory storing local scalar variable");
        return -1;
    }
    return 0;
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
        t = x->ctype;
        *out_size = cc_ct_size(t, tu);
        *out_offset = 0;
        return *out_size > 0;
    }
    if (e->kind == CC_EXPR_BIN && (e->op == CC_BIN_ADD || e->op == CC_BIN_SUB) && e->lhs != NULL &&
        e->rhs != NULL && e->rhs->kind == CC_EXPR_INT) {
        t = cc_ct_decay(e->lhs->ctype);
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
    t = e->ctype;
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

static int builtin_bswap_bits(const char *name) {
    return(cc_builtin_bswap_bits(name));
}

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
        m1 = cc_ir_const(sf, 0x55555555);
        m2 = cc_ir_const(sf, 0x33333333);
        m4 = cc_ir_const(sf, 0x0f0f0f0f);
        h01 = cc_ir_const(sf, 0x01010101);
    }
    c1 = cc_ir_const(sf, 1);
    c2 = cc_ir_const(sf, 2);
    c4 = cc_ir_const(sf, 4);
    ctop = cc_ir_const(sf, wide ? 56 : 24);
    cff = wide ? emit_const_u64_instr(sf, 0xff, diag) : cc_ir_const(sf, 0xff);
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
    zero = bits > 32 ? emit_const_u64_instr(sf, 0, diag) : cc_ir_const(sf, 0);
    one = bits > 32 ? emit_const_u64_instr(sf, 1, diag) : cc_ir_const(sf, 1);
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
        /* the operand's bits */
        v = emit_ext(sf, v, bits, 1, 8, diag);
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
        /* the operand's bits */
        x = emit_ext(sf, x, bits, 1, 8, diag);
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
        /* the operand's bits */
        x = emit_ext(sf, x, bits, 1, 8, diag);
        if (x < 0) {
            return -1;
        }
    }

    /* as wide as x, or i386 compares a 64-bit x against half a zero */
    z = cc_ir_const(sf, 0);
    one = cc_ir_const(sf, 1);
    if (z < 0 || one < 0) {
        return -1;
    }
    cc_ir_set_size(sf, z, value_size(sf, x));
    cc_ir_set_size(sf, one, value_size(sf, x));

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CMP;
    in.cmp_kind = CC_CMP_NE;
    in.is_unsigned = 1;
    in.dst = cc_ir_new_value(sf, CC_VAL_I64);
    in.lhs = x;
    in.rhs = z;
    if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
        set_diag(diag, "out of memory lowering __builtin_ffs compare");
        return -1;
    }
    cmp = in.dst;

    dst = cc_ir_new_value(sf, CC_VAL_I64);
    l_nz = cc_ir_new_label(sf);
    l_z = cc_ir_new_label(sf);
    l_end = cc_ir_new_label(sf);
    if (dst < 0 || cc_ir_br_cond(sf, cmp, l_nz, l_z) != 0 || cc_ir_label(sf, l_nz) != 0) {
        set_diag(diag, "out of memory lowering __builtin_ffs control flow");
        return -1;
    }

    ctz = lower_builtin_ctz_from_normalized(sf, x, bits, diag);
    if (ctz < 0) {
        return -1;
    }
    nz = emit_i64_binop_instr(sf, CC_SSA_ADD, ctz, one, 0, diag);
    if (nz < 0 || cc_ir_mov(sf, dst, nz) != 0 || cc_ir_br(sf, l_end) != 0 || cc_ir_label(sf, l_z) != 0 ||
        cc_ir_mov(sf, dst, z) != 0 || cc_ir_br(sf, l_end) != 0 || cc_ir_label(sf, l_end) != 0) {
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
        /* the operand's bits */
        x = emit_ext(sf, x, bits, 1, 8, diag);
        if (x < 0) {
            return -1;
        }
    }

    c1 = cc_ir_const(sf, 1);
    c2 = cc_ir_const(sf, 2);
    c4 = cc_ir_const(sf, 4);
    c8 = cc_ir_const(sf, 8);
    c16 = cc_ir_const(sf, 16);
    c32 = cc_ir_const(sf, 32);
    cbits = cc_ir_const(sf, bits);
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
    cc_ir_set_size(sf, cbits, value_size(sf, pc)); /* made early, so as wide as the count it is taken from here */
    return emit_i64_binop_instr(sf, CC_SSA_SUB, cbits, pc, 0, diag);
}

/**
 * Lower a call of a builtin the compiler implements itself (va_*, alloca,
 * the bit, overflow, atomic, memory and floating-point builtins, bswap).
 *
 * @param tu          the translation unit
 * @param sf          the function being lowered
 * @param ctx         the lowering context
 * @param vars        the variables in scope
 * @param var_count   their number
 * @param depth       the scope depth
 * @param e           the call
 * @param bk          which builtin it names, BUILTIN_NONE for none
 * @param bswap_bits  the width of a __builtin_bswapN it names, else 0
 * @param in_io       the call's instruction; a builtin that is not
 *                    expanded here leaves its changes to it
 * @param diag        receives an error
 * @return            the value, -1 on error, or LOWER_NOT_BUILTIN when the
 *                    call is to be made as an ordinary call
 */
static int lower_builtin_call(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                              var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e,
                              builtin_kind_t bk, int bswap_bits, cc_ssa_instr_t *in_io, cc_diag_t *diag) {
    cc_ssa_instr_t in = *in_io;

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
        in.dst = cc_ir_new_value(sf, CC_VAL_I64);
        in.imm = (long)(last_idx + 1);
        if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
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
        return cc_ir_const(sf, 0);
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
         * Where a va_list points at the ABI's state record (x86-64:
         * gp_offset, fp_offset, overflow_arg_area, reg_save_area),
         * va_copy duplicates the record, so that advancing one list does
         * not advance the other.
         */
        if (cc_target_current()->va_list_record_size > 0) {
            long rec = cc_target_current()->va_list_record_size;

            copy_src = srcv;
            copy_dst = cc_ir_stackalloc(sf, rec, diag);
            if (copy_dst < 0) {
                return -1;
            }
            if (emit_memcpy_instr(sf, copy_dst, copy_src, rec, diag) != 0) {
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
        int next_ap;
        long mem_size;
        cc_abi_va_type_t va_type;
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
        va_type.size = mem_size;
        va_type.value_kind = lower_value_kind(e);
        va_type.is_aggregate = lower_record_id(e) >= 0;
        va_type.is_fp = lower_has_kind(e, CC_CT_DOUBLE) || lower_has_kind(e, CC_CT_FLOAT);
        va_type.is_ldouble = lower_has_kind(e, CC_CT_LDOUBLE);
        va_type.is_unsigned = lower_loads_unsigned(e) ? 1 : 0;

        cur_ap = emit_local_var_scalar_load(sf, &vars[ap_idx], diag);
        if (cur_ap < 0) {
            return -1;
        }
        cur_ap = cast_value(sf, cur_ap, CC_VAL_I64, diag);
        if (cur_ap < 0) {
            return -1;
        }
        /* the target's ABI reads the argument and says how the va_list moves */
        load_v = cc_abi_va_arg(sf, cur_ap, &va_type, &next_ap, diag);
        if (load_v < 0) {
            return -1;
        }
        if (next_ap >= 0 && emit_local_var_scalar_store(sf, &vars[ap_idx], next_ap, diag) != 0) {
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
        return cc_ir_const(sf, is_const);
    }
    if (bk == BUILTIN_TRAP) {
        if (cc_ir_trap(sf) != 0) {
            set_diag(diag, "out of memory emitting __builtin_trap");
            return -1;
        }
        return cc_ir_const(sf, 0);
    }
    if (bk == BUILTIN_UNREACHABLE) {
        if (cc_ir_trap(sf) != 0) {
            set_diag(diag, "out of memory emitting __builtin_unreachable");
            return -1;
        }
        return cc_ir_const(sf, 0);
    }
    if (bk == BUILTIN_ASSUME) {
        if (e->arg_count != 1) {
            set_diag(diag, "__builtin_assume lowering expects 1 argument");
            return -1;
        }
        if (lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag) < 0) {
            return -1;
        }
        return cc_ir_const(sf, 0);
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
        const cc_ctype_t *store_t;

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
            int zero = cc_ir_const(sf, 0);
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
            int mask = cc_ir_const(sf, (long long)((1ULL << bits) - 1ULL));
            if (mask < 0 || (cnt = emit_i64_binop_instr(sf, CC_SSA_AND, v, mask, 1, diag)) < 0) {
                return -1;
            }
        }
        cnt = lower_popcount(sf, cnt, bits, diag);
        if (cnt >= 0 && sign_extends) {
            int zero = cc_ir_const(sf, 0);
            int ext = cc_ir_const(sf, builtin_bitop_width(e) - arg_bits);
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
            int mask = cc_ir_const(sf, (long long)((1ULL << bits) - 1ULL));
            if (mask < 0 || (v = emit_i64_binop_instr(sf, CC_SSA_AND, v, mask, 1, diag)) < 0) {
                return -1;
            }
        }
        cnt = lower_popcount(sf, v, bits, diag);
        one = cc_ir_const(sf, 1);
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
                                      cc_ct_basic(lower_is_unsigned(e->args[0]) ? CC_CT_ULLONG : CC_CT_LLONG), diag);
        bv = normalize_integral_value(sf, bv,
                                      cc_ct_basic(lower_is_unsigned(e->args[1]) ? CC_CT_ULLONG : CC_CT_LLONG), diag);
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
        bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
        bin.lhs = av;
        bin.rhs = bv;
        if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
            return -1;
        }
        cc_ir_set_size(sf, bin.dst, 8);
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
            out_bits = integral_type_bits(out_ct);
        }
        if (has_store) {
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_STORE;
            in.lhs = pv;
            in.rhs = rv;
            in.imm = mem_size;
            if (cc_ir_push(sf, in) != 0) {
                return -1;
            }
        }

        /* the overflow check: values from here on are 64-bit (see the end) */
        first_instr = sf->instr_count;
        if (out_bits > 0 && out_bits < 64) {
            /* overflowed if the result, widened back from its type, is not the exact one */
            int rv_wide =
                normalize_integral_value(sf, rv, cc_ct_basic(out_unsigned ? CC_CT_ULLONG : CC_CT_LLONG), diag);
            if (rv_wide < 0) {
                return -1;
            }
            memset(&bin, 0, sizeof(bin));
            bin.op = CC_SSA_CMP;
            bin.cmp_kind = CC_CMP_NE;
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = rv_exact;
            bin.rhs = rv_wide;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            ov = bin.dst;
        } else if (out_unsigned) {
            if (overflow_op == 2) {
                int z = cc_ir_const(sf, 0);
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
                bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
                bin.lhs = bv;
                bin.rhs = z;
                if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                    return -1;
                }
                cmp_nz = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_EQ;
                bin.is_unsigned = 1;
                bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
                bin.lhs = bv;
                bin.rhs = z;
                if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                    return -1;
                }
                cmp_z = normalize_integral_value(sf, bin.dst, cc_ct_basic(CC_CT_ULLONG), diag);
                if (cmp_z < 0) {
                    return -1;
                }
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_OR;
                bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
                bin.lhs = bv;
                bin.rhs = cmp_z;
                if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                    return -1;
                }
                safe_bv = bin.dst;
                cc_ir_set_size(sf, safe_bv, 8);
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_DIV;
                bin.is_unsigned = 1;
                bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
                bin.lhs = rv;
                bin.rhs = safe_bv;
                if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                    return -1;
                }
                qv = bin.dst;
                cc_ir_set_size(sf, qv, 8);
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_NE;
                bin.is_unsigned = 1;
                bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
                bin.lhs = qv;
                bin.rhs = av;
                if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                    return -1;
                }
                cmp_ne = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_AND;
                bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
                bin.lhs = cmp_nz;
                bin.rhs = cmp_ne;
                if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                    return -1;
                }
                ov = bin.dst;
            } else {
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_LT;
                bin.is_unsigned = 1;
                bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
                bin.lhs = (overflow_op == 1) ? av : rv;
                bin.rhs = (overflow_op == 1) ? bv : av;
                if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                    return -1;
                }
                ov = bin.dst;
            }
            {
                int z = cc_ir_const(sf, 0);
                int exact_neg;
                if (z < 0) {
                    return -1;
                }
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_CMP;
                bin.cmp_kind = CC_CMP_LT;
                bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
                bin.lhs = rv_exact;
                bin.rhs = z;
                if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                    return -1;
                }
                exact_neg = bin.dst;
                memset(&bin, 0, sizeof(bin));
                bin.op = CC_SSA_OR;
                bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
                bin.lhs = ov;
                bin.rhs = exact_neg;
                if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                    return -1;
                }
                ov = bin.dst;
            }
        } else if (overflow_op == 2) {
            int z = cc_ir_const(sf, 0);
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
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = bv;
            bin.rhs = z;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            cmp_nz = bin.dst;
            memset(&bin, 0, sizeof(bin));
            bin.op = CC_SSA_CMP;
            bin.cmp_kind = CC_CMP_EQ;
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = bv;
            bin.rhs = z;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            cmp_z = normalize_integral_value(sf, bin.dst, cc_ct_basic(CC_CT_ULLONG), diag);
            if (cmp_z < 0) {
                return -1;
            }
            memset(&bin, 0, sizeof(bin));
            bin.op = CC_SSA_OR;
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = bv;
            bin.rhs = cmp_z;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            safe_bv = bin.dst;
            cc_ir_set_size(sf, safe_bv, 8);
            memset(&bin, 0, sizeof(bin));
            bin.op = CC_SSA_DIV;
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = rv;
            bin.rhs = safe_bv;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            qv = bin.dst;
            cc_ir_set_size(sf, qv, 8);
            memset(&bin, 0, sizeof(bin));
            bin.op = CC_SSA_CMP;
            bin.cmp_kind = CC_CMP_NE;
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = qv;
            bin.rhs = av;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            cmp_eq = bin.dst;
            memset(&bin, 0, sizeof(bin));
            bin.op = CC_SSA_AND;
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = cmp_nz;
            bin.rhs = cmp_eq;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
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
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = rv;
            bin.rhs = cc_ir_const(sf, 0);
            if (bin.rhs < 0 || bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            sign_r = bin.dst;
            memset(&bin, 0, sizeof(bin));
            bin.op = CC_SSA_CMP;
            bin.cmp_kind = CC_CMP_LT;
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = av;
            bin.rhs = cc_ir_const(sf, 0);
            if (bin.rhs < 0 || bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            sign_a = bin.dst;
            memset(&bin, 0, sizeof(bin));
            bin.op = CC_SSA_CMP;
            bin.cmp_kind = CC_CMP_LT;
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = bv;
            bin.rhs = cc_ir_const(sf, 0);
            if (bin.rhs < 0 || bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            sign_b = bin.dst;
            memset(&bin, 0, sizeof(bin));
            bin.op = CC_SSA_CMP;
            bin.cmp_kind = (overflow_op == 0) ? CC_CMP_EQ : CC_CMP_NE;
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = sign_a;
            bin.rhs = sign_b;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            sa_sb_eq = bin.dst;
            memset(&bin, 0, sizeof(bin));
            bin.op = CC_SSA_CMP;
            bin.cmp_kind = CC_CMP_NE;
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = sign_a;
            bin.rhs = sign_r;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            sa_sr_ne = bin.dst;
            memset(&bin, 0, sizeof(bin));
            bin.op = CC_SSA_AND;
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = sa_sb_eq;
            bin.rhs = sa_sr_ne;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
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
                    cc_ir_set_size(sf, sf->instrs[k].dst, 8);
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
        return cc_ir_const(sf, n);
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
            in.dst = cc_ir_new_value(sf, CC_VAL_I64);
            in.lhs = nbytes;
            in.rhs = objsz;
            if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
                return -1;
            }
            cmp_ok = in.dst;
            l_ok = cc_ir_new_label(sf);
            l_bad = cc_ir_new_label(sf);
            if (l_ok < 0 || l_bad < 0) {
                return -1;
            }
            if (cc_ir_br_cond(sf, cmp_ok, l_ok, l_bad) != 0 || cc_ir_label(sf, l_bad) != 0 ||
                cc_ir_trap(sf) != 0 || cc_ir_label(sf, l_ok) != 0) {
                return -1;
            }
        }

        memset(&call_in, 0, sizeof(call_in));
        call_in.op = CC_SSA_CALL;
        call_in.call_is_variadic = 0;
        call_in.dst = cc_ir_new_value(sf, CC_VAL_I64);
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
        if (cc_ir_push(sf, call_in) != 0) {
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
        call_in.dst = cc_ir_new_value(sf, CC_VAL_I64);
        if (call_in.sym == NULL || call_in.args == NULL || call_in.dst < 0) {
            free(call_in.sym);
            free(call_in.args);
            return -1;
        }
        call_in.args[0] = arg0;
        if (cc_ir_push(sf, call_in) != 0) {
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
        return cc_ir_const(sf, 0);
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
        return cc_ir_const(sf, 0);
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
        return cc_ir_const(sf, 0);
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
        return cc_ir_const(sf, 0);
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
        z = cc_ir_const(sf, 0);
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
        if (cc_ir_push(sf, in) != 0) {
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
        in.dst = cc_ir_new_value(sf, ct_value_kind(elem_ct));
        in.lhs = p;
        in.rhs = -1;
        in.imm = mem_size;
        in.is_unsigned = ct_loads_unsigned(elem_ct) ? 1 : 0;
        if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
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
            bin.dst = cc_ir_new_value(sf, ct_value_kind(elem_ct));
            bin.lhs = oldv;
            bin.rhs = rhsv;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            newv = bin.dst;
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_STORE;
            in.dst = -1;
            in.lhs = p;
            in.rhs = newv;
            in.imm = mem_size;
            if (cc_ir_push(sf, in) != 0) {
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
            if (cc_ir_push(sf, in) != 0) {
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
            if (cc_ir_push(sf, in) != 0) {
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
            bin.dst = cc_ir_new_value(sf, CC_VAL_I64);
            bin.lhs = oldv;
            bin.rhs = expectv;
            if (bin.dst < 0 || cc_ir_push(sf, bin) != 0) {
                return -1;
            }
            cmpv = bin.dst;
            l_store = cc_ir_new_label(sf);
            l_done = cc_ir_new_label(sf);
            if (l_store < 0 || l_done < 0) {
                return -1;
            }
            if (cc_ir_br_cond(sf, cmpv, l_store, l_done) != 0) {
                return -1;
            }
            if (cc_ir_label(sf, l_store) != 0) {
                return -1;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_STORE;
            in.dst = -1;
            in.lhs = p;
            in.rhs = desiredv;
            in.imm = mem_size;
            if (cc_ir_push(sf, in) != 0) {
                return -1;
            }
            if (cc_ir_br(sf, l_done) != 0) {
                return -1;
            }
            if (cc_ir_label(sf, l_done) != 0) {
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
        /*
         * res |= ((x & (0xff << 8*i)) shifted to the mirrored byte) for each byte i.
         * A 64-bit swap works on 64-bit values: its constants are 8 bytes, which
         * makes every result 8 (a pair of words on i386).
         */
        res = cc_ir_const(sf, 0);
        if (res < 0) {
            return -1;
        }
        if (bswap_bits == 64) {
            cc_ir_set_size(sf, res, 8);
        }
        for (bi = 0; bi < nbytes; ++bi) {
            int shift = ((nbytes - 1) - (2 * bi)) * 8;
            int cmask = cc_ir_const(sf, (long long)(0xffULL << (bi * 8)));
            int cshift;
            int part;

            if (cmask < 0) {
                return -1;
            }
            if (bswap_bits == 64) {
                cc_ir_set_size(sf, cmask, 8);
            }
            part = emit_i64_binop_instr(sf, CC_SSA_AND, x, cmask, 0, diag);
            if (part >= 0 && shift != 0) {
                cshift = cc_ir_const(sf, shift > 0 ? shift : -shift);
                part = cshift < 0 ? -1
                                  : emit_i64_binop_instr(sf, shift > 0 ? CC_SSA_SHL : CC_SSA_SHR, part, cshift,
                                                         shift < 0, diag);
            }
            res = part < 0 ? -1 : emit_i64_binop_instr(sf, CC_SSA_OR, res, part, 0, diag);
            if (res < 0) {
                return -1;
            }
        }
        return res;
    }
    *in_io = in;
    return LOWER_NOT_BUILTIN;
}

/**
 * Lower a call: a builtin, a direct or indirect call, with its arguments passed as the target's ABI says (CC_EXPR_CALL).
 *
 * @param tu         the translation unit
 * @param sf         the function being lowered
 * @param ctx        the lowering context
 * @param vars       the variables in scope
 * @param var_count  their number
 * @param depth      the scope depth
 * @param e          the expression
 * @param diag       receives an error
 * @return           the value, or -1 on error
 */
int lower_call_expr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                           var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    size_t i;

    lower_expr_instr_init(&in);
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
    {
        int bv = lower_builtin_call(tu, sf, ctx, vars, var_count, depth, e, bk, bswap_bits, &in, diag);

        if (bv != LOWER_NOT_BUILTIN) {
            return bv;
        }
    }
    in.rhs = -1;
    if (e->ident == NULL ||
        (e->ident != NULL && callee == NULL && bk == BUILTIN_NONE && bswap_bits == 0 &&
         (ident_var_idx >= 0 || (ident_global != NULL && global_is_pointer_object(ident_global))))) {
        if (e->ident == NULL) {
            const cc_expr_t *callee_expr = e->lhs;
            cc_expr_t callee_tmp;
            if (e->lhs == NULL) {
                set_diag(diag, "malformed indirect call expression");
                return -1;
            }
            /* a `*` whose operand is a function or a pointer to one changes nothing: (*fp)() and (**fp)() call fp */
            while (callee_expr->kind == CC_EXPR_DEREF && lower_designates_function(callee_expr->lhs)) {
                callee_expr = callee_expr->lhs;
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
            if (ident_global != NULL && ident_var_idx < 0 && global_is_pointer_object(ident_global)) {
                cc_ssa_instr_t load_fp;
                long mem_size = cc_ct_size(lower_global_ctype(ident_global), tu);
                int gaddr = emit_global_addr(sf, ident_global->name, diag);
                if (gaddr < 0) {
                    return -1;
                }
                if (mem_size <= 0) {
                    mem_size = cc_target_pointer_size();
                }
                memset(&load_fp, 0, sizeof(load_fp));
                load_fp.op = CC_SSA_LOAD;
                load_fp.dst = cc_ir_new_value(sf, CC_VAL_I64);
                load_fp.lhs = gaddr;
                load_fp.rhs = -1;
                load_fp.imm = mem_size;
                load_fp.is_unsigned = 1;
                if (load_fp.dst < 0 || cc_ir_push(sf, load_fp) != 0) {
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
         * A pointer without a prototype may reach a variadic function;
         * use variadic caller setup for CALLI so va_start-based callees
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

    /* the argument registers the aggregates so far took */
    cc_abi_regs_t regs = {0, 0};

    for (i = 0; i < e->arg_count; ++i) {
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
        if (e->ident != NULL && callee != NULL && callee->has_prototype && i < callee->param_count) {
            if (ct_record_id(lower_param_ctype(&callee->params[i])) >= 0) {
                long agg_size = cc_ct_size(lower_param_ctype(&callee->params[i]), tu);
                int is_transparent_union = 0;
                int pass_union_scalar = 0;
                int arg_is_same_aggregate = 0;
                int agg_abi = CC_CALL_ARG_ABI_DEFAULT;
                int param_sid = param_record_id(&callee->params[i]);

                arg_size = cc_target_pointer_size();
                if (param_sid >= 0 && (size_t)param_sid < tu->struct_count) {
                    const cc_struct_def_t *sd = &tu->structs[param_sid];
                    if ((sd->attr_flags & CC_ATTR_TRANSPARENT_UNION) != 0) {
                        is_transparent_union = 1;
                    }
                    if (e->args[i] != NULL && lower_record_id(e->args[i]) >= 0 &&
                        lower_record_id(e->args[i]) == param_sid) {
                        arg_is_same_aggregate = 1;
                    }
                    if (sd->is_union && !arg_is_same_aggregate) {
                        pass_union_scalar = 1;
                    }
                }
                if (!is_transparent_union && !pass_union_scalar) {
                    /* the target's ABI: in registers, in memory, or as an integer of its size */
                    agg_abi = cc_abi_aggregate_arg(tu, param_sid, agg_size, &regs);
                }
                if (agg_abi == CC_CALL_ARG_ABI_ZERO) {
                    in.call_arg_abi[i] = CC_CALL_ARG_ABI_ZERO;
                    arg_size = 0;
                }
                av = cast_value(sf, av, CC_VAL_I64, diag);
                if (av < 0) {
                    free(in.sym);
                    free(in.args);
                    free(in.call_arg_abi);
                    free(in.call_arg_sizes);
                    return -1;
                }
                if (!is_transparent_union && !pass_union_scalar && agg_abi == CC_CALL_ARG_ABI_DEFAULT &&
                    agg_size > 0 && agg_size <= cc_target_pointer_size()) {
                    cc_ssa_instr_t load_in;
                    memset(&load_in, 0, sizeof(load_in));
                    load_in.op = CC_SSA_LOAD;
                    load_in.dst = cc_ir_new_value(sf, CC_VAL_I64);
                    load_in.lhs = av;
                    load_in.rhs = -1;
                    load_in.imm = agg_size;
                    load_in.is_unsigned = 1;
                    if (load_in.dst < 0 || cc_ir_push(sf, load_in) != 0) {
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
                if (CC_ABI_KIND(agg_abi) == CC_CALL_ARG_ABI_AGG_MEM ||
                    CC_ABI_KIND(agg_abi) == CC_CALL_ARG_ABI_AGG_REG) {
                    in.call_arg_abi[i] = (unsigned char)agg_abi;
                    arg_size = agg_size;
                }
            } else {
                if (ct_has_kind(lower_param_ctype(&callee->params[i]), CC_CT_LDOUBLE)) {
                    arg_needs_ldouble_abi = 1;
                }
                if (ct_has_kind(lower_param_ctype(&callee->params[i]), CC_CT_FLOAT) && callee->has_prototype) {
                    /* a prototyped float parameter takes a float, not the double it is held in */
                    arg_needs_float_abi = 1;
                }
                arg_size =
                    cc_ct_size(lower_param_ctype(&callee->params[i]), tu);
                if (arg_size <= 0) {
                    arg_size = cc_target_pointer_size();
                }
                /*
                 * Sema converted the argument to the parameter's type; what
                 * is left is its representation: the register kind, an
                 * integer narrowed to its width, a float rounded.
                 */
                av = cast_value(sf, av, ct_value_kind(lower_param_ctype(&callee->params[i])), diag);
                if (av >= 0) {
                    av = normalize_int_ct(sf, av, lower_param_ctype(&callee->params[i]), diag);
                }
                if (av >= 0) {
                    av = normalize_float_ct(sf, av, lower_param_ctype(&callee->params[i]), diag);
                }
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
                load_in.dst = cc_ir_new_value(sf, CC_VAL_I64);
                load_in.lhs = av;
                load_in.rhs = -1;
                load_in.imm = agg_size;
                load_in.is_unsigned = 1;
                if (load_in.dst < 0 || cc_ir_push(sf, load_in) != 0) {
                    free(in.sym);
                    free(in.args);
                    free(in.call_arg_abi);
                    free(in.call_arg_sizes);
                    set_diag(diag, "out of memory lowering indirect small aggregate call argument");
                    return -1;
                }
                av = load_in.dst;
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
        agg_addr = cc_ir_stackalloc(sf, agg_size, diag);
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
        in.agg_class = cc_abi_aggregate_return(tu, lower_record_id(e), agg_size);
    } else if (lower_has_kind(e, CC_CT_VOID)) {
        in.dst = cc_ir_new_value(sf, CC_VAL_I64);
        in.imm = cc_target_pointer_size();
        in.is_unsigned = 1;
    } else {
        in.dst = cc_ir_new_value(sf, lower_value_kind(e));
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

    if (cc_ir_push(sf, in) != 0) {
        free(in.sym);
        free(in.args);
        free(in.call_arg_abi);
        free(in.call_arg_sizes);
        return -1;
    }
    if (lower_has_kind(e, CC_CT_LDOUBLE)) {
        cc_ir_set_size(sf, in.dst, 16);
    }
    return in.dst;
}

static int emit_const_ldouble_special_instr(cc_ssa_function_t *sf, long code) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CONST;
    in.dst = cc_ir_new_value(sf, CC_VAL_F64);
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
    if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
        return -1;
    }
    cc_ir_set_size(sf, in.dst, 16);
    return in.dst;
}

static int lower_fp_builtin_arg(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *arg,
                                const cc_ctype_t **out_storage_type, cc_diag_t *diag) {
    int v;
    const cc_ctype_t *store_t;

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
    if (store_t->kind == CC_CT_LDOUBLE) {
        v = promote_f64_to_ldouble_value(sf, v, diag);
        if (v < 0) {
            return -1;
        }
    }
    v = normalize_float_ct(sf, v, store_t, diag);
    if (v < 0) {
        return -1;
    }
    if (out_storage_type != NULL) {
        *out_storage_type = store_t;
    }
    return v;
}

static int emit_fp_signbit_from_value(cc_ssa_function_t *sf, int v, const cc_ctype_t *store_t, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    long store_size;
    int tmp_ptr;
    int bits;
    int mask;
    int masked;
    int zero;

    store_size = fp_store_size(store_t);
    tmp_ptr = cc_ir_stackalloc(sf, store_size, diag);
    if (tmp_ptr < 0) {
        return -1;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_STORE;
    in.lhs = tmp_ptr;
    in.rhs = v;
    in.imm = store_size;
    if (cc_ir_push(sf, in) != 0) {
        set_diag(diag, "out of memory storing floating signbit operand");
        return -1;
    }

    {
        /* the sign is the top bit of the last byte: float 3, double 7, long double 9 (80-bit) */
        int off = cc_ir_const(sf, store_size == 4 ? 3 : (store_size == 16 ? 9 : 7));
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
    in.dst = cc_ir_new_value(sf, CC_VAL_I64);
    in.lhs = tmp_ptr;
    in.imm = 1;
    in.is_unsigned = 1;
    if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
        set_diag(diag, "out of memory loading floating signbit bits");
        return -1;
    }
    bits = in.dst;

    mask = emit_const_u64_instr(sf, 0x80ULL, diag);
    if (mask < 0) {
        return -1;
    }
    cc_ir_set_size(sf, mask, 1); /* as wide as the byte it tests */
    masked = emit_i64_binop_instr(sf, CC_SSA_AND, bits, mask, 0, diag);
    if (masked < 0) {
        return -1;
    }
    zero = cc_ir_const(sf, 0);
    if (zero < 0) {
        return -1;
    }
    return emit_cmp_instr(sf, CC_CMP_NE, masked, zero, 1, diag);
}

static int emit_fp_isinf_sign_from_value(cc_ssa_function_t *sf, int v, const cc_ctype_t *store_t, cc_diag_t *diag) {
    int pos_inf;
    int neg_inf;
    int pos_cmp;
    int neg_cmp;

    pos_inf = emit_const_f64_instr(sf, INFINITY);
    neg_inf = emit_const_f64_instr(sf, -INFINITY);
    if (pos_inf < 0 || neg_inf < 0) {
        return -1;
    }
    pos_inf = normalize_float_ct(sf, pos_inf, store_t, diag);
    neg_inf = normalize_float_ct(sf, neg_inf, store_t, diag);
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
