/*
 * lower_call.c: lowering a call -- the builtins the compiler expands itself
 * (lower_builtin_call()) and the ordinary call, its arguments passed as the
 * target's ABI says (lower_call_expr()).
 */

#include "cc_frontend.h"
#include "cc_abi.h"
#include "cc_options.h"
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
 * The function a call to @p name goes to: for `__builtin_memcmp` and the
 * other builtins named after a C library function that nothing lowers
 * specially, the library function, as gcc calls it; otherwise @p name.
 * A call to `__builtin_memcmp` itself was to a function nobody defines
 * (Linux's init/main.o, at the vmlinux link).
 */
static const char *builtin_library_name(const char *name) {
    const char *lib = cc_builtin_library_function(name);

    return lib != NULL ? lib : name;
}

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

/** One input of an asm emit_atomic_asm() writes. */
typedef struct {
    int value;              /**< the value, or CC_SSA_ASM_MEM_INDIRECT_ENCODE() of an address for "m" */
    const char *constraint; /**< its constraint */
    long size;              /**< its size in bytes */
} atomic_asm_in_t;

/**
 * Emit `asm volatile (tmpl : "<out>" (result) : inputs : "memory", "cc")`, the one instruction an atomic builtin
 * is: the asm keeps the read and the write of the object one locked access, which separate LOAD and STORE are not.
 *
 * @param sf        the function
 * @param tmpl      the template
 * @param out       the output's constraint
 * @param out_size  the output's size in bytes
 * @param ins       the inputs
 * @param n_in      how many
 * @param diag      receives an error
 * @return          the output's value, or -1
 */
static int emit_atomic_asm(cc_ssa_function_t *sf, const char *tmpl, const char *out, long out_size,
                           const atomic_asm_in_t *ins, size_t n_in, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int outv = cc_ir_new_value(sf, CC_VAL_I64);
    size_t i;
    int ok;

    if (outv < 0) {
        set_diag(diag, "out of memory emitting an atomic operation");
        return -1;
    }
    cc_ir_set_size(sf, outv, out_size);
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_ASM;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.asm_volatile = 1;
    in.sym = cc_strdup(tmpl);
    in.asm_out_count = 1;
    in.asm_out_values = (int *)calloc(1, sizeof(*in.asm_out_values));
    in.asm_out_sizes = (unsigned char *)calloc(1, sizeof(*in.asm_out_sizes));
    in.asm_out_constraints = (char **)calloc(1, sizeof(*in.asm_out_constraints));
    in.asm_out_names = (char **)calloc(1, sizeof(*in.asm_out_names));
    in.asm_in_count = n_in;
    in.asm_in_values = (int *)calloc(n_in, sizeof(*in.asm_in_values));
    in.asm_in_sizes = (unsigned char *)calloc(n_in, sizeof(*in.asm_in_sizes));
    in.asm_in_constraints = (char **)calloc(n_in, sizeof(*in.asm_in_constraints));
    in.asm_in_names = (char **)calloc(n_in, sizeof(*in.asm_in_names));
    in.asm_clobber_count = 2;
    in.asm_clobbers = (char **)calloc(2, sizeof(*in.asm_clobbers));
    ok = in.sym != NULL && in.asm_out_values != NULL && in.asm_out_sizes != NULL && in.asm_out_constraints != NULL &&
         in.asm_out_names != NULL && in.asm_in_values != NULL && in.asm_in_sizes != NULL &&
         in.asm_in_constraints != NULL && in.asm_in_names != NULL && in.asm_clobbers != NULL;
    if (ok) {
        in.asm_out_values[0] = outv;
        in.asm_out_sizes[0] = (unsigned char)out_size;
        in.asm_out_constraints[0] = cc_strdup(out);
        ok = in.asm_out_constraints[0] != NULL;
        for (i = 0; ok && i < n_in; ++i) {
            in.asm_in_values[i] = ins[i].value;
            in.asm_in_sizes[i] = (unsigned char)ins[i].size;
            in.asm_in_constraints[i] = cc_strdup(ins[i].constraint);
            ok = in.asm_in_constraints[i] != NULL;
        }
        if (ok) {
            in.asm_clobbers[0] = cc_strdup("memory");
            in.asm_clobbers[1] = cc_strdup("cc");
            ok = in.asm_clobbers[0] != NULL && in.asm_clobbers[1] != NULL;
        }
    }
    if (!ok || cc_ir_push(sf, in) != 0) {
        free_asm_instr_fields(&in);
        set_diag(diag, "out of memory emitting an atomic operation");
        return -1;
    }
    return outv;
}

/**
 * Value @p v as the object of @p size bytes an atomic builtin works on holds it: its low bytes, extended as the
 * object's type loads.  An operand converts to the object's type, so a CAS of an unsigned char with -1 compares
 * with 255; an atomic's result is what it read or wrote.
 *
 * @param sf           the function
 * @param v            the value
 * @param size         the object's size
 * @param is_unsigned  whether it loads zero-extended
 * @param diag         receives an error
 * @return             the value converted, or -1
 */
static int atomic_fit(cc_ssa_function_t *sf, int v, long size, int is_unsigned, cc_diag_t *diag) {
    if (v < 0 || size >= 8 || (size == 4 && value_size(sf, v) <= 4)) {
        return v;
    }
    return emit_ext(sf, v, (int)size * 8, is_unsigned, size < 4 ? 4 : size, diag);
}

/**
 * Argument @p arg of an atomic builtin on an object of @p size bytes, lowered and converted to the object's type.
 *
 * @return the value, or -1
 */
static int lower_atomic_operand(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                                var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *arg, long size,
                                int is_unsigned, cc_diag_t *diag) {
    int v = lower_expr(tu, sf, ctx, vars, var_count, depth, arg, diag);

    if (v < 0 || (v = cast_value(sf, v, CC_VAL_I64, diag)) < 0) {
        return -1;
    }
    if (size == 8 && value_size(sf, v) < 8) {
        /* a word for a 64-bit object: extended as its own type says */
        long vs = value_size(sf, v) > 0 ? value_size(sf, v) : 4;

        return emit_ext(sf, v, (int)vs * 8, lower_loads_unsigned(arg), 8, diag);
    }
    return atomic_fit(sf, v, size, is_unsigned, diag);
}

/**
 * The compare-exchange of @p size bytes at @p memop: `lock cmpxchg`, or `lock cmpxchg8b` for a pair of words.
 *
 * @param sf           the function
 * @param memop        the object, CC_SSA_ASM_MEM_INDIRECT_ENCODE() of its address
 * @param size         its size in bytes
 * @param expectv      the value it must hold
 * @param desiredv     the value it then gets
 * @param diag         receives an error
 * @return             the value it held, or -1
 */
static int emit_atomic_cmpxchg(cc_ssa_function_t *sf, int memop, long size, int expectv, int desiredv,
                               cc_diag_t *diag) {
    atomic_asm_in_t ins[4];

    ins[0].value = expectv;
    ins[0].constraint = "0";
    ins[0].size = size;
    ins[1].value = memop;
    ins[1].constraint = "m";
    ins[1].size = size;
    if (size > cc_target_pointer_size()) {
        int c32 = cc_ir_const(sf, 32);
        int hi;

        if (c32 < 0) {
            return -1;
        }
        hi = emit_i64_binop_instr(sf, CC_SSA_SHR, desiredv, c32, 1, diag);
        ins[2].value = emit_ext(sf, desiredv, 32, 1, 4, diag);
        ins[3].value = hi < 0 ? -1 : emit_ext(sf, hi, 32, 1, 4, diag);
        if (ins[2].value < 0 || ins[3].value < 0) {
            return -1;
        }
        ins[2].constraint = "b";
        ins[2].size = 4;
        ins[3].constraint = "c";
        ins[3].size = 4;
        return emit_atomic_asm(sf, "lock cmpxchg8b %2", "=A", size, ins, 4, diag);
    }
    ins[2].value = desiredv;
    ins[2].constraint = size == 1 ? "q" : "r";
    ins[2].size = size;
    return emit_atomic_asm(sf, "lock cmpxchg %3, %2", "=a", size, ins, 3, diag);
}

/**
 * The call gcc makes for atomic builtin @p builtin on an object of @p size bytes where the -march has no
 * instruction for it: `__sync_<op>_<size>` or `__atomic_<op>_<size>` (the builtin's name, an `_n` dropped), from
 * libgcc or libatomic, with the builtin's own arguments.
 *
 * @param sf       the function
 * @param builtin  the builtin's name
 * @param size     the object's size in bytes
 * @param args     the arguments, lowered and converted
 * @param n        how many
 * @param diag     receives an error
 * @return         the call's value, or -1
 */
static int emit_atomic_libcall(cc_ssa_function_t *sf, const char *builtin, long size, const int *args, size_t n,
                               cc_diag_t *diag) {
    cc_ssa_instr_t in;
    size_t len = strlen(builtin);
    char name[64];
    size_t i;

    if (len >= 2 && strcmp(builtin + len - 2, "_n") == 0) {
        len -= 2;
    }
    if (len > sizeof(name) - 8) {
        set_diag(diag, "atomic builtin name too long");
        return -1;
    }
    memcpy(name, builtin, len);
    snprintf(name + len, sizeof(name) - len, "_%ld", size);
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CALL;
    in.dst = cc_ir_new_value(sf, CC_VAL_I64);
    in.lhs = -1;
    in.rhs = -1;
    in.arg_count = n;
    in.args = (int *)calloc(n, sizeof(*in.args));
    in.sym = cc_strdup(name);
    if (in.dst < 0 || in.args == NULL || in.sym == NULL) {
        free(in.args);
        free(in.sym);
        set_diag(diag, "out of memory emitting an atomic operation");
        return -1;
    }
    cc_ir_set_size(sf, in.dst, size > 4 ? size : 4);
    for (i = 0; i < n; ++i) {
        in.args[i] = args[i];
    }
    if (cc_ir_push(sf, in) != 0) {
        free(in.args);
        free(in.sym);
        set_diag(diag, "out of memory emitting an atomic operation");
        return -1;
    }
    return in.dst;
}

/**
 * An i386 8-byte atomic load or store, as gcc makes one from the Pentium on: through the x87, whose fildq and
 * fistpq move all eight bytes in one access -- exactly, as an x87 register holds any 64-bit integer.
 *
 * @param sf     the function
 * @param p      the object's address
 * @param v      the value to store, or -1 to load
 * @param diag   receives an error
 * @return       the value loaded (or stored), or -1
 */
static int emit_atomic_x87_move8(cc_ssa_function_t *sf, int p, int v, cc_diag_t *diag) {
    atomic_asm_in_t ins[2];

    ins[0].value = CC_SSA_ASM_MEM_INDIRECT_ENCODE(p);
    ins[0].constraint = "m";
    ins[0].size = 8;
    if (v < 0) {
        return emit_atomic_asm(sf, "fildq %1\n\tfistpq %0", "=m", 8, ins, 1, diag);
    }
    ins[1] = ins[0];
    ins[0].value = v;
    /* the output is only somewhere for the asm to have one: the store is to %2 */
    return emit_atomic_asm(sf, "fildq %1\n\tfistpq %2", "=m", 8, ins, 2, diag) < 0 ? -1 : v;
}

/**
 * Lower an atomic read-modify-write builtin (cc_builtin_atomic_rmw()) or a __sync compare-and-swap to locked
 * instructions: `lock xadd` adds, `xchg` exchanges, `lock cmpxchg` compares and exchanges.  An operation with no
 * instruction that also gives the old value -- and, or, xor, nand, and any on a pair of words on i386 -- is a
 * compare-exchange loop.  Each of these is a full barrier on x86, which every memory order asks no more than.
 *
 * As gcc, an i386 -march without the instruction calls libgcc's or libatomic's function instead
 * (emit_atomic_libcall()): cmpxchg and xadd came with the 486, cmpxchg8b with the Pentium; xchg the 386 has.
 *
 * @return the builtin's value, or -1
 */
static int lower_atomic_rmw(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
                            var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, builtin_kind_t bk,
                            cc_diag_t *diag) {
    cc_rmw_builtin_t rmw;
    const cc_ctype_t *elem_ct;
    long size;
    int uns;
    int p;
    int memop;
    int v;
    int cas = bk == BUILTIN_SYNC_BOOL_CAS || bk == BUILTIN_SYNC_VAL_CAS;
    int wide;

    memset(&rmw, 0, sizeof(rmw));
    if (!cas) {
        (void)cc_builtin_atomic_rmw(e->ident, &rmw);
    }
    if (e->arg_count != (cas || rmw.has_memorder ? 3u : 2u) || e->args[0] == NULL || e->args[1] == NULL) {
        set_diag(diag, "atomic builtin lowering has the wrong arguments");
        return -1;
    }
    p = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
    if (p < 0 || (p = cast_value(sf, p, CC_VAL_I64, diag)) < 0) {
        return -1;
    }
    elem_ct = lower_atomic_elem_ctype(e->args[0]);
    uns = ct_loads_unsigned(elem_ct) ? 1 : 0;
    size = lower_pointee_size(tu, e->args[0]);
    if (size <= 0) {
        size = lower_pointer_step(tu, e->args[0]);
    }
    if (size <= 0) {
        size = cc_target_pointer_size();
    }
    if (size != 1 && size != 2 && size != 4 && size != 8) {
        set_diag(diag, "atomic builtin on an object of unsupported size");
        return -1;
    }
    wide = size > cc_target_pointer_size();
    memop = CC_SSA_ASM_MEM_INDIRECT_ENCODE(p);
    v = lower_atomic_operand(tu, sf, ctx, vars, var_count, depth, e->args[1], size, uns, diag);
    if (v < 0) {
        return -1;
    }
    {
        const cc_options_t *o = cc_options_current();
        int level = o != NULL && o->target == CC_TARGET_I386 ? o->i386_isa_level : 7;

        if (level < (wide ? 5 : 4) && (cas || wide || rmw.op != CC_RMW_XCHG)) {
            int args[4];
            size_t n = 0;
            int r;

            args[n++] = p;
            args[n++] = v;
            if (cas || rmw.has_memorder) {
                int third = cas ? lower_atomic_operand(tu, sf, ctx, vars, var_count, depth, e->args[2], size, uns,
                                                       diag)
                                : lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[2], diag);

                if (third < 0 || (!cas && (third = cast_value(sf, third, CC_VAL_I64, diag)) < 0)) {
                    return -1;
                }
                args[n++] = third;
            }
            r = emit_atomic_libcall(sf, e->ident, size, args, n, diag);
            return r < 0 || bk == BUILTIN_SYNC_BOOL_CAS ? r : atomic_fit(sf, r, size, uns, diag);
        }
    }
    if (cas) {
        int desiredv = lower_atomic_operand(tu, sf, ctx, vars, var_count, depth, e->args[2], size, uns, diag);
        int prev;

        if (desiredv < 0) {
            return -1;
        }
        prev = emit_atomic_cmpxchg(sf, memop, size, v, desiredv, diag);
        if (prev < 0 || (prev = atomic_fit(sf, prev, size, uns, diag)) < 0) {
            return -1;
        }
        return bk == BUILTIN_SYNC_VAL_CAS ? prev : emit_cmp_instr(sf, CC_CMP_EQ, prev, v, uns, diag);
    }
    if (rmw.has_memorder && lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[2], diag) < 0) {
        return -1;
    }
    if (!wide && (rmw.op == CC_RMW_ADD || rmw.op == CC_RMW_SUB || rmw.op == CC_RMW_XCHG)) {
        atomic_asm_in_t ins[2];
        int old;

        ins[0].value = v;
        ins[0].constraint = "0";
        ins[0].size = size;
        ins[1].value = memop;
        ins[1].constraint = "m";
        ins[1].size = size;
        if (rmw.op == CC_RMW_XCHG) {
            old = emit_atomic_asm(sf, "xchg %0, %2", size == 1 ? "=q" : "=r", size, ins, 2, diag);
            return old < 0 ? -1 : atomic_fit(sf, old, size, uns, diag);
        }
        if (rmw.op == CC_RMW_SUB) {
            int zero = cc_ir_const(sf, 0);

            ins[0].value = zero < 0 ? -1 : emit_i64_binop_instr(sf, CC_SSA_SUB, zero, v, uns, diag);
            if (ins[0].value < 0) {
                return -1;
            }
        }
        old = emit_atomic_asm(sf, "lock xadd %0, %2", size == 1 ? "=q" : "=r", size, ins, 2, diag);
        if (old < 0 || (old = atomic_fit(sf, old, size, uns, diag)) < 0) {
            return -1;
        }
        if (!rmw.returns_new) {
            return old;
        }
        return atomic_fit(sf, emit_i64_binop_instr(sf, rmw.op == CC_RMW_ADD ? CC_SSA_ADD : CC_SSA_SUB, old, v, uns,
                                                   diag),
                          size, uns, diag);
    }
    {
        /* old = *p; do new = old OP v; while ((prev = cmpxchg(p, old, new)) != old && (old = prev, 1)); */
        cc_ssa_instr_t in;
        int old;
        int newv;
        int prev;
        int eq;
        int l_loop = cc_ir_new_label(sf);
        int l_done = cc_ir_new_label(sf);

        if (l_loop < 0 || l_done < 0) {
            return -1;
        }
        memset(&in, 0, sizeof(in));
        in.op = CC_SSA_LOAD;
        in.dst = cc_ir_new_value(sf, CC_VAL_I64);
        in.lhs = p;
        in.rhs = -1;
        in.imm = size;
        in.is_unsigned = uns;
        if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
            set_diag(diag, "out of memory emitting an atomic operation");
            return -1;
        }
        old = in.dst;
        if (wide) {
            cc_ir_set_size(sf, old, size);
        }
        if (cc_ir_br(sf, l_loop) != 0 || cc_ir_label(sf, l_loop) != 0) {
            return -1;
        }
        if (rmw.op == CC_RMW_XCHG) {
            newv = v;
        } else {
            static const cc_ssa_opcode_t ops[] = {
                [CC_RMW_ADD] = CC_SSA_ADD, [CC_RMW_SUB] = CC_SSA_SUB, [CC_RMW_AND] = CC_SSA_AND,
                [CC_RMW_OR] = CC_SSA_OR,   [CC_RMW_XOR] = CC_SSA_XOR, [CC_RMW_NAND] = CC_SSA_AND,
            };

            newv = emit_i64_binop_instr(sf, ops[rmw.op], old, v, uns, diag);
            if (newv >= 0 && rmw.op == CC_RMW_NAND) {
                int ones = cc_ir_const(sf, -1);

                newv = ones < 0 ? -1 : emit_i64_binop_instr(sf, CC_SSA_XOR, newv, ones, uns, diag);
            }
            if (newv >= 0 && !wide) {
                newv = atomic_fit(sf, newv, size, uns, diag);
            }
        }
        if (newv < 0 || (prev = emit_atomic_cmpxchg(sf, memop, size, old, newv, diag)) < 0 ||
            (!wide && (prev = atomic_fit(sf, prev, size, uns, diag)) < 0) ||
            (eq = emit_cmp_instr(sf, CC_CMP_EQ, prev, old, uns, diag)) < 0) {
            return -1;
        }
        memset(&in, 0, sizeof(in));
        in.op = CC_SSA_MOV;
        in.dst = old;
        in.lhs = prev;
        in.rhs = -1;
        if (cc_ir_push(sf, in) != 0) {
            set_diag(diag, "out of memory emitting an atomic operation");
            return -1;
        }
        if (cc_ir_br_cond(sf, eq, l_done, l_loop) != 0 || cc_ir_label(sf, l_done) != 0) {
            return -1;
        }
        return rmw.returns_new ? newv : old;
    }
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
    if (strcmp(name, "__builtin_strcmp") == 0) {
        return BUILTIN_STRCMP;
    }
    if (strcmp(name, "__builtin_extract_return_addr") == 0) {
        return BUILTIN_EXTRACT_RETURN_ADDR;
    }
    if (strcmp(name, "__builtin_return_address") == 0) {
        return BUILTIN_RETURN_ADDRESS;
    }
    if (strcmp(name, "__builtin_frame_address") == 0) {
        return BUILTIN_FRAME_ADDRESS;
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
    if (cc_builtin_atomic_rmw(name, NULL)) {
        return BUILTIN_ATOMIC_RMW;
    }
    if (strcmp(name, "__sync_bool_compare_and_swap") == 0) {
        return BUILTIN_SYNC_BOOL_CAS;
    }
    if (strcmp(name, "__sync_val_compare_and_swap") == 0) {
        return BUILTIN_SYNC_VAL_CAS;
    }
    if (strcmp(name, "__sync_lock_release") == 0) {
        return BUILTIN_SYNC_LOCK_RELEASE;
    }
    if (strcmp(name, "__sync_synchronize") == 0) {
        return BUILTIN_SYNC_SYNCHRONIZE;
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
 * Append `a op b` (a compare when @p op is CMP, of kind @p kind).  Arithmetic on 64-bit values is
 * 64 bits; a compare's 0/1, and what combines such results, is a value of the default width, as a
 * compare writes it (on i386 one word: read as 64 bits, its high word would be garbage).
 *
 * @return the result, or -1 when out of memory
 */
static int ovf_op(cc_ssa_function_t *sf, cc_ssa_opcode_t op, cc_cmp_kind_t kind, int a, int b, int is_unsigned) {
    cc_ssa_instr_t in;

    if (a < 0 || b < 0) {
        return -1;
    }
    memset(&in, 0, sizeof(in));
    in.op = op;
    in.cmp_kind = kind;
    in.is_unsigned = is_unsigned;
    in.dst = cc_ir_new_value(sf, CC_VAL_I64);
    in.lhs = a;
    in.rhs = b;
    if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
        return -1;
    }
    if (op != CC_SSA_CMP && value_size(sf, a) == 8) {
        cc_ir_set_size(sf, in.dst, 8);
    }
    return in.dst;
}

/** A 64-bit constant, or -1 when out of memory. */
static int ovf_const(cc_ssa_function_t *sf, long long v) {
    int c = cc_ir_const(sf, v);

    if (c >= 0) {
        cc_ir_set_size(sf, c, 8);
    }
    return c;
}

/**
 * Whether the product of unsigned 64-bit values @p a and @p b overflows 64 bits, computed from
 * their 32-bit halves with no division: both high halves nonzero, a cross product with a high
 * word, or the cross products' low words and the low product's high word carrying past 32 bits.
 * Every multiplication is of values under 2^32, so exact.  The division the check was made with
 * (the product divided by one operand is the other) is a call of libgcc's __udivdi3 on i386,
 * which Linux does not link (check_mul_overflow() in lib/kstrtox.c).
 *
 * @return the 0/1 result, or -1 when out of memory
 */
static int umul64_overflows(cc_ssa_function_t *sf, int a, int b) {
    int m = ovf_const(sf, 0xffffffffLL);
    int s = ovf_const(sf, 32);
    int z = ovf_const(sf, 0);
    int ah = ovf_op(sf, CC_SSA_SHR, CC_CMP_EQ, a, s, 1);
    int al = ovf_op(sf, CC_SSA_AND, CC_CMP_EQ, a, m, 1);
    int bh = ovf_op(sf, CC_SSA_SHR, CC_CMP_EQ, b, s, 1);
    int bl = ovf_op(sf, CC_SSA_AND, CC_CMP_EQ, b, m, 1);
    int both = ovf_op(sf, CC_SSA_AND, CC_CMP_EQ, ovf_op(sf, CC_SSA_CMP, CC_CMP_NE, ah, z, 1),
                      ovf_op(sf, CC_SSA_CMP, CC_CMP_NE, bh, z, 1), 1);
    int t1 = ovf_op(sf, CC_SSA_MUL, CC_CMP_EQ, al, bh, 1);
    int t2 = ovf_op(sf, CC_SSA_MUL, CC_CMP_EQ, ah, bl, 1);
    int t0 = ovf_op(sf, CC_SSA_MUL, CC_CMP_EQ, al, bl, 1);
    int h1 = ovf_op(sf, CC_SSA_CMP, CC_CMP_NE, ovf_op(sf, CC_SSA_SHR, CC_CMP_EQ, t1, s, 1), z, 1);
    int h2 = ovf_op(sf, CC_SSA_CMP, CC_CMP_NE, ovf_op(sf, CC_SSA_SHR, CC_CMP_EQ, t2, s, 1), z, 1);
    int sum = ovf_op(sf, CC_SSA_ADD, CC_CMP_EQ,
                     ovf_op(sf, CC_SSA_ADD, CC_CMP_EQ, ovf_op(sf, CC_SSA_AND, CC_CMP_EQ, t1, m, 1),
                            ovf_op(sf, CC_SSA_AND, CC_CMP_EQ, t2, m, 1), 1),
                     ovf_op(sf, CC_SSA_SHR, CC_CMP_EQ, t0, s, 1), 1);
    int carry = ovf_op(sf, CC_SSA_CMP, CC_CMP_NE, ovf_op(sf, CC_SSA_SHR, CC_CMP_EQ, sum, s, 1), z, 1);

    return ovf_op(sf, CC_SSA_OR, CC_CMP_EQ, ovf_op(sf, CC_SSA_OR, CC_CMP_EQ, both, h1, 1),
                  ovf_op(sf, CC_SSA_OR, CC_CMP_EQ, h2, carry, 1), 1);
}

/**
 * Whether the product of signed 64-bit values @p a and @p b overflows: the unsigned check
 * (umul64_overflows()) on their magnitudes, or a product of magnitudes past 2^63 - 1, or 2^63 for
 * operands of different signs.
 *
 * @return the 0/1 result, or -1 when out of memory
 */
static int smul64_overflows(cc_ssa_function_t *sf, int a, int b) {
    int k63 = ovf_const(sf, 63);
    int sa = ovf_op(sf, CC_SSA_SHR, CC_CMP_EQ, a, k63, 0); /* all ones when negative */
    int sb = ovf_op(sf, CC_SSA_SHR, CC_CMP_EQ, b, k63, 0);
    int ua = ovf_op(sf, CC_SSA_SUB, CC_CMP_EQ, ovf_op(sf, CC_SSA_XOR, CC_CMP_EQ, a, sa, 1), sa, 1);
    int ub = ovf_op(sf, CC_SSA_SUB, CC_CMP_EQ, ovf_op(sf, CC_SSA_XOR, CC_CMP_EQ, b, sb, 1), sb, 1);
    int ovu = umul64_overflows(sf, ua, ub);
    int p = ovf_op(sf, CC_SSA_MUL, CC_CMP_EQ, ua, ub, 1);
    /* 1 when the signs differ, in 64 bits: the sign bit of a ^ b */
    int neg = ovf_op(sf, CC_SSA_SHR, CC_CMP_EQ, ovf_op(sf, CC_SSA_XOR, CC_CMP_EQ, a, b, 1), k63, 1);
    int lim = ovf_op(sf, CC_SSA_ADD, CC_CMP_EQ, ovf_const(sf, 0x7fffffffffffffffLL), neg, 1);
    int big = ovf_op(sf, CC_SSA_CMP, CC_CMP_GT, p, lim, 1);

    return ovf_op(sf, CC_SSA_OR, CC_CMP_EQ, ovu, big, 1);
}

/** The most words a __builtin_memcpy or __builtin_memset of a constant size is copied or set in without a call; a
 *  __builtin_memmove, which reads all before it writes, half as many. */
#define INLINE_MEM_WORDS 8

/** IR value @p base plus @p off: a new ADD, or @p base itself for 0; -1 when out of memory. */
static int ir_addr_plus(cc_ssa_function_t *sf, int base, long off) {
    cc_ssa_instr_t in;
    int k;

    if (off == 0) {
        return base;
    }
    k = cc_ir_const(sf, off);
    if (k < 0) {
        return -1;
    }
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_ADD;
    in.dst = cc_ir_new_value(sf, CC_VAL_I64);
    in.lhs = base;
    in.rhs = k;
    if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
        return -1;
    }
    return in.dst;
}

