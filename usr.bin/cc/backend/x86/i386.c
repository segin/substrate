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
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * The integer registers: the cache allocates from the first I386_CACHE_REGS; the rest are homes of values
 * for the whole of their range (regalloc_i386.c), in the order i386_assign_homes() numbers them.
 */
static const char *const emit_regs32[] = {"%eax", "%ecx", "%edx", "%ebx", "%esi", "%edi"};
#define I386_CACHE_REGS 3

static void emit_i386_call_sym(FILE *fp, const char *sym, int known_local);
static int i386_pic(void);

/** The stack's alignment at a call, in bytes: 16 (the psABI's), or -mpreferred-stack-boundary's 2^N. */
static int i386_stack_boundary(void) {
    int b = cc_options_current()->i386_stack_boundary;

    return b >= 4 ? b : 16;
}

/**
 * A global's, a stack object's or a pointer's address plus a constant, folded into the loads and stores that
 * use it (i386_fold_addrs()).
 */
typedef struct {
    const char *sym; /**< the symbol, "" for the frame, i386_base_mark for a value's register, or NULL: not folded */
    long off;        /**< plus this; from %ebp for the frame */
    int base;        /**< for i386_base_mark: the value whose register is the base */
} i386_faddr_t;

/** i386_faddr_t.sym of an address that is a value plus a constant: `off(%reg)`. */
static const char i386_base_mark[] = "(base)";

/**
 * Empty the register cache without writing anything back: at a return, the frame its values' slots are in
 * is about to go.
 */
static void i386_drop_cache(int_reg_state_t *ist) {
    int r;

    for (r = 0; r < ist->reg_count; ++r) {
        if (ist->reg_val[r] >= 0) {
            int_regs_forget(ist, ist->reg_val[r]);
        }
    }
}

/**
 * Whether the instruction being emitted is value @p v's last use, @p v defined once and used in its block
 * only (live_end): nothing reads it after, from its register or its slot.
 */
static int i386_dies_here(const slot_layout_t *lay, const int_reg_state_t *ist, int v) {
    return v >= 0 && v < lay->value_count && lay->live_end != NULL && lay->live_end[v] >= 0 &&
           lay->live_end[v] == ist->cur_index;
}

/*
 * Where the first parameter is: above the return address, and above the
 * hidden pointer a function returning a struct is passed first.
 */
static int i386_first_param_offset(const cc_ssa_function_t *f) {
    /* (under regparm the hidden pointer came in %eax instead) */
    return f->ret_abi == CC_CALL_ARG_ABI_AGG_MEM && !f->sret_reg ? 12 : 8;
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

        if ((CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_DEFAULT || CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM) &&
            CC_ABI_REGPARM(abi) > 0) {
            bytes = 0; /* regparm: it came in a register, which the prologue stored in the frame */
        } else if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_LDOUBLE) {
            bytes = 12;
        } else if (abi == CC_CALL_ARG_ABI_FLOAT) {
            bytes = 4;
        } else if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_REG || abi == CC_CALL_ARG_ABI_ZERO) {
            bytes = 0; /* a vector in a register, or an empty structure: no stack */
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

    long k;
    int imm = ist->remat != NULL && in->rhs >= 0 && ist->remat[in->rhs] == 1;

    rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    /* a constant operand is an immediate */
    rr = imm ? -1 : int_regs_load(fp, f, lay, ist, in->rhs, -1, rl);
    /* an operand at its last use leaves its register to the result: setcc writes it after cmpl reads it */
    if (i386_dies_here(lay, ist, in->lhs)) {
        int_regs_forget(ist, in->lhs);
    }
    if (!imm && i386_dies_here(lay, ist, in->rhs)) {
        int_regs_forget(ist, in->rhs);
    }
    rd = int_regs_define(fp, f, lay, ist, in->dst, rl, -1);

    if (imm) {
        k = (long)(int)(unsigned int)ist->remat_imm[in->rhs];
        if (k == 0) {
            /* against 0: testl, a byte shorter, and the flags the same for every condition (CF and OF clear) */
            fprintf(fp, "\ttestl %s, %s\n", ist->regs[rl], ist->regs[rl]);
        } else {
            fprintf(fp, "\tcmpl $%ld, %s\n", k, ist->regs[rl]);
        }
    } else {
        fprintf(fp, "\tcmpl %s, %s\n", ist->regs[rr], ist->regs[rl]);
    }
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
    emit_i386_call_sym(fp, "__extendhfdf2", 0);
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
    emit_i386_call_sym(fp, "__truncdfhf2", 0);
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

/**
 * Call memcpy(%eax, %edx, @p size) -- the destination already in %eax and
 * the source in %edx -- as gcc calls it: under -mregparm=N the first N of
 * the three in %eax, %edx, %ecx (Linux's own memcpy is built so and reads
 * them there), the rest on the stack.
 */
static void emit_i386_call_memcpy(FILE *fp, long size) {
    const cc_options_t *o = cc_options_current();
    int nregs = o != NULL ? o->i386_regparm : 0;
    int pushed = 0;

    if (nregs >= 3) {
        fprintf(fp, "\tmovl $%ld, %%ecx\n", size);
    } else {
        fprintf(fp, "\tpushl $%ld\n", size);
        pushed++;
        if (nregs < 2) {
            fprintf(fp, "\tpushl %%edx\n");
            pushed++;
        }
        if (nregs < 1) {
            fprintf(fp, "\tpushl %%eax\n");
            pushed++;
        }
    }
    emit_i386_call_sym(fp, "memcpy", 0);
    if (pushed > 0) {
        fprintf(fp, "\taddl $%d, %%esp\n", pushed * 4);
    }
}

static void emit_i386_store_aggregate_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                               const cc_ssa_instr_t *in) {
    long mem_size = in->imm > 0 ? in->imm : 4;
    emit_i386_load_int_value_to_reg(fp, f, lay, in->lhs, "%eax");
    emit_i386_load_int_value_to_reg(fp, f, lay, in->rhs, "%edx");
    emit_i386_call_memcpy(fp, mem_size);
}

/** Whether value @p v is a word or less of integer, as a register holds it whole. */
static int i386_int_word(const cc_ssa_function_t *f, int v) {
    return v >= 0 && v < f->value_count && cc_ssa_value_class(f, v) == CC_VAL_I64 && value_size_bytes(f, v, 4) <= 4;
}

/**
 * Whether @p in's emission reads and writes registers only through the register cache (int_regs_load(),
 * int_regs_define()), so the values the cache holds may stay in their registers across it rather than all be
 * written back first.  A label, a call, asm, a division or a shift by a variable count (%eax, %edx, %ecx by
 * name), anything floating or of 64 bits, and anything else is not: the cache is flushed before it, so that
 * blocks meet with every value in its slot and fixed registers are free.  A shift by a constant
 * (@p ist's remat) is an immediate.
 */
static int i386_cache_safe(const cc_ssa_function_t *f, const int_reg_state_t *ist, const cc_ssa_instr_t *in) {
    switch (in->op) {
    case CC_SSA_GADDR:
    case CC_SSA_STR:
    case CC_SSA_LADDR:
        /* an address made by movl $sym into a register; under -fPIC through the GOT */
        return !i386_pic() && i386_int_word(f, in->dst);
    case CC_SSA_PARAM:
        /* a word read from the frame into a register (a vector parameter's path flushes first itself) */
        return in->imm <= 4 && i386_int_word(f, in->dst);
    case CC_SSA_SHL:
    case CC_SSA_SHR:
        /* a count in the instruction (rhs -1, imm) or a constant value */
        return (in->rhs < 0 || (ist->remat != NULL && ist->remat[in->rhs] == 1)) && i386_int_word(f, in->dst) &&
               i386_int_word(f, in->lhs);
    case CC_SSA_CONST:
    case CC_SSA_STACKALLOC:
        return i386_int_word(f, in->dst);
    case CC_SSA_MOV:
        return i386_int_word(f, in->dst) && i386_int_word(f, in->lhs);
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_CMP:
        return i386_int_word(f, in->dst) && i386_int_word(f, in->lhs) && i386_int_word(f, in->rhs);
    case CC_SSA_BR_COND:
        /* emit_i386_branch_cond() flushes after reading the condition, or carries the cache */
        return i386_int_word(f, in->lhs);
    case CC_SSA_LABEL:
    case CC_SSA_BR:
        /* they flush, drop or carry the cache themselves (i386_carry_t) */
        return 1;
    case CC_SSA_RET:
        /* a word or nothing returned in %eax: the frame dies, and the cache with it (i386_drop_cache()) */
        return f->ret_abi != CC_CALL_ARG_ABI_AGG_MEM &&
               (in->agg_class & (CC_ABI_RET_VEC128 | CC_ABI_RET_VEC64 | CC_ABI_RET_X87)) == 0 &&
               f->ret_type != CC_VAL_F64 && in->imm <= 4 && in->rhs < 0 && (in->lhs < 0 || i386_int_word(f, in->lhs));
    case CC_SSA_LOAD:
        return in->imm > 0 && in->imm <= 4 && i386_int_word(f, in->dst) && i386_int_word(f, in->lhs);
    case CC_SSA_STORE:
        return in->imm > 0 && in->imm <= 4 && i386_int_word(f, in->lhs) && i386_int_word(f, in->rhs);
    default:
        return 0;
    }
}

/** Whether every value of @p f is an integer of 4 bytes or fewer: none needs an 8-byte slot. */
static int i386_words_only(const cc_ssa_function_t *f) {
    int v;

    for (v = 0; v < f->value_count; ++v) {
        if (cc_ssa_value_class(f, v) != CC_VAL_I64 || cc_ssa_value_size(f, v) > 4) {
            return 0;
        }
    }
    return 1;
}

/**
 * The word constants of @p f made where they are read rather than where they are defined (int_reg_state_t's
 * remat), whose address is not taken: numbers defined once, 1; and a
 * global's address (GADDR, not under -fPIC), or one plus a constant (ADD), each defined once, 2.  Their values
 * go to *@p imm, the symbols to *@p sym.  The buffers are kept from one function to the next; NULL when out of
 * memory.
 */
static const unsigned char *i386_remat_for(const cc_ssa_function_t *f, const unsigned char *addr_taken,
                                           const long long **imm, const char *const **sym) {
    static unsigned char *flag;
    static long long *val;
    static const char **name;
    static int *defs;
    static size_t cap;
    size_t n = (size_t)(f->value_count > 0 ? f->value_count : 1);
    size_t i;

    if (n > cap) {
        unsigned char *nf = (unsigned char *)realloc(flag, n);
        long long *nv = nf != NULL ? (long long *)realloc(val, n * sizeof(*val)) : NULL;
        const char **nn = nv != NULL ? (const char **)realloc((void *)name, n * sizeof(*name)) : NULL;
        int *nd = nn != NULL ? (int *)realloc(defs, n * sizeof(*defs)) : NULL;

        if (nf != NULL) {
            flag = nf;
        }
        if (nv != NULL) {
            val = nv;
        }
        if (nn != NULL) {
            name = nn;
        }
        if (nd == NULL) {
            return NULL;
        }
        defs = nd;
        cap = n;
    }
    memset(flag, 0, n);
    memset(defs, 0, n * sizeof(*defs));
    for (i = 0; i < f->instr_count; ++i) {
        int d = f->instrs[i].dst;

        if (d >= 0 && d < f->value_count) {
            defs[d]++;
        }
    }
    /* in stream order: a value made once is made before what reads it */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int d = in->dst;

        if (d < 0 || d >= f->value_count || !i386_int_word(f, d) || addr_taken[d]) {
            continue;
        }
        if (in->op == CC_SSA_CONST && defs[d] == 1) {
            flag[d] = 1;
            val[d] = in->imm;
        } else if (in->op == CC_SSA_GADDR && in->sym != NULL && defs[d] == 1 && !i386_pic()) {
            flag[d] = 2;
            name[d] = in->sym;
            val[d] = 0;
        } else if (in->op == CC_SSA_ADD && defs[d] == 1 && in->lhs >= 0 && in->lhs < f->value_count &&
                   flag[in->lhs] == 2 && in->rhs >= 0 && in->rhs < f->value_count && flag[in->rhs] == 1) {
            flag[d] = 2;
            name[d] = name[in->lhs];
            val[d] = val[in->lhs] + (long)(int)(unsigned int)val[in->rhs];
        }
    }
    /*
     * an asm's operand -- an output's address too ("=m"), which the slot writes before an instruction do not
     * see as read -- is made, in its slot
     */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        size_t k;

        for (k = 0; in->op == CC_SSA_ASM && k < in->asm_out_count + in->asm_in_count; ++k) {
            int o = k < in->asm_out_count ? in->asm_out_values[k] : in->asm_in_values[k - in->asm_out_count];

            if (CC_SSA_ASM_MEM_INDIRECT_P(o)) {
                o = CC_SSA_ASM_MEM_INDIRECT_DECODE(o);
            }
            if (o >= 0 && o < f->value_count) {
                flag[o] = 0;
            }
        }
    }
    *imm = val;
    *sym = (const char *const *)name;
    return flag;
}

/**
 * Per value of @p f, the register that is its home for the whole of its range (an index into emit_regs32,
 * past the cache's), or -1 (i386_assign_homes()); none under -fPIC, where %ebx is the GOT's.  The registers
 * given go to *@p used, a bit each for %ebx, %esi, %edi.  The buffer is kept from one function to the next;
 * NULL when out of memory.
 */
static const int *i386_homes_for(const cc_ssa_function_t *f, const unsigned char *addr_taken,
                                 const unsigned char *remat, const i386_faddr_t *faddr, const int *alias,
                                 unsigned *used) {
    static int *buf;
    static size_t cap;
    size_t n = (size_t)(f->value_count > 0 ? f->value_count : 1);
    int v;

    if (n > cap) {
        int *next = (int *)realloc(buf, n * sizeof(*next));

        if (next == NULL) {
            return NULL;
        }
        buf = next;
        cap = n;
    }
    *used = 0;
    if (i386_pic()) {
        for (v = 0; v < f->value_count; ++v) {
            buf[v] = -1;
        }
        return buf;
    }
    if (i386_assign_homes(f, addr_taken, remat, alias, buf, used) != 0) {
        return NULL;
    }
    /* a global's address folded into its uses is never made (i386_fold_addrs()): no home */
    *used = 0;
    for (v = 0; v < f->value_count; ++v) {
        if (buf[v] >= 0 && faddr[v].sym != NULL) {
            buf[v] = -1;
        }
        if (buf[v] >= 0) {
            *used |= 1u << buf[v];
            buf[v] += I386_CACHE_REGS;
        }
    }
    return buf;
}

/**
 * The prologue's pushes of the caller's %ebx, %esi, %edi that values have their homes in (@p used), right
 * below %ebp (slot_layout_t.bias); or with @p restore, a return's loads of them back from there.
 */
