/*
 * types.c: facts about C types that every phase of the compiler needs.
 *
 * This is the one place the size and alignment of a scalar type are
 * decided; the parser (structure layout, sizeof, _Alignof), sema,
 * lowering and the emitter all ask here.
 */

#include "cc_types.h"

/**
 * Look up the layout of a scalar type on a target.
 *
 * @param tgt  the target
 * @param t    the type
 * @param out  receives the size and alignment
 * @return     1 if @p t is a scalar type, 0 if it is not
 */
static int scalar_layout(const cc_target_desc_t *tgt, cc_type_t t, cc_target_scalar_layout_t *out) {
    unsigned char ptr = (unsigned char)tgt->pointer_size;

    if (cc_type_is_pointer(t)) {
        out->size = ptr;
        out->align = ptr;
        return 1;
    }
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_SCHAR:
    case CC_TYPE_UCHAR:
        out->size = 1;
        out->align = 1;
        return 1;
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
    case CC_TYPE_FLOAT16:
        out->size = 2;
        out->align = 2;
        return 1;
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
    case CC_TYPE_FLOAT:
    case CC_TYPE_ENUM:
    case CC_TYPE_DECIMAL32:
        out->size = 4;
        out->align = 4;
        return 1;
    case CC_TYPE_LONG:
    case CC_TYPE_ULONG:
        out->size = ptr;
        out->align = ptr;
        return 1;
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
        *out = tgt->int64;
        return 1;
    case CC_TYPE_DOUBLE:
        *out = tgt->float64;
        return 1;
    case CC_TYPE_LDOUBLE:
        *out = tgt->long_double;
        return 1;
    case CC_TYPE_COMPLEX:
        *out = tgt->complex_double;
        return 1;
    case CC_TYPE_IMAGINARY:
        *out = tgt->imaginary_double;
        return 1;
    case CC_TYPE_DECIMAL64:
        *out = tgt->decimal64;
        return 1;
    case CC_TYPE_DECIMAL128:
        *out = tgt->decimal128;
        return 1;
    default:
        return 0;
    }
}

/**
 * Size of a scalar type on the target being compiled for.
 *
 * @param t  the type
 * @return   its size in bytes, or -1 if @p t is not a scalar type
 */
long cc_type_scalar_size(cc_type_t t) {
    cc_target_scalar_layout_t l;

    if (!scalar_layout(cc_target_current(), t, &l)) {
        return -1;
    }
    return l.size;
}

/**
 * Alignment of a scalar type on the target being compiled for.
 *
 * @param t  the type
 * @return   its alignment in bytes, or -1 if @p t is not a scalar type
 */
long cc_type_scalar_align(cc_type_t t) {
    cc_target_scalar_layout_t l;

    if (!scalar_layout(cc_target_current(), t, &l)) {
        return -1;
    }
    return l.align;
}
