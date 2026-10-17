/*
 * ctype.c: C types as an interned graph (see cc_ctype.h).
 */

#include "cc_ctype.h"
#include "cc_target.h"
#include "cc_types.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The intern table: open addressing over every node built so far. */
static cc_ctype_t **g_table;
static size_t g_cap;
static size_t g_count;

/**
 * Hash the fields that make a type what it is.
 *
 * @param k  the key
 * @return   its hash
 */
static size_t ct_hash(const cc_ctype_t *k) {
    uint64_t h = 1469598103934665603ULL;
    size_t i;

#define MIX(v) (h = (h ^ (uint64_t)(v)) * 1099511628211ULL)
    MIX(k->kind);
    MIX(k->quals);
    MIX((uintptr_t)k->of);
    MIX(k->count);
    MIX(k->id);
    MIX(k->param_count);
    MIX(k->variadic);
    MIX(k->prototyped);
    for (i = 0; i < k->param_count; ++i) {
        MIX((uintptr_t)k->params[i]);
    }
#undef MIX
    return (size_t)h;
}

/**
 * Whether two keys describe the same type.  The parts are interned, so
 * comparing their pointers compares them.
 *
 * @param a  one
 * @param b  the other
 * @return   1 if they do, else 0
 */
static int ct_same(const cc_ctype_t *a, const cc_ctype_t *b) {
    size_t i;

    if (a->kind != b->kind || a->quals != b->quals || a->of != b->of || a->count != b->count || a->id != b->id ||
        a->param_count != b->param_count || a->variadic != b->variadic || a->prototyped != b->prototyped) {
        return 0;
    }
    for (i = 0; i < a->param_count; ++i) {
        if (a->params[i] != b->params[i]) {
            return 0;
        }
    }
    return 1;
}

/**
 * Double the intern table.
 *
 * @return 0, or -1 when out of memory
 */
static int ct_grow(void) {
    size_t ncap = g_cap == 0 ? 256 : g_cap * 2;
    cc_ctype_t **next = (cc_ctype_t **)calloc(ncap, sizeof(*next));
    size_t i;

    if (next == NULL) {
        return -1;
    }
    for (i = 0; i < g_cap; ++i) {
        if (g_table[i] != NULL) {
            size_t j = ct_hash(g_table[i]) & (ncap - 1);
            while (next[j] != NULL) {
                j = (j + 1) & (ncap - 1);
            }
            next[j] = g_table[i];
        }
    }
    free(g_table);
    g_table = next;
    g_cap = ncap;
    return 0;
}

/**
 * The node for a key: the one built before, or a new copy of the key.
 *
 * @param key  the type wanted; its params, if any, are copied
 * @return     the node, or NULL when out of memory
 */
static const cc_ctype_t *ct_intern(const cc_ctype_t *key) {
    cc_ctype_t *n;
    size_t i;

    if ((g_count + 1) * 2 > g_cap && ct_grow() != 0) {
        return NULL;
    }
    i = ct_hash(key) & (g_cap - 1);
    while (g_table[i] != NULL) {
        if (ct_same(g_table[i], key)) {
            return g_table[i];
        }
        i = (i + 1) & (g_cap - 1);
    }
    /* the parameter list lives in the same block as the node */
    n = (cc_ctype_t *)malloc(sizeof(*n) + key->param_count * sizeof(n->params[0]));
    if (n == NULL) {
        return NULL;
    }
    *n = *key;
    if (key->param_count > 0) {
        const cc_ctype_t **params = (const cc_ctype_t **)(n + 1);
        memcpy(params, key->params, key->param_count * sizeof(params[0]));
        n->params = params;
    } else {
        n->params = NULL;
    }
    if (n->quals == 0) {
        n->unqual = n;
    }
    g_table[i] = n;
    ++g_count;
    return n;
}

/**
 * A key with every field cleared but the kind.
 *
 * @param kind  the kind
 * @return      the key
 */
static cc_ctype_t ct_key(cc_ctype_kind_t kind) {
    cc_ctype_t k;

    memset(&k, 0, sizeof(k));
    k.kind = kind;
    k.id = -1;
    return k;
}

/**
 * Forget every type built so far.
 */
/** The structures standing for float, double and long double _Complex; -1 where none is made. */
static int ct_complex_records[3] = {-1, -1, -1};

void cc_ct_reset(void) {
    size_t i;

    for (i = 0; i < g_cap; ++i) {
        free(g_table[i]);
    }
    free(g_table);
    g_table = NULL;
    g_cap = 0;
    g_count = 0;
    for (i = 0; i < sizeof(ct_complex_records) / sizeof(ct_complex_records[0]); ++i) {
        ct_complex_records[i] = -1;
    }
}