/**
 * A __builtin_memcpy (@p kind 0), __builtin_memmove (1) or __builtin_memset (2) of @p n bytes, a constant, made
 * as loads and stores of words and then of 4, 2 and 1 bytes, as gcc makes one: no call of the library's
 * function, which code with none to call -- Linux's vDSO, its unaligned accesses -- may not have.  A memmove
 * reads every piece before it writes one; a memset stores byte @p c in each.
 *
 * @return 0, or -1 when out of memory
 */
static int lower_inline_mem(cc_ssa_function_t *sf, int kind, int dst, int src, long long c, long n) {
    int word = cc_target_pointer_size();
    long offs[INLINE_MEM_WORDS + 3];
    int sizes[INLINE_MEM_WORDS + 3];
    int vals[INLINE_MEM_WORDS + 3];
    int np = 0;
    long off = 0;
    int p;
    int i;

    for (p = word; p >= 1; p /= 2) {
        while (n - off >= p && np < INLINE_MEM_WORDS + 3) {
            offs[np] = off;
            sizes[np] = p;
            np++;
            off += p;
        }
    }
    for (i = 0; i < np; ++i) {
        if (kind == 2) {
            unsigned long long b = (unsigned long long)(c & 0xff);
            unsigned long long v = 0;
            int k;

            for (k = 0; k < sizes[i]; ++k) {
                v |= b << (8 * k);
            }
            vals[i] = cc_ir_const(sf, (long long)v);
        } else {
            cc_ssa_instr_t ld;

            memset(&ld, 0, sizeof(ld));
            ld.op = CC_SSA_LOAD;
            ld.lhs = ir_addr_plus(sf, src, offs[i]);
            ld.imm = sizes[i];
            ld.is_unsigned = 1;
            ld.dst = cc_ir_new_value(sf, CC_VAL_I64);
            ld.rhs = -1;
            if (ld.lhs < 0 || ld.dst < 0 || cc_ir_push(sf, ld) != 0) {
                return -1;
            }
            vals[i] = ld.dst;
        }
        if (vals[i] < 0) {
            return -1;
        }
        if (kind != 1) {
            /* (a memmove writes once it has read all) */
            cc_ssa_instr_t st;

            memset(&st, 0, sizeof(st));
            st.op = CC_SSA_STORE;
            st.lhs = ir_addr_plus(sf, dst, offs[i]);
            st.rhs = vals[i];
            st.imm = sizes[i];
            st.dst = -1;
            if (st.lhs < 0 || cc_ir_push(sf, st) != 0) {
                return -1;
            }
        }
    }
    for (i = 0; kind == 1 && i < np; ++i) {
        cc_ssa_instr_t st;

        memset(&st, 0, sizeof(st));
        st.op = CC_SSA_STORE;
        st.lhs = ir_addr_plus(sf, dst, offs[i]);
        st.rhs = vals[i];
        st.imm = sizes[i];
        st.dst = -1;
        if (st.lhs < 0 || cc_ir_push(sf, st) != 0) {
            return -1;
        }
    }
    return 0;
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
    if (bk == BUILTIN_EXTRACT_RETURN_ADDR) {
        /* on x86 a return address is the code address itself */
        if (e->arg_count != 1) {
            set_diag(diag, "__builtin_extract_return_addr lowering expects 1 argument");
            return -1;
        }
        return lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[0], diag);
    }
    if (bk == BUILTIN_RETURN_ADDRESS || bk == BUILTIN_FRAME_ADDRESS) {
        /*
         * The frame pointer N frames up; a return address is the word
         * above the saved frame pointer it was pushed before.  They were
         * calls to functions nobody defines, and Linux's _RET_IP_ is one.
         */
        long long level;
        cc_ssa_instr_t in;
        int v;

        if (e->arg_count != 1 || eval_const_i64_expr(tu, e->args[0], &level) != 0 || level < 0) {
            set_diag(diag, bk == BUILTIN_RETURN_ADDRESS
                               ? "__builtin_return_address argument must be a non-negative integer constant"
                               : "__builtin_frame_address argument must be a non-negative integer constant");
            return -1;
        }
        memset(&in, 0, sizeof(in));
        in.op = CC_SSA_FRAMEADDR;
        in.lhs = -1;
        in.rhs = -1;
        in.imm = level;
        in.dst = cc_ir_new_value(sf, CC_VAL_I64);
        if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
            set_diag(diag, "out of memory lowering __builtin_frame_address");
            return -1;
        }
        v = in.dst;
        if (bk == BUILTIN_RETURN_ADDRESS) {
            int off = cc_ir_const(sf, cc_target_pointer_size());

            if (off < 0) {
                return -1;
            }
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_ADD;
            in.lhs = v;
            in.rhs = off;
            in.dst = cc_ir_new_value(sf, CC_VAL_I64);
            if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
                set_diag(diag, "out of memory lowering __builtin_return_address");
                return -1;
            }
            cc_ir_set_size(sf, in.dst, cc_target_pointer_size());
            v = in.dst;
            memset(&in, 0, sizeof(in));
            in.op = CC_SSA_LOAD;
            in.lhs = v;
            in.rhs = -1;
            in.imm = cc_target_pointer_size();
            in.is_unsigned = 1;
            in.dst = cc_ir_new_value(sf, CC_VAL_I64);
            if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
                set_diag(diag, "out of memory lowering __builtin_return_address");
                return -1;
            }
            v = in.dst;
        }
        return v;
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
                /* no division: on i386 a call of __udivdi3 */
                ov = umul64_overflows(sf, av, bv);
                if (ov < 0) {
                    return -1;
                }
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
            if (!lower_is_unsigned(e->args[0]) || !lower_is_unsigned(e->args[1])) {
                /*
                 * a signed operand: a negative exact result does not fit.
                 * Unsigned operands have no negative result -- their wrapped
                 * one at 2^63 and over is a large number, not an overflow.
                 */
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
            /*
             * no division: on i386 a call of __divdi3, and dividing the
             * product of INT64_MIN and -1 back by -1 traps (SIGFPE)
             */
            ov = smul64_overflows(sf, av, bv);
            if (ov < 0) {
                return -1;
            }
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
        /* a size_t, as the prototype has it: a wider count (a loff_t difference on i386) is cut to its word */
        nbytes = normalize_integral_value(sf, nbytes, cc_ct_basic(CC_CT_ULONG), diag);
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

        {
            /* a constant size up to a few words: the copy or the stores themselves, as gcc makes them */
            int kind = (bk == BUILTIN_MEMCPY || bk == BUILTIN_MEMCPY_CHK)     ? 0
                       : (bk == BUILTIN_MEMMOVE || bk == BUILTIN_MEMMOVE_CHK) ? 1
                                                                              : 2;
            long long n_const;
            long long c = 0;
            long max = (long)cc_target_pointer_size() * (kind == 1 ? INLINE_MEM_WORDS / 2 : INLINE_MEM_WORDS);

            if (eval_const_i64_expr(tu, e->args[2], &n_const) == 0 && n_const >= 0 && n_const <= max &&
                (kind != 2 || eval_const_i64_expr(tu, e->args[1], &c) == 0)) {
                int srcv = -1;

                if (kind != 2) {
                    srcv = cast_value(sf, lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag),
                                      CC_VAL_I64, diag);
                    if (srcv < 0) {
                        return -1;
                    }
                }
                if (lower_inline_mem(sf, kind, dstv, srcv, c, (long)n_const) != 0) {
                    set_diag(diag, "out of memory expanding a __builtin_mem* call");
                    return -1;
                }
                return dstv;
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
            /* the byte value is an int */
            call_in.args[1] = call_in.args[1] < 0 ? -1
                                                  : normalize_integral_value(sf, call_in.args[1],
                                                                             cc_ct_basic(CC_CT_INT), diag);
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
    if (bk == BUILTIN_STRLEN || bk == BUILTIN_STRCMP) {
        /* of string literals a constant, as gcc folds it; otherwise the library's function */
        size_t nargs = bk == BUILTIN_STRCMP ? 2 : 1;
        cc_ssa_instr_t call_in;
        long long folded;
        int argv[2];
        size_t ai;

        if (e->arg_count != nargs) {
            set_diag(diag, bk == BUILTIN_STRCMP ? "__builtin_strcmp lowering expects 2 arguments"
                                                : "__builtin_strlen lowering expects 1 argument");
            return -1;
        }
        if (eval_const_i64_expr(tu, e, &folded) == 0) {
            int c = emit_const_u64_instr(sf, (unsigned long long)folded, diag);

            if (c >= 0 && bk == BUILTIN_STRCMP) {
                cc_ir_set_size(sf, c, 4);
            }
            return c;
        }
        for (ai = 0; ai < nargs; ++ai) {
            if (e->args[ai] == NULL) {
                return -1;
            }
            argv[ai] = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[ai], diag);
            if (argv[ai] < 0) {
                return -1;
            }
            argv[ai] = cast_value(sf, argv[ai], CC_VAL_I64, diag);
            if (argv[ai] < 0) {
                return -1;
            }
        }
        memset(&call_in, 0, sizeof(call_in));
        call_in.op = CC_SSA_CALL;
        call_in.call_is_variadic = 0;
        call_in.sym = cc_strdup(bk == BUILTIN_STRCMP ? "strcmp" : "strlen");
        call_in.arg_count = (int)nargs;
        call_in.args = (int *)calloc(nargs, sizeof(*call_in.args));
        call_in.dst = cc_ir_new_value(sf, CC_VAL_I64);
        if (call_in.sym == NULL || call_in.args == NULL || call_in.dst < 0) {
            free(call_in.sym);
            free(call_in.args);
            return -1;
        }
        for (ai = 0; ai < nargs; ++ai) {
            call_in.args[ai] = argv[ai];
        }
        if (bk == BUILTIN_STRCMP) {
            cc_ir_set_size(sf, call_in.dst, 4);
        }
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
    if (bk == BUILTIN_ATOMIC_RMW || bk == BUILTIN_SYNC_BOOL_CAS || bk == BUILTIN_SYNC_VAL_CAS) {
        return lower_atomic_rmw(tu, sf, ctx, vars, var_count, depth, e, bk, diag);
    }
    if (bk == BUILTIN_ATOMIC_LOAD_N || bk == BUILTIN_ATOMIC_STORE_N) {
        int p;
        int oldv = -1;
        int rhsv = -1;
        long mem_size;
        long long memorder = CC_ATOMIC_SEQ_CST;
        int need_pre_compiler = 0;
        int need_post_compiler = 0;
        int need_pre_full = 0;
        int need_post_full = 0;
        const cc_ctype_t *elem_ct;
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
        } else {
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
        } else {
            if (memorder == CC_ATOMIC_SEQ_CST) {
                need_pre_full = 1;
                need_post_full = 1;
            } else if (memorder != CC_ATOMIC_RELAXED) {
                need_pre_compiler = 1;
            }
        }
        if (mem_size == 8 && cc_target_pointer_size() == 4) {
            /*
             * Eight bytes on i386 are two words to a plain load or store: as gcc, the x87's one access from
             * the Pentium on, and __atomic_load_8 or __atomic_store_8 before it.
             */
            const cc_options_t *o = cc_options_current();
            int args[3];
            size_t n = 0;
            int r;

            if (bk == BUILTIN_ATOMIC_STORE_N) {
                rhsv = e->arg_count >= 2 && e->args[1] != NULL
                           ? lower_atomic_operand(tu, sf, ctx, vars, var_count, depth, e->args[1], 8, 0, diag)
                           : -1;
                if (rhsv < 0) {
                    return -1;
                }
            }
            if (o != NULL && o->i386_isa_level < 5) {
                args[n++] = p;
                if (rhsv >= 0) {
                    args[n++] = rhsv;
                }
                args[n] = cc_ir_const(sf, memorder);
                if (args[n++] < 0) {
                    return -1;
                }
                r = emit_atomic_libcall(sf, e->ident, 8, args, n, diag);
                return r < 0 || rhsv < 0 ? r : rhsv;
            }
            if (need_pre_full ? emit_atomic_full_barrier(sf, diag) != 0
                              : need_pre_compiler && emit_atomic_compiler_barrier(sf, diag) != 0) {
                return -1;
            }
            r = emit_atomic_x87_move8(sf, p, rhsv, diag);
            if (r < 0 || (need_post_full ? emit_atomic_full_barrier(sf, diag) != 0
                                         : need_post_compiler && emit_atomic_compiler_barrier(sf, diag) != 0)) {
                return -1;
            }
            return r;
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
        if (e->arg_count >= 2 && e->args[1] != NULL) {
            rhsv = lower_expr(tu, sf, ctx, vars, var_count, depth, e->args[1], diag);
            if (rhsv < 0) {
                return -1;
            }
            rhsv = cast_value(sf, rhsv, ct_value_kind(elem_ct), diag);
            if (rhsv < 0) {
                return -1;
            }
        }
        {
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
 * Whether function @p name binds within the object by a declaration of it in @p tu: static, or of hidden,
 * protected or internal visibility, by an attribute or a `#pragma GCC visibility` the declaration came under.
 * A position-independent call of it is then direct (cc_ssa_instr_t.call_binds_local); a prototype is in no IR
 * module the backend could ask.
 */
static int callee_binds_locally(const cc_translation_unit_t *tu, const char *name) {
    const cc_attr_flags_t local_vis = CC_ATTR_VIS_HIDDEN | CC_ATTR_VIS_PROTECTED | CC_ATTR_VIS_INTERNAL;
    size_t i;

    if (tu == NULL || name == NULL) {
        return 0;
    }
    for (i = 0; i < tu->func_count; ++i) {
        const cc_function_t *f = &tu->funcs[i];

        if (f->name != NULL && strcmp(f->name, name) == 0 &&
            ((f->storage & CC_STORAGE_STATIC) != 0 || (f->attr_flags & local_vis) != 0)) {
            return 1;
        }
    }
    return 0;
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
    const cc_ctype_t *ptr_fnt = NULL; /* a call through a pointer: the function type pointed to */
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
        {
            /* the function type pointed to, whose prototype says how each argument is passed */
            const cc_ctype_t *ct = e->lhs != NULL              ? lower_value_ctype(e->lhs)
                                   : ident_var_idx >= 0         ? lower_var_ctype(&vars[ident_var_idx])
                                   : ident_global != NULL       ? lower_global_ctype(ident_global)
                                                                : NULL;

            if (ct != NULL && ct->kind == CC_CT_POINTER) {
                ct = ct->of;
            }
            ptr_fnt = ct != NULL && ct->unqual->kind == CC_CT_FUNCTION ? ct->unqual : NULL;
        }
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
        in.call_binds_local = callee_binds_locally(tu, e->ident);
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
        in.sym = cc_strdup(builtin_library_name(e->ident));
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
    /* i386 regparm: a structure result's hidden pointer goes in %eax (CC_ABI_RET_SRET_REG) */
    int regparm_sret = 0;

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
        } else if (in.op == CC_SSA_CALLI && ptr_fnt != NULL && ptr_fnt->prototyped && i < ptr_fnt->param_count &&
                   ptr_fnt->params[i] != NULL && ct_has_kind(ptr_fnt->params[i], CC_CT_FLOAT)) {
            /* a float parameter of the prototype pointed to takes a float, as a declared function's does */
            arg_needs_float_abi = 1;
        }
        in.args[i] = av;
        /* (an empty structure's CC_CALL_ARG_ABI_ZERO stays: it takes no register and no stack) */
        if (CC_ABI_KIND(in.call_arg_abi[i]) != CC_CALL_ARG_ABI_AGG_MEM &&
            CC_ABI_KIND(in.call_arg_abi[i]) != CC_CALL_ARG_ABI_AGG_REG &&
            in.call_arg_abi[i] != CC_CALL_ARG_ABI_ZERO) {
            in.call_arg_abi[i] = (unsigned char)(arg_needs_ldouble_abi ? CC_CALL_ARG_ABI_LDOUBLE
                                                 : arg_needs_float_abi ? CC_CALL_ARG_ABI_FLOAT
                                                                       : CC_CALL_ARG_ABI_DEFAULT);
        }
        in.call_arg_sizes[i] = (uint32_t)(arg_needs_ldouble_abi ? 12 : arg_needs_float_abi ? 4 : arg_size);
    }
    if (cc_target_current()->id == CC_TARGET_I386) {
        /*
         * -mregparm / regparm(N): the first integer arguments go in %eax,
         * %edx, %ecx -- by the callee's declarations for a direct call,
         * by -mregparm through a pointer (whose type gcc's regparm would
         * be an attribute of, which cc does not keep); never for a
         * variadic callee.  A structure result's hidden pointer comes
         * first, in %eax.
         */
        int dflt = cc_options_current() != NULL ? cc_options_current()->i386_regparm : 0;
        const cc_ctype_t *fnt = NULL;
        int nregs;
        int rid = lower_record_id(e);

        if (in.op == CC_SSA_CALLI) {
            fnt = ptr_fnt;
            nregs = fnt != NULL && fnt->variadic ? 0 : dflt;
        } else {
            nregs = cc_abi_i386_function_regparm(tu, e->ident,
                                                 callee != NULL && callee->has_prototype && callee->is_variadic, dflt);
        }
        if (nregs > 0 && rid >= 0 &&
            (cc_abi_aggregate_return(tu, rid, lower_value_size(tu, e)) &
             (CC_ABI_RET_EAX_EDX | CC_ABI_RET_VEC128 | CC_ABI_RET_VEC64 | CC_ABI_RET_X87)) == 0) {
            regparm_sret = 1;
        }
        if (nregs > 0 && in.arg_count > 0) {
            const cc_ctype_t **atypes = (const cc_ctype_t **)calloc(in.arg_count, sizeof(*atypes));

            if (atypes == NULL) {
                free(in.sym);
                free(in.args);
                free(in.call_arg_abi);
                free(in.call_arg_sizes);
                set_diag(diag, "out of memory assigning regparm registers");
                return -1;
            }
            for (i = 0; i < in.arg_count; ++i) {
                atypes[i] = (callee != NULL && callee->has_prototype && i < callee->param_count)
                                ? lower_param_ctype(&callee->params[i])
                            : (fnt != NULL && fnt->prototyped && i < fnt->param_count) ? fnt->params[i]
                                                                                        : lower_value_ctype(e->args[i]);
            }
            cc_abi_i386_regparm_assign(tu, atypes, in.arg_count, nregs, regparm_sret, in.call_arg_abi);
            free(atypes);
        }
    }
    in.call_ret_x87 = (lower_has_kind(e, CC_CT_LDOUBLE)) ? 1 : 0;
    if (lower_has_kind(e, CC_CT_FLOAT)) {
        /* a float comes back in single precision */
        in.agg_class = CC_ABI_RET_FLOAT;
    }
    if (lower_record_id(e) >= 0) {
        long agg_size = lower_value_size(tu, e);
        int agg_addr;
        if (agg_size < 0) {
            free(in.sym);
            free(in.args);
            free(in.call_arg_abi);
            free(in.call_arg_sizes);
            set_diag(diag, "unsupported aggregate return size in call lowering");
            return -1;
        }
        /* an empty structure (size 0) still needs a place for the i386 hidden pointer to name */
        agg_addr = cc_ir_stackalloc(sf, agg_size > 0 ? agg_size : 1, diag);
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
        if (regparm_sret) {
            in.agg_class |= CC_ABI_RET_SRET_REG;
        }
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
