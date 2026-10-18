/*
 * x86_64.c: instruction selection for x86-64 (System V AMD64 psABI), from
 * legal IR: argument and parameter locations, calls and returns, and every
 * instruction's code.
 */

#include "cc_asm.h"
#include "cc_backend.h"
#include "cc_diag.h"
#include "cc_options.h"
#include "cc_types.h"
#include "cc_x86.h"

#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** The registers the integer register cache allocates from. */
static const char *const emit_regs64[] = {"%rax", "%rcx", "%rdx", "%rsi", "%rdi", "%r8", "%r9", "%r10", "%r11"};

/**
 * The stack alignment of an x86-64 aggregate argument with ABI byte
 * @p abi and @p size bytes: a vector's is its size from 16 bytes up (one
 * that went to memory, or a 16-byte one the xmm registers ran out for),
 * as gcc places it; a _Complex _Float128's 16; anything else's 8.
 */
static size_t abi64_stack_align(unsigned char abi, long size) {
    if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM && (abi & CC_ABI_STACK_16) != 0) {
        return 16;
    }
    if (size >= 16 && (((CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM) && (abi & CC_ABI_STACK_VEC) != 0) ||
                       ((CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_REG) && (abi & CC_ABI_VEC128) != 0))) {
        return (size_t)size;
    }
    return 8;
}

static abi_loc_t abi64_param_loc(const cc_ssa_function_t *f, int param_index, size_t gpr_start) {
    abi_loc_t loc;
    size_t gpr = gpr_start;
    size_t xmm = 0;
    size_t stack_bytes = 0;
    int i;
    loc.kind = ABI_LOC_STACK;
    loc.index = 0;
    loc.size = 8;
    loc.xmm_index = 0;
    loc.sse = 0;

    for (i = 0; i <= param_index; ++i) {
        int is_ldouble = (f->param_abi != NULL && f->param_abi[i] == CC_CALL_ARG_ABI_LDOUBLE) ? 1 : 0;
        int is_agg_mem = (f->param_abi != NULL && CC_ABI_KIND(f->param_abi[i]) == CC_CALL_ARG_ABI_AGG_MEM) ? 1 : 0;
        int is_agg_reg = (f->param_abi != NULL && CC_ABI_KIND(f->param_abi[i]) == CC_CALL_ARG_ABI_AGG_REG) ? 1 : 0;
        int is_zero = (f->param_abi != NULL && f->param_abi[i] == CC_CALL_ARG_ABI_ZERO) ? 1 : 0;
        cc_value_type_t vt = f->param_types[i];
        if (is_zero) {
            if (i == param_index) {
                loc.kind = ABI_LOC_NONE;
                loc.index = 0;
                loc.size = 0;
                return loc;
            }
            continue;
        }
        if (is_agg_mem) {
            long size = x86_64_param_size_bytes(f, i, 8);
            size_t slot_size = align_up_size((size_t)(size > 0 ? size : 8), 8);
            stack_bytes = align_up_size(stack_bytes, abi64_stack_align(f->param_abi[i], size));
            if (i == param_index) {
                loc.kind = ABI_LOC_STACK;
                loc.index = stack_bytes;
                loc.size = size > 0 ? (size_t)size : 8;
                return loc;
            }
            stack_bytes += slot_size;
            continue;
        }
        if (is_agg_reg) {
            long size = x86_64_param_size_bytes(f, i, 8);
            unsigned sse = abi64_sse_mask(f->param_abi[i]);
            size_t ngpr;
            size_t nxmm;

            abi64_agg_reg_need((size_t)(size > 0 ? size : 8), sse, &ngpr, &nxmm);
            if (gpr + ngpr <= 6 && xmm + nxmm <= 8) {
                if (i == param_index) {
                    loc.kind = ABI_LOC_GPR;
                    loc.index = gpr;
                    loc.size = size > 0 ? (size_t)size : 8;
                    loc.xmm_index = xmm;
                    loc.sse = sse;
                    return loc;
                }
                gpr += ngpr;
                xmm += nxmm;
            } else {
                size_t slot_size = align_up_size((size_t)(size > 0 ? size : 8), 8);
                stack_bytes = align_up_size(stack_bytes, abi64_stack_align(f->param_abi[i], size));
                if (i == param_index) {
                    loc.kind = ABI_LOC_STACK;
                    loc.index = stack_bytes;
                    loc.size = size > 0 ? (size_t)size : 8;
                    return loc;
                }
                stack_bytes += slot_size;
            }
            continue;
        }
        if (is_ldouble) {
            stack_bytes = align_up_size(stack_bytes, 16);
            if (i == param_index) {
                loc.kind = ABI_LOC_STACK;
                loc.index = stack_bytes;
                loc.size = 16;
                return loc;
            }
            stack_bytes += 16;
            continue;
        }
        if (vt == CC_VAL_F64) {
            if (xmm < 8) {
                if (i == param_index) {
                    loc.kind = ABI_LOC_XMM;
                    loc.index = xmm;
                    loc.size = 8;
                    return loc;
                }
                xmm++;
            } else {
                if (i == param_index) {
                    loc.kind = ABI_LOC_STACK;
                    loc.index = stack_bytes;
                    loc.size = 8;
                    return loc;
                }
                stack_bytes += 8;
            }
        } else {
            if (gpr < 6) {
                if (i == param_index) {
                    loc.kind = ABI_LOC_GPR;
                    loc.index = gpr;
                    loc.size = 8;
                    return loc;
                }
                gpr++;
            } else {
                if (i == param_index) {
                    loc.kind = ABI_LOC_STACK;
                    loc.index = stack_bytes;
                    loc.size = 8;
                    return loc;
                }
                stack_bytes += 8;
            }
        }
    }

    return loc;
}

static void abi64_classify_call_args(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, size_t gpr_start,
                                     abi_loc_t *locs, size_t *out_stack_bytes, size_t *out_xmm_regs) {
    size_t gpr = gpr_start;
    size_t xmm = 0;
    size_t stack_bytes = 0;
    size_t i;

    for (i = 0; i < in->arg_count; ++i) {
        cc_value_type_t vt = cc_ssa_value_class(f, in->args[i]);
        if (in->call_arg_abi != NULL && in->call_arg_abi[i] == CC_CALL_ARG_ABI_ZERO) {
            locs[i].kind = ABI_LOC_NONE;
            locs[i].index = 0;
            locs[i].size = 0;
            continue;
        }
        if (call_arg_is_agg_mem_abi(in, i)) {
            long size = call_arg_size_bytes(in, i, 8);
            size_t slot_size = align_up_size((size_t)(size > 0 ? size : 8), 8);
            stack_bytes = align_up_size(stack_bytes, abi64_stack_align(in->call_arg_abi[i], size));
            locs[i].kind = ABI_LOC_STACK;
            locs[i].index = stack_bytes;
            locs[i].size = size > 0 ? (size_t)size : 8;
            stack_bytes += slot_size;
            continue;
        }
        if (call_arg_is_agg_reg_abi(in, i)) {
            long size = call_arg_size_bytes(in, i, 8);
            unsigned sse = abi64_sse_mask(in->call_arg_abi[i]);
            size_t ngpr;
            size_t nxmm;

            abi64_agg_reg_need((size_t)(size > 0 ? size : 8), sse, &ngpr, &nxmm);
            locs[i].xmm_index = 0;
            locs[i].sse = 0;
            if (gpr + ngpr <= 6 && xmm + nxmm <= 8) {
                locs[i].kind = ABI_LOC_GPR;
                locs[i].index = gpr;
                locs[i].size = size > 0 ? (size_t)size : 8;
                locs[i].xmm_index = xmm;
                locs[i].sse = sse;
                gpr += ngpr;
                xmm += nxmm;
            } else {
                size_t slot_size = align_up_size((size_t)(size > 0 ? size : 8), 8);
                stack_bytes = align_up_size(stack_bytes, abi64_stack_align(in->call_arg_abi[i], size));
                locs[i].kind = ABI_LOC_STACK;
                locs[i].index = stack_bytes;
                locs[i].size = size > 0 ? (size_t)size : 8;
                stack_bytes += slot_size;
            }
            continue;
        }
        if (call_arg_is_ldouble_abi(in, i)) {
            stack_bytes = align_up_size(stack_bytes, 16);
            locs[i].kind = ABI_LOC_STACK;
            locs[i].index = stack_bytes;
            locs[i].size = 16;
            stack_bytes += 16;
            continue;
        }
        if (vt == CC_VAL_F64 && xmm < 8) {
            locs[i].kind = ABI_LOC_XMM;
            locs[i].index = xmm++;
            locs[i].size = 8;
            continue;
        }
        if (vt != CC_VAL_F64 && gpr < 6) {
            locs[i].kind = ABI_LOC_GPR;
            locs[i].index = gpr++;
            locs[i].size = 8;
            continue;
        }
        locs[i].kind = ABI_LOC_STACK;
        locs[i].index = stack_bytes;
        locs[i].size = 8;
        stack_bytes += 8;
    }

    *out_stack_bytes = stack_bytes;
    *out_xmm_regs = xmm;
}

static void abi64_fixed_usage(const cc_ssa_function_t *f, int fixed_count, size_t gpr_start, size_t *out_gpr, size_t *out_xmm,
                              size_t *out_stack_bytes) {
    size_t gpr = gpr_start;
    size_t xmm = 0;
    size_t stack_bytes = 0;
    int i;
    if (fixed_count < 0) {
        fixed_count = 0;
    }
    if ((size_t)fixed_count > f->param_count) {
        fixed_count = (int)f->param_count;
    }
    for (i = 0; i < fixed_count; ++i) {
        abi_loc_t loc = abi64_param_loc(f, i, gpr_start);
        if (loc.kind == ABI_LOC_GPR) {
            size_t ngpr;
            size_t nxmm;

            abi64_agg_reg_need(loc.size, loc.sse, &ngpr, &nxmm);
            gpr += ngpr;
            xmm += nxmm;
        } else if (loc.kind == ABI_LOC_XMM) {
            xmm++;
        } else {
            size_t end = loc.index + loc.size;
            if (end > stack_bytes) {
                stack_bytes = end;
            }
        }
    }
    if (out_gpr != NULL) {
        *out_gpr = gpr;
    }
    if (out_xmm != NULL) {
        *out_xmm = xmm;
    }
    if (out_stack_bytes != NULL) {
        *out_stack_bytes = stack_bytes;
    }
}

static const char *x86_64_fp_tmp_reg(void) {
    return "%xmm15";
}