static void emit_i386_home_saves(FILE *fp, unsigned used, int emit_debug, int restore) {
    int r;
    int off = 0;

    for (r = 0; r < I386_HOME_REGS; ++r) {
        if ((used & (1u << r)) == 0) {
            continue;
        }
        off -= 4;
        if (restore) {
            fprintf(fp, "\tmovl %d(%%ebp), %s\n", off, emit_regs32[I386_CACHE_REGS + r]);
        } else {
            fprintf(fp, "\tpushl %s\n", emit_regs32[I386_CACHE_REGS + r]);
            if (emit_debug) {
                /* where an unwinder finds the caller's: from the CFA, %ebp + 8 */
                fprintf(fp, "\t.cfi_offset %s, %d\n", emit_regs32[I386_CACHE_REGS + r], off - 8);
            }
        }
    }
}

typedef struct {
    FILE *fp;
    const slot_layout_t *lay;
    const int_reg_state_t *ist;
} home_store_ctx_t;

/** x86_for_each_use() callback: a value with a home goes to its slot. */
static void home_store_cb(int v, void *ctx) {
    const home_store_ctx_t *c = (const home_store_ctx_t *)ctx;

    if (c->ist->home[v] >= 0) {
        fprintf(c->fp, "\tmovl %s, %d(%%ebp)\n", c->ist->regs[c->ist->home[v]], slot_off(c->lay, v));
    }
}

/** Before an instruction that may read its operands' slots: each operand with a home register goes to its slot. */
static void i386_store_home_uses(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                 const int_reg_state_t *ist, const cc_ssa_instr_t *in) {
    home_store_ctx_t c;

    c.fp = fp;
    c.lay = lay;
    c.ist = ist;
    x86_for_each_use(f, in, home_store_cb, &c);
}

/**
 * Before an operation writes its result over register @p rl, which holds value @p v dirty and still needed:
 * move @p v to a free register of the cache (not @p avoid), a register move rather than a store and a reload.
 *
 * @return 1 if it moved, 0 when no register is free
 */
static int i386_move_aside(FILE *fp, int_reg_state_t *ist, int v, int rl, int avoid) {
    int r;

    for (r = 0; r < ist->alloc_count; ++r) {
        if (r != rl && r != avoid && ist->reg_val[r] < 0) {
            fprintf(fp, "\tmovl %s, %s\n", ist->regs[rl], ist->regs[r]);
            int_regs_bind(ist, v, r, 1);
            return 1;
        }
    }
    return 0;
}

/**
 * Register @p rl, read for an instruction that writes its result over it: a copy in one of the cache's
 * registers (not @p avoid) when @p rl is a value's home, which keeps it.
 */
static int i386_writable(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist, int rl,
                         int avoid) {
    int t;

    if (rl < ist->alloc_count) {
        return rl;
    }
    t = int_regs_scratch(fp, f, lay, ist, avoid);
    fprintf(fp, "\tmovl %s, %s\n", ist->regs[rl], ist->regs[t]);
    return t;
}


/** Whether @p in reads value @p v only as the address of a word or narrower integer load or store. */
static int i386_addr_use_only(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, int v) {
    if (in->op == CC_SSA_LOAD) {
        return in->lhs == v && in->imm > 0 && in->imm <= 4 && i386_int_word(f, in->dst);
    }
    if (in->op == CC_SSA_STORE) {
        return in->lhs == v && in->rhs != v && in->imm > 0 && in->imm <= 4 && i386_int_word(f, in->rhs);
    }
    return 0;
}

typedef struct {
    const cc_ssa_function_t *f;
    const cc_ssa_instr_t *in;
    i386_faddr_t *fa;
    int folded_add; /**< in is an ADD whose result is folded */
    int changed;
} i386_fold_ctx_t;

/**
 * x86_for_each_use() callback: a candidate read other than as an address, or as a folded ADD's left operand,
 * is not folded.
 */
static void i386_fold_check_cb(int v, void *ctx) {
    i386_fold_ctx_t *c = (i386_fold_ctx_t *)ctx;

    if (c->fa[v].sym != NULL && !i386_addr_use_only(c->f, c->in, v) &&
        !(c->folded_add && c->in->lhs == v && c->in->rhs != v)) {
        c->fa[v].sym = NULL;
        c->changed = 1;
    }
}

/**
 * The values of @p f that are a global's address (GADDR), a stack object's (STACKALLOC) or a pointer's defined
 * once, or one plus a constant (ADD), read only as the address of word loads and stores -- or to make another
 * such value: those instructions address the global, the frame or the pointer's register themselves
 * (`movl sym+off, %reg`, `movl off(%ebp), %reg`, `movl off(%reg), %reg`), and the value is never made.  A
 * global not under -fPIC; a pointer whose address is not taken (@p addr_taken).  Per value, the pointer a
 * folded one is read as in its place goes to *@p alias (else -1), for the frame layout and the homes.  The
 * buffers are kept from one function to the next; NULL when out of memory.
 */
static const i386_faddr_t *i386_fold_addrs(const cc_ssa_function_t *f, const unsigned char *addr_taken,
                                           const int **alias) {
    static i386_faddr_t *fa;
    static long *def_at;
    static int *al;
    static size_t cap;
    size_t n = (size_t)(f->value_count > 0 ? f->value_count : 1);
    size_t i;
    int v;
    int changed;

    if (n > cap) {
        i386_faddr_t *a = (i386_faddr_t *)realloc(fa, n * sizeof(*a));
        long *d = a != NULL ? (long *)realloc(def_at, n * sizeof(*d)) : NULL;
        int *na = d != NULL ? (int *)realloc(al, n * sizeof(*na)) : NULL;

        if (a != NULL) {
            fa = a;
        }
        if (d != NULL) {
            def_at = d;
        }
        if (na == NULL) {
            return NULL;
        }
        al = na;
        cap = n;
    }
    for (v = 0; v < f->value_count; ++v) {
        fa[v].sym = NULL;
        fa[v].off = 0;
        fa[v].base = -1;
        def_at[v] = -1;
    }
    /* each value's one definition (-2: more than one) */
    for (i = 0; i < f->instr_count; ++i) {
        int d = f->instrs[i].dst;

        if (d >= 0 && d < f->value_count) {
            def_at[d] = def_at[d] == -1 ? (long)i : -2;
        }
    }
    /* in stream order: a value's definition comes first */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int d = in->dst;

        if (d < 0 || d >= f->value_count || def_at[d] != (long)i || !i386_int_word(f, d)) {
            continue;
        }
        if (in->op == CC_SSA_GADDR && in->sym != NULL && !i386_pic()) {
            fa[d].sym = in->sym;
            fa[d].off = 0;
        } else if (in->op == CC_SSA_STACKALLOC) {
            /* a stack object: addressed off the frame pointer, at its offset (base) once the frame is laid out */
            fa[d].sym = "";
            fa[d].base = d;
            fa[d].off = 0;
        } else if (in->op == CC_SSA_ADD && in->lhs >= 0 && in->lhs < f->value_count && fa[in->lhs].sym != NULL &&
                   in->rhs >= 0 && in->rhs < f->value_count && def_at[in->rhs] >= 0 &&
                   f->instrs[def_at[in->rhs]].op == CC_SSA_CONST) {
            fa[d].sym = fa[in->lhs].sym;
            fa[d].base = fa[in->lhs].base;
            fa[d].off = fa[in->lhs].off + (long)(int)(unsigned int)f->instrs[def_at[in->rhs]].imm;
        } else if (in->op == CC_SSA_ADD && in->lhs >= 0 && in->lhs < f->value_count && def_at[in->lhs] >= 0 &&
                   !addr_taken[in->lhs] && i386_int_word(f, in->lhs) && in->rhs >= 0 && in->rhs < f->value_count &&
                   def_at[in->rhs] >= 0 &&
                   f->instrs[def_at[in->rhs]].op == CC_SSA_CONST) {
            /* a pointer defined once plus a constant: `off(%reg)`, the pointer read where the access is */
            fa[d].sym = i386_base_mark;
            fa[d].base = in->lhs;
            fa[d].off = (long)(int)(unsigned int)f->instrs[def_at[in->rhs]].imm;
        }
    }
    /* an asm operand, an output's address among them ("=m"), is made: the asm takes a register or slot */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        size_t k;

        for (k = 0; in->op == CC_SSA_ASM && k < in->asm_out_count + in->asm_in_count; ++k) {
            int o = k < in->asm_out_count ? in->asm_out_values[k] : in->asm_in_values[k - in->asm_out_count];

            if (CC_SSA_ASM_MEM_INDIRECT_P(o)) {
                o = CC_SSA_ASM_MEM_INDIRECT_DECODE(o);
            }
            if (o >= 0 && o < f->value_count) {
                fa[o].sym = NULL;
            }
        }
    }
    /* one read of a candidate that is neither an address nor a folded ADD's operand: it is made after all */
    do {
        i386_fold_ctx_t c;

        c.f = f;
        c.fa = fa;
        c.changed = 0;
        for (i = 0; i < f->instr_count; ++i) {
            c.in = &f->instrs[i];
            c.folded_add = c.in->op == CC_SSA_ADD && c.in->dst >= 0 && c.in->dst < f->value_count &&
                           fa[c.in->dst].sym != NULL;
            x86_for_each_use(f, c.in, i386_fold_check_cb, &c);
        }
        changed = c.changed;
    } while (changed);
    /* a pointer plus a constant is read as the pointer, where its loads and stores are */
    for (v = 0; v < f->value_count; ++v) {
        al[v] = fa[v].sym == i386_base_mark ? fa[v].base : -1;
    }
    *alias = al;
    return fa;
}

/**
 * The address operand of a load or store through @p v when it is folded (i386_fold_addrs()), else NULL: a
 * global by its symbol, the frame off %ebp, or a value's register plus a constant -- that value read into a
 * register first, its number in *@p base_reg (-1 for the others).
 */
static const char *i386_faddr_operand(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                      int_reg_state_t *ist, const i386_faddr_t *fa, int v, char *buf, size_t size,
                                      int *base_reg) {
    *base_reg = -1;
    if (v >= 0 && ist->remat != NULL && ist->remat[v] == 2) {
        /* a constant address (a global's, plus a constant): by its symbol */
        snprintf(buf, size, "%s%+ld", ist->remat_sym[v], (long)ist->remat_imm[v]);
        return buf;
    }
    if (fa == NULL || v < 0 || fa[v].sym == NULL) {
        return NULL;
    }
    if (fa[v].sym == i386_base_mark && ist->remat != NULL && ist->remat[fa[v].base] == 2) {
        /* a constant address plus a constant: by its symbol too */
        snprintf(buf, size, "%s%+ld", ist->remat_sym[fa[v].base], (long)ist->remat_imm[fa[v].base] + fa[v].off);
    } else if (fa[v].sym == i386_base_mark) {
        *base_reg = int_regs_load(fp, f, lay, ist, fa[v].base, -1, -1);
        snprintf(buf, size, "%ld(%s)", fa[v].off, ist->regs[*base_reg]);
    } else if (fa[v].sym[0] == '\0') {
        snprintf(buf, size, "%ld(%%ebp)", (long)stackalloc_off(lay, fa[v].base) + fa[v].off);
    } else if (fa[v].off == 0) {
        snprintf(buf, size, "%s", fa[v].sym);
    } else {
        snprintf(buf, size, "%s%+ld", fa[v].sym, fa[v].off);
    }
    return buf;
}

/** Whether value @p v is a constant made where it is read, its value then in *@p imm. */
static int i386_remat_const(const int_reg_state_t *ist, int v, long *imm) {
    if (v < 0 || ist->remat == NULL || ist->remat[v] != 1) {
        return 0;
    }
    *imm = (long)(int)(unsigned int)ist->remat_imm[v];
    return 1;
}

/**
 * For each -mregparm register (1 %eax, 2 %edx, 3 %ecx; out[1..3]), the value of the one-word parameter that
 * came in it, read by @p f's one PARAM of it: the register cache starts with it there, and the prologue does
 * not store the register.  -1 for a register that carries nothing, part of a wider parameter, or a parameter
 * read more than once.
 */
static void i386_regparm_bound(const cc_ssa_function_t *f, int out[4]) {
    int count[4] = {0, 0, 0, 0};
    size_t i;
    int r;

    for (r = 0; r < 4; ++r) {
        out[r] = -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        unsigned char abi;
        int words;

        if (in->op != CC_SSA_PARAM || f->param_abi == NULL || in->param_index < 0 ||
            (size_t)in->param_index >= f->param_count) {
            continue;
        }
        abi = f->param_abi[in->param_index];
        r = CC_ABI_REGPARM(abi);
        if (r == 0 || (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT && CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_MEM)) {
            continue;
        }
        words = CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_DEFAULT && in->imm <= 4 && i386_int_word(f, in->dst) ? 1 : 3;
        if (words == 1) {
            count[r]++;
            out[r] = in->dst;
        } else {
            /* a wider one, or a structure: it and the registers after it read the stored home */
            for (; r <= 3; ++r) {
                count[r] += 2;
            }
        }
    }
    if (f->sret_reg) {
        /* the hidden pointer came in %eax */
        count[1] += 2;
    }
    for (r = 1; r <= 3; ++r) {
        if (count[r] != 1) {
            out[r] = -1;
        }
    }
}

/**
 * The -mregparm registers @p f reads its parameters from, a bit each by number (bit 1 %eax, 2 %edx, 3 %ecx):
 * a parameter read (CC_SSA_PARAM) fills its register and, of several words, those after it; the hidden pointer
 * of a structure result under regparm is in %eax.
 */
static unsigned i386_regparm_used(const cc_ssa_function_t *f) {
    unsigned used = f->sret_reg ? 2u : 0u;
    size_t i;

    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        unsigned char abi;
        long words;
        int r;

        if (in->op != CC_SSA_PARAM || f->param_abi == NULL || in->param_index < 0 ||
            (size_t)in->param_index >= f->param_count) {
            continue;
        }
        abi = f->param_abi[in->param_index];
        r = CC_ABI_REGPARM(abi);
        if (r == 0 || (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT && CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_MEM)) {
            continue;
        }
        words = CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_DEFAULT && in->imm <= 4 && i386_int_word(f, in->dst)
                    ? 1
                    : 3; /* a wider one or a structure: to the last register, as the loads of it may read */
        for (; r <= 3 && words > 0; ++r, --words) {
            used |= 1u << r;
        }
    }
    return used;
}

/**
 * The register cache carried from a block into a successor it is the only way into: what each register held
 * at the jump, restored at the label (i386_carry_build()).
 */
