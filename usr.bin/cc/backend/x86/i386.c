/*
 * i386.c: instruction selection for i386 (System V i386 psABI), from legal
 * IR -- 64-bit integers arrive split into words (legalize_i386.c), so only
 * their moves are 64-bit here; floating point is x87, or SSE with
 * -mfpmath=sse.
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
static const char *const emit_regs32[] = {"%eax", "%ecx", "%edx"};

/*
 * Where the first parameter is: above the return address, and above the
 * hidden pointer a function returning a struct is passed first.
 */
static int i386_first_param_offset(const cc_ssa_function_t *f) {
    return f->ret_abi == CC_CALL_ARG_ABI_AGG_MEM ? 12 : 8;
}

/* Stack bytes an integer parameter of size bytes takes: a multiple of 4. */
static int i386_param_slot_bytes(long size) {
    if (size <= 4) {
        return 4;
    }
    return (int)((size + 3) & ~3L);
}

/**
 * The stack alignment of an i386 argument or parameter with ABI byte
 * @p abi and @p size bytes: a vector's copy on the stack is aligned to its
 * size from 16 bytes up, as gcc places it, a _Complex _Float128's to 16;
 * everything else to 4.
 */
static long i386_stack_align(unsigned char abi, long size) {
    if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM && (abi & CC_ABI_STACK_VEC) != 0 && size >= 16) {
        return size;
    }
    if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM && (abi & CC_ABI_STACK_16) != 0) {
        return 16;
    }
    return 4;
}

/**
 * The i386 stack layout of a function's parameters: the offset from %ebp
 * of parameter @p want, or with @p want -1 the end of the first @p count.
 * A vector that comes in a register takes no stack.
 */
static int i386_param_layout(const cc_ssa_function_t *f, int count, int want) {
    /* offsets from the caller's %esp at the call (8 below %ebp): alignment is reckoned from there */
    int base = 8;
    long off = i386_first_param_offset(f) - base;
    int i;

    for (i = 0; i < count && i < (int)f->param_count; ++i) {
        unsigned char abi = f->param_abi != NULL ? f->param_abi[i] : CC_CALL_ARG_ABI_DEFAULT;
        long bytes;

        if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_LDOUBLE) {
            bytes = 12;
        } else if (abi == CC_CALL_ARG_ABI_FLOAT) {
            bytes = 4;
        } else if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_REG) {
            bytes = 0;
        } else if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM) {
            long size = x86_64_param_size_bytes(f, i, 4);

            off = (long)align_up_size((size_t)off, (size_t)i386_stack_align(abi, size));
            bytes = i386_param_slot_bytes(size);
        } else if (f->param_types[i] == CC_VAL_F64) {
            bytes = 8;
        } else {
            bytes = i386_param_slot_bytes(value_size_bytes(f, f->param_values != NULL ? f->param_values[i] : -1, 4));
        }
        if (i == want) {
            return base + (int)off;
        }
        off += bytes;
    }
    return base + (int)off;
}

static int i386_param_offset(const cc_ssa_function_t *f, int param_index) {
    return i386_param_layout(f, param_index + 1, param_index);
}

static int i386_variadic_start_offset(const cc_ssa_function_t *f, int fixed_count) {
    return i386_param_layout(f, fixed_count < 0 ? 0 : fixed_count, -1);
}

/** The register (%xmmN or %mmN) i386 vector parameter @p param_index came in: its rank among its kind. */
static int i386_vector_param_reg(const cc_ssa_function_t *f, int param_index) {
    unsigned char want = f->param_abi[param_index] & (CC_ABI_VEC128 | CC_ABI_VEC64);
    int n = 0;
    int i;

    for (i = 0; i < param_index; ++i) {
        if (CC_ABI_KIND(f->param_abi[i]) == CC_CALL_ARG_ABI_AGG_REG &&
            (f->param_abi[i] & (CC_ABI_VEC128 | CC_ABI_VEC64)) == want) {
            ++n;
        }
    }
    return n;
}

static int function_uses_f64(const cc_ssa_function_t *f) {
    int i;
    if (f->ret_type == CC_VAL_F64) {
        return 1;
    }
    for (i = 0; i < (int)f->param_count; ++i) {
        if (f->param_types[i] == CC_VAL_F64) {
            return 1;
        }
    }
    for (i = 0; i < f->value_count; ++i) {
        if (cc_ssa_value_class(f, i) == CC_VAL_F64) {
            return 1;
        }
    }
    return 0;
}

static const char *i386_fp_tmp_reg(void) {
    return "%xmm7";
}

static int i386_has_ud2(void) {
    return cc_options_current()->i386_isa_level >= 6;
}

static const char *i386_float_binop_mnemonic(cc_ssa_opcode_t op, int use_x87) {
    switch (op) {
    case CC_SSA_ADD:
        return use_x87 ? "faddl" : "addsd";
    case CC_SSA_SUB:
        return use_x87 ? "fsubl" : "subsd";
    case CC_SSA_MUL:
        return use_x87 ? "fmull" : "mulsd";
    case CC_SSA_DIV:
        return use_x87 ? "fdivl" : "divsd";
    default:
        return NULL;
    }
}

static void emit_i386_sse_load_f64(FILE *fp, const slot_layout_t *lay, int value, const char *xmm) {
    fprintf(fp, "\tmovsd %d(%%ebp), %s\n", slot_off(lay, value), xmm);
}

static void emit_i386_sse_store_f64(FILE *fp, const slot_layout_t *lay, const char *xmm, int value) {
    fprintf(fp, "\tmovsd %s, %d(%%ebp)\n", xmm, slot_off(lay, value));
}

/**
 * Whether an i386 floating value is a long double.  A long double is kept
 * in its slot in x87's 80-bit form (the slots are 16 bytes then, see
 * i386_function_has_ext()), so it keeps its 64-bit mantissa between
 * operations; a double is kept as a double.
 *
 * @param f  the function
 * @param v  the value
 * @return   1 or 0
 */
static int i386_fp_is_ext(const cc_ssa_function_t *f, int v) {
    return v >= 0 && v < f->value_count && cc_ssa_value_class(f, v) == CC_VAL_F64 && value_size_bytes(f, v, 8) >= 10;
}

/**
 * Whether an instruction reads or writes a long double, and so must be
 * done on the x87 even where doubles use SSE.
 */
static int i386_instr_has_ext(const cc_ssa_function_t *f, const cc_ssa_instr_t *in) {
    return i386_fp_is_ext(f, in->dst) || i386_fp_is_ext(f, in->lhs) || i386_fp_is_ext(f, in->rhs);
}

/** Whether a function has any long double value (its slots must then hold 80 bits). */
static int i386_function_has_ext(const cc_ssa_function_t *f) {
    int v;

    for (v = 0; v < f->value_count; ++v) {
        if (i386_fp_is_ext(f, v)) {
            return 1;
        }
    }
    return 0;
}

/** Push a floating value onto the x87 stack from its slot: 80 bits for a long double, else a double. */
static void emit_i386_x87_load_f64(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int value) {
    fprintf(fp, "\t%s %d(%%ebp)\n", i386_fp_is_ext(f, value) ? "fldt" : "fldl", slot_off(lay, value));
}

/** Pop the x87 stack top into a floating value's slot, rounding to double unless it is a long double. */
static void emit_i386_x87_store_f64(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int value) {
    fprintf(fp, "\t%s %d(%%ebp)\n", i386_fp_is_ext(f, value) ? "fstpt" : "fstpl", slot_off(lay, value));
}

static int emit_i386_float_binop_sse(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in, cc_diag_t *diag) {
    const char *op = i386_float_binop_mnemonic(in->op, 0);
    const char *tmp = i386_fp_tmp_reg();

    if (op == NULL) {
        set_diag(diag, "invalid floating arithmetic opcode");
        return -1;
    }

    emit_i386_sse_load_f64(fp, lay, in->lhs, tmp);
    fprintf(fp, "\t%s %d(%%ebp), %s\n", op, slot_off(lay, in->rhs), tmp);
    emit_i386_sse_store_f64(fp, lay, tmp, in->dst);
    return 0;
}

