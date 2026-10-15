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

#endif
