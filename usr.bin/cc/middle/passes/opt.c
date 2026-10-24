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

/** The most label x variable states join_state_t keeps; past it labels forget, as before. */
#define JOIN_MAX_STATES (8L * 1024 * 1024)

/**
 * What the edges into each label carry of the function's variables -- the integer values defined
 * more than once, `x = c ? 1 : 2`'s and `a || b`'s result -- so that at a join a variable every
 * incoming edge gives the same constant is that constant.  A label with an edge from later in the
 * stream (a loop), or one an asm goto or a label address reaches, forgets them, as every label did.
 */
typedef struct {
    int nvars;             /**< how many variables */
    int *var_of;           /**< per value: its variable's index, or -1 */
    int *vars;             /**< per variable: its value */
    unsigned char *forget; /**< per label: 1 if it cannot be joined */
    unsigned char *seen;   /**< per label x variable: 0 no edge yet, 1 a constant, 2 varying */
    long long *val;        /**< per label x variable: the constant */
    unsigned char *reached; /**< per label: an edge from code that can run has come into it */
} join_state_t;

static void join_free(join_state_t *j) {
    free(j->var_of);
    free(j->vars);
    free(j->forget);
    free(j->seen);
    free(j->val);
    free(j->reached);
    memset(j, 0, sizeof(*j));
}

/**
 * Set up @p j for @p f; with no variables, or too many states, it joins nothing (nvars 0).
 *
 * @return 0, or -1 when out of memory
 */
static int join_init(join_state_t *j, const cc_ssa_function_t *f) {
    int *defs;
    long *label_at;
    size_t i;
    int v;

    memset(j, 0, sizeof(*j));
    if (f->value_count <= 0 || f->label_count <= 0) {
        return 0;
    }
    defs = (int *)calloc((size_t)f->value_count, sizeof(*defs));
    label_at = (long *)malloc((size_t)f->label_count * sizeof(*label_at));
    j->var_of = (int *)malloc((size_t)f->value_count * sizeof(*j->var_of));
    j->forget = (unsigned char *)calloc((size_t)f->label_count, 1);
    if (defs == NULL || label_at == NULL || j->var_of == NULL || j->forget == NULL) {
        free(defs);
        free(label_at);
        join_free(j);
        return -1;
    }
    for (i = 0; i < (size_t)f->label_count; ++i) {
        label_at[i] = -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->dst >= 0 && in->dst < f->value_count) {
            defs[in->dst]++;
        }
        if (in->op == CC_SSA_LABEL && in->label >= 0 && in->label < f->label_count) {
            label_at[in->label] = (long)i;
        }
    }
    for (v = 0; v < f->value_count; ++v) {
        j->var_of[v] = -1;
        if (defs[v] > 1 && cc_ssa_value_class(f, v) == CC_VAL_I64) {
            j->var_of[v] = j->nvars++;
        }
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        size_t k;

        /* an edge from later in the stream (a loop), or one not a branch's, and the label forgets */
        if (in->op == CC_SSA_BR && in->label >= 0 && in->label < f->label_count && label_at[in->label] <= (long)i) {
            j->forget[in->label] = 1;
        } else if (in->op == CC_SSA_BR_COND) {
            if (in->true_label >= 0 && in->true_label < f->label_count && label_at[in->true_label] <= (long)i) {
                j->forget[in->true_label] = 1;
            }
            if (in->false_label >= 0 && in->false_label < f->label_count && label_at[in->false_label] <= (long)i) {
                j->forget[in->false_label] = 1;
            }
        } else if (in->op == CC_SSA_LADDR && in->label >= 0 && in->label < f->label_count) {
            j->forget[in->label] = 1;
        } else if (in->op == CC_SSA_ASM) {
            for (k = 0; k < in->asm_goto_count; ++k) {
                if (in->asm_goto_labels[k] >= 0 && in->asm_goto_labels[k] < f->label_count) {
                    j->forget[in->asm_goto_labels[k]] = 1;
                }
            }
            /* an asm's outputs are set by the asm: never a join's constant */
            for (k = 0; k < in->asm_out_count; ++k) {
                int ov = in->asm_out_values[k];

                if (CC_SSA_ASM_MEM_INDIRECT_P(ov)) {
                    ov = CC_SSA_ASM_MEM_INDIRECT_DECODE(ov);
                }
                if (ov >= 0 && ov < f->value_count && j->var_of[ov] >= 0) {
                    j->var_of[ov] = -1;
                }
            }
        }
    }
    free(defs);
    free(label_at);
    if (j->nvars == 0 || (long)j->nvars * f->label_count > JOIN_MAX_STATES) {
        join_free(j);
        return 0;
    }
    j->vars = (int *)malloc((size_t)j->nvars * sizeof(*j->vars));
    j->seen = (unsigned char *)calloc((size_t)j->nvars * (size_t)f->label_count, 1);
    j->val = (long long *)malloc((size_t)j->nvars * (size_t)f->label_count * sizeof(*j->val));
    j->reached = (unsigned char *)calloc((size_t)f->label_count, 1);
    if (j->vars == NULL || j->seen == NULL || j->val == NULL || j->reached == NULL) {
        join_free(j);
        return -1;
    }
    for (v = 0; v < f->value_count; ++v) {
        if (j->var_of[v] >= 0) {
            j->vars[j->var_of[v]] = v;
        }
    }
    return 0;
}

/**
 * Record the edge into @p label carrying the variables' states @p st -- if it comes from code that can run
 * (@p live): an edge out of dead code (a branch the folder has not removed yet, a dead label falling into
 * the next) says nothing.
 */
static void join_edge(join_state_t *j, int label, int label_count, const const_state_t *st, int live) {
    int k;

    if (!live || j->nvars == 0 || label < 0 || label >= label_count || j->forget[label]) {
        return;
    }
    j->reached[label] = 1;
    for (k = 0; k < j->nvars; ++k) {
        size_t at = (size_t)label * (size_t)j->nvars + (size_t)k;
        const const_state_t *s = &st[j->vars[k]];
        int known = s->known && s->type == CC_VAL_I64;

        if (j->seen[at] == 0) {
            j->seen[at] = known ? 1 : 2;
            j->val[at] = s->i;
        } else if (j->seen[at] == 1 && (!known || j->val[at] != s->i)) {
            j->seen[at] = 2;
        }
    }
}

/**
 * At @p label, take in @p st what every edge into it agrees on.
 *
 * @return whether the code from the label can run: a label that forgets (a loop's, an address taken) always
 *         can; another only if an edge from code that can run came into it
 */
static int join_enter(const join_state_t *j, int label, int label_count, const_state_t *st) {
    int k;

    if (j->nvars == 0 || label < 0 || label >= label_count || j->forget[label]) {
        return 1;
    }
    if (!j->reached[label]) {
        return 0;
    }
    for (k = 0; k < j->nvars; ++k) {
        size_t at = (size_t)label * (size_t)j->nvars + (size_t)k;

        if (j->seen[at] == 1) {
            const_state_t *s = &st[j->vars[k]];

            s->known = 1;
            s->type = CC_VAL_I64;
            s->i = j->val[at];
        }
    }
    return 1;
}

/**
 * Fold what is constant in @p f: arithmetic, compares and conversions of known values become
 * CONSTs, and with @p fold_branches a branch on a known condition a jump.
 *
 * @return 1 if anything changed, 0 if not, -1 when out of memory
 */