/**
 * A type with no parts.
 *
 * @param kind  a void or arithmetic kind other than _BitInt, _Complex, _Imaginary
 * @return      the type, or NULL
 */
const cc_ctype_t *cc_ct_basic(cc_ctype_kind_t kind) {
    cc_ctype_t k;

    if (kind > CC_CT_DECIMAL128 || kind == CC_CT_BITINT || kind == CC_CT_UBITINT) {
        return NULL;
    }
    k = ct_key(kind);
    return ct_intern(&k);
}

/**
 * _BitInt(width), signed or not.
 *
 * @param width        the number of bits
 * @param is_unsigned  nonzero for unsigned _BitInt
 * @return             the type, or NULL
 */
const cc_ctype_t *cc_ct_bitint(long width, int is_unsigned) {
    cc_ctype_t k = ct_key(is_unsigned ? CC_CT_UBITINT : CC_CT_BITINT);

    if (width < 1) {
        return NULL;
    }
    k.count = width;
    return ct_intern(&k);
}

/**
 * _Complex or _Imaginary of a real floating type.
 *
 * @param kind  CC_CT_COMPLEX or CC_CT_IMAGINARY
 * @param elem  float, double or long double
 * @return      the type, or NULL
 */
const cc_ctype_t *cc_ct_complex(cc_ctype_kind_t kind, const cc_ctype_t *elem) {
    cc_ctype_t k = ct_key(kind);

    if ((kind != CC_CT_COMPLEX && kind != CC_CT_IMAGINARY) || elem == NULL ||
        (elem->kind != CC_CT_FLOAT && elem->kind != CC_CT_DOUBLE && elem->kind != CC_CT_LDOUBLE)) {
        return NULL;
    }
    k.of = elem->unqual;
    return ct_intern(&k);
}

/** The slot in ct_complex_records for an element kind, or -1. */
static int ct_complex_slot(cc_ctype_kind_t elem) {
    return elem == CC_CT_FLOAT ? 0 : (elem == CC_CT_DOUBLE ? 1 : (elem == CC_CT_LDOUBLE ? 2 : -1));
}

/**
 * Record the structure, `{ T __re, __im; }`, that stands for _Complex T in
 * the legacy encoding (which has no complex kind of its own per element).
 *
 * @param elem  float, double or long double
 * @param id    the structure's index in the translation unit
 */
void cc_ct_set_complex_record(cc_ctype_kind_t elem, int id) {
    int slot = ct_complex_slot(elem);

    if (slot >= 0) {
        ct_complex_records[slot] = id;
    }
}

/**
 * The structure standing for a complex type in the legacy encoding.
 *
 * @param t  a _Complex type
 * @return   its index in the translation unit, or -1 if none was made
 */
int cc_ct_complex_record(const cc_ctype_t *t) {
    int slot;

    if (t == NULL || t->kind != CC_CT_COMPLEX || t->of == NULL) {
        return -1;
    }
    slot = ct_complex_slot(t->of->kind);
    return slot < 0 ? -1 : ct_complex_records[slot];
}

/**
 * A struct or union.
 *
 * @param id  its index in the translation unit's structs
 * @return    the type, or NULL
 */
const cc_ctype_t *cc_ct_record(int id) {
    cc_ctype_t k = ct_key(CC_CT_RECORD);

    k.id = id;
    return ct_intern(&k);
}

/**
 * An enumerated type.
 *
 * @param id  its id, or -1
 * @return    the type, or NULL
 */
const cc_ctype_t *cc_ct_enum(int id) {
    cc_ctype_t k = ct_key(CC_CT_ENUM);

    k.id = id;
    return ct_intern(&k);
}

/**
 * Pointer to a type.
 *
 * @param to  the pointee
 * @return    the type, or NULL
 */
const cc_ctype_t *cc_ct_pointer(const cc_ctype_t *to) {
    cc_ctype_t k = ct_key(CC_CT_POINTER);

    if (to == NULL) {
        return NULL;
    }
    k.of = to;
    return ct_intern(&k);
}

/**
 * Array of a type.
 *
 * @param of     the element type
 * @param count  the number of elements, or -1
 * @return       the type, or NULL
 */
const cc_ctype_t *cc_ct_array(const cc_ctype_t *of, long count) {
    cc_ctype_t k = ct_key(CC_CT_ARRAY);

    if (of == NULL) {
        return NULL;
    }
    k.of = of;
    k.count = count < 0 ? -1 : count;
    return ct_intern(&k);
}

/**
 * A function type.
 *
 * @param ret          the return type
 * @param params       the parameter types
 * @param param_count  how many
 * @param variadic     nonzero for `...`
 * @param prototyped   nonzero if there is a parameter list
 * @return             the type, or NULL
 */