static int emit_i386_float_binop_x87(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                     const cc_ssa_instr_t *in, cc_diag_t *diag) {
    const char *op = i386_float_binop_mnemonic(in->op, 1);

    if (op == NULL) {
        set_diag(diag, "invalid floating arithmetic opcode");
        return -1;
    }

    if (i386_fp_is_ext(f, in->rhs)) {
        /* no x87 arithmetic takes an 80-bit memory operand: both on the stack */
        const char *pop = NULL;

        if (in->op == CC_SSA_ADD) pop = "faddp";
        else if (in->op == CC_SSA_SUB) pop = "fsubrp";
        else if (in->op == CC_SSA_MUL) pop = "fmulp";
        else if (in->op == CC_SSA_DIV) pop = "fdivrp";
        if (pop == NULL) {
            set_diag(diag, "invalid long double arithmetic opcode");
            return -1;
        }
        emit_i386_x87_load_f64(fp, f, lay, in->lhs);
        emit_i386_x87_load_f64(fp, f, lay, in->rhs);
        fprintf(fp, "\t%s %%st, %%st(1)\n", pop);
        emit_i386_x87_store_f64(fp, f, lay, in->dst);
        return 0;
    }
    emit_i386_x87_load_f64(fp, f, lay, in->lhs);
    fprintf(fp, "\t%s %d(%%ebp)\n", op, slot_off(lay, in->rhs));
    emit_i386_x87_store_f64(fp, f, lay, in->dst);
    return 0;
}

static void emit_i386_cmp_float_flags_sse(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    const char *tmp = i386_fp_tmp_reg();
    emit_i386_sse_load_f64(fp, lay, in->lhs, tmp);
    fprintf(fp, "\tucomisd %d(%%ebp), %s\n", slot_off(lay, in->rhs), tmp);
}

static void emit_i386_cmp_float_flags_x87(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                          const cc_ssa_instr_t *in) {
    emit_i386_x87_load_f64(fp, f, lay, in->rhs);
    emit_i386_x87_load_f64(fp, f, lay, in->lhs);
    fprintf(fp, "\tfucompp\n");
    fprintf(fp, "\tfnstsw %%ax\n");
    fprintf(fp, "\tsahf\n");
}

static int emit_i386_cmp_float_result(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                      const cc_ssa_instr_t *in) {
    int rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    emit_float_setcc_to_reg(fp, in->cmp_kind, ist->regs[rd], 0);
    return 0;
}

static int emit_i386_cmp_float_sse(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                   const cc_ssa_instr_t *in) {
    int_regs_flush(fp, f, lay, ist);
    emit_i386_cmp_float_flags_sse(fp, lay, in);
    return emit_i386_cmp_float_result(fp, f, lay, ist, in);
}

static int emit_i386_cmp_float_x87(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                   const cc_ssa_instr_t *in) {
    int eax = int_reg_index(ist, "%eax");

    int_regs_flush(fp, f, lay, ist);
    int_regs_clobber_reg(fp, f, lay, ist, eax);
    emit_i386_cmp_float_flags_x87(fp, f, lay, in);
    return emit_i386_cmp_float_result(fp, f, lay, ist, in);
}

static int emit_i386_cmp_int(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                             const cc_ssa_instr_t *in) {
    const char *m = setcc_int_mnemonic(in->cmp_kind, in->is_unsigned);
    int rl;
    int rr;
    int rd;

    rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    rr = int_regs_load(fp, f, lay, ist, in->rhs, -1, rl);
    rd = int_regs_define(fp, f, lay, ist, in->dst, rl, rr);

    fprintf(fp, "\tcmpl %s, %s\n", ist->regs[rr], ist->regs[rl]);
    fprintf(fp, "\t%s %s\n", m, reg32_to8(ist->regs[rd]));
    emit_setcc_zext_to_reg(fp, ist->regs[rd], 0);
    return 0;
}

static void emit_i386_i2f_sse(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                              const cc_ssa_instr_t *in) {
    const char *tmp = i386_fp_tmp_reg();
    if (in->is_unsigned) {
        unsigned long lid = next_aux_label_id();
        int eax = int_reg_index(ist, "%eax");
        int ecx = int_reg_index(ist, "%ecx");

        int_regs_flush(fp, f, lay, ist);
        int_regs_clobber_reg(fp, f, lay, ist, eax);
        int_regs_clobber_reg(fp, f, lay, ist, ecx);
        fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(lay, in->lhs));
        fprintf(fp, "\ttestl %%eax, %%eax\n");
        fprintf(fp, "\tjns .L__cc_u32tof_small_%lu\n", lid);
        fprintf(fp, "\tmovl %%eax, %%ecx\n");
        fprintf(fp, "\tshrl $1, %%ecx\n");
        fprintf(fp, "\tandl $1, %%eax\n");
        fprintf(fp, "\torl %%eax, %%ecx\n");
        fprintf(fp, "\tcvtsi2sdl %%ecx, %s\n", tmp);
        fprintf(fp, "\taddsd %s, %s\n", tmp, tmp);
        fprintf(fp, "\tjmp .L__cc_u32tof_done_%lu\n", lid);
        fprintf(fp, ".L__cc_u32tof_small_%lu:\n", lid);
        fprintf(fp, "\tcvtsi2sdl %%eax, %s\n", tmp);
        fprintf(fp, ".L__cc_u32tof_done_%lu:\n", lid);
    } else {
        int rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
        fprintf(fp, "\tcvtsi2sdl %s, %s\n", ist->regs[rl], tmp);
    }
    fprintf(fp, "\tmovsd %s, %d(%%ebp)\n", tmp, slot_off(lay, in->dst));
}

static void emit_i386_i2f_x87(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                              const cc_ssa_instr_t *in, int scratch_off) {
    if (value_size_bytes(f, in->lhs, 4) >= 8 && !is_stackalloc_value(lay, in->lhs)) {
        /* a 64-bit integer: all of it from its slot; unsigned with the top bit set is 2^64 more */
        int_regs_flush(fp, f, lay, ist);
        fprintf(fp, "\tfildll %d(%%ebp)\n", slot_off(lay, in->lhs));
        if (in->is_unsigned) {
            unsigned long lid = next_aux_label_id();

            fprintf(fp, "\tcmpl $0, %d(%%ebp)\n", slot_off(lay, in->lhs) + 4);
            fprintf(fp, "\tjge .L__cc_u64tox87_%lu\n", lid);
            fprintf(fp, "\tmovl $0x5f800000, %d(%%ebp)\n", scratch_off);
            fprintf(fp, "\tfadds %d(%%ebp)\n", scratch_off);
            fprintf(fp, ".L__cc_u64tox87_%lu:\n", lid);
        }
        emit_i386_x87_store_f64(fp, f, lay, in->dst);
        return;
    }
    if (in->is_unsigned) {
        unsigned long lid = next_aux_label_id();
        int eax = int_reg_index(ist, "%eax");
        int ecx = int_reg_index(ist, "%ecx");

        int_regs_flush(fp, f, lay, ist);
        int_regs_clobber_reg(fp, f, lay, ist, eax);
        int_regs_clobber_reg(fp, f, lay, ist, ecx);
        fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(lay, in->lhs));
        fprintf(fp, "\ttestl %%eax, %%eax\n");
        fprintf(fp, "\tjns .L__cc_u32tox87_small_%lu\n", lid);
        fprintf(fp, "\tmovl %%eax, %%ecx\n");
        fprintf(fp, "\tshrl $1, %%ecx\n");
        fprintf(fp, "\tandl $1, %%eax\n");
        fprintf(fp, "\torl %%eax, %%ecx\n");
        fprintf(fp, "\tmovl %%ecx, %d(%%ebp)\n", scratch_off);
        fprintf(fp, "\tfildl %d(%%ebp)\n", scratch_off);
        fprintf(fp, "\tfadd %%st(0), %%st\n");
        fprintf(fp, "\tjmp .L__cc_u32tox87_done_%lu\n", lid);
        fprintf(fp, ".L__cc_u32tox87_small_%lu:\n", lid);
        fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", scratch_off);
        fprintf(fp, "\tfildl %d(%%ebp)\n", scratch_off);
        fprintf(fp, ".L__cc_u32tox87_done_%lu:\n", lid);
    } else {
        int rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
        fprintf(fp, "\tmovl %s, %d(%%ebp)\n", ist->regs[rl], scratch_off);
        fprintf(fp, "\tfildl %d(%%ebp)\n", scratch_off);
    }
    emit_i386_x87_store_f64(fp, f, lay, in->dst);
}

/*
 * Floating to integer, truncating (C's conversion) whatever the x87
 * rounding mode.  The conversion is always to 64 bits, so an unsigned
 * int above INT_MAX and a long long come out whole; the result goes
 * straight to the destination's slot, as the flush leaves no register
 * bound.
 */
static void emit_i386_f2i_x87(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                              const cc_ssa_instr_t *in, int scratch_off) {
    int dst = slot_off(lay, in->dst);
    int is_u64 = value_size_bytes(f, in->dst, 4) >= 8 &&
                 (in->is_unsigned || cc_ssa_value_is_unsigned(f, in->dst));

    int_regs_flush(fp, f, lay, ist);
    fprintf(fp, "\tfnstcw %d(%%ebp)\n", scratch_off);
    fprintf(fp, "\tmovw %d(%%ebp), %%ax\n", scratch_off);
    fprintf(fp, "\torw $0x0c00, %%ax\n");
    fprintf(fp, "\tmovw %%ax, %d(%%ebp)\n", scratch_off + 2);
    fprintf(fp, "\tfldcw %d(%%ebp)\n", scratch_off + 2);
    emit_i386_x87_load_f64(fp, f, lay, in->lhs);
    emit_x87_store_i64(fp, "%ebp", scratch_off + 4, scratch_off + 12, is_u64);
    fprintf(fp, "\tfldcw %d(%%ebp)\n", scratch_off);
    fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", scratch_off + 4);
    fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", dst);
    if (value_size_bytes(f, in->dst, 4) >= 8) {
        fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", scratch_off + 8);
        fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", dst + 4);
    }
}

