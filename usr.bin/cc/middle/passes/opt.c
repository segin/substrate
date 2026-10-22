#include "cc_middle.h"
#include "cc_options.h"
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

/**
 * The integer values that are constant on every path: defined once, by a
 * CONST.  What fold_function() learns between labels it must forget at a
 * label (another path may join there), but not these -- `switch
 * (sizeof(x))` compares a value set to the size before the first label.
 *
 * @return an array of f->value_count states (free it), or NULL when out of memory
 */
static const_state_t *single_def_consts(const cc_ssa_function_t *f) {
    const_state_t *g = (const_state_t *)calloc((size_t)f->value_count, sizeof(*g));
    int *defs = (int *)calloc((size_t)f->value_count, sizeof(*defs));
    size_t i;

    if (g == NULL || defs == NULL) {
        free(g);
        free(defs);
        return NULL;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int d = in->dst;
        size_t k;

        if (d >= 0 && d < f->value_count) {
            defs[d]++;
        }
        /* an asm output writes its value too, without being the instruction's dst */
        for (k = 0; in->op == CC_SSA_ASM && in->asm_out_values != NULL && k < in->asm_out_count; ++k) {
            int v = in->asm_out_values[k];

            if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
                v = CC_SSA_ASM_MEM_INDIRECT_DECODE(v);
            }
            if (v >= 0 && v < f->value_count) {
                defs[v] += 2;
            }
        }
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int d = in->dst;
        if (in->op == CC_SSA_CONST && d >= 0 && d < f->value_count && defs[d] == 1 &&
            cc_ssa_value_class(f, d) == CC_VAL_I64) {
            g[d].known = 1;
            g[d].type = CC_VAL_I64;
            g[d].i = in->imm;
        }
    }
    /*
     * A value every definition of which is a move of one and the same such
     * constant is that constant too: the result of `0 && x` once the arm
     * that computed x is gone (Linux's `IS_ENABLED(CONFIG_TMPFS) &&
     * is_tmpfs`), and of `x && 0`, 0 on both paths.
     */
    for (;;) {
        int changed = 0;
        size_t v;

        for (v = 0; v < (size_t)f->value_count; ++v) {
            int agree = 0;
            long long val = 0;
            int ok = defs[v] > 0 && !g[v].known && cc_ssa_value_class(f, (int)v) == CC_VAL_I64;

            for (i = 0; ok && i < f->instr_count; ++i) {
                const cc_ssa_instr_t *in = &f->instrs[i];

                if (in->dst != (int)v) {
                    continue;
                }
                if (in->op != CC_SSA_MOV || in->lhs < 0 || in->lhs >= f->value_count || !g[in->lhs].known ||
                    cc_ssa_value_size(f, (int)v) != cc_ssa_value_size(f, in->lhs) ||
                    (agree > 0 && g[in->lhs].i != val)) {
                    ok = 0;
                    break;
                }
                val = g[in->lhs].i;
                agree++;
            }
            if (ok && agree == defs[v]) {
                g[v].known = 1;
                g[v].type = CC_VAL_I64;
                g[v].i = val;
                changed = 1;
            }
        }
        if (!changed) {
            break;
        }
    }
    free(defs);
    return g;
}

