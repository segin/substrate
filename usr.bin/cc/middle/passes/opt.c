#include "cc_middle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int known;
    cc_value_type_t type;
    long i;
    double f;
} const_state_t;

static void clear_known(const_state_t *st, int count) {
    int i;
    for (i = 0; i < count; ++i) {
        st[i].known = 0;
    }
}

static int fold_function(cc_ssa_function_t *f) {
    const_state_t *st;
    size_t i;
    int changed = 0;

    if (f->value_count <= 0) {
        return 0;
    }

    st = (const_state_t *)calloc((size_t)f->value_count, sizeof(*st));
    if (st == NULL) {
        return -1;
    }

    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];
        int dst = in->dst;
        cc_value_type_t vt = CC_VAL_I64;

        if (dst >= 0 && (size_t)dst < (size_t)f->value_count) {
            vt = f->value_types[dst];
        }

        switch (in->op) {
        case CC_SSA_CONST:
            if (dst >= 0) {
                st[dst].known = 1;
                st[dst].type = vt;
                st[dst].i = in->imm;
                st[dst].f = in->fimm;
            }
            break;

        case CC_SSA_MOV:
            if (dst >= 0 && in->lhs >= 0 && st[in->lhs].known) {
                st[dst] = st[in->lhs];
            } else if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_ADDR:
        case CC_SSA_STR:
        case CC_SSA_GADDR:
        case CC_SSA_LADDR:
        case CC_SSA_LOAD:
            if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_STORE:
            break;

        case CC_SSA_I2F:
            if (dst >= 0 && in->lhs >= 0 && st[in->lhs].known && st[in->lhs].type == CC_VAL_I64) {
                long src = st[in->lhs].i;
                in->op = CC_SSA_CONST;
                in->lhs = -1;
                in->rhs = -1;
                in->fimm = (double)src;
                st[dst].known = 1;
                st[dst].type = CC_VAL_F64;
                st[dst].f = in->fimm;
                changed = 1;
            } else if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_F2I:
            if (dst >= 0 && in->lhs >= 0 && st[in->lhs].known && st[in->lhs].type == CC_VAL_F64) {
                double src = st[in->lhs].f;
                in->op = CC_SSA_CONST;
                in->lhs = -1;
                in->rhs = -1;
                in->imm = (long)src;
                st[dst].known = 1;
                st[dst].type = CC_VAL_I64;
                st[dst].i = in->imm;
                changed = 1;
            } else if (dst >= 0) {
                st[dst].known = 0;
            }
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
            if (dst >= 0 && in->lhs >= 0 && in->rhs >= 0 &&
                st[in->lhs].known && st[in->rhs].known &&
                st[in->lhs].type == st[in->rhs].type) {
                if (st[in->lhs].type == CC_VAL_I64) {
                    long a = st[in->lhs].i;
                    long b = st[in->rhs].i;
                    long out = 0;
                    if (in->op == CC_SSA_DIV && b == 0) {
                        st[dst].known = 0;
                        break;
                    }
                    if (in->op == CC_SSA_ADD) {
                        out = a + b;
                    } else if (in->op == CC_SSA_SUB) {
                        out = a - b;
                    } else if (in->op == CC_SSA_MUL) {
                        out = a * b;
                    } else if (in->op == CC_SSA_DIV) {
                        if (in->is_unsigned) {
                            out = (long)((unsigned long)a / (unsigned long)b);
                        } else {
                            out = a / b;
                        }
                    } else if (in->op == CC_SSA_AND) {
                        out = a & b;
                    } else if (in->op == CC_SSA_OR) {
                        out = a | b;
                    } else if (in->op == CC_SSA_XOR) {
                        out = a ^ b;
                    } else if (in->op == CC_SSA_SHL) {
                        out = a << (b & 63);
                    } else {
                        if (in->is_unsigned) {
                            out = (long)((unsigned long)a >> (b & 63));
                        } else {
                            out = a >> (b & 63);
                        }
                    }
                    in->op = CC_SSA_CONST;
                    in->lhs = -1;
                    in->rhs = -1;
                    in->imm = out;
                    st[dst].known = 1;
                    st[dst].type = CC_VAL_I64;
                    st[dst].i = out;
                    changed = 1;
                } else {
                    double a = st[in->lhs].f;
                    double b = st[in->rhs].f;
                    double out = 0.0;
                    if (in->op == CC_SSA_ADD) {
                        out = a + b;
                    } else if (in->op == CC_SSA_SUB) {
                        out = a - b;
                    } else if (in->op == CC_SSA_MUL) {
                        out = a * b;
                    } else if (in->op == CC_SSA_DIV) {
                        out = a / b;
                    } else {
                        st[dst].known = 0;
                        break;
                    }
                    in->op = CC_SSA_CONST;
                    in->lhs = -1;
                    in->rhs = -1;
                    in->fimm = out;
                    st[dst].known = 1;
                    st[dst].type = CC_VAL_F64;
                    st[dst].f = out;
                    changed = 1;
                }
            } else if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_CMP:
            if (dst >= 0 && in->lhs >= 0 && in->rhs >= 0 && st[in->lhs].known && st[in->rhs].known &&
                st[in->lhs].type == CC_VAL_I64 && st[in->rhs].type == CC_VAL_I64) {
                long a = st[in->lhs].i;
                long b = st[in->rhs].i;
                long out = 0;
                if (in->cmp_kind == CC_CMP_EQ) {
                    out = (a == b);
                } else if (in->cmp_kind == CC_CMP_NE) {
                    out = (a != b);
                } else if (in->cmp_kind == CC_CMP_LT) {
                    if (in->is_unsigned) {
                        out = ((unsigned long)a < (unsigned long)b);
                    } else {
                        out = (a < b);
                    }
                } else if (in->cmp_kind == CC_CMP_LE) {
                    if (in->is_unsigned) {
                        out = ((unsigned long)a <= (unsigned long)b);
                    } else {
                        out = (a <= b);
                    }
                } else if (in->cmp_kind == CC_CMP_GT) {
                    if (in->is_unsigned) {
                        out = ((unsigned long)a > (unsigned long)b);
                    } else {
                        out = (a > b);
                    }
                } else {
                    if (in->is_unsigned) {
                        out = ((unsigned long)a >= (unsigned long)b);
                    } else {
                        out = (a >= b);
                    }
                }
                in->op = CC_SSA_CONST;
                in->lhs = -1;
                in->rhs = -1;
                in->imm = out;
                st[dst].known = 1;
                st[dst].type = CC_VAL_I64;
                st[dst].i = out;
                changed = 1;
            } else if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_PARAM:
        case CC_SSA_VA_START:
        case CC_SSA_CALL:
        case CC_SSA_CALLI:
            if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_LABEL:
            /*
             * The current IR stream is linearized and does not maintain
             * per-block dataflow states. Clear known constants at labels so
             * values from one predecessor path cannot be propagated across
             * control-flow joins.
             */
            clear_known(st, f->value_count);
            break;

        case CC_SSA_BR:
        case CC_SSA_BR_COND:
        case CC_SSA_RET:
            break;
        }
    }

    free(st);
    return changed;
}

