#include "cc_ssa.h"

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

void cc_ssa_instr_free(cc_ssa_instr_t *in) {
    size_t i;

    if (in == NULL) {
        return;
    }

    free(in->sym);
    free(in->args);
    free(in->asm_out_values);
    free(in->asm_out_sizes);
    if (in->asm_out_constraints != NULL) {
        for (i = 0; i < in->asm_out_count; ++i) {
            free(in->asm_out_constraints[i]);
        }
    }
    free(in->asm_out_constraints);
    if (in->asm_out_names != NULL) {
        for (i = 0; i < in->asm_out_count; ++i) {
            free(in->asm_out_names[i]);
        }
    }
    free(in->asm_out_names);
    free(in->asm_in_values);
    free(in->asm_in_sizes);
    if (in->asm_in_constraints != NULL) {
        for (i = 0; i < in->asm_in_count; ++i) {
            free(in->asm_in_constraints[i]);
        }
    }
    free(in->asm_in_constraints);
    if (in->asm_in_names != NULL) {
        for (i = 0; i < in->asm_in_count; ++i) {
            free(in->asm_in_names[i]);
        }
    }
    free(in->asm_in_names);
    if (in->asm_clobbers != NULL) {
        for (i = 0; i < in->asm_clobber_count; ++i) {
            free(in->asm_clobbers[i]);
        }
    }
    free(in->asm_clobbers);
    free(in->asm_goto_labels);
    if (in->asm_goto_names != NULL) {
        for (i = 0; i < in->asm_goto_count; ++i) {
            free(in->asm_goto_names[i]);
        }
    }
    free(in->asm_goto_names);
    memset(in, 0, sizeof(*in));
}

size_t cc_ssa_function_basic_block_count(const cc_ssa_function_t *f) {
    size_t i;
    size_t blocks;

    if (f == NULL || f->instr_count == 0) {
        return(0);
    }

    blocks = 1;
    for (i = 0; i < f->instr_count; ++i) {
        if (f->instrs[i].op == CC_SSA_LABEL) {
            blocks++;
        }
    }
    return(blocks);
}

int cc_ssa_find_label_instr_index(const cc_ssa_function_t *f, int label) {
    size_t i;

    if (f == NULL || label < 0) {
        return(-1);
    }
    for (i = 0; i < f->instr_count; ++i) {
        if (f->instrs[i].op == CC_SSA_LABEL && f->instrs[i].label == label) {
            return((int)i);
        }
    }
    return(-1);
}
