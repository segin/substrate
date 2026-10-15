#ifndef CC_TYPES_H
#define CC_TYPES_H

#include "cc_frontend.h"
#include "cc_target.h"

/**
 * Size of a scalar type -- an arithmetic type, an enumeration or a
 * pointer -- on the target being compiled for (cc_target_current()).
 *
 * @param t  the type
 * @return   its size in bytes, or -1 if @p t is not a scalar type
 *           (void, a structure or union, a function, `_Atomic(T)`,
 *           `_BitInt(N)`: their size depends on more than the kind)
 */
long cc_type_scalar_size(cc_type_t t);

/**
 * Alignment of a scalar type on the target being compiled for: the
 * alignment the psABI gives it as a structure member, which is also
 * its `_Alignof`.
 *
 * @param t  the type
 * @return   its alignment in bytes, or -1 if @p t is not a scalar type
 */
long cc_type_scalar_align(cc_type_t t);

/**
 * Whether a type is an integer type: `_Bool`, the character, short, int,
 * long and long long types of either signedness, an enumeration or
 * `_BitInt`.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_type_is_integral(cc_type_t t);

/**
 * Whether a type is one of the unsigned integer types (not `_Bool`).
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_type_is_unsigned_integral(cc_type_t t);

/**
 * Whether a type is a real floating type: `_Float16`, `float`, `double`,
 * `long double` or a decimal floating type.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_type_is_floating(cc_type_t t);

/**
 * Whether a type is a complex or imaginary type.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_type_is_complex(cc_type_t t);

/**
 * Whether a type is arithmetic: integral, floating, complex -- or
 * `_Atomic`, which sema treats as its (arithmetic) base type.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_type_is_numeric(cc_type_t t);

/**
 * The integer conversion rank used by the usual arithmetic conversions,
 * coarsened to three steps: 1 for int and narrower, 2 for long, 3 for
 * long long.
 *
 * @param t  an integer type
 * @return   its rank
 */
int cc_type_integer_rank(cc_type_t t);

/**
 * The size in bytes of the integer types of a type's rank on the target
 * being compiled for: 4, the size of `long`, or 8.
 *
 * @param t  an integer type
 * @return   the size of its rank's types
 */
int cc_type_integer_rank_size(cc_type_t t);

/**
 * The integer promotion: `_Bool`, the character types and the short
 * types promote to `int`; every other type is unchanged.
 *
 * @param t  the type
 * @return   its promoted type
 */
cc_type_t cc_type_promote(cc_type_t t);

#endif
