#include "cc_middle.h"
#include "cc_target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int known;
    cc_value_type_t type;
    long long i;
    double f;
    long double ld;
} const_state_t;

static void clear_known(const_state_t *st, int count) {
    int i;
    for (i = 0; i < count; ++i) {
        st[i].known = 0;
    }
}

static int value_is_ldouble(const cc_ssa_function_t *f, int v) {
    return f != NULL && f->value_sizes != NULL && v >= 0 && v < f->value_count && f->value_sizes[v] >= 16;
}

/*
 * On a 32-bit target an integer value of fewer than 8 bytes lives in a
 * 32-bit register, and the backend computes it with 32-bit
 * instructions; folding it must do the same.
 */
static int value_is_word32(const cc_ssa_function_t *f, int v) {
    if (cc_target_pointer_size() != 4) {
        return 0;
    }
    if (f->value_sizes == NULL || v < 0 || v >= f->value_count || f->value_sizes[v] == 0) {
        return 1;
    }
    return f->value_sizes[v] < 8;
}

/*
 * Fold an integer binary operation the way the target computes it.
 * Arithmetic is done unsigned so that wrap-around is defined on the
 * host.  Returns -1 for a division the target would trap on.
 */
static int fold_int_binop(cc_ssa_opcode_t op, int is_unsigned, int word32, long long a, long long b,
                          long long *out) {
    unsigned long long ua = (unsigned long long)a;
    unsigned long long ub = (unsigned long long)b;
    unsigned long long r;

    if (word32) {
        unsigned int wa = (unsigned int)ua;
        unsigned int wb = (unsigned int)ub;
        unsigned int w;
        int sa = (int)wa;
        int sb = (int)wb;

        switch (op) {
        case CC_SSA_ADD: w = wa + wb; break;
        case CC_SSA_SUB: w = wa - wb; break;
        case CC_SSA_MUL: w = wa * wb; break;
        case CC_SSA_AND: w = wa & wb; break;
        case CC_SSA_OR: w = wa | wb; break;
        case CC_SSA_XOR: w = wa ^ wb; break;
        case CC_SSA_SHL: w = wa << (wb & 31u); break;
        case CC_SSA_SHR:
            w = is_unsigned ? wa >> (wb & 31u) : (unsigned int)(sa >> (wb & 31u));
            break;
        case CC_SSA_DIV:
            if (wb == 0 || (!is_unsigned && sa == (-2147483647 - 1) && sb == -1)) {
                return -1;
            }
            w = is_unsigned ? wa / wb : (unsigned int)(sa / sb);
            break;
        default:
            return -1;
        }
        *out = (long long)(int)w;
        return 0;
    }

    switch (op) {
    case CC_SSA_ADD: r = ua + ub; break;
    case CC_SSA_SUB: r = ua - ub; break;
    case CC_SSA_MUL: r = ua * ub; break;
    case CC_SSA_AND: r = ua & ub; break;
    case CC_SSA_OR: r = ua | ub; break;
    case CC_SSA_XOR: r = ua ^ ub; break;
    case CC_SSA_SHL: r = ua << (ub & 63u); break;
    case CC_SSA_SHR:
        r = is_unsigned ? ua >> (ub & 63u) : (unsigned long long)(a >> (ub & 63u));
        break;
    case CC_SSA_DIV:
        if (b == 0 || (!is_unsigned && a == (-9223372036854775807LL - 1) && b == -1)) {
            return -1;
        }
        r = is_unsigned ? ua / ub : (unsigned long long)(a / b);
        break;
    default:
        return -1;
    }
    *out = (long long)r;
    return 0;
}