static int value_is_ldouble(const cc_ssa_function_t *f, int v) {
    return f != NULL && v >= 0 && v < f->value_count && cc_ssa_value_size(f, v) >= 16;
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
    if (v < 0 || v >= f->value_count || cc_ssa_value_size(f, v) == 0) {
        return 1;
    }
    return cc_ssa_value_size(f, v) < 8;
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

/**
 * Fold what is constant in @p f: arithmetic, compares and conversions of known values become
 * CONSTs, and with @p fold_branches a branch on a known condition a jump.
 *
 * @return 1 if anything changed, 0 if not, -1 when out of memory
 */
static int fold_function(cc_ssa_function_t *f, int fold_branches) {
    const_state_t *st;
    const_state_t *g;
    size_t i;
    int changed = 0;

    if (f->value_count <= 0) {
        return 0;
    }

    st = (const_state_t *)calloc((size_t)f->value_count, sizeof(*st));
    if (st == NULL) {
        return -1;
    }
    g = single_def_consts(f);
    if (g == NULL) {
        free(st);
        return -1;
    }
    memcpy(st, g, (size_t)f->value_count * sizeof(*st));

    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];
        int dst = in->dst;
        cc_value_type_t vt = CC_VAL_I64;

        if (dst >= 0 && (size_t)dst < (size_t)f->value_count) {
            vt = cc_ssa_value_class(f, dst);
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
        case CC_SSA_FRAMEADDR:
        case CC_SSA_REGREAD:
        case CC_SSA_LOAD:
        case CC_SSA_LO32:
        case CC_SSA_HI32:
        case CC_SSA_PAIR:
        case CC_SSA_UMULW:
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
                int to_unsigned = in->is_unsigned || cc_ssa_value_is_unsigned(f, dst);

                in->op = CC_SSA_CONST;
                in->lhs = -1;
                in->rhs = -1;
                /* 2^63 and more fit only an unsigned long long; as long long the conversion is undefined */
                in->imm = (to_unsigned && src >= 9223372036854775808.0L) ? (long long)(unsigned long long)src
                                                                          : (long long)src;
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
        case CC_SSA_EXT: /* legalization, which runs first, expands every one */
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
            memcpy(st, g, (size_t)f->value_count * sizeof(*st));
            break;

        case CC_SSA_BR_COND:
            /* a branch on a known condition goes one way: unreachable_function() removes the other */
            if (fold_branches && in->lhs >= 0 && in->lhs < f->value_count && st[in->lhs].known &&
                st[in->lhs].type == CC_VAL_I64) {
                in->label = st[in->lhs].i != 0 ? in->true_label : in->false_label;
                in->op = CC_SSA_BR;
                in->lhs = -1;
                changed = 1;
            }
            break;

        case CC_SSA_BR:
        case CC_SSA_TRAP:
        case CC_SSA_RET:
            break;
        }
    }

    free(st);
    free(g);
    return changed;
}

/**
 * The value of integer value @p v if it is the same on every path: defined
 * once, by a CONST, or by integer arithmetic, a compare or a move of
 * values that are themselves so -- `sizeof(void *) == 2` lowers to a
 * chain of them.  @p def_at gives each value's one defining instruction
 * (or < 0), @p g the single-def CONSTs.
 *
 * @return 1 with *@p out set, or 0 if it is not known
 */
static int eval_invariant(const cc_ssa_function_t *f, int v, const int *def_at, const const_state_t *g, int depth,
                          long long *out) {
    const cc_ssa_instr_t *in;
    long long a;
    long long b;

    if (v < 0 || v >= f->value_count || depth > 16) {
        return 0;
    }
    if (g[v].known) {
        *out = g[v].i;
        return 1;
    }
    if (def_at[v] < 0 || cc_ssa_value_class(f, v) != CC_VAL_I64) {
        return 0;
    }
    in = &f->instrs[def_at[v]];
    switch (in->op) {
    case CC_SSA_MOV:
        return eval_invariant(f, in->lhs, def_at, g, depth + 1, out);
    case CC_SSA_CMP:
        if (!eval_invariant(f, in->lhs, def_at, g, depth + 1, &a) || !eval_invariant(f, in->rhs, def_at, g, depth + 1, &b)) {
            return 0;
        }
        *out = fold_int_cmp(in->cmp_kind, in->is_unsigned, value_is_word32(f, in->lhs) || value_is_word32(f, in->rhs),
                            a, b);
        return 1;
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
        /* a shift by an immediate has its count in imm (x86-64's sign extension: shl 32, ashr 32) */
        if (!eval_invariant(f, in->lhs, def_at, g, depth + 1, &a) ||
            !((in->rhs < 0 && (in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) && (b = in->imm, 1)) ||
              eval_invariant(f, in->rhs, def_at, g, depth + 1, &b))) {
            return 0;
        }
        return fold_int_binop(in->op, in->is_unsigned, value_is_word32(f, v), a, b, out) == 0;
    default:
        return 0;
    }
}

