/*
 * legalize_i386.c: i386 legalization -- 64-bit integer arithmetic in
 * 32-bit words.
 *
 * i386 has no 64-bit integer registers.  A 64-bit value stays one value
 * (the emitter keeps it in an 8-byte frame slot, its low word possibly in
 * a register), but every operation on one is rewritten here into
 * operations on its two words:
 *
 *  - lo32 / hi32 take a word of a 64-bit value, and pair builds one from
 *    two words;
 *  - add and sub carry and borrow through an unsigned compare of the low
 *    words; and, or and xor work word by word;
 *  - mul is the three-multiply product, its low words' full 64-bit
 *    product from umulw (the target's mull);
 *  - a shift by a constant moves bits between the words directly, and a
 *    shift by a variable amount branches on whether it is 0, under 32 or
 *    32 and over;
 *  - a compare decides on the high words unless they are equal, and then
 *    on the low words as unsigned numbers;
 *  - div calls libgcc's __divdi3 or __udivdi3.
 *
 * After this, the emitter has no 64-bit arithmetic of its own: it moves
 * 64-bit values (load, store, move, constant, call and return) and
 * selects instructions for words.
 */

#include "cc_diag.h"
#include "cc_middle.h"
#include "cc_target.h"
#include "cc_util.h"

#include <stdlib.h>
#include <string.h>

/** The rewrite of one function: the new instruction list being built. */
typedef struct {
    cc_ssa_function_t *f; /**< the function, whose values grow */
    cc_ssa_instr_t *out;  /**< the instructions written so far */
    size_t count;         /**< how many */
    size_t cap;           /**< room for */
} lz_t;

/**
 * Append @p in to the new list, which takes ownership of what it points
 * at.
 *
 * @return 0, or -1 when out of memory
 */
static int lz_put(lz_t *z, const cc_ssa_instr_t *in) {
    if (z->count == z->cap) {
        size_t ncap = z->cap == 0 ? 64 : z->cap * 2;
        cc_ssa_instr_t *next = (cc_ssa_instr_t *)realloc(z->out, ncap * sizeof(*next));

        if (next == NULL) {
            return -1;
        }
        z->out = next;
        z->cap = ncap;
    }
    z->out[z->count++] = *in;
    return 0;
}

/**
 * A new integer value of @p size bytes.
 *
 * @return the value, or -1 when out of memory
 */
static int lz_value(lz_t *z, long size, int is_unsigned) {
    int v = cc_ssa_function_new_value(z->f, CC_VAL_I64);

    if (v >= 0) {
        cc_ssa_value_set_size(z->f, v, (uint32_t)size);
        cc_ssa_value_set_unsigned(z->f, v, is_unsigned);
    }
    return v;
}

/**
 * Append `dst = lhs op rhs` on words into a new word, or into @p dst when
 * it is not negative.
 *
 * @return the result, or -1 when out of memory
 */
static int lz_op(lz_t *z, cc_ssa_opcode_t op, int lhs, int rhs, long long imm, int is_unsigned, int dst) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.dst = dst >= 0 ? dst : lz_value(z, 4, is_unsigned);
    in.lhs = lhs;
    in.rhs = rhs;
    in.imm = imm;
    in.is_unsigned = is_unsigned;
    in.label = -1;
    in.true_label = -1;
    in.false_label = -1;
    in.param_index = -1;
    return in.dst < 0 || lz_put(z, &in) != 0 ? -1 : in.dst;
}

/** A word constant. */
static int lz_const(lz_t *z, long long c) {
    return lz_op(z, CC_SSA_CONST, -1, -1, c, 1, -1);
}

/** A compare of words, into a new word holding 0 or 1. */
static int lz_cmp(lz_t *z, cc_cmp_kind_t kind, int is_unsigned, int lhs, int rhs, int dst) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CMP;
    in.cmp_kind = kind;
    in.is_unsigned = is_unsigned;
    in.dst = dst >= 0 ? dst : lz_value(z, 4, 1);
    in.lhs = lhs;
    in.rhs = rhs;
    in.label = -1;
    in.true_label = -1;
    in.false_label = -1;
    in.param_index = -1;
    return in.dst < 0 || lz_put(z, &in) != 0 ? -1 : in.dst;
}

