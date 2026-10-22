#include "cc_diag.h"
#include "cc_middle.h"
#include "cc_target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Record a legalization error, which has no source position; the first
 * one recorded is kept.
 *
 * @param diag  the diagnostic
 * @param msg   the message
 */
static void set_diag(cc_diag_t *diag, const char *msg) {
    cc_diag_set(diag, "", 0, 0, msg);
}

/**
 * The value an EXT's expansion computes its intermediate result in: the
 * one lowering reserved, as an integer of @p size bytes, signed.
 *
 * @return the value, or -1 when lowering reserved none
 */
static int ext_scratch(cc_ssa_function_t *f, const cc_ssa_instr_t *ext, long size) {
    int v = ext->rhs;

    if (v >= 0 && v < f->value_count) {
        cc_ssa_value_set_class(f, v, CC_VAL_I64);
        cc_ssa_value_set_unsigned(f, v, 0);
        cc_ssa_value_set_size(f, v, (uint32_t)size);
        return v;
    }
    return -1;
}

/**
 * Write the instructions an EXT becomes on the x86 targets, whose
 * emitters keep an integer in a word (or, on i386, a 64-bit integer in a
 * pair of words) and have no extension of their own:
 *
 *  - a zero extension masks: dst = lhs & (2^bits - 1);
 *  - a sign extension that keeps a whole i386 word is a move;
 *  - any other sign extension shifts the bits to the top of the word and
 *    arithmetically back: dst = (lhs << (w - bits)) >> (w - bits).
 *
 * The word is 64 bits on x86-64, and on i386 for a result of 8 bytes.
 *
 * @param f    the function, for the intermediate values
 * @param ext  the EXT
 * @param out  receives one or two instructions
 * @return     how many, or -1 when lowering reserved no intermediate value
 */
static int expand_ext(cc_ssa_function_t *f, const cc_ssa_instr_t *ext, cc_ssa_instr_t out[2]) {
    long ptr = cc_target_pointer_size();
    long dsize = cc_ssa_value_size(f, ext->dst);
    int bits = (int)ext->imm;
    int wbits = cc_ssa_ext_word_bits(dsize, ptr);

    memset(out, 0, 2 * sizeof(out[0]));
    if (ext->is_unsigned) {
        int c = ext_scratch(f, ext, dsize >= 8 ? 8 : ptr);

        if (c < 0) {
            return -1;
        }
        out[0].op = CC_SSA_CONST;
        out[0].dst = c;
        out[0].lhs = -1;
        out[0].rhs = -1;
        out[0].imm = bits >= 64 ? -1LL : (long long)((1ULL << bits) - 1ULL);
        out[1].op = CC_SSA_AND;
        out[1].is_unsigned = 1;
        out[1].dst = ext->dst;
        out[1].lhs = ext->lhs;
        out[1].rhs = c;
        return 2;
    }
    if (bits >= wbits) {
        out[0].op = CC_SSA_MOV;
        out[0].dst = ext->dst;
        out[0].lhs = ext->lhs;
        out[0].rhs = -1;
        return 1;
    }
    {
        int t = ext_scratch(f, ext, wbits / 8);

        if (t < 0) {
            return -1;
        }
        out[0].op = CC_SSA_SHL;
        out[0].dst = t;
        out[0].lhs = ext->lhs;
        out[0].rhs = -1;
        out[0].imm = wbits - bits;
        out[1].op = CC_SSA_SHR;
        out[1].dst = ext->dst;
        out[1].lhs = t;
        out[1].rhs = -1;
        out[1].imm = wbits - bits;
        return 2;
    }
}

/**
 * Expand every EXT of a function in place (see expand_ext()).
 *
 * @return 0, or -1 when out of memory
 */
static int legalize_ext(cc_ssa_function_t *f, cc_diag_t *diag) {
    size_t i;
    size_t n = 0;
    size_t count = 0;
    cc_ssa_instr_t *next;

    for (i = 0; i < f->instr_count; ++i) {
        count += f->instrs[i].op == CC_SSA_EXT ? 2 : 1;
    }
    if (count == f->instr_count) {
        return 0;
    }
    next = (cc_ssa_instr_t *)malloc(count * sizeof(*next));
    if (next == NULL) {
        set_diag(diag, "out of memory legalizing integer conversions");
        return -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        if (f->instrs[i].op == CC_SSA_EXT) {
            int k = expand_ext(f, &f->instrs[i], &next[n]);

            if (k < 0) {
                free(next);
                set_diag(diag, "out of memory legalizing integer conversions");
                return -1;
            }
            n += (size_t)k;
        } else {
            next[n++] = f->instrs[i];
        }
    }
    free(f->instrs);
    f->instrs = next;
    f->instr_count = n;
    f->instr_cap = count;
    return 0;
}