/**
 * At -O0: turn into a jump only a branch whose condition is a constant on
 * every path (eval_invariant()) and rewrite nothing else.  Enough for `if (0)` and `switch
 * (sizeof(x))`, whose dead arms gcc drops at -O0 too, while -O0 code is
 * otherwise as written.
 *
 * @return 1 if a branch was folded, 0 if not, -1 when out of memory
 */
static int fold_const_branches(cc_ssa_function_t *f) {
    const_state_t *g;
    int *def_at;
    size_t i;
    int changed = 0;

    if (f->value_count <= 0) {
        return 0;
    }
    g = single_def_consts(f);
    def_at = (int *)malloc((size_t)f->value_count * sizeof(*def_at));
    if (g == NULL || def_at == NULL) {
        free(g);
        free(def_at);
        return -1;
    }
    for (i = 0; i < (size_t)f->value_count; ++i) {
        def_at[i] = -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        int d = f->instrs[i].dst;
        if (d >= 0 && d < f->value_count) {
            def_at[d] = def_at[d] == -1 ? (int)i : -2; /* -2: defined more than once */
        }
    }
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];
        long long cond = 0;

        if (in->op != CC_SSA_BR_COND) {
            continue;
        }
        if (eval_invariant(f, in->lhs, def_at, g, 0, &cond)) {
            in->label = cond != 0 ? in->true_label : in->false_label;
            in->op = CC_SSA_BR;
            in->lhs = -1;
            changed = 1;
        }
    }
    free(g);
    free(def_at);
    return changed;
}

/**
 * Mark the labels the instruction can transfer control to as reachable.
 *
 * @return 1 if one was newly marked
 */
static int mark_targets(const cc_ssa_instr_t *in, unsigned char *reach, int label_count) {
    int marked = 0;
    size_t k;

#define MARK(l)                                                                                                    \
    do {                                                                                                           \
        int _l = (l);                                                                                              \
        if (_l >= 0 && _l < label_count && !reach[_l]) {                                                           \
            reach[_l] = 1;                                                                                         \
            marked = 1;                                                                                            \
        }                                                                                                          \
    } while (0)
    if (in->op == CC_SSA_BR) {
        MARK(in->label);
    } else if (in->op == CC_SSA_BR_COND) {
        MARK(in->true_label);
        MARK(in->false_label);
    } else if (in->op == CC_SSA_ASM) {
        for (k = 0; k < in->asm_goto_count; ++k) {
            MARK(in->asm_goto_labels[k]);
        }
    }
#undef MARK
    return marked;
}

/** Whether control never passes from the instruction to the next one. */
static int ends_flow(const cc_ssa_instr_t *in) {
    return in->op == CC_SSA_BR || in->op == CC_SSA_BR_COND || in->op == CC_SSA_RET || in->op == CC_SSA_TRAP;
}

/**
 * Remove the instructions no path reaches: those after a jump, a return
 * or a trap up to a label something reachable branches to.  With
 * fold_function() turning a branch on a constant into a jump, this drops
 * the arms of `if (0)` and of `switch (sizeof(x))` that cannot run -- the
 * way Linux writes size-generic inline asm, whose dead arms hold asm for
 * the other sizes (`cmpxchgq` and %rax in an i386 build) that the
 * assembler refuses.  A label whose address is taken (`&&l`) may be
 * jumped to from anywhere and is always kept reachable; labels themselves
 * are never removed.
 *
 * @return 1 if anything was removed, 0 if not, -1 when out of memory
 */