/** Place a label. */
static int lz_label(lz_t *z, int label) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_LABEL;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.label = label;
    in.true_label = -1;
    in.false_label = -1;
    in.param_index = -1;
    return lz_put(z, &in);
}

/** Jump to @p label, or, with @p cond not negative, to @p label or @p other. */
static int lz_br(lz_t *z, int cond, int label, int other) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = cond >= 0 ? CC_SSA_BR_COND : CC_SSA_BR;
    in.dst = -1;
    in.lhs = cond;
    in.rhs = -1;
    in.label = cond >= 0 ? -1 : label;
    in.true_label = cond >= 0 ? label : -1;
    in.false_label = cond >= 0 ? other : -1;
    in.param_index = -1;
    return lz_put(z, &in);
}

/** The words of a 64-bit operand. */
typedef struct {
    int lo; /**< bits 0-31 */
    int hi; /**< bits 32-63 */
} words_t;

static int is_wide(const cc_ssa_function_t *f, int v);

/**
 * Split value @p v into its words: a 64-bit one by taking them, a word
 * (an operand of a 64-bit operation an extension writes, such as the AND
 * of a zero extension) by extending it as its signedness says.
 *
 * @return 0, or -1 when out of memory
 */
static int lz_split(lz_t *z, int v, words_t *w) {
    if (!is_wide(z->f, v)) {
        w->lo = v;
        w->hi = cc_ssa_value_is_unsigned(z->f, v) ? lz_const(z, 0) : lz_op(z, CC_SSA_SHR, v, -1, 31, 0, -1);
        return w->hi < 0 ? -1 : 0;
    }
    w->lo = lz_op(z, CC_SSA_LO32, v, -1, 0, 1, -1);
    w->hi = w->lo < 0 ? -1 : lz_op(z, CC_SSA_HI32, v, -1, 0, 1, -1);
    return w->hi < 0 ? -1 : 0;
}

/** Build 64-bit @p dst from words @p lo and @p hi. */
static int lz_pair(lz_t *z, int dst, int lo, int hi) {
    return lz_op(z, CC_SSA_PAIR, lo, hi, 0, cc_ssa_value_is_unsigned(z->f, dst), dst) < 0 ? -1 : 0;
}

/**
 * Value @p v as a 64-bit value: itself, or a word extended into a pair.
 *
 * @return the value, or -1 when out of memory
 */
static int lz_widen(lz_t *z, int v) {
    words_t w;
    int p;

    if (is_wide(z->f, v)) {
        return v;
    }
    p = lz_value(z, 8, cc_ssa_value_is_unsigned(z->f, v));
    if (p < 0 || lz_split(z, v, &w) != 0 || lz_pair(z, p, w.lo, w.hi) != 0) {
        return -1;
    }
    return p;
}

/**
 * Whether value @p v is an integer the i386 emitter holds in a pair of
 * words.
 */
static int is_wide(const cc_ssa_function_t *f, int v) {
    return v >= 0 && v < f->value_count && cc_ssa_value_class(f, v) == CC_VAL_I64 && cc_ssa_value_size(f, v) >= 8;
}

/**
 * Add, subtract, and, or, xor on two 64-bit values.
 *
 * @return 0, or -1 when out of memory
 */
static int split_simple(lz_t *z, const cc_ssa_instr_t *in, const words_t *a, const words_t *b) {
    int lo;
    int hi;

    if (in->op == CC_SSA_ADD || in->op == CC_SSA_SUB) {
        /* the carry out of the low words, or the borrow from them */
        int c;

        lo = lz_op(z, in->op, a->lo, b->lo, 0, 1, -1);
        /* a sum below an addend carried; a minuend below the subtrahend borrows */
        c = lo < 0 ? -1
            : in->op == CC_SSA_ADD ? lz_cmp(z, CC_CMP_LT, 1, lo, a->lo, -1)
                                   : lz_cmp(z, CC_CMP_LT, 1, a->lo, b->lo, -1);
        hi = c < 0 ? -1 : lz_op(z, in->op, a->hi, b->hi, 0, 1, -1);
        hi = hi < 0 ? -1 : lz_op(z, in->op, hi, c, 0, 1, -1);
    } else {
        lo = lz_op(z, in->op, a->lo, b->lo, 0, 1, -1);
        hi = lo < 0 ? -1 : lz_op(z, in->op, a->hi, b->hi, 0, 1, -1);
    }
    return hi < 0 ? -1 : lz_pair(z, in->dst, lo, hi);
}

