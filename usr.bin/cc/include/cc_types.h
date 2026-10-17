#ifndef CC_TYPES_H
#define CC_TYPES_H

#include "cc_ctype.h"
#include "cc_frontend.h"
#include "cc_target.h"

/**
 * Size of a basic type -- a standard arithmetic type other than a
 * complex one, or an enumeration -- on the target being compiled for
 * (cc_target_current()).  cc_ct_size() asks here for those kinds.
 *
 * @param kind  the type's kind
 * @return      its size in bytes, or -1 if @p kind is not a basic kind
 *              (void, a pointer, array, structure or union, a function,
 *              a complex type, `_BitInt(N)`: their size depends on more
 *              than the kind)
 */
long cc_scalar_size(cc_ctype_kind_t kind);

/**
 * Alignment of a basic type on the target being compiled for: the
 * alignment the psABI gives it as a structure member, which is also
 * its `_Alignof`.
 *
 * @param kind  the type's kind
 * @return      its alignment in bytes, or -1 if @p kind is not a basic kind
 */
long cc_scalar_align(cc_ctype_kind_t kind);

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
 * prefix): its element type (cc_string_literal_elem()) is wider than a
 * character.  A `u8` or unprefixed literal is narrow.
 *
 * @param e  a CC_EXPR_STR expression
 * @return   1 if it is wide, else 0 (and 0 for anything else)
 */
int cc_string_literal_is_wide(const cc_expr_t *e);

/**
 * The element type of a string literal, which the parser records in the
 * expression's aux_ctype: char, or a wide literal's character type.
 *
 * @param e  a CC_EXPR_STR expression
 * @return   the type; char for anything else
 */
const cc_ctype_t *cc_string_literal_elem(const cc_expr_t *e);

/**
 * The number of elements of the array a string literal is: its
 * characters (each escape one, each UTF-8 sequence one in a wide
 * literal) and the terminating null.
 *
 * @param e  a CC_EXPR_STR expression
 * @return   the bound, or 0 if @p e is not a quoted string literal
 */
long cc_string_literal_units(const cc_expr_t *e);

#endif