static int unreachable_function(cc_ssa_function_t *f) {
    unsigned char *reach;
    int label_count = f->label_count;
    size_t i;
    size_t w = 0;
    int live;
    int removed = 0;
    int again;

    if (f->instr_count == 0) {
        return 0;
    }
    reach = (unsigned char *)calloc((size_t)(label_count > 0 ? label_count : 1), 1);
    if (reach == NULL) {
        return -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        if (in->op == CC_SSA_LADDR && in->label >= 0 && in->label < label_count) {
            reach[in->label] = 1;
        }
    }
    /* a branch back to a label above it can make more reachable: until nothing changes */
    do {
        again = 0;
        live = 1;
        for (i = 0; i < f->instr_count; ++i) {
            const cc_ssa_instr_t *in = &f->instrs[i];

            if (in->op == CC_SSA_LABEL && in->label >= 0 && in->label < label_count) {
                if (live && !reach[in->label]) {
                    reach[in->label] = 1;
                    again = 1;
                }
                live = reach[in->label];
                continue;
            }
            if (!live) {
                continue;
            }
            again |= mark_targets(in, reach, label_count);
            if (ends_flow(in)) {
                live = 0;
            }
        }
    } while (again);

    live = 1;
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_LABEL) {
            live = in->label >= 0 && in->label < label_count ? reach[in->label] : 1;
        } else if (!live) {
            cc_ssa_instr_free(in);
            removed = 1;
            continue;
        } else if (ends_flow(in)) {
            live = 0;
            f->instrs[w++] = *in;
            continue;
        }
        f->instrs[w++] = *in;
    }
    f->instr_count = w;
    free(reach);
    return removed;
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
        int iv = in->asm_in_values[i];

        if (CC_SSA_ASM_MEM_INDIRECT_P(iv)) {
            /* a memory input by its address uses the address */
            iv = CC_SSA_ASM_MEM_INDIRECT_DECODE(iv);
        }
        if (iv >= 0) {
            uses[iv]++;
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
    case CC_SSA_FRAMEADDR:
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
    case CC_SSA_EXT:
    case CC_SSA_LO32:
    case CC_SSA_HI32:
    case CC_SSA_PAIR:
    case CC_SSA_UMULW:
        return 1;
    case CC_SSA_LABEL:
    case CC_SSA_BR:
    case CC_SSA_BR_COND:
    case CC_SSA_VA_START:
    case CC_SSA_STORE:
    case CC_SSA_DYNALLOC:
    case CC_SSA_CHAIN:
    case CC_SSA_REGREAD:
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

/** The length of symbol reference @p s without an offset: `fn+8` names fn. */
static size_t sym_name_len(const char *s) {
    size_t n = 0;

    while (s[n] != '\0' && s[n] != '+' && s[n] != '-' && s[n] != ' ' && s[n] != '@') {
        n++;
    }
    return n;
}

/** Mark the function @p sym names as kept, and queue it, if it is one not kept yet. */
static void keep_named(const cc_ssa_module_t *m, const char *sym, unsigned char *keep, size_t *stack, size_t *top) {
    size_t n;
    size_t i;

    if (sym == NULL) {
        return;
    }
    n = sym_name_len(sym);
    for (i = 0; i < m->func_count; ++i) {
        const char *name = m->funcs[i].name;

        if (!keep[i] && name != NULL && strlen(name) == n && strncmp(name, sym, n) == 0) {
            keep[i] = 1;
            stack[(*top)++] = i;
        }
    }
}

/**
 * Drop the static functions nothing refers to, as gcc does: one marked
 * `static inline` at every level, any static one from -O1.  Kept are the
 * functions with external linkage, those marked used, constructor or
 * destructor, and whatever a kept function calls or takes the address of,
 * or a global's initializer or an alias names.  Linux's headers define
 * many static inline helpers per file, some for the other architecture
 * (rip_rel_ptr's `leaq ...(%rip)` in an i386 build), which gcc never
 * compiles unless called; cc emitted them all.
 *
 * @return 0, or -1 when out of memory
 */
static int remove_unused_static_functions(cc_ssa_module_t *m, int opt_level) {
    const cc_attr_flags_t pinned = CC_ATTR_USED | CC_ATTR_CONSTRUCTOR | CC_ATTR_DESTRUCTOR;
    unsigned char *keep;
    size_t *stack;
    size_t top = 0;
    size_t i;
    size_t w = 0;

    if (m->func_count == 0) {
        return 0;
    }
    keep = (unsigned char *)calloc(m->func_count, 1);
    stack = (size_t *)malloc(m->func_count * sizeof(*stack));
    if (keep == NULL || stack == NULL) {
        free(keep);
        free(stack);
        return -1;
    }
    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        int removable = (f->storage & CC_STORAGE_STATIC) != 0 && (opt_level > 0 || (f->storage & CC_STORAGE_INLINE) != 0) &&
                        (f->attr_flags & pinned) == 0;

        if (!removable) {
            keep[i] = 1;
            stack[top++] = i;
        }
    }
    for (i = 0; i < m->global_count; ++i) {
        const cc_ssa_global_t *g = &m->globals[i];
        size_t k;

        keep_named(m, g->init_sym, keep, stack, &top);
        keep_named(m, g->attr_alias, keep, stack, &top);
        for (k = 0; k < g->init_item_count; ++k) {
            keep_named(m, g->init_items[k].init_sym, keep, stack, &top);
        }
    }
    while (top > 0) {
        const cc_ssa_function_t *f = &m->funcs[stack[--top]];
        size_t k;

        keep_named(m, f->attr_alias, keep, stack, &top);
        for (k = 0; k < f->instr_count; ++k) {
            const cc_ssa_instr_t *in = &f->instrs[k];

            if (in->sym != NULL && (in->op == CC_SSA_CALL || in->op == CC_SSA_GADDR)) {
                keep_named(m, in->sym, keep, stack, &top);
            }
        }
    }
    for (i = 0; i < m->func_count; ++i) {
        if (!keep[i]) {
            cc_ssa_function_free(&m->funcs[i]);
            continue;
        }
        if (w != i) {
            m->funcs[w] = m->funcs[i];
        }
        w++;
    }
    m->func_count = w;
    free(keep);
    free(stack);
    return 0;
}