/**
 * The low 64 bits of a product: alo * blo in full, plus the cross
 * products into the high word.
 *
 * @return 0, or -1 when out of memory
 */
static int split_mul(lz_t *z, const cc_ssa_instr_t *in, const words_t *a, const words_t *b) {
    int p = lz_value(z, 8, 1);
    words_t pw;
    int t1;
    int t2;
    int hi;

    if (p < 0 || lz_op(z, CC_SSA_UMULW, a->lo, b->lo, 0, 1, p) < 0 || lz_split(z, p, &pw) != 0) {
        return -1;
    }
    t1 = lz_op(z, CC_SSA_MUL, a->hi, b->lo, 0, 1, -1);
    t2 = t1 < 0 ? -1 : lz_op(z, CC_SSA_MUL, a->lo, b->hi, 0, 1, -1);
    hi = t2 < 0 ? -1 : lz_op(z, CC_SSA_ADD, pw.hi, t1, 0, 1, -1);
    hi = hi < 0 ? -1 : lz_op(z, CC_SSA_ADD, hi, t2, 0, 1, -1);
    return hi < 0 ? -1 : lz_pair(z, in->dst, pw.lo, hi);
}

/**
 * A shift by the constant @p k, 1 to 63.
 *
 * @return 0, or -1 when out of memory
 */
static int split_shift_const(lz_t *z, const cc_ssa_instr_t *in, const words_t *a, int k) {
    int lo;
    int hi;

    if (in->op == CC_SSA_SHL) {
        if (k >= 32) {
            hi = k == 32 ? a->lo : lz_op(z, CC_SSA_SHL, a->lo, -1, k - 32, 1, -1);
            lo = hi < 0 ? -1 : lz_const(z, 0);
        } else {
            int spill = lz_op(z, CC_SSA_SHR, a->lo, -1, 32 - k, 1, -1);

            hi = spill < 0 ? -1 : lz_op(z, CC_SSA_SHL, a->hi, -1, k, 1, -1);
            hi = hi < 0 ? -1 : lz_op(z, CC_SSA_OR, hi, spill, 0, 1, -1);
            lo = hi < 0 ? -1 : lz_op(z, CC_SSA_SHL, a->lo, -1, k, 1, -1);
        }
    } else if (k >= 32) {
        lo = k == 32 ? a->hi : lz_op(z, CC_SSA_SHR, a->hi, -1, k - 32, in->is_unsigned, -1);
        hi = lo < 0 ? -1 : in->is_unsigned ? lz_const(z, 0) : lz_op(z, CC_SSA_SHR, a->hi, -1, 31, 0, -1);
    } else {
        int spill = lz_op(z, CC_SSA_SHL, a->hi, -1, 32 - k, 1, -1);

        lo = spill < 0 ? -1 : lz_op(z, CC_SSA_SHR, a->lo, -1, k, 1, -1);
        lo = lo < 0 ? -1 : lz_op(z, CC_SSA_OR, lo, spill, 0, 1, -1);
        hi = lo < 0 ? -1 : lz_op(z, CC_SSA_SHR, a->hi, -1, k, in->is_unsigned, -1);
    }
    return lo < 0 || hi < 0 ? -1 : lz_pair(z, in->dst, lo, hi);
}

/**
 * A shift by the variable amount in the low word of @p b, taken modulo
 * 64: a is unchanged for 0, bits cross between the words under 32, and
 * one word moves into the other from 32 up.
 *
 * @return 0, or -1 when out of memory
 */