/** Whether value @p v is the integer constant @p k as far as @p st knows, read at @p mask's width. */
static int known_int(const const_state_t *st, int v, unsigned long long k, unsigned long long mask) {
    return v >= 0 && st[v].known && st[v].type == CC_VAL_I64 && ((unsigned long long)st[v].i & mask) == (k & mask);
}

/**
 * The operand @p in yields unchanged, the other a constant that leaves it so -- x & ~0, x | 0, x ^ 0, x + 0,
 * x - 0, x << 0, x >> 0, x * 1, and b != 0 of a @p is_bool value b (NULL: none known) -- when it is an
 * integer of the result's size and signedness; else -1.
 */
static int identity_operand(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, const const_state_t *st,
                            const unsigned char *is_bool) {
    uint32_t size = cc_ssa_value_size(f, in->dst);
    unsigned long long mask = size >= 8 ? ~0ULL : (1ULL << (size * 8)) - 1;
    int x = -1;

    if (in->lhs < 0 || in->rhs < 0 || in->lhs >= f->value_count || in->rhs >= f->value_count || size == 0) {
        return -1;
    }
    switch (in->op) {
    case CC_SSA_AND:
        x = known_int(st, in->rhs, ~0ULL, mask) ? in->lhs : known_int(st, in->lhs, ~0ULL, mask) ? in->rhs : -1;
        break;
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_ADD:
        x = known_int(st, in->rhs, 0, mask) ? in->lhs : known_int(st, in->lhs, 0, mask) ? in->rhs : -1;
        break;
    case CC_SSA_SUB:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
        x = known_int(st, in->rhs, 0, mask) ? in->lhs : -1;
        break;
    case CC_SSA_MUL:
        x = known_int(st, in->rhs, 1, mask) ? in->lhs : known_int(st, in->lhs, 1, mask) ? in->rhs : -1;
        break;
    case CC_SSA_CMP:
        /* a truth value compared unequal to 0 is itself: cc tests each condition so */
        x = in->cmp_kind == CC_CMP_NE && is_bool != NULL && is_bool[in->lhs] && known_int(st, in->rhs, 0, ~0ULL)
                ? in->lhs
                : -1;
        break;
    default:
        break;
    }
    /*
     * of another signedness, a narrow x is held extended the other way: `& 0xff` of a signed char is a
     * zero-extension.  A word on i386 fills its register, and is held one way whatever its signedness.
     */
    if (x < 0 || cc_ssa_value_class(f, x) != CC_VAL_I64 || cc_ssa_value_size(f, x) != size ||
        (cc_ssa_value_is_unsigned(f, x) != cc_ssa_value_is_unsigned(f, in->dst) &&
         !(size == 4 && cc_options_current()->target == CC_TARGET_I386))) {
        return -1;
    }
    return x;
}

/** What is known of a value as an extension of a narrower one (ext_info_build()). */
typedef struct {
    int shl_src; /**< made once by `shl x, k` with x made once: x, else -1 */
    int shl_k;   /**< that k */
    int width;   /**< it is extended from this many bits (0: not known) */
    int sign;    /**< sign-extended (1) or zero-extended (0) */
    int mask_k;  /**< a constant 2^k - 1, a mask of the low k bits: k (else 0) */
    long shl_at; /**< the shl's instruction */
    int shl_src_var; /**< x is a variable, defined more than once */
} ext_info_t;

/**
 * For each value of @p f defined once (@p defs), what makes it an extension: a narrow load (zload or
 * load of 1 or 2 bytes), or `shr (shl x, k), k` -- how legalization extends a narrow integer -- of a word
 * of W bits, extended from W - k.  NULL when out of memory.
 */
static ext_info_t *ext_info_build(const cc_ssa_function_t *f, const int *defs) {
    ext_info_t *e = (ext_info_t *)malloc((size_t)f->value_count * sizeof(*e));
    size_t i;
    int v;

    if (e == NULL) {
        return NULL;
    }
    for (v = 0; v < f->value_count; ++v) {
        e[v].shl_src = -1;
        e[v].shl_k = 0;
        e[v].width = 0;
        e[v].sign = 0;
        e[v].mask_k = 0;
        e[v].shl_at = -1;
        e[v].shl_src_var = 0;
    }
    /* in stream order: a definition comes before what reads it, as each value here is made once */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *d = &f->instrs[i];
        int dst = d->dst;
        uint32_t bits;

        if (dst < 0 || dst >= f->value_count || defs[dst] != 1 || cc_ssa_value_class(f, dst) != CC_VAL_I64) {
            continue;
        }
        bits = 8 * cc_ssa_value_size(f, dst);
        if (d->op == CC_SSA_LOAD && (d->imm == 1 || d->imm == 2) && bits > 8 * (uint32_t)d->imm) {
            e[dst].width = (int)(8 * d->imm);
            e[dst].sign = !d->is_unsigned;
        } else if (d->op == CC_SSA_SHL && d->rhs < 0 && d->imm > 0 && d->imm < 64 && d->lhs >= 0 &&
                   d->lhs < f->value_count) {
            /* of a variable (defined more than once) only right before the shr: nothing redefines it between */
            e[dst].shl_src = d->lhs;
            e[dst].shl_k = (int)d->imm;
            e[dst].shl_at = (long)i;
            e[dst].shl_src_var = defs[d->lhs] != 1;
        } else if (d->op == CC_SSA_SHR && d->rhs < 0 && d->lhs >= 0 && d->lhs < f->value_count &&
                   e[d->lhs].shl_src >= 0 && e[d->lhs].shl_k == (int)d->imm &&
                   8 * cc_ssa_value_size(f, d->lhs) > (uint32_t)d->imm) {
            e[dst].width = (int)(8 * cc_ssa_value_size(f, d->lhs)) - (int)d->imm;
            e[dst].sign = !d->is_unsigned;
        } else if (d->op == CC_SSA_CMP) {
            /* 0 or 1 */
            e[dst].width = 1;
            e[dst].sign = 0;
        } else if (d->op == CC_SSA_MOV && d->lhs >= 0 && d->lhs < f->value_count && e[d->lhs].width > 0) {
            /* a copy is extended as what it copies */
            e[dst].width = e[d->lhs].width;
            e[dst].sign = e[d->lhs].sign;
        } else if ((d->op == CC_SSA_CALL || d->op == CC_SSA_CALLI) && (d->imm == 1 || d->imm == 2) &&
                   cc_options_current()->target == CC_TARGET_I386) {
            /* a char or short result, which the i386 caller extends from %al or %ax as it is signed */
            e[dst].width = (int)(8 * d->imm);
            e[dst].sign = !d->is_unsigned;
        } else if (d->op == CC_SSA_AND && d->rhs >= 0 && d->rhs < f->value_count && e[d->rhs].mask_k > 0) {
            e[dst].width = e[d->rhs].mask_k;
            e[dst].sign = 0;
        } else if (d->op == CC_SSA_AND && d->lhs >= 0 && d->lhs < f->value_count && e[d->lhs].mask_k > 0) {
            e[dst].width = e[d->lhs].mask_k;
            e[dst].sign = 0;
        } else if (d->op == CC_SSA_CONST) {
            /* a mask of the low k bits, 2^k - 1 */
            unsigned long long m = (unsigned long long)d->imm & 0xffffffffULL;
            int k = 0;

            while (k < 32 && ((m >> k) & 1u) != 0) {
                k++;
            }
            e[dst].mask_k = k > 0 && k < 32 && (m >> k) == 0 ? k : 0;
        }
    }
    /*
     * a variable (defined more than once) each of whose definitions moves in a value extended alike, or is a
     * small non-negative constant, is extended so too: to the widest of them; until nothing changes
     */
    {
        int *mw = (int *)malloc((size_t)f->value_count * sizeof(int));   /* widest moved in */
        int *ms = (int *)malloc((size_t)f->value_count * sizeof(int));   /* their sign, -1 none yet */
        int *mc = (int *)malloc((size_t)f->value_count * sizeof(int));   /* bits of the widest constant */
        unsigned char *ok = (unsigned char *)malloc((size_t)f->value_count);
        int again = 1;
        int round = 0;

        while (mw != NULL && ms != NULL && mc != NULL && ok != NULL && again && round++ < 4) {
            again = 0;
            for (v = 0; v < f->value_count; ++v) {
                mw[v] = 0;
                ms[v] = -1;
                mc[v] = 0;
                ok[v] = defs[v] > 1 && cc_ssa_value_class(f, v) == CC_VAL_I64 && e[v].width == 0;
            }
            for (i = 0; i < f->instr_count; ++i) {
                const cc_ssa_instr_t *d = &f->instrs[i];
                int t = d->dst;

                if (t < 0 || t >= f->value_count || !ok[t]) {
                    continue;
                }
                if (d->op == CC_SSA_MOV && d->lhs >= 0 && d->lhs < f->value_count && e[d->lhs].width > 0 &&
                    (ms[t] < 0 || ms[t] == e[d->lhs].sign)) {
                    ms[t] = e[d->lhs].sign;
                    mw[t] = e[d->lhs].width > mw[t] ? e[d->lhs].width : mw[t];
                } else if (d->op == CC_SSA_CONST && d->imm >= 0 && d->imm < 0x8000) {
                    int k = 1;

                    while (k < 16 && (d->imm >> k) != 0) {
                        k++;
                    }
                    mc[t] = k > mc[t] ? k : mc[t];
                } else {
                    ok[t] = 0;
                }
            }
            for (v = 0; v < f->value_count; ++v) {
                /* a non-negative constant sign-extended from w bits needs w - 1 for its value */
                int need = ms[v] == 1 ? mc[v] + 1 : mc[v];
                int width = mw[v] > need ? mw[v] : need;

                if (ok[v] && width > 0 && (mw[v] > 0 || mc[v] > 0)) {
                    e[v].width = width;
                    e[v].sign = ms[v] < 0 ? 0 : ms[v];
                    again = 1;
                }
            }
        }
        free(mw);
        free(ms);
        free(mc);
        free(ok);
    }
    return e;
}

