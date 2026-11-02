#ifndef CC_CONSTEVAL_H
#define CC_CONSTEVAL_H

#include <stdint.h>

#include "cc_ctype.h"
#include "cc_frontend.h"

/*
 * Integer constant expressions (C11 6.6): the one evaluator the parser,
 * sema and lowering share.  It works on the AST as the parser builds it;
 * what it cannot know by itself -- the size of the operand of a sizeof --
 * it asks its caller through a cc_const_env_t.  Types are interned types
 * (cc_ctype.h).
 */

/** An integer constant: a value and its type. */
typedef struct {
    /**
     * The value, extended to 64 bits as its type reads it: sign-extended
     * for a signed type, zero-extended for an unsigned one, _Bool or a
     * pointer.  (long long)bits is the value whenever it fits.
     */
    uint64_t bits;
    const cc_ctype_t *type; /**< an integer or pointer type, unqualified */
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
     * The size of a type in bytes, for an element's address in an array
     * of structures (`&((T *)0)->arr[2].m`); NULL to size only types that
     * need no structure table.
     *
     * @param ctx  the caller's context
     * @param t    the type
     * @return     the size, or -1 if it is not known here
     */
    long (*type_bytes)(void *ctx, const cc_ctype_t *t);

    void *ctx; /**< passed to the callbacks */
} cc_const_env_t;

long cc_const_tu_type_bytes(void *ctx, const cc_ctype_t *t);

/**
 * Evaluate an integer constant expression.
 *
 * @param env  the context; its callback may be NULL
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
 * Whether an expression is a null pointer constant (C11 6.3.2.3p3): an
 * integer constant expression of value 0, or one cast to void *.
 *
 * @param env  the context
 * @param e    the expression
 * @return     1 if it is, else 0
 */
int cc_const_is_null_pointer(const cc_const_env_t *env, const cc_expr_t *e);

/**
 * Whether a constant reads as unsigned: its type is an unsigned integer
 * type, _Bool or a pointer.
 *
 * @param v  the constant
 * @return   1 if it does, else 0
 */
int cc_const_is_unsigned(const cc_const_t *v);

/**
 * Whether an integer constant's value is one integer type @p t can hold
 * (a type wider than 64 bits holds any).
 *
 * @param v  the constant
 * @param t  the integer type
 * @return   1 if it is, else 0
 */
int cc_const_fits(const cc_const_t *v, const cc_ctype_t *t);

/**
 * The type a constant expression has, worked out without evaluating it
 * (a conditional's arms both count, though only one is evaluated).
 *
 * @param e  the expression
 * @return   its type, unqualified; int where it cannot be told
 */
const cc_ctype_t *cc_const_type_of(const cc_expr_t *e);

#endif