static int split_shift_var(lz_t *z, const cc_ssa_instr_t *in, const words_t *a, const words_t *b) {
    int lo = lz_value(z, 4, 1);
    int hi = lz_value(z, 4, 1);
    int l_zero = z->f->label_count++;
    int l_small = z->f->label_count++;
    int l_check = z->f->label_count++;
    int l_big = z->f->label_count++;
    int l_done = z->f->label_count++;
    int n;
    int c32;
    int is_zero;
    int is_small;
    int t;
    int u;
    int m;

    if (lo < 0 || hi < 0) {
        return -1;
    }
    m = lz_const(z, 63);
    n = m < 0 ? -1 : lz_op(z, CC_SSA_AND, b->lo, m, 0, 1, -1);
    c32 = n < 0 ? -1 : lz_const(z, 32);
    t = c32 < 0 ? -1 : lz_const(z, 0);
    is_zero = t < 0 ? -1 : lz_cmp(z, CC_CMP_EQ, 1, n, t, -1);
    if (is_zero < 0 || lz_br(z, is_zero, l_zero, l_check) != 0) {
        return -1;
    }

    /* 0: unchanged */
    if (lz_label(z, l_zero) != 0 || lz_op(z, CC_SSA_MOV, a->lo, -1, 0, 1, lo) < 0 ||
        lz_op(z, CC_SSA_MOV, a->hi, -1, 0, 1, hi) < 0 || lz_br(z, -1, l_done, -1) != 0) {
        return -1;
    }

    if (lz_label(z, l_check) != 0) {
        return -1;
    }
    is_small = lz_cmp(z, CC_CMP_LT, 1, n, c32, -1);
    if (is_small < 0 || lz_br(z, is_small, l_small, l_big) != 0) {
        return -1;
    }

    /* 1 to 31: the bits shifted out of one word enter the other */
    if (lz_label(z, l_small) != 0) {
        return -1;
    }
    u = lz_op(z, CC_SSA_SUB, c32, n, 0, 1, -1);
    if (in->op == CC_SSA_SHL) {
        t = u < 0 ? -1 : lz_op(z, CC_SSA_SHR, a->lo, u, 0, 1, -1);
        u = t < 0 ? -1 : lz_op(z, CC_SSA_SHL, a->hi, n, 0, 1, -1);
        if (u < 0 || lz_op(z, CC_SSA_OR, u, t, 0, 1, hi) < 0 || lz_op(z, CC_SSA_SHL, a->lo, n, 0, 1, lo) < 0) {
            return -1;
        }
    } else {
        t = u < 0 ? -1 : lz_op(z, CC_SSA_SHL, a->hi, u, 0, 1, -1);
        u = t < 0 ? -1 : lz_op(z, CC_SSA_SHR, a->lo, n, 0, 1, -1);
        if (u < 0 || lz_op(z, CC_SSA_OR, u, t, 0, 1, lo) < 0 ||
            lz_op(z, CC_SSA_SHR, a->hi, n, 0, in->is_unsigned, hi) < 0) {
            return -1;
        }
    }
    if (lz_br(z, -1, l_done, -1) != 0) {
        return -1;
    }

    /* 32 to 63: one word moves into the other, shifted by the rest */
    if (lz_label(z, l_big) != 0) {
        return -1;
    }
    u = lz_op(z, CC_SSA_SUB, n, c32, 0, 1, -1);
    if (u < 0) {
        return -1;
    }
    if (in->op == CC_SSA_SHL) {
        t = lz_op(z, CC_SSA_SHL, a->lo, u, 0, 1, hi) < 0 ? -1 : lz_const(z, 0);
        if (t < 0 || lz_op(z, CC_SSA_MOV, t, -1, 0, 1, lo) < 0) {
            return -1;
        }
    } else {
        if (lz_op(z, CC_SSA_SHR, a->hi, u, 0, in->is_unsigned, lo) < 0) {
            return -1;
        }
        t = in->is_unsigned ? lz_const(z, 0) : lz_op(z, CC_SSA_SHR, a->hi, -1, 31, 0, -1);
        if (t < 0 || lz_op(z, CC_SSA_MOV, t, -1, 0, 1, hi) < 0) {
            return -1;
        }
    }
    if (lz_br(z, -1, l_done, -1) != 0 || lz_label(z, l_done) != 0) {
        return -1;
    }
    return lz_pair(z, in->dst, lo, hi);
}