/**
 * The value `shr (shl x, k), k` (@p in) is when x is already extended so from as few bits or fewer: x; else
 * -1.  cc extends a narrow integer at each conversion, and a char converted twice is extended twice.
 */
static int redundant_ext(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, const ext_info_t *e) {
    int u;
    int x;
    int w;

    if (e != NULL && in->op == CC_SSA_AND && in->lhs >= 0 && in->lhs < f->value_count && in->rhs >= 0 &&
        in->rhs < f->value_count) {
        /* x & (2^k - 1) of an x already zero-extended from k bits or fewer, a word of 32 bits: x */
        int k = e[in->rhs].mask_k > 0 ? e[in->rhs].mask_k : e[in->lhs].mask_k;

        x = e[in->rhs].mask_k > 0 ? in->lhs : in->rhs;
        if (k > 0 && e[x].width > 0 && !e[x].sign && e[x].width <= k && cc_ssa_value_size(f, x) == 4 &&
            cc_ssa_value_size(f, in->dst) == 4) {
            return x;
        }
        return -1;
    }
    if (e == NULL || in->op != CC_SSA_SHR || in->rhs >= 0 || in->lhs < 0 || in->lhs >= f->value_count) {
        return -1;
    }
    u = in->lhs;
    x = e[u].shl_src;
    /* words of 32 bits: a narrow load on x86-64 extends to 32, not 64 */
    if (x < 0 || e[u].shl_k != (int)in->imm || cc_ssa_value_size(f, u) != 4) {
        return -1;
    }
    if (e[u].shl_src_var && e[u].shl_at != (long)(in - f->instrs) - 1) {
        /* a variable may have changed between the shl and here */
        return -1;
    }
    w = (int)(8 * cc_ssa_value_size(f, u)) - (int)in->imm;
    /*
     * x's register holds it extended (its width): the same bits the shifts would leave.  Of another size the
     * result is a move too on i386, where a narrow value's register holds the whole extended word.
     */
    if (e[x].width <= 0 || e[x].width > w || e[x].sign != !in->is_unsigned ||
        (cc_ssa_value_size(f, x) != cc_ssa_value_size(f, in->dst) &&
         !(cc_options_current()->target == CC_TARGET_I386 && cc_ssa_value_size(f, in->dst) == 4))) {
        return -1;
    }
    return x;
}

/** Whether @p in defines value @p v: its result, or an asm's output. */
static int instr_defines(const cc_ssa_instr_t *in, int v) {
    size_t k;

    if (in->dst == v) {
        return 1;
    }
    for (k = 0; in->op == CC_SSA_ASM && k < in->asm_out_count; ++k) {
        int o = in->asm_out_values[k];

        if (CC_SSA_ASM_MEM_INDIRECT_P(o)) {
            o = CC_SSA_ASM_MEM_INDIRECT_DECODE(o);
        }
        if (o == v) {
            return 1;
        }
    }
    return 0;
}

/**
 * The instruction of @p f's block before @p at (exclusive) that last defines @p v, scanning back to the
 * block's label; -1 if none there.
 */
static long last_def_in_block(const cc_ssa_function_t *f, size_t at, int v) {
    size_t k = at;

    while (k > 0) {
        const cc_ssa_instr_t *in = &f->instrs[--k];

        if (instr_defines(in, v)) {
            return (long)k;
        }
        if (in->op == CC_SSA_LABEL || in->op == CC_SSA_BR || in->op == CC_SSA_BR_COND || in->op == CC_SSA_RET ||
            in->op == CC_SSA_TRAP) {
            return -1;
        }
    }
    return -1;
}

static void mark_uses(const cc_ssa_instr_t *in, int *uses);

/** The most instructions a block that only tests a variable has before its branch (block_test_of()). */
#define TEST_BLOCK_MAX 6

/**
 * Whether the block at label index @p li of @p f only tests a variable defined before it: constants, moves
 * and compares of it against 0 (`!= 0`, `== 0`), each read only there (@p uses), then a branch on the result.
 * The variable goes to *@p v, the branch to *@p test, and *@p neg is 1 when a `== 0` turns its sense.
 */
