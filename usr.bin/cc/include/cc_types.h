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
 * Size of `_BitInt(N)` (either signedness) on the target being compiled
 * for, as the psABI lays it out: up to 64 bits, the smallest of 1, 2, 4
 * and 8 bytes that holds it; past 64 bits, whole 8-byte words on x86-64
 * and whole 4-byte words on i386.
 *
 * @param width  N, the width in bits
 * @return       its size in bytes, or -1 if @p width is not positive
 */
long cc_bitint_size(long width);

/**
 * Alignment of `_BitInt(N)` on the target being compiled for: its size
 * up to the target's word (8 bytes on x86-64, 4 on i386).
 *
 * @param width  N, the width in bits
 * @return       its alignment in bytes, or -1 if @p width is not positive
 */
long cc_bitint_align(long width);

/**
 * Whether a string literal expression is a wide one (`L`, `u` or `U`
 * prefix): its element type, in the expression's aux_type, is wider than
 * a character.  A `u8` or unprefixed literal is narrow.
 *
 * @param e  a CC_EXPR_STR expression
 * @return   1 if it is wide, else 0 (and 0 for anything else)
 */
int cc_string_literal_is_wide(const cc_expr_t *e);

/**
 * The pointer type a string literal of a given element type decays to:
 * `char *`, `char16_t *` (unsigned short), `wchar_t *` (int) or
 * `char32_t *` (unsigned int).
 *
 * @param elem  the literal's element type
 * @return      the pointer type
 */
cc_type_t cc_string_literal_pointer_type(cc_type_t elem);

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