/**
 * A compare of 64-bit values: equality on both words at once, an
 * ordering on the high words unless they are equal, else on the low
 * words, unsigned.
 *
 * @return 0, or -1 when out of memory
 */
static int split_cmp(lz_t *z, const cc_ssa_instr_t *in, const words_t *a, const words_t *b) {
    int l_hi;
    int l_lo;
    int l_done;
    int ne;

    if (in->cmp_kind == CC_CMP_EQ || in->cmp_kind == CC_CMP_NE) {
        int x = lz_op(z, CC_SSA_XOR, a->lo, b->lo, 0, 1, -1);
        int y = x < 0 ? -1 : lz_op(z, CC_SSA_XOR, a->hi, b->hi, 0, 1, -1);
        int o = y < 0 ? -1 : lz_op(z, CC_SSA_OR, x, y, 0, 1, -1);
        int c0 = o < 0 ? -1 : lz_const(z, 0);

        return c0 < 0 || lz_cmp(z, in->cmp_kind, 1, o, c0, in->dst) < 0 ? -1 : 0;
    }
    l_hi = z->f->label_count++;
    l_lo = z->f->label_count++;
    l_done = z->f->label_count++;
    ne = lz_cmp(z, CC_CMP_NE, 1, a->hi, b->hi, -1);
    if (ne < 0 || lz_br(z, ne, l_hi, l_lo) != 0) {
        return -1;
    }
    if (lz_label(z, l_hi) != 0 || lz_cmp(z, in->cmp_kind, in->is_unsigned, a->hi, b->hi, in->dst) < 0 ||
        lz_br(z, -1, l_done, -1) != 0) {
        return -1;
    }
    if (lz_label(z, l_lo) != 0 || lz_cmp(z, in->cmp_kind, 1, a->lo, b->lo, in->dst) < 0 ||
        lz_br(z, -1, l_done, -1) != 0) {
        return -1;
    }
    return lz_label(z, l_done);
}

/**
 * A 64-bit division: a call of libgcc's __divdi3 or __udivdi3, written in
 * place of the DIV so the quotient keeps its value.
 *
 * @return 0, or -1 when out of memory
 */
static int split_div(lz_t *z, const cc_ssa_instr_t *div) {
    cc_ssa_instr_t in;
    int dividend = lz_widen(z, div->lhs);
    int divisor = div->rhs >= 0 ? lz_widen(z, div->rhs) : -1;

    if (dividend < 0 || (div->rhs >= 0 && divisor < 0)) {
        return -1;
    }
    if (divisor < 0) {
        /* an immediate divisor: as a 64-bit constant argument */
        divisor = lz_value(z, 8, div->is_unsigned);
        if (divisor < 0 || lz_op(z, CC_SSA_CONST, -1, -1, div->imm, div->is_unsigned, divisor) < 0) {
            return -1;
        }
    }
    memset(&in, 0, sizeof(in));
    in.op = CC_SSA_CALL;
    in.dst = div->dst;
    in.lhs = -1;
    in.rhs = -1;
    in.imm = 8;
    in.is_unsigned = div->is_unsigned;
    in.label = -1;
    in.true_label = -1;
    in.false_label = -1;
    in.param_index = -1;
    in.sym = cc_strdup(div->is_unsigned ? "__udivdi3" : "__divdi3");
    in.arg_count = 2;
    in.args = (int *)calloc(2, sizeof(*in.args));
    in.call_arg_abi = (unsigned char *)calloc(2, sizeof(*in.call_arg_abi));
    in.call_arg_sizes = (uint32_t *)calloc(2, sizeof(*in.call_arg_sizes));
    if (in.sym == NULL || in.args == NULL || in.call_arg_abi == NULL || in.call_arg_sizes == NULL) {
        free(in.sym);
        free(in.args);
        free(in.call_arg_abi);
        free(in.call_arg_sizes);
        return -1;
    }
    in.args[0] = dividend;
    in.args[1] = divisor;
    in.call_arg_sizes[0] = 8;
    in.call_arg_sizes[1] = 8;
    in.call_fixed_count = 2;
    return lz_put(z, &in);
}