static int block_test_of(const cc_ssa_function_t *f, long li, const const_state_t *g, const int *uses, int *v,
                         const cc_ssa_instr_t **test, int *neg) {
    int inside[TEST_BLOCK_MAX];
    int n = 0;
    long k;
    long b;
    int t;
    int j;

    for (k = li + 1; (size_t)k < f->instr_count && f->instrs[k].op != CC_SSA_BR_COND; ++k) {
        const cc_ssa_instr_t *in = &f->instrs[k];

        if (n == TEST_BLOCK_MAX || in->dst < 0 || in->dst >= f->value_count ||
            (in->op != CC_SSA_CONST && in->op != CC_SSA_MOV && in->op != CC_SSA_CMP)) {
            return 0;
        }
        inside[n++] = in->dst;
    }
    if ((size_t)k >= f->instr_count) {
        return 0;
    }
    b = k;
    /* what the block defines is read in it only: a jump past it leaves nothing undefined */
    for (j = 0; j < n; ++j) {
        int in_block = 0;
        long q;

        for (q = li + 1; q <= b; ++q) {
            const cc_ssa_instr_t *in = &f->instrs[q];

            in_block += (in->lhs == inside[j]) + (in->rhs == inside[j]);
        }
        if (uses[inside[j]] != in_block) {
            return 0;
        }
    }
    /* back from the branch's operand through the block's moves and compares with 0 */
    t = f->instrs[b].lhs;
    *neg = 0;
    for (k = b - 1; k > li; --k) {
        const cc_ssa_instr_t *in = &f->instrs[k];

        if (in->dst != t) {
            continue;
        }
        if (in->op == CC_SSA_MOV) {
            t = in->lhs;
        } else if (in->op == CC_SSA_CMP && (in->cmp_kind == CC_CMP_NE || in->cmp_kind == CC_CMP_EQ) &&
                   in->rhs >= 0 && in->rhs < f->value_count && g[in->rhs].known &&
                   g[in->rhs].type == CC_VAL_I64 && g[in->rhs].i == 0) {
            *neg ^= in->cmp_kind == CC_CMP_EQ;
            t = in->lhs;
        } else {
            return 0;
        }
    }
    for (j = 0; j < n; ++j) {
        if (inside[j] == t) {
            return 0;
        }
    }
    if (t < 0 || t >= f->value_count || cc_ssa_value_class(f, t) != CC_VAL_I64) {
        return 0;
    }
    *v = t;
    *test = &f->instrs[b];
    return 1;
}

/**
 * Jump threading.  A block that only tests a variable (block_test_of()) is where cc's && and || meet: each way
 * in sets the variable, to 0 or 1 or to a compare.  A way in that sets it to a constant just before goes
 * where the test sends that constant; an unconditional jump in after `v = s` branches on s itself.  The test's
 * label may be left with no way in (unreachable_function()) and the moves with no reader (dce_function()).
 *
 * @return 1 if anything changed, 0 if not, -1 when out of memory
 */
static int thread_jumps(cc_ssa_function_t *f) {
    long *at;
    const_state_t *g;
    int *uses;
    size_t i;
    int changed = 0;

    if (f->label_count <= 0 || f->value_count <= 0) {
        return 0;
    }
    at = (long *)malloc((size_t)f->label_count * sizeof(*at));
    uses = (int *)calloc((size_t)f->value_count, sizeof(*uses));
    g = single_def_consts(f);
    if (at == NULL || g == NULL || uses == NULL) {
        free(at);
        free(uses);
        free(g);
        return -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        mark_uses(&f->instrs[i], uses);
    }
    for (i = 0; i < (size_t)f->label_count; ++i) {
        at[i] = -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        if (f->instrs[i].op == CC_SSA_LABEL && f->instrs[i].label >= 0 && f->instrs[i].label < f->label_count) {
            at[f->instrs[i].label] = (long)i;
        }
    }
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];
        int edge;

        if (in->op != CC_SSA_BR && in->op != CC_SSA_BR_COND) {
            continue;
        }
        for (edge = 0; edge < (in->op == CC_SSA_BR ? 1 : 2); ++edge) {
            int *target = in->op == CC_SSA_BR ? &in->label : (edge == 0 ? &in->true_label : &in->false_label);
            const cc_ssa_instr_t *test;
            const cc_ssa_instr_t *d;
            long li;
            long di;
            int v;
            int neg;
            long long c;
            int known = 0;

            if (*target >= 0 && *target < f->label_count && (li = at[*target]) >= 0 &&
                (size_t)li + 1 < f->instr_count && f->instrs[li + 1].op == CC_SSA_BR &&
                f->instrs[li + 1].label != *target && (size_t)li + 1 != i) {
                /* a block that only jumps on: go there */
                *target = f->instrs[li + 1].label;
                changed = 1;
                continue;
            }
            if (*target < 0 || *target >= f->label_count || (li = at[*target]) < 0 ||
                (size_t)li == i || !block_test_of(f, li, g, uses, &v, &test, &neg) ||
                (di = last_def_in_block(f, i, v)) < 0) {
                continue;
            }
            d = &f->instrs[di];
            if (d->op == CC_SSA_CONST && d->dst == v) {
                c = d->imm;
                known = 1;
            } else if (d->op == CC_SSA_MOV && d->dst == v && d->lhs >= 0 && d->lhs < f->value_count &&
                       g[d->lhs].known && g[d->lhs].type == CC_VAL_I64) {
                c = g[d->lhs].i;
                known = 1;
            }
            if (known) {
                /* v is known on this way in: it goes where the test sends it */
                *target = (c != 0) != neg ? test->true_label : test->false_label;
                changed = 1;
            } else if (in->op == CC_SSA_BR && d->op == CC_SSA_MOV && d->dst == v && d->lhs >= 0 &&
                       d->lhs < f->value_count && d->lhs != v && last_def_in_block(f, i, d->lhs) < di &&
                       cc_ssa_value_class(f, d->lhs) == CC_VAL_I64 &&
                       cc_ssa_value_size(f, d->lhs) == cc_ssa_value_size(f, v)) {
                /* v = s, and s unchanged since: the test is of s, made here */
                in->op = CC_SSA_BR_COND;
                in->lhs = d->lhs;
                in->true_label = neg ? test->false_label : test->true_label;
                in->false_label = neg ? test->true_label : test->false_label;
                in->label = -1;
                changed = 1;
                break;
            }
        }
    }
    free(at);
    free(uses);
    free(g);
    return changed;
}