static void mark_uses(const cc_ssa_instr_t *in, int *uses) {
    size_t i;
    if (in->lhs >= 0) {
        uses[in->lhs]++;
    }
    if (in->rhs >= 0) {
        uses[in->rhs]++;
    }
    for (i = 0; i < in->arg_count; ++i) {
        if (in->args[i] >= 0) {
            uses[in->args[i]]++;
        }
    }
}

static int is_pure(const cc_ssa_instr_t *in) {
    switch (in->op) {
    case CC_SSA_PARAM:
    case CC_SSA_CONST:
    case CC_SSA_MOV:
    case CC_SSA_ADDR:
    case CC_SSA_STR:
    case CC_SSA_GADDR:
    case CC_SSA_LADDR:
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
    case CC_SSA_CMP:
    case CC_SSA_I2F:
    case CC_SSA_F2I:
        return 1;
    case CC_SSA_LABEL:
    case CC_SSA_BR:
    case CC_SSA_BR_COND:
    case CC_SSA_VA_START:
    case CC_SSA_STORE:
    case CC_SSA_CALL:
    case CC_SSA_CALLI:
    case CC_SSA_RET:
        return 0;
    }
    return 0;
}

static int dce_function(cc_ssa_function_t *f) {
    int any = 0;

    for (;;) {
        int *uses;
        size_t i;
        size_t w = 0;
        int removed = 0;

        if (f->value_count <= 0 || f->instr_count == 0) {
            return any;
        }

        uses = (int *)calloc((size_t)f->value_count, sizeof(*uses));
        if (uses == NULL) {
            return -1;
        }

        for (i = 0; i < f->instr_count; ++i) {
            mark_uses(&f->instrs[i], uses);
        }

        for (i = 0; i < f->instr_count; ++i) {
            cc_ssa_instr_t *in = &f->instrs[i];
            if (in->dst >= 0 && is_pure(in) && uses[in->dst] == 0) {
                removed = 1;
                continue;
            }
            if (w != i) {
                f->instrs[w] = f->instrs[i];
            }
            w++;
        }

        free(uses);
        f->instr_count = w;
        if (!removed) {
            break;
        }
        any = 1;
    }

    return any;
}

int cc_run_middle_passes(cc_ssa_module_t *m, int opt_level, cc_diag_t *diag) {
    size_t i;
    (void)diag;

    if (opt_level <= 0) {
        return 0;
    }

    for (i = 0; i < m->func_count; ++i) {
        int rc1 = fold_function(&m->funcs[i]);
        int rc2 = dce_function(&m->funcs[i]);
        if (rc1 < 0 || rc2 < 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                diag->line = 0;
                diag->col = 0;
                snprintf(diag->message, sizeof(diag->message), "out of memory in optimizer");
            }
            return -1;
        }
    }
    return 0;
}