static void emit_i386_fround32_sse(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    const char *tmp = i386_fp_tmp_reg();
    fprintf(fp, "\tmovsd %d(%%ebp), %s\n", slot_off(lay, in->lhs), tmp);
    fprintf(fp, "\tcvtsd2ss %s, %s\n", tmp, tmp);
    fprintf(fp, "\tcvtss2sd %s, %s\n", tmp, tmp);
    fprintf(fp, "\tmovsd %s, %d(%%ebp)\n", tmp, slot_off(lay, in->dst));
}

static void emit_i386_fround32_x87(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                   const cc_ssa_instr_t *in, int scratch_off) {
    emit_i386_x87_load_f64(fp, f, lay, in->lhs);
    fprintf(fp, "\tfstps %d(%%ebp)\n", scratch_off);
    fprintf(fp, "\tflds %d(%%ebp)\n", scratch_off);
    emit_i386_x87_store_f64(fp, f, lay, in->dst);
}

/**
 * Load the `_Float16` at the address in register @p reg into value @p dst
 * as a double: libgcc's __extendhfdf2, which takes the half on the stack
 * and returns the double in st(0), as gcc calls it.
 */
static void emit_i386_load_half(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                const char *reg, int dst) {
    int_regs_flush(fp, f, lay, ist);
    fprintf(fp, "\tmovzwl (%s), %%eax\n", reg);
    fprintf(fp, "\tsubl $12, %%esp\n");
    fprintf(fp, "\tpushl %%eax\n");
    fprintf(fp, "\tcall __extendhfdf2\n");
    fprintf(fp, "\taddl $16, %%esp\n");
    fprintf(fp, "\tfstpl %d(%%ebp)\n", slot_off(lay, dst));
}

/**
 * Store double value @p v as the `_Float16` at the address in register
 * @p reg: libgcc's __truncdfhf2, which takes the double on the stack and
 * returns the half in %xmm0, as gcc calls it.
 */
static void emit_i386_store_half(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                 const char *reg, int v) {
    int_regs_flush(fp, f, lay, ist);
    fprintf(fp, "\tsubl $16, %%esp\n");
    fprintf(fp, "\tmovl %s, 8(%%esp)\n", reg);
    fprintf(fp, "\tfldl %d(%%ebp)\n", slot_off(lay, v));
    fprintf(fp, "\tfstpl (%%esp)\n");
    fprintf(fp, "\tcall __truncdfhf2\n");
    fprintf(fp, "\tmovl 8(%%esp), %%edx\n");
    fprintf(fp, "\taddl $16, %%esp\n");
    fprintf(fp, "\tmovd %%xmm0, %%eax\n");
    fprintf(fp, "\tmovw %%ax, (%%edx)\n");
}

static void emit_i386_load_fp_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                       const cc_ssa_instr_t *in, int use_x87_fp) {
    long mem_size = in->imm > 0 ? in->imm : 8;
    int rp = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);

    if (mem_size == 2) {
        emit_i386_load_half(fp, f, lay, ist, ist->regs[rp], in->dst);
        return;
    }
    if (use_x87_fp) {
        if (mem_size == 4) {
            fprintf(fp, "\tflds (%s)\n", ist->regs[rp]);
        } else if (mem_size >= 10) {
            fprintf(fp, "\tfldt (%s)\n", ist->regs[rp]);
        } else {
            fprintf(fp, "\tfldl (%s)\n", ist->regs[rp]);
        }
        emit_i386_x87_store_f64(fp, f, lay, in->dst);
    } else {
        const char *tmp = i386_fp_tmp_reg();
        if (mem_size == 4) {
            fprintf(fp, "\tmovss (%s), %s\n", ist->regs[rp], tmp);
            fprintf(fp, "\tcvtss2sd %s, %s\n", tmp, tmp);
        } else if (mem_size >= 10) {
            fprintf(fp, "\tfldt (%s)\n", ist->regs[rp]);
            fprintf(fp, "\tfstpl %d(%%ebp)\n", slot_off(lay, in->dst));
            return;
        } else {
            fprintf(fp, "\tmovsd (%s), %s\n", ist->regs[rp], tmp);
        }
        fprintf(fp, "\tmovsd %s, %d(%%ebp)\n", tmp, slot_off(lay, in->dst));
    }
}

static void emit_i386_store_fp_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                        const cc_ssa_instr_t *in, int use_x87_fp) {
    long mem_size = in->imm > 0 ? in->imm : 8;
    int rp = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);

    if (mem_size == 2) {
        emit_i386_store_half(fp, f, lay, ist, ist->regs[rp], in->rhs);
        return;
    }
    if (use_x87_fp) {
        emit_i386_x87_load_f64(fp, f, lay, in->rhs);
        if (mem_size == 4) {
            fprintf(fp, "\tfstps (%s)\n", ist->regs[rp]);
        } else if (mem_size >= 10) {
            fprintf(fp, "\tfstpt (%s)\n", ist->regs[rp]);
        } else {
            fprintf(fp, "\tfstpl (%s)\n", ist->regs[rp]);
        }
    } else {
        const char *tmp = i386_fp_tmp_reg();
        {
            fprintf(fp, "\tmovsd %d(%%ebp), %s\n", slot_off(lay, in->rhs), tmp);
            if (mem_size == 4) {
                fprintf(fp, "\tcvtsd2ss %s, %s\n", tmp, tmp);
                fprintf(fp, "\tmovss %s, (%s)\n", tmp, ist->regs[rp]);
            } else if (mem_size >= 10) {
                fprintf(fp, "\tfldl %d(%%ebp)\n", slot_off(lay, in->rhs));
                fprintf(fp, "\tfstpt (%s)\n", ist->regs[rp]);
            } else {
                fprintf(fp, "\tmovsd %s, (%s)\n", tmp, ist->regs[rp]);
            }
        }
    }
}

static void emit_i386_load_int_value_to_reg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int value,
                                             const char *reg) {
    if (value < 0 || value >= f->value_count) {
        fprintf(fp, "\tmovl $0, %s\n", reg);
        return;
    }
    if (is_stackalloc_value(lay, value)) {
        fprintf(fp, "\tleal %d(%%ebp), %s\n", stackalloc_off(lay, value), reg);
    } else {
        fprintf(fp, "\tmovl %d(%%ebp), %s\n", slot_off(lay, value), reg);
    }
}

static void emit_i386_store_aggregate_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                               const cc_ssa_instr_t *in) {
    long mem_size = in->imm > 0 ? in->imm : 4;
    emit_i386_load_int_value_to_reg(fp, f, lay, in->lhs, "%eax");
    emit_i386_load_int_value_to_reg(fp, f, lay, in->rhs, "%ecx");
    fprintf(fp, "\tpushl $%ld\n", mem_size);
    fprintf(fp, "\tpushl %%ecx\n");
    fprintf(fp, "\tpushl %%eax\n");
    fprintf(fp, "\tcall memcpy\n");
    fprintf(fp, "\taddl $12, %%esp\n");
}

static void emit_i386_store_scalar_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                            const int_reg_state_t *ist, const cc_ssa_instr_t *in, int rp, int rv,
                                            long mem_size, int is_unsigned) {
    if (mem_size == 1) {
        fprintf(fp, "\tmovb %s, (%s)\n", reg32_to8(ist->regs[rv]), ist->regs[rp]);
    } else if (mem_size == 2) {
        fprintf(fp, "\tmovw %s, (%s)\n", reg32_to16(ist->regs[rv]), ist->regs[rp]);
    } else if (mem_size == 8) {
        fprintf(fp, "\tmovl %s, (%s)\n", ist->regs[rv], ist->regs[rp]);
        if (in != NULL && in->rhs >= 0 && in->rhs < f->value_count && value_size_bytes(f, in->rhs, 4) >= 8 &&
            !is_stackalloc_value(lay, in->rhs)) {
            const char *scratch = strcmp(ist->regs[rp], "%eax") == 0 ? "%edx" : "%eax";
            fprintf(fp, "\tmovl %d(%%ebp), %s\n", slot_off(lay, in->rhs) + 4, scratch);
            fprintf(fp, "\tmovl %s, 4(%s)\n", scratch, ist->regs[rp]);
            if (strcmp(ist->regs[rv], scratch) == 0) {
                fprintf(fp, "\tmovl %d(%%ebp), %s\n", slot_off(lay, in->rhs), scratch);
            }
        } else if (is_unsigned) {
            fprintf(fp, "\tmovl $0, 4(%s)\n", ist->regs[rp]);
        } else {
            fprintf(fp, "\tmovl %s, 4(%s)\n", ist->regs[rv], ist->regs[rp]);
            fprintf(fp, "\tsarl $31, 4(%s)\n", ist->regs[rp]);
        }
    } else {
        fprintf(fp, "\tmovl %s, (%s)\n", ist->regs[rv], ist->regs[rp]);
    }
}