static int emit_x86_64_float_binop(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                    const cc_ssa_instr_t *in, cc_diag_t *diag) {
    const char *op = NULL;
    const char *tmp = x86_64_fp_tmp_reg();

    if (value_size_bytes(f, in->dst, 8) >= 16 || value_size_bytes(f, in->lhs, 8) >= 16 ||
        value_size_bytes(f, in->rhs, 8) >= 16) {
        if (in->op == CC_SSA_ADD) op = "faddp";
        else if (in->op == CC_SSA_SUB) op = "fsubrp";
        else if (in->op == CC_SSA_MUL) op = "fmulp";
        else if (in->op == CC_SSA_DIV) op = "fdivrp";
        else {
            set_diag(diag, "invalid long double arithmetic opcode");
            return -1;
        }
        if (value_size_bytes(f, in->lhs, 8) >= 16) fprintf(fp, "\tfldt %d(%%rbp)\n", slot_off(lay, in->lhs));
        else fprintf(fp, "\tfldl %d(%%rbp)\n", slot_off(lay, in->lhs));
        if (value_size_bytes(f, in->rhs, 8) >= 16) fprintf(fp, "\tfldt %d(%%rbp)\n", slot_off(lay, in->rhs));
        else fprintf(fp, "\tfldl %d(%%rbp)\n", slot_off(lay, in->rhs));
        fprintf(fp, "\t%s %%st, %%st(1)\n", op);
        fprintf(fp, "\tfstpt %d(%%rbp)\n", slot_off(lay, in->dst));
        return 0;
    }

    if (in->op == CC_SSA_ADD) op = "addsd";
    else if (in->op == CC_SSA_SUB) op = "subsd";
    else if (in->op == CC_SSA_MUL) op = "mulsd";
    else if (in->op == CC_SSA_DIV) op = "divsd";
    else {
        set_diag(diag, "invalid floating arithmetic opcode");
        return -1;
    }

    fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, in->lhs), tmp);
    fprintf(fp, "\t%s %d(%%rbp), %s\n", op, slot_off(lay, in->rhs), tmp);
    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", tmp, slot_off(lay, in->dst));
    return 0;
}

static void emit_x86_64_mov_fp(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    const char *tmp = x86_64_fp_tmp_reg();
    if (value_size_bytes(f, in->dst, 8) >= 16 || value_size_bytes(f, in->lhs, 8) >= 16) {
        if (value_size_bytes(f, in->lhs, 8) >= 16) fprintf(fp, "\tfldt %d(%%rbp)\n", slot_off(lay, in->lhs));
        else fprintf(fp, "\tfldl %d(%%rbp)\n", slot_off(lay, in->lhs));
        fprintf(fp, "\tfstpt %d(%%rbp)\n", slot_off(lay, in->dst));
        return;
    }
    fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, in->lhs), tmp);
    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", tmp, slot_off(lay, in->dst));
}

static void emit_x86_64_load_fp_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                         int_reg_state_t *ist, const cc_ssa_instr_t *in) {
    long mem_size = in->imm > 0 ? in->imm : 8;
    int rp = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    const char *tmp = x86_64_fp_tmp_reg();

    if (mem_size == 2) {
        int_regs_flush(fp, f, lay, ist);
        fprintf(fp, "\tmovzwl (%s), %%eax\n", ist->regs[rp]);
        fprintf(fp, "\tmovd %%eax, %%xmm0\n");
        fprintf(fp, "\tcall __extendhfdf2\n");
        fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", slot_off(lay, in->dst));
        return;
    } else if (mem_size == 4) {
        fprintf(fp, "\tmovss (%s), %s\n", ist->regs[rp], tmp);
        fprintf(fp, "\tcvtss2sd %s, %s\n", tmp, tmp);
    } else if (mem_size >= 16) {
        fprintf(fp, "\tfldt (%s)\n", ist->regs[rp]);
        if (value_size_bytes(f, in->dst, 8) >= 16) fprintf(fp, "\tfstpt %d(%%rbp)\n", slot_off(lay, in->dst));
        else fprintf(fp, "\tfstpl %d(%%rbp)\n", slot_off(lay, in->dst));
        return;
    } else {
        fprintf(fp, "\tmovsd (%s), %s\n", ist->regs[rp], tmp);
    }
    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", tmp, slot_off(lay, in->dst));
}

static void emit_x86_64_store_fp_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                          int_reg_state_t *ist, const cc_ssa_instr_t *in, long mem_size) {
    int rp = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    const char *tmp = x86_64_fp_tmp_reg();

    if (mem_size == 2) {
        int_regs_flush(fp, f, lay, ist);
        fprintf(fp, "\tsubq $16, %%rsp\n");
        fprintf(fp, "\tmovq %s, 8(%%rsp)\n", ist->regs[rp]);
        fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", slot_off(lay, in->rhs));
        fprintf(fp, "\tcall __truncdfhf2\n");
        fprintf(fp, "\tmovq 8(%%rsp), %%rdx\n");
        fprintf(fp, "\taddq $16, %%rsp\n");
        fprintf(fp, "\tmovd %%xmm0, %%eax\n");
        fprintf(fp, "\tmovw %%ax, (%%rdx)\n");
    } else if (value_size_bytes(f, in->rhs, 8) >= 16 && mem_size < 16) {
        /* a long double stored to a double or float object is rounded to it */
        fprintf(fp, "\tfldt %d(%%rbp)\n", slot_off(lay, in->rhs));
        fprintf(fp, "\t%s (%s)\n", mem_size == 4 ? "fstps" : "fstpl", ist->regs[rp]);
    } else {
        fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, in->rhs), tmp);
        if (mem_size == 4) {
            fprintf(fp, "\tcvtsd2ss %s, %s\n", tmp, tmp);
            fprintf(fp, "\tmovss %s, (%s)\n", tmp, ist->regs[rp]);
        } else if (mem_size >= 16) {
            fprintf(fp, "\tfldt %d(%%rbp)\n", slot_off(lay, in->rhs));
            fprintf(fp, "\tfstpt (%s)\n", ist->regs[rp]);
        } else {
            fprintf(fp, "\tmovsd %s, (%s)\n", tmp, ist->regs[rp]);
        }
    }
}

static void emit_x86_64_i2f(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                            const cc_ssa_instr_t *in) {
    const char *tmp = x86_64_fp_tmp_reg();
    if (value_size_bytes(f, in->dst, 8) >= 16) {
        int dst_off = slot_off(lay, in->dst);
        if (in->is_unsigned) {
            unsigned long lid = next_aux_label_id();
            int_regs_flush(fp, f, lay, ist);
            int_regs_clobber_reg(fp, f, lay, ist, int_reg_index(ist, "%rax"));
            int_regs_clobber_reg(fp, f, lay, ist, int_reg_index(ist, "%rcx"));
            emit_x86_64_load_int_value_to_reg(fp, f, lay, in->lhs, "%rax");
            fprintf(fp, "\ttestq %%rax, %%rax\n");
            fprintf(fp, "\tjns .L__cc_u64told_small_%lu\n", lid);
            fprintf(fp, "\tmovq %%rax, %%rcx\n");
            fprintf(fp, "\tshrq $1, %%rcx\n");
            fprintf(fp, "\tandq $1, %%rax\n");
            fprintf(fp, "\torq %%rax, %%rcx\n");
            fprintf(fp, "\tmovq %%rcx, %d(%%rbp)\n", dst_off);
            fprintf(fp, "\tfildll %d(%%rbp)\n", dst_off);
            fprintf(fp, "\tfadd %%st(0), %%st\n");
            fprintf(fp, "\tfstpt %d(%%rbp)\n", dst_off);
            fprintf(fp, "\tjmp .L__cc_u64told_done_%lu\n", lid);
            fprintf(fp, ".L__cc_u64told_small_%lu:\n", lid);
            fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", dst_off);
            fprintf(fp, "\tfildll %d(%%rbp)\n", dst_off);
            fprintf(fp, "\tfstpt %d(%%rbp)\n", dst_off);
            fprintf(fp, ".L__cc_u64told_done_%lu:\n", lid);
        } else {
            int rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
            fprintf(fp, "\tmovq %s, %d(%%rbp)\n", ist->regs[rl], dst_off);
            fprintf(fp, "\tfildll %d(%%rbp)\n", dst_off);
            fprintf(fp, "\tfstpt %d(%%rbp)\n", dst_off);
        }
        return;
    }
    if (in->is_unsigned) {
        unsigned long lid = next_aux_label_id();
        int rax = int_reg_index(ist, "%rax");
        int rcx = int_reg_index(ist, "%rcx");

        int_regs_flush(fp, f, lay, ist);
        int_regs_clobber_reg(fp, f, lay, ist, rax);
        int_regs_clobber_reg(fp, f, lay, ist, rcx);
        fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(lay, in->lhs));
        fprintf(fp, "\ttestq %%rax, %%rax\n");
        fprintf(fp, "\tjns .L__cc_u64tof_small_%lu\n", lid);
        fprintf(fp, "\tmovq %%rax, %%rcx\n");
        fprintf(fp, "\tshrq $1, %%rcx\n");
        fprintf(fp, "\tandq $1, %%rax\n");
        fprintf(fp, "\torq %%rax, %%rcx\n");
        fprintf(fp, "\tcvtsi2sdq %%rcx, %s\n", tmp);
        fprintf(fp, "\taddsd %s, %s\n", tmp, tmp);
        fprintf(fp, "\tjmp .L__cc_u64tof_done_%lu\n", lid);
        fprintf(fp, ".L__cc_u64tof_small_%lu:\n", lid);
        fprintf(fp, "\tcvtsi2sdq %%rax, %s\n", tmp);
        fprintf(fp, ".L__cc_u64tof_done_%lu:\n", lid);
    } else {
        int rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
        fprintf(fp, "\tcvtsi2sdq %s, %s\n", ist->regs[rl], tmp);
    }
    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", tmp, slot_off(lay, in->dst));
}

/**
 * Round a long double value to double.
 *
 * The x86-64 back end keeps a 16-byte long double value in its slot as
 * x87 extended precision, so there this is an x87 load and a double
 * store.  The i386 back end computes long double in double and converts
 * only where it loads or stores memory, so its slots already hold the
 * double and this is a copy.  A source that is a double already --
 * lowering asks for the rounding without always knowing -- is copied.
 *
 * @param fp            the assembly output
 * @param f             the function, for the source value's size
 * @param lay           its slot layout
 * @param in            the CC_SSA_FROUND64 instruction
 * @param frame         the frame register: "%rbp" or "%ebp"
 * @param x87_in_slots  whether a 16-byte value's slot holds x87 extended
 *                      precision (x86-64) rather than a double (i386)
 */
static void emit_fround64(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, const cc_ssa_instr_t *in,
                          const char *frame, int x87_in_slots) {
    int extended = x87_in_slots && value_size_bytes(f, in->lhs, 8) >= 16;

    fprintf(fp, "\t%s %d(%s)\n", extended ? "fldt" : "fldl", slot_off(lay, in->lhs), frame);
    fprintf(fp, "\tfstpl %d(%s)\n", slot_off(lay, in->dst), frame);
}

static void emit_x86_64_fround32(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    const char *tmp = x86_64_fp_tmp_reg();
    fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, in->lhs), tmp);
    fprintf(fp, "\tcvtsd2ss %s, %s\n", tmp, tmp);
    fprintf(fp, "\tcvtss2sd %s, %s\n", tmp, tmp);
    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", tmp, slot_off(lay, in->dst));
}