static int fold_function(cc_ssa_function_t *f, int fold_branches) {
    const_state_t *st;
    const_state_t *g;
    join_state_t join;
    unsigned char *single_def = NULL;
    unsigned char *is_bool = NULL; /* per value: only ever 0 or 1 (fold_branches only) */
    int *pair_half = NULL;         /* per value: the words of its one PAIR, two each, or -1 (fold_branches only) */
    ext_info_t *ext = NULL;        /* per value: what extends it (fold_branches only) */
    int live = 1; /* the code being walked can run, as far as the joins tell (join_enter()) */
    size_t i;
    int x;
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
    /* the optimizer's folding joins variables at labels; the -O0 one, before legalization, does not */
    memset(&join, 0, sizeof(join));
    if (fold_branches && join_init(&join, f) != 0) {
        free(st);
        free(g);
        return -1;
    }
    if (fold_branches) {
        /* the values defined once, whose known value a MOV can make a CONST */
        int *defs = (int *)calloc((size_t)f->value_count, sizeof(*defs));

        single_def = (unsigned char *)calloc((size_t)f->value_count, 1);
        if (defs == NULL || single_def == NULL) {
            free(defs);
            free(single_def);
            join_free(&join);
            free(st);
            free(g);
            return -1;
        }
        for (i = 0; i < f->instr_count; ++i) {
            const cc_ssa_instr_t *d = &f->instrs[i];
            size_t k;

            if (d->dst >= 0 && d->dst < f->value_count) {
                defs[d->dst]++;
            }
            for (k = 0; d->op == CC_SSA_ASM && k < d->asm_out_count; ++k) {
                int ov = d->asm_out_values[k];

                if (CC_SSA_ASM_MEM_INDIRECT_P(ov)) {
                    ov = CC_SSA_ASM_MEM_INDIRECT_DECODE(ov);
                }
                if (ov >= 0 && ov < f->value_count) {
                    defs[ov] += 2;
                }
            }
        }
        for (i = 0; i < (size_t)f->value_count; ++i) {
            single_def[i] = defs[i] == 1;
        }
        /* the values only ever 0 or 1: each definition a compare or the constant 0 or 1 */
        is_bool = (unsigned char *)malloc((size_t)f->value_count);
        if (is_bool == NULL) {
            free(defs);
            free(single_def);
            join_free(&join);
            free(st);
            free(g);
            return -1;
        }
        for (i = 0; i < (size_t)f->value_count; ++i) {
            is_bool[i] = defs[i] > 0;
        }
        /* per value made once by a PAIR of words each made once: its halves, else -1 */
        pair_half = (int *)malloc((size_t)f->value_count * 2 * sizeof(*pair_half));
        if (pair_half == NULL) {
            free(defs);
            free(single_def);
            free(is_bool);
            join_free(&join);
            free(st);
            free(g);
            return -1;
        }
        for (i = 0; i < (size_t)f->value_count * 2; ++i) {
            pair_half[i] = -1;
        }
        for (i = 0; i < f->instr_count; ++i) {
            const cc_ssa_instr_t *d = &f->instrs[i];

            if (d->op == CC_SSA_PAIR && d->dst >= 0 && d->dst < f->value_count && defs[d->dst] == 1 &&
                d->lhs >= 0 && d->lhs < f->value_count && defs[d->lhs] == 1 && d->rhs >= 0 &&
                d->rhs < f->value_count && defs[d->rhs] == 1) {
                pair_half[2 * d->dst] = d->lhs;
                pair_half[2 * d->dst + 1] = d->rhs;
            }
        }
        ext = ext_info_build(f, defs);
        if (ext == NULL) {
            free(defs);
            free(single_def);
            free(is_bool);
            free(pair_half);
            join_free(&join);
            free(st);
            free(g);
            return -1;
        }
        for (i = 0; i < f->instr_count; ++i) {
            const cc_ssa_instr_t *d = &f->instrs[i];

            if (d->op == CC_SSA_ASM) {
                size_t k;

                for (k = 0; k < d->asm_out_count; ++k) {
                    int ov = d->asm_out_values[k];

                    if (CC_SSA_ASM_MEM_INDIRECT_P(ov)) {
                        ov = CC_SSA_ASM_MEM_INDIRECT_DECODE(ov);
                    }
                    if (ov >= 0 && ov < f->value_count) {
                        is_bool[ov] = 0;
                    }
                }
            }
            if (d->dst >= 0 && d->dst < f->value_count && d->op != CC_SSA_CMP && d->op != CC_SSA_MOV &&
                !(d->op == CC_SSA_CONST && (d->imm == 0 || d->imm == 1) &&
                  cc_ssa_value_class(f, d->dst) == CC_VAL_I64)) {
                is_bool[d->dst] = 0;
            }
        }
        /* a move makes a truth value of a truth value only: && and || set their result so */
        {
            int again = 1;

            while (again) {
                again = 0;
                for (i = 0; i < f->instr_count; ++i) {
                    const cc_ssa_instr_t *d = &f->instrs[i];

                    if (d->op == CC_SSA_MOV && d->dst >= 0 && d->dst < f->value_count && is_bool[d->dst] &&
                        (d->lhs < 0 || d->lhs >= f->value_count || !is_bool[d->lhs])) {
                        is_bool[d->dst] = 0;
                        again = 1;
                    }
                }
            }
        }
        free(defs);
    }

    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];
        int dst = in->dst;
        cc_value_type_t vt = CC_VAL_I64;

        if (in->op == CC_SSA_LABEL) {
            /* falling into it from the instruction before is an edge too */
            if (i > 0 && !cc_ssa_opcode_is_terminator(f->instrs[i - 1].op)) {
                join_edge(&join, in->label, f->label_count, st, live);
            }
        }

        if (dst >= 0 && (size_t)dst < (size_t)f->value_count) {
            vt = cc_ssa_value_class(f, dst);
        }
        if (!fold_branches && dst >= 0 && vt == CC_VAL_F64) {
            /*
             * Before legalization (cc_fold_constant_values()) only integers
             * are folded: a floating value computed on the host would take
             * its precision -- x87's excess precision on a 32-bit host --
             * and -O0 output would depend on where cc was built.
             */
            st[dst].known = 0;
            continue;
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
                /*
                 * a value defined only here holds what it is given here wherever it is used: the
                 * constant a join gave its operand (join_state_t) lasts past the next label as a CONST
                 */
                if (fold_branches && single_def != NULL && single_def[dst] && st[dst].type == CC_VAL_I64 &&
                    vt == CC_VAL_I64) {
                    in->op = CC_SSA_CONST;
                    in->imm = st[dst].i;
                    in->lhs = -1;
                    in->rhs = -1;
                    changed = 1;
                }
            } else if (dst >= 0 && pair_half != NULL && in->lhs >= 0 && in->lhs < f->value_count &&
                       pair_half[2 * in->lhs] >= 0 && cc_ssa_value_size(f, dst) <= 4 &&
                       cc_ssa_value_size(f, in->lhs) == 8) {
                /* a word taken of a pair is its low word: the high one's arithmetic may go */
                in->lhs = pair_half[2 * in->lhs];
                st[dst].known = 0;
                changed = 1;
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
        case CC_SSA_UMULW:
            if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_LO32:
        case CC_SSA_HI32:
            if (dst >= 0 && in->lhs >= 0 && in->lhs < f->value_count && st[in->lhs].known &&
                st[in->lhs].type == CC_VAL_I64) {
                /* a half of a known 64-bit value */
                unsigned long long u = (unsigned long long)st[in->lhs].i;
                long long half = (long long)(in->op == CC_SSA_LO32 ? (u & 0xffffffffULL) : (u >> 32));

                in->op = CC_SSA_CONST;
                in->lhs = -1;
                in->rhs = -1;
                in->imm = half;
                st[dst].known = 1;
                st[dst].type = CC_VAL_I64;
                st[dst].i = half;
                changed = 1;
            } else if (dst >= 0 && pair_half != NULL && in->lhs >= 0 && in->lhs < f->value_count &&
                       pair_half[2 * in->lhs] >= 0) {
                /* a half of a pair is the word it was made of */
                in->lhs = pair_half[2 * in->lhs + (in->op == CC_SSA_HI32 ? 1 : 0)];
                in->op = CC_SSA_MOV;
                in->rhs = -1;
                st[dst].known = 0;
                changed = 1;
            } else if (dst >= 0) {
                st[dst].known = 0;
            }
            break;

        case CC_SSA_PAIR:
            if (dst >= 0 && in->lhs >= 0 && in->rhs >= 0 && st[in->lhs].known && st[in->rhs].known &&
                st[in->lhs].type == CC_VAL_I64 && st[in->rhs].type == CC_VAL_I64) {
                /* two known words are a known 64-bit value */
                long long v = (long long)(((unsigned long long)st[in->rhs].i << 32) |
                                          ((unsigned long long)st[in->lhs].i & 0xffffffffULL));

                in->op = CC_SSA_CONST;
                in->lhs = -1;
                in->rhs = -1;
                in->imm = v;
                st[dst].known = 1;
                st[dst].type = CC_VAL_I64;
                st[dst].i = v;
                changed = 1;
            } else if (dst >= 0) {
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
            } else if (dst >= 0 && vt == CC_VAL_I64 && (in->op == CC_SSA_AND || in->op == CC_SSA_MUL) &&
                       in->rhs >= 0 &&
                       ((in->lhs >= 0 && st[in->lhs].known && st[in->lhs].type == CC_VAL_I64 && st[in->lhs].i == 0) ||
                        (st[in->rhs].known && st[in->rhs].type == CC_VAL_I64 && st[in->rhs].i == 0))) {
                /*
                 * x & 0 and x * 0 are 0 whatever x is: Linux's IS_DAX(inode),
                 * `inode->i_flags & S_DAX`, with S_DAX 0 when DAX is not built
                 */
                in->op = CC_SSA_CONST;
                in->lhs = -1;
                in->rhs = -1;
                in->imm = 0;
                st[dst].known = 1;
                st[dst].type = CC_VAL_I64;
                st[dst].i = 0;
                changed = 1;
            } else if (dst >= 0 && vt == CC_VAL_I64 && (x = redundant_ext(f, in, ext)) >= 0) {
                /* an extension of what is already extended so */
                in->op = CC_SSA_MOV;
                in->lhs = x;
                in->rhs = -1;
                in->imm = 0;
                st[dst].known = 0;
                changed = 1;
            } else if (dst >= 0 && vt == CC_VAL_I64 && (x = identity_operand(f, in, st, is_bool)) >= 0) {
                /* x & ~0, x | 0, x + 0, x * 1 and the like are x: cc masks each unsigned int so */
                in->op = CC_SSA_MOV;
                in->lhs = x;
                in->rhs = -1;
                in->imm = 0;
                st[dst].known = 0;
                changed = 1;
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
            } else if (dst >= 0 && (x = identity_operand(f, in, st, is_bool)) >= 0) {
                /* b != 0 of a truth value b is b */
                in->op = CC_SSA_MOV;
                in->lhs = x;
                in->rhs = -1;
                in->imm = 0;
                st[dst].known = 0;
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
             * control-flow joins -- but for what every edge into it agrees
             * on (join_state_t).
             */
            memcpy(st, g, (size_t)f->value_count * sizeof(*st));
            live = join_enter(&join, in->label, f->label_count, st);
            break;

        case CC_SSA_BR_COND:
            /* a branch on a known condition goes one way: unreachable_function() removes the other */
            if (fold_branches && in->lhs >= 0 && in->lhs < f->value_count && st[in->lhs].known &&
                st[in->lhs].type == CC_VAL_I64) {
                in->label = st[in->lhs].i != 0 ? in->true_label : in->false_label;
                in->op = CC_SSA_BR;
                in->lhs = -1;
                changed = 1;
                join_edge(&join, in->label, f->label_count, st, live);
            } else {
                join_edge(&join, in->true_label, f->label_count, st, live);
                join_edge(&join, in->false_label, f->label_count, st, live);
            }
            live = 0;
            break;

        case CC_SSA_BR:
            join_edge(&join, in->label, f->label_count, st, live);
            live = 0;
            break;

        case CC_SSA_TRAP:
        case CC_SSA_RET:
            live = 0;
            break;
        }
    }

    join_free(&join);
    free(single_def);
    free(is_bool);
    free(pair_half);
    free(ext);
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

