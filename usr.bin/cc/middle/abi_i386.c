/*
 * abi_i386.c: how the i386 System V psABI passes and returns aggregates,
 * as gcc does: on the stack and by hidden pointer, except for vectors,
 * float _Complex and the decimal floating types.
 */

#include "cc_abi.h"
#include "cc_ctype.h"
#include "cc_irbuild.h"
#include "cc_options.h"

static int i386_record_is_float_mode(const cc_translation_unit_t *tu, int sid, long size);

#include <stddef.h>
#include <string.h>

/**
 * How an i386 vector argument or parameter of structure @p sid and
 * @p size bytes travels, as gcc passes one: the first three 16-byte
 * vectors in %xmm0-2 and the first three 8-byte ones in %mm0-2
 * (CC_CALL_ARG_ABI_AGG_REG with CC_ABI_VEC128 or CC_ABI_VEC64); a wider
 * one, or one the registers ran out for, on the stack aligned to its size
 * (CC_ABI_STACK_VEC).  *@p n16 and *@p n8 count the registers taken.
 *
 * @return the ABI byte, or -1 if it travels as any aggregate of its size
 */
static int i386_vector_abi(const cc_translation_unit_t *tu, int sid, long size, int *n16, int *n8) {
    if (!cc_abi_sid_is_vector(tu, sid) || size <= 4) {
        return -1;
    }
    if (size == 16 && *n16 < 3) {
        ++*n16;
        return CC_CALL_ARG_ABI_AGG_REG | CC_ABI_VEC128;
    }
    if (size == 8 && *n8 < 3) {
        ++*n8;
        return CC_CALL_ARG_ABI_AGG_REG | CC_ABI_VEC64;
    }
    return CC_CALL_ARG_ABI_AGG_MEM | (size >= 16 ? CC_ABI_STACK_VEC : 0);
}

int cc_abi_i386_aggregate_arg(const cc_translation_unit_t *tu, int sid, long size, cc_abi_regs_t *regs) {
    int vec = i386_vector_abi(tu, sid, size, &regs->vec16, &regs->vec8);

    if (vec >= 0) {
        /* a vector: in an xmm or MMX register, or on the stack aligned to its size */
        return vec;
    }
    if (size == 0) {
        /* a GNU C empty structure takes no stack */
        return CC_CALL_ARG_ABI_ZERO;
    }
    if (size > 4) {
        /* i386 psABI: a struct argument is copied onto the stack */
        return CC_CALL_ARG_ABI_AGG_MEM | cc_abi_stack_vec_flag(tu, sid, size);
    }
    /* 4 bytes or fewer (or of unknown size): as an integer of its size */
    return CC_CALL_ARG_ABI_DEFAULT;
}

int cc_abi_i386_aggregate_return(const cc_translation_unit_t *tu, int sid, long size) {
    (void)size;
    /* i386 psABI: a float _Complex comes back in %eax:%edx, as gcc has it; any other aggregate in memory */
    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].complex_elem == cc_ct_basic(CC_CT_FLOAT)) {
        return CC_ABI_RET_EAX_EDX;
    }
    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && cc_struct_soft_float_bits(&tu->structs[sid]) > 0) {
        /* _Decimal32 in %eax, _Decimal64 in %eax:%edx, _Decimal128 and _Float128 in memory, as gcc returns them */
        return cc_struct_soft_float_bits(&tu->structs[sid]) <= 64 ? CC_ABI_RET_EAX_EDX : 0;
    }
    if (cc_abi_sid_is_vector(tu, sid)) {
        /* a vector of 16 bytes in %xmm0, of 8 in %mm0, of 4 or fewer in %eax, as gcc returns one */
        long vsize = tu->structs[sid].size;

        return vsize == 16 ? CC_ABI_RET_VEC128 : vsize == 8 ? CC_ABI_RET_VEC64 : vsize <= 4 ? CC_ABI_RET_EAX_EDX : 0;
    }
    if (cc_options_current() != NULL && cc_options_current()->i386_reg_struct_return && tu != NULL && sid >= 0 &&
        (size_t)sid < tu->struct_count && tu->structs[sid].complex_elem == NULL &&
        tu->structs[sid].wide_bits == 0) {
        /*
         * -freg-struct-return (Linux's i386 build): a structure of one
         * float or double comes back in %st0 as that value, one of 1, 2, 4
         * or 8 bytes in %eax or %eax:%edx, as gcc returns them; 3, 5, 6, 7
         * and over 8 bytes still in memory.
         */
        if (i386_record_is_float_mode(tu, sid, size)) {
            return CC_ABI_RET_X87;
        }
        if (size == 1 || size == 2 || size == 4 || size == 8) {
            return CC_ABI_RET_EAX_EDX;
        }
    }
    return 0;
}