static void emit_x86_64_va_start(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                 const cc_ssa_instr_t *in, int va_state_off, int va_regsave_off, int gpr_bias) {
    size_t gpr_used = 0;
    size_t xmm_used = 0;
    size_t stack_bytes = 0;
    long gp_offset;
    long fp_offset;
    long overflow_off;
    int rd;
    int_regs_flush(fp, f, lay, ist);
    rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    if (f->is_variadic) {
        abi64_fixed_usage(f, (int)in->imm, (size_t)(gpr_bias > 0 ? gpr_bias : 0), &gpr_used, &xmm_used, &stack_bytes);
        gp_offset = (long)(gpr_used * 8);
        fp_offset = 48 + (long)(xmm_used * 16);
        overflow_off = 16 + (long)stack_bytes;

        fprintf(fp, "\tmovl $%ld, %d(%%rbp)\n", gp_offset, va_state_off);
        fprintf(fp, "\tmovl $%ld, %d(%%rbp)\n", fp_offset, va_state_off + 4);
        fprintf(fp, "\tleaq %ld(%%rbp), %%rax\n", overflow_off);
        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", va_state_off + 8);
        fprintf(fp, "\tleaq %d(%%rbp), %%rax\n", va_regsave_off);
        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", va_state_off + 16);
        fprintf(fp, "\tleaq %d(%%rbp), %s\n", va_state_off, ist->regs[rd]);
    } else {
        int poff = 16 + (int)(in->imm * 8);
        fprintf(fp, "\tleaq %d(%%rbp), %s\n", poff, ist->regs[rd]);
    }
}

static int emit_x86_64_cmp_float(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                 const cc_ssa_instr_t *in) {
    const char *tmp = x86_64_fp_tmp_reg();
    int rd;
    int_regs_flush(fp, f, lay, ist);
    if (value_size_bytes(f, in->lhs, 8) >= 16 || value_size_bytes(f, in->rhs, 8) >= 16) {
        int_regs_clobber_reg(fp, f, lay, ist, int_reg_index(ist, "%rax"));
        if (value_size_bytes(f, in->rhs, 8) >= 16) fprintf(fp, "\tfldt %d(%%rbp)\n", slot_off(lay, in->rhs));
        else fprintf(fp, "\tfldl %d(%%rbp)\n", slot_off(lay, in->rhs));
        if (value_size_bytes(f, in->lhs, 8) >= 16) fprintf(fp, "\tfldt %d(%%rbp)\n", slot_off(lay, in->lhs));
        else fprintf(fp, "\tfldl %d(%%rbp)\n", slot_off(lay, in->lhs));
        fprintf(fp, "\tfucompp\n");
        fprintf(fp, "\tfnstsw %%ax\n");
        fprintf(fp, "\tsahf\n");
    } else {
        fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, in->lhs), tmp);
        fprintf(fp, "\tucomisd %d(%%rbp), %s\n", slot_off(lay, in->rhs), tmp);
    }
    rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    emit_float_setcc_to_reg(fp, in->cmp_kind, ist->regs[rd], 1);
    return 0;
}

static int emit_x86_64_cmp_int(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                               const cc_ssa_instr_t *in) {
    const char *m = setcc_int_mnemonic(in->cmp_kind, in->is_unsigned);
    int rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    int rr = int_regs_load(fp, f, lay, ist, in->rhs, -1, rl);
    int rd;
    fprintf(fp, "\tcmpq %s, %s\n", ist->regs[rr], ist->regs[rl]);
    rd = int_regs_define(fp, f, lay, ist, in->dst, rl, rr);
    fprintf(fp, "\t%s %s\n", m, reg64_to8(ist->regs[rd]));
    emit_setcc_zext_to_reg(fp, ist->regs[rd], 1);
    return 0;
}

static int emit_x86_64_cmp(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                           const cc_ssa_instr_t *in) {
    if (cc_ssa_value_class(f, in->lhs) == CC_VAL_F64 || cc_ssa_value_class(f, in->rhs) == CC_VAL_F64) {
        return emit_x86_64_cmp_float(fp, f, lay, ist, in);
    } else {
        return emit_x86_64_cmp_int(fp, f, lay, ist, in);
    }
}

static int emit_x86_64_store_value_to_rsp(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                          int value, size_t off_rsp, cc_diag_t *diag) {
    int off;

    if (value < 0 || value >= f->value_count) {
        set_diag(diag, "call argument value out of range");
        return -1;
    }
    if (cc_ssa_value_class(f, value) == CC_VAL_F64) {
        fprintf(fp, "\tmovsd %d(%%rbp), %%xmm15\n", slot_off(lay, value));
        fprintf(fp, "\tmovsd %%xmm15, %zu(%%rsp)\n", off_rsp);
    } else {
        if (is_stackalloc_value(lay, value)) {
            off = stackalloc_off(lay, value);
            fprintf(fp, "\tleaq %d(%%rbp), %%r11\n", off);
        } else {
            fprintf(fp, "\tmovq %d(%%rbp), %%r11\n", slot_off(lay, value));
        }
        fprintf(fp, "\tmovq %%r11, %zu(%%rsp)\n", off_rsp);
    }
    return 0;
}

static int emit_x86_64_store_ldouble_to_rsp(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                            int value, size_t off_rsp, cc_diag_t *diag) {
    if (value < 0 || value >= f->value_count) {
        set_diag(diag, "call argument value out of range");
        return -1;
    }
    if (cc_ssa_value_class(f, value) != CC_VAL_F64) {
        set_diag(diag, "internal error: non-float assigned to long double argument");
        return -1;
    }
    if (value_size_bytes(f, value, 8) >= 16) {
        fprintf(fp, "\tfldt %d(%%rbp)\n", slot_off(lay, value));
    } else {
        fprintf(fp, "\tmovsd %d(%%rbp), %%xmm15\n", slot_off(lay, value));
        fprintf(fp, "\tmovsd %%xmm15, %zu(%%rsp)\n", off_rsp);
        fprintf(fp, "\tfldl %zu(%%rsp)\n", off_rsp);
    }
    fprintf(fp, "\tfstpt %zu(%%rsp)\n", off_rsp);
    /*
     * Keep the upper 2 padding bytes deterministic. Use byte stores instead
     * of a 16-bit immediate store to avoid assembler-width ambiguities.
     */
    fprintf(fp, "\tmovb $0, %zu(%%rsp)\n", off_rsp + 10);
    fprintf(fp, "\tmovb $0, %zu(%%rsp)\n", off_rsp + 11);
    return 0;
}

static int emit_x86_64_store_agg_mem_to_rsp(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int value,
                                            size_t off_rsp, size_t size, cc_diag_t *diag) {
    size_t off = 0;

    if (value < 0 || value >= f->value_count) {
        set_diag(diag, "call aggregate argument value out of range");
        return -1;
    }
    emit_x86_64_load_int_value_to_reg(fp, f, lay, value, "%r11");
    while (off + 8 <= size) {
        fprintf(fp, "\tmovq %zu(%%r11), %%r10\n", off);
        fprintf(fp, "\tmovq %%r10, %zu(%%rsp)\n", off_rsp + off);
        off += 8;
    }
    if (off + 4 <= size) {
        fprintf(fp, "\tmovl %zu(%%r11), %%r10d\n", off);
        fprintf(fp, "\tmovl %%r10d, %zu(%%rsp)\n", off_rsp + off);
        off += 4;
    }
    if (off + 2 <= size) {
        fprintf(fp, "\tmovw %zu(%%r11), %%r10w\n", off);
        fprintf(fp, "\tmovw %%r10w, %zu(%%rsp)\n", off_rsp + off);
        off += 2;
    }
    if (off < size) {
        fprintf(fp, "\tmovb %zu(%%r11), %%r10b\n", off);
        fprintf(fp, "\tmovb %%r10b, %zu(%%rsp)\n", off_rsp + off);
    }
    return 0;
}

/**
 * Load the @p bytes (1 to 8) bytes at @p off(%r11) into general register
 * @p reg, the first in its low byte -- exactly those bytes: an odd-sized
 * tail is gathered a byte at a time rather than read past the object.
 */
static void emit_x86_64_load_agg_part_to_reg(FILE *fp, size_t off, size_t bytes, const char *reg) {
    if (bytes >= 8) {
        fprintf(fp, "\tmovq %zu(%%r11), %s\n", off, reg);
    } else if (bytes == 4) {
        fprintf(fp, "\tmovl %zu(%%r11), %s\n", off, reg64_to32(reg));
    } else if (bytes == 2) {
        fprintf(fp, "\tmovzwl %zu(%%r11), %s\n", off, reg64_to32(reg));
    } else if (bytes == 1) {
        fprintf(fp, "\tmovzbl %zu(%%r11), %s\n", off, reg64_to32(reg));
    } else if (bytes > 0) {
        size_t k = bytes;

        fprintf(fp, "\tmovzbl %zu(%%r11), %s\n", off + k - 1, reg64_to32(reg));
        while (--k > 0) {
            fprintf(fp, "\tshlq $8, %s\n", reg);
            fprintf(fp, "\tmovb %zu(%%r11), %s\n", off + k - 1, reg64_to8(reg));
        }
    }
}

/** Load the 4 or 8 bytes at @p off(%r11) into xmm register @p reg. */
static void emit_x86_64_load_agg_part_to_xmm(FILE *fp, size_t off, size_t bytes, const char *reg) {
    fprintf(fp, "\t%s %zu(%%r11), %s\n", bytes > 4 ? "movsd" : "movss", off, reg);
}

/**
 * Store the low @p bytes (1 to 8) bytes of general register @p reg at
 * @p base_off + @p off (%rbp) -- exactly those: an odd-sized tail a byte
 * at a time.  @p reg is shifted when it is.
 */
static void emit_x86_64_store_agg_part_from_reg(FILE *fp, const char *reg, int base_off, size_t off, size_t bytes) {
    if (bytes >= 8) {
        fprintf(fp, "\tmovq %s, %d(%%rbp)\n", reg, base_off + (int)off);
    } else if (bytes == 4) {
        fprintf(fp, "\tmovl %s, %d(%%rbp)\n", reg64_to32(reg), base_off + (int)off);
    } else if (bytes == 2) {
        fprintf(fp, "\tmovw %s, %d(%%rbp)\n", reg64_to16(reg), base_off + (int)off);
    } else if (bytes > 0) {
        size_t k;

        for (k = 0; k < bytes; ++k) {
            if (k > 0) {
                fprintf(fp, "\tshrq $8, %s\n", reg);
            }
            fprintf(fp, "\tmovb %s, %d(%%rbp)\n", reg64_to8(reg), base_off + (int)(off + k));
        }
    }
}

/** Store the low 4 or 8 bytes of xmm register @p reg at @p base_off + @p off (%rbp). */
static void emit_x86_64_store_agg_part_from_xmm(FILE *fp, const char *reg, int base_off, size_t off, size_t bytes) {
    fprintf(fp, "\t%s %s, %d(%%rbp)\n", bytes > 4 ? "movsd" : "movss", reg, base_off + (int)off);
}