/** Whether @p sym (a symbol, perhaps with an offset) is @p name. */
static int sym_is(const char *sym, const char *name) {
    size_t n;

    if (sym == NULL || name == NULL) {
        return 0;
    }
    n = sym_name_len(sym);
    return strlen(name) == n && strncmp(name, sym, n) == 0;
}

/** Whether global @p g's initializer names @p name. */
static int global_names(const cc_ssa_global_t *g, const char *name) {
    size_t k;

    if (sym_is(g->init_sym, name) || sym_is(g->attr_alias, name)) {
        return 1;
    }
    for (k = 0; k < g->init_item_count; ++k) {
        if (sym_is(g->init_items[k].init_sym, name)) {
            return 1;
        }
    }
    return 0;
}

/**
 * Drop the block-scope statics (and their guards) of the functions dropped: those cc made, named
 * __cc_static_*, that no function left names or keeps (a used one, cc_ssa_function_t.keep_syms) and
 * no global kept names.  A static inline in a header that
 * nothing calls left its tables behind, and a table of pointers in .data has relocations: the vDSO,
 * which must have none, got of_overlay_action_name()'s.
 *
 * @return 0, or -1 when out of memory
 */
static int remove_orphan_static_locals(cc_ssa_module_t *m) {
    static const char prefix[] = "__cc_static_";
    unsigned char *keep;
    size_t i;
    size_t w = 0;
    int grew = 1;

    if (m->global_count == 0) {
        return 0;
    }
    keep = (unsigned char *)calloc(m->global_count, 1);
    if (keep == NULL) {
        return -1;
    }
    for (i = 0; i < m->global_count; ++i) {
        const char *name = m->globals[i].name;
        size_t fi;

        if (name == NULL || strncmp(name, prefix, sizeof(prefix) - 1) != 0) {
            keep[i] = 1;
            continue;
        }
        for (fi = 0; fi < m->func_count && !keep[i]; ++fi) {
            const cc_ssa_function_t *f = &m->funcs[fi];
            size_t k;

            for (k = 0; k < f->instr_count && !keep[i]; ++k) {
                keep[i] = sym_is(f->instrs[k].sym, name);
            }
            /* a used one its function, or one inlined into it, keeps */
            for (k = 0; k < f->keep_sym_count && !keep[i]; ++k) {
                keep[i] = strcmp(f->keep_syms[k], name) == 0;
            }
        }
    }
    /* and what a kept one's initializer names, until no more is */
    while (grew) {
        grew = 0;
        for (i = 0; i < m->global_count; ++i) {
            size_t j;

            for (j = 0; !keep[i] && j < m->global_count; ++j) {
                if (keep[j] && j != i && global_names(&m->globals[j], m->globals[i].name)) {
                    keep[i] = 1;
                    grew = 1;
                }
            }
        }
    }
    for (i = 0; i < m->global_count; ++i) {
        if (!keep[i]) {
            cc_ssa_global_free(&m->globals[i]);
            continue;
        }
        if (w != i) {
            m->globals[w] = m->globals[i];
        }
        w++;
    }
    m->global_count = w;
    free(keep);
    return 0;
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
    return remove_orphan_static_locals(m);
}

/**
 * Forward the constant a local slot holds into its loads: a STACKALLOC whose address is used only
 * as the address of loads and stores of one size, all integer, stored once with a known constant,
 * is read as that constant.  `bool set_uncached = IS_ENABLED(...) && ...;` in Linux's
 * dma_direct_alloc() is such a local, and the inlined parameters of a call with constant
 * arguments are; folding then decides the branches on them, as gcc does once it has promoted
 * them to registers.  A load reached before the store reads an uninitialized local, so the
 * constant is as good an answer there; a loop changes nothing, the slot never holding another.
 *
 * @return 1 if a load was replaced, 0 if not, -1 when out of memory
 */