const cc_ctype_t *cc_ct_function(const cc_ctype_t *ret, const cc_ctype_t *const *params, size_t param_count,
                                 int variadic, int prototyped) {
    cc_ctype_t k = ct_key(CC_CT_FUNCTION);
    size_t i;

    if (ret == NULL || (param_count > 0 && params == NULL)) {
        return NULL;
    }
    for (i = 0; i < param_count; ++i) {
        if (params[i] == NULL) {
            return NULL;
        }
    }
    k.of = ret;
    k.params = params;
    k.param_count = param_count;
    k.variadic = variadic ? 1 : 0;
    k.prototyped = prototyped ? 1 : 0;
    return ct_intern(&k);
}

/**
 * A type with qualifiers added.  Qualifying an array qualifies its
 * elements (C11 6.7.3p9).
 *
 * @param t      the type
 * @param quals  CC_CQ_* to add
 * @return       the qualified type, or NULL
 */
const cc_ctype_t *cc_ct_qualified(const cc_ctype_t *t, unsigned quals) {
    cc_ctype_t k;
    cc_ctype_t *n;

    if (t == NULL) {
        return NULL;
    }
    if (quals == 0 || (t->quals | quals) == t->quals) {
        return t;
    }
    if (t->kind == CC_CT_ARRAY) {
        return cc_ct_array(cc_ct_qualified(t->of, quals), t->count);
    }
    k = *t;
    k.quals |= quals;
    k.unqual = t->unqual;
    n = (cc_ctype_t *)ct_intern(&k);
    if (n != NULL && n->unqual == NULL) {
        n->unqual = t->unqual;
    }
    return n;
}

/**
 * Whether a type is _Bool or an integer or enum type.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_ct_is_integer(const cc_ctype_t *t) {
    return t != NULL && ((t->kind >= CC_CT_BOOL && t->kind <= CC_CT_UBITINT) || t->kind == CC_CT_ENUM);
}

/**
 * Whether a type is an integer type that holds no negative values.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_ct_is_unsigned(const cc_ctype_t *t) {
    if (t == NULL) {
        return 0;
    }
    switch (t->kind) {
    case CC_CT_BOOL:
    case CC_CT_UCHAR:
    case CC_CT_USHORT:
    case CC_CT_UINT:
    case CC_CT_ULONG:
    case CC_CT_ULLONG:
    case CC_CT_UBITINT:
        return 1;
    default:
        return 0;
    }
}

/**
 * Whether a type is a real or complex floating type.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_ct_is_floating(const cc_ctype_t *t) {
    return t != NULL && t->kind >= CC_CT_FLOAT16 && t->kind <= CC_CT_IMAGINARY;
}

/**
 * Whether a type is an integer or floating type.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_ct_is_arithmetic(const cc_ctype_t *t) {
    return cc_ct_is_integer(t) || cc_ct_is_floating(t);
}

/**
 * Whether a type is an arithmetic or pointer type.
 *
 * @param t  the type
 * @return   1 if it is, else 0
 */
int cc_ct_is_scalar(const cc_ctype_t *t) {
    return cc_ct_is_arithmetic(t) || (t != NULL && t->kind == CC_CT_POINTER);
}

/**
 * The type of a value of type @p t: arrays and functions decay to
 * pointers, and qualifiers are dropped.
 *
 * @param t  the type
 * @return   the decayed type, or NULL
 */
const cc_ctype_t *cc_ct_decay(const cc_ctype_t *t) {
    if (t == NULL) {
        return NULL;
    }
    if (t->kind == CC_CT_ARRAY) {
        return cc_ct_pointer(t->of);
    }
    if (t->kind == CC_CT_FUNCTION) {
        return cc_ct_pointer(t->unqual);
    }
    return t->unqual;
}

/**
 * The width in bits of an integer or pointer type.
 *
 * @param t  the type
 * @return   its width, 1 for _Bool; 0 if @p t is neither, or its size
 *           is not known
 */
long cc_ct_int_width(const cc_ctype_t *t) {
    long size;

    if (t == NULL) {
        return 0;
    }
    switch (t->kind) {
    case CC_CT_BOOL:
        return 1;
    case CC_CT_POINTER:
        return (long)cc_target_pointer_size() * 8;
    case CC_CT_BITINT:
    case CC_CT_UBITINT:
        return t->count > 0 ? t->count : 0;
    default:
        break;
    }
    if (!cc_ct_is_integer(t)) {
        return 0;
    }
    size = cc_ct_size(t, NULL);
    return size > 0 ? size * 8 : 0;
}

/**
 * The integer promotion of a type.  See cc_ctype.h.
 *
 * @param t  the type; NULL reads as int
 * @return   the promoted type, unqualified
 */