static int emit_x86_64_move_value_to_abi_loc(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                             const cc_ssa_instr_t *in, size_t arg_index, int value,
                                             const abi_loc_t *loc, cc_diag_t *diag) {
    if (value < 0 || value >= f->value_count) {
        set_diag(diag, "call argument value out of range");
        return -1;
    }
    if (loc->kind == ABI_LOC_NONE) {
        return 0;
    }
    if (loc->kind == ABI_LOC_XMM) {
        const char *reg = arg_reg64_xmm(loc->index);
        if (reg == NULL) {
            set_diag(diag, "call with unsupported floating argument index");
            return -1;
        }
        if (cc_ssa_value_class(f, value) != CC_VAL_F64) {
            set_diag(diag, "internal error: non-float assigned to XMM argument");
            return -1;
        }
        fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, value), reg);
        if (in->call_arg_abi != NULL && in->call_arg_abi[arg_index] == CC_CALL_ARG_ABI_FLOAT) {
            /* a float parameter takes single precision */
            fprintf(fp, "\tcvtsd2ss %s, %s\n", reg, reg);
        }
        return 0;
    }
    if (loc->kind == ABI_LOC_GPR) {
        const char *reg = arg_reg64_gpr(loc->index);
        int off;
        if (reg == NULL && !call_arg_is_agg_reg_abi(in, arg_index)) {
            set_diag(diag, "call with unsupported integer argument index");
            return -1;
        }
        if (call_arg_is_agg_reg_abi(in, arg_index)) {
            /* each eightbyte to the next general or xmm register its class names */
            size_t size = loc->size > 0 ? loc->size : 8;
            size_t gi = loc->index;
            size_t xi = loc->xmm_index;
            size_t k;

            emit_x86_64_load_int_value_to_reg(fp, f, lay, value, "%r11");
            if ((loc->sse & ABI_SSE_WHOLE) != 0) {
                const char *r = arg_reg64_xmm(xi);

                if (r == NULL) {
                    set_diag(diag, "call with unsupported vector argument register index");
                    return -1;
                }
                fprintf(fp, "\tmovups 0(%%r11), %s\n", r);
                return 0;
            }
            for (k = 0; k * 8 < size && k < 2; ++k) {
                size_t part = size - k * 8 > 8 ? 8 : size - k * 8;
                const char *r = (loc->sse & (1u << k)) != 0 ? arg_reg64_xmm(xi++) : arg_reg64_gpr(gi++);

                if (r == NULL) {
                    set_diag(diag, "call with unsupported aggregate argument register index");
                    return -1;
                }
                if ((loc->sse & (1u << k)) != 0) {
                    emit_x86_64_load_agg_part_to_xmm(fp, k * 8, part, r);
                } else {
                    emit_x86_64_load_agg_part_to_reg(fp, k * 8, part, r);
                }
            }
            return 0;
        }
        if (cc_ssa_value_class(f, value) == CC_VAL_F64) {
            set_diag(diag, "internal error: float assigned to integer argument register");
            return -1;
        }
        if (is_stackalloc_value(lay, value)) {
            off = stackalloc_off(lay, value);
            fprintf(fp, "\tleaq %d(%%rbp), %s\n", off, reg);
        } else {
            fprintf(fp, "\tmovq %d(%%rbp), %s\n", slot_off(lay, value), reg);
        }
        return 0;
    }
    if (loc->kind == ABI_LOC_STACK) {
        if (call_arg_is_agg_mem_abi(in, arg_index) || call_arg_is_agg_reg_abi(in, arg_index)) {
            return emit_x86_64_store_agg_mem_to_rsp(fp, f, lay, value, loc->index, loc->size, diag);
        }
        if (call_arg_is_ldouble_abi(in, arg_index)) {
            return emit_x86_64_store_ldouble_to_rsp(fp, f, lay, value, loc->index, diag);
        }
        if (in->call_arg_abi != NULL && in->call_arg_abi[arg_index] == CC_CALL_ARG_ABI_FLOAT) {
            fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, value), x86_64_fp_tmp_reg());
            fprintf(fp, "\tcvtsd2ss %s, %s\n", x86_64_fp_tmp_reg(), x86_64_fp_tmp_reg());
            fprintf(fp, "\tmovss %s, %zu(%%rsp)\n", x86_64_fp_tmp_reg(), loc->index);
            return 0;
        }
        return emit_x86_64_store_value_to_rsp(fp, f, lay, value, loc->index, diag);
    }
    set_diag(diag, "internal error: unknown ABI argument location");
    return -1;
}

static void emit_x86_64_normalize_int_return(FILE *fp, long ret_bytes, int is_unsigned) {
    if (ret_bytes == 1) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzbl %%al, %%eax\n");
        } else {
            fprintf(fp, "\tmovsbq %%al, %%rax\n");
        }
    } else if (ret_bytes == 2) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzwl %%ax, %%eax\n");
        } else {
            fprintf(fp, "\tmovswq %%ax, %%rax\n");
        }
    } else if (ret_bytes == 4) {
        if (is_unsigned) {
            fprintf(fp, "\tmovl %%eax, %%eax\n");
        } else {
            fprintf(fp, "\tmovslq %%eax, %%rax\n");
        }
    }
}

static void emit_x86_64_normalize_int_param_reg(FILE *fp, const char *reg, long bytes, int is_unsigned) {
    if (bytes == 1) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzbl %s, %s\n", reg64_to8(reg), reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovsbq %s, %s\n", reg64_to8(reg), reg);
        }
    } else if (bytes == 2) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzwl %s, %s\n", reg64_to16(reg), reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovswq %s, %s\n", reg64_to16(reg), reg);
        }
    } else if (bytes == 4) {
        if (is_unsigned) {
            fprintf(fp, "\tmovl %s, %s\n", reg64_to32(reg), reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovslq %s, %s\n", reg64_to32(reg), reg);
        }
    }
}

static void emit_x86_64_load_int_param_from_stack(FILE *fp, const char *reg, int poff, long bytes, int is_unsigned) {
    if (bytes == 1) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzbl %d(%%rbp), %s\n", poff, reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovsbq %d(%%rbp), %s\n", poff, reg);
        }
    } else if (bytes == 2) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzwl %d(%%rbp), %s\n", poff, reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovswq %d(%%rbp), %s\n", poff, reg);
        }
    } else if (bytes == 4) {
        if (is_unsigned) {
            fprintf(fp, "\tmovl %d(%%rbp), %s\n", poff, reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovslq %d(%%rbp), %s\n", poff, reg);
        }
    } else {
        fprintf(fp, "\tmovq %d(%%rbp), %s\n", poff, reg);
    }
}

static void emit_x86_64_store_int_indirect(FILE *fp, const int_reg_state_t *ist, int rp, int rv, long mem_size) {
    if (mem_size == 1) {
        fprintf(fp, "\tmovb %s, (%s)\n", reg64_to8(ist->regs[rv]), ist->regs[rp]);
    } else if (mem_size == 2) {
        fprintf(fp, "\tmovw %s, (%s)\n", reg64_to16(ist->regs[rv]), ist->regs[rp]);
    } else if (mem_size == 4) {
        fprintf(fp, "\tmovl %s, (%s)\n", reg64_to32(ist->regs[rv]), ist->regs[rp]);
    } else {
        fprintf(fp, "\tmovq %s, (%s)\n", ist->regs[rv], ist->regs[rp]);
    }
}

void emit_x86_64_load_int_value_to_reg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int value,
                                               const char *reg) {
    if (value < 0 || value >= f->value_count) {
        fprintf(fp, "\tmovq $0, %s\n", reg);
        return;
    }
    if (is_stackalloc_value(lay, value)) {
        fprintf(fp, "\tleaq %d(%%rbp), %s\n", stackalloc_off(lay, value), reg);
    } else {
        fprintf(fp, "\tmovq %d(%%rbp), %s\n", slot_off(lay, value), reg);
    }
}

static void emit_x86_64_store_aggregate_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                                 const cc_ssa_instr_t *in, long mem_size) {
    emit_x86_64_load_int_value_to_reg(fp, f, lay, in->lhs, "%rdi");
    emit_x86_64_load_int_value_to_reg(fp, f, lay, in->rhs, "%rsi");
    fprintf(fp, "\tmovq $%ld, %%rdx\n", mem_size);
    fprintf(fp, "\tcall memcpy\n");
}

static void emit_x86_64_store_ret_agg_part(FILE *fp, const char *src_reg, long off, long bytes) {
    if (bytes <= 0) {
        return;
    }
    if (bytes == 1) {
        fprintf(fp, "\tmovb %s, %ld(%%r11)\n", reg64_to8(src_reg), off);
    } else if (bytes == 2) {
        fprintf(fp, "\tmovw %s, %ld(%%r11)\n", reg64_to16(src_reg), off);
    } else if (bytes == 4) {
        fprintf(fp, "\tmovl %s, %ld(%%r11)\n", reg64_to32(src_reg), off);
    } else {
        fprintf(fp, "\tmovq %s, %ld(%%r11)\n", src_reg, off);
    }
}

/**
 * Store an aggregate a call returned in registers into the storage
 * @p agg_ptr_value points at: its eightbytes from %rax then %rdx, or for
 * SSE ones %xmm0 then %xmm1, as @p cls says; one returned in x87
 * registers from %st0 (and %st1).
 */
static void emit_x86_64_store_aggregate_return(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                                int agg_ptr_value, long agg_size, int cls) {
    static const char *const gprs[] = {"%rax", "%rdx"};
    static const char *const xmms[] = {"%xmm0", "%xmm1"};
    size_t gi = 0;
    size_t xi = 0;
    long k;

    emit_x86_64_load_int_value_to_reg(fp, f, lay, agg_ptr_value, "%r11");
    if ((cls & (CC_ABI_RET_X87 | CC_ABI_RET_X87_COMPLEX)) != 0) {
        fprintf(fp, "\tfstpt 0(%%r11)\n");
        if ((cls & CC_ABI_RET_X87_COMPLEX) != 0) {
            fprintf(fp, "\tfstpt 16(%%r11)\n");
        }
        return;
    }
    if ((cls & CC_ABI_RET_VEC128) != 0) {
        fprintf(fp, "\tmovups %%xmm0, 0(%%r11)\n");
        return;
    }
    for (k = 0; k * 8 < agg_size && k < 2; ++k) {
        long part = agg_size - k * 8 > 8 ? 8 : agg_size - k * 8;

        if ((cls & (k == 0 ? CC_ABI_SSE0 : CC_ABI_SSE1)) != 0) {
            fprintf(fp, "\t%s %s, %ld(%%r11)\n", part > 4 ? "movsd" : "movss", xmms[xi++], k * 8);
        } else if (part == 3 || (part > 4 && part < 8)) {
            /* an odd-sized tail, a byte at a time */
            long b;

            for (b = 0; b < part; ++b) {
                if (b > 0) {
                    fprintf(fp, "\tshrq $8, %s\n", gprs[gi]);
                }
                fprintf(fp, "\tmovb %s, %ld(%%r11)\n", reg64_to8(gprs[gi]), k * 8 + b);
            }
            ++gi;
        } else {
            emit_x86_64_store_ret_agg_part(fp, gprs[gi++], k * 8, part);
        }
    }
}

