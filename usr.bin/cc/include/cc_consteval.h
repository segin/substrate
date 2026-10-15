#ifndef CC_CONSTEVAL_H
#define CC_CONSTEVAL_H

#include <stdint.h>

#include "cc_frontend.h"

/*
 * Integer constant expressions (C11 6.6): the one evaluator the parser,
 * sema and lowering share.  It works on the AST as the parser builds it;
 * what it cannot know by itself -- the size of a type or of the operand
 * of a sizeof -- it asks its caller through a cc_const_env_t.
 */

/** An integer constant: a value and its type. */
typedef struct {
    /**
     * The value, extended to 64 bits as its type reads it: sign-extended
     * for a signed type, zero-extended for an unsigned one, _Bool or a
     * pointer.  (long long)bits is the value whenever it fits.
     */
    uint64_t bits;
    cc_type_t type;
    int struct_id;
} cc_const_t;

/** What the evaluator asks of the context it runs in. */
typedef struct {
    /**
     * The value of a sizeof expression, in bytes.
     *
     * @param ctx  the caller's context
     * @param e    the CC_EXPR_SIZEOF node
     * @return     the size, or -1 if it is not known here
     */
    long (*sizeof_bytes)(void *ctx, const cc_expr_t *e);

    /**
     * The size of an integral type in bytes, for the types whose size
     * depends on a struct id (enums with a fixed underlying type,
     * _BitInt).  NULL, or a result <= 0, means cc_type_scalar_size().
     *
     * @param ctx        the caller's context
     * @param t          the type
     * @param struct_id  its struct id, or -1
     * @return           the size, or <= 0 if unknown
     */
    long (*type_size)(void *ctx, cc_type_t t, int struct_id);

    void *ctx; /**< passed to the callbacks */
} cc_const_env_t;

/**
 * Evaluate an integer constant expression.
 *
 * @param env  the context; its callbacks may be NULL
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression
 *             (or one this evaluator cannot evaluate)
 */
int cc_const_eval(const cc_const_env_t *env, const cc_expr_t *e, cc_const_t *out);

/**
 * Evaluate an integer constant expression into a long long.
 *
 * @param env  the context
 * @param e    the expression
 * @param out  receives the value; an unsigned value above LLONG_MAX
 *             wraps, as a conversion would
 * @return     0, or -1 if @p e is not an integer constant expression
 */
int cc_const_eval_ll(const cc_const_env_t *env, const cc_expr_t *e, long long *out);

/**
 * Convert a constant to another integral (or pointer) type, as a cast
 * does.
 *
 * @param env        the context
 * @param v          the constant
 * @param t          the type to convert to
 * @param struct_id  its struct id, or -1
 * @param out        receives the result; may be @p v
 * @return           0, or -1 if @p t is not an integral or pointer type
 */
int cc_const_convert(const cc_const_env_t *env, const cc_const_t *v, cc_type_t t, int struct_id, cc_const_t *out);

/**
 * The type a constant expression has, worked out without evaluating it
 * (a conditional's arms both count, though only one is evaluated).
 *
 * @param e  the expression
 * @return   its type; int where it cannot be told
 */
cc_type_t cc_const_type_of(const cc_expr_t *e);

/**
 * The common type of two integer operands after the usual arithmetic
 * conversions (C11 6.3.1.8).
 *
 * @param a  one operand's type
 * @param b  the other's
 * @return   the type both are converted to
 */
cc_type_t cc_const_common_type(cc_type_t a, cc_type_t b);

#endif
