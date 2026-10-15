#ifndef CC_CTYPE_H
#define CC_CTYPE_H

#include <stddef.h>

#include "cc_frontend.h"

/*
 * C types as a graph: a pointer, an array or a function is a node that
 * refers to the type it is built from, so `int (*)[7]`, `int *[7]` and
 * `int *(*)(void)` are different types, as C says.  Types are interned:
 * building the same type twice gives the same node, so two types are
 * the same type exactly when their pointers are equal.
 *
 * This replaces cc_type_t, which spelled pointer depth into the
 * enumerator and kept array bounds, struct identity and function
 * signatures beside it (or not at all).  The two live side by side
 * until every consumer has moved over.
 */

/** What kind of type a node is. */
typedef enum {
    CC_CT_VOID = 0,
    CC_CT_BOOL,
    CC_CT_CHAR,
    CC_CT_SCHAR,
    CC_CT_UCHAR,
    CC_CT_SHORT,
    CC_CT_USHORT,
    CC_CT_INT,
    CC_CT_UINT,
    CC_CT_LONG,
    CC_CT_ULONG,
    CC_CT_LLONG,
    CC_CT_ULLONG,
    CC_CT_BITINT,    /**< _BitInt(N): count is N */
    CC_CT_UBITINT,   /**< unsigned _BitInt(N) */
    CC_CT_FLOAT16,
    CC_CT_FLOAT,
    CC_CT_DOUBLE,
    CC_CT_LDOUBLE,
    CC_CT_DECIMAL32,
    CC_CT_DECIMAL64,
    CC_CT_DECIMAL128,
    CC_CT_COMPLEX,   /**< _Complex of: float, double or long double */
    CC_CT_IMAGINARY, /**< _Imaginary of */
    CC_CT_ENUM,      /**< id names the enum, or -1 */
    CC_CT_RECORD,    /**< a struct or union: id indexes the translation unit's structs */
    CC_CT_POINTER,   /**< pointer to of */
    CC_CT_ARRAY,     /**< count elements of of; count < 0 when the bound is unknown */
    CC_CT_FUNCTION   /**< function returning of, taking params */
} cc_ctype_kind_t;

/** Type qualifiers, which are part of a type's identity. */
enum {
    CC_CQ_CONST = 1u,
    CC_CQ_VOLATILE = 2u,
    CC_CQ_RESTRICT = 4u,
    CC_CQ_ATOMIC = 8u
};

typedef struct cc_ctype cc_ctype_t;

/** A type.  Never built directly: use the constructors below. */
struct cc_ctype {
    cc_ctype_kind_t kind;
    unsigned quals;               /**< CC_CQ_* */
    const cc_ctype_t *of;         /**< pointee, element, return or complex element type */
    long count;                   /**< array bound, or _BitInt width */
    int id;                       /**< record or enum id */
    size_t param_count;           /**< function: number of parameters */
    const cc_ctype_t *const *params; /**< function: parameter types */
    unsigned char variadic;       /**< function: ends in `...` */
    unsigned char prototyped;     /**< function: has a parameter list, `f(void)` included */
    const cc_ctype_t *unqual;     /**< the same type without qualifiers */
};

/**
 * Forget every type built so far.  Types from before are invalid after.
 * The pipeline calls this once per translation unit.
 */
void cc_ct_reset(void);

/**
 * A type with no parts: void or an arithmetic type other than _BitInt,
 * _Complex and _Imaginary.
 *
 * @param kind  CC_CT_VOID through CC_CT_DECIMAL128, but not the _BitInt kinds
 * @return      the type, or NULL for another kind
 */
const cc_ctype_t *cc_ct_basic(cc_ctype_kind_t kind);

/**
 * _BitInt(width), signed or not.
 *
 * @param width        the number of bits, at least 1
 * @param is_unsigned  nonzero for unsigned _BitInt
 * @return             the type, or NULL when out of memory
 */
const cc_ctype_t *cc_ct_bitint(long width, int is_unsigned);

/**
 * _Complex or _Imaginary of a real floating type.
 *
 * @param kind  CC_CT_COMPLEX or CC_CT_IMAGINARY
 * @param elem  float, double or long double
 * @return      the type, or NULL
 */
const cc_ctype_t *cc_ct_complex(cc_ctype_kind_t kind, const cc_ctype_t *elem);

