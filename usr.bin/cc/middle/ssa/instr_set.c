#include "cc_diag.h"
#include "cc_ssa.h"

#include <stdio.h>

/**
 * Record an invalid-instruction error, naming the instruction; the
 * first one recorded is kept.
 *
 * @param diag   the diagnostic
 * @param msg    what is wrong
 * @param index  the instruction's index in its function
 */
static void set_diag(cc_diag_t *diag, const char *msg, size_t index) {
    char buf[sizeof(diag->message)];

    snprintf(buf, sizeof(buf), "%s at instruction %zu", msg, index);
    cc_diag_set(diag, "", 0, 0, buf);
}

int cc_ssa_opcode_is_terminator(cc_ssa_opcode_t op) {
    if (op == CC_SSA_BR || op == CC_SSA_BR_COND || op == CC_SSA_TRAP || op == CC_SSA_RET) {
        return(1);
    }
    return(0);
}

int cc_ssa_opcode_has_side_effect(cc_ssa_opcode_t op) {
    if (op == CC_SSA_STORE || op == CC_SSA_CALL || op == CC_SSA_CALLI || op == CC_SSA_ASM || op == CC_SSA_TRAP ||
        op == CC_SSA_RET || op == CC_SSA_BR || op == CC_SSA_BR_COND) {
        return(1);
    }
    return(0);
}

int cc_ssa_instr_validate(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, size_t index, cc_diag_t *diag) {
    size_t i;

    if (f == NULL || in == NULL) {
        set_diag(diag, "null SSA function/instruction", index);
        return(-1);
    }

    if (in->dst >= f->value_count || in->lhs >= f->value_count || in->rhs >= f->value_count) {
        set_diag(diag, "SSA value index out of range", index);
        return(-1);
    }

    for (i = 0; i < in->arg_count; ++i) {
        if (in->args != NULL && in->args[i] >= f->value_count) {
            set_diag(diag, "SSA call argument value out of range", index);
            return(-1);
        }
    }

    if (in->op == CC_SSA_BR && in->label < 0) {
        set_diag(diag, "SSA branch has invalid label", index);
        return(-1);
    }
    if (in->op == CC_SSA_BR_COND && (in->true_label < 0 || in->false_label < 0)) {
        set_diag(diag, "SSA conditional branch has invalid label", index);
        return(-1);
    }

    return(0);
}