const cc_ctype_t *cc_ct_promote(const cc_ctype_t *t) {
    if (t == NULL) {
        return cc_ct_basic(CC_CT_INT);
    }
    switch (t->kind) {
    case CC_CT_ENUM:
    case CC_CT_BOOL:
    case CC_CT_CHAR:
    case CC_CT_SCHAR:
    case CC_CT_UCHAR:
    case CC_CT_SHORT:
    case CC_CT_USHORT:
        return cc_ct_basic(CC_CT_INT);
    case CC_CT_POINTER:
        return cc_ct_basic(cc_target_pointer_size() >= 8 ? CC_CT_ULONG : CC_CT_UINT);
    default:
        return t->unqual;
    }
}

/**
 * An integer type's conversion rank (C23 6.3.1.1), as a number to
 * compare: wider types rank higher, and of two the same width a standard
 * type ranks above a bit-precise one, `long long` above `long`.
 *
 * @param t  a promoted integer type
 * @return   its rank
 */
static long ct_int_rank(const cc_ctype_t *t) {
    long std;

    switch (t->kind) {
    case CC_CT_INT:
    case CC_CT_UINT:
        std = 1;
        break;
    case CC_CT_LONG:
    case CC_CT_ULONG:
        std = 2;
        break;
    case CC_CT_LLONG:
    case CC_CT_ULLONG:
        std = 3;
        break;
    default:
        std = 0;
        break;
    }
    return cc_ct_int_width(t) * 4 + std;
}

/**
 * The unsigned type corresponding to a signed one.
 *
 * @param t  a promoted signed integer type
 * @return   its unsigned counterpart
 */
static const cc_ctype_t *ct_unsigned_of(const cc_ctype_t *t) {
    switch (t->kind) {
    case CC_CT_LONG:
        return cc_ct_basic(CC_CT_ULONG);
    case CC_CT_LLONG:
        return cc_ct_basic(CC_CT_ULLONG);
    case CC_CT_BITINT:
        return cc_ct_bitint(t->count, 1);
    default:
        return cc_ct_basic(CC_CT_UINT);
    }
}

/**
 * The common type of two integer operands.  See cc_ctype.h.
 *
 * @param a  one operand's type
 * @param b  the other's
 * @return   the common type
 */
const cc_ctype_t *cc_ct_common_int(const cc_ctype_t *a, const cc_ctype_t *b) {
    const cc_ctype_t *u;
    const cc_ctype_t *s;

    a = cc_ct_promote(a);
    b = cc_ct_promote(b);
    if (!cc_ct_is_integer(a) || !cc_ct_is_integer(b)) {
        return cc_ct_basic(CC_CT_INT);
    }
    if (a == b) {
        return a;
    }
    if (cc_ct_is_unsigned(a) == cc_ct_is_unsigned(b)) {
        return ct_int_rank(a) >= ct_int_rank(b) ? a : b;
    }
    u = cc_ct_is_unsigned(a) ? a : b;
    s = cc_ct_is_unsigned(a) ? b : a;
    if (ct_int_rank(u) >= ct_int_rank(s)) {
        return u;
    }
    if (cc_ct_int_width(s) > cc_ct_int_width(u)) {
        return s;
    }
    return ct_unsigned_of(s);
}

/**
 * The type of an expression, where the parser recorded it.  See
 * cc_ctype.h.
 *
 * @param e  the expression
 * @return   its type, or NULL if it is not known
 */
const cc_ctype_t *cc_ct_of_expr(const cc_expr_t *e) {
    const cc_ctype_t *t;

    if (e == NULL) {
        return NULL;
    }
    if (e->kind == CC_EXPR_IDENT || e->kind == CC_EXPR_MEMBER || e->kind == CC_EXPR_CALL ||
        e->kind == CC_EXPR_CAST) {
        return e->ctype;
    }
    if (e->kind == CC_EXPR_ADDR) {
        /* `&x` points to x as it is, an array included (no decay) */
        t = cc_ct_of_expr(e->lhs);
        return t != NULL ? cc_ct_pointer(t) : NULL;
    }
    if (e->kind == CC_EXPR_BIN && e->op == CC_BIN_ADD) {
        /* pointer + integer, either way round, as `a[i]` is `*(a + i)` */
        t = cc_ct_decay(cc_ct_of_expr(e->lhs));
        if (t == NULL || t->kind != CC_CT_POINTER) {
            t = cc_ct_decay(cc_ct_of_expr(e->rhs));
        }
        return (t != NULL && t->kind == CC_CT_POINTER) ? t : NULL;
    }
    if (e->kind != CC_EXPR_DEREF) {
        return NULL;
    }
    t = cc_ct_of_expr(e->lhs);
    if (t == NULL || t->kind == CC_CT_FUNCTION) {
        return t;
    }
    t = cc_ct_decay(t);
    return (t != NULL && t->kind == CC_CT_POINTER) ? t->of : NULL;
}

