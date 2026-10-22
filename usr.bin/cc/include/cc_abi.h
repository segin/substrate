#ifndef CC_ABI_H
#define CC_ABI_H

/*
 * cc_abi.h: how an aggregate crosses a call on each target.
 *
 * Lowering asks these questions of the selected target; the answers are
 * the ABI bytes of cc_ssa.h (CC_CALL_ARG_ABI_* with the CC_ABI_* flags)
 * that the emitters act on.  The rules themselves are per target, in
 * middle/abi_x86_64.c and middle/abi_i386.c, reached through the target
 * descriptor's hooks.
 */

#include "cc_ctype.h"
#include "cc_frontend.h"
#include "cc_ssa.h"
#include "cc_target.h"

/**
 * The argument registers that the aggregate arguments of one call (or
 * the parameters of one function) have taken so far, for a target that
 * passes some aggregates in registers of their own: i386 gives the first
 * three 16-byte vectors %xmm0-2 and the first three 8-byte ones %mm0-2.
 * Zero it before the first argument.
 */
typedef struct cc_abi_regs {
    int vec16; /**< xmm registers taken by vectors */
    int vec8;  /**< MMX registers taken by vectors */
} cc_abi_regs_t;

/** The type va_arg reads, as the ABI needs to know it. */
typedef struct cc_abi_va_type {
    long size;                  /**< its size in bytes */
    cc_value_type_t value_kind; /**< the register class of its value */
    int is_aggregate;           /**< a structure or union: the value is its address */
    int is_fp;                  /**< float or double */
    int is_ldouble;             /**< long double */
    int is_unsigned;            /**< read zero-extended */
} cc_abi_va_type_t;

/**
 * Whether structure @p sid stands for a vector type.
 *
 * @param tu   the translation unit
 * @param sid  a structure index, or -1
 */
static inline int cc_abi_sid_is_vector(const cc_translation_unit_t *tu, int sid) {
    return tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].vector_count > 0;
}

/**
 * CC_ABI_STACK_VEC for a vector of 16 bytes or more, a _Decimal128 or a _Float128,
 * passed in memory (its copy aligned to its size, as gcc places them);
 * CC_ABI_STACK_16 for a _Complex _Float128 (its copy 16-aligned);
 * else 0.
 *
 * @param tu    the translation unit
 * @param sid   the argument's structure
 * @param size  its size
 */
static inline int cc_abi_stack_vec_flag(const cc_translation_unit_t *tu, int sid, long size) {
    int dec128 = tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count &&
                 cc_struct_soft_float_bits(&tu->structs[sid]) == 128;

    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].complex_elem != NULL &&
        tu->structs[sid].complex_elem->kind == CC_CT_RECORD) {
        /* _Complex _Float128 (its parts' structure as the element): 16-aligned, not to its 32 bytes */
        return CC_ABI_STACK_16;
    }
    return (cc_abi_sid_is_vector(tu, sid) || dec128) && size >= 16 ? CC_ABI_STACK_VEC : 0;
}

/**
 * How the x86-64 System V ABI passes an aggregate argument.
 *
 * @param tu    the translation unit
 * @param sid   the structure, union, or the hidden structure of a complex,
 *              decimal or vector type
 * @param size  its size in bytes
 * @param regs  the registers taken so far (unused: x86-64 counts none)
 * @return      the ABI byte: CC_CALL_ARG_ABI_ZERO for an empty one,
 *              CC_CALL_ARG_ABI_AGG_REG with its eightbytes' classes,
 *              CC_CALL_ARG_ABI_AGG_MEM, or CC_CALL_ARG_ABI_DEFAULT for one
 *              that travels as an integer of its size
 */
int cc_abi_x86_64_aggregate_arg(const cc_translation_unit_t *tu, int sid, long size, cc_abi_regs_t *regs);

/**
 * How the x86-64 System V ABI returns an aggregate: the agg_class of the
 * CALL or RET.
 *
 * @param tu    the translation unit
 * @param sid   its structure
 * @param size  its size in bytes
 * @return      the CC_ABI_* flags; 0 for the default (in %rax/%rdx at 16
 *              bytes or fewer, else by hidden pointer)
 */
int cc_abi_x86_64_aggregate_return(const cc_translation_unit_t *tu, int sid, long size);

/**
 * How the i386 System V ABI passes an aggregate argument, as gcc does:
 * on the stack, except that vectors take %xmm0-2 and %mm0-2 while they
 * last, and one of 4 bytes or fewer travels as an integer.
 *
 * @param tu    the translation unit
 * @param sid   its structure
 * @param size  its size in bytes
 * @param regs  the vector registers taken so far; updated
 * @return      the ABI byte, as cc_abi_x86_64_aggregate_arg()'s
 */
int cc_abi_i386_aggregate_arg(const cc_translation_unit_t *tu, int sid, long size, cc_abi_regs_t *regs);

