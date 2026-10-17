/*
 * irbuild.c: appending instructions to an SSA function (cc_irbuild.h),
 * for lowering and for the per-target passes that write IR.
 */

#include "cc_irbuild.h"
#include "cc_target.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int cc_ir_push(cc_ssa_function_t *f, cc_ssa_instr_t in) {
    if (f->instr_count == f->instr_cap) {
        size_t ncap = f->instr_cap == 0 ? 16 : f->instr_cap * 2;
        cc_ssa_instr_t *next = (cc_ssa_instr_t *)realloc(f->instrs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        f->instrs = next;
        f->instr_cap = ncap;
    }
    f->instrs[f->instr_count++] = in;
    if (in.dst >= 0 && in.dst < f->value_count && cc_ssa_value_class(f, in.dst) == CC_VAL_I64) {
        switch (in.op) {
        case CC_SSA_CONST:
        case CC_SSA_PARAM:
        case CC_SSA_LOAD:
        case CC_SSA_ADD:
        case CC_SSA_SUB:
        case CC_SSA_MUL:
        case CC_SSA_DIV:
        case CC_SSA_AND:
        case CC_SSA_OR:
        case CC_SSA_XOR:
        case CC_SSA_SHL:
        case CC_SSA_SHR:
        case CC_SSA_CALL:
        case CC_SSA_CALLI:
        case CC_SSA_F2I:
            cc_ssa_value_set_unsigned(f, in.dst, in.is_unsigned);
            break;
        case CC_SSA_MOV:
            if (in.lhs >= 0 && in.lhs < f->value_count) {
                cc_ssa_value_set_unsigned(f, in.dst, cc_ssa_value_is_unsigned(f, in.lhs));
            }
            break;
        case CC_SSA_CMP:
        case CC_SSA_GADDR:
        case CC_SSA_LADDR:
        case CC_SSA_ADDR:
        case CC_SSA_STR:
        case CC_SSA_STACKALLOC:
        case CC_SSA_DYNALLOC:
        case CC_SSA_VA_START:
            cc_ssa_value_set_unsigned(f, in.dst, 1);
            break;
        default:
            cc_ssa_value_set_unsigned(f, in.dst, 0);
            break;
        }
    }
    if (in.dst >= 0 && in.dst < f->value_count) {
        switch (in.op) {
        case CC_SSA_PARAM:
        case CC_SSA_LOAD:
        case CC_SSA_CALL:
        case CC_SSA_CALLI:
            cc_ir_set_size(f, in.dst, in.imm > 0 ? in.imm : cc_target_pointer_size());
            break;
        case CC_SSA_MOV:
            if (in.lhs >= 0 && in.lhs < f->value_count) {
                cc_ir_set_size(f, in.dst, cc_ssa_value_size(f, in.lhs));
            }
            break;
        case CC_SSA_CMP:
            cc_ir_set_size(f, in.dst, 4);
            break;
        case CC_SSA_GADDR:
        case CC_SSA_LADDR:
        case CC_SSA_ADDR:
        case CC_SSA_STR:
        case CC_SSA_STACKALLOC:
        case CC_SSA_DYNALLOC:
        case CC_SSA_VA_START:
            cc_ir_set_size(f, in.dst, cc_target_pointer_size());
            break;
        case CC_SSA_CONST:
        case CC_SSA_ADD:
        case CC_SSA_SUB:
        case CC_SSA_MUL:
        case CC_SSA_DIV:
        case CC_SSA_AND:
        case CC_SSA_OR:
        case CC_SSA_XOR:
        case CC_SSA_SHL:
        case CC_SSA_SHR:
        case CC_SSA_F2I:
            break;
        default:
            if (cc_ssa_value_class(f, in.dst) == CC_VAL_F64) {
                cc_ir_set_size(f, in.dst, 8);
            }
            break;
        }
    }
    return 0;
}

int cc_ir_new_value(cc_ssa_function_t *f, cc_value_type_t vt) {
    return cc_ssa_function_new_value(f, vt);
}

void cc_ir_set_size(cc_ssa_function_t *f, int v, long size) {
    if (f == NULL || v < 0 || v >= f->value_count) {
        return;
    }
    if (size <= 0) {
        size = cc_target_pointer_size();
    }
    /* compared unsigned: on a 32-bit host (long)UINT32_MAX is -1, and every size would be clamped */
    if ((unsigned long)size > UINT32_MAX) {
        size = (long)(UINT32_MAX >> 1);
    }
    cc_ssa_value_set_size(f, v, (uint32_t)size);
}

int cc_ir_new_label(cc_ssa_function_t *f) {
    return f->label_count++;
}

int cc_ir_stackalloc(cc_ssa_function_t *sf, long total_size, cc_diag_t *diag) {
    cc_ssa_instr_t in;
    int dst;

    if (total_size <= 0) {
        cc_diag_set(diag, "", 0, 0, "invalid local storage size");
        return -1;
    }
    dst = cc_ir_new_value(sf, CC_VAL_I64);
    if (dst < 0) {
        cc_diag_set(diag, "", 0, 0, "out of memory allocating local storage value");
        return -1;
    }
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_STACKALLOC;
    in.dst = dst;
    in.imm = total_size;
    if (cc_ir_push(sf, in) != 0) {
        cc_diag_set(diag, "", 0, 0, "out of memory appending local storage allocation");
        return -1;
    }
    return dst;
}

int cc_ir_label(cc_ssa_function_t *sf, int label) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LABEL;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.label = label;
    return cc_ir_push(sf, in);
}

int cc_ir_br(cc_ssa_function_t *sf, int label) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_BR;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.label = label;
    return cc_ir_push(sf, in);
}

int cc_ir_br_cond(cc_ssa_function_t *sf, int cond, int label_true, int label_false) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_BR_COND;
    in.dst = -1;
    in.lhs = cond;
    in.rhs = -1;
    in.true_label = label_true;
    in.false_label = label_false;
    return cc_ir_push(sf, in);
}

int cc_ir_trap(cc_ssa_function_t *sf) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_TRAP;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    return cc_ir_push(sf, in);
}

int cc_ir_mov(cc_ssa_function_t *sf, int dst, int src) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_MOV;
    in.dst = dst;
    in.lhs = src;
    in.rhs = -1;
    return cc_ir_push(sf, in);
}

int cc_ir_const(cc_ssa_function_t *sf, long long v) {
    cc_ssa_instr_t in;
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CONST;
    in.dst = cc_ir_new_value(sf, CC_VAL_I64);
    in.lhs = -1;
    in.rhs = -1;
    in.imm = v;
    if (in.dst < 0 || cc_ir_push(sf, in) != 0) {
        return -1;
    }
    return in.dst;
}