typedef struct {
    int nlabels;
    int nregs;
    unsigned char *npred;    /**< per label: the ways into it, 2 for "more than one" */
    unsigned char *has_snap; /**< per label: a jump to it recorded the cache */
    int *snap_val;           /**< per label x register: the value held, or -1 */
    unsigned char *snap_dirty;
} i386_carry_t;

/** Whether instruction @p in ends its block's flow: nothing falls through it. */
static int i386_ends_flow(const cc_ssa_instr_t *in) {
    return in->op == CC_SSA_BR || in->op == CC_SSA_BR_COND || in->op == CC_SSA_RET || in->op == CC_SSA_TRAP;
}

/** Count one more way into label @p l, saturating at 2. */
static void i386_carry_pred(i386_carry_t *c, int l, int n) {
    if (l >= 0 && l < c->nlabels) {
        c->npred[l] = (unsigned char)(c->npred[l] + n > 2 ? 2 : c->npred[l] + n);
    }
}

/**
 * Count the ways into each label of @p f -- jumps, branches, falling through -- with a label whose address is
 * taken or an asm goto's counted as many; and clear the recorded caches.  The buffers are kept from one
 * function to the next.
 *
 * @return 0, or -1 when out of memory
 */
static int i386_carry_build(i386_carry_t *c, const cc_ssa_function_t *f, int nregs) {
    static unsigned char *npred;
    static unsigned char *has_snap;
    static int *snap_val;
    static unsigned char *snap_dirty;
    static size_t cap;
    size_t n = (size_t)(f->label_count > 0 ? f->label_count : 1);
    size_t i;

    if (n * (size_t)nregs > cap) {
        size_t want = n * (size_t)nregs;
        unsigned char *a = (unsigned char *)realloc(npred, want);
        unsigned char *b = a != NULL ? (unsigned char *)realloc(has_snap, want) : NULL;
        int *v = b != NULL ? (int *)realloc(snap_val, want * sizeof(*v)) : NULL;
        unsigned char *d = v != NULL ? (unsigned char *)realloc(snap_dirty, want) : NULL;

        if (a != NULL) {
            npred = a;
        }
        if (b != NULL) {
            has_snap = b;
        }
        if (v != NULL) {
            snap_val = v;
        }
        if (d == NULL) {
            return -1;
        }
        snap_dirty = d;
        cap = want;
    }
    c->nlabels = f->label_count;
    c->nregs = nregs;
    c->npred = npred;
    c->has_snap = has_snap;
    c->snap_val = snap_val;
    c->snap_dirty = snap_dirty;
    memset(npred, 0, n);
    memset(has_snap, 0, n);
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        size_t k;

        switch (in->op) {
        case CC_SSA_BR:
            i386_carry_pred(c, in->label, 1);
            break;
        case CC_SSA_BR_COND:
            i386_carry_pred(c, in->true_label, 1);
            i386_carry_pred(c, in->false_label, 1);
            break;
        case CC_SSA_LABEL:
            /* falling into it from the instruction before, or the function's first: as many ways in */
            i386_carry_pred(c, in->label, i == 0 ? 2 : (i386_ends_flow(&f->instrs[i - 1]) ? 0 : 1));
            break;
        case CC_SSA_LADDR:
            i386_carry_pred(c, in->label, 2);
            break;
        case CC_SSA_ASM:
            for (k = 0; k < in->asm_goto_count; ++k) {
                i386_carry_pred(c, in->asm_goto_labels[k], 2);
            }
            break;
        default:
            break;
        }
    }
    return 0;
}

/** Whether label @p l has one way in, from where the cache can be carried. */
static int i386_carry_single(const i386_carry_t *c, int l) {
    return l >= 0 && l < c->nlabels && c->npred[l] == 1;
}

/**
 * Write each dirty register's value back and keep it in the register, clean; one past its slot's use
 * (int_regs_spill_reg()'s slot_end) is dropped instead.
 */
static void i386_sync_cache(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist) {
    int r;

    for (r = 0; r < ist->reg_count; ++r) {
        int v = ist->reg_val[r];

        if (v < 0 || !ist->reg_dirty[r]) {
            continue;
        }
        if (lay->slot_end != NULL && lay->slot_end[v] >= 0 && ist->cur_index > lay->slot_end[v]) {
            int_regs_forget(ist, v);
            continue;
        }
        if (cc_ssa_value_class(f, v) == CC_VAL_I64 && !is_stackalloc_value(lay, v)) {
            fprintf(fp, "\tmovl %s, %d(%%ebp)\n", ist->regs[r], slot_off(lay, v));
        }
        ist->reg_dirty[r] = 0;
    }
}

/** Record the cache for label @p l, entered only by the jump being emitted. */
static void i386_carry_save(i386_carry_t *c, const int_reg_state_t *ist, int l) {
    int r;

    for (r = 0; r < c->nregs; ++r) {
        c->snap_val[l * c->nregs + r] = ist->reg_val[r];
        c->snap_dirty[l * c->nregs + r] = ist->reg_dirty[r];
    }
    c->has_snap[l] = 1;
}

/** Make the cache what the jump into label @p l left, whatever the code before the label held. */
static void i386_carry_restore(const i386_carry_t *c, int_reg_state_t *ist, int l) {
    int r;

    i386_drop_cache(ist);
    for (r = 0; r < c->nregs; ++r) {
        int v = c->snap_val[l * c->nregs + r];

        if (v >= 0) {
            int_regs_bind(ist, v, r, c->snap_dirty[l * c->nregs + r]);
        }
    }
}

/**
 * The values of @p f whose address is taken (CC_SSA_ADDR), one byte each, in a buffer kept from one
 * function to the next; NULL when out of memory.
 */
static const unsigned char *i386_addr_taken_for(const cc_ssa_function_t *f) {
    static unsigned char *buf;
    static size_t cap;
    size_t n = (size_t)(f->value_count > 0 ? f->value_count : 1);
    size_t i;

    if (n > cap) {
        unsigned char *next = (unsigned char *)realloc(buf, n);

        if (next == NULL) {
            return NULL;
        }
        buf = next;
        cap = n;
    }
    memset(buf, 0, n);
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_ADDR && in->lhs >= 0 && in->lhs < f->value_count) {
            buf[in->lhs] = 1;
        }
    }
    return buf;
}

/**
 * Before a load or store through a pointer: write back the values whose address is taken (@p addr_taken,
 * from CC_SSA_ADDR), the only ones memory can alias -- a value is otherwise in its slot or a register and
 * nowhere a pointer reaches -- and with @p drop forget their registers too, as a store may change them.
 * The cache keeps every other value: it was all flushed, and each was stored and loaded again.
 */
static void i386_sync_aliased(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                              const unsigned char *addr_taken, int drop) {
    int r;

    for (r = 0; r < ist->reg_count; ++r) {
        int v = ist->reg_val[r];

        if (v < 0 || v >= f->value_count || !addr_taken[v]) {
            continue;
        }
        if (drop) {
            int_regs_spill_reg(fp, f, lay, ist, r);
        } else if (ist->reg_dirty[r]) {
            if (cc_ssa_value_class(f, v) == CC_VAL_I64 && !is_stackalloc_value(lay, v)) {
                fprintf(fp, "\tmovl %s, %d(%%ebp)\n", ist->regs[r], slot_off(lay, v));
            }
            ist->reg_dirty[r] = 0;
        }
    }
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
    /* the low word; a 64-bit constant's high word goes to its slot separately.  Zero is xorl: no flags are live */
    if ((unsigned int)imm == 0) {
        fprintf(fp, "\txorl %s, %s\n", ist->regs[rd], ist->regs[rd]);
    } else {
        fprintf(fp, "\tmovl $%ld, %s\n", (long)(int)(unsigned int)imm, ist->regs[rd]);
    }
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

/*
 * Position-independent code (-fPIC) reaches its data through the GOT: the GOT's address is the pc, from a
 * thunk that returns its return address, plus _GLOBAL_OFFSET_TABLE_ (R_386_GOTPC); a symbol that binds within
 * the object is at a fixed offset from it (sym@GOTOFF), another's address is in its GOT entry (sym@GOT).  A
 * thunk per register is written once in each object that uses it, local to the object.
 */

/** The registers whose pc thunk this object uses, by bit (i386_thunk_bit()). */
static unsigned i386_pc_thunks;
/** The module being written, for where a symbol binds (module_symbol_binds_locally()). */
static const cc_ssa_module_t *i386_module;

/** Whether the code is position-independent. */
static int i386_pic(void) {
    const cc_options_t *opts = cc_options_current();

    return opts != NULL && opts->pic;
}

/** The register names a pc thunk is made for, as `%eax` is written, and the thunk's suffix. */
static const char *const i386_thunk_regs[] = {"%eax", "%ebx", "%ecx", "%edx", "%esi", "%edi", "%ebp"};

/** The bit of register @p reg in i386_pc_thunks, or 0 for one no thunk is made for. */
static unsigned i386_thunk_bit(const char *reg) {
    size_t i;

    for (i = 0; i < sizeof(i386_thunk_regs) / sizeof(i386_thunk_regs[0]); ++i) {
        if (strcmp(i386_thunk_regs[i], reg) == 0) {
            return 1u << i;
        }
    }
    return 0;
}

/** Put the GOT's address in register @p reg (`%ecx`): its pc thunk's return address plus the GOT's offset. */
static void emit_i386_got_base(FILE *fp, const char *reg) {
    i386_pc_thunks |= i386_thunk_bit(reg);
    fprintf(fp, "\tcall __x86.get_pc_thunk.%s\n", reg + 2);
    fprintf(fp, "\taddl $_GLOBAL_OFFSET_TABLE_, %s\n", reg);
}

/** Write the pc thunks this object used, each local to it, and go back to .text. */
static void emit_i386_pc_thunks(FILE *fp) {
    size_t i;

    for (i = 0; i < sizeof(i386_thunk_regs) / sizeof(i386_thunk_regs[0]); ++i) {
        const char *reg = i386_thunk_regs[i];

        if ((i386_pc_thunks & (1u << i)) == 0) {
            continue;
        }
        fprintf(fp, "\n\t.text\n");
        fprintf(fp, ".type __x86.get_pc_thunk.%s, @function\n", reg + 2);
        fprintf(fp, "__x86.get_pc_thunk.%s:\n", reg + 2);
        fprintf(fp, "\tmovl (%%esp), %s\n", reg);
        fprintf(fp, "\tret\n");
        fprintf(fp, ".size __x86.get_pc_thunk.%s, .-__x86.get_pc_thunk.%s\n", reg + 2, reg + 2);
    }
    i386_pc_thunks = 0;
}

/**
 * Write symbol reference @p sym (`name` or `name+offset`) relocated as @p kind (`GOTOFF`, `GOT`): gas takes the
 * relocation after the name, the offset after that.
 */
static void emit_i386_sym_reloc(FILE *fp, const char *sym, const char *kind) {
    size_t n = strcspn(sym, "+-");

    fprintf(fp, "%.*s@%s%s", (int)n, sym, kind, sym + n);
}

/**
 * The address of @p sym into register @p reg: an immediate, or under -fPIC the GOT's address plus the
 * symbol's offset from it when it binds within the object, else its GOT entry (and its offset added).
 */
static void emit_i386_symbol_address(FILE *fp, const cc_ssa_module_t *m, const char *sym, const char *reg) {
    if (!i386_pic()) {
        fprintf(fp, "\tmovl $%s, %s\n", sym, reg);
        return;
    }
    emit_i386_got_base(fp, reg);
    if (module_symbol_binds_locally(m, sym)) {
        fprintf(fp, "\tleal ");
        emit_i386_sym_reloc(fp, sym, "GOTOFF");
        fprintf(fp, "(%s), %s\n", reg, reg);
    } else {
        size_t n = strcspn(sym, "+-");

        fprintf(fp, "\tmovl %.*s@GOT(%s), %s\n", (int)n, sym, reg, reg);
        if (sym[n] != '\0') {
            fprintf(fp, "\taddl $%s, %s\n", sym + n + (sym[n] == '+' ? 1 : 0), reg);
        }
    }
}

/**
 * A direct call of @p sym.  Under -fPIC one that may bind outside the object goes through its PLT entry,
 * which finds the GOT in %ebx (the i386 psABI): %ebx is loaded first.  It is callee-saved and the
 * allocator never gives it out, so the function saves it on entry and restores it on return
 * (i386_pic_saves_ebx).  @p known_local says a declaration the module does not keep made the symbol bind
 * within the object (cc_ssa_instr_t.call_binds_local): it is then called directly.
 */
static void emit_i386_call_sym(FILE *fp, const char *sym, int known_local) {
    if (i386_pic() && !known_local && !module_symbol_binds_locally(i386_module, sym)) {
        emit_i386_got_base(fp, "%ebx");
        fprintf(fp, "\tcall %s@PLT\n", sym);
        return;
    }
    fprintf(fp, "\tcall %s\n", sym);
}

static void emit_i386_addr_of_global_sym(FILE *fp, const cc_ssa_module_t *m, const cc_ssa_function_t *f,
                                         const slot_layout_t *lay, int_reg_state_t *ist, const cc_ssa_instr_t *in) {
    int rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);

    emit_i386_symbol_address(fp, m, in->sym, ist->regs[rd]);
}

static void emit_i386_addr_of_local_label(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                          const cc_ssa_instr_t *in) {
    int rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);

    if (i386_pic()) {
        /* a label of this function: at a fixed offset from the GOT */
        emit_i386_got_base(fp, ist->regs[rd]);
        fprintf(fp, "\tleal ");
        emit_local_label(fp, f->name, in->label);
        fprintf(fp, "@GOTOFF(%s), %s\n", ist->regs[rd], ist->regs[rd]);
        return;
    }
    fprintf(fp, "\tmovl $");
    emit_local_label(fp, f->name, in->label);
    fprintf(fp, ", %s\n", ist->regs[rd]);
}

static void emit_i386_addr_of_string_label(FILE *fp, const cc_ssa_module_t *m, const cc_ssa_function_t *f,
                                           const slot_layout_t *lay, int_reg_state_t *ist, int dst, size_t fn_index,
                                           size_t instr_index) {
    char label[96];
    int rd;
    snprintf(label, sizeof(label), ".L__cc_str_%zu_%zu", fn_index, instr_index);
    rd = int_regs_define(fp, f, lay, ist, dst, -1, -1);
    emit_i386_symbol_address(fp, m, label, ist->regs[rd]);
}

