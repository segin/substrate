/*
 * abi_x86_64.c: how the x86-64 System V psABI passes and returns
 * aggregates -- the eightbyte classification of psABI 3.2.3, with gcc's
 * treatment of vectors, decimal floating types and complex long double.
 */

#include "cc_abi.h"
#include "cc_ctype.h"

#include <stddef.h>

/** An eightbyte's x86-64 psABI class (System V psABI 3.2.3), in merge order. */
enum { ABI64_NONE = 0, ABI64_SSE, ABI64_INTEGER, ABI64_X87, ABI64_MEMORY };

/**
 * Merge class @p c into an eightbyte's class: INTEGER beats SSE, and x87
 * with anything else, or MEMORY anywhere, makes it MEMORY.
 */
static void abi64_merge(int *cls, int c) {
    if (*cls == c || c == ABI64_NONE) {
        return;
    }
    if (*cls == ABI64_NONE) {
        *cls = c;
    } else if (*cls == ABI64_MEMORY || c == ABI64_MEMORY || *cls == ABI64_X87 || c == ABI64_X87) {
        *cls = ABI64_MEMORY;
    } else {
        *cls = ABI64_INTEGER;
    }
}

/** Give the eightbytes @p size bytes at @p off cover class @p c. */
static void abi64_cover(int cls[2], long off, long size, int c) {
    long first = off / 8;
    long last = (off + (size > 0 ? size : 1) - 1) / 8;
    long i;

    for (i = first; i <= last; ++i) {
        if (i > 1) {
            abi64_merge(&cls[1], ABI64_MEMORY);
            return;
        }
        abi64_merge(&cls[i], c);
    }
}

static int abi64_classify_record(const cc_translation_unit_t *tu, int sid, long off, int cls[2], int depth);

/** The class of a scalar of type @p t. */
static int abi64_scalar_class(const cc_ctype_t *t) {
    switch (t->unqual->kind) {
    case CC_CT_FLOAT:
    case CC_CT_DOUBLE:
    case CC_CT_FLOAT16:
    case CC_CT_DECIMAL32:
    case CC_CT_DECIMAL64:
    case CC_CT_DECIMAL128:
        return ABI64_SSE;
    case CC_CT_LDOUBLE:
        return ABI64_X87;
    default:
        return ABI64_INTEGER;
    }
}

/**
 * Classify an object of interned type @p ct at offset @p off into the
 * eightbytes @p cls.
 *
 * @return 0, or -1 if the type cannot be classified (then the caller keeps
 *         the INTEGER default)
 */
static int abi64_classify_ctype(const cc_translation_unit_t *tu, const cc_ctype_t *ct, long off, int cls[2],
                                int depth) {
    long size = cc_ct_size(ct, tu);
    long align = cc_ct_align(ct, tu);

    if (depth > 32 || size < 0) {
        return -1;
    }
    if (align > 0 && off % align != 0) {
        /* an unaligned member (a packed structure) puts the whole in memory */
        abi64_cover(cls, off, size, ABI64_MEMORY);
        return 0;
    }
    switch (ct->kind) {
    case CC_CT_FLOAT16:
    case CC_CT_FLOAT:
    case CC_CT_DOUBLE:
    case CC_CT_DECIMAL32:
    case CC_CT_DECIMAL64:
    case CC_CT_DECIMAL128:
        abi64_cover(cls, off, size, ABI64_SSE);
        return 0;
    case CC_CT_LDOUBLE:
        abi64_cover(cls, off, size, ABI64_X87);
        return 0;
    case CC_CT_COMPLEX:
    case CC_CT_IMAGINARY: {
        long es = cc_ct_size(ct->of, tu);

        if (es <= 0 || abi64_classify_ctype(tu, ct->of, off, cls, depth + 1) != 0) {
            return -1;
        }
        return ct->kind == CC_CT_COMPLEX ? abi64_classify_ctype(tu, ct->of, off + es, cls, depth + 1) : 0;
    }
    case CC_CT_RECORD:
        return abi64_classify_record(tu, ct->id, off, cls, depth + 1);
    case CC_CT_ARRAY: {
        long es = cc_ct_size(ct->of, tu);
        long i;

        if (ct->count < 0 || es <= 0) {
            return ct->count <= 0 ? 0 : -1;
        }
        for (i = 0; i < ct->count && off + i * es < 16; ++i) {
            if (abi64_classify_ctype(tu, ct->of, off + i * es, cls, depth + 1) != 0) {
                return -1;
            }
        }
        return 0;
    }
    case CC_CT_FUNCTION:
    case CC_CT_VOID:
        return -1;
    default:
        abi64_cover(cls, off, size, ABI64_INTEGER);
        return 0;
    }
}