/**
 * Whether @p in is 64-bit arithmetic this pass rewrites.
 */
static int needs_split(const cc_ssa_function_t *f, const cc_ssa_instr_t *in) {
    switch (in->op) {
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
    case CC_SSA_MUL:
    case CC_SSA_DIV:
        /* a 64-bit result: an operand that is a word is extended (lz_split()) */
        return is_wide(f, in->dst);
    case CC_SSA_CMP:
        return is_wide(f, in->lhs) && is_wide(f, in->rhs);
    default:
        return 0;
    }
}

/**
 * Rewrite one 64-bit operation (see the file comment).
 *
 * @return 0, or -1 when out of memory
 */
static int split_one(lz_t *z, const cc_ssa_instr_t *in) {
    words_t a;
    words_t b;

    if (in->op == CC_SSA_DIV) {
        return split_div(z, in);
    }
    if (lz_split(z, in->lhs, &a) != 0) {
        return -1;
    }
    if ((in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) && in->rhs < 0) {
        int k = (int)(in->imm & 63);

        if (k == 0) {
            return lz_pair(z, in->dst, a.lo, a.hi);
        }
        return split_shift_const(z, in, &a, k);
    }
    if (in->rhs < 0) {
        /* an immediate operand: as a constant pair */
        unsigned long long u = (unsigned long long)in->imm;

        b.lo = lz_const(z, (long long)(u & 0xffffffffULL));
        b.hi = b.lo < 0 ? -1 : lz_const(z, (long long)(u >> 32));
        if (b.hi < 0) {
            return -1;
        }
    } else if (in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) {
        /* a shift count: only its low word matters */
        b.lo = is_wide(z->f, in->rhs) ? lz_op(z, CC_SSA_LO32, in->rhs, -1, 0, 1, -1) : in->rhs;
        b.hi = -1;
        if (b.lo < 0) {
            return -1;
        }
    } else if (lz_split(z, in->rhs, &b) != 0) {
        return -1;
    }
    switch (in->op) {
    case CC_SSA_SHL:
    case CC_SSA_SHR:
        return split_shift_var(z, in, &a, &b);
    case CC_SSA_MUL:
        return split_mul(z, in, &a, &b);
    case CC_SSA_CMP:
        return split_cmp(z, in, &a, &b);
    default:
        return split_simple(z, in, &a, &b);
    }
}

int cc_legalize_i386_function(cc_ssa_function_t *f, cc_diag_t *diag) {
    lz_t z;
    size_t i;
    int any = 0;

    for (i = 0; i < f->instr_count && !any; ++i) {
        any = needs_split(f, &f->instrs[i]);
    }
    if (!any) {
        return 0;
    }
    memset(&z, 0, sizeof(z));
    z.f = f;
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];

        if (needs_split(f, in)) {
            if (split_one(&z, in) != 0) {
                break;
            }
            cc_ssa_instr_free(in);
        } else if (lz_put(&z, in) != 0) {
            break;
        }
    }
    if (i < f->instr_count) {
        /* the instructions not yet moved are freed with the old list */
        for (; i < f->instr_count; ++i) {
            cc_ssa_instr_free(&f->instrs[i]);
        }
        free(f->instrs);
        f->instrs = z.out;
        f->instr_count = z.count;
        f->instr_cap = z.cap;
        cc_diag_set(diag, "", 0, 0, "out of memory legalizing 64-bit arithmetic");
        return -1;
    }
    free(f->instrs);
    f->instrs = z.out;
    f->instr_count = z.count;
    f->instr_cap = z.cap;
    return 0;
}
