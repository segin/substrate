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

/**
 * Size of `_BitInt(N)` on the target being compiled for.
 *
 * @param width  N, the width in bits
 * @return       its size in bytes, or -1 if @p width is not positive
 */
long cc_bitint_size(long width) {
    long word = cc_target_current()->pointer_size;

    if (width <= 0) {
        return -1;
    }
    if (width <= 8) {
        return 1;
    }
    if (width <= 16) {
        return 2;
    }
    if (width <= 32) {
        return 4;
    }
    if (width <= 64) {
        return 8;
    }
    return (width + word * 8 - 1) / (word * 8) * word;
}

/**
 * Whether a string literal expression is a wide one.
 *
 * @param e  a CC_EXPR_STR expression
 * @return   1 if it is wide, else 0
 */
int cc_string_literal_is_wide(const cc_expr_t *e) {
    if (e == NULL || e->kind != CC_EXPR_STR) {
        return 0;
    }
    switch (e->aux_type) {
    case CC_TYPE_USHORT:
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
        return 1;
    default:
        return 0;
    }
}

/**
 * The pointer type a string literal of a given element type decays to.
 *
 * @param elem  the literal's element type
 * @return      the pointer type
 */
cc_type_t cc_string_literal_pointer_type(cc_type_t elem) {
    switch (elem) {
    case CC_TYPE_USHORT:
        return CC_TYPE_PTR_USHORT;
    case CC_TYPE_INT:
        return CC_TYPE_PTR_INT;
    case CC_TYPE_UINT:
        return CC_TYPE_PTR_UINT;
    default:
        return CC_TYPE_PTR_CHAR;
    }
}

/**
 * Alignment of `_BitInt(N)` on the target being compiled for.
 *
 * @param width  N, the width in bits
 * @return       its alignment in bytes, or -1 if @p width is not positive
 */
long cc_bitint_align(long width) {
    long size = cc_bitint_size(width);
    long word = cc_target_current()->pointer_size;

    if (size < 0) {
        return -1;
    }
    return size < word ? size : word;
}

/**
 * Whether a type is an integer type.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_type_is_integral(cc_type_t t) {
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_SCHAR:
    case CC_TYPE_UCHAR:
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
    case CC_TYPE_LONG:
    case CC_TYPE_ULONG:
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
    case CC_TYPE_ENUM:
    case CC_TYPE_BITINT:
        return 1;
    default:
        return 0;
    }
}

/**
 * Whether a type is one of the unsigned integer types.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_type_is_unsigned_integral(cc_type_t t) {
    return t == CC_TYPE_UCHAR || t == CC_TYPE_USHORT || t == CC_TYPE_UINT || t == CC_TYPE_ULONG ||
           t == CC_TYPE_ULONG_LONG;
}

/**
 * Whether a type is a real floating type.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_type_is_floating(cc_type_t t) {
    return t == CC_TYPE_FLOAT16 || t == CC_TYPE_FLOAT || t == CC_TYPE_DOUBLE || t == CC_TYPE_LDOUBLE ||
           t == CC_TYPE_DECIMAL32 || t == CC_TYPE_DECIMAL64 || t == CC_TYPE_DECIMAL128;
}

/**
 * Whether a type is a complex or imaginary type.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_type_is_complex(cc_type_t t) {
    return t == CC_TYPE_COMPLEX || t == CC_TYPE_IMAGINARY;
}

/**
 * Whether a type is arithmetic, `_Atomic` included.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_type_is_numeric(cc_type_t t) {
    return cc_type_is_integral(t) || cc_type_is_floating(t) || cc_type_is_complex(t) || t == CC_TYPE_ATOMIC;
}

/**
 * The coarse integer conversion rank: 1 int and narrower, 2 long,
 * 3 long long.
 *
 * @param t  an integer type
 * @return   its rank
 */
int cc_type_integer_rank(cc_type_t t) {
    switch (t) {
    case CC_TYPE_LONG:
    case CC_TYPE_ULONG:
        return 2;
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
        return 3;
    default:
        return 1;
    }
}

/**
 * The size of the integer types of a type's rank on the current target.
 *
 * @param t  an integer type
 * @return   4, the size of `long`, or 8
 */
int cc_type_integer_rank_size(cc_type_t t) {
    int rank = cc_type_integer_rank(t);

    return rank == 3 ? 8 : rank == 2 ? cc_target_current()->pointer_size : 4;
}

/**
 * The integer promotion of a type.
 *
 * @param t  the type
 * @return   `int` for `_Bool`, character and short types; else @p t
 */
cc_type_t cc_type_promote(cc_type_t t) {
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_SCHAR:
    case CC_TYPE_UCHAR:
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
        return CC_TYPE_INT;
    default:
        return t;
    }
}