/**
 * Whether an expression is known to be a function or a pointer to one.
 * See cc_ctype.h.
 *
 * @param e  the expression
 * @return   1 if so, 0 if not or not known
 */
int cc_ct_expr_designates_function(const cc_expr_t *e) {
    const cc_ctype_t *t = cc_ct_of_expr(e);

    if (t == NULL) {
        return 0;
    }
    return t->kind == CC_CT_FUNCTION ||
           (t->kind == CC_CT_POINTER && t->of != NULL && t->of->kind == CC_CT_FUNCTION);
}

/**
 * The legacy enumerator for a kind, for the layout table.
 *
 * @param kind  an arithmetic kind or CC_CT_VOID
 * @return      the enumerator, or CC_TYPE_VOID
 */
cc_type_t cc_ct_legacy_kind(cc_ctype_kind_t kind) {
    switch (kind) {
    case CC_CT_BOOL:
        return CC_TYPE_BOOL;
    case CC_CT_CHAR:
        return CC_TYPE_CHAR;
    case CC_CT_SCHAR:
        return CC_TYPE_SCHAR;
    case CC_CT_UCHAR:
        return CC_TYPE_UCHAR;
    case CC_CT_SHORT:
        return CC_TYPE_SHORT;
    case CC_CT_USHORT:
        return CC_TYPE_USHORT;
    case CC_CT_INT:
        return CC_TYPE_INT;
    case CC_CT_UINT:
        return CC_TYPE_UINT;
    case CC_CT_LONG:
        return CC_TYPE_LONG;
    case CC_CT_ULONG:
        return CC_TYPE_ULONG;
    case CC_CT_LLONG:
        return CC_TYPE_LONG_LONG;
    case CC_CT_ULLONG:
        return CC_TYPE_ULONG_LONG;
    case CC_CT_FLOAT16:
        return CC_TYPE_FLOAT16;
    case CC_CT_FLOAT:
        return CC_TYPE_FLOAT;
    case CC_CT_DOUBLE:
        return CC_TYPE_DOUBLE;
    case CC_CT_LDOUBLE:
        return CC_TYPE_LDOUBLE;
    case CC_CT_DECIMAL32:
        return CC_TYPE_DECIMAL32;
    case CC_CT_DECIMAL64:
        return CC_TYPE_DECIMAL64;
    case CC_CT_DECIMAL128:
        return CC_TYPE_DECIMAL128;
    case CC_CT_ENUM:
        return CC_TYPE_ENUM;
    default:
        return CC_TYPE_VOID;
    }
}

/**
 * The interned type of the same scalar kind as a legacy enumerator.
 *
 * @param t  a legacy non-pointer, non-aggregate type
 * @return   the type, or NULL when there is no single counterpart
 */
const cc_ctype_t *cc_ct_from_scalar(cc_type_t t) {
    cc_ctype_kind_t k;

    if (t == CC_TYPE_COMPLEX || t == CC_TYPE_IMAGINARY) {
        return cc_ct_complex(t == CC_TYPE_COMPLEX ? CC_CT_COMPLEX : CC_CT_IMAGINARY, cc_ct_basic(CC_CT_DOUBLE));
    }
    if (t == CC_TYPE_ENUM) {
        return cc_ct_enum(-1);
    }
    for (k = CC_CT_VOID; k <= CC_CT_DECIMAL128; k = (cc_ctype_kind_t)(k + 1)) {
        if (k != CC_CT_BITINT && k != CC_CT_UBITINT && cc_ct_legacy_kind(k) == t) {
            return cc_ct_basic(k);
        }
    }
    return NULL;
}

/**
 * The type under every pointer level of a legacy type.
 *
 * @param base       a legacy type that is not a pointer
 * @param struct_id  its struct id, _BitInt width or _Atomic's type
 * @return           the type, or NULL when there is no counterpart
 */
static const cc_ctype_t *ct_from_legacy_base(cc_type_t base, int struct_id) {
    switch (base) {
    case CC_TYPE_VOID:
        return struct_id >= 0 ? cc_ct_record(struct_id) : cc_ct_basic(CC_CT_VOID);
    case CC_TYPE_BITINT:
        return cc_ct_bitint(struct_id > 0 ? struct_id : 1, 0);
    case CC_TYPE_ATOMIC:
        return cc_ct_qualified(ct_from_legacy_base(struct_id > 0 ? (cc_type_t)struct_id : CC_TYPE_INT, -1),
                               CC_CQ_ATOMIC);
    case CC_TYPE_ENUM:
        return cc_ct_enum(struct_id);
    case CC_TYPE_FUNC:
        return cc_ct_function(cc_ct_basic(CC_CT_INT), NULL, 0, 0, 0);
    default:
        return cc_ct_from_scalar(base);
    }
}