/**
 * Whether an x86-64 aggregate of @p size bytes and agg_class @p cls is
 * returned through a hidden pointer: one over 16 bytes or of MEMORY class,
 * unless it comes back in x87 registers.
 */
static int x86_64_agg_ret_in_memory(long size, int cls) {
    if ((cls & (CC_ABI_RET_X87 | CC_ABI_RET_X87_COMPLEX)) != 0) {
        return 0;
    }
    return size > 16 || (cls & CC_ABI_RET_MEMORY) != 0;
}

static int emit_x86_64_call(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                            const cc_ssa_instr_t *in, cc_diag_t *diag) {
    size_t a;
    size_t abi_stack_bytes = 0;
    size_t xmm_regs = 0;
    size_t stack_bytes;
    size_t stack_pad;
    size_t stack_total;
    abi_loc_t *locs = NULL;
    int agg_ret_ptr = -1;
    long agg_ret_size = 0;
    int agg_ret_memory = 0;
    size_t gpr_start = 0;

    int_regs_flush(fp, f, lay, ist);

    if (in->rhs >= 0 && in->rhs < f->value_count && in->dst == in->rhs && in->imm > 0) {
        agg_ret_ptr = in->rhs;
        agg_ret_size = in->imm;
        if (x86_64_agg_ret_in_memory(agg_ret_size, in->agg_class)) {
            agg_ret_memory = 1;
            gpr_start = 1;
        }
    }

    if (in->arg_count > 0) {
        locs = (abi_loc_t *)calloc(in->arg_count, sizeof(*locs));
        if (locs == NULL) {
            set_diag(diag, "out of memory classifying call arguments");
            return -1;
        }
        abi64_classify_call_args(f, in, gpr_start, locs, &abi_stack_bytes, &xmm_regs);
    }

    stack_bytes = abi_stack_bytes;
    stack_pad = (stack_bytes & 0xF) == 0 ? 0 : (16 - (stack_bytes & 0xF));
    stack_total = stack_bytes + stack_pad;

    if (stack_total > 0) {
        fprintf(fp, "\tsubq $%zu, %%rsp\n", stack_total);
    }
    if (agg_ret_memory) {
        emit_x86_64_load_int_value_to_reg(fp, f, lay, agg_ret_ptr, "%rdi");
    }
    for (a = 0; a < in->arg_count; ++a) {
        int arg_value = in->args[a];
        if (emit_x86_64_move_value_to_abi_loc(fp, f, lay, in, a, arg_value, &locs[a], diag) != 0) {
            free(locs);
            return -1;
        }
    }
    if (in->call_is_variadic) {
        fprintf(fp, "\tmovb $%zu, %%al\n", xmm_regs);
    }
    if (in->op == CC_SSA_CALLI) {
        emit_x86_64_load_int_value_to_reg(fp, f, lay, in->lhs, "%r11");
        fprintf(fp, "\tcall *%%r11\n");
    } else {
        if (in->call_chain > 0) {
            /* a GNU nested function's environment, in the static chain register */
            emit_x86_64_load_int_value_to_reg(fp, f, lay, in->call_chain - 1, "%r10");
        }
        fprintf(fp, "\tcall %s\n", in->sym);
    }
    if (stack_total > 0) {
        fprintf(fp, "\taddq $%zu, %%rsp\n", stack_total);
    }
    free(locs);
    if (agg_ret_ptr >= 0) {
        if (!agg_ret_memory) {
            emit_x86_64_store_aggregate_return(fp, f, lay, agg_ret_ptr, agg_ret_size, in->agg_class);
        }
        return 0;
    }
    if (in->dst >= 0) {
        if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
            if (in->call_ret_x87) {
                if (value_size_bytes(f, in->dst, 8) >= 16) fprintf(fp, "\tfstpt %d(%%rbp)\n", slot_off(lay, in->dst));
                else fprintf(fp, "\tfstpl %d(%%rbp)\n", slot_off(lay, in->dst));
            } else {
                if ((in->agg_class & CC_ABI_RET_FLOAT) != 0) {
                    fprintf(fp, "\tcvtss2sd %%xmm0, %%xmm0\n");
                }
                fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", slot_off(lay, in->dst));
            }
        } else {
            long ret_bytes = in->imm > 0 ? in->imm : 8;
            emit_x86_64_normalize_int_return(fp, ret_bytes, in->is_unsigned);
            {
                int rd = int_regs_define(fp, f, lay, ist, in->dst, int_reg_index(ist, "%rax"), -1);
                if (strcmp(ist->regs[rd], "%rax") != 0) {
                    fprintf(fp, "\tmovq %%rax, %s\n", ist->regs[rd]);
                }
            }
        }
    }
    return 0;
}

static long x86_64_function_sret_size(const cc_ssa_function_t *f) {
    size_t i;
    long max_size = 0;

    if (f == NULL) {
        return 0;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        if (in->op != CC_SSA_RET || !x86_64_agg_ret_in_memory(in->imm, in->agg_class)) {
            continue;
        }
        if (in->imm > max_size) {
            max_size = in->imm;
        }
    }
    return max_size;
}

static long x86_64_infer_sret_size_from_calls(const cc_ssa_module_t *m, const cc_ssa_function_t *callee) {
    size_t i;
    long max_size = 0;

    if (m == NULL || callee == NULL || callee->name == NULL) {
        return 0;
    }
    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        size_t j;
        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];
            if (in->op != CC_SSA_CALL) {
                continue;
            }
            if (in->sym == NULL || strcmp(in->sym, callee->name) != 0) {
                continue;
            }
            if (in->rhs < 0 || in->dst < 0 || in->rhs != in->dst || !x86_64_agg_ret_in_memory(in->imm, in->agg_class)) {
                continue;
            }
            if (in->imm > max_size) {
                max_size = in->imm;
            }
        }
    }
    return max_size;
}