static void emit_i386_const_f64(FILE *fp, const slot_layout_t *lay, int dst, double value) {
    union {
        double d;
        uint64_t u;
    } cvt;
    uint32_t lo;
    uint32_t hi;

    cvt.d = value;
    lo = (uint32_t)(cvt.u & 0xffffffffu);
    hi = (uint32_t)(cvt.u >> 32);
    fprintf(fp, "\tmovl $0x%x, %d(%%ebp)\n", lo, slot_off(lay, dst));
    fprintf(fp, "\tmovl $0x%x, %d(%%ebp)\n", hi, slot_off(lay, dst) + 4);
}

static void emit_i386_const_i32(FILE *fp, const int_reg_state_t *ist, int rd, long long imm) {
    /* the low word; a 64-bit constant's high word goes to its slot separately */
    fprintf(fp, "\tmovl $%ld, %s\n", (long)(int)(unsigned int)imm, ist->regs[rd]);
}

static void emit_i386_mov_fp(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, const cc_ssa_instr_t *in,
                             int use_x87_fp) {
    if (use_x87_fp) {
        emit_i386_x87_load_f64(fp, f, lay, in->lhs);
        emit_i386_x87_store_f64(fp, f, lay, in->dst);
    } else {
        const char *tmp = i386_fp_tmp_reg();
        fprintf(fp, "\tmovsd %d(%%ebp), %s\n", slot_off(lay, in->lhs), tmp);
        fprintf(fp, "\tmovsd %s, %d(%%ebp)\n", tmp, slot_off(lay, in->dst));
    }
}

static void emit_i386_addr_of_local(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                    const cc_ssa_instr_t *in) {
    int rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    fprintf(fp, "\tleal %d(%%ebp), %s\n", slot_off(lay, in->lhs), ist->regs[rd]);
}

static void emit_i386_addr_of_global_sym(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                         const cc_ssa_instr_t *in) {
    int rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    fprintf(fp, "\tmovl $%s, %s\n", in->sym, ist->regs[rd]);
}

static void emit_i386_addr_of_local_label(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                          const cc_ssa_instr_t *in) {
    int rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    fprintf(fp, "\tmovl $");
    emit_local_label(fp, f->name, in->label);
    fprintf(fp, ", %s\n", ist->regs[rd]);
}

static void emit_i386_addr_of_string_label(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                           int dst, size_t fn_index, size_t instr_index) {
    char label[96];
    int rd;
    snprintf(label, sizeof(label), ".L__cc_str_%zu_%zu", fn_index, instr_index);
    rd = int_regs_define(fp, f, lay, ist, dst, -1, -1);
    fprintf(fp, "\tmovl $%s, %s\n", label, ist->regs[rd]);
}

static void emit_i386_load_scalar_indirect(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in, int_reg_state_t *ist,
                                           int rp, int rd, long mem_size, int is_unsigned) {
    if (mem_size == 1) {
        fprintf(fp, "\t%s (%s), %s\n", is_unsigned ? "movzbl" : "movsbl", ist->regs[rp], ist->regs[rd]);
    } else if (mem_size == 2) {
        fprintf(fp, "\t%s (%s), %s\n", is_unsigned ? "movzwl" : "movswl", ist->regs[rp], ist->regs[rd]);
    } else {
        fprintf(fp, "\tmovl (%s), %s\n", ist->regs[rp], ist->regs[rd]);
    }
    if (mem_size == 8 && in != NULL) {
        const char *scratch = strcmp(ist->regs[rp], "%eax") == 0 ? "%edx" : "%eax";
        fprintf(fp, "\tmovl 4(%s), %s\n", ist->regs[rp], scratch);
        fprintf(fp, "\tmovl %s, %d(%%ebp)\n", scratch, slot_off(lay, in->dst) + 4);
        if (strcmp(ist->regs[rd], scratch) == 0) {
            fprintf(fp, "\tmovl (%s), %s\n", ist->regs[rp], scratch);
        }
    }
}

static int i386_load_clean_lhs(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist, int lhs) {
    int rl = int_regs_load(fp, f, lay, ist, lhs, -1, -1);
    if (rl >= 0 && ist->reg_dirty[rl]) {
        int_regs_spill_reg(fp, f, lay, ist, rl);
        rl = int_regs_load(fp, f, lay, ist, lhs, rl, -1);
    }
    return rl;
}

static int i386_move_lhs_to_eax(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                const cc_ssa_instr_t *in, int rl, int rax, cc_diag_t *diag) {
    if (rax < 0) {
        set_diag(diag, "i386 register set missing %eax");
        return -1;
    }
    if (rl == rax) {
        return rl;
    }
    {
        int lhs_dirty = ist->reg_dirty[rl];
        int_regs_clobber_reg(fp, f, lay, ist, rax);
        fprintf(fp, "\tmovl %s, %%eax\n", ist->regs[rl]);
        int_regs_bind(ist, in->lhs, rax, lhs_dirty);
    }
    return rax;
}

static int i386_bind_shift_count_in_ecx(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                        const cc_ssa_instr_t *in, int rl, int rcx, cc_diag_t *diag) {
    int rr_shift;
    int rhs_dirty;

    if (rcx < 0) {
        set_diag(diag, "i386 register set missing %ecx");
        return -1;
    }
    if (rl == rcx) {
        int alt = int_regs_load(fp, f, lay, ist, in->lhs, -1, rcx);
        if (alt == rcx) {
            int lhs_dirty = ist->reg_dirty[rcx];
            alt = int_regs_define(fp, f, lay, ist, in->lhs, -1, rcx);
            fprintf(fp, "\tmovl %%ecx, %s\n", ist->regs[alt]);
            ist->reg_dirty[alt] = (unsigned char)(lhs_dirty ? 1 : 0);
        }
        rl = alt;
    }
    int_regs_clobber_reg(fp, f, lay, ist, rcx);
    rr_shift = int_regs_load(fp, f, lay, ist, in->rhs, rcx, rl);
    rhs_dirty = ist->reg_dirty[rr_shift];
    if (rr_shift != rcx) {
        fprintf(fp, "\tmovl %s, %%ecx\n", ist->regs[rr_shift]);
        int_regs_bind(ist, in->rhs, rcx, rhs_dirty);
    }
    return rl;
}

static int i386_emit_div(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                         const cc_ssa_instr_t *in, int rl, int rcx, cc_diag_t *diag) {
    int rax = int_reg_index(ist, "%eax");
    int rdx = int_reg_index(ist, "%edx");
    int rr_div;

    if (rax < 0 || rdx < 0) {
        set_diag(diag, "i386 register set missing %eax/%edx");
        return -1;
    }
    rl = i386_move_lhs_to_eax(fp, f, lay, ist, in, rl, rax, diag);
    if (rl < 0) {
        return -1;
    }

    int_regs_clobber_reg(fp, f, lay, ist, rdx);
    rr_div = int_regs_load(fp, f, lay, ist, in->rhs, -1, rax);
    if (rr_div == rdx) {
        int rhs_dirty = ist->reg_dirty[rr_div];
        if (rcx < 0 || rcx == rax || rcx == rdx) {
            set_diag(diag, "i386 register set missing divisor scratch register");
            return -1;
        }
        int_regs_clobber_reg(fp, f, lay, ist, rcx);
        fprintf(fp, "\tmovl %%edx, %%ecx\n");
        int_regs_bind(ist, in->rhs, rcx, rhs_dirty);
        rr_div = rcx;
    }

    if (in->is_unsigned) {
        fprintf(fp, "\txorl %%edx, %%edx\n");
        fprintf(fp, "\tdivl %s\n", ist->regs[rr_div]);
    } else {
        fprintf(fp, "\tcltd\n");
        fprintf(fp, "\tidivl %s\n", ist->regs[rr_div]);
    }
    int_regs_bind(ist, in->dst, rax, 1);
    return 0;
}

