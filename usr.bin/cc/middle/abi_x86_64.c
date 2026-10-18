/*
 * abi_x86_64.c: how the x86-64 System V psABI passes and returns
 * aggregates -- the eightbyte classification of psABI 3.2.3, with gcc's
 * treatment of vectors, decimal floating types and complex long double.
 */

#include "cc_abi.h"
#include "cc_ctype.h"
#include "cc_irbuild.h"

#include <stddef.h>
#include <string.h>

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
    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && cc_struct_soft_float_bits(&tu->structs[sid]) > 0) {
        /*
         * A decimal floating value or a _Float128 is SSE (psABI 3.2.3):
         * the 128-bit ones SSE and SSEUP, whole in one xmm register.
         */
        if (cc_struct_soft_float_bits(&tu->structs[sid]) == 128) {
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

/** @p n rounded up to a multiple of @p a. */
static long va_align_up(long n, long a) {
    return ((n + a - 1) / a) * a;
}

/**
 * Append `lhs op rhs` on words.
 *
 * @return the result, or -1 when out of memory
 */
static int va_binop(cc_ssa_function_t *sf, cc_ssa_opcode_t op, int lhs, int rhs) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.dst = cc_ir_new_value(sf, CC_VAL_I64);
    in.lhs = lhs;
    in.rhs = rhs;
    return in.dst < 0 || cc_ir_push(sf, in) != 0 ? -1 : in.dst;
}

/**
 * Append a load of @p size bytes at @p addr into a new value of class @p vt.
 *
 * @return the value, or -1 when out of memory
 */
static int va_load(cc_ssa_function_t *sf, cc_value_type_t vt, int addr, long size, int is_unsigned) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LOAD;
    in.dst = cc_ir_new_value(sf, vt);
    in.lhs = addr;
    in.imm = size;
    in.is_unsigned = is_unsigned;
    return in.dst < 0 || cc_ir_push(sf, in) != 0 ? -1 : in.dst;
}

/**
 * Append a store of the low @p size bytes of @p v at @p addr.
 *
 * @return 0, or -1 when out of memory
 */
static int va_store(cc_ssa_function_t *sf, int addr, int v, long size) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_STORE;
    in.lhs = addr;
    in.rhs = v;
    in.imm = size;
    return cc_ir_push(sf, in);
}

/**
 * The address of the va_list record's field at @p off.
 *
 * @return the address, or -1 when out of memory
 */
static int va_field(cc_ssa_function_t *sf, int ap, long off) {
    int c = cc_ir_const(sf, off);

    return c < 0 ? -1 : va_binop(sf, CC_SSA_ADD, ap, c);
}

/**
 * va_arg of a long double: always from the overflow area, aligned to 16.
 *
 * @param sf                the function
 * @param overflow_ptr_ptr  the address of the record's overflow_arg_area
 * @param overflow_ptr      its value
 * @param t                 the type read
 * @return                  the value, or -1 when out of memory
 */
static int va_arg_ldouble(cc_ssa_function_t *sf, int overflow_ptr_ptr, int overflow_ptr, const cc_abi_va_type_t *t) {
    int align_add = cc_ir_const(sf, 15);
    int align_mask = cc_ir_const(sf, -16);
    int aligned;
    int v;
    int step;
    int next;

    if (align_add < 0 || align_mask < 0) {
        return -1;
    }
    aligned = va_binop(sf, CC_SSA_ADD, overflow_ptr, align_add);
    aligned = aligned < 0 ? -1 : va_binop(sf, CC_SSA_AND, aligned, align_mask);
    v = aligned < 0 ? -1 : va_load(sf, t->value_kind, aligned, t->size, 0);
    step = v < 0 ? -1 : cc_ir_const(sf, va_align_up(t->size, 8));
    next = step < 0 ? -1 : va_binop(sf, CC_SSA_ADD, aligned, step);
    if (next < 0 || va_store(sf, overflow_ptr_ptr, next, 8) != 0) {
        return -1;
    }
    return v;
}