/**
 * A type rebuilt from the legacy encoding.
 *
 * @param t          the legacy type
 * @param struct_id  its struct id (or _BitInt width, or _Atomic's type)
 * @param ndim       its number of array dimensions
 * @param dims       their bounds, outermost first
 * @return           the type, or NULL
 */
const cc_ctype_t *cc_ct_from_legacy(cc_type_t t, int struct_id, int ndim, const long *dims) {
    unsigned depth = cc_type_pointer_depth(t);
    const cc_ctype_t *ct;
    unsigned i;
    int d;

    if (t == CC_TYPE_FUNC) {
        /* a pointer to a function the encoding knows nothing more about */
        return cc_ct_pointer(ct_from_legacy_base(CC_TYPE_FUNC, -1));
    }
    ct = ct_from_legacy_base(depth > 0 ? cc_type_pointer_base(t) : t, struct_id);
    if (ndim < 0 || (unsigned)ndim > depth) {
        ndim = (int)depth;
    }
    for (i = (unsigned)ndim; i < depth; ++i) {
        ct = cc_ct_pointer(ct);
    }
    for (d = ndim - 1; d >= 0; --d) {
        ct = cc_ct_array(ct, dims[d]);
    }
    return ct;
}

/**
 * Project a type onto the legacy encoding.
 *
 * @param t    the type
 * @param out  receives the encoding
 * @return     0, or -1 if it has too many array dimensions or no
 *             counterpart
 */
int cc_ct_to_legacy(const cc_ctype_t *t, cc_ct_legacy_t *out) {
    int seen_pointer = 0;

    memset(out, 0, sizeof(*out));
    out->array_len = -1;
    out->struct_id = -1;
    while (t != NULL) {
        if (t->kind == CC_CT_POINTER) {
            ++out->depth;
            seen_pointer = 1;
        } else if (t->kind == CC_CT_ARRAY) {
            long n = t->count < 0 ? 0 : t->count;
            if (out->ndim >= CC_MAX_ARRAY_DIMS) {
                return -1;
            }
            if (seen_pointer) {
                out->lossy = 1;
            }
            out->dims[out->ndim++] = n;
            ++out->depth;
            if (out->array_len < 0) {
                out->array_len = n;
            } else if (out->array_len == 0 || n == 0 || out->array_len > LONG_MAX / n) {
                out->array_len = 0;
            } else {
                out->array_len *= n;
            }
        } else if (t->kind != CC_CT_FUNCTION) {
            break;
        }
        t = t->of;
    }
    if (t == NULL) {
        return -1;
    }
    if (t->quals & CC_CQ_ATOMIC) {
        out->base = CC_TYPE_ATOMIC;
        out->struct_id = (int)cc_ct_legacy_kind(t->unqual->kind);
        return 0;
    }
    switch (t->kind) {
    case CC_CT_VOID:
        out->base = CC_TYPE_VOID;
        return 0;
    case CC_CT_RECORD:
        out->base = CC_TYPE_VOID;
        out->struct_id = t->id;
        return 0;
    case CC_CT_ENUM:
        out->base = CC_TYPE_ENUM;
        out->struct_id = t->id;
        return 0;
    case CC_CT_BITINT:
    case CC_CT_UBITINT:
        out->base = CC_TYPE_BITINT;
        out->struct_id = (int)t->count;
        return 0;
    case CC_CT_COMPLEX:
        /* the structure that stands for it, where the parser made one */
        if (cc_ct_complex_record(t) >= 0) {
            out->base = CC_TYPE_VOID;
            out->struct_id = cc_ct_complex_record(t);
            return 0;
        }
        out->base = CC_TYPE_COMPLEX;
        return 0;
    case CC_CT_IMAGINARY:
        out->base = CC_TYPE_IMAGINARY;
        return 0;
    default:
        out->base = cc_ct_legacy_kind(t->kind);
        return 0;
    }
}

/**
 * The size of a type in bytes.
 *
 * @param t   the type
 * @param tu  the translation unit, for struct sizes; may be NULL
 * @return    the size, or -1 for an incomplete type
 */
