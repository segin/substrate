#include "cc_middle.h"

#include <stdio.h>
#include <stdlib.h>

static void set_diag(cc_diag_t *diag, const char *msg) {
    if (diag == NULL || diag->message[0] != '\0') {
        return;
    }
    diag->path[0] = '\0';
    diag->line = 0;
    diag->col = 0;
    snprintf(diag->message, sizeof(diag->message), "%s", msg);
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
        if (legalize_function(&m->funcs[i], diag) != 0) {
            return(-1);
        }
    }
    return(0);
}