static int i386_emit_shift(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                           const cc_ssa_instr_t *in, int rl, int rcx, cc_diag_t *diag) {
    const char *mnemonic = in->op == CC_SSA_SHL ? "shll" : (in->is_unsigned ? "shrl" : "sarl");
    if (in->rhs < 0) {
        unsigned sh = (unsigned)(in->imm & 31);
        fprintf(fp, "\t%s $%u, %s\n", mnemonic, sh, ist->regs[rl]);
        int_regs_remap_dst(fp, f, lay, ist, in->dst, rl);
        return 0;
    }

    rl = i386_bind_shift_count_in_ecx(fp, f, lay, ist, in, rl, rcx, diag);
    if (rl < 0) {
        return -1;
    }
    fprintf(fp, "\tandl $31, %%ecx\n");
    fprintf(fp, "\t%s %%cl, %s\n", mnemonic, ist->regs[rl]);
    int_regs_remap_dst(fp, f, lay, ist, in->dst, rl);
    return 0;
}

static int emit_i386_int_div_or_shift(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                      const cc_ssa_instr_t *in, cc_diag_t *diag) {
    int rl = i386_load_clean_lhs(fp, f, lay, ist, in->lhs);
    int rcx = int_reg_index(ist, "%ecx");

    if (in->op == CC_SSA_DIV) {
        return i386_emit_div(fp, f, lay, ist, in, rl, rcx, diag);
    }
    return i386_emit_shift(fp, f, lay, ist, in, rl, rcx, diag);
}

static const char *i386_int_binop_mnemonic(cc_ssa_opcode_t op) {
    switch (op) {
    case CC_SSA_ADD:
        return "addl";
    case CC_SSA_SUB:
        return "subl";
    case CC_SSA_MUL:
        return "imull";
    case CC_SSA_AND:
        return "andl";
    case CC_SSA_OR:
        return "orl";
    case CC_SSA_XOR:
        return "xorl";
    default:
        return NULL;
    }
}

static void emit_i386_int_binop_regular(FILE *fp, const int_reg_state_t *ist, const cc_ssa_instr_t *in, int rr, int rl) {
    const char *m = i386_int_binop_mnemonic(in->op);
    if (m != NULL) {
        fprintf(fp, "\t%s %s, %s\n", m, ist->regs[rr], ist->regs[rl]);
    }
}

static void emit_i386_label_def(FILE *fp, const char *fn, int label) {
    emit_local_label(fp, fn, label);
    fprintf(fp, ":\n");
}

static void emit_i386_jcc_label(FILE *fp, const char *cc, const char *fn, int label) {
    fprintf(fp, "\t%s ", cc);
    emit_local_label(fp, fn, label);
    fprintf(fp, "\n");
}

static void emit_i386_jump_label(FILE *fp, const char *fn, int label) {
    emit_i386_jcc_label(fp, "jmp", fn, label);
}

static void emit_i386_branch_cond(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                  const cc_ssa_instr_t *in) {
    int rc = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    fprintf(fp, "\tcmpl $0, %s\n", ist->regs[rc]);
    int_regs_flush(fp, f, lay, ist);
    emit_i386_jcc_label(fp, "jne", f->name, in->true_label);
    emit_i386_jump_label(fp, f->name, in->false_label);
}

static void emit_i386_va_start(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                               const cc_ssa_instr_t *in) {
    int poff = i386_variadic_start_offset(f, (int)in->imm);
    int rd;
    int_regs_flush(fp, f, lay, ist);
    rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    fprintf(fp, "\tleal %d(%%ebp), %s\n", poff, ist->regs[rd]);
}

static void emit_i386_normalize_int_return(FILE *fp, long ret_bytes, int is_unsigned) {
    if (ret_bytes == 1)
        fprintf(fp, "\t%s %%al, %%eax\n", is_unsigned ? "movzbl" : "movsbl");
    else if (ret_bytes == 2)
        fprintf(fp, "\t%s %%ax, %%eax\n", is_unsigned ? "movzwl" : "movswl");
}

static void emit_i386_push_slot(FILE *fp, int off) {
    fprintf(fp, "\tpushl %d(%%ebp)\n", off);
}

/**
 * The stack bytes emit_i386_push_call_arg() pushes for an argument, for
 * aligning the stack before the pushes.  Keep the two in step.
 */
static long i386_call_arg_bytes(const cc_ssa_function_t *f, const slot_layout_t *lay, const cc_ssa_instr_t *in,
                                size_t arg_index, int value) {
    long arg_size = call_arg_size_bytes(in, arg_index, value_size_bytes(f, value, 4));

    if (call_arg_is_ldouble_abi(in, arg_index)) {
        return 12;
    }
    if (in->call_arg_abi != NULL && in->call_arg_abi[arg_index] == CC_CALL_ARG_ABI_FLOAT) {
        return 4;
    }
    if (call_arg_is_agg_reg_abi(in, arg_index)) {
        /* a vector in %xmm0-2 or %mm0-2 */
        return 0;
    }
    if (call_arg_is_agg_mem_abi(in, arg_index)) {
        return i386_param_slot_bytes(arg_size);
    }
    if (i386_fp_is_ext(f, value) || cc_ssa_value_class(f, value) == CC_VAL_F64) {
        return 8;
    }
    if (is_stackalloc_value(lay, value)) {
        return 4;
    }
    return arg_size >= 8 ? 8 : 4;
}

static void emit_i386_push_call_arg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                    const cc_ssa_instr_t *in, size_t arg_index, int value, long *stack_bytes) {
    int off = slot_off(lay, value);
    long arg_size = call_arg_size_bytes(in, arg_index, value_size_bytes(f, value, 4));

    if (call_arg_is_ldouble_abi(in, arg_index)) {
        fprintf(fp, "\tsubl $12, %%esp\n");
        emit_i386_x87_load_f64(fp, f, lay, value);
        fprintf(fp, "\tfstpt (%%esp)\n");
        fprintf(fp, "\tmovw $0, 10(%%esp)\n");
        *stack_bytes += 12;
    } else if (in->call_arg_abi != NULL && in->call_arg_abi[arg_index] == CC_CALL_ARG_ABI_FLOAT) {
        /* a float parameter takes 4 bytes of single precision */
        fprintf(fp, "\tsubl $4, %%esp\n");
        emit_i386_x87_load_f64(fp, f, lay, value);
        fprintf(fp, "\tfstps (%%esp)\n");
        *stack_bytes += 4;
    } else if (call_arg_is_agg_mem_abi(in, arg_index)) {
        /* i386 psABI: a struct argument is a copy of it on the stack */
        long words = i386_param_slot_bytes(arg_size) / 4;
        long w;
        emit_load_ptr_value_reg(fp, lay, value, 0, "%eax");
        for (w = words - 1; w >= 0; --w) {
            fprintf(fp, "\tpushl %ld(%%eax)\n", w * 4);
        }
        *stack_bytes += words * 4;
    } else if (i386_fp_is_ext(f, value)) {
        /* a long double passed as a double: rounded */
        fprintf(fp, "\tsubl $8, %%esp\n");
        emit_i386_x87_load_f64(fp, f, lay, value);
        fprintf(fp, "\tfstpl (%%esp)\n");
        *stack_bytes += 8;
    } else if (cc_ssa_value_class(f, value) == CC_VAL_F64) {
        emit_i386_push_slot(fp, off + 4);
        emit_i386_push_slot(fp, off);
        *stack_bytes += 8;
    } else if (is_stackalloc_value(lay, value)) {
        fprintf(fp, "\tleal %d(%%ebp), %%eax\n", stackalloc_off(lay, value));
        fprintf(fp, "\tpushl %%eax\n");
        *stack_bytes += 4;
    } else if (arg_size >= 8) {
        emit_i386_push_slot(fp, off + 4);
        emit_i386_push_slot(fp, off);
        *stack_bytes += 8;
    } else {
        emit_i386_push_slot(fp, off);
        *stack_bytes += 4;
    }
}

static void emit_i386_call_target(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                  const cc_ssa_instr_t *in) {
    if (in->op == CC_SSA_CALLI) {
        if (is_stackalloc_value(lay, in->lhs)) {
            fprintf(fp, "\tleal %d(%%ebp), %%eax\n", stackalloc_off(lay, in->lhs));
            fprintf(fp, "\tcall *%%eax\n");
        } else {
            fprintf(fp, "\tcall *%d(%%ebp)\n", slot_off(lay, in->lhs));
        }
    } else {
        if (in->call_chain > 0) {
            /* a GNU nested function's environment, in the static chain register */
            emit_i386_load_int_value_to_reg(fp, f, lay, in->call_chain - 1, "%ecx");
        }
        fprintf(fp, "\tcall %s\n", in->sym);
    }
}