int cc_fold_constant_values(cc_ssa_function_t *f) {
    return fold_function(f, 0) < 0 ? -1 : 0;
}

/**
 * Fold, drop unreachable code and remove dead values in @p f.
 *
 * Folding a branch on a constant makes code unreachable; removing it can
 * leave a label with one predecessor fewer and a compare more to fold.
 * Until neither finds anything (or a few rounds).  -O0 folds only the
 * branches whose condition is a constant on every path
 * (fold_const_branches()): gcc drops the dead arms of `if (0)` and
 * `switch (sizeof(x))` even then, and code relies on it -- an arm may
 * hold inline asm the target's assembler refuses.
 *
 * @param f          the function
 * @param opt_level  the -O level
 * @return 0, or -1 when out of memory
 */
static int optimize_function(cc_ssa_function_t *f, int opt_level) {
    int rc1;
    int rc2;
    int rc3 = 0;
    int round = 0;

    if (opt_level <= 0) {
        do {
            rc1 = fold_const_branches(f);
            rc3 = rc1 > 0 ? unreachable_function(f) : 0;
        } while (rc1 > 0 && rc3 >= 0 && ++round < 8);
        rc2 = 0;
    } else {
        do {
            rc1 = fold_function(f, 1);
            rc3 = rc1 < 0 ? 0 : unreachable_function(f);
        } while (rc1 > 0 && rc3 >= 0 && ++round < 8);
        rc2 = dce_function(f);
    }
    return rc1 < 0 || rc2 < 0 || rc3 < 0 ? -1 : 0;
}

/** The index of the function of @p m that @p sym calls, or -1 when it is not one of the module's. */
static long called_function(const cc_ssa_module_t *m, const char *sym) {
    size_t i;

    for (i = 0; i < m->func_count; ++i) {
        if (m->funcs[i].name != NULL && strcmp(m->funcs[i].name, sym) == 0) {
            return (long)i;
        }
    }
    return -1;
}

/**
 * Inline the module's small static inline functions (passes/inline.c) bottom-up: a function is
 * rewritten once every function it calls is, so what it copies has its own calls inlined and is
 * folded; then it is folded again, and a call guarded by what an inlined body made constant goes
 * with its dead arm.  A function on a cycle of calls is not inlined into the cycle.
 *
 * @return 0, or -1 when out of memory
 */