/** Classify structure or union @p sid at offset @p off into @p cls; -1 if it cannot be. */
static int abi64_classify_record(const cc_translation_unit_t *tu, int sid, long off, int cls[2], int depth) {
    const cc_struct_def_t *sd;
    size_t i;

    if (tu == NULL || sid < 0 || (size_t)sid >= tu->struct_count || depth > 32) {
        return -1;
    }
    sd = &tu->structs[sid];
    if (sd->complex_elem != NULL) {
        long es = cc_ct_size(sd->complex_elem, tu);

        abi64_cover(cls, off, es, abi64_scalar_class(sd->complex_elem));
        abi64_cover(cls, off + es, es, abi64_scalar_class(sd->complex_elem));
        return 0;
    }
    for (i = 0; i < sd->member_count; ++i) {
        const cc_struct_member_t *m = &sd->members[i];
        long moff = off + m->offset;

        if (m->is_bitfield) {
            if (m->bit_width > 0) {
                abi64_cover(cls, moff, m->bit_storage_size > 0 ? m->bit_storage_size : 1, ABI64_INTEGER);
            }
            continue;
        }
        if (abi64_classify_ctype(tu, m->ctype, moff, cls, depth) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * How an x86-64 aggregate of @p size bytes travels: for one of at most 16
 * bytes that goes in registers, CC_ABI_SSE0 / CC_ABI_SSE1 for its
 * eightbytes that are SSE (none set: all INTEGER, the old behaviour); for
 * a returned one that goes in x87 registers CC_ABI_RET_X87 or
 * CC_ABI_RET_X87_COMPLEX; -1 for one passed in memory, as anything over
 * 16 bytes, containing a long double (as an argument), or packed is.
 *
 * @param tu         the translation unit
 * @param sid        the structure, union or complex type's structure
 * @param size       its size
 * @param is_return  nonzero for a return value
 */
static int abi64_aggregate_class(const cc_translation_unit_t *tu, int sid, long size, int is_return) {
    int cls[2] = {ABI64_NONE, ABI64_NONE};
    int flags = 0;

    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count &&
        tu->structs[sid].complex_elem == cc_ct_basic(CC_CT_LDOUBLE)) {
        return is_return ? CC_ABI_RET_X87_COMPLEX : -1;
    }
    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].decimal_bits > 0) {
        /* a decimal floating value is SSE (psABI 3.2.3): _Decimal128 SSE and SSEUP, whole in one xmm register */
        if (tu->structs[sid].decimal_bits == 128) {
            return CC_ABI_SSE0 | CC_ABI_SSE1 | (is_return ? CC_ABI_RET_VEC128 : CC_ABI_VEC128);
        }
        return CC_ABI_SSE0;
    }
    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].vector_count > 0) {
        /*
         * A vector, by its size as gcc classes it: 16 bytes SSE and SSEUP,
         * whole in one xmm register; 8 bytes SSE; 4 or fewer INTEGER;
         * wider (an AVX vector without AVX) in memory.
         */
        if (size == 16) {
            return CC_ABI_SSE0 | CC_ABI_SSE1 | (is_return ? CC_ABI_RET_VEC128 : CC_ABI_VEC128);
        }
        return size == 8 ? CC_ABI_SSE0 : size < 8 ? 0 : -1;
    }
    if (size > 16 || size <= 0) {
        return -1;
    }
    if (abi64_classify_record(tu, sid, 0, cls, 0) != 0) {
        return 0;
    }
    if (cls[0] == ABI64_MEMORY || cls[1] == ABI64_MEMORY) {
        return -1;
    }
    if (cls[0] == ABI64_X87 || cls[1] == ABI64_X87) {
        /* one long double (X87 then X87UP) is returned in %st0; as an argument it is memory */
        return (is_return && cls[0] == ABI64_X87 && size == 16) ? CC_ABI_RET_X87 : -1;
    }
    if (cls[0] == ABI64_SSE) {
        flags |= CC_ABI_SSE0;
    }
    if (cls[1] == ABI64_SSE && size > 8) {
        flags |= CC_ABI_SSE1;
    }
    return flags;
}

int cc_abi_x86_64_aggregate_arg(const cc_translation_unit_t *tu, int sid, long size, cc_abi_regs_t *regs) {
    int cls;

    (void)regs;
    if (size < 0) {
        return CC_CALL_ARG_ABI_DEFAULT;
    }
    if (size == 0) {
        /* a GNU C empty structure takes no register and no stack */
        return CC_CALL_ARG_ABI_ZERO;
    }
    if (size > 16) {
        return CC_CALL_ARG_ABI_AGG_MEM | cc_abi_stack_vec_flag(tu, sid, size);
    }
    cls = abi64_aggregate_class(tu, sid, size, 0);
    if (cls < 0) {
        /* 16 bytes or fewer, but in memory: a long double in it, or packed */
        return CC_CALL_ARG_ABI_AGG_MEM | cc_abi_stack_vec_flag(tu, sid, size);
    }
    if (size > 8 || cls != 0) {
        /* by its eightbytes' classes, in general and xmm registers */
        return CC_CALL_ARG_ABI_AGG_REG | cls;
    }
    /* one INTEGER eightbyte: as an integer of its size */
    return CC_CALL_ARG_ABI_DEFAULT;
}

int cc_abi_x86_64_aggregate_return(const cc_translation_unit_t *tu, int sid, long size) {
    int c = abi64_aggregate_class(tu, sid, size, 1);

    return c >= 0 ? c : (size <= 16 ? CC_ABI_RET_MEMORY : 0);
}