long cc_ct_size(const cc_ctype_t *t, const cc_translation_unit_t *tu) {
    long n;

    if (t == NULL) {
        return -1;
    }
    switch (t->kind) {
    case CC_CT_VOID:
        return -1;
    case CC_CT_BITINT:
    case CC_CT_UBITINT:
        return cc_bitint_size(t->count);
    case CC_CT_COMPLEX:
        n = cc_ct_size(t->of, tu);
        return n < 0 ? -1 : 2 * n;
    case CC_CT_IMAGINARY:
        return cc_ct_size(t->of, tu);
    case CC_CT_RECORD:
        if (tu == NULL || t->id < 0 || (size_t)t->id >= tu->struct_count || !tu->structs[t->id].complete) {
            return -1;
        }
        return tu->structs[t->id].size;
    case CC_CT_POINTER:
        return cc_target_pointer_size();
    case CC_CT_ARRAY:
        n = cc_ct_size(t->of, tu);
        if (t->count < 0 || n < 0 || (n > 0 && t->count > LONG_MAX / n)) {
            return -1;
        }
        return t->count * n;
    case CC_CT_FUNCTION:
        return 1;
    default:
        return cc_type_scalar_size(cc_ct_legacy_kind(t->kind));
    }
}

int cc_ct_is_character(const cc_ctype_t *t) {
    return t != NULL && (t->kind == CC_CT_CHAR || t->kind == CC_CT_SCHAR || t->kind == CC_CT_UCHAR);
}

long cc_ct_sizeof_type(const cc_ctype_t *t, const cc_translation_unit_t *tu) {
    if (t == NULL) {
        return -1;
    }
    return t->kind == CC_CT_VOID ? 1 : cc_ct_size(t, tu);
}

/**
 * The alignment of a type in bytes.
 *
 * @param t   the type
 * @param tu  the translation unit, for struct alignments; may be NULL
 * @return    the alignment, or -1 if not known
 */
long cc_ct_align(const cc_ctype_t *t, const cc_translation_unit_t *tu) {
    if (t == NULL) {
        return -1;
    }
    switch (t->kind) {
    case CC_CT_VOID:
        return -1;
    case CC_CT_BITINT:
    case CC_CT_UBITINT:
        return cc_bitint_align(t->count);
    case CC_CT_COMPLEX:
    case CC_CT_IMAGINARY:
    case CC_CT_ARRAY:
        return cc_ct_align(t->of, tu);
    case CC_CT_RECORD:
        if (tu == NULL || t->id < 0 || (size_t)t->id >= tu->struct_count || !tu->structs[t->id].complete) {
            return -1;
        }
        return tu->structs[t->id].align;
    case CC_CT_POINTER:
        return cc_target_pointer_size();
    case CC_CT_FUNCTION:
        return 1;
    default:
        return cc_type_scalar_align(cc_ct_legacy_kind(t->kind));
    }
}

const cc_struct_member_t *cc_ct_union_cast_member(const cc_translation_unit_t *tu, int sid, const cc_ctype_t *src) {
    const cc_struct_def_t *sd;
    size_t i;

    src = cc_ct_decay(src);
    if (tu == NULL || src == NULL || sid < 0 || (size_t)sid >= tu->struct_count) {
        return NULL;
    }
    sd = &tu->structs[sid];
    if (!sd->complete || !sd->is_union) {
        return NULL;
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        const cc_ctype_t *mt = m->ctype != NULL ? m->ctype
                                                : cc_ct_from_legacy(m->type, m->type_struct_id, m->array_ndim,
                                                                    m->array_dims);

        if (mt != NULL && mt->unqual == src->unqual) {
            return m;
        }
    }
    return NULL;
}

/**
 * Append a string to a buffer, cutting it short if it does not fit.
 *
 * @param buf  the buffer, NUL-terminated
 * @param cap  its size
 * @param s    what to append
 */
static void ct_cat(char *buf, size_t cap, const char *s) {
    size_t n = strlen(buf);

    if (n + 1 < cap) {
        snprintf(buf + n, cap - n, "%s", s);
    }
}

/**
 * Append the spelling of a set of qualifiers, each followed by a space.
 *
 * @param buf    the buffer
 * @param cap    its size
 * @param quals  CC_CQ_*
 */
static void ct_cat_quals(char *buf, size_t cap, unsigned quals) {
    if (quals & CC_CQ_CONST) {
        ct_cat(buf, cap, "const ");
    }
    if (quals & CC_CQ_VOLATILE) {
        ct_cat(buf, cap, "volatile ");
    }
    if (quals & CC_CQ_RESTRICT) {
        ct_cat(buf, cap, "restrict ");
    }
    if (quals & CC_CQ_ATOMIC) {
        ct_cat(buf, cap, "_Atomic ");
    }
}

/**
 * Spell a type's specifier part: everything left of the declarator.
 *
 * @param t    a type that is not a pointer, array or function
 * @param buf  receives the spelling
 * @param cap  its size
 */