static void emit_i386_capture_call_result(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                          int_reg_state_t *ist, const cc_ssa_instr_t *in) {
    if (in->dst < 0) {
        return;
    }
    if (in->rhs == in->dst && is_stackalloc_value(lay, in->dst) && in->imm > 0) {
        int dst_off = stackalloc_off(lay, in->dst);
        long ret_bytes = in->imm;
        if (ret_bytes <= 4) {
            fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", dst_off);
        } else if (ret_bytes <= 8) {
            fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", dst_off);
            fprintf(fp, "\tmovl %%edx, %d(%%ebp)\n", dst_off + 4);
        } else {
            fprintf(fp, "\tpushl $%ld\n", ret_bytes);
            fprintf(fp, "\tpushl %%eax\n");
            fprintf(fp, "\tleal %d(%%ebp), %%eax\n", dst_off);
            fprintf(fp, "\tpushl %%eax\n");
            fprintf(fp, "\tcall memcpy\n");
            fprintf(fp, "\taddl $12, %%esp\n");
        }
        return;
    }
    if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
        emit_i386_x87_store_f64(fp, f, lay, in->dst);
    } else {
        long ret_bytes = in->imm > 0 ? in->imm : 4;
        int rd;
        emit_i386_normalize_int_return(fp, ret_bytes, in->is_unsigned);
        if (ret_bytes >= 8) {
            fprintf(fp, "\tmovl %%edx, %d(%%ebp)\n", slot_off(lay, in->dst) + 4);
        }
        rd = int_regs_define(fp, f, lay, ist, in->dst, int_reg_index(ist, "%eax"), -1);
        if (strcmp(ist->regs[rd], "%eax") != 0) {
            fprintf(fp, "\tmovl %%eax, %s\n", ist->regs[rd]);
        }
    }
}

/**
 * An i386 call.  Each stack argument's offset from the call's %esp is
 * worked out first -- a vector's aligned to its size, as gcc places it --
 * and the arguments are pushed last to first with the padding between
 * them, the whole 16-aligned (the psABI's alignment at a call).  Vectors
 * that travel in %xmm0-2 or %mm0-2 are loaded after the pushes.  A struct
 * result goes through the hidden pointer, except a float _Complex
 * (%eax:%edx) and a vector of 4, 8 or 16 bytes (%eax, %mm0, %xmm0).
 */
static int emit_i386_call(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                          const cc_ssa_instr_t *in) {
    long stack_bytes = 0;
    long a;
    /* a struct result: the i386 psABI passes the buffer's address first */
    int agg = in->dst >= 0 && in->rhs == in->dst && is_stackalloc_value(lay, in->dst) && in->imm > 0;
    int ret_vec = agg && (in->agg_class & (CC_ABI_RET_VEC128 | CC_ABI_RET_VEC64)) != 0;
    int in_regs = agg && (in->agg_class & CC_ABI_RET_EAX_EDX) != 0;
    int sret = agg && !in_regs && !ret_vec;
    long *offs = (long *)calloc(in->arg_count > 0 ? in->arg_count : 1, sizeof(*offs));
    long pos = sret ? 4 : 0;
    long cur;
    int nx = 0;
    int nm = 0;

    if (offs == NULL) {
        return -1;
    }
    int_regs_flush(fp, f, lay, ist);
    for (a = 0; a < (long)in->arg_count; ++a) {
        long bytes = i386_call_arg_bytes(f, lay, in, (size_t)a, in->args[a]);
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;

        if (bytes == 0) {
            offs[a] = -1;
            continue;
        }
        pos = (long)align_up_size((size_t)pos, (size_t)i386_stack_align(abi, call_arg_size_bytes(in, (size_t)a, 4)));
        offs[a] = pos;
        pos += bytes;
    }
    /* the body runs with %esp 16-aligned; the psABI wants it so again at the call */
    cur = (long)align_up_size((size_t)pos, 16);
    for (a = (long)in->arg_count - 1; a >= -1; --a) {
        long start = a >= 0 ? offs[a] : (sret ? 0 : -1);
        long end = a >= 0 ? offs[a] + i386_call_arg_bytes(f, lay, in, (size_t)a, in->args[a]) : (sret ? 4 : 0);

        if (start < 0 && a >= 0) {
            continue;
        }
        if (cur > end) {
            fprintf(fp, "\tsubl $%ld, %%esp\n", cur - end);
            stack_bytes += cur - end;
        }
        if (a >= 0) {
            emit_i386_push_call_arg(fp, f, lay, in, (size_t)a, in->args[a], &stack_bytes);
        } else if (sret) {
            fprintf(fp, "\tleal %d(%%ebp), %%eax\n", stackalloc_off(lay, in->dst));
            fprintf(fp, "\tpushl %%eax\n");
        }
        cur = start < 0 ? 0 : start;
    }
    for (a = 0; a < (long)in->arg_count; ++a) {
        if (offs[a] >= 0) {
            continue;
        }
        emit_load_ptr_value_reg(fp, lay, in->args[a], 0, "%eax");
        if ((in->call_arg_abi[a] & CC_ABI_VEC128) != 0) {
            fprintf(fp, "\tmovups (%%eax), %%xmm%d\n", nx++);
        } else {
            fprintf(fp, "\tmovq (%%eax), %%mm%d\n", nm++);
        }
    }
    free(offs);

    emit_i386_call_target(fp, f, lay, in);

    /* the callee popped the hidden pointer itself (ret $4) */
    if (stack_bytes > 0) {
        fprintf(fp, "\taddl $%ld, %%esp\n", stack_bytes);
    }

    if (ret_vec) {
        int off = stackalloc_off(lay, in->dst);

        if ((in->agg_class & CC_ABI_RET_VEC128) != 0) {
            fprintf(fp, "\tmovups %%xmm0, %d(%%ebp)\n", off);
        } else {
            fprintf(fp, "\tmovq %%mm0, %d(%%ebp)\n", off);
            fprintf(fp, "\temms\n");
        }
    } else if (in_regs) {
        int off = stackalloc_off(lay, in->dst);

        fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", off);
        if (in->imm > 4) {
            fprintf(fp, "\tmovl %%edx, %d(%%ebp)\n", off + 4);
        }
    } else if (!sret) {
        emit_i386_capture_call_result(fp, f, lay, ist, in);
    }
    return 0;
}