/**
 * How the i386 System V ABI returns an aggregate: the agg_class of the
 * CALL or RET.
 *
 * @param tu    the translation unit
 * @param sid   its structure
 * @param size  its size in bytes
 * @return      the CC_ABI_* flags; 0 for the default (by hidden pointer)
 */
int cc_abi_i386_aggregate_return(const cc_translation_unit_t *tu, int sid, long size);

/**
 * i386 -mregparm / regparm(N): how many of %eax, %edx, %ecx an argument of
 * type @p t takes, as gcc counts them -- an integer, pointer or aggregate
 * one per four bytes (a long long two, a 12-byte structure three) -- or 0
 * for one gcc keeps on the stack without using up a register: a floating
 * value, a structure of one float or double, a vector, an empty structure.
 */
int cc_abi_i386_regparm_words(const cc_translation_unit_t *tu, const cc_ctype_t *t);

/**
 * Give arguments of types @p types the registers of regparm(@p nregs), as
 * gcc does: in order, each that takes words gets the next ones if they are
 * all free; the first that does not fit goes on the stack and so does
 * every one after it.  Each one's ABI byte in @p abi (CC_CALL_ARG_ABI_
 * DEFAULT or _AGG_MEM) gets its register (CC_ABI_WITH_REGPARM).  @p first
 * is the first register free: 1 when a structure result's hidden pointer
 * takes %eax, as gcc passes it under regparm.
 */
void cc_abi_i386_regparm_assign(const cc_translation_unit_t *tu, const cc_ctype_t *const *types, size_t n, int nregs,
                                int first, unsigned char *abi);

/**
 * The regparm count a call of, or the body of, the function named @p name
 * uses: a regparm(N) on any of its declarations, else @p default_regparm
 * (-mregparm); 0 for a variadic function, which gcc always passes on the
 * stack.
 */
int cc_abi_i386_function_regparm(const cc_translation_unit_t *tu, const char *name, int is_variadic,
                                 int default_regparm);

/**
 * The x86-64 va_arg: from the register save area while the va_list
 * record's gp_offset (or fp_offset) says registers remain, else from the
 * overflow area; a long double always from the overflow area, aligned to
 * 16.  The record is updated in place.
 *
 * @param sf       the function
 * @param ap       the va_list: the record's address
 * @param t        the type read
 * @param next_ap  set to -1: the va_list itself does not change
 * @param diag     receives an error
 * @return         the value read (an aggregate's address), or -1
 */
int cc_abi_x86_64_va_arg(cc_ssa_function_t *sf, int ap, const cc_abi_va_type_t *t, int *next_ap, cc_diag_t *diag);

/**
 * The i386 va_arg: every argument is on the stack, in 4-byte slots, and
 * an aggregate's slot holds its address.
 *
 * @param sf       the function
 * @param ap       the va_list: the next argument's address
 * @param t        the type read
 * @param next_ap  receives the va_list's new value, past the argument
 * @param diag     receives an error
 * @return         the value read (an aggregate's address), or -1
 */
int cc_abi_i386_va_arg(cc_ssa_function_t *sf, int ap, const cc_abi_va_type_t *t, int *next_ap, cc_diag_t *diag);

/**
 * The selected target's va_arg.
 *
 * @param sf       the function
 * @param ap       the va_list's value
 * @param t        the type read
 * @param next_ap  receives the va_list's new value, or -1 when the va_list
 *                 itself is unchanged (its record was updated)
 * @param diag     receives an error
 * @return         the value read (an aggregate's address), or -1
 */
static inline int cc_abi_va_arg(cc_ssa_function_t *sf, int ap, const cc_abi_va_type_t *t, int *next_ap,
                                cc_diag_t *diag) {
    return cc_target_current()->abi_va_arg(sf, ap, t, next_ap, diag);
}

/**
 * How the selected target passes an aggregate argument (or receives an
 * aggregate parameter).  A transparent union, or a union passed as the
 * scalar it holds, is not asked about.
 *
 * @param tu    the translation unit
 * @param sid   its structure
 * @param size  its size in bytes; a negative (unknown) size travels as an
 *              integer
 * @param regs  the registers the earlier arguments took; updated
 * @return      the ABI byte
 */
static inline int cc_abi_aggregate_arg(const cc_translation_unit_t *tu, int sid, long size, cc_abi_regs_t *regs) {
    return cc_target_current()->abi_aggregate_arg(tu, sid, size, regs);
}

/**
 * How the selected target returns an aggregate: the agg_class of a CALL
 * or RET of one.
 *
 * @param tu    the translation unit
 * @param sid   its structure
 * @param size  its size in bytes
 * @return      the CC_ABI_* flags, 0 for the target's default
 */
static inline int cc_abi_aggregate_return(const cc_translation_unit_t *tu, int sid, long size) {
    return cc_target_current()->abi_aggregate_return(tu, sid, size);
}

#endif