static int inline_module(cc_ssa_module_t *m, int opt_level) {
    size_t n = m->func_count;
    unsigned char *open = (unsigned char *)calloc(n > 0 ? n : 1, 1);
    unsigned char *done = (unsigned char *)calloc(n > 0 ? n : 1, 1);
    size_t *stack = (size_t *)malloc((n > 0 ? n : 1) * sizeof(*stack));
    size_t *next = (size_t *)malloc((n > 0 ? n : 1) * sizeof(*next));
    size_t root;
    int rc = 0;

    if (open == NULL || done == NULL || stack == NULL || next == NULL) {
        rc = -1;
        goto out;
    }
    for (root = 0; root < n && rc == 0; ++root) {
        size_t top = 0;

        if (open[root]) {
            continue;
        }
        open[root] = 1;
        next[root] = 0;
        stack[top++] = root;
        while (top > 0 && rc == 0) {
            size_t fi = stack[top - 1];
            cc_ssa_function_t *f = &m->funcs[fi];
            long gi = -1;

            /* the first function it calls not yet visited, visited first */
            while (next[fi] < f->instr_count && gi < 0) {
                const cc_ssa_instr_t *in = &f->instrs[next[fi]++];

                if (in->op == CC_SSA_CALL && in->sym != NULL) {
                    gi = called_function(m, in->sym);
                    if (gi >= 0 && open[gi]) {
                        gi = -1;
                    }
                }
            }
            if (gi >= 0) {
                open[gi] = 1;
                next[gi] = 0;
                stack[top++] = (size_t)gi;
                continue;
            }
            top--;
            rc = cc_inline_calls(m, fi, done);
            if (rc > 0) {
                rc = optimize_function(&m->funcs[fi], opt_level);
            }
            if (rc > 0) {
                rc = 0;
            }
            done[fi] = 1;
        }
    }
out:
    free(open);
    free(done);
    free(stack);
    free(next);
    return rc < 0 ? -1 : 0;
}

/**
 * Whether calling @p g has no effect but its result: a body that calls nothing, has no asm, traps
 * or alloca, writes and reads memory only in its own stack slots, and has no backward branch, so
 * it always returns.  A weak or aliased function is not known by its body here, nor, under -fPIC,
 * an external one, which another object may interpose.
 */
static int call_has_no_effect(const cc_ssa_function_t *g, int pic) {
    unsigned char *local;
    long *label_at;
    size_t i;
    int ok = 1;

    if ((g->attr_flags & CC_ATTR_WEAK) != 0 || g->attr_alias != NULL || g->instrs == NULL ||
        (pic && (g->storage & CC_STORAGE_STATIC) == 0)) {
        return 0;
    }
    local = (unsigned char *)calloc((size_t)(g->value_count > 0 ? g->value_count : 1), 1);
    label_at = (long *)malloc((size_t)(g->label_count > 0 ? g->label_count : 1) * sizeof(*label_at));
    if (local == NULL || label_at == NULL) {
        free(local);
        free(label_at);
        return 0;
    }
    for (i = 0; i < (size_t)g->label_count; ++i) {
        label_at[i] = -1;
    }
    for (i = 0; i < g->instr_count; ++i) {
        const cc_ssa_instr_t *in = &g->instrs[i];

        if (in->op == CC_SSA_STACKALLOC && in->dst >= 0) {
            local[in->dst] = 1;
        } else if (in->op == CC_SSA_LABEL && in->label >= 0 && in->label < g->label_count) {
            label_at[in->label] = (long)i;
        }
    }
    for (i = 0; i < g->instr_count && ok; ++i) {
        const cc_ssa_instr_t *in = &g->instrs[i];

        switch (in->op) {
        case CC_SSA_LOAD:
        case CC_SSA_STORE:
            /* only its own slots: another load may be of a device register */
            ok = in->lhs >= 0 && local[in->lhs];
            break;
        case CC_SSA_BR:
            ok = in->label >= 0 && in->label < g->label_count && label_at[in->label] > (long)i;
            break;
        case CC_SSA_BR_COND:
            ok = in->true_label >= 0 && in->true_label < g->label_count && in->false_label >= 0 &&
                 in->false_label < g->label_count && label_at[in->true_label] > (long)i &&
                 label_at[in->false_label] > (long)i;
            break;
        case CC_SSA_CALL:
        case CC_SSA_CALLI:
        case CC_SSA_ASM:
        case CC_SSA_TRAP:
        case CC_SSA_VA_START:
        case CC_SSA_DYNALLOC:
        case CC_SSA_CHAIN:
        case CC_SSA_LADDR:
            ok = 0;
            break;
        default:
            break;
        }
    }
    free(local);
    free(label_at);
    return ok;
}