static void ct_format_base(const cc_ctype_t *t, char *buf, size_t cap) {
    static const char *const names[] = {
        "void", "_Bool", "char", "signed char", "unsigned char", "short", "unsigned short", "int",
        "unsigned int", "long", "unsigned long", "long long", "unsigned long long", "_BitInt",
        "unsigned _BitInt", "_Float16", "float", "double", "long double", "_Decimal32", "_Decimal64",
        "_Decimal128",
    };
    char tmp[64];

    buf[0] = '\0';
    ct_cat_quals(buf, cap, t->quals);
    switch (t->kind) {
    case CC_CT_BITINT:
    case CC_CT_UBITINT:
        snprintf(tmp, sizeof(tmp), "%s(%ld)", names[t->kind], t->count);
        break;
    case CC_CT_COMPLEX:
    case CC_CT_IMAGINARY:
        snprintf(tmp, sizeof(tmp), "%s %s", t->kind == CC_CT_COMPLEX ? "_Complex" : "_Imaginary",
                 names[t->of->kind]);
        break;
    case CC_CT_ENUM:
        snprintf(tmp, sizeof(tmp), "enum #%d", t->id);
        break;
    case CC_CT_RECORD:
        snprintf(tmp, sizeof(tmp), "struct #%d", t->id);
        break;
    default:
        snprintf(tmp, sizeof(tmp), "%s", names[t->kind]);
        break;
    }
    ct_cat(buf, cap, tmp);
}

/**
 * Spell a type around a declarator spelled so far, working outward from
 * the name: a pointer prefixes `*`, an array or function suffixes its
 * bounds or parameters, parenthesizing a pointer that one follows.
 *
 * @param t     the type
 * @param decl  the declarator so far ("" for an abstract one)
 * @param buf   receives the spelling
 * @param cap   its size
 */
static void ct_format_decl(const cc_ctype_t *t, const char *decl, char *buf, size_t cap) {
    char inner[512];
    char tmp[64];
    size_t i;

    inner[0] = '\0';
    switch (t->kind) {
    case CC_CT_POINTER:
        ct_cat(inner, sizeof(inner), "*");
        if (t->quals != 0) {
            char q[64];
            q[0] = '\0';
            ct_cat_quals(q, sizeof(q), t->quals);
            q[strlen(q) - 1] = '\0';
            ct_cat(inner, sizeof(inner), q);
            if (decl[0] != '\0') {
                ct_cat(inner, sizeof(inner), " ");
            }
        }
        ct_cat(inner, sizeof(inner), decl);
        if (t->of->kind == CC_CT_ARRAY || t->of->kind == CC_CT_FUNCTION) {
            char paren[516];
            paren[0] = '\0';
            ct_cat(paren, sizeof(paren), "(");
            ct_cat(paren, sizeof(paren), inner);
            ct_cat(paren, sizeof(paren), ")");
            ct_format_decl(t->of, paren, buf, cap);
        } else {
            ct_format_decl(t->of, inner, buf, cap);
        }
        return;
    case CC_CT_ARRAY:
        ct_cat(inner, sizeof(inner), decl);
        if (t->count < 0) {
            ct_cat(inner, sizeof(inner), "[]");
        } else {
            snprintf(tmp, sizeof(tmp), "[%ld]", t->count);
            ct_cat(inner, sizeof(inner), tmp);
        }
        ct_format_decl(t->of, inner, buf, cap);
        return;
    case CC_CT_FUNCTION:
        ct_cat(inner, sizeof(inner), decl);
        ct_cat(inner, sizeof(inner), "(");
        for (i = 0; i < t->param_count; ++i) {
            char p[256];
            if (i > 0) {
                ct_cat(inner, sizeof(inner), ", ");
            }
            cc_ct_format(t->params[i], p, sizeof(p));
            ct_cat(inner, sizeof(inner), p);
        }
        if (t->variadic) {
            ct_cat(inner, sizeof(inner), t->param_count > 0 ? ", ..." : "...");
        } else if (t->prototyped && t->param_count == 0) {
            ct_cat(inner, sizeof(inner), "void");
        }
        ct_cat(inner, sizeof(inner), ")");
        ct_format_decl(t->of, inner, buf, cap);
        return;
    default:
        ct_format_base(t, buf, cap);
        if (decl[0] != '\0') {
            /* "int *", "int (*)[7]", but "int[7]" and "int(void)", as gcc spells them */
            int tight = decl[0] == '[' || (decl[0] == '(' && decl[1] != '*');
            if (!tight) {
                ct_cat(buf, cap, " ");
            }
            ct_cat(buf, cap, decl);
        }
        return;
    }
}

/**
 * Spell a type as a C type name.
 *
 * @param t    the type
 * @param buf  receives the spelling
 * @param cap  the size of @p buf
 * @return     @p buf
 */
const char *cc_ct_format(const cc_ctype_t *t, char *buf, size_t cap) {
    if (cap == 0) {
        return buf;
    }
    buf[0] = '\0';
    if (t == NULL) {
        snprintf(buf, cap, "<null>");
        return buf;
    }
    ct_format_decl(t, "", buf, cap);
    return buf;
}