/* Fold a compare at the width the backend compares its operands. */
static long long fold_int_cmp(cc_cmp_kind_t kind, int is_unsigned, int word32, long long a, long long b) {
    if (word32) {
        if (is_unsigned) {
            a = (long long)(unsigned int)a;
            b = (long long)(unsigned int)b;
        } else {
            a = (long long)(int)(unsigned int)a;
            b = (long long)(int)(unsigned int)b;
        }
    } else if (is_unsigned) {
        unsigned long long ua = (unsigned long long)a;
        unsigned long long ub = (unsigned long long)b;
        switch (kind) {
        case CC_CMP_EQ: return ua == ub;
        case CC_CMP_NE: return ua != ub;
        case CC_CMP_LT: return ua < ub;
        case CC_CMP_LE: return ua <= ub;
        case CC_CMP_GT: return ua > ub;
        case CC_CMP_GE: return ua >= ub;
        }
        return 0;
    }
    switch (kind) {
    case CC_CMP_EQ: return a == b;
    case CC_CMP_NE: return a != b;
    case CC_CMP_LT: return a < b;
    case CC_CMP_LE: return a <= b;
    case CC_CMP_GT: return a > b;
    case CC_CMP_GE: return a >= b;
    }
    return 0;
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
                st[dst].ld = value_is_ldouble(f, dst) ? in->fimm_ld : (long double)in->fimm;
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
        case CC_SSA_STACKALLOC:
        case CC_SSA_DYNALLOC:
        case CC_SSA_CHAIN:
        case CC_SSA_LOAD:
            if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_STORE:
            break;

        case CC_SSA_ASM:
            {
                size_t a;
                for (a = 0; a < in->asm_out_count; ++a) {
                    int ov = in->asm_out_values[a];
                    if (CC_SSA_ASM_MEM_INDIRECT_P(ov)) {
                        ov = CC_SSA_ASM_MEM_INDIRECT_DECODE(ov);
                    }
                    if (ov >= 0 && ov < f->value_count) {
                        st[ov].known = 0;
                    }
                }
            }
            break;

        case CC_SSA_I2F:
            if (dst >= 0 && in->lhs >= 0 && st[in->lhs].known && st[in->lhs].type == CC_VAL_I64) {
                long long src = st[in->lhs].i;
                in->op = CC_SSA_CONST;
                in->lhs = -1;
                in->rhs = -1;
                in->fimm = in->is_unsigned ? (double)(unsigned long long)src : (double)src;
                in->fimm_ld = in->is_unsigned ? (long double)(unsigned long long)src : (long double)src;
                st[dst].known = 1;
                st[dst].type = CC_VAL_F64;
                st[dst].f = in->fimm;
                st[dst].ld = in->fimm_ld;
                changed = 1;
            } else if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_F2I:
            if (dst >= 0 && in->lhs >= 0 && st[in->lhs].known && st[in->lhs].type == CC_VAL_F64) {
                long double src = value_is_ldouble(f, in->lhs) ? st[in->lhs].ld : (long double)st[in->lhs].f;
                in->op = CC_SSA_CONST;
                in->lhs = -1;
                in->rhs = -1;
                in->imm = (long long)src;
                st[dst].known = 1;
                st[dst].type = CC_VAL_I64;
                st[dst].i = in->imm;
                changed = 1;
            } else if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_FROUND32:
            if (dst >= 0 && in->lhs >= 0 && st[in->lhs].known && st[in->lhs].type == CC_VAL_F64) {
                float srcf = (float)st[in->lhs].f;
                in->op = CC_SSA_CONST;
                in->lhs = -1;
                in->rhs = -1;
                in->fimm = (double)srcf;
                in->fimm_ld = (long double)srcf;
                st[dst].known = 1;
                st[dst].type = CC_VAL_F64;
                st[dst].f = in->fimm;
                st[dst].ld = in->fimm_ld;
                changed = 1;
            } else if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_FROUND64:
            /* not folded: the source's long double value is not tracked here */
            if (dst >= 0) {
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
            if (dst >= 0 && in->lhs >= 0 && st[in->lhs].known &&
                st[in->lhs].type == CC_VAL_I64 &&
                ((in->rhs >= 0 && st[in->rhs].known && st[in->rhs].type == CC_VAL_I64) ||
                 ((in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) && in->rhs < 0))) {
                {
                    long long a = st[in->lhs].i;
                    long long b = (in->rhs >= 0) ? st[in->rhs].i : in->imm;
                    long long out = 0;
                    if (fold_int_binop(in->op, in->is_unsigned, value_is_word32(f, dst), a, b, &out) != 0) {
                        st[dst].known = 0;
                        break;
                    }
                    in->op = CC_SSA_CONST;
                    in->lhs = -1;
                    in->rhs = -1;
                    in->imm = out;
                    st[dst].known = 1;
                    st[dst].type = CC_VAL_I64;
                    st[dst].i = out;
                    changed = 1;
                }
            } else if (dst >= 0 && in->lhs >= 0 && in->rhs >= 0 &&
                       st[in->lhs].known && st[in->rhs].known &&
                       st[in->lhs].type == CC_VAL_F64 && st[in->rhs].type == CC_VAL_F64) {
                if (value_is_ldouble(f, dst) || value_is_ldouble(f, in->lhs) || value_is_ldouble(f, in->rhs)) {
                    long double a = value_is_ldouble(f, in->lhs) ? st[in->lhs].ld : (long double)st[in->lhs].f;
                    long double b = value_is_ldouble(f, in->rhs) ? st[in->rhs].ld : (long double)st[in->rhs].f;
                    long double out = 0.0L;
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
                    in->fimm = (double)out;
                    in->fimm_ld = out;
                    st[dst].known = 1;
                    st[dst].type = CC_VAL_F64;
                    st[dst].f = in->fimm;
                    st[dst].ld = out;
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
                    in->fimm_ld = (long double)out;
                    st[dst].known = 1;
                    st[dst].type = CC_VAL_F64;
                    st[dst].f = out;
                    st[dst].ld = (long double)out;
                    changed = 1;
                }
            } else if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_CMP:
            if (dst >= 0 && in->lhs >= 0 && in->rhs >= 0 && st[in->lhs].known && st[in->rhs].known &&
                st[in->lhs].type == CC_VAL_I64 && st[in->rhs].type == CC_VAL_I64) {
                int word32 = value_is_word32(f, in->lhs) || value_is_word32(f, in->rhs);
                long long out = fold_int_cmp(in->cmp_kind, in->is_unsigned, word32, st[in->lhs].i, st[in->rhs].i);
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
        case CC_SSA_TRAP:
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
    if (in->call_chain > 0) {
        uses[in->call_chain - 1]++;
    }
    for (i = 0; i < in->arg_count; ++i) {
        if (in->args[i] >= 0) {
            uses[in->args[i]]++;
        }
    }
    for (i = 0; i < in->asm_out_count; ++i) {
        int ov = in->asm_out_values[i];
        if (CC_SSA_ASM_MEM_INDIRECT_P(ov)) {
            ov = CC_SSA_ASM_MEM_INDIRECT_DECODE(ov);
        }
        if (ov >= 0) {
            uses[ov]++;
        }
    }
    for (i = 0; i < in->asm_in_count; ++i) {
        if (in->asm_in_values[i] >= 0) {
            uses[in->asm_in_values[i]]++;
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
    case CC_SSA_STACKALLOC:
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
    case CC_SSA_FROUND32:
    case CC_SSA_FROUND64:
        return 1;
    case CC_SSA_LABEL:
    case CC_SSA_BR:
    case CC_SSA_BR_COND:
    case CC_SSA_VA_START:
    case CC_SSA_STORE:
    case CC_SSA_DYNALLOC:
    case CC_SSA_CHAIN:
    case CC_SSA_CALL:
    case CC_SSA_CALLI:
    case CC_SSA_ASM:
    case CC_SSA_TRAP:
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
                diag->path[0] = '\0';
                diag->line = 0;
                diag->col = 0;
                snprintf(diag->message, sizeof(diag->message), "out of memory in optimizer");
            }
            return -1;
        }
    }
    return 0;
}