int emit_x86_64(FILE *fp, const cc_ssa_module_t *m, const char *src_path, int emit_debug, int pic,
                       cc_diag_t *diag) {
    size_t i;
    const char *trace_env = getenv("CC_DEBUG_TRACE");
    int debug_trace = (trace_env != NULL && trace_env[0] != '\0');

    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        const char *func_sec = (f->attr_section != NULL && f->attr_section[0] != '\0') ? f->attr_section : ".text";
        slot_layout_t lay;
        int_reg_state_t ist;
        size_t j;
        int frame;
        int raw_frame;
        int va_state_off = 0;
        int va_regsave_off = 0;
        long sret_mem_size = x86_64_function_sret_size(f);
        long inferred_sret_size = x86_64_infer_sret_size_from_calls(m, f);
        int has_sret;
        int sret_ptr_off = 0;
        int param_gpr_bias;

        if (inferred_sret_size > sret_mem_size) {
            sret_mem_size = inferred_sret_size;
        }
        has_sret = sret_mem_size > 0 ? 1 : 0;
        param_gpr_bias = has_sret ? 1 : 0;

        if (build_slot_layout(f, 16, &lay, diag) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                set_diag(diag, "x86_64: failed to build slot layout");
            }
            return -1;
        }
        raw_frame = lay.frame_bytes;
        if (has_sret) {
            sret_ptr_off = -(raw_frame + 8);
            raw_frame += 8;
        }
        if (f->is_variadic) {
            va_state_off = -(raw_frame + 24);
            raw_frame += 24;
            va_regsave_off = -(raw_frame + 176);
            raw_frame += 176;
        }
        /*
         * SysV AMD64 call-site rule: before executing "call", %rsp must be
         * 0 mod 16 (callee entry then sees %rsp at 8 mod 16 due return addr).
         *
         * After "push %rbp", %rsp is 0 mod 16, so keep frame size a multiple
         * of 16 to preserve call-site alignment at internal calls.
         */
        frame = cc_backend_align_frame_size(raw_frame, 16);

        if (debug_trace) {
            int v;
            fprintf(fp, "\t# ccdbg begin %s values=%d frame=%d raw=%d\n", f->name, f->value_count, frame, raw_frame);
            for (v = 0; v < f->value_count; ++v) {
                long off = slot_off(&lay, v);
                if (off != 0) {
                    fprintf(fp, "\t# ccdbg slot v%d off=%ld ty=%d\n", v, off, (int)cc_ssa_value_class(f, v));
                }
            }
        }

        fprintf(fp, "\n");
        emit_text_section(fp, f->attr_section != NULL && f->attr_section[0] != '\0' ? f->attr_section : NULL);
        if ((f->storage & CC_STORAGE_STATIC) == 0 &&
            !(((f->storage & CC_STORAGE_INLINE) != 0) && ((f->storage & CC_STORAGE_EXTERN) == 0))) {
            if ((f->attr_flags & CC_ATTR_WEAK) != 0) {
                fprintf(fp, ".weak %s\n", f->name);
            } else {
                fprintf(fp, ".globl %s\n", f->name);
            }
        }
        emit_visibility_attr(fp, f->name, f->attr_flags);
        if (f->attr_align > 1) {
            fprintf(fp, ".align %ld\n", f->attr_align);
        }
        fprintf(fp, ".type %s, @function\n", f->name);
        fprintf(fp, "%s:\n", f->name);

        if (emit_debug) {
            fprintf(fp, "\t.cfi_startproc\n");
            fprintf(fp, "\t.cfi_def_cfa_offset 16\n");
            fprintf(fp, "\t.cfi_offset %%rbp, -16\n");
        }

        fprintf(fp, "\tpushq %%rbp\n");
        fprintf(fp, "\tmovq %%rsp, %%rbp\n");
        if (emit_debug) {
            fprintf(fp, "\t.cfi_def_cfa_register %%rbp\n");
            if (src_path != NULL) {
                fprintf(fp, "\t.loc 1 1 0\n");
            }
        }
        if (frame > 0) {
            fprintf(fp, "\tsubq $%d, %%rsp\n", frame);
        }
        if (has_sret) {
            fprintf(fp, "\tmovq %%rdi, %d(%%rbp)\n", sret_ptr_off);
        }
        if (f->is_variadic) {
            fprintf(fp, "\tmovq %%rdi, %d(%%rbp)\n", va_regsave_off + 0);
            fprintf(fp, "\tmovq %%rsi, %d(%%rbp)\n", va_regsave_off + 8);
            fprintf(fp, "\tmovq %%rdx, %d(%%rbp)\n", va_regsave_off + 16);
            fprintf(fp, "\tmovq %%rcx, %d(%%rbp)\n", va_regsave_off + 24);
            fprintf(fp, "\tmovq %%r8, %d(%%rbp)\n", va_regsave_off + 32);
            fprintf(fp, "\tmovq %%r9, %d(%%rbp)\n", va_regsave_off + 40);
            fprintf(fp, "\tmovdqu %%xmm0, %d(%%rbp)\n", va_regsave_off + 48);
            fprintf(fp, "\tmovdqu %%xmm1, %d(%%rbp)\n", va_regsave_off + 64);
            fprintf(fp, "\tmovdqu %%xmm2, %d(%%rbp)\n", va_regsave_off + 80);
            fprintf(fp, "\tmovdqu %%xmm3, %d(%%rbp)\n", va_regsave_off + 96);
            fprintf(fp, "\tmovdqu %%xmm4, %d(%%rbp)\n", va_regsave_off + 112);
            fprintf(fp, "\tmovdqu %%xmm5, %d(%%rbp)\n", va_regsave_off + 128);
            fprintf(fp, "\tmovdqu %%xmm6, %d(%%rbp)\n", va_regsave_off + 144);
            fprintf(fp, "\tmovdqu %%xmm7, %d(%%rbp)\n", va_regsave_off + 160);
        }
        if (emit_dynalloc_slot_clears(fp, f, &lay, 1, diag) != 0) {
            slot_layout_free(&lay);
            return -1;
        }
        if (int_regs_init(&ist, f, emit_regs64, (int)(sizeof(emit_regs64) / sizeof(emit_regs64[0])), 1, diag) != 0) {
            slot_layout_free(&lay);
            if (diag != NULL && diag->message[0] == '\0') {
                set_diag(diag, "x86_64: failed to initialize integer register state");
            }
            return -1;
        }

        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];
            ist.cur_index = (int)j;
            if (debug_trace) {
                fprintf(fp, "\t# ccdbg i=%zu op=%s dst=%d lhs=%d rhs=%d imm=%lld lbl=%d tl=%d fl=%d\n", j,
                        ssa_op_name(in->op), in->dst, in->lhs, in->rhs, in->imm, in->label, in->true_label,
                        in->false_label);
            }
            int_regs_flush(fp, f, &lay, &ist);

            switch (in->op) {
            case CC_SSA_PARAM: {
                abi_loc_t loc = abi64_param_loc(f, in->param_index, (size_t)param_gpr_bias);
                cc_value_type_t vt = cc_ssa_value_class(f, in->dst);
                int param_is_float = f->param_abi != NULL && in->param_index >= 0 &&
                                     (size_t)in->param_index < f->param_count &&
                                     f->param_abi[in->param_index] == CC_CALL_ARG_ABI_FLOAT;
                int param_is_ldouble =
                    (f->param_abi != NULL && in->param_index >= 0 &&
                     (size_t)in->param_index < f->param_count &&
                     f->param_abi[in->param_index] == CC_CALL_ARG_ABI_LDOUBLE)
                        ? 1
                        : 0;
                int param_is_agg_mem =
                    (f->param_abi != NULL && in->param_index >= 0 &&
                     (size_t)in->param_index < f->param_count &&
                     CC_ABI_KIND(f->param_abi[in->param_index]) == CC_CALL_ARG_ABI_AGG_MEM)
                        ? 1
                        : 0;
                int param_is_agg_reg =
                    (f->param_abi != NULL && in->param_index >= 0 &&
                     (size_t)in->param_index < f->param_count &&
                     CC_ABI_KIND(f->param_abi[in->param_index]) == CC_CALL_ARG_ABI_AGG_REG)
                        ? 1
                        : 0;
                int param_is_zero =
                    (f->param_abi != NULL && in->param_index >= 0 &&
                     (size_t)in->param_index < f->param_count &&
                     f->param_abi[in->param_index] == CC_CALL_ARG_ABI_ZERO)
                        ? 1
                        : 0;
                long pbytes = in->imm > 0 ? in->imm : 8;
                if (param_is_zero) {
                    break;
                }
                if (param_is_agg_mem) {
                    int rd;
                    if (loc.kind != ABI_LOC_STACK) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        set_diag(diag, "aggregate memory parameter was not assigned a stack location");
                        return -1;
                    }
                    rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tleaq %zu(%%rbp), %s\n", (size_t)(16 + loc.index), ist.regs[rd]);
                    break;
                }
                if (param_is_agg_reg) {
                    int storage_off;
                    int rd;
                    if (in->lhs < 0 || !is_stackalloc_value(&lay, in->lhs)) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        set_diag(diag, "aggregate register parameter missing local storage");
                        return -1;
                    }
                    storage_off = stackalloc_off(&lay, in->lhs);
                    if (loc.kind == ABI_LOC_GPR) {
                        /* each eightbyte from the next general or xmm register its class names */
                        size_t gi = loc.index;
                        size_t xi = loc.xmm_index;
                        size_t k;

                        if ((loc.sse & ABI_SSE_WHOLE) != 0) {
                            fprintf(fp, "\tmovups %s, %d(%%rbp)\n", arg_reg64_xmm(xi), storage_off);
                            pbytes = 0;
                        }
                        for (k = 0; k * 8 < (size_t)pbytes && k < 2; ++k) {
                            size_t part = (size_t)pbytes - k * 8 > 8 ? 8 : (size_t)pbytes - k * 8;
                            int is_sse = (loc.sse & (1u << k)) != 0;
                            const char *r = is_sse ? arg_reg64_xmm(xi++) : arg_reg64_gpr(gi++);

                            if (r == NULL) {
                                int_regs_free(&ist);
                                slot_layout_free(&lay);
                                set_diag(diag, "unsupported aggregate parameter register index");
                                return -1;
                            }
                            if (is_sse) {
                                emit_x86_64_store_agg_part_from_xmm(fp, r, storage_off, k * 8, part);
                            } else {
                                int phys = int_reg_index(&ist, r);

                                if (phys >= 0) {
                                    /* the byte-wise tail shifts the register: free it of any value first */
                                    int_regs_spill_reg(fp, f, &lay, &ist, phys);
                                }
                                emit_x86_64_store_agg_part_from_reg(fp, r, storage_off, k * 8, part);
                            }
                        }
                    } else if (loc.kind == ABI_LOC_STACK) {
                        size_t off = 0;
                        int poff = 16 + (int)loc.index;
                        while (off + 8 <= (size_t)pbytes) {
                            fprintf(fp, "\tmovq %d(%%rbp), %%r11\n", poff + (int)off);
                            fprintf(fp, "\tmovq %%r11, %d(%%rbp)\n", storage_off + (int)off);
                            off += 8;
                        }
                        if (off < (size_t)pbytes) {
                            fprintf(fp, "\tmovq %d(%%rbp), %%r11\n", poff + (int)off);
                            emit_x86_64_store_agg_part_from_reg(fp, "%r11", storage_off, off, (size_t)pbytes - off);
                        }
                    } else {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        set_diag(diag, "aggregate register parameter was not assigned an integer or stack location");
                        return -1;
                    }
                    rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tleaq %d(%%rbp), %s\n", storage_off, ist.regs[rd]);
                    break;
                }
                if (loc.kind == ABI_LOC_XMM) {
                    int_regs_flush(fp, f, &lay, &ist);
                    const char *reg = arg_reg64_xmm(loc.index);
                    if (reg == NULL) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        set_diag(diag, "unsupported floating parameter register index");
                        return -1;
                    }
                    if (param_is_float) {
                        /* it came in single precision; the function holds it as a double */
                        fprintf(fp, "\tcvtss2sd %s, %s\n", reg, reg);
                    }
                    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", reg, slot_off(&lay, in->dst));
                } else if (loc.kind == ABI_LOC_GPR) {
                    const char *reg = arg_reg64_gpr(loc.index);
                    int rd;
                    int phys;
                    if (reg == NULL) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        set_diag(diag, "unsupported integer parameter register index");
                        return -1;
                    }
                    if (vt == CC_VAL_F64) {
                        int_regs_flush(fp, f, &lay, &ist);
                        fprintf(fp, "\tmovq %s, %d(%%rbp)\n", reg, slot_off(&lay, in->dst));
                        break;
                    }
                    phys = int_reg_index(&ist, reg);
                    if (phys >= 0) {
                        int_regs_spill_reg(fp, f, &lay, &ist, phys);
                        int_regs_bind(&ist, in->dst, phys, 1);
                        emit_x86_64_normalize_int_param_reg(fp, ist.regs[phys], pbytes, in->is_unsigned);
                    } else {
                        rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                        fprintf(fp, "\tmovq %s, %s\n", reg, ist.regs[rd]);
                        emit_x86_64_normalize_int_param_reg(fp, ist.regs[rd], pbytes, in->is_unsigned);
                    }
                } else {
                    int poff = 16 + (int)loc.index;
                    if (param_is_ldouble && vt == CC_VAL_F64) {
                        int_regs_flush(fp, f, &lay, &ist);
                        fprintf(fp, "\tfldt %d(%%rbp)\n", poff);
                        fprintf(fp, "\tfstpt %d(%%rbp)\n", slot_off(&lay, in->dst));
                    } else if (param_is_float && vt == CC_VAL_F64) {
                        int_regs_flush(fp, f, &lay, &ist);
                        fprintf(fp, "\tmovss %d(%%rbp), %s\n", poff, x86_64_fp_tmp_reg());
                        fprintf(fp, "\tcvtss2sd %s, %s\n", x86_64_fp_tmp_reg(), x86_64_fp_tmp_reg());
                        fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", x86_64_fp_tmp_reg(), slot_off(&lay, in->dst));
                    } else if (vt == CC_VAL_F64) {
                        int_regs_flush(fp, f, &lay, &ist);
                        fprintf(fp, "\tmovsd %d(%%rbp), %s\n", poff, x86_64_fp_tmp_reg());
                        fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", x86_64_fp_tmp_reg(), slot_off(&lay, in->dst));
                    } else {
                        int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                        emit_x86_64_load_int_param_from_stack(fp, ist.regs[rd], poff, pbytes, in->is_unsigned);
                    }
                }
                break;
            }

            case CC_SSA_CONST:
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    if (value_size_bytes(f, in->dst, 8) >= 16) {
                        unsigned char raw[16];
                        uint64_t lo;
                        uint64_t hi;
                        long double cvt = (in->is_unsigned && in->imm >= 1 && in->imm <= 3)
                                              ? x86_64_special_ldouble_const(in->imm)
                                              : in->fimm_ld;
                        size_t ncopy = sizeof(cvt) < sizeof(raw) ? sizeof(cvt) : sizeof(raw);
                        memset(raw, 0, sizeof(raw));
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
                        if (ncopy > 10) {
                            ncopy = 10;
                        }
#endif
                        memcpy(raw, &cvt, ncopy);
                        memcpy(&lo, raw, sizeof(lo));
                        memcpy(&hi, raw + 8, sizeof(hi));
                        fprintf(fp, "\tmovabsq $0x%llx, %%rax\n", (unsigned long long)lo);
                        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                        fprintf(fp, "\tmovabsq $0x%llx, %%rax\n", (unsigned long long)hi);
                        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst) + 8);
                    } else {
                        union {
                            double d;
                            uint64_t u;
                        } cvt;
                        cvt.d = in->fimm;
                        fprintf(fp, "\tmovabsq $0x%llx, %%rax\n", (unsigned long long)cvt.u);
                        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                    }
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tmovq $%lld, %s\n", in->imm, ist.regs[rd]);
                }
                break;

            case CC_SSA_STR:
                emit_string_literal_label(fp, i, j, in->sym, func_sec);
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    fprintf(fp, "\tleaq .L__cc_str_%zu_%zu(%%rip), %%rax\n", i, j);
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tleaq .L__cc_str_%zu_%zu(%%rip), %s\n", i, j, ist.regs[rd]);
                }
                break;

            case CC_SSA_MOV:
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    emit_x86_64_mov_fp(fp, f, &lay, in);
                } else {
                    int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                }
                break;

            case CC_SSA_STACKALLOC: {
                int off = stackalloc_off(&lay, in->dst);
                int rd;
                if (off == 0) {
                    set_diag(diag, "x86_64: missing stack allocation offset");
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    return -1;
                }
                rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                fprintf(fp, "\tleaq %d(%%rbp), %s\n", off, ist.regs[rd]);
                break;
            }

            case CC_SSA_CHAIN: {
                /* the static chain, as the function was entered: nothing has run before this */
                int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                fprintf(fp, "\tmovq %%r10, %s\n", ist.regs[rd]);
                break;
            }

            case CC_SSA_DYNALLOC:
                if (emit_dynalloc(fp, f, &lay, &ist, in, j, diag) != 0) {
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    return -1;
                }
                break;

            case CC_SSA_ADDR:
                /*
                 * Taking an address of an SSA-backed local requires the
                 * current value to be materialized in its stack slot.
                 */
                int_regs_flush(fp, f, &lay, &ist);
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    fprintf(fp, "\tleaq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tleaq %d(%%rbp), %s\n", slot_off(&lay, in->lhs), ist.regs[rd]);
                }
                break;

            case CC_SSA_GADDR: {
                int use_got = pic || module_symbol_is_extern_global(m, in->sym);
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    if (use_got) {
                        fprintf(fp, "\tmovq %s@GOTPCREL(%%rip), %%rax\n", in->sym);
                    } else {
                        fprintf(fp, "\tleaq %s(%%rip), %%rax\n", in->sym);
                    }
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    if (use_got) {
                        fprintf(fp, "\tmovq %s@GOTPCREL(%%rip), %s\n", in->sym, ist.regs[rd]);
                    } else {
                        fprintf(fp, "\tleaq %s(%%rip), %s\n", in->sym, ist.regs[rd]);
                    }
                }
                break;
            }

            case CC_SSA_LADDR:
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    fprintf(fp, "\tleaq ");
                    emit_local_label(fp, f->name, in->label);
                    fprintf(fp, "(%%rip), %%rax\n");
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tleaq ");
                    emit_local_label(fp, f->name, in->label);
                    fprintf(fp, "(%%rip), %s\n", ist.regs[rd]);
                }
                break;

            case CC_SSA_LOAD:
                /*
                 * Pointer-based loads must observe prior writes to possibly
                 * aliased locals.
                 */
                int_regs_flush(fp, f, &lay, &ist);
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    emit_x86_64_load_fp_indirect(fp, f, &lay, &ist, in);
                } else {
                    int rp = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, rp, -1);
                    long mem_size = in->imm > 0 ? in->imm : 8;
                    if (mem_size == 1) {
                        if (in->is_unsigned) {
                            fprintf(fp, "\tmovzbl (%s), %s\n", ist.regs[rp], reg64_to32(ist.regs[rd]));
                        } else {
                            fprintf(fp, "\tmovsbq (%s), %s\n", ist.regs[rp], ist.regs[rd]);
                        }
                    } else if (mem_size == 2) {
                        if (in->is_unsigned) {
                            fprintf(fp, "\tmovzwl (%s), %s\n", ist.regs[rp], reg64_to32(ist.regs[rd]));
                        } else {
                            fprintf(fp, "\tmovswq (%s), %s\n", ist.regs[rp], ist.regs[rd]);
                        }
                    } else if (mem_size == 4) {
                        if (in->is_unsigned) {
                            fprintf(fp, "\tmovl (%s), %s\n", ist.regs[rp], reg64_to32(ist.regs[rd]));
                        } else {
                            fprintf(fp, "\tmovslq (%s), %s\n", ist.regs[rp], ist.regs[rd]);
                        }
                    } else {
                        fprintf(fp, "\tmovq (%s), %s\n", ist.regs[rp], ist.regs[rd]);
                    }
                    int_regs_bind(&ist, in->dst, rd, 1);
                }
                break;

            case CC_SSA_STORE:
                /*
                 * Unknown-pointer stores can alias any promoted local. Keep
                 * semantics correct by spilling/invalidating integer cache.
                 */
                int_regs_flush(fp, f, &lay, &ist);
                if (cc_ssa_value_class(f, in->rhs) == CC_VAL_F64) {
                    long mem_size = in->imm > 0 ? in->imm : 8;
                    emit_x86_64_store_fp_indirect(fp, f, &lay, &ist, in, mem_size);
                } else {
                    int rp = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rv = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, rp);
                    long mem_size = in->imm > 0 ? in->imm : 8;
                    if (mem_size > 8) {
                        /* Aggregate copy: rhs is source address, lhs is destination address. */
                        int_regs_flush(fp, f, &lay, &ist);
                        emit_x86_64_store_aggregate_indirect(fp, f, &lay, in, mem_size);
                    } else {
                        emit_x86_64_store_int_indirect(fp, &ist, rp, rv, mem_size);
                    }
                }
                int_regs_flush(fp, f, &lay, &ist);
                break;

            case CC_SSA_ADD:
            case CC_SSA_SUB:
            case CC_SSA_MUL:
            case CC_SSA_DIV:
            case CC_SSA_AND:
            case CC_SSA_OR:
            case CC_SSA_XOR:
            case CC_SSA_SHL:
            case CC_SSA_SHR:
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    if (in->op == CC_SSA_AND || in->op == CC_SSA_OR || in->op == CC_SSA_XOR || in->op == CC_SSA_SHL ||
                        in->op == CC_SSA_SHR) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        set_diag(diag, "bitwise/shift operation on floating value");
                        return -1;
                    }
                    if (emit_x86_64_float_binop(fp, f, &lay, in, diag) != 0) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        if (diag != NULL && diag->message[0] == '\0') {
                            set_diag(diag, "x86_64: floating arithmetic emission failed");
                        }
                        return -1;
                    }
                } else {
                    int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rr;
                    if (ist.reg_dirty[rl]) {
                        int_regs_spill_reg(fp, f, &lay, &ist, rl);
                        rl = int_regs_load(fp, f, &lay, &ist, in->lhs, rl, -1);
                    }
                    if (in->op == CC_SSA_DIV) {
                        int rax = int_reg_index(&ist, "%rax");
                        int rdx = int_reg_index(&ist, "%rdx");
                        int rcx = int_reg_index(&ist, "%rcx");
                        int rr_div;
                        int lhs_dirty = ist.reg_dirty[rl];
                        if (rax < 0 || rdx < 0) {
                            int_regs_free(&ist);
                            slot_layout_free(&lay);
                            set_diag(diag, "x86_64 register set missing %rax/%rdx");
                            return -1;
                        }
                        if (rl != rax) {
                            int_regs_clobber_reg(fp, f, &lay, &ist, rax);
                            fprintf(fp, "\tmovq %s, %%rax\n", ist.regs[rl]);
                            int_regs_bind(&ist, in->lhs, rax, lhs_dirty);
                            rl = rax;
                        }
                        int_regs_clobber_reg(fp, f, &lay, &ist, rdx);
                        rr_div = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, rax);
                        if (rr_div == rdx) {
                            int rhs_dirty = ist.reg_dirty[rr_div];
                            if (rcx < 0 || rcx == rax || rcx == rdx) {
                                int_regs_free(&ist);
                                slot_layout_free(&lay);
                                set_diag(diag, "x86_64 register set missing divisor scratch register");
                                return -1;
                            }
                            int_regs_clobber_reg(fp, f, &lay, &ist, rcx);
                            fprintf(fp, "\tmovq %%rdx, %%rcx\n");
                            int_regs_bind(&ist, in->rhs, rcx, rhs_dirty);
                            rr_div = rcx;
                        }
                        if (in->is_unsigned) {
                            fprintf(fp, "\txorq %%rdx, %%rdx\n");
                            fprintf(fp, "\tdivq %s\n", ist.regs[rr_div]);
                        } else {
                            fprintf(fp, "\tcqto\n");
                            fprintf(fp, "\tidivq %s\n", ist.regs[rr_div]);
                        }
                        int_regs_bind(&ist, in->dst, rax, 1);
                        break;
                    }
                    if (in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) {
                        int rcx = int_reg_index(&ist, "%rcx");
                        int rr_shift;
                        int rhs_dirty = 0;
                        const char *mnemonic = in->op == CC_SSA_SHL ? "shlq" : (in->is_unsigned ? "shrq" : "sarq");
                        if (in->rhs < 0) {
                            unsigned sh = (unsigned)(in->imm & 63);
                            fprintf(fp, "\t%s $%u, %s\n", mnemonic, sh, ist.regs[rl]);
                            int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                            break;
                        }
                        if (rcx < 0) {
                            int_regs_free(&ist);
                            slot_layout_free(&lay);
                            set_diag(diag, "x86_64 register set missing %rcx");
                            return -1;
                        }
                        if (rl == rcx) {
                            int alt = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, rcx);
                            if (alt == rcx) {
                                int lhs_dirty = ist.reg_dirty[rcx];
                                alt = int_regs_define(fp, f, &lay, &ist, in->lhs, -1, rcx);
                                fprintf(fp, "\tmovq %%rcx, %s\n", ist.regs[alt]);
                                ist.reg_dirty[alt] = (unsigned char)(lhs_dirty ? 1 : 0);
                            }
                            rl = alt;
                        }
                        int_regs_clobber_reg(fp, f, &lay, &ist, rcx);
                        rr_shift = int_regs_load(fp, f, &lay, &ist, in->rhs, rcx, rl);
                        rhs_dirty = ist.reg_dirty[rr_shift];
                        if (rr_shift != rcx) {
                            fprintf(fp, "\tmovq %s, %%rcx\n", ist.regs[rr_shift]);
                            int_regs_bind(&ist, in->rhs, rcx, rhs_dirty);
                        }
                        fprintf(fp, "\tandq $63, %%rcx\n");
                        fprintf(fp, "\t%s %%cl, %s\n", mnemonic, ist.regs[rl]);
                        int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                        break;
                    }
                    rr = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, rl);
                    if (in->op == CC_SSA_ADD) {
                        fprintf(fp, "\taddq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    } else if (in->op == CC_SSA_SUB) {
                        fprintf(fp, "\tsubq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    } else if (in->op == CC_SSA_MUL) {
                        fprintf(fp, "\timulq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    } else if (in->op == CC_SSA_AND) {
                        fprintf(fp, "\tandq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    } else if (in->op == CC_SSA_OR) {
                        fprintf(fp, "\torq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    } else if (in->op == CC_SSA_XOR) {
                        fprintf(fp, "\txorq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    }
                    int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                }
                break;

            case CC_SSA_CMP: {
                if (emit_x86_64_cmp(fp, f, &lay, &ist, in) != 0) {
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    if (diag != NULL && diag->message[0] == '\0') {
                        set_diag(diag, "x86_64: compare emission failed");
                    }
                    return -1;
                }
                break;
            }

            case CC_SSA_I2F:
                emit_x86_64_i2f(fp, f, &lay, &ist, in);
                break;

            case CC_SSA_F2I:
                {
                    int f2i_u64 = value_size_bytes(f, in->dst, 8) >= 8 &&
                                  (in->is_unsigned || cc_ssa_value_is_unsigned(f, in->dst));

                    if (value_size_bytes(f, in->lhs, 8) >= 16) {
                        int rd;
                        int_regs_flush(fp, f, &lay, &ist);
                        int_regs_clobber_reg(fp, f, &lay, &ist, int_reg_index(&ist, "%rax"));
                        fprintf(fp, "\tfnstcw -16(%%rsp)\n");
                        fprintf(fp, "\tmovw -16(%%rsp), %%ax\n");
                        fprintf(fp, "\torw $0x0c00, %%ax\n");
                        fprintf(fp, "\tmovw %%ax, -14(%%rsp)\n");
                        fprintf(fp, "\tfldcw -14(%%rsp)\n");
                        fprintf(fp, "\tfldt %d(%%rbp)\n", slot_off(&lay, in->lhs));
                        emit_x87_store_i64(fp, "%rsp", -8, -20, f2i_u64);
                        fprintf(fp, "\tfldcw -16(%%rsp)\n");
                        rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                        fprintf(fp, "\tmovq -8(%%rsp), %s\n", ist.regs[rd]);
                    } else if (f2i_u64) {
                        /* cvttsd2si is signed: from 2^63 up, convert less 2^63 and set the top bit back */
                        unsigned long lid = next_aux_label_id();
                        int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);

                        fprintf(fp, "\tmovsd %d(%%rbp), %%xmm15\n", slot_off(&lay, in->lhs));
                        fprintf(fp, "\tmovabsq $0x43e0000000000000, %s\n", ist.regs[rd]);
                        fprintf(fp, "\tmovq %s, %%xmm14\n", ist.regs[rd]);
                        fprintf(fp, "\tucomisd %%xmm14, %%xmm15\n");
                        fprintf(fp, "\tjae .L__cc_d2u64_big_%lu\n", lid);
                        fprintf(fp, "\tcvttsd2siq %%xmm15, %s\n", ist.regs[rd]);
                        fprintf(fp, "\tjmp .L__cc_d2u64_done_%lu\n", lid);
                        fprintf(fp, ".L__cc_d2u64_big_%lu:\n", lid);
                        fprintf(fp, "\tsubsd %%xmm14, %%xmm15\n");
                        fprintf(fp, "\tcvttsd2siq %%xmm15, %s\n", ist.regs[rd]);
                        fprintf(fp, "\tbtcq $63, %s\n", ist.regs[rd]);
                        fprintf(fp, ".L__cc_d2u64_done_%lu:\n", lid);
                    } else {
                        int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                        fprintf(fp, "\tcvttsd2siq %d(%%rbp), %s\n", slot_off(&lay, in->lhs), ist.regs[rd]);
                    }
                }
                break;

            case CC_SSA_FROUND32:
                emit_x86_64_fround32(fp, &lay, in);
                break;

            case CC_SSA_FROUND64:
                emit_fround64(fp, f, &lay, in, "%rbp", 1);
                break;

            case CC_SSA_EXT:
                /* legalization expands every one */
                int_regs_free(&ist);
                slot_layout_free(&lay);
                set_diag(diag, "x86_64: integer conversion reached the emitter unexpanded");
                return -1;

            case CC_SSA_LABEL:
                int_regs_flush(fp, f, &lay, &ist);
                emit_local_label(fp, f->name, in->label);
                fprintf(fp, ":\n");
                break;

            case CC_SSA_BR:
                int_regs_flush(fp, f, &lay, &ist);
                fprintf(fp, "\tjmp ");
                emit_local_label(fp, f->name, in->label);
                fprintf(fp, "\n");
                break;

            case CC_SSA_BR_COND:
                {
                    int rc = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    fprintf(fp, "\tcmpq $0, %s\n", ist.regs[rc]);
                    int_regs_flush(fp, f, &lay, &ist);
                }
                fprintf(fp, "\tjne ");
                emit_local_label(fp, f->name, in->true_label);
                fprintf(fp, "\n");
                fprintf(fp, "\tjmp ");
                emit_local_label(fp, f->name, in->false_label);
                fprintf(fp, "\n");
                break;

            case CC_SSA_VA_START: {
                emit_x86_64_va_start(fp, f, &lay, &ist, in, va_state_off, va_regsave_off, param_gpr_bias);
                break;
            }

            case CC_SSA_CALL:
            case CC_SSA_CALLI: {
                if (emit_x86_64_call(fp, f, &lay, &ist, in, diag) != 0) {
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    if (diag != NULL && diag->message[0] == '\0') {
                        set_diag(diag, "x86_64: call emission failed");
                    }
                    return -1;
                }
                break;
            }

            case CC_SSA_ASM:
                int_regs_flush(fp, f, &lay, &ist);
                if (emit_inline_asm(fp, f, &lay, in, i, j, 1, diag) != 0) {
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    if (diag != NULL && diag->message[0] == '\0') {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "x86_64: inline asm emission failed (fn=%s instr=%zu tmpl=%.64s)",
                                 f->name != NULL ? f->name : "<anon>", j, in->sym != NULL ? in->sym : "");
                        set_diag(diag, msg);
                    }
                    return -1;
                }
                break;

            case CC_SSA_TRAP:
                fprintf(fp, "\tud2\n");
                break;

            case CC_SSA_LO32:
            case CC_SSA_HI32:
            case CC_SSA_PAIR:
            case CC_SSA_UMULW:
                /* i386 legalization writes these; x86-64 has 64-bit registers */
                set_diag(diag, "x86-64: word-pair operation in legal IR");
                int_regs_free(&ist);
                slot_layout_free(&lay);
                return -1;

            case CC_SSA_RET:
                if (has_sret) {
                    long copy_bytes = in->imm > 0 ? in->imm : sret_mem_size;
                    if (copy_bytes <= 0) {
                        copy_bytes = sret_mem_size;
                    }
                    int_regs_flush(fp, f, &lay, &ist);
                    if (in->lhs >= 0) {
                        emit_x86_64_load_int_value_to_reg(fp, f, &lay, in->lhs, "%rsi");
                        fprintf(fp, "\tmovq %d(%%rbp), %%rdi\n", sret_ptr_off);
                        fprintf(fp, "\tmovq $%ld, %%rdx\n", copy_bytes);
                        fprintf(fp, "\tcall memcpy\n");
                    }
                    fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", sret_ptr_off);
                } else if (in->lhs >= 0) {
                    if (f->ret_type == CC_VAL_F64) {
                        int_regs_flush(fp, f, &lay, &ist);
                        if (f->ret_abi == CC_CALL_ARG_ABI_LDOUBLE) {
                            if (value_size_bytes(f, in->lhs, 8) >= 16) fprintf(fp, "\tfldt %d(%%rbp)\n", slot_off(&lay, in->lhs));
                            else fprintf(fp, "\tfldl %d(%%rbp)\n", slot_off(&lay, in->lhs));
                        } else {
                            fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->lhs));
                            if (f->ret_abi == CC_CALL_ARG_ABI_FLOAT) {
                                /* a float is returned in single precision */
                                fprintf(fp, "\tcvtsd2ss %%xmm0, %%xmm0\n");
                            }
                        }
                    } else if ((in->agg_class & (CC_ABI_RET_X87 | CC_ABI_RET_X87_COMPLEX)) != 0) {
                        /* lhs points at it: a long double in %st0, or the parts of a complex one in %st0, %st1 */
                        int_regs_flush(fp, f, &lay, &ist);
                        emit_x86_64_load_int_value_to_reg(fp, f, &lay, in->lhs, "%r11");
                        if ((in->agg_class & CC_ABI_RET_X87_COMPLEX) != 0) {
                            fprintf(fp, "\tfldt 16(%%r11)\n");
                        }
                        fprintf(fp, "\tfldt 0(%%r11)\n");
                    } else if (in->imm > 0 && in->imm <= 16) {
                        /* its eightbytes (lhs, rhs) to %rax then %rdx, or the SSE ones to %xmm0 then %xmm1 */
                        static const char *const gprs[] = {"%rax", "%rdx"};
                        static const char *const xmms[] = {"%xmm0", "%xmm1"};
                        size_t gi = 0;
                        size_t xi = 0;
                        int vec128 = (in->agg_class & CC_ABI_RET_VEC128) != 0;
                        int k;

                        int_regs_flush(fp, f, &lay, &ist);
                        if (vec128) {
                            /* a 16-byte vector: both eightbytes in %xmm0 */
                            fprintf(fp, "\tmovq %d(%%rbp), %%xmm0\n", slot_off(&lay, in->lhs));
                            fprintf(fp, "\tmovhps %d(%%rbp), %%xmm0\n", slot_off(&lay, in->rhs));
                        }
                        for (k = vec128 ? 2 : 0; k < 2; ++k) {
                            int part = k == 0 ? in->lhs : in->rhs;

                            if (k == 1 && (in->imm <= 8 || part < 0)) {
                                break;
                            }
                            if ((in->agg_class & (k == 0 ? CC_ABI_SSE0 : CC_ABI_SSE1)) != 0) {
                                fprintf(fp, "\tmovq %d(%%rbp), %s\n", slot_off(&lay, part), xmms[xi++]);
                            } else {
                                fprintf(fp, "\tmovq %d(%%rbp), %s\n", slot_off(&lay, part), gprs[gi++]);
                            }
                        }
                    } else {
                        int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, int_reg_index(&ist, "%rax"), -1);
                        int_regs_flush(fp, f, &lay, &ist);
                        if (strcmp(ist.regs[rl], "%rax") != 0) {
                            fprintf(fp, "\tmovq %s, %%rax\n", ist.regs[rl]);
                        }
                    }
                } else {
                    int_regs_flush(fp, f, &lay, &ist);
                }
                fprintf(fp, "\tleave\n");
                if (emit_debug) {
                    fprintf(fp, "\t.cfi_def_cfa %%rsp, 8\n");
                }
                fprintf(fp, "\tret\n");
                break;
            }
        }

        if (emit_debug) {
            fprintf(fp, "\t.cfi_endproc\n");
        }
        fprintf(fp, ".size %s, .-%s\n", f->name, f->name);
        if ((f->attr_flags & CC_ATTR_CONSTRUCTOR) != 0) {
            emit_lifecycle_attr(fp, ".init_array", "init_array", f->name, 1);
        }
        if ((f->attr_flags & CC_ATTR_DESTRUCTOR) != 0) {
            emit_lifecycle_attr(fp, ".fini_array", "fini_array", f->name, 1);
        }
        int_regs_free(&ist);
        slot_layout_free(&lay);
    }

    return 0;
}

