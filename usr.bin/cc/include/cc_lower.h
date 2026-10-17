#ifndef CC_LOWER_H
#define CC_LOWER_H

/*
 * cc_lower.h: what the files of AST -> IR lowering share (middle/ast2ir.c,
 * middle/lower_call.c): the variable table, the lowering context, the
 * builtins, and the helpers one file defines and another uses.  Nothing
 * outside lowering includes it.
 */

#include "cc_frontend.h"
#include "cc_consteval.h"
#include "cc_ctype.h"
#include "cc_diag.h"
#include "cc_ssa.h"

/** A variable in scope while a function is lowered: its value or storage, and its type. */
typedef struct {
    char *name;
    int value;
    int addr_value;
    long addr_size;
    int is_static_storage;
    int is_parameter;
    char *static_sym;
    int depth;
    int vla_bytes; /**< a variable-length array's size in bytes, as the value computed for its allocation; else -1 */
    /** a variable-length array's strides (owned, vla_rank of them): the size in bytes of one element of
     *  each dimension, outermost first, as values computed at its declaration; NULL if not a VLA.
     *  For a pointer to one, the first is the size of the array it points to, the rest the array's. */
    int *vla_strides;
    int vla_rank;       /**< the number of strides; 0 if neither a VLA nor a pointer to one */
    int vla_is_pointer; /**< a pointer to a VLA, whose own size is a pointer's */
    const cc_ctype_t *ctype; /**< the variable's declared type; NULL where the declaration recorded none */
} var_entry_t;

/** A C label of the function being lowered, and the IR label it is. */
typedef struct {
    char *name;
    int label;
} label_entry_t;

/** A declaration whose storage is allocated at the function's entry, and the allocation's address. */
typedef struct {
    const cc_stmt_t *decl;
    int value;
} hoisted_alloc_entry_t;

/** What lowering one function keeps: its labels, its hoisted allocations, the function and module. */
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

typedef cc_const_t const_integral_value_t;

/* ast2ir.c */

void free_asm_instr_fields(cc_ssa_instr_t *in);

/**
 * Record a lowering error, which has no source position; the first one
 * recorded is kept.
 *
 * @param d    the diagnostic
 * @param msg  the message
 */
