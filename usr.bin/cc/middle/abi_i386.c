/*
 * abi_i386.c: how the i386 System V psABI passes and returns aggregates,
 * as gcc does: on the stack and by hidden pointer, except for vectors,
 * float _Complex and the decimal floating types.
 */

#include "cc_abi.h"
#include "cc_ctype.h"

#include <stddef.h>

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
    if (tu != NULL && sid >= 0 && (size_t)sid < tu->struct_count && tu->structs[sid].decimal_bits > 0) {
        /* _Decimal32 in %eax, _Decimal64 in %eax:%edx, _Decimal128 in memory, as gcc returns them */
        return tu->structs[sid].decimal_bits <= 64 ? CC_ABI_RET_EAX_EDX : 0;
    }
    if (cc_abi_sid_is_vector(tu, sid)) {
        /* a vector of 16 bytes in %xmm0, of 8 in %mm0, of 4 or fewer in %eax, as gcc returns one */
        long vsize = tu->structs[sid].size;

        return vsize == 16 ? CC_ABI_RET_VEC128 : vsize == 8 ? CC_ABI_RET_VEC64 : vsize <= 4 ? CC_ABI_RET_EAX_EDX : 0;
    }
    return 0;
}