/** The instruction loading @p size (1, 2 or 4) bytes into a word register, extended as @p is_unsigned says. */
static const char *i386_load_mnemonic(long size, int is_unsigned) {
    if (size == 1) {
        return is_unsigned ? "movzbl" : "movsbl";
    }
    if (size == 2) {
        return is_unsigned ? "movzwl" : "movswl";
    }
    return "movl";
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

    /* written over: not a value's home */
    rl = i386_writable(fp, f, lay, ist, rl, -1);
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

/** Whether label @p label is defined right after instruction @p in of @p f, with only labels between: a jump there falls through. */
static int i386_label_follows(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, int label) {
    size_t k;

    for (k = (size_t)(in - f->instrs) + 1; k < f->instr_count && f->instrs[k].op == CC_SSA_LABEL; ++k) {
        if (f->instrs[k].label == label) {
            return 1;
        }
    }
    return 0;
}

/** The condition code of jcc/setcc condition @p cc ("l", "ae", ...) negated. */
static const char *i386_cc_negate(const char *cc) {
    static const char *const pairs[][2] = {{"e", "ne"}, {"z", "nz"}, {"l", "ge"}, {"le", "g"}, {"b", "ae"},
                                           {"be", "a"}, {"s", "ns"}, {"p", "np"}, {"o", "no"}, {"c", "nc"}};
    size_t i;

    for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        if (strcmp(cc, pairs[i][0]) == 0) {
            return pairs[i][1];
        }
        if (strcmp(cc, pairs[i][1]) == 0) {
            return pairs[i][0];
        }
    }
    return NULL;
}

/**
 * The jump on condition @p cc to @p true_label, else to @p false_label, after instruction @p in: no jump to a
 * label that follows, and the condition negated when the true one does.
 */
static void emit_i386_cond_jump(FILE *fp, const cc_ssa_function_t *f, const cc_ssa_instr_t *in, const char *cc,
                                int true_label, int false_label) {
    const char *neg = i386_cc_negate(cc);

    if (i386_label_follows(f, in, true_label) && neg != NULL && true_label != false_label) {
        fprintf(fp, "\tj%s ", neg);
        emit_local_label(fp, f->name, false_label);
        fprintf(fp, "\n");
        return;
    }
    fprintf(fp, "\tj%s ", cc);
    emit_local_label(fp, f->name, true_label);
    fprintf(fp, "\n");
    if (!i386_label_follows(f, in, false_label)) {
        emit_i386_jump_label(fp, f->name, false_label);
    }
}

/**
 * Before a conditional jump to @p t, else to @p e: when each is entered only so, the register cache goes into
 * both; else it is written back (movl, which leaves the flags).
 */
static void i386_branch_cache(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                              i386_carry_t *c, int t, int e) {
    if (t != e && (i386_carry_single(c, t) || i386_carry_single(c, e))) {
        /*
         * written back once here, not on each way: the registers go along clean into a target entered only
         * so; one entered other ways too starts with nothing in them
         */
        i386_sync_cache(fp, f, lay, ist);
        if (i386_carry_single(c, t)) {
            i386_carry_save(c, ist, t);
        }
        if (i386_carry_single(c, e)) {
            i386_carry_save(c, ist, e);
        }
        return;
    }
    int_regs_flush(fp, f, lay, ist);
}

/**
 * Integer compare @p cmp and the branch @p br on its result, the compare's only use: cmpl, the register cache
 * written back or carried (i386_branch_cache()), and a jump on the flags.
 */
static void emit_i386_cmp_branch(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                 i386_carry_t *c, const cc_ssa_instr_t *cmp, const cc_ssa_instr_t *br) {
    const char *set = setcc_int_mnemonic(cmp->cmp_kind, cmp->is_unsigned); /* "setl": the condition after "set" */
    int rl = int_regs_load(fp, f, lay, ist, cmp->lhs, -1, -1);

    if (ist->remat != NULL && cmp->rhs >= 0 && ist->remat[cmp->rhs] == 1 && (unsigned int)ist->remat_imm[cmp->rhs] == 0) {
        /* against zero: testl, shorter, sets the flags as cmpl $0 does for every condition (CF and OF clear) */
        fprintf(fp, "\ttestl %s, %s\n", ist->regs[rl], ist->regs[rl]);
    } else if (ist->remat != NULL && cmp->rhs >= 0 && ist->remat[cmp->rhs] == 1) {
        fprintf(fp, "\tcmpl $%ld, %s\n", (long)(int)(unsigned int)ist->remat_imm[cmp->rhs], ist->regs[rl]);
    } else {
        int rr = int_regs_load(fp, f, lay, ist, cmp->rhs, -1, rl);

        fprintf(fp, "\tcmpl %s, %s\n", ist->regs[rr], ist->regs[rl]);
        if (i386_dies_here(lay, ist, cmp->rhs)) {
            int_regs_forget(ist, cmp->rhs);
        }
    }
    /* an operand whose last use this is is not written back */
    if (i386_dies_here(lay, ist, cmp->lhs)) {
        int_regs_forget(ist, cmp->lhs);
    }
    i386_branch_cache(fp, f, lay, ist, c, br->true_label, br->false_label);
    emit_i386_cond_jump(fp, f, br, set + 3, br->true_label, br->false_label);
}