static inline void set_diag(cc_diag_t *d, const char *msg) {
    cc_diag_set(d, "", 0, 0, msg);
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
const cc_ctype_t *lower_value_ctype(const cc_expr_t *e);

/**
 * Whether an expression's value is of an unsigned integer type other
 * than _Bool (whose one value bit never needs extending).
 *
 * @param e  the expression
 * @return   1 or 0
 */
int lower_is_unsigned(const cc_expr_t *e);

/**
 * Whether loading a value of type @p t from memory zero-extends it: an
 * unsigned integer, _Bool or a pointer.
 *
 * @param t  the type, decayed; may be NULL
 * @return   1 or 0
 */
int ct_loads_unsigned(const cc_ctype_t *t);

/** Whether loading expression @p e's value zero-extends it (ct_loads_unsigned()). */
int lower_loads_unsigned(const cc_expr_t *e);

/**
 * The type of a variable as a value: its declared type decayed.
 *
 * @param v  the variable
 * @return   the type, or NULL if not known
 */
const cc_ctype_t *lower_var_ctype(const var_entry_t *v);

/** The type of parameter @p p as a value: its declared type decayed; NULL if not known. */
const cc_ctype_t *lower_param_ctype(const cc_param_t *p);

/** The type of file-scope variable @p g as a value: its declared type decayed; NULL if not known. */
const cc_ctype_t *lower_global_ctype(const cc_global_t *g);

/**
 * The declared type of a file-scope variable, not decayed.
 *
 * @param g  the variable
 * @return   the type, or NULL if not known
 */
const cc_ctype_t *global_decl_ctype(const cc_global_t *g);

/**
 * The kind of IR value a value of type @p t is held in: F64 for a real
 * binary floating type or an imaginary one (held as its one part), I64
 * for everything else -- integers, pointers, and the structures that
 * stand for complex, decimal, wide and vector values.
 *
 * @param t  the type, decayed; may be NULL
 * @return   CC_VAL_F64 or CC_VAL_I64
 */
cc_value_type_t ct_value_kind(const cc_ctype_t *t);

/**
 * The kind of IR value an expression's value is held in (ct_value_kind()).
 *
 * @param e  the expression
 * @return   CC_VAL_F64 or CC_VAL_I64
 */
cc_value_type_t lower_value_kind(const cc_expr_t *e);

/**
 * The structure behind a type: the record it is, or the hidden structure
 * that stands for a complex type (as for the wide, vector and decimal
 * types, which are records already).
 *
 * @param t  the type, or NULL
 * @return   the structure's index, or -1 if the type is not one
 */
int ct_record_id(const cc_ctype_t *t);

/**
 * The structure behind an expression's value (ct_record_id()).
 *
 * @param e  the expression
 * @return   the structure's index, or -1 if the value is not one
 */
int lower_record_id(const cc_expr_t *e);

/**
 * Whether an expression's value is of type kind @p k.
 *
 * @param e  the expression
 * @param k  the kind
 * @return   1 or 0
 */
int lower_has_kind(const cc_expr_t *e, cc_ctype_kind_t k);

/**
 * The size of what an expression's pointer value points to: the step of
 * pointer arithmetic on it, an array pointee's whole size included.
 *
 * @param tu  the translation unit, for structure sizes
 * @param e   an expression whose value is a pointer
 * @return    the size, or -1 where the type does not give one (void, a
 *            function, an incomplete type, a variable-length array)
 */
long lower_pointee_size(const cc_translation_unit_t *tu, const cc_expr_t *e);

/** Whether @p t is of kind @p k. */
int ct_has_kind(const cc_ctype_t *t, cc_ctype_kind_t k);

/**
 * The step of pointer arithmetic on pointer expression @p e: its pointee's
 * size, or 1 for GNU arithmetic on `void *` and function pointers.
 */
long lower_pointer_step(const cc_translation_unit_t *tu, const cc_expr_t *e);

/**
 * Whether expression @p e is a function or a pointer to one, by its type.
 *
 * @param e  the expression, or NULL
 * @return   1 or 0
 */
int lower_designates_function(const cc_expr_t *e);

/** The size of an expression's value (an array's is a pointer's), or <= 0 if it has none. */
long lower_value_size(const cc_translation_unit_t *tu, const cc_expr_t *e);

/**
 * Evaluate an integer constant expression in lowering's context.  Sema
 * folded every sizeof whose size is known, so the evaluator needs no way
 * to size one here: a sizeof left is a variable-length array's, which is
 * no constant.
 *
 * @param tu   the translation unit; may be NULL
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression
 */
int eval_const_integral_expr_value(const cc_translation_unit_t *tu, const cc_expr_t *e,
                                   const_integral_value_t *out);

/**
 * Convert integer value @p v by keeping its low @p bits bits, zero- or
 * sign-extended as @p is_unsigned says, in a new value of @p size bytes:
 * one EXT, which legalization expands for the target.
 *
 * @param sf           the function
 * @param v            the value
 * @param bits         how many of its low bits to keep
 * @param is_unsigned  zero-extend them, rather than sign-extend
 * @param size         the size of the result in bytes
 * @param diag         receives an error
 * @return             the converted value, or -1
 */
int emit_ext(cc_ssa_function_t *sf, int v, int bits, int is_unsigned, long size, cc_diag_t *diag);

/** How many bytes value @p v holds; 0 for no value. */
long value_size(const cc_ssa_function_t *f, int v);

/** The register class of value @p v. */
cc_value_type_t value_type(const cc_ssa_function_t *f, int v);

/** The innermost variable named @p name visible at scope @p depth: its index, or -1. */
int var_find_visible(var_entry_t *vars, size_t var_count, const char *name, int depth);

/** The function named @p name, its definition before a declaration; NULL if none. */
const cc_function_t *find_fn(const cc_translation_unit_t *tu, const char *name);

/** The file-scope variable named @p name; NULL if none. */
const cc_global_t *find_global(const cc_translation_unit_t *tu, const char *name);

/** Whether @p e is an array reached through pointers (its value is an address to index). */
int expr_is_array_pointer_chain(const cc_translation_unit_t *tu, var_entry_t *vars, size_t var_count, int depth,
                                const cc_expr_t *e);

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
int emit_dynalloc_instr(cc_ssa_function_t *sf, int bytes_v, int reuse, cc_diag_t *diag);

/** Value @p v in register class @p dst: itself, or converted between integer and floating; -1 on error. */
int cast_value(cc_ssa_function_t *sf, int v, cc_value_type_t dst, cc_diag_t *diag);

/** A double value @p v widened to long double (x87 extended precision); -1 on error. */
int promote_f64_to_ldouble_value(cc_ssa_function_t *sf, int v, cc_diag_t *diag);

/**
 * The width in bits of a standard integer type, as a value of it is
 * narrowed to; 0 for any other type (a bit-precise or enumerated one
 * included, whose values are left as they are).
 *
 * @param t  the type
 * @return   the width, or 0
 */
int integral_type_bits(const cc_ctype_t *t);

/**
 * Convert an integer value to standard integer type @p t: compare a
 * `_Bool` with zero, otherwise keep the type's low bits, sign- or
 * zero-extended (see normalize_int_ct() for the bit-precise types).
 *
 * @param sf    the function
 * @param v     the value
 * @param t     the type converted to; any type but an integer leaves @p v
 * @param diag  receives an error
 * @return      the converted value, or -1
 */
int normalize_integral_value(cc_ssa_function_t *sf, int v, const cc_ctype_t *t, cc_diag_t *diag);

/**
 * Convert an integer value to integer type @p ct: a standard integer type
 * as normalize_integral_value() does, and a bit-precise type by keeping
 * the low N bits, sign-extended for `_BitInt(N)` and zero-extended for
 * `unsigned _BitInt(N)`, in a value of the size it already has.  A value
 * of any other type is left as it is.
 *
 * @param sf    the function
 * @param v     the value
 * @param ct    the type converted to; may be NULL
 * @param diag  receives an error
 * @return      the converted value, or -1
 */
int normalize_int_ct(cc_ssa_function_t *sf, int v, const cc_ctype_t *ct, cc_diag_t *diag);

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
int normalize_float_ct(cc_ssa_function_t *sf, int v, const cc_ctype_t *ct, cc_diag_t *diag);

/** Evaluate integer constant expression @p e into @p out: 0, or -1 if it is not one. */
int eval_const_i64_expr(const cc_translation_unit_t *tu, const cc_expr_t *e, long long *out);

/** Append `lhs op rhs` on integer values, a constant operand widened to the other's size; -1 on error. */
int emit_i64_binop_instr(cc_ssa_function_t *sf, cc_ssa_opcode_t op, int lhs, int rhs, int is_unsigned,
                         cc_diag_t *diag);

/*
 * One 8-byte constant.  Building it as hi << 32 | lo does not work on
 * i386, where the pieces are 32-bit values and a shift by 32 shifts by 0.
 */
int emit_const_u64_instr(cc_ssa_function_t *sf, unsigned long long v, cc_diag_t *diag);

/** The address of global symbol @p name; -1 on error. */
int emit_global_addr(cc_ssa_function_t *sf, const char *name, cc_diag_t *diag);

/**
 * Clear an instruction as lower_expr() starts one: no operands, no labels.
 *
 * @param in  the instruction
 */
void lower_expr_instr_init(cc_ssa_instr_t *in);

/** Lower expression @p e: its value (an aggregate's address), or -1 on error. */
int lower_expr(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, const lower_ctx_t *ctx,
               var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag);

/** Append a floating constant; its value, or -1 when out of memory. */
int emit_const_f64_instr(cc_ssa_function_t *sf, double v);

/** Append an integer compare, a constant operand widened to the other's size; its 0/1 value, or -1. */
int emit_cmp_instr(cc_ssa_function_t *sf, cc_cmp_kind_t cmp_kind, int lhs, int rhs, int is_unsigned,
                   cc_diag_t *diag);

/** Append a call of memcpy copying @p size bytes; 0, or -1 on error. */
int emit_memcpy_instr(cc_ssa_function_t *sf, int dst_ptr, int src_ptr, long size, cc_diag_t *diag);

/**
 * The floating type a floating builtin's operand is stored and inspected
 * as: float, long double, or double for anything else (the integer and
 * double operands, held as doubles).
 *
 * @param e  the operand
 * @return   float, long double or double
 */
const cc_ctype_t *fp_builtin_storage_type(const cc_expr_t *e);

/**
 * The bytes a floating value of type @p t is stored in to inspect its
 * bits.
 *
 * @param t  float, double or long double
 * @return   4, 8 or 16
 */
long fp_store_size(const cc_ctype_t *t);

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
int lower_nested_env(const cc_translation_unit_t *tu, cc_ssa_function_t *sf, var_entry_t *vars,
                     size_t var_count, const cc_function_t *callee, cc_diag_t *diag);

/* lower_call.c */

/**
 * The structure a parameter is passed as (ct_record_id()).
 *
 * @param p  the parameter
 * @return   the structure's index, or -1 if the parameter is not one
 */
int param_record_id(const cc_param_t *p);

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
                    var_entry_t *vars, size_t var_count, int depth, const cc_expr_t *e, cc_diag_t *diag);

#endif