/**
 * Drop each call whose result is not used of a function of the module whose call has no other
 * effect (call_has_no_effect()), as gcc does from what it learns of a body even when it does not
 * inline it: Linux calls the noinline apply_retpolines(__retpoline_sites, __retpoline_sites_end),
 * empty without retpolines, whose linker script then defines neither symbol.  A function that
 * dropped a call is folded again, its arguments' computations going with it.
 *
 * @return 0, or -1 when out of memory
 */
static int drop_effectless_calls(cc_ssa_module_t *m, int opt_level) {
    const cc_options_t *opts = cc_options_current();
    int pic = opts != NULL && opts->pic;
    unsigned char *no_effect;
    size_t i;
    int rc = 0;

    if (m->func_count == 0) {
        return 0;
    }
    no_effect = (unsigned char *)malloc(m->func_count);
    if (no_effect == NULL) {
        return -1;
    }
    for (i = 0; i < m->func_count; ++i) {
        no_effect[i] = (unsigned char)call_has_no_effect(&m->funcs[i], pic);
    }
    for (i = 0; i < m->func_count && rc == 0; ++i) {
        cc_ssa_function_t *f = &m->funcs[i];
        int *uses = (int *)calloc((size_t)(f->value_count > 0 ? f->value_count : 1), sizeof(*uses));
        size_t k;
        size_t w = 0;
        int dropped = 0;

        if (uses == NULL) {
            rc = -1;
            break;
        }
        for (k = 0; k < f->instr_count; ++k) {
            mark_uses(&f->instrs[k], uses);
        }
        for (k = 0; k < f->instr_count; ++k) {
            cc_ssa_instr_t *in = &f->instrs[k];
            long gi;

            if (in->op == CC_SSA_CALL && in->sym != NULL && (in->dst < 0 || uses[in->dst] == 0) &&
                (gi = called_function(m, in->sym)) >= 0 && no_effect[gi]) {
                cc_ssa_instr_free(in);
                dropped = 1;
                continue;
            }
            if (w != k) {
                f->instrs[w] = f->instrs[k];
            }
            w++;
        }
        f->instr_count = w;
        free(uses);
        if (dropped) {
            rc = optimize_function(f, opt_level);
        }
    }
    free(no_effect);
    return rc;
}

int cc_run_middle_passes(cc_ssa_module_t *m, int opt_level, cc_diag_t *diag) {
    size_t i;
    int oom = 0;

    for (i = 0; i < m->func_count && !oom; ++i) {
        oom = optimize_function(&m->funcs[i], opt_level) != 0;
    }
    /* gcc inlines static inline functions from -O1 and folds what they make constant */
    if (!oom && opt_level > 0) {
        oom = inline_module(m, opt_level) != 0 || drop_effectless_calls(m, opt_level) != 0;
    }
    if (oom) {
        if (diag != NULL && diag->message[0] == '\0') {
            diag->path[0] = '\0';
            diag->line = 0;
            diag->col = 0;
            snprintf(diag->message, sizeof(diag->message), "out of memory in optimizer");
        }
        return -1;
    }
    /* after the functions' own passes: a call that was in a dead arm no longer keeps its callee */
    if (remove_unused_static_functions(m, opt_level) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            snprintf(diag->message, sizeof(diag->message), "out of memory in optimizer");
        }
        return -1;
    }
    return 0;
}