static void emit_i386_branch_cond(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                  i386_carry_t *c, const cc_ssa_instr_t *in) {
    int rc = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    fprintf(fp, "\ttestl %s, %s\n", ist->regs[rc], ist->regs[rc]);
    if (lay->live_end != NULL && lay->live_end[in->lhs] == ist->cur_index) {
        /* the condition's last use: it is not written back */
        int_regs_forget(ist, in->lhs);
    }
    i386_branch_cache(fp, f, lay, ist, c, in->true_label, in->false_label);
    emit_i386_cond_jump(fp, f, in, "ne", in->true_label, in->false_label);
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
static int i386_call_arg_regparm(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, size_t a);

static long i386_call_arg_bytes(const cc_ssa_function_t *f, const slot_layout_t *lay, const cc_ssa_instr_t *in,
                                size_t arg_index, int value) {
    long arg_size = call_arg_size_bytes(in, arg_index, value_size_bytes(f, value, 4));

    if (i386_call_arg_regparm(f, in, arg_index) > 0) {
        return 0; /* in %eax, %edx or %ecx */
    }
    if (call_arg_is_ldouble_abi(in, arg_index)) {
        return 12;
    }
    if (in->call_arg_abi != NULL && in->call_arg_abi[arg_index] == CC_CALL_ARG_ABI_FLOAT) {
        return 4;
    }
    if (call_arg_is_agg_reg_abi(in, arg_index) ||
        (in->call_arg_abi != NULL && in->call_arg_abi[arg_index] == CC_CALL_ARG_ABI_ZERO)) {
        /* a vector in %xmm0-2 or %mm0-2, or an empty structure, which gcc passes as nothing */
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

/** The regparm registers, in the order arguments take them. */
static const char *const i386_regparm_regs[3] = {"%eax", "%edx", "%ecx"};

/**
 * The register argument @p a of call @p in starts in under regparm: 1
 * %eax, 2 %edx, 3 %ecx, or 0 for the stack.  Lowering marks a source
 * call's arguments in their ABI bytes; a call the compiler made itself
 * (memcpy for a structure copy) has none, and under -mregparm takes the
 * registers by its arguments' sizes, as gcc's library calls do.
 */
static int i386_call_arg_regparm(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, size_t a) {
    const cc_options_t *o = cc_options_current();
    int next = 0;
    size_t k;

    if (in->call_arg_abi != NULL) {
        unsigned char abi = in->call_arg_abi[a];
        int kind = CC_ABI_KIND(abi);

        return kind == CC_CALL_ARG_ABI_DEFAULT || kind == CC_CALL_ARG_ABI_AGG_MEM ? CC_ABI_REGPARM(abi) : 0;
    }
    if (o == NULL || o->i386_regparm <= 0 || in->call_is_variadic) {
        return 0;
    }
    for (k = 0; k <= a && k < in->arg_count; ++k) {
        int v = in->args[k];
        /* the compiler's own calls pass pointers, size_t and int: a word each (a constant may be held wider) */
        int words = cc_ssa_value_class(f, v) == CC_VAL_F64 ? 0 : 1;

        if (words == 0) {
            if (k == a) {
                return 0;
            }
            continue;
        }
        if (next + words > o->i386_regparm) {
            return 0;
        }
        if (k == a) {
            return next + 1;
        }
        next += words;
    }
    return 0;
}

/**
 * Load the register arguments of call @p in, after its stack arguments are
 * pushed: each its value -- an integer, a long long's two halves, or a
 * structure's words (its address first in the last register it fills, so
 * the others are not disturbed).
 *
 * @return a mask of the registers loaded (bit 0 %eax, 1 %edx, 2 %ecx)
 */
static unsigned emit_i386_load_regparm_args(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                            const cc_ssa_instr_t *in) {
    unsigned used = 0;
    size_t a;

    for (a = 0; a < in->arg_count; ++a) {
        int r = i386_call_arg_regparm(f, in, a);
        int v = in->args[a];

        if (r == 0) {
            continue;
        }
        if (call_arg_is_agg_mem_abi(in, a)) {
            long words = (call_arg_size_bytes(in, a, 4) + 3) / 4;
            int last = r - 1 + (int)words - 1;
            long w;

            if (last > 2) {
                last = 2;
            }
            emit_load_ptr_value_reg(fp, lay, v, 0, i386_regparm_regs[last]);
            for (w = 0; r - 1 + w < last; ++w) {
                fprintf(fp, "\tmovl %ld(%s), %s\n", w * 4, i386_regparm_regs[last], i386_regparm_regs[r - 1 + w]);
                used |= 1u << (r - 1 + w);
            }
            fprintf(fp, "\tmovl %ld(%s), %s\n", (long)(last - (r - 1)) * 4, i386_regparm_regs[last],
                    i386_regparm_regs[last]);
            used |= 1u << last;
        } else if (!is_stackalloc_value(lay, v) && call_arg_size_bytes(in, a, value_size_bytes(f, v, 4)) >= 8 && r < 3) {
            fprintf(fp, "\tmovl %d(%%ebp), %s\n", slot_off(lay, v), i386_regparm_regs[r - 1]);
            fprintf(fp, "\tmovl %d(%%ebp), %s\n", slot_off(lay, v) + 4, i386_regparm_regs[r]);
            used |= 3u << (r - 1);
        } else {
            emit_i386_load_int_value_to_reg(fp, f, lay, v, i386_regparm_regs[r - 1]);
            used |= 1u << (r - 1);
        }
    }
    return used;
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
        emit_i386_call_sym(fp, in->sym, in->call_binds_local);
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
            /* the callee returned the buffer's address in %eax: copy from it */
            fprintf(fp, "\tmovl %%eax, %%edx\n");
            fprintf(fp, "\tleal %d(%%ebp), %%eax\n", dst_off);
            emit_i386_call_memcpy(fp, ret_bytes);
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
/**
 * Whether call @p in passes only integer words, by register (-mregparm) or on the stack, to a direct target,
 * with no structure result and no static chain: its register arguments are moved where they are, not stored
 * and loaded again (i386_regparm_moves_t).
 */
static int i386_call_plain(const cc_ssa_function_t *f, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    size_t a;

    /* an indirect target is called from its slot (emit_i386_call_target()), a stack object's by %eax */
    if ((in->op != CC_SSA_CALL && in->op != CC_SSA_CALLI) || in->call_chain > 0 ||
        (in->op == CC_SSA_CALLI && (in->lhs < 0 || is_stackalloc_value(lay, in->lhs))) ||
        (in->dst >= 0 && in->rhs == in->dst && is_stackalloc_value(lay, in->dst) && in->imm >= 0)) {
        return 0;
    }
    for (a = 0; a < in->arg_count; ++a) {
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;

        /* a stack object's address pushed is made in %eax first: only in a register argument */
        if (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT || call_arg_size_bytes(in, a, 4) > 4 ||
            !i386_int_word(f, in->args[a]) ||
            (is_stackalloc_value(lay, in->args[a]) && i386_call_arg_regparm(f, in, a) == 0)) {
            return 0;
        }
    }
    return 1;
}

/** Where a register argument of a plain call comes from (i386_call_plain()). */
typedef struct {
    int target; /**< the argument's register, an index into the cache's */
    int kind;   /**< 0 a register (reg), 1 a constant (imm), 2 the value's slot (v), 3 a stack object's address (v),
                     4 a constant address (v: remat 2) */
    int reg;
    long imm;
    int v;
} i386_arg_move_t;

/**
 * Note where each register argument of plain call @p in is before the cache is written back, into @p mv
 * (three at most): a cache or home register, a constant, or its slot.  An argument at its last use in a cache
 * register is not written back.
 *
 * @return how many there are
 */
/** Note in @p m where value @p v is, to be moved into register @p target (i386_note_arg_moves()). */
static void i386_note_move(const slot_layout_t *lay, int_reg_state_t *ist, int v, int target, i386_arg_move_t *m) {
    long k;

    m->target = target;
    m->v = v;
    if (i386_remat_const(ist, v, &k)) {
        m->kind = 1;
        m->imm = k;
    } else if (is_stackalloc_value(lay, v)) {
        m->kind = 3;
    } else if (ist->remat != NULL && ist->remat[v] == 2) {
        m->kind = 4;
    } else if (ist->home != NULL && ist->home[v] >= 0) {
        m->kind = 0;
        m->reg = ist->home[v];
    } else if (ist->val_reg[v] >= 0) {
        m->kind = 0;
        m->reg = ist->val_reg[v];
        if (i386_dies_here(lay, ist, v)) {
            /* read from its register: its slot need not have it */
            ist->reg_dirty[ist->val_reg[v]] = 0;
        }
    } else {
        m->kind = 2;
    }
}

static int i386_note_arg_moves(const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                               const cc_ssa_instr_t *in, i386_arg_move_t mv[3]) {
    int n = 0;
    size_t a;

    for (a = 0; a < in->arg_count && n < 3; ++a) {
        int r = i386_call_arg_regparm(f, in, a);

        if (r == 0) {
            continue;
        }
        i386_note_move(lay, ist, in->args[a], int_reg_index(ist, i386_regparm_regs[r - 1]), &mv[n]);
        n++;
    }
    return n;
}

/**
 * Fill the argument registers from @p mv: the register-to-register moves in an order that reads each source
 * before it is written (xchgl breaks a cycle), then the constants and slots.
 */
static void emit_i386_arg_moves(FILE *fp, const slot_layout_t *lay, const int_reg_state_t *ist, i386_arg_move_t *mv,
                                int n) {
    int done[3] = {0, 0, 0};
    int left = 0;
    int i;
    int j;

    for (i = 0; i < n; ++i) {
        if (mv[i].kind == 0 && mv[i].reg == mv[i].target) {
            done[i] = 1;
        } else if (mv[i].kind == 0) {
            left++;
        }
    }
    while (left > 0) {
        int progress = 0;

        for (i = 0; i < n; ++i) {
            int blocked = 0;

            if (done[i] || mv[i].kind != 0) {
                continue;
            }
            /* not while another move still reads this target */
            for (j = 0; j < n; ++j) {
                if (j != i && !done[j] && mv[j].kind == 0 && mv[j].reg == mv[i].target) {
                    blocked = 1;
                }
            }
            if (!blocked) {
                fprintf(fp, "\tmovl %s, %s\n", ist->regs[mv[i].reg], ist->regs[mv[i].target]);
                done[i] = 1;
                left--;
                progress = 1;
            }
        }
        if (!progress) {
            /* a cycle: exchange the first pending move's two registers, and what read either reads the other */
            for (i = 0; i < n && (done[i] || mv[i].kind != 0); ++i) {
            }
            fprintf(fp, "\txchgl %s, %s\n", ist->regs[mv[i].reg], ist->regs[mv[i].target]);
            done[i] = 1;
            left--;
            for (j = 0; j < n; ++j) {
                if (!done[j] && mv[j].kind == 0) {
                    if (mv[j].reg == mv[i].target) {
                        mv[j].reg = mv[i].reg;
                    } else if (mv[j].reg == mv[i].reg) {
                        mv[j].reg = mv[i].target;
                    }
                    if (mv[j].reg == mv[j].target) {
                        done[j] = 1;
                        left--;
                    }
                }
            }
        }
    }
    for (i = 0; i < n; ++i) {
        if (mv[i].kind == 1) {
            if (mv[i].imm == 0) {
                fprintf(fp, "\txorl %s, %s\n", ist->regs[mv[i].target], ist->regs[mv[i].target]);
            } else {
                fprintf(fp, "\tmovl $%ld, %s\n", mv[i].imm, ist->regs[mv[i].target]);
            }
        } else if (mv[i].kind == 2) {
            fprintf(fp, "\tmovl %d(%%ebp), %s\n", slot_off(lay, mv[i].v), ist->regs[mv[i].target]);
        } else if (mv[i].kind == 3) {
            fprintf(fp, "\tleal %d(%%ebp), %s\n", stackalloc_off(lay, mv[i].v), ist->regs[mv[i].target]);
        } else if (mv[i].kind == 4) {
            fprintf(fp, "\tmovl $%s%+ld, %s\n", ist->remat_sym[mv[i].v], (long)ist->remat_imm[mv[i].v],
                    ist->regs[mv[i].target]);
        }
    }
}

static int emit_i386_call(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                          const cc_ssa_instr_t *in) {
    long stack_bytes = 0;
    long a;
    /*
     * a struct result: the i386 psABI passes the buffer's address first --
     * an empty structure's too (size 0), which the callee pops as well
     */
    int agg = in->dst >= 0 && in->rhs == in->dst && is_stackalloc_value(lay, in->dst) && in->imm >= 0;
    int ret_vec = agg && (in->agg_class & (CC_ABI_RET_VEC128 | CC_ABI_RET_VEC64)) != 0;
    int in_regs = agg && (in->agg_class & CC_ABI_RET_EAX_EDX) != 0;
    int ret_x87 = agg && (in->agg_class & CC_ABI_RET_X87) != 0;
    /* regparm: the hidden pointer goes in %eax, loaded with the register arguments, not pushed */
    int sret_reg = agg && (in->agg_class & CC_ABI_RET_SRET_REG) != 0;
    int sret = agg && !in_regs && !ret_vec && !ret_x87 && !sret_reg;
    long *offs = (long *)calloc(in->arg_count > 0 ? in->arg_count : 1, sizeof(*offs));
    long pos = sret ? 4 : 0;
    long cur;
    int nx = 0;
    int nm = 0;
    int plain = i386_call_plain(f, lay, in);
    i386_arg_move_t mv[3];
    int nmv = 0;
    int target_reg = -1; /* an indirect plain call's target's register */

    if (offs == NULL) {
        return -1;
    }
    if (plain) {
        /*
         * the register arguments where they are now; the stack ones, pushed from their slots, there --
         * a constant made where it is read and a value with a home register are not yet
         */
        nmv = i386_note_arg_moves(f, lay, ist, in, mv);
        if (in->op == CC_SSA_CALLI && nmv < 3) {
            /* an indirect call's target into a register no argument has: called through it */
            static const char *const order[3] = {"%ecx", "%edx", "%eax"};
            int o;

            for (o = 0; o < 3 && target_reg < 0; ++o) {
                int r = int_reg_index(ist, order[o]);
                int k;
                int taken = 0;

                for (k = 0; k < nmv; ++k) {
                    taken |= mv[k].target == r;
                }
                if (!taken) {
                    target_reg = r;
                }
            }
            i386_note_move(lay, ist, in->lhs, target_reg, &mv[nmv]);
            nmv++;
        }
        for (a = in->op == CC_SSA_CALLI && target_reg < 0 ? -1 : 0; a < (long)in->arg_count; ++a) {
            /* -1: an indirect call's target, called from its slot */
            int v = a < 0 ? in->lhs : in->args[a];
            long k;

            if (a >= 0 && i386_call_arg_regparm(f, in, (size_t)a) > 0) {
                continue;
            }
            if (i386_remat_const(ist, v, &k)) {
                fprintf(fp, "\tmovl $%ld, %d(%%ebp)\n", k, slot_off(lay, v));
            } else if (ist->remat != NULL && ist->remat[v] == 2) {
                fprintf(fp, "\tmovl $%s%+ld, %d(%%ebp)\n", ist->remat_sym[v], (long)ist->remat_imm[v],
                        slot_off(lay, v));
            } else if (ist->home != NULL && ist->home[v] >= 0) {
                fprintf(fp, "\tmovl %s, %d(%%ebp)\n", ist->regs[ist->home[v]], slot_off(lay, v));
            }
        }
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
    /* the body runs with %esp aligned to the stack boundary (16, the psABI's); it is so again at the call */
    cur = (long)align_up_size((size_t)pos, (size_t)i386_stack_boundary());
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
        if (offs[a] >= 0 || (in->call_arg_abi != NULL && in->call_arg_abi[a] == CC_CALL_ARG_ABI_ZERO) ||
            i386_call_arg_regparm(f, in, (size_t)a) > 0) {
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

    /* regparm: the register arguments last, once nothing else needs %eax, %edx, %ecx */
    if (plain) {
        emit_i386_arg_moves(fp, lay, ist, mv, nmv);
    } else {
        unsigned rp = emit_i386_load_regparm_args(fp, f, lay, in);

        if (sret_reg) {
            fprintf(fp, "\tleal %d(%%ebp), %%eax\n", stackalloc_off(lay, in->dst));
            rp |= 1u;
        }
        if (rp != 0 && in->op == CC_SSA_CALLI && is_stackalloc_value(lay, in->lhs)) {
            return -1; /* the target would need %eax */
        }
        if ((rp & 4u) != 0 && in->op == CC_SSA_CALL && in->call_chain > 0) {
            return -1; /* a nested function's static chain is %ecx too */
        }
    }

    if (target_reg >= 0) {
        fprintf(fp, "\tcall *%s\n", ist->regs[target_reg]);
    } else {
        emit_i386_call_target(fp, f, lay, in);
    }

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

        /* by its size: a 1- or 2-byte structure (-freg-struct-return) has no room for all of %eax */
        if (in->imm == 1) {
            fprintf(fp, "\tmovb %%al, %d(%%ebp)\n", off);
        } else if (in->imm == 2) {
            fprintf(fp, "\tmovw %%ax, %d(%%ebp)\n", off);
        } else {
            fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", off);
        }
        if (in->imm > 4) {
            fprintf(fp, "\tmovl %%edx, %d(%%ebp)\n", off + 4);
        }
    } else if (ret_x87) {
        /* -freg-struct-return: a structure of one float or double came back in %st0 */
        fprintf(fp, "\t%s %d(%%ebp)\n", in->imm == 4 ? "fstps" : "fstpl", stackalloc_off(lay, in->dst));
    } else if (!sret && !sret_reg) {
        /* (a hidden pointer's callee filled the buffer itself: %eax holds only its address) */
        emit_i386_capture_call_result(fp, f, lay, ist, in);
    }
    return 0;
}

/** A line of assembly read from @p in into *@p buf (grown as needed), its newline dropped; 0 at the end. */
static int i386_read_line(FILE *in, char **buf, size_t *cap) {
    size_t n = 0;
    int c;

    while ((c = fgetc(in)) != EOF && c != '\n') {
        if (n + 1 >= *cap) {
            size_t ncap = *cap == 0 ? 128 : *cap * 2;
            char *next = (char *)realloc(*buf, ncap);

            if (next == NULL) {
                return -1;
            }
            *buf = next;
            *cap = ncap;
        }
        (*buf)[n++] = (char)c;
    }
    if (c == EOF && n == 0) {
        return 0;
    }
    if (*buf == NULL) {
        *buf = (char *)malloc(1);
        if (*buf == NULL) {
            return -1;
        }
        *cap = 1;
    }
    (*buf)[n] = '\0';
    return 1;
}

/**
 * Split `\tmovl SRC, DST` in @p line into its operands (copied to @p src and @p dst, @p size each).
 *
 * @return 1 if it is one, else 0
 */
static int i386_split_movl(const char *line, char *src, char *dst, size_t size) {
    const char *p;
    const char *comma;

    if (strncmp(line, "\tmovl ", 6) != 0) {
        return 0;
    }
    p = line + 6;
    comma = strstr(p, ", ");
    if (comma == NULL || (size_t)(comma - p) >= size || strlen(comma + 2) >= size || strchr(comma + 2, ',') != NULL) {
        return 0;
    }
    memcpy(src, p, (size_t)(comma - p));
    src[comma - p] = '\0';
    snprintf(dst, size, "%s", comma + 2);
    return 1;
}

/** Whether operand @p op is a 32-bit register: %eax ... %edi. */
static int i386_is_reg32(const char *op) {
    return strlen(op) == 4 && op[0] == '%' && op[1] == 'e';
}

/** Whether operand @p op is a frame slot: N(%ebp). */
static int i386_is_frame_slot(const char *op) {
    size_t n = strlen(op);
    const char *p = op;

    if (n < 7 || strcmp(op + n - 6, "(%ebp)") != 0) {
        return 0;
    }
    if (*p == '-') {
        p++;
    }
    if (p == op + n - 6) {
        return 0;
    }
    for (; p < op + n - 6; ++p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    return 1;
}

/**
 * Copy a function's assembly from @p in to @p out, with what a store to a frame slot makes plain after it:
 * `movl %r, M` then `movl M, %r` keeps the store alone, `movl M, %s` reads %r instead; `movl M, %r` then
 * `movl %r, M` keeps the load alone; `movl %r, %r` goes.  Only adjacent lines: a label or any other line
 * between them ends the match.
 *
 * @return 0, or -1 when out of memory
 */
static int i386_peephole_copy(FILE *in, FILE *out) {
    char *line = NULL;
    size_t cap = 0;
    char prev_src[64];
    char prev_dst[64];
    int have_prev = 0; /* the line before was a movl, its operands in prev_src, prev_dst */
    int rc;

    rewind(in);
    while ((rc = i386_read_line(in, &line, &cap)) > 0) {
        char src[64];
        char dst[64];

        if (!i386_split_movl(line, src, dst, sizeof(src))) {
            fprintf(out, "%s\n", line);
            /* a line-table or frame-description directive is no instruction: the movl before is still the last */
            if (strncmp(line, "\t.loc ", 6) != 0 && strncmp(line, "\t.cfi_", 6) != 0) {
                have_prev = 0;
            }
            continue;
        }
        if (strcmp(src, dst) == 0 && i386_is_reg32(src)) {
            /* movl %r, %r: nothing; and what came before is still the last */
            continue;
        }
        if (have_prev && i386_is_reg32(prev_src) && i386_is_frame_slot(prev_dst) && strcmp(src, prev_dst) == 0 &&
            i386_is_reg32(dst)) {
            /* the slot was just written from prev_src */
            if (strcmp(dst, prev_src) == 0) {
                continue;
            }
            fprintf(out, "\tmovl %s, %s\n", prev_src, dst);
            have_prev = 0;
            continue;
        }
        if (have_prev && i386_is_frame_slot(prev_src) && i386_is_reg32(prev_dst) && strcmp(src, prev_dst) == 0 &&
            strcmp(dst, prev_src) == 0) {
            /* the register was just read from that slot */
            continue;
        }
        fprintf(out, "%s\n", line);
        snprintf(prev_src, sizeof(prev_src), "%s", src);
        snprintf(prev_dst, sizeof(prev_dst), "%s", dst);
        have_prev = 1;
    }
    free(line);
    return rc < 0 ? -1 : 0;
}

/** Whether assembly @p line reads the flags an instruction before it set: a conditional jump, setcc, ... */
static int i386_reads_flags(const char *line) {
    static const char *const ops[] = {"\tj", "\tset", "\tadc", "\tsbb", "\tcmov", "\tpushf"};
    size_t i;

    if (line == NULL) {
        /* the function's end: nothing reads them */
        return 0;
    }
    if (strncmp(line, "\tjmp", 4) == 0) {
        return 0;
    }
    if (strncmp(line, "\t.", 2) == 0 || strncmp(line, "\t#", 2) == 0) {
        /* a directive or a comment: what reads them may come next, unseen */
        return 1;
    }
    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        if (strncmp(line, ops[i], strlen(ops[i])) == 0) {
            return 1;
        }
    }
    /* a label: a jump to it may read them (none does; no matter) */
    return line[0] != '\t';
}

/**
 * The extension @p line (and, for a pair of shifts, @p next) makes of a 32-bit register, as a movsx/movzx
 * line in @p buf: `shll $24, %r` + `sarl $24, %r` is `movsbl`, + `shrl` `movzbl`; 16 the same with
 * `movswl`/`movzwl`; `andl $255, %r` is `movzbl`, `andl $65535, %r` `movzwl`.
 *
 * @return the lines it replaces (1 or 2), or 0
 */
static int i386_ext_line(const char *line, const char *next, char *buf, size_t size) {
    static const char *const regs[] = {"%eax", "%ecx", "%edx", "%ebx", "%esi", "%edi"};
    static const char *const byte_regs[] = {"%al", "%cl", "%dl", "%bl", NULL, NULL};
    char op[16];
    char other[16];
    int amount;
    int r;

    for (r = 0; r < 6; ++r) {
        char want[48];

        snprintf(want, sizeof(want), "\tandl $255, %s", regs[r]);
        if (strcmp(line, want) == 0 && byte_regs[r] != NULL) {
            snprintf(buf, size, "\tmovzbl %s, %s", byte_regs[r], regs[r]);
            return 1;
        }
        snprintf(want, sizeof(want), "\tandl $65535, %s", regs[r]);
        if (strcmp(line, want) == 0) {
            snprintf(buf, size, "\tmovzwl %%%s, %s", regs[r] + 2, regs[r]);
            return 1;
        }
        for (amount = 16; amount <= 24; amount += 8) {
            snprintf(want, sizeof(want), "\tshll $%d, %s", amount, regs[r]);
            if (next == NULL || strcmp(line, want) != 0) {
                continue;
            }
            if (sscanf(next, "\t%15s $%*d, %15s", op, other) != 2 || strcmp(other, regs[r]) != 0) {
                continue;
            }
            snprintf(want, sizeof(want), "\t%s $%d, %s", op, amount, regs[r]);
            if (strcmp(next, want) != 0 || (strcmp(op, "sarl") != 0 && strcmp(op, "shrl") != 0)) {
                continue;
            }
            if (amount == 24 && byte_regs[r] == NULL) {
                continue;
            }
            snprintf(buf, size, "\tmov%c%cl %s%s, %s", op[1] == 'a' ? 's' : 'z', amount == 24 ? 'b' : 'w',
                     amount == 24 ? byte_regs[r] : "%", amount == 24 ? "" : regs[r] + 2, regs[r]);
            return 2;
        }
    }
    return 0;
}

/**
 * Copy a function's assembly from @p in to @p out with each extension done by shifts or a mask made a
 * movsx/movzx, half the bytes (i386_ext_line()), where nothing after reads the flags those set.
 *
 * @return 0, or -1 when out of memory
 */
static int i386_peephole_ext(FILE *in, FILE *out) {
    char *lines[3] = {NULL, NULL, NULL}; /* a window: the line, and the two after it */
    size_t caps[3] = {0, 0, 0};
    int have[3] = {0, 0, 0};
    int rc = 0;
    int k;

    rewind(in);
    for (k = 0; k < 3; ++k) {
        rc = i386_read_line(in, &lines[k], &caps[k]);
        if (rc <= 0) {
            break;
        }
        have[k] = 1;
    }
    while (rc >= 0 && have[0]) {
        char buf[64];
        int n = i386_ext_line(lines[0], have[1] ? lines[1] : NULL, buf, sizeof(buf));
        int shift;
        int reads = n > 0 && i386_reads_flags(have[n] ? lines[n] : NULL);

        if (reads && n == 1 && lines[1][0] != '\t' && have[2] && strchr(lines[1], ':') != NULL) {
            /* a label: what comes after it is what reads them, falling through (a jump to it brings its own) */
            reads = i386_reads_flags(lines[2]);
        }
        if (n > 0 && !reads) {
            fprintf(out, "%s\n", buf);
        } else {
            n = 1;
            fprintf(out, "%s\n", lines[0]);
        }
        for (shift = 0; shift < n; ++shift) {
            /* the window moves on a line */
            char *first = lines[0];
            size_t first_cap = caps[0];

            lines[0] = lines[1];
            caps[0] = caps[1];
            have[0] = have[1];
            lines[1] = lines[2];
            caps[1] = caps[2];
            have[1] = have[2];
            lines[2] = first;
            caps[2] = first_cap;
            rc = i386_read_line(in, &lines[2], &caps[2]);
            have[2] = rc > 0;
        }
    }
    for (k = 0; k < 3; ++k) {
        free(lines[k]);
    }
    return rc < 0 ? -1 : 0;
}

/**
 * The %ebp offsets assembly @p line names other than as the destination of a plain store (`movl X, N(%ebp)`,
 * whose offset is put in @p *store, else INT_MIN), into @p reads (up to @p max of them).
 *
 * @return how many
 */
static int i386_frame_refs(const char *line, int *reads, int max, int *store) {
    const char *p = line;
    const char *dst = NULL;
    int n = 0;

    *store = INT_MIN;
    if (strncmp(line, "\tmovl ", 6) == 0) {
        const char *comma = strrchr(line, ',');

        if (comma != NULL && strstr(comma, "(%ebp)") != NULL && strstr(comma, "(%ebp)")[6] == '\0' &&
            strstr(line, "(%ebp)") == strstr(comma, "(%ebp)")) {
            dst = comma;
        }
    }
    while ((p = strstr(p, "(%ebp)")) != NULL) {
        const char *q = p;
        long off;

        while (q > line && ((q[-1] >= '0' && q[-1] <= '9') || q[-1] == '-')) {
            q--;
        }
        if (q > line && q[-1] != ' ' && q[-1] != ',' && q[-1] != '\t' && q[-1] != '*' && q[-1] != '(') {
            /* an offset not in decimal (0x18, a symbol's): not told apart */
            reads[0] = INT_MIN;
            return max;
        }
        off = q == p ? 0 : strtol(q, NULL, 10);
        if (dst != NULL && p > dst) {
            *store = (int)off;
        } else if (n < max) {
            reads[n++] = (int)off;
        } else {
            /* more than can be noted: as if every offset were read */
            reads[0] = INT_MIN;
        }
        p += 6;
    }
    return n;
}

/**
 * Whether the flags are dead at line @p from of the @p count @p lines (those @p skip marks gone): before
 * anything reads them, an instruction sets them (or a call or a return makes them the callee's or nobody's)
 * after only moves, leal, pushes and pops, which leave them be.  A label, a jump, anything else: not known.
 */
static int i386_flags_dead_at(char *const *lines, const unsigned char *skip, size_t count, size_t from) {
    static const char *const keep[] = {"\tmov", "\tleal ", "\tpush", "\tpop", "\tleave", "\t.", "\t#"};
    static const char *const set[] = {"\tadd", "\tsub", "\tand", "\tor", "\txor", "\tcmp", "\ttest", "\tinc",
                                      "\tdec", "\tneg", "\tshl", "\tshr", "\tsar", "\timul", "\tcall", "\tret"};
    size_t j;

    for (j = from; j < count; ++j) {
        size_t k;
        int known = 0;

        if (skip != NULL && skip[j]) {
            continue;
        }
        for (k = 0; k < sizeof(set) / sizeof(set[0]); ++k) {
            if (strncmp(lines[j], set[k], strlen(set[k])) == 0) {
                return 1;
            }
        }
        if (strncmp(lines[j], "\tmovs", 5) == 0 && strncmp(lines[j], "\tmovsbl", 7) != 0 &&
            strncmp(lines[j], "\tmovswl", 7) != 0 && strncmp(lines[j], "\tmovsbw", 7) != 0) {
            /* a string move (movsb, movsl): with rep, perhaps, in an asm; not known */
            return 0;
        }
        for (k = 0; k < sizeof(keep) / sizeof(keep[0]); ++k) {
            known |= strncmp(lines[j], keep[k], strlen(keep[k])) == 0;
        }
        if (!known) {
            return 0;
        }
    }
    /* the function's end */
    return 1;
}

/**
 * Assembly @p line with each %ebp offset that names a value slot of [@p lo, @p hi) moved to the slot @p map
 * gives its own (slot s is the @p slot_size bytes below hi - slot_size * s), and each below @p lo raised by
 * @p delta, into @p buf.
 */
static void i386_move_frame_refs(const char *line, int lo, int hi, int slot_size, const int *map, int delta,
                                 char *buf, size_t size) {
    const char *p = line;
    size_t used = 0;

    buf[0] = '\0';
    for (;;) {
        const char *at = strstr(p, "(%ebp)");
        const char *q;
        long off;
        long moved;

        if (at == NULL) {
            snprintf(buf + used, size - used, "%s", p);
            return;
        }
        q = at;
        while (q > p && ((q[-1] >= '0' && q[-1] <= '9') || q[-1] == '-')) {
            q--;
        }
        off = q == at ? 0 : strtol(q, NULL, 10);
        moved = off;
        if (off >= lo && off < hi) {
            int sl = (hi - (int)off - 1) / slot_size;

            moved = off + (long)slot_size * (sl - map[sl]);
        } else if (off < lo) {
            moved = off + delta;
        }
        used += (size_t)snprintf(buf + used, size - used, "%.*s%ld(%%ebp)", (int)(q - p), p, moved);
        if (used >= size) {
            return;
        }
        p = at + 6;
    }
}

/**
 * Copy a function's assembly from @p in to @p out without the stores into its value slots, %ebp offsets
 * [@p lo, @p hi), that nothing reads: no instruction (a leal among them) names an offset from 15 bytes below
 * to 3 above, where an access of any width would overlap the four bytes stored.  A value's slot is written
 * where its register is let go of, whether or not it is read again; the stack objects, a volatile local
 * among them, are outside the range, and a pointer to one does not reach the slots.
 *
 * Then the slots still named are packed: the most named nearest %ebp, where a displacement is a byte, and
 * what is below them raised by the bytes freed (a multiple of 16, the alignment of what is there), which
 * the prologue's `subl $frame_sub, %esp` allocates no more.  Nothing is moved or dropped where a line names
 * the frame in a way not told apart.  *@p moved gets those bytes; the frame below @p lo is that much nearer.
 *
 * @return 0, or -1 when out of memory
 */
static int i386_peephole_dead_slots(FILE *in, FILE *out, int lo, int hi, int slot_size, int frame_sub, int *moved) {
    char **lines = NULL;
    size_t count = 0;
    size_t cap = 0;
    unsigned char *read = NULL; /* per offset in [lo - 16, hi + 4): an access names it */
    unsigned char *dead = NULL; /* per line: a store dropped */
    int *uses = NULL;           /* per slot: the lines that name it */
    int *map = NULL;            /* per slot: where it goes */
    int span = hi - lo + 20;
    int nslots = slot_size > 0 && hi > lo ? (hi - lo) / slot_size : 0;
    int all = 0;                /* an instruction names more than can be told apart: keep every store */
    int delta = 0;
    long prologue = -1;         /* the line that allocates the frame */
    char want[48];
    int rc;
    size_t i;

    rewind(in);
    for (;;) {
        char *line = NULL;
        size_t lcap = 0;

        rc = i386_read_line(in, &line, &lcap);
        if (rc <= 0) {
            free(line);
            break;
        }
        if (count == cap) {
            size_t ncap = cap == 0 ? 256 : cap * 2;
            char **next = (char **)realloc(lines, ncap * sizeof(*lines));

            if (next == NULL) {
                free(line);
                rc = -1;
                break;
            }
            lines = next;
            cap = ncap;
        }
        lines[count++] = line;
    }
    read = rc == 0 && span > 0 ? (unsigned char *)calloc((size_t)span, 1) : NULL;
    dead = rc == 0 ? (unsigned char *)calloc(count + 1, 1) : NULL;
    uses = rc == 0 ? (int *)calloc((size_t)nslots + 1, sizeof(*uses)) : NULL;
    map = rc == 0 ? (int *)calloc((size_t)nslots + 1, sizeof(*map)) : NULL;
    if (read == NULL || dead == NULL || uses == NULL || map == NULL) {
        all = 1;
    }
    for (i = 0; !all && i < count; ++i) {
        int offs[8];
        int store;
        int n = i386_frame_refs(lines[i], offs, 8, &store);
        int k;

        if (n > 0 && offs[0] == INT_MIN) {
            all = 1;
            break;
        }
        for (k = 0; k < n; ++k) {
            if (offs[k] >= lo - 16 && offs[k] < hi + 4) {
                read[offs[k] - (lo - 16)] = 1;
            }
        }
    }
    for (i = 0; !all && i < count; ++i) {
        int offs[8];
        int store = INT_MIN;
        int n = i386_frame_refs(lines[i], offs, 8, &store);
        int k;

        if (store != INT_MIN && store >= lo && store < hi) {
            int m;

            dead[i] = 1;
            for (m = store - 15; m < store + 4 && dead[i]; ++m) {
                dead[i] = !read[m - (lo - 16)];
            }
        }
        if (dead[i]) {
            continue;
        }
        if (store != INT_MIN && n < 8) {
            offs[n++] = store;
        }
        for (k = 0; k < n; ++k) {
            if (offs[k] >= lo && offs[k] < hi) {
                uses[(hi - offs[k] - 1) / slot_size]++;
            }
        }
    }
    if (!all && nslots > 0) {
        /* the slots named, most named first */
        int placed = 0;

        for (;;) {
            int best = -1;
            int sl;

            for (sl = 0; sl < nslots; ++sl) {
                if (uses[sl] > 0 && (best < 0 || uses[sl] > uses[best])) {
                    best = sl;
                }
            }
            if (best < 0) {
                break;
            }
            map[best] = placed++;
            uses[best] = -1;
        }
        snprintf(want, sizeof(want), "\tsubl $%d, %%esp", frame_sub);
        for (i = 0; frame_sub > 0 && i < count && prologue < 0; ++i) {
            if (strcmp(lines[i], want) == 0) {
                prologue = (long)i;
            }
        }
        if (prologue >= 0) {
            /* (a slot no line names needs no place) */
            delta = ((nslots - placed) * slot_size) & ~15;
        }
    }
    for (i = 0; i < count; ++i) {
        if (all || rc != 0) {
            fprintf(out, "%s\n", lines[i]);
        } else if (dead[i]) {
            /* dropped */
        } else if (strncmp(lines[i], "\tmovl $-1, %e", 13) == 0 && strlen(lines[i]) == 15 &&
                   i386_flags_dead_at(lines, dead, count, i + 1)) {
            /* -1 by an or of a byte sign-extended: three bytes, not five, where the flags it sets are dead */
            fprintf(out, "\torl $-1, %s\n", lines[i] + 11);
        } else if ((long)i == prologue && delta > 0) {
            if (frame_sub - delta > 0) {
                fprintf(out, "\tsubl $%d, %%esp\n", frame_sub - delta);
            }
        } else if (strstr(lines[i], "(%ebp)") != NULL && nslots > 0) {
            char buf[512];

            i386_move_frame_refs(lines[i], lo, hi, slot_size, map, delta, buf, sizeof(buf));
            fprintf(out, "%s\n", strlen(buf) + 1 < sizeof(buf) ? buf : lines[i]);
        } else {
            fprintf(out, "%s\n", lines[i]);
        }
        free(lines[i]);
    }
    free(lines);
    free(read);
    free(dead);
    free(uses);
    free(map);
    *moved = all || rc != 0 ? 0 : delta;
    return rc < 0 ? -1 : 0;
}

/** Where a function's assembly is written first: a scratch file, or @p out itself when none can be made. */
static FILE *i386_function_file(FILE *out) {
    FILE *tmp = tmpfile();

    return tmp != NULL ? tmp : out;
}

/**
 * Whether value @p v is a 64-bit integer that lives in its 8-byte slot: one an 8-byte load or store of a
 * stack object copies word by word off the frame pointer (i386_frame_copy64()), with no leal.
 */
static int i386_slot_i64(const cc_ssa_function_t *f, const slot_layout_t *lay, const int_reg_state_t *ist, int v) {
    return v >= 0 && cc_ssa_value_class(f, v) == CC_VAL_I64 && cc_ssa_value_size(f, v) == 8 &&
           lay->slot_size >= 8 && !is_stackalloc_value(lay, v) && (ist->remat == NULL || ist->remat[v] == 0) &&
           (ist->home == NULL || ist->home[v] < 0);
}

/** Copy 8 bytes at %ebp offset @p from to %ebp offset @p to, a word at a time through a scratch register. */
static void i386_frame_copy64(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                              int from, int to) {
    int t = int_regs_scratch(fp, f, lay, ist, -1);
    int w;

    for (w = 0; w < 8; w += 4) {
        fprintf(fp, "\tmovl %d(%%ebp), %s\n", from + w, ist->regs[t]);
        fprintf(fp, "\tmovl %s, %d(%%ebp)\n", ist->regs[t], to + w);
    }
}

int emit_i386(FILE *out, const cc_ssa_module_t *m, const char *src_path, int emit_debug, cc_diag_t *diag) {
    const cc_options_t *opts = cc_options_current();
    size_t i;

    i386_module = m;
    for (i = 0; i < m->func_count; ++i) {
        /* each function written to a scratch file first, and copied out through i386_peephole_copy() */
        FILE *fp = i386_function_file(out);
        const cc_ssa_function_t *f = &m->funcs[i];
        const char *func_sec = (f->attr_section != NULL && f->attr_section[0] != '\0') ? f->attr_section : ".text";
        slot_layout_t lay;
        int_reg_state_t ist;
        const unsigned char *addr_taken = NULL;
        const unsigned char *remat = NULL; /* i386_remat_for() */
        const long long *remat_imm = NULL;
        const char *const *remat_sym = NULL;
        const int *home = NULL;            /* i386_homes_for() */
        unsigned home_used = 0;
        int dst_in_home = 0;               /* the instruction made its result in the result's home */
        int stack_shift = 0;               /* how far the stack objects moved down for the registers pushed */
        int pushed = 0;                    /* the bytes of those registers */
        int slot_lo;                       /* the %ebp offsets of the value slots: [slot_lo, slot_hi) */
        int slot_hi;
        int slot_size;
        int word_lo;                       /* and of the 4-byte slots below them: [word_lo, slot_lo) */
        int frame_sub;                     /* what the prologue's subl allocates */
        int moved;                         /* how far the first peephole pass raised what is below the slots */
        const i386_faddr_t *faddr = NULL;  /* i386_fold_addrs() */
        const int *faddr_alias = NULL;
        char faddr_buf[160];
        int rp_bound[4]; /* per -mregparm register 1..3: the parameter value bound to it, or -1 */
        i386_carry_t carry;
        long kimm;
        size_t j;
        int frame;
        int fn_x87_fp = 0; /* doubles on the x87, not SSE; a long double always is */
        int scratch_off;
        int regparm_home = 0; /* regparm: where the prologue stores %eax, %edx, %ecx; 0 when nothing came in them */
        size_t pi;

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
        /*
         * the values whose address is taken (the only ones a pointer may reach, i386_sync_aliased()), the
         * constants made where they are read, and the addresses folded into their loads and stores
         */
        addr_taken = i386_addr_taken_for(f);
        remat = addr_taken != NULL ? i386_remat_for(f, addr_taken, &remat_imm, &remat_sym) : NULL;
        faddr = remat != NULL ? i386_fold_addrs(f, addr_taken, &faddr_alias) : NULL;
        if (faddr == NULL) {
            set_diag(diag, "out of memory laying out the function's values");
            return -1;
        }
        /*
         * a long double keeps its 80 bits in its slot; a function of words only has word slots: more are near
         * %ebp, at a short displacement.  A folded address's base is read where the address would be.
         */
        if (build_slot_layout(f, i386_function_has_ext(f) ? 16 : i386_words_only(f) ? 4 : 8, 1, faddr_alias, &lay,
                              diag) != 0) {
            return -1;
        }
        /* and the values whose home is %ebx, %esi or %edi */
        home = i386_homes_for(f, addr_taken, remat, faddr, faddr_alias, &home_used);
        if (home == NULL) {
            slot_layout_free(&lay);
            set_diag(diag, "out of memory laying out the function's values");
            return -1;
        }
        stack_shift = 0;
        if (home_used != 0) {
            /*
             * the caller's %ebx, %esi, %edi pushed right below %ebp: the slots start under them, and the stack
             * objects 16 bytes further on at least, where they keep the alignment the layout gave them
             */
            int r;
            int v;

            for (r = 0; r < I386_HOME_REGS; ++r) {
                pushed += (home_used & (1u << r)) != 0 ? 4 : 0;
            }
            stack_shift = (pushed + 15) & ~15;
            lay.bias += stack_shift;
            for (v = 0; v < f->value_count; ++v) {
                if (lay.stackalloc_off[v] != 0) {
                    lay.stackalloc_off[v] -= stack_shift;
                }
            }
        }
        frame = lay.frame_bytes + stack_shift;
        for (pi = 0; pi < f->param_count || (pi == 0 && f->sret_reg); ++pi) {
            int kind = f->param_abi != NULL && pi < f->param_count ? CC_ABI_KIND(f->param_abi[pi]) : -1;

            if (f->sret_reg || ((kind == CC_CALL_ARG_ABI_DEFAULT || kind == CC_CALL_ARG_ABI_AGG_MEM) &&
                                CC_ABI_REGPARM(f->param_abi[pi]) > 0)) {
                /* twelve bytes below the slots, %eax lowest: a value of two or three words reads them in order */
                regparm_home = -(frame + 12);
                if (cc_backend_checked_frame_add(&frame, 12, diag, "i386 regparm home") != 0) {
                    slot_layout_free(&lay);
                    return -1;
                }
                break;
            }
        }
        if (cc_backend_checked_frame_add(&frame, 16, diag, "i386 call scratch area") != 0) {
            slot_layout_free(&lay);
            return -1;
        }
        /* -fPIC: %ebx, which a PLT call loads with the GOT, saved above the scratch area */
        if (i386_pic() && cc_backend_checked_frame_add(&frame, 16, diag, "i386 PIC %ebx save") != 0) {
            slot_layout_free(&lay);
            return -1;
        }
        /*
         * The return address and the saved %ebp leave %esp 8 short of a
         * 16-byte boundary (the psABI aligns it at the call); a frame of
         * 8 more than a multiple of 16 puts the body's %esp on one, and
         * emit_i386_call() pads each call's arguments to keep it there.
         * Under -mpreferred-stack-boundary=2 or 3 (Linux's), 4 or 8: the
         * two words leave %esp on such a boundary, and the frame keeps it.
         */
        if (i386_stack_boundary() >= 16) {
            frame = cc_backend_align_frame_size(frame, 16) + 8;
        } else {
            frame = cc_backend_align_frame_size(frame, i386_stack_boundary());
        }
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
        /* the caller's %ebx, %esi, %edi that values have their homes in, pushed: the frame's first bytes */
        emit_i386_home_saves(fp, home_used, emit_debug, 0);
        if (frame - pushed > 0) {
            fprintf(fp, "\tsubl $%d, %%esp\n", frame - pushed);
        }
        i386_regparm_bound(f, rp_bound);
        if (regparm_home != 0) {
            /*
             * before anything else can touch them; one a parameter's value is bound to stays in the register,
             * and one no parameter came in is not stored
             */
            unsigned rp_used = i386_regparm_used(f);

            if (rp_bound[1] < 0 && (rp_used & 2u) != 0) {
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", regparm_home);
            }
            if (rp_bound[2] < 0 && (rp_used & 4u) != 0) {
                fprintf(fp, "\tmovl %%edx, %d(%%ebp)\n", regparm_home + 4);
            }
            if (rp_bound[3] < 0 && (rp_used & 8u) != 0) {
                fprintf(fp, "\tmovl %%ecx, %d(%%ebp)\n", regparm_home + 8);
            }
        }
        if (i386_pic()) {
            fprintf(fp, "\tmovl %%ebx, %d(%%ebp)\n", scratch_off + 16);
        }
        if (emit_dynalloc_slot_clears(fp, f, &lay, 0, diag) != 0) {
            slot_layout_free(&lay);
            return -1;
        }
        if (int_regs_init(&ist, f, emit_regs32, (int)(sizeof(emit_regs32) / sizeof(emit_regs32[0])), 0, diag) != 0) {
            slot_layout_free(&lay);
            return -1;
        }
        /* values are cached in %eax, %ecx, %edx; %ebx, %esi, %edi are homes */
        ist.alloc_count = I386_CACHE_REGS;
        ist.remat = remat;
        ist.remat_imm = remat_imm;
        ist.remat_sym = remat_sym;
        ist.home = home;
        if (i386_carry_build(&carry, f, ist.reg_count) != 0) {
            int_regs_free(&ist);
            slot_layout_free(&lay);
            set_diag(diag, "out of memory noting the ways into each label");
            return -1;
        }
        /* a word parameter that came in a register is that register's value, not yet in a slot */
        if (regparm_home != 0) {
            static const char *const rp_reg[4] = {NULL, "%eax", "%edx", "%ecx"};
            int r;

            for (r = 1; r <= 3; ++r) {
                if (rp_bound[r] >= 0) {
                    int_regs_bind(&ist, rp_bound[r], int_reg_index(&ist, rp_reg[r]), 1);
                }
            }
        }

        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];
            int use_x87_fp = fn_x87_fp || i386_instr_has_ext(f, in);
            ist.cur_index = (int)j;
            dst_in_home = 0;
            /* the cache's values stay in their registers across what uses only the cache (i386_cache_safe()) */
            /* (a plain call takes its register arguments where they are: emit_i386_call()) */
            if (!i386_cache_safe(f, &ist, in) && !i386_call_plain(f, &lay, in)) {
                /* it may read its operands' slots: a constant made where it is read is put there, and a value with a home */
                int_regs_store_remat_uses(fp, f, &lay, &ist, in);
                int_regs_flush(fp, f, &lay, &ist);
                i386_store_home_uses(fp, f, &lay, &ist, in);
            }

            switch (in->op) {
            case CC_SSA_PARAM: {
                int poff = i386_param_offset(f, in->param_index);

                if (in->dst >= 0 &&
                    (in->dst == rp_bound[1] || in->dst == rp_bound[2] || in->dst == rp_bound[3])) {
                    /* bound to the register it came in at the prologue */
                    break;
                }

                if (f->param_abi != NULL && in->param_index >= 0 && (size_t)in->param_index < f->param_count &&
                    (CC_ABI_KIND(f->param_abi[in->param_index]) == CC_CALL_ARG_ABI_DEFAULT ||
                     CC_ABI_KIND(f->param_abi[in->param_index]) == CC_CALL_ARG_ABI_AGG_MEM) &&
                    CC_ABI_REGPARM(f->param_abi[in->param_index]) > 0) {
                    /* it came in a register: read it where the prologue stored it */
                    poff = regparm_home + 4 * (CC_ABI_REGPARM(f->param_abi[in->param_index]) - 1);
                }
                int param_is_ldouble =
                    (f->param_abi != NULL && in->param_index >= 0 &&
                     (size_t)in->param_index < f->param_count &&
                     f->param_abi[in->param_index] == CC_CALL_ARG_ABI_LDOUBLE)
                        ? 1
                        : 0;
                if (f->param_abi != NULL && in->param_index >= 0 && (size_t)in->param_index < f->param_count &&
                    f->param_abi[in->param_index] == CC_CALL_ARG_ABI_ZERO) {
                    /* an empty structure: nothing came, nothing to read (as on x86-64) */
                    break;
                }
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
                } else if (ist.remat[in->dst]) {
                    /* made where it is read */
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
                emit_i386_addr_of_string_label(fp, m, f, &lay, &ist, in->dst, i, j);
                break;

            case CC_SSA_MOV:
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    emit_i386_mov_fp(fp, f, &lay, in, use_x87_fp);
                } else {
                    int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    if (i386_dies_here(&lay, &ist, in->lhs) && value_size_bytes(f, in->dst, 4) < 8) {
                        /* its last use: its register is the copy's, and it is not written back */
                        int_regs_forget(&ist, in->lhs);
                    }
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
                if (stackalloc_off(&lay, in->dst) == 0) {
                    set_diag(diag, "i386: missing stack allocation offset");
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    return -1;
                }
                /*
                 * nothing to do here: what reads its address is given it by leal, off the frame pointer
                 * (int_regs_load()), or addresses the frame itself (i386_fold_addrs())
                 */
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

            case CC_SSA_REGREAD: {
                /* a global register variable: the register as it is here */
                int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);

                fprintf(fp, "\tmovl %%%s, %s\n", in->sym, ist.regs[rd]);
                break;
            }

            case CC_SSA_FRAMEADDR: {
                /* every function keeps %ebp as its frame pointer: walk the saved ones up */
                int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                long long k;

                fprintf(fp, "\tmovl %%ebp, %s\n", ist.regs[rd]);
                for (k = 0; k < in->imm; ++k) {
                    fprintf(fp, "\tmovl (%s), %s\n", ist.regs[rd], ist.regs[rd]);
                }
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
                if (faddr[in->dst].sym != NULL || remat[in->dst] == 2) {
                    /* folded into the loads and stores that use it, or made where it is read */
                    break;
                }
                emit_i386_addr_of_global_sym(fp, m, f, &lay, &ist, in);
                break;

            case CC_SSA_LADDR:
                emit_i386_addr_of_local_label(fp, f, &lay, &ist, in);
                break;

            case CC_SSA_LOAD:
                /*
                 * Pointer-based loads must observe prior writes to possibly
                 * aliased locals: those whose address is taken.  An 8-byte
                 * load uses scratch registers of its own: all is flushed.
                 */
                if (cc_ssa_value_class(f, in->dst) != CC_VAL_F64 && in->imm > 0 && in->imm <= 4) {
                    i386_sync_aliased(fp, f, &lay, &ist, addr_taken, 0);
                } else {
                    int_regs_flush(fp, f, &lay, &ist);
                }
                if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64) {
                    emit_i386_load_fp_indirect(fp, f, &lay, &ist, in, use_x87_fp);
                } else if (in->imm == 8 && is_stackalloc_value(&lay, in->lhs) && i386_slot_i64(f, &lay, &ist, in->dst)) {
                    /* a 64-bit integer from a stack object to its slot: word by word off the frame pointer */
                    i386_frame_copy64(fp, f, &lay, &ist, stackalloc_off(&lay, in->lhs), slot_off(&lay, in->dst));
                } else if (in->imm > 0 && in->imm <= 4 && is_stackalloc_value(&lay, in->lhs)) {
                    /* from a stack object: addressed off the frame pointer, no leal */
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);

                    fprintf(fp, "\t%s %d(%%ebp), %s\n", i386_load_mnemonic(in->imm, in->is_unsigned),
                            stackalloc_off(&lay, in->lhs), ist.regs[rd]);
                } else if (in->imm > 0 && in->imm <= 4 && (faddr[in->lhs].sym != NULL || remat[in->lhs] == 2)) {
                    /* from a global by its symbol, or a pointer's register plus a constant */
                    int rb;
                    int rd;

                    i386_faddr_operand(fp, f, &lay, &ist, faddr, in->lhs, faddr_buf, sizeof(faddr_buf), &rb);
                    rd = int_regs_define(fp, f, &lay, &ist, in->dst, rb, -1);
                    fprintf(fp, "\t%s %s, %s\n", i386_load_mnemonic(in->imm, in->is_unsigned), faddr_buf,
                            ist.regs[rd]);
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
                 * A store through a pointer may change a local whose address
                 * is taken: those are written back first and forgotten after.
                 * The others stay where they are.  An 8-byte or aggregate
                 * store uses scratch registers of its own: all is flushed.
                 */
                if (cc_ssa_value_class(f, in->rhs) != CC_VAL_F64 && in->imm > 0 && in->imm <= 4) {
                    int rp;
                    int rv;

                    i386_sync_aliased(fp, f, &lay, &ist, addr_taken, 1);
                    if (is_stackalloc_value(&lay, in->lhs) || i386_remat_const(&ist, in->rhs, &kimm) ||
                        faddr[in->lhs].sym != NULL || remat[in->lhs] == 2) {
                        /* to a stack object off the frame pointer or a global by its symbol, a constant as an immediate */
                        char addr[176];
                        const char *sfx = in->imm == 1 ? "b" : in->imm == 2 ? "w" : "l";
                        int ra = -1; /* the register the address is in, if one is */

                        if (is_stackalloc_value(&lay, in->lhs)) {
                            snprintf(addr, sizeof(addr), "%d(%%ebp)", stackalloc_off(&lay, in->lhs));
                        } else if (i386_faddr_operand(fp, f, &lay, &ist, faddr, in->lhs, faddr_buf, sizeof(faddr_buf),
                                                      &ra) != NULL) {
                            snprintf(addr, sizeof(addr), "%s", faddr_buf);
                        } else {
                            rp = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                            ra = rp;
                            snprintf(addr, sizeof(addr), "(%s)", ist.regs[rp]);
                        }
                        if (i386_remat_const(&ist, in->rhs, &kimm)) {
                            long k = in->imm == 1 ? (long)(signed char)kimm : in->imm == 2 ? (long)(short)kimm : kimm;

                            fprintf(fp, "\tmov%s $%ld, %s\n", sfx, k, addr);
                        } else {
                            rv = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, ra);
                            if (in->imm == 1 && rv >= ist.alloc_count && strcmp(ist.regs[rv], "%ebx") != 0) {
                                /* not over the address's register */
                                int t = int_regs_scratch(fp, f, &lay, &ist, ra < ist.alloc_count ? ra : -1);

                                fprintf(fp, "\tmovl %s, %s\n", ist.regs[rv], ist.regs[t]);
                                rv = t;
                            }
                            fprintf(fp, "\tmov%s %s, %s\n", sfx,
                                    in->imm == 1 ? reg32_to8(ist.regs[rv])
                                                 : in->imm == 2 ? reg32_to16(ist.regs[rv]) : ist.regs[rv],
                                    addr);
                        }
                        break;
                    }
                    rp = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    rv = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, rp);
                    if (in->imm == 1 && rv >= ist.alloc_count && strcmp(ist.regs[rv], "%ebx") != 0) {
                        /* %esi and %edi have no byte register: the byte from a copy */
                        int t = int_regs_scratch(fp, f, &lay, &ist, rp < ist.alloc_count ? rp : -1);

                        fprintf(fp, "\tmovl %s, %s\n", ist.regs[rv], ist.regs[t]);
                        rv = t;
                    }
                    emit_i386_store_scalar_indirect(fp, f, &lay, &ist, in, rp, rv, in->imm, in->is_unsigned);
                    break;
                }
                int_regs_flush(fp, f, &lay, &ist);
                if (cc_ssa_value_class(f, in->rhs) == CC_VAL_F64) {
                    emit_i386_store_fp_indirect(fp, f, &lay, &ist, in, use_x87_fp);
                } else if (in->imm == 8 && is_stackalloc_value(&lay, in->lhs) && i386_slot_i64(f, &lay, &ist, in->rhs)) {
                    /* a 64-bit integer from its slot to a stack object: word by word off the frame pointer */
                    i386_frame_copy64(fp, f, &lay, &ist, slot_off(&lay, in->rhs), stackalloc_off(&lay, in->lhs));
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
                    int rl;
                    int rr;
                    int in_home;

                    if (in->op == CC_SSA_ADD && (faddr[in->dst].sym != NULL || remat[in->dst] == 2)) {
                        /* a global's address plus a constant, folded into what uses it or made where it is read */
                        break;
                    }
                    rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    /* x = x op k, x's home its register: the operation is made there (cc_coalesce_moves()) */
                    in_home = rl >= ist.alloc_count && home[in->dst] == rl && in->op != CC_SSA_DIV;

                    if ((in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) &&
                        (in->rhs < 0 ? (kimm = (long)in->imm, 1) : i386_remat_const(&ist, in->rhs, &kimm))) {
                        /* a constant count, in the instruction or a constant value, is an immediate: no %cl */
                        if (in_home) {
                            fprintf(fp, "\t%s $%ld, %s\n",
                                    in->op == CC_SSA_SHL ? "shll" : (in->is_unsigned ? "shrl" : "sarl"), kimm & 31,
                                    ist.regs[rl]);
                            dst_in_home = 1;
                            break;
                        }
                        rl = i386_writable(fp, f, &lay, &ist, rl, -1);
                        if (ist.reg_dirty[rl] && i386_dies_here(&lay, &ist, in->lhs)) {
                            int_regs_forget(&ist, in->lhs);
                        } else if (ist.reg_dirty[rl] && !i386_move_aside(fp, &ist, in->lhs, rl, -1)) {
                            int_regs_spill_reg(fp, f, &lay, &ist, rl);
                            rl = int_regs_load(fp, f, &lay, &ist, in->lhs, rl, -1);
                        }
                        fprintf(fp, "\t%s $%ld, %s\n",
                                in->op == CC_SSA_SHL ? "shll" : (in->is_unsigned ? "shrl" : "sarl"), kimm & 31,
                                ist.regs[rl]);
                        int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                        break;
                    }
                    if (in->op == CC_SSA_DIV || in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) {
                        if (emit_i386_int_div_or_shift(fp, f, &lay, &ist, in, diag) != 0) {
                            int_regs_free(&ist);
                            slot_layout_free(&lay);
                            return -1;
                        }
                        break;
                    }
                    if (in_home) {
                        if (i386_remat_const(&ist, in->rhs, &kimm) && in->op != CC_SSA_MUL) {
                            fprintf(fp, "\t%s $%ld, %s\n", i386_int_binop_mnemonic(in->op), kimm, ist.regs[rl]);
                        } else {
                            rr = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, -1);
                            emit_i386_int_binop_regular(fp, &ist, in, rr, rl);
                        }
                        dst_in_home = 1;
                        break;
                    }
                    /* the result is written over the left operand: not over a value's home */
                    rl = i386_writable(fp, f, &lay, &ist, rl, -1);
                    if (ist.reg_dirty[rl] && i386_dies_here(&lay, &ist, in->lhs) && in->rhs != in->lhs) {
                        /* its last use: the result takes its register, and it is not written back */
                        int_regs_forget(&ist, in->lhs);
                    } else if (ist.reg_dirty[rl] && !i386_move_aside(fp, &ist, in->lhs, rl, -1)) {
                        /* still needed, and no register to keep it in: to its slot */
                        int_regs_spill_reg(fp, f, &lay, &ist, rl);
                        rl = int_regs_load(fp, f, &lay, &ist, in->lhs, rl, -1);
                    }
                    if (i386_remat_const(&ist, in->rhs, &kimm) && i386_int_binop_mnemonic(in->op) != NULL) {
                        /* a constant operand is an immediate */
                        if (in->op == CC_SSA_MUL) {
                            fprintf(fp, "\timull $%ld, %s, %s\n", kimm, ist.regs[rl], ist.regs[rl]);
                        } else {
                            fprintf(fp, "\t%s $%ld, %s\n", i386_int_binop_mnemonic(in->op), kimm, ist.regs[rl]);
                        }
                        int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                        break;
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
                } else if (j + 1 < f->instr_count && f->instrs[j + 1].op == CC_SSA_BR_COND &&
                           f->instrs[j + 1].lhs == in->dst && i386_cache_safe(f, &ist, in) &&
                           lay.live_end != NULL && in->dst >= 0 && lay.live_end[in->dst] == (int)j + 1) {
                    /* a compare only the branch after it reads: the branch is on the flags */
                    emit_i386_cmp_branch(fp, f, &lay, &ist, &carry, in, &f->instrs[j + 1]);
                    ++j;
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
                if (j > 0 && i386_ends_flow(&f->instrs[j - 1])) {
                    /*
                     * Nothing falls in: what the code before held is no one's here, and a write-back of it
                     * would run on the paths that do come in.
                     */
                    if (i386_carry_single(&carry, in->label) && carry.has_snap[in->label]) {
                        /* one jump comes in: the cache it left */
                        i386_carry_restore(&carry, &ist, in->label);
                    } else {
                        i386_drop_cache(&ist);
                    }
                } else if (!i386_carry_single(&carry, in->label)) {
                    /* falling into a label others jump to: they meet with every value in its slot */
                    int_regs_flush(fp, f, &lay, &ist);
                }
                emit_i386_label_def(fp, f->name, in->label);
                break;

            case CC_SSA_BR:
                if (i386_carry_single(&carry, in->label)) {
                    /* the only way into it: the cache goes along */
                    i386_carry_save(&carry, &ist, in->label);
                } else {
                    int_regs_flush(fp, f, &lay, &ist);
                }
                if (!i386_label_follows(f, in, in->label)) {
                    emit_i386_jump_label(fp, f->name, in->label);
                }
                break;

            case CC_SSA_BR_COND:
                emit_i386_branch_cond(fp, f, &lay, &ist, &carry, in);
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
                    /* under regparm the hidden pointer came in %eax, which the prologue stored */
                    int hidden = f->sret_reg ? regparm_home : 8;
                    int_regs_flush(fp, f, &lay, &ist);
                    if (in->lhs >= 0 && in->imm > 0) {
                        emit_load_ptr_value_reg(fp, &lay, in->lhs, 0, "%edx");
                        fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", hidden);
                        emit_i386_call_memcpy(fp, (long)in->imm);
                    }
                    fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", hidden);
                    if (i386_pic()) {
                        fprintf(fp, "\tmovl %d(%%ebp), %%ebx\n", scratch_off + 16);
                    }
                    emit_i386_home_saves(fp, home_used, 0, 1);
                    fprintf(fp, "\tleave\n");
                    if (emit_debug) {
                        fprintf(fp, "\t.cfi_def_cfa %%esp, 4\n");
                    }
                    /* popping the pointer only if it was on the stack */
                    fprintf(fp, f->sret_reg ? "\tret\n" : "\tret $4\n");
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
                } else if (in->lhs >= 0 && (in->agg_class & CC_ABI_RET_X87) != 0) {
                    /* -freg-struct-return: a structure of one float or double, as that value in %st0 */
                    int_regs_flush(fp, f, &lay, &ist);
                    emit_load_ptr_value_reg(fp, &lay, in->lhs, 0, "%eax");
                    fprintf(fp, "\t%s (%%eax)\n", in->imm == 4 ? "flds" : "fldl");
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
                        i386_drop_cache(&ist);
                        if (strcmp(ist.regs[rl], "%eax") != 0) {
                            fprintf(fp, "\tmovl %s, %%eax\n", ist.regs[rl]);
                        }
                    }
                } else {
                    i386_drop_cache(&ist);
                }
                if (i386_pic()) {
                    fprintf(fp, "\tmovl %d(%%ebp), %%ebx\n", scratch_off + 16);
                }
                emit_i386_home_saves(fp, home_used, 0, 1);
                fprintf(fp, "\tleave\n");
                if (emit_debug) {
                    fprintf(fp, "\t.cfi_def_cfa %%esp, 4\n");
                }
                fprintf(fp, "\tret\n");
                break;
            }
            if (in->dst >= 0 && in->dst < f->value_count && home[in->dst] >= 0 && !dst_in_home) {
                /* a value with a home: from the register or slot it was made in to its home */
                int rd = ist.val_reg[in->dst];

                if (rd >= 0) {
                    fprintf(fp, "\tmovl %s, %s\n", ist.regs[rd], ist.regs[home[in->dst]]);
                    int_regs_forget(&ist, in->dst);
                } else {
                    fprintf(fp, "\tmovl %d(%%ebp), %s\n", slot_off(&lay, in->dst), ist.regs[home[in->dst]]);
                }
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
        slot_lo = -(lay.bias + lay.slot_size * lay.slot_count);
        slot_hi = -lay.bias;
        slot_size = lay.slot_size;
        word_lo = slot_lo - 4 * lay.word_count;
        frame_sub = frame - pushed;
        slot_layout_free(&lay);
        if (fp != out) {
            /*
             * the extensions made movsx/movzx first, and the stores nothing reads dropped, each into another
             * scratch file when one can be made
             */
            FILE *ext = tmpfile();
            FILE *dead;
            int prc = ext != NULL ? i386_peephole_ext(fp, ext) : 0;

            if (ext != NULL) {
                fclose(fp);
                fp = ext;
            }
            dead = prc == 0 ? tmpfile() : NULL;
            moved = 0;
            prc = dead != NULL ? i386_peephole_dead_slots(fp, dead, slot_lo, slot_hi, slot_size, frame_sub, &moved)
                               : prc;
            if (dead != NULL) {
                fclose(fp);
                fp = dead;
            }
            if (prc == 0 && word_lo < slot_lo) {
                /* the 4-byte slots, the first pass having raised them and what is below by moved */
                int moved2 = 0;

                dead = tmpfile();
                prc = dead != NULL ? i386_peephole_dead_slots(fp, dead, word_lo + moved, slot_lo + moved, 4,
                                                              frame_sub - moved, &moved2)
                                   : prc;
                if (dead != NULL) {
                    fclose(fp);
                    fp = dead;
                }
            }
            prc = prc != 0 ? prc : i386_peephole_copy(fp, out);
            fclose(fp);
            if (prc != 0) {
                set_diag(diag, "out of memory copying a function's assembly");
                return -1;
            }
        }
    }
    emit_i386_pc_thunks(out);

    return 0;
}