int emit_i386(FILE *fp, const cc_ssa_module_t *m, const char *src_path, int emit_debug, cc_diag_t *diag) {
    const cc_options_t *opts = cc_options_current();
    size_t i;

    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        const char *func_sec = (f->attr_section != NULL && f->attr_section[0] != '\0') ? f->attr_section : ".text";
        slot_layout_t lay;
        int_reg_state_t ist;
        size_t j;
        int frame;
        int fn_x87_fp = 0; /* doubles on the x87, not SSE; a long double always is */
        int scratch_off;

        if (opts->i386_mmx && opts->i386_isa_level < 5) {
            set_diag(diag, "i386 MMX codegen requires -march=i586 or newer");
            return -1;
        }
        if (opts->i386_fpmath == CC_FPMATH_SSE) {
            if (!opts->i386_sse2 && function_uses_f64(f)) {
                set_diag(diag, "i386 -mfpmath=sse requires SSE2-enabled codegen");
                return -1;
            }
            fn_x87_fp = 0;
        } else if (opts->i386_fpmath == CC_FPMATH_387) {
            fn_x87_fp = 1;
        } else {
            fn_x87_fp = opts->i386_sse2 ? 0 : 1;
        }
        /* a long double keeps its 80 bits in its slot */
        if (build_slot_layout(f, i386_function_has_ext(f) ? 16 : 8, &lay, diag) != 0) {
            return -1;
        }
        frame = lay.frame_bytes;
        if (cc_backend_checked_frame_add(&frame, 16, diag, "i386 call scratch area") != 0) {
            slot_layout_free(&lay);
            return -1;
        }
        /*
         * The return address and the saved %ebp leave %esp 8 short of a
         * 16-byte boundary (the psABI aligns it at the call); a frame of
         * 8 more than a multiple of 16 puts the body's %esp on one, and
         * emit_i386_call() pads each call's arguments to keep it there.
         */
        frame = cc_backend_align_frame_size(frame, 16) + 8;
        scratch_off = -frame;

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
            fprintf(fp, "\t.cfi_def_cfa_offset 8\n");
            fprintf(fp, "\t.cfi_offset %%ebp, -8\n");
        }

        fprintf(fp, "\tpushl %%ebp\n");
        fprintf(fp, "\tmovl %%esp, %%ebp\n");
        if (emit_debug) {
            fprintf(fp, "\t.cfi_def_cfa_register %%ebp\n");
            if (src_path != NULL) {
                fprintf(fp, "\t.loc 1 1 0\n");
            }
        }
        if (frame > 0) {
            fprintf(fp, "\tsubl $%d, %%esp\n", frame);
        }
        if (emit_dynalloc_slot_clears(fp, f, &lay, 0, diag) != 0) {
            slot_layout_free(&lay);
            return -1;
        }
        if (int_regs_init(&ist, f, emit_regs32, (int)(sizeof(emit_regs32) / sizeof(emit_regs32[0])), 0, diag) != 0) {
            slot_layout_free(&lay);
            return -1;
        }

        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];
            int use_x87_fp = fn_x87_fp || i386_instr_has_ext(f, in);
            ist.cur_index = (int)j;
            int_regs_flush(fp, f, &lay, &ist);

            switch (in->op) {
            case CC_SSA_PARAM: {
                int poff = i386_param_offset(f, in->param_index);
                int param_is_ldouble =
                    (f->param_abi != NULL && in->param_index >= 0 &&
                     (size_t)in->param_index < f->param_count &&
                     f->param_abi[in->param_index] == CC_CALL_ARG_ABI_LDOUBLE)
                        ? 1
                        : 0;
                if (f->param_abi != NULL && in->param_index >= 0 && (size_t)in->param_index < f->param_count &&
                    CC_ABI_KIND(f->param_abi[in->param_index]) == CC_CALL_ARG_ABI_AGG_MEM) {
                    /* a struct passed by value is its copy on the stack */
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tleal %d(%%ebp), %s\n", poff, ist.regs[rd]);
                } else if (f->param_abi != NULL && in->param_index >= 0 &&
                           (size_t)in->param_index < f->param_count &&
                           CC_ABI_KIND(f->param_abi[in->param_index]) == CC_CALL_ARG_ABI_AGG_REG &&
                           in->lhs >= 0 && is_stackalloc_value(&lay, in->lhs)) {
                    /* a vector that came in %xmm0-2 or %mm0-2: into its storage, which is the value */
                    int vreg = i386_vector_param_reg(f, in->param_index);
                    int soff = stackalloc_off(&lay, in->lhs);
                    int rd;

                    int_regs_flush(fp, f, &lay, &ist);
                    if ((f->param_abi[in->param_index] & CC_ABI_VEC128) != 0) {
                        fprintf(fp, "\tmovups %%xmm%d, %d(%%ebp)\n", vreg, soff);
                    } else {
                        fprintf(fp, "\tmovq %%mm%d, %d(%%ebp)\n", vreg, soff);
                        fprintf(fp, "\temms\n");
                    }
                    rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tleal %d(%%ebp), %s\n", soff, ist.regs[rd]);
                } else if (param_is_ldouble && cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    fprintf(fp, "\tfldt %d(%%ebp)\n", poff);
                    emit_i386_x87_store_f64(fp, f, &lay, in->dst);
                } else if (f->param_abi != NULL && in->param_index >= 0 && (size_t)in->param_index < f->param_count &&
                           f->param_abi[in->param_index] == CC_CALL_ARG_ABI_FLOAT &&
                           cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    /* it came as 4 bytes of single precision; the function holds it as a double */
                    int_regs_flush(fp, f, &lay, &ist);
                    fprintf(fp, "\tflds %d(%%ebp)\n", poff);
                    emit_i386_x87_store_f64(fp, f, &lay, in->dst);
                } else if (i386_fp_is_ext(f, in->dst)) {
                    int_regs_flush(fp, f, &lay, &ist);
                    fprintf(fp, "\tfldl %d(%%ebp)\n", poff);
                    emit_i386_x87_store_f64(fp, f, &lay, in->dst);
                } else if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    emit_i386_copy_f64_param_to_slot(fp, &lay, in->dst, poff);
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    long param_size = in->imm > 0 ? in->imm : value_size_bytes(f, in->dst, 4);
                    fprintf(fp, "\tmovl %d(%%ebp), %s\n", poff, ist.regs[rd]);
                    if (param_size >= 8) {
                        fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", poff + 4);
                        fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst) + 4);
                        if (strcmp(ist.regs[rd], "%eax") == 0) {
                            fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", poff);
                        }
                    }
                }
                break;
            }

            case CC_SSA_CONST:
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    if (i386_fp_is_ext(f, in->dst)) {
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
                        /* the constant's 80 bits, as the host's long double holds them */
                        unsigned char raw[12];
                        uint32_t w[3];
                        long double cvt = (in->is_unsigned && in->imm >= 1 && in->imm <= 3)
                                              ? x86_64_special_ldouble_const(in->imm)
                                              : in->fimm_ld;
                        int k;

                        memset(raw, 0, sizeof(raw));
                        memcpy(raw, &cvt, 10);
                        memcpy(w, raw, sizeof(w));
                        for (k = 0; k < 3; ++k) {
                            fprintf(fp, "\tmovl $0x%x, %d(%%ebp)\n", (unsigned)w[k], slot_off(&lay, in->dst) + 4 * k);
                        }
#else
                        /* no 80-bit long double on this host: the constant as a double, widened */
                        emit_i386_const_f64(fp, &lay, in->dst, in->fimm);
                        fprintf(fp, "\tfldl %d(%%ebp)\n", slot_off(&lay, in->dst));
                        fprintf(fp, "\tfstpt %d(%%ebp)\n", slot_off(&lay, in->dst));
#endif
                    } else {
                        emit_i386_const_f64(fp, &lay, in->dst, in->fimm);
                    }
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    emit_i386_const_i32(fp, &ist, rd, in->imm);
                    if (value_size_bytes(f, in->dst, 4) >= 8) {
                        unsigned long long u = (unsigned long long)in->imm;
                        fprintf(fp, "\tmovl $0x%lx, %d(%%ebp)\n", (unsigned long)((u >> 32) & 0xffffffffUL),
                                slot_off(&lay, in->dst) + 4);
                    }
                }
                break;

            case CC_SSA_STR:
                emit_string_literal_label(fp, i, j, in->sym, func_sec);
                int_regs_flush(fp, f, &lay, &ist);
                emit_i386_addr_of_string_label(fp, f, &lay, &ist, in->dst, i, j);
                break;

            case CC_SSA_MOV:
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    emit_i386_mov_fp(fp, f, &lay, in, use_x87_fp);
                } else {
                    int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                    if (value_size_bytes(f, in->dst, 4) >= 8 && !is_stackalloc_value(&lay, in->lhs)) {
                        fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs) + 4);
                        fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst) + 4);
                        if (strcmp(ist.regs[rl], "%eax") == 0) {
                            fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                        }
                    }
                }
                break;

            case CC_SSA_STACKALLOC: {
                int off = stackalloc_off(&lay, in->dst);
                int rd;
                if (off == 0) {
                    set_diag(diag, "i386: missing stack allocation offset");
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    return -1;
                }
                rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                fprintf(fp, "\tleal %d(%%ebp), %s\n", off, ist.regs[rd]);
                break;
            }

            case CC_SSA_LO32: {
                /* the low word is the value as a register holds it */
                int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                break;
            }

            case CC_SSA_HI32: {
                /* the high word of a 64-bit value is always in its slot */
                int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                fprintf(fp, "\tmovl %d(%%ebp), %s\n", slot_off(&lay, in->lhs) + 4, ist.regs[rd]);
                break;
            }

            case CC_SSA_PAIR: {
                int rh = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, -1);
                int rl;

                fprintf(fp, "\tmovl %s, %d(%%ebp)\n", ist.regs[rh], slot_off(&lay, in->dst) + 4);
                rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, rh);
                int_regs_forget(&ist, in->dst);
                fprintf(fp, "\tmovl %s, %d(%%ebp)\n", ist.regs[rl], slot_off(&lay, in->dst));
                break;
            }

            case CC_SSA_UMULW:
                /* mull: %edx:%eax = %eax * the operand */
                int_regs_flush(fp, f, &lay, &ist);
                fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                fprintf(fp, "\tmull %d(%%ebp)\n", slot_off(&lay, in->rhs));
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                fprintf(fp, "\tmovl %%edx, %d(%%ebp)\n", slot_off(&lay, in->dst) + 4);
                break;

            case CC_SSA_CHAIN: {
                /* the static chain, as the function was entered: nothing has run before this */
                int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                fprintf(fp, "\tmovl %%ecx, %s\n", ist.regs[rd]);
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
                emit_i386_addr_of_local(fp, f, &lay, &ist, in);
                break;

            case CC_SSA_GADDR:
                int_regs_flush(fp, f, &lay, &ist);
                emit_i386_addr_of_global_sym(fp, f, &lay, &ist, in);
                break;

            case CC_SSA_LADDR:
                int_regs_flush(fp, f, &lay, &ist);
                emit_i386_addr_of_local_label(fp, f, &lay, &ist, in);
                break;

            case CC_SSA_LOAD:
                /*
                 * Pointer-based loads must observe prior writes to possibly
                 * aliased locals.
                 */
                int_regs_flush(fp, f, &lay, &ist);
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    emit_i386_load_fp_indirect(fp, f, &lay, &ist, in, use_x87_fp);
                } else {
                    int rp = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, rp, -1);
                    long mem_size = in->imm > 0 ? in->imm : 4;
                    emit_i386_load_scalar_indirect(fp, &lay, in, &ist, rp, rd, mem_size, in->is_unsigned);
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
                    emit_i386_store_fp_indirect(fp, f, &lay, &ist, in, use_x87_fp);
                } else {
                    int rp = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rv = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, rp);
                    long mem_size = in->imm > 0 ? in->imm : 4;
                    if (mem_size > 8) {
                        /* Aggregate copy: rhs is source address, lhs is destination address. */
                        int_regs_flush(fp, f, &lay, &ist);
                        emit_i386_store_aggregate_indirect(fp, f, &lay, in);
                    } else {
                        emit_i386_store_scalar_indirect(fp, f, &lay, &ist, in, rp, rv, mem_size, in->is_unsigned);
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
                    if ((use_x87_fp ? emit_i386_float_binop_x87(fp, f, &lay, in, diag)
                                    : emit_i386_float_binop_sse(fp, &lay, in, diag)) != 0) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        return -1;
                    }
                } else {
                    /* words only: legalization split every 64-bit operation */
                    {
                    int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rr;
                    if (in->op == CC_SSA_DIV || in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) {
                        if (emit_i386_int_div_or_shift(fp, f, &lay, &ist, in, diag) != 0) {
                            int_regs_free(&ist);
                            slot_layout_free(&lay);
                            return -1;
                        }
                        break;
                    }
                    if (ist.reg_dirty[rl]) {
                        int_regs_spill_reg(fp, f, &lay, &ist, rl);
                        rl = int_regs_load(fp, f, &lay, &ist, in->lhs, rl, -1);
                    }
                    rr = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, rl);
                    emit_i386_int_binop_regular(fp, &ist, in, rr, rl);
                    int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                    }
                }
                break;

            case CC_SSA_CMP: {
                if (cc_ssa_value_class(f, in->lhs) == CC_VAL_F64 || cc_ssa_value_class(f, in->rhs) == CC_VAL_F64) {
                    if (use_x87_fp) {
                        emit_i386_cmp_float_x87(fp, f, &lay, &ist, in);
                    } else {
                        emit_i386_cmp_float_sse(fp, f, &lay, &ist, in);
                    }
                } else {
                    emit_i386_cmp_int(fp, f, &lay, &ist, in);
                }
                break;
            }

            case CC_SSA_I2F:
                /* SSE2 on i386 converts only a 32-bit integer; a 64-bit one goes through the x87 */
                if (use_x87_fp || value_size_bytes(f, in->lhs, 4) >= 8) {
                    emit_i386_i2f_x87(fp, f, &lay, &ist, in, scratch_off);
                } else {
                    emit_i386_i2f_sse(fp, f, &lay, &ist, in);
                }
                break;

            case CC_SSA_F2I:
                /* SSE2 on i386 converts only to a signed 32-bit integer */
                if (use_x87_fp || value_size_bytes(f, in->dst, 4) >= 8 || in->is_unsigned ||
                    cc_ssa_value_is_unsigned(f, in->dst)) {
                    emit_i386_f2i_x87(fp, f, &lay, &ist, in, scratch_off);
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tcvttsd2sil %d(%%ebp), %s\n", slot_off(&lay, in->lhs), ist.regs[rd]);
                }
                break;

            case CC_SSA_FROUND32:
                if (use_x87_fp) {
                    emit_i386_fround32_x87(fp, f, &lay, in, scratch_off);
                } else {
                    emit_i386_fround32_sse(fp, &lay, in);
                }
                break;

            case CC_SSA_FROUND64:
                /* a long double rounded to double: through a double's memory form */
                emit_i386_x87_load_f64(fp, f, &lay, in->lhs);
                fprintf(fp, "\tfstpl %d(%%ebp)\n", scratch_off + 4);
                fprintf(fp, "\tfldl %d(%%ebp)\n", scratch_off + 4);
                emit_i386_x87_store_f64(fp, f, &lay, in->dst);
                break;

            case CC_SSA_EXT:
                /* legalization expands every one */
                set_diag(diag, "i386: integer conversion reached the emitter unexpanded");
                int_regs_free(&ist);
                slot_layout_free(&lay);
                return -1;

            case CC_SSA_LABEL:
                int_regs_flush(fp, f, &lay, &ist);
                emit_i386_label_def(fp, f->name, in->label);
                break;

            case CC_SSA_BR:
                int_regs_flush(fp, f, &lay, &ist);
                emit_i386_jump_label(fp, f->name, in->label);
                break;

            case CC_SSA_BR_COND:
                emit_i386_branch_cond(fp, f, &lay, &ist, in);
                break;

            case CC_SSA_VA_START: {
                emit_i386_va_start(fp, f, &lay, &ist, in);
                break;
            }

            case CC_SSA_CALL:
            case CC_SSA_CALLI: {
                if (emit_i386_call(fp, f, &lay, &ist, in) != 0) {
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    return -1;
                }
                break;
            }

            case CC_SSA_ASM:
                int_regs_flush(fp, f, &lay, &ist);
                if (emit_inline_asm(fp, f, &lay, in, i, j, 0, diag) != 0) {
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    return -1;
                }
                break;

            case CC_SSA_TRAP:
                if (i386_has_ud2()) {
                    fprintf(fp, "\tud2\n");
                } else {
                    fprintf(fp, "\tint3\n");
                }
                break;

            case CC_SSA_RET:
                if (f->ret_abi == CC_CALL_ARG_ABI_AGG_MEM) {
                    /*
                     * i386 psABI: copy the struct to the caller's buffer,
                     * whose address is the hidden first argument, return
                     * that address, and pop it.
                     */
                    int_regs_flush(fp, f, &lay, &ist);
                    if (in->lhs >= 0 && in->imm > 0) {
                        fprintf(fp, "\tpushl $%ld\n", (long)in->imm);
                        emit_load_ptr_value_reg(fp, &lay, in->lhs, 0, "%eax");
                        fprintf(fp, "\tpushl %%eax\n");
                        fprintf(fp, "\tpushl 8(%%ebp)\n");
                        fprintf(fp, "\tcall memcpy\n");
                        fprintf(fp, "\taddl $12, %%esp\n");
                    }
                    fprintf(fp, "\tmovl 8(%%ebp), %%eax\n");
                    fprintf(fp, "\tleave\n");
                    if (emit_debug) {
                        fprintf(fp, "\t.cfi_def_cfa %%esp, 4\n");
                    }
                    fprintf(fp, "\tret $4\n");
                    break;
                }
                if (in->lhs >= 0 && (in->agg_class & (CC_ABI_RET_VEC128 | CC_ABI_RET_VEC64)) != 0) {
                    /* a vector, from the storage lhs points at, to %xmm0 or %mm0 */
                    int_regs_flush(fp, f, &lay, &ist);
                    emit_load_ptr_value_reg(fp, &lay, in->lhs, 0, "%eax");
                    if ((in->agg_class & CC_ABI_RET_VEC128) != 0) {
                        fprintf(fp, "\tmovups (%%eax), %%xmm0\n");
                    } else {
                        fprintf(fp, "\tmovq (%%eax), %%mm0\n");
                    }
                } else if (in->lhs >= 0) {
                    if (f->ret_type == CC_VAL_F64) {
                        int_regs_flush(fp, f, &lay, &ist);
                        emit_i386_x87_load_f64(fp, f, &lay, in->lhs);
                    } else if (in->imm > 4 && in->imm <= 8) {
                        int_regs_flush(fp, f, &lay, &ist);
                        fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                        if (in->rhs >= 0) {
                            fprintf(fp, "\tmovl %d(%%ebp), %%edx\n", slot_off(&lay, in->rhs));
                        } else {
                            fprintf(fp, "\tmovl %d(%%ebp), %%edx\n", slot_off(&lay, in->lhs) + 4);
                        }
                    } else {
                        int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, int_reg_index(&ist, "%eax"), -1);
                        int_regs_flush(fp, f, &lay, &ist);
                        if (strcmp(ist.regs[rl], "%eax") != 0) {
                            fprintf(fp, "\tmovl %s, %%eax\n", ist.regs[rl]);
                        }
                    }
                } else {
                    int_regs_flush(fp, f, &lay, &ist);
                }
                fprintf(fp, "\tleave\n");
                if (emit_debug) {
                    fprintf(fp, "\t.cfi_def_cfa %%esp, 4\n");
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
            emit_lifecycle_attr(fp, ".init_array", "init_array", f->name, 0);
        }
        if ((f->attr_flags & CC_ATTR_DESTRUCTOR) != 0) {
            emit_lifecycle_attr(fp, ".fini_array", "fini_array", f->name, 0);
        }
        int_regs_free(&ist);
        slot_layout_free(&lay);
    }

    return 0;
}