int cc_abi_x86_64_va_arg(cc_ssa_function_t *sf, int ap, const cc_abi_va_type_t *t, int *next_ap, cc_diag_t *diag) {
    cc_value_type_t dst_ty = t->is_aggregate ? CC_VAL_I64 : t->value_kind;
    long load_size = t->is_aggregate ? 8 : t->size;
    int load_unsigned = t->is_aggregate ? 1 : t->is_unsigned;
    int overflow_ptr_ptr;
    int overflow_ptr;
    int regsave_ptr;
    int off_ptr;
    int offv;
    int off_limit;
    int reg_incv;
    int stack_incv;
    int cmpv;
    int dstv;
    int addrv;
    int v;
    int l_reg;
    int l_stack;
    int l_end;
    cc_ssa_instr_t in;

    *next_ap = -1;
    /* {gp_offset @0, fp_offset @4, overflow_arg_area @8, reg_save_area @16} */
    overflow_ptr_ptr = va_field(sf, ap, 8);
    overflow_ptr = overflow_ptr_ptr < 0 ? -1 : va_load(sf, CC_VAL_I64, overflow_ptr_ptr, 8, 1);
    if (overflow_ptr < 0) {
        goto oom;
    }
    if (t->is_ldouble) {
        v = va_arg_ldouble(sf, overflow_ptr_ptr, overflow_ptr, t);
        if (v < 0) {
            goto oom;
        }
        return v;
    }
    regsave_ptr = va_field(sf, ap, 16);
    regsave_ptr = regsave_ptr < 0 ? -1 : va_load(sf, CC_VAL_I64, regsave_ptr, 8, 1);
    off_ptr = regsave_ptr < 0 ? -1 : va_field(sf, ap, t->is_fp ? 4 : 0);
    offv = off_ptr < 0 ? -1 : va_load(sf, CC_VAL_I64, off_ptr, 4, 1);
    if (offv < 0) {
        goto oom;
    }
    /* the six general registers end at offset 48, the eight xmm ones at 176 */
    off_limit = cc_ir_const(sf, t->is_fp ? 160 : 40);
    reg_incv = cc_ir_const(sf, t->is_fp ? 16 : 8);
    stack_incv = (off_limit < 0 || reg_incv < 0) ? -1 : cc_ir_const(sf, t->is_fp ? 8 : va_align_up(t->size, 8));
    if (stack_incv < 0) {
        goto oom;
    }

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CMP;
    in.cmp_kind = CC_CMP_LE;
    in.is_unsigned = 1;
    in.dst = cc_ir_new_value(sf, CC_VAL_I64);
    in.lhs = offv;
    in.rhs = off_limit;
    if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
        goto oom;
    }
    cmpv = in.dst;
    dstv = cc_ir_new_value(sf, dst_ty);
    addrv = cc_ir_new_value(sf, CC_VAL_I64);
    if (dstv < 0 || addrv < 0) {
        goto oom;
    }
    l_reg = cc_ir_new_label(sf);
    l_stack = cc_ir_new_label(sf);
    l_end = cc_ir_new_label(sf);
    if (cc_ir_br_cond(sf, cmpv, l_reg, l_stack) != 0) {
        goto oom;
    }

    /* from the register save area: advance the offset */
    if (cc_ir_label(sf, l_reg) != 0) {
        goto oom;
    }
    v = va_binop(sf, CC_SSA_ADD, regsave_ptr, offv);
    if (v < 0 || cc_ir_mov(sf, addrv, v) != 0) {
        goto oom;
    }
    v = va_binop(sf, CC_SSA_ADD, offv, reg_incv);
    if (v < 0 || va_store(sf, off_ptr, v, 4) != 0) {
        goto oom;
    }
    v = va_load(sf, dst_ty, addrv, load_size, load_unsigned);
    if (v < 0 || cc_ir_mov(sf, dstv, v) != 0 || cc_ir_br(sf, l_end) != 0) {
        goto oom;
    }

    /* from the overflow area: advance its pointer */
    if (cc_ir_label(sf, l_stack) != 0 || cc_ir_mov(sf, addrv, overflow_ptr) != 0) {
        goto oom;
    }
    v = va_binop(sf, CC_SSA_ADD, overflow_ptr, stack_incv);
    if (v < 0 || va_store(sf, overflow_ptr_ptr, v, 8) != 0) {
        goto oom;
    }
    v = va_load(sf, dst_ty, addrv, load_size, load_unsigned);
    if (v < 0 || cc_ir_mov(sf, dstv, v) != 0 || cc_ir_br(sf, l_end) != 0) {
        goto oom;
    }

    if (cc_ir_label(sf, l_end) != 0) {
        goto oom;
    }
    return dstv;

oom:
    cc_diag_set(diag, "", 0, 0, "out of memory lowering va_arg");
    return -1;
}