static int legalize_function(cc_ssa_function_t *f, cc_diag_t *diag) {
    size_t i;
    size_t w;
    int dropped = 0;
    int terminated = 0;
    cc_ssa_instr_t trap;

    if (f == NULL) {
        return(0);
    }

    for (i = 0; i < f->instr_count; ++i) {
        if (cc_ssa_instr_validate(f, &f->instrs[i], i, diag) != 0) {
            return(-1);
        }
    }

    w = 0;
    terminated = 0;
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];
        if (terminated) {
            if (in->op == CC_SSA_LABEL) {
                /* A label starts a new block after a terminator. */
                terminated = 0;
            } else if (in->op == CC_SSA_STACKALLOC) {
                /*
                 * STACKALLOC is a pseudo-definition consumed by backend slot
                 * layout; it is not a runtime side-effecting instruction.
                 * Preserve it even if emitted after a terminator so value->slot
                 * mappings remain valid for later reachable uses.
                 */
            } else {
                dropped = 1;
                cc_ssa_instr_free(in);
                continue;
            }
        }
        if (w != i) {
            f->instrs[w] = f->instrs[i];
            f->instrs[i].sym = NULL;
            f->instrs[i].args = NULL;
            f->instrs[i].asm_out_values = NULL;
            f->instrs[i].asm_out_sizes = NULL;
            f->instrs[i].asm_out_constraints = NULL;
            f->instrs[i].asm_out_names = NULL;
            f->instrs[i].asm_in_values = NULL;
            f->instrs[i].asm_in_sizes = NULL;
            f->instrs[i].asm_in_constraints = NULL;
            f->instrs[i].asm_in_names = NULL;
            f->instrs[i].asm_clobbers = NULL;
            f->instrs[i].asm_goto_labels = NULL;
            f->instrs[i].asm_goto_names = NULL;
        }
        if (cc_ssa_opcode_is_terminator(in->op)) {
            terminated = 1;
        }
        w++;
    }
    f->instr_count = w;

    if (f->instr_count > 0 && cc_ssa_opcode_is_terminator(f->instrs[f->instr_count - 1].op)) {
        return(0);
    }

    trap.op = CC_SSA_TRAP;
    trap.dst = -1;
    trap.lhs = -1;
    trap.rhs = -1;
    trap.imm = 0;
    trap.fimm = 0.0;
    trap.label = -1;
    trap.true_label = -1;
    trap.false_label = -1;
    trap.param_index = -1;
    trap.cmp_kind = CC_CMP_EQ;
    trap.is_unsigned = 0;
    trap.call_is_variadic = 0;
    trap.call_fixed_count = 0;
    trap.sym = NULL;
    trap.args = NULL;
    trap.arg_count = 0;
    trap.asm_volatile = 0;
    trap.asm_is_goto = 0;
    trap.asm_out_values = NULL;
    trap.asm_out_sizes = NULL;
    trap.asm_out_constraints = NULL;
    trap.asm_out_names = NULL;
    trap.asm_out_count = 0;
    trap.asm_in_values = NULL;
    trap.asm_in_sizes = NULL;
    trap.asm_in_constraints = NULL;
    trap.asm_in_names = NULL;
    trap.asm_in_count = 0;
    trap.asm_clobbers = NULL;
    trap.asm_clobber_count = 0;
    trap.asm_goto_labels = NULL;
    trap.asm_goto_names = NULL;
    trap.asm_goto_count = 0;
    if (cc_ssa_function_append_instr(f, &trap, diag) != 0) {
        return(-1);
    }

    if (dropped) {
        set_diag(diag, "dropped dead instructions after terminator during SSA legalize");
    }
    return(0);
}

int cc_middle_legalize_module(cc_ssa_module_t *m, cc_diag_t *diag) {
    size_t i;

    if (m == NULL) {
        return(0);
    }
    for (i = 0; i < m->func_count; ++i) {
        const cc_target_desc_t *tgt = cc_target_current();

        /* constants folded before a target splits what it cannot compute in one operation */
        if (legalize_ext(&m->funcs[i], diag) != 0 || cc_fold_constant_values(&m->funcs[i]) != 0 ||
            (tgt->legalize != NULL && tgt->legalize(&m->funcs[i], diag) != 0) ||
            legalize_function(&m->funcs[i], diag) != 0) {
            return(-1);
        }
    }
    return(0);
}