static int forward_const_slots(cc_ssa_function_t *f) {
    enum { SLOT_NONE = 0, SLOT_OK = 1, SLOT_BAD = 2 };
    unsigned char *state;
    int *stores;
    long *size;
    int *stored;
    const_state_t *g;
    size_t i;
    size_t a;
    int changed = 0;

    if (f->value_count <= 0) {
        return 0;
    }
    state = (unsigned char *)calloc((size_t)f->value_count, 1);
    stores = (int *)calloc((size_t)f->value_count, sizeof(*stores));
    size = (long *)calloc((size_t)f->value_count, sizeof(*size));
    stored = (int *)malloc((size_t)f->value_count * sizeof(*stored));
    g = state == NULL || stores == NULL || size == NULL || stored == NULL ? NULL : single_def_consts(f);
    if (g == NULL) {
        free(state);
        free(stores);
        free(size);
        free(stored);
        return -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        if (f->instrs[i].op == CC_SSA_STACKALLOC && f->instrs[i].dst >= 0) {
            state[f->instrs[i].dst] = SLOT_OK;
        }
    }
    /* a slot's address used any other way, or accesses of different sizes, and it is not one */
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if ((in->op == CC_SSA_LOAD || in->op == CC_SSA_STORE) && in->lhs >= 0 && state[in->lhs] == SLOT_OK) {
            long sz = in->imm > 0 ? in->imm : 0;

            if (sz != 1 && sz != 2 && sz != 4 && sz != 8) {
                state[in->lhs] = SLOT_BAD;
            } else if (size[in->lhs] != 0 && size[in->lhs] != sz) {
                state[in->lhs] = SLOT_BAD;
            } else {
                size[in->lhs] = sz;
            }
            if (in->op == CC_SSA_STORE) {
                if (in->rhs < 0 || cc_ssa_value_class(f, in->rhs) != CC_VAL_I64 || !g[in->rhs].known) {
                    state[in->lhs] = SLOT_BAD;
                } else if (stores[in->lhs]++ == 0) {
                    stored[in->lhs] = in->rhs;
                }
                if (in->rhs >= 0 && state[in->rhs] != SLOT_NONE) {
                    state[in->rhs] = SLOT_BAD; /* the address of another slot, stored */
                }
            } else if (in->dst >= 0 && cc_ssa_value_class(f, in->dst) != CC_VAL_I64) {
                state[in->lhs] = SLOT_BAD;
            }
            continue;
        }
        if (in->dst >= 0 && in->op != CC_SSA_STACKALLOC && state[in->dst] != SLOT_NONE) {
            state[in->dst] = SLOT_BAD; /* defined again */
        }
        if (in->lhs >= 0 && state[in->lhs] != SLOT_NONE) {
            state[in->lhs] = SLOT_BAD;
        }
        if (in->rhs >= 0 && state[in->rhs] != SLOT_NONE) {
            state[in->rhs] = SLOT_BAD;
        }
        if (in->call_chain > 0 && state[in->call_chain - 1] != SLOT_NONE) {
            state[in->call_chain - 1] = SLOT_BAD;
        }
        for (a = 0; a < in->arg_count; ++a) {
            if (in->args[a] >= 0 && state[in->args[a]] != SLOT_NONE) {
                state[in->args[a]] = SLOT_BAD;
            }
        }
        for (a = 0; a < in->asm_out_count + in->asm_in_count; ++a) {
            int v = a < in->asm_out_count ? in->asm_out_values[a] : in->asm_in_values[a - in->asm_out_count];

            if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
                v = CC_SSA_ASM_MEM_INDIRECT_DECODE(v);
            }
            if (v >= 0 && v < f->value_count && state[v] != SLOT_NONE) {
                state[v] = SLOT_BAD;
            }
        }
    }
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];
        unsigned long long v;
        long sz;

        if (in->op != CC_SSA_LOAD || in->lhs < 0 || state[in->lhs] != SLOT_OK || stores[in->lhs] != 1 ||
            in->dst < 0) {
            continue;
        }
        /* the constant as the slot holds it, then as the load reads it */
        sz = size[in->lhs];
        v = (unsigned long long)g[stored[in->lhs]].i;
        if (sz < 8) {
            unsigned long long mask = (1ULL << (sz * 8)) - 1;
            unsigned long long sign = 1ULL << (sz * 8 - 1);

            v &= mask;
            if (!in->is_unsigned && (v & sign) != 0) {
                v |= ~mask;
            }
        }
        in->op = CC_SSA_CONST;
        in->imm = (long long)v;
        in->lhs = -1;
        in->rhs = -1;
        changed = 1;
    }
    free(state);
    free(stores);
    free(size);
    free(stored);
    free(g);
    return changed;
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
        /* a slot's constant forwarded may fold a branch, and a branch folded may leave one store */
        do {
            int rcs = cc_optimize_slots(f);
            int rc0 = rcs < 0 ? -1 : forward_const_slots(f);

            if (rc0 == 0 && rcs > 0) {
                rc0 = 1;
            }
            rc1 = rc0 < 0 ? -1 : fold_function(f, 1);
            if (rc1 >= 0) {
                /* a copy read as its source, which may be known */
                int rcp = cc_propagate_copies(f);
                int rct = rcp < 0 ? -1 : thread_jumps(f);

                rc1 = rct < 0 ? -1 : (rc1 > 0 || rcp > 0 || rct > 0);
            }
            rc3 = rc1 < 0 ? 0 : unreachable_function(f);
            if (rc0 > 0 && rc1 == 0) {
                rc1 = 1;
            }
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

/** The index of the function of @p m that symbol @p sym names (an offset or @PLT after it ignored), or -1. */
static long function_named(const cc_ssa_module_t *m, const char *sym) {
    size_t n;
    size_t i;

    if (sym == NULL) {
        return -1;
    }
    n = sym_name_len(sym);
    for (i = 0; i < m->func_count; ++i) {
        const char *name = m->funcs[i].name;

        if (name != NULL && strlen(name) == n && strncmp(name, sym, n) == 0) {
            return (long)i;
        }
    }
    return -1;
}

/**
 * Mark in @p rec the functions of @p m that are on a cycle of calls: one
 * that calls itself, or one of a strongly connected group (Tarjan's
 * algorithm, without recursion).
 *
 * @param m    the module
 * @param rec  receives 1 or 0 per function of @p m
 * @return     0, or -1 when out of memory
 */
static int mark_recursive(const cc_ssa_module_t *m, unsigned char *rec) {
    size_t n = m->func_count;
    long *idx = (long *)malloc((n > 0 ? n : 1) * sizeof(*idx));
    long *low = (long *)malloc((n > 0 ? n : 1) * sizeof(*low));
    size_t *scc = (size_t *)malloc((n > 0 ? n : 1) * sizeof(*scc));
    size_t *walk = (size_t *)malloc((n > 0 ? n : 1) * sizeof(*walk));
    size_t *next = (size_t *)malloc((n > 0 ? n : 1) * sizeof(*next));
    unsigned char *on = (unsigned char *)calloc(n > 0 ? n : 1, 1);
    long counter = 0;
    size_t sp = 0;
    size_t root;

    if (idx == NULL || low == NULL || scc == NULL || walk == NULL || next == NULL || on == NULL) {
        free(idx);
        free(low);
        free(scc);
        free(walk);
        free(next);
        free(on);
        return -1;
    }
    for (root = 0; root < n; ++root) {
        idx[root] = -1;
        rec[root] = 0;
    }
    for (root = 0; root < n; ++root) {
        size_t wp = 0;

        if (idx[root] >= 0) {
            continue;
        }
        idx[root] = low[root] = counter++;
        scc[sp++] = root;
        on[root] = 1;
        next[root] = 0;
        walk[wp++] = root;
        while (wp > 0) {
            size_t v = walk[wp - 1];
            const cc_ssa_function_t *f = &m->funcs[v];
            int down = 0;

            while (next[v] < f->instr_count) {
                const cc_ssa_instr_t *in = &f->instrs[next[v]++];
                long w;

                if (in->op != CC_SSA_CALL || in->sym == NULL || (w = function_named(m, in->sym)) < 0) {
                    continue;
                }
                if ((size_t)w == v) {
                    rec[v] = 1;
                } else if (idx[w] < 0) {
                    idx[w] = low[w] = counter++;
                    scc[sp++] = (size_t)w;
                    on[w] = 1;
                    next[w] = 0;
                    walk[wp++] = (size_t)w;
                    down = 1;
                    break;
                } else if (on[w] && idx[w] < low[v]) {
                    low[v] = idx[w];
                }
            }
            if (down) {
                continue;
            }
            if (low[v] == idx[v]) {
                /* v roots a group: everything above it on the stack */
                size_t base = sp;
                int group;

                do {
                    base--;
                } while (scc[base] != v);
                group = sp - base > 1;
                for (; sp > base; --sp) {
                    on[scc[sp - 1]] = 0;
                    if (group) {
                        rec[scc[sp - 1]] = 1;
                    }
                }
            }
            wp--;
            if (wp > 0 && low[v] < low[walk[wp - 1]]) {
                low[walk[wp - 1]] = low[v];
            }
        }
    }
    free(idx);
    free(low);
    free(scc);
    free(walk);
    free(next);
    free(on);
    return 0;
}

/**
 * Mark in @p once the static functions of @p m that gcc's
 * -finline-functions-called-once inlines: called by one call, from another
 * function, and named nowhere else -- no address taken, no global's
 * initializer or alias naming it -- and not used, constructor, destructor,
 * weak or noinline, nor given a section its caller is not in; and not on a
 * cycle of calls (mark_recursive()), whose frames inlining would grow at
 * every level of the recursion -- cc's own lower_cast_expr() inlined into
 * lower_expr() ran a deeply nested expression out of stack.  Inlined, such
 * a function is called by nothing, and is dropped.
 *
 * @param m     the module
 * @param once  receives 1 or 0 per function of @p m
 * @return      0, or -1 when out of memory
 */
static int mark_called_once(const cc_ssa_module_t *m, unsigned char *once) {
    const cc_attr_flags_t barred =
        CC_ATTR_USED | CC_ATTR_CONSTRUCTOR | CC_ATTR_DESTRUCTOR | CC_ATTR_WEAK | CC_ATTR_NOINLINE;
    size_t n = m->func_count;
    size_t *calls = (size_t *)calloc(n > 0 ? n : 1, sizeof(*calls));
    long *caller = (long *)malloc((n > 0 ? n : 1) * sizeof(*caller));
    unsigned char *named = (unsigned char *)calloc(n > 0 ? n : 1, 1);
    unsigned char *rec = (unsigned char *)calloc(n > 0 ? n : 1, 1);
    size_t i;

    if (calls == NULL || caller == NULL || named == NULL || rec == NULL || mark_recursive(m, rec) != 0) {
        free(calls);
        free(caller);
        free(named);
        free(rec);
        return -1;
    }
    for (i = 0; i < m->global_count; ++i) {
        const cc_ssa_global_t *g = &m->globals[i];
        long gi;
        size_t k;

        if ((gi = function_named(m, g->init_sym)) >= 0) {
            named[gi] = 1;
        }
        if ((gi = function_named(m, g->attr_alias)) >= 0) {
            named[gi] = 1;
        }
        for (k = 0; k < g->init_item_count; ++k) {
            if ((gi = function_named(m, g->init_items[k].init_sym)) >= 0) {
                named[gi] = 1;
            }
        }
    }
    for (i = 0; i < n; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        long gi;
        size_t k;

        if ((gi = function_named(m, f->attr_alias)) >= 0) {
            named[gi] = 1;
        }
        for (k = 0; k < f->instr_count; ++k) {
            const cc_ssa_instr_t *in = &f->instrs[k];

            if (in->sym == NULL || (in->op != CC_SSA_CALL && in->op != CC_SSA_GADDR)) {
                continue;
            }
            gi = function_named(m, in->sym);
            if (gi < 0) {
                continue;
            }
            if (in->op == CC_SSA_GADDR || (size_t)gi == i) {
                named[gi] = 1;
            } else {
                calls[gi]++;
                caller[gi] = (long)i;
            }
        }
    }
    for (i = 0; i < n; ++i) {
        const cc_ssa_function_t *g = &m->funcs[i];
        const char *g_sec = g->attr_section;
        const char *c_sec = calls[i] == 1 ? m->funcs[caller[i]].attr_section : NULL;

        once[i] = (g->storage & CC_STORAGE_STATIC) != 0 && (g->attr_flags & barred) == 0 && g->attr_alias == NULL &&
                  calls[i] == 1 && !named[i] && !rec[i] &&
                  (g_sec == NULL || (c_sec != NULL && strcmp(g_sec, c_sec) == 0));
    }
    free(calls);
    free(caller);
    free(named);
    free(rec);
    return 0;
}

/**
 * Inline the module's small static inline functions (passes/inline.c) bottom-up: a function is
 * rewritten once every function it calls is, so what it copies has its own calls inlined and is
 * folded; then it is folded again, and a call guarded by what an inlined body made constant goes
 * with its dead arm.  A function on a cycle of calls is not inlined into the cycle.  So is a
 * static function called from one place only (mark_called_once()).
 *
 * @return 0, or -1 when out of memory
 */
static int inline_module(cc_ssa_module_t *m, int opt_level) {
    size_t n = m->func_count;
    unsigned char *open = (unsigned char *)calloc(n > 0 ? n : 1, 1);
    unsigned char *done = (unsigned char *)calloc(n > 0 ? n : 1, 1);
    unsigned char *once = (unsigned char *)calloc(n > 0 ? n : 1, 1);
    size_t *stack = (size_t *)malloc((n > 0 ? n : 1) * sizeof(*stack));
    size_t *next = (size_t *)malloc((n > 0 ? n : 1) * sizeof(*next));
    size_t root;
    int rc = 0;

    if (open == NULL || done == NULL || once == NULL || stack == NULL || next == NULL ||
        mark_called_once(m, once) != 0) {
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
            rc = cc_inline_calls(m, fi, done, once);
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
    free(once);
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
    /* last, once nothing reasons about the slots: a scalar local is a value, not memory */
    for (i = 0; i < m->func_count && !oom && opt_level > 0; ++i) {
        int rc = cc_promote_slots(&m->funcs[i]);

        if (rc > 0) {
            /* what the new values make known or plain copies */
            rc = fold_function(&m->funcs[i], 1);
            rc = rc < 0 ? -1 : cc_propagate_copies(&m->funcs[i]);
            rc = rc < 0 ? -1 : thread_jumps(&m->funcs[i]);
            rc = rc < 0 ? -1 : unreachable_function(&m->funcs[i]);
            rc = rc < 0 ? -1 : 1;
        }
        oom = rc < 0 || (rc > 0 && dce_function(&m->funcs[i]) < 0);
        /* definitions nothing reads, a variable's overwritten initializer among them; and what they read */
        if (!oom) {
            int round;

            for (round = 0; round < 2 && !oom; ++round) {
                int rd = cc_dead_defs(&m->funcs[i]);

                oom = rd < 0 || (rd > 0 && dce_function(&m->funcs[i]) < 0);
                if (rd == 0) {
                    break;
                }
            }
        }
        /*
         * what the target split before the constants were known: a 64-bit division by one, as a shift
         * (Linux's roundup() divides by a local), and the constant it leaves dead
         */
        if (!oom && cc_target_current()->legalize != NULL) {
            oom = cc_target_current()->legalize(&m->funcs[i], diag) != 0 || dce_function(&m->funcs[i]) < 0;
        }
        /* last of all: a variable's update made in place */
        oom = oom || cc_coalesce_moves(&m->funcs[i]) < 0;
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