int cc_abi_i386_va_arg(cc_ssa_function_t *sf, int ap, const cc_abi_va_type_t *t, int *next_ap, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    long step;
    int v;
    int stepv;

    *next_ap = -1;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LOAD;
    in.lhs = ap;
    in.rhs = -1;
    if (t->is_aggregate) {
        /* an aggregate travels by address: its slot holds a pointer */
        in.dst = cc_ir_new_value(sf, CC_VAL_I64);
        in.imm = 4;
        in.is_unsigned = 1;
        step = 4;
    } else {
        in.dst = cc_ir_new_value(sf, t->value_kind);
        in.imm = t->size;
        in.is_unsigned = t->is_unsigned ? 1 : 0;
        step = ((t->size + 3) / 4) * 4;
    }
    if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
        goto oom;
    }
    v = in.dst;
    stepv = cc_ir_const(sf, step);
    if (stepv < 0) {
        goto oom;
    }
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_ADD;
    in.dst = cc_ir_new_value(sf, CC_VAL_I64);
    in.lhs = ap;
    in.rhs = stepv;
    if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
        goto oom;
    }
    *next_ap = in.dst;
    return v;

oom:
    cc_diag_set(diag, "", 0, 0, "out of memory lowering va_arg");
    return -1;
}

/**
 * Whether structure or union @p sid has a floating mode, as gcc gives one
 * whose bytes are all one float or one double -- `struct { float f; }`,
 * `union { double a, b; }`: gcc passes it as that float, on the stack
 * even under regparm.  (`struct { float a, b; }` is two floats in eight
 * bytes: an integer mode, so registers.)
 */
static int i386_record_is_float_mode(const cc_translation_unit_t *tu, int sid, long size) {
    const cc_struct_def_t *sd;
    size_t k;
    int any = 0;

    if (tu == NULL || sid < 0 || (size_t)sid >= tu->struct_count || (size != 4 && size != 8)) {
        return 0;
    }
    sd = &tu->structs[sid];
    if (sd->vector_count > 0 || sd->complex_elem != NULL || cc_struct_soft_float_bits(sd) > 0) {
        return 0;
    }
    for (k = 0; k < sd->member_count; ++k) {
        const cc_ctype_t *mt = sd->members[k].ctype;
        long ms;

        if (mt == NULL) {
            return 0;
        }
        mt = mt->unqual;
        ms = cc_ct_size(mt, tu);
        if (ms == 0) {
            continue; /* an empty member adds nothing */
        }
        if (ms != size) {
            return 0;
        }
        if (mt->kind == CC_CT_RECORD) {
            if (!i386_record_is_float_mode(tu, mt->id, size)) {
                return 0;
            }
        } else if (!((mt->kind == CC_CT_FLOAT && size == 4) || (mt->kind == CC_CT_DOUBLE && size == 8))) {
            return 0;
        }
        any = 1;
    }
    return any;
}

int cc_abi_i386_regparm_words(const cc_translation_unit_t *tu, const cc_ctype_t *t) {
    long size;

    if (t == NULL) {
        return 0;
    }
    t = t->unqual;
    if (t->kind == CC_CT_ARRAY || t->kind == CC_CT_FUNCTION) {
        return 1; /* adjusted to a pointer */
    }
    size = cc_ct_size(t, tu);
    if (size <= 0) {
        return 0;
    }
    if (cc_ct_is_floating(t) || t->kind == CC_CT_COMPLEX || t->kind == CC_CT_IMAGINARY) {
        return 0;
    }
    if (t->kind == CC_CT_RECORD) {
        const cc_struct_def_t *sd = t->id >= 0 && (size_t)t->id < tu->struct_count ? &tu->structs[t->id] : NULL;

        if (sd == NULL || sd->vector_count > 0 || sd->complex_elem != NULL || cc_struct_soft_float_bits(sd) > 0 ||
            sd->wide_bits > 0 || i386_record_is_float_mode(tu, t->id, size)) {
            return 0;
        }
    }
    return (int)((size + 3) / 4);
}

void cc_abi_i386_regparm_assign(const cc_translation_unit_t *tu, const cc_ctype_t *const *types, size_t n, int nregs,
                                int first, unsigned char *abi) {
    int next = first; /* the next free register: 0 %eax, 1 %edx, 2 %ecx */
    size_t i;

    for (i = 0; i < n && next < nregs; ++i) {
        int words = cc_abi_i386_regparm_words(tu, types[i]);
        int kind = CC_ABI_KIND(abi[i]);

        if (words == 0 || (kind != CC_CALL_ARG_ABI_DEFAULT && kind != CC_CALL_ARG_ABI_AGG_MEM) ||
            (abi[i] & CC_ABI_STACK_VEC) != 0) {
            continue;
        }
        if (next + words > nregs) {
            break; /* it goes on the stack, and so does every argument after it */
        }
        abi[i] = CC_ABI_WITH_REGPARM(abi[i], next + 1);
        next += words;
    }
}

int cc_abi_i386_function_regparm(const cc_translation_unit_t *tu, const char *name, int is_variadic,
                                 int default_regparm) {
    size_t k;

    if (is_variadic) {
        return 0;
    }
    for (k = 0; tu != NULL && name != NULL && k < tu->func_count; ++k) {
        if (tu->funcs[k].attr_regparm > 0 && tu->funcs[k].name != NULL && strcmp(tu->funcs[k].name, name) == 0) {
            return tu->funcs[k].attr_regparm - 1;
        }
    }
    return default_regparm;
}