/**
 * A struct or union, by its index in the translation unit's structs.
 *
 * @param id  the index
 * @return    the type, or NULL when out of memory
 */
const cc_ctype_t *cc_ct_record(int id);

/**
 * An enumerated type.
 *
 * @param id  its id, or -1 when the parser has not given it one
 * @return    the type, or NULL when out of memory
 */
const cc_ctype_t *cc_ct_enum(int id);

/**
 * Pointer to a type.
 *
 * @param to  the pointee
 * @return    the type, or NULL when out of memory
 */
const cc_ctype_t *cc_ct_pointer(const cc_ctype_t *to);

/**
 * Array of a type.
 *
 * @param of     the element type
 * @param count  the number of elements, or -1 when not known (`T[]`)
 * @return       the type, or NULL when out of memory
 */
const cc_ctype_t *cc_ct_array(const cc_ctype_t *of, long count);

/**
 * A function type.
 *
 * @param ret          the return type
 * @param params       the parameter types (already adjusted: arrays and
 *                     functions as pointers); copied
 * @param param_count  how many
 * @param variadic     nonzero if the list ends in `...`
 * @param prototyped   nonzero if there is a parameter list; `int f()` has
 *                     none
 * @return             the type, or NULL when out of memory
 */
const cc_ctype_t *cc_ct_function(const cc_ctype_t *ret, const cc_ctype_t *const *params, size_t param_count,
                                 int variadic, int prototyped);

/**
 * A type with qualifiers added.
 *
 * @param t      the type
 * @param quals  CC_CQ_* to add
 * @return       the qualified type, or NULL when out of memory
 */
const cc_ctype_t *cc_ct_qualified(const cc_ctype_t *t, unsigned quals);

/** @return whether @p t is _Bool or a (signed or unsigned) integer or enum type */
int cc_ct_is_integer(const cc_ctype_t *t);

/** @return whether @p t is an integer type whose values are never negative */
int cc_ct_is_unsigned(const cc_ctype_t *t);

/** @return whether @p t is a real or complex floating type */
int cc_ct_is_floating(const cc_ctype_t *t);

/** @return whether @p t is an integer or floating type */
int cc_ct_is_arithmetic(const cc_ctype_t *t);

/** @return whether @p t is an arithmetic or pointer type */
int cc_ct_is_scalar(const cc_ctype_t *t);

/**
 * The type an expression of type @p t has when used as a value: an
 * array becomes a pointer to its first element and a function a pointer
 * to itself; qualifiers are dropped.
 *
 * @param t  the type
 * @return   the decayed type, or NULL when out of memory
 */
const cc_ctype_t *cc_ct_decay(const cc_ctype_t *t);

/**
 * The size of a type in bytes on the selected target.
 *
 * @param t   the type
 * @param tu  the translation unit, for struct sizes; may be NULL
 * @return    the size, or -1 for an incomplete type (void, an array of
 *            unknown bound, an incomplete struct); a function is 1, as
 *            gcc has sizeof of one
 */
long cc_ct_size(const cc_ctype_t *t, const cc_translation_unit_t *tu);

/**
 * The alignment of a type in bytes on the selected target.
 *
 * @param t   the type
 * @param tu  the translation unit, for struct alignments; may be NULL
 * @return    the alignment, or -1 if not known
 */
long cc_ct_align(const cc_ctype_t *t, const cc_translation_unit_t *tu);

/**
 * Spell a type as a C type name: "int (*)[7]", "char *const".
 *
 * @param t    the type
 * @param buf  receives the spelling, cut short if it does not fit
 * @param cap  the size of @p buf
 * @return     @p buf
 */
const char *cc_ct_format(const cc_ctype_t *t, char *buf, size_t cap);

/**
 * The type of the same scalar kind as a legacy enumerator, for code that
 * still holds a cc_type_t.
 *
 * @param t  a legacy type that is neither a pointer nor an aggregate
 * @return   the type, or NULL if @p t has no single interned counterpart
 */
const cc_ctype_t *cc_ct_from_scalar(cc_type_t t);

/**
 * The legacy enumerator for a type's kind, for the layout table.
 *
 * @param kind  an arithmetic kind or CC_CT_VOID
 * @return      the enumerator, or CC_TYPE_VOID when there is none
 */
cc_type_t cc_ct_legacy_kind(cc_ctype_kind_t kind);

#endif
