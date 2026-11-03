/*
 * range.c: the ranges integer values keep, and the compares they decide.
 *
 * gcc's value-range propagation knows what a compare's operands can be
 * and folds the compare when every pair of them answers alike; Linux's
 * BUILD_BUG_ON leans on it.  min() and max() ask, in a BUILD_BUG_ON,
 * __builtin_constant_p((long long)x >= 0) && (long long)x >= 0 of a
 * signed operand compared with an unsigned one -- true for gcc where x is
 * a loop's `len` inside `while (len > 0)` (fs/exec.c, fs/ext4/mballoc.c);
 * and kaslr's `vaddr_start >= vaddr_end`, of a vaddr_start one of two
 * constants, mmu_gather's `flags > 3` of a flags `delay ? 1 : 0` or'ed
 * with 2, task_mmu's `58 < (c ? 52 : 46) - 12`, a switch's case comparing
 * its value again: each left a call of an undefined __compiletime_assert_N
 * where cc did not know.
 *
 * What a value can hold is an interval of its register's content, read as
 * a signed word of the width the folder computes it in (opt.c): 64 bits
 * on x86-64, 32 for a value narrower than 8 bytes on i386.  It is found in
 * two steps.
 *
 * Over the whole function first: each definition's interval from its
 * operands' -- a constant, a narrow load's or an extension's bounds, a
 * mask, sums and differences that cannot overflow -- and a value defined
 * more than once (a variable) the union of its definitions', until
 * nothing changes; a value whose interval keeps growing (a loop's
 * counter) is let go to every value it can hold.  A value whose address
 * is taken, or that an asm reads or writes, holds anything.
 *
 * Then along the stream, what branches teach: on the edge of a compare of
 * a value with a constant into a label nothing else reaches, the value is
 * within what the compare allows -- and, where the compare reads an
 * extension of a variable, so is that variable's narrow part.  Such a
 * fact holds from where it is learned until the value is defined again or
 * until a label some other way reaches: a label any edge into which comes
 * from before the fact, or from later in the stream (a loop's jump back).
 * A definition of a value defined once made where facts hold has their
 * interval wherever it is read, as every read comes after it; a
 * variable's, as a fact of its own.
 *
 * A compare both of whose operands' intervals answer it one way is that
 * answer: an unsigned compare only where neither interval straddles the
 * sign.  Nothing else is changed here; the folding that follows (opt.c)
 * takes the constants on.
 */

#include "cc_middle.h"
#include "cc_options.h"
#include "cc_target.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/** A closed interval of a value's content, read as signed; empty when lo > hi. */
typedef cc_range_t vrange_t;

/** What a stretch of the stream knows of one value: its content, or its low bits extended, within r. */
typedef struct {
    int v;         /**< the value */
    int bits;      /**< 0: the value's whole content; else the low `bits` bits, extended as `sign` says */
    int sign;      /**< those bits sign-extended (1) or zero-extended (0) */
    vrange_t r;    /**< what it is within */
    long start;    /**< the instruction from which it holds */
} vfact_t;

/** vfact_t.start of a fact about a value defined once, made where it holds: it holds wherever the value is read. */
#define FACT_ALWAYS LONG_MIN

/** The most facts a label takes in from the branch into it. */
#define EDGE_FACTS 4

/** The most rounds the whole-function step makes before it gives the function up. */
#define RANGE_ROUNDS 64

/** How many times a value's interval may grow before it is let go to every value it can hold. */
#define RANGE_GROWTH 3

/** The state of one function's walk. */
typedef struct {
    cc_ssa_function_t *f;
    const cc_range_t *params; /**< per parameter index: what every call passes it, or NULL */
    cc_range_call_fn on_call; /**< receives each direct call, or NULL */
    void *ctx;                /**< passed to on_call */
    int nv;
    unsigned char *bits;      /**< per value: the width its content is read at, 32 or 64; 0 not an integer */
    unsigned char *pinned;    /**< per value: reached otherwise than by its name -- address taken, an asm's */
    int *ndefs;               /**< per value: how many instructions define it */
    long *def_at;             /**< per value defined once: its instruction, else -1 */
    vrange_t *r;              /**< per value: what it can hold anywhere it is read */
    unsigned char *grew;      /**< per value: how many times r grew */
    vrange_t *shl_s;          /**< per value made by `shl x, k`: x's low bits sign-extended as the shl read them */
    vrange_t *shl_z;          /**< and zero-extended */
    long *label_at;           /**< per label: its instruction, or -1 */
    long *edge_min;           /**< per label: the first instruction an edge into it comes from (-1: anywhere) */
    long *edge_max;           /**< per label: the last */
    int *edge_count;          /**< per label: how many edges come into it */
    vfact_t *edge;            /**< per label: EDGE_FACTS facts the one branch into it teaches */
    int *edge_n;              /**< per label: how many */
    vfact_t *facts;           /**< what holds at the walk's place */
    int nfacts;
    int cap;
} range_t;

/** The interval of every value of @p bits bits. */
static vrange_t full_range(int bits) {
    vrange_t r;

    r.lo = bits >= 64 ? LLONG_MIN : -(1LL << (bits - 1));
    r.hi = bits >= 64 ? LLONG_MAX : (1LL << (bits - 1)) - 1;
    return r;
}

/** The interval of the one value @p c. */
static vrange_t point_range(long long c) {
    vrange_t r;

    r.lo = c;
    r.hi = c;
    return r;
}

/** Whether @p r holds every value of @p bits bits. */
static int is_full(vrange_t r, int bits) {
    vrange_t f = full_range(bits);

    return r.lo <= f.lo && r.hi >= f.hi;
}

/** What @p a and @p b both allow; when nothing, @p a. */
static vrange_t meet(vrange_t a, vrange_t b) {
    vrange_t r;

    r.lo = a.lo > b.lo ? a.lo : b.lo;
    r.hi = a.hi < b.hi ? a.hi : b.hi;
    return r.lo > r.hi ? a : r;
}

/** What @p a or @p b allows; an empty one adds nothing. */
static vrange_t join(vrange_t a, vrange_t b) {
    vrange_t r;

    if (a.lo > a.hi) {
        return b;
    }
    if (b.lo > b.hi) {
        return a;
    }
    r.lo = a.lo < b.lo ? a.lo : b.lo;
    r.hi = a.hi > b.hi ? a.hi : b.hi;
    return r;
}

/** @p x read as a signed word of @p bits bits. */
static long long sign_word(long long x, int bits) {
    if (bits >= 64) {
        return x;
    }
    return (long long)(int)(unsigned int)(unsigned long long)x;
}

/** @p x shifted right @p k places, arithmetically, without relying on how the host shifts a negative. */
static long long asr(long long x, int k) {
    return x >= 0 ? x >> k : ~((~x) >> k);
}

/**
 * The interval of @p x's low @p n bits extended -- sign-extended if @p sign -- when x is within @p r: r itself
 * where every value of it is so already, else every value the extension can give.
 */
static vrange_t ext_range(vrange_t r, int n, int sign) {
    vrange_t b;

    if (n >= 64) {
        return r;
    }
    if (sign) {
        b.lo = -(1LL << (n - 1));
        b.hi = (1LL << (n - 1)) - 1;
    } else {
        b.lo = 0;
        b.hi = n >= 63 ? LLONG_MAX : (1LL << n) - 1;
    }
    return (r.lo >= b.lo && r.hi <= b.hi) ? r : b;
}

/** a + b of @p bits bits, or 0 when it may overflow them. */
static int add_fits(long long a, long long b, int bits, long long *out) {
    vrange_t f = full_range(bits);

    if ((b > 0 && a > f.hi - b) || (b < 0 && a < f.lo - b)) {
        return 0;
    }
    *out = a + b;
    return 1;
}

/** The interval of @p a + @p b (@p sub: a - b) in @p bits bits: every value where a sum may overflow. */
static vrange_t add_range(vrange_t a, vrange_t b, int sub, int bits) {
    vrange_t r;
    long long blo = b.lo;
    long long bhi = b.hi;

    if (sub) {
        /* -b: its bounds swap, and the least value has no negation */
        if (b.lo <= full_range(bits).lo) {
            return full_range(bits);
        }
        blo = -b.hi;
        bhi = -b.lo;
    }
    if (!add_fits(a.lo, blo, bits, &r.lo) || !add_fits(a.hi, bhi, bits, &r.hi)) {
        return full_range(bits);
    }
    return r;
}

/** The least power of two above @p x (x >= 0), less one: the mask of x's bits. */
static long long bits_mask(long long x) {
    unsigned long long m = 0;

    while (m < (unsigned long long)x) {
        m = (m << 1) | 1;
    }
    return (long long)m;
}

/** Whether @p v is an integer value the walk follows. */
static int tracked(const range_t *s, int v) {
    return v >= 0 && v < s->nv && s->bits[v] != 0 && !s->pinned[v];
}

/** What value @p v can hold anywhere, by the whole-function step. */
static vrange_t whole(const range_t *s, int v) {
    if (v < 0 || v >= s->nv || s->bits[v] == 0) {
        return full_range(64);
    }
    if (!tracked(s, v)) {
        return full_range(s->bits[v]);
    }
    return s->r[v];
}

/**
 * What value @p v holds at the walk's place (@p walking), or anywhere (not): its interval with the facts
 * about its whole content.
 */
static vrange_t current(const range_t *s, int v, int walking) {
    vrange_t r = whole(s, v);
    int i;

    for (i = 0; walking && i < s->nfacts; ++i) {
        if (s->facts[i].v == v && s->facts[i].bits == 0) {
            r = meet(r, s->facts[i].r);
        }
    }
    return r;
}

/**
 * What the low @p n bits of value @p v, extended as @p sign says, are within at the walk's place (@p walking)
 * or anywhere: from its whole interval, and from the facts about those bits.
 */
static vrange_t current_ext(const range_t *s, int v, int n, int sign, int walking) {
    vrange_t r = ext_range(current(s, v, walking), n, sign);
    int i;

    for (i = 0; walking && i < s->nfacts; ++i) {
        const vfact_t *fa = &s->facts[i];

        /* (the other extension of the same bits, where their top bit is clear: the same value) */
        if (fa->v == v && fa->bits == n &&
            (fa->sign == sign || (fa->r.lo >= 0 && (n >= 64 || fa->r.hi < (1LL << (n - 1)))))) {
            r = meet(r, fa->r);
        }
    }
    return r;
}

/**
 * How many low bits of its operand x the shl @p in keeps, if it is one whose `shr (shl x, k), k` the walk reads
 * as x's extension (shl_s, shl_z): `shl x, k` by a constant, defined once, x and the result of the width of the
 * result's size; else 0.
 */
static int shl_ext_bits(const range_t *s, const cc_ssa_instr_t *in) {
    int d = in->dst;

    if (in->op != CC_SSA_SHL || !tracked(s, d) || s->def_at[d] < 0 || !tracked(s, in->lhs) ||
        s->bits[in->lhs] != s->bits[d] || in->rhs >= 0 || in->imm <= 0 || in->imm >= s->bits[d] ||
        8 * (int)cc_ssa_value_size(s->f, d) != s->bits[d]) {
        return 0;
    }
    return s->bits[d] - (int)in->imm;
}

/**
 * How many low bits of an operand the and @p in keeps, if it masks them with a constant 2^n - 1, n below the
 * result's width: a zero extension of them, as cc converts an int to an unsigned int; else 0.  @p x receives
 * the operand.
 */
static int mask_ext_bits(const range_t *s, const cc_ssa_instr_t *in, int *x) {
    int k;

    if (in->op != CC_SSA_AND || !tracked(s, in->dst)) {
        return 0;
    }
    for (k = 0; k < 2; ++k) {
        int m = k == 0 ? in->rhs : in->lhs;
        int o = k == 0 ? in->lhs : in->rhs;
        long long c;
        int n = 0;

        if (!tracked(s, m) || !tracked(s, o) || s->bits[o] != s->bits[in->dst] || s->def_at[m] < 0 ||
            s->f->instrs[s->def_at[m]].op != CC_SSA_CONST) {
            continue;
        }
        c = sign_word(s->f->instrs[s->def_at[m]].imm, s->bits[m]);
        if (c <= 0) {
            continue;
        }
        while (n < 63 && ((c >> n) & 1) != 0) {
            n++;
        }
        if (n < s->bits[in->dst] && (c >> n) == 0) {
            *x = o;
            return n;
        }
    }
    return 0;
}

/** Whether `shr (shl x, k), k`'s shl, value @p u, is one the walk knows x's extension at (shl_ext_bits()). */
static int ext_of(const range_t *s, int u, int k, int *n) {
    const cc_ssa_instr_t *d;

    if (u < 0 || u >= s->nv || s->def_at[u] < 0) {
        return 0;
    }
    d = &s->f->instrs[s->def_at[u]];
    if (d->imm != k || (*n = shl_ext_bits(s, d)) == 0) {
        return 0;
    }
    return 1;
}

/** Whether @p r holds nothing: no definition of its value has been seen yet. */
static int is_empty(vrange_t r) {
    return r.lo > r.hi;
}

/**
 * The interval of what instruction @p in defines, from its operands' intervals at the walk's place
 * (@p walking) or anywhere.  @p bits is the result's width.
 */
static vrange_t eval(range_t *s, const cc_ssa_instr_t *in, int bits, int walking) {
    vrange_t full = full_range(bits);
    vrange_t a;
    vrange_t b;
    int n;

    /* an operand nothing has defined yet: nothing yet (the whole-function step comes back to it) */
    if ((in->op == CC_SSA_ADD || in->op == CC_SSA_SUB || in->op == CC_SSA_MUL || in->op == CC_SSA_AND ||
         in->op == CC_SSA_OR || in->op == CC_SSA_XOR || in->op == CC_SSA_DIV) &&
        tracked(s, in->lhs) && tracked(s, in->rhs) &&
        (is_empty(current(s, in->lhs, walking)) || is_empty(current(s, in->rhs, walking)))) {
        a.lo = 1;
        a.hi = 0;
        return a;
    }
    if ((in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) && tracked(s, in->lhs) &&
        is_empty(current(s, in->lhs, walking))) {
        a.lo = 1;
        a.hi = 0;
        return a;
    }
    switch (in->op) {
    case CC_SSA_CONST:
        return point_range(sign_word(in->imm, bits));
    case CC_SSA_PARAM:
        /* what every call of a static function passes it (fold_module_ranges(), opt.c) */
        if (s->params != NULL && in->param_index >= 0 && (size_t)in->param_index < s->f->param_count &&
            s->params[in->param_index].lo >= full.lo && s->params[in->param_index].hi <= full.hi &&
            !is_empty(s->params[in->param_index])) {
            return s->params[in->param_index];
        }
        return full;
    case CC_SSA_CMP:
    case CC_SSA_ISCONST:
        a.lo = 0;
        a.hi = 1;
        return a;
    case CC_SSA_MOV:
        if (!tracked(s, in->lhs) || s->bits[in->lhs] != bits) {
            return full;
        }
        return current(s, in->lhs, walking);
    case CC_SSA_LOAD:
        /* a narrow load extends the bytes it reads, as its signedness says */
        if (in->seg == 0 && (in->imm == 1 || in->imm == 2 || in->imm == 4) && 8 * in->imm < bits) {
            return ext_range(full, (int)(8 * in->imm), !in->is_unsigned);
        }
        return full;
    case CC_SSA_ADD:
    case CC_SSA_SUB:
        if (!tracked(s, in->lhs) || !tracked(s, in->rhs) || s->bits[in->lhs] != bits || s->bits[in->rhs] != bits) {
            return full;
        }
        return add_range(current(s, in->lhs, walking), current(s, in->rhs, walking), in->op == CC_SSA_SUB, bits);
    case CC_SSA_MUL:
        if (!tracked(s, in->lhs) || !tracked(s, in->rhs) || s->bits[in->lhs] != bits || s->bits[in->rhs] != bits) {
            return full;
        }
        a = current(s, in->lhs, walking);
        b = current(s, in->rhs, walking);
        /* of two that are not negative, if the largest product fits */
        if (a.lo < 0 || b.lo < 0 || (a.hi != 0 && b.hi > full.hi / a.hi)) {
            return full;
        }
        a.lo *= b.lo;
        a.hi *= b.hi;
        return a;
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
        if (!tracked(s, in->lhs) || !tracked(s, in->rhs) || s->bits[in->lhs] != bits || s->bits[in->rhs] != bits) {
            return full;
        }
        {
            int x = -1;

            /* x & (2^n - 1): x's low n bits zero-extended */
            if ((n = mask_ext_bits(s, in, &x)) > 0) {
                return current_ext(s, x, n, 0, walking);
            }
        }
        a = current(s, in->lhs, walking);
        b = current(s, in->rhs, walking);
        if (in->op == CC_SSA_AND) {
            /* not negative, and no more than the operand that is not negative */
            if (a.lo >= 0 && b.lo >= 0) {
                a.lo = 0;
                a.hi = a.hi < b.hi ? a.hi : b.hi;
                return a;
            }
            if (a.lo >= 0 || b.lo >= 0) {
                a.hi = a.lo >= 0 ? a.hi : b.hi;
                a.lo = 0;
                return a;
            }
            return full;
        }
        /* of two not negative: within the bits either may have set (an or, no less than either) */
        if (a.lo < 0 || b.lo < 0) {
            return full;
        }
        a.lo = in->op == CC_SSA_OR ? (a.lo > b.lo ? a.lo : b.lo) : 0;
        a.hi = bits_mask(a.hi > b.hi ? a.hi : b.hi);
        return a;
    case CC_SSA_SHL:
        if (in->rhs >= 0 || in->imm <= 0 || in->imm >= bits || !tracked(s, in->lhs) || s->bits[in->lhs] != bits) {
            return full;
        }
        a = current(s, in->lhs, walking);
        if (a.lo < asr(full.lo, (int)in->imm) || a.hi > (full.hi >> in->imm)) {
            return full;
        }
        a.lo = (long long)((unsigned long long)a.lo << in->imm);
        a.hi = (long long)((unsigned long long)a.hi << in->imm);
        return a;
    case CC_SSA_SHR:
        if (in->rhs >= 0 || in->imm <= 0 || in->imm >= bits || in->lhs < 0 || in->lhs >= s->nv) {
            return full;
        }
        if (ext_of(s, in->lhs, (int)in->imm, &n)) {
            /* `shr (shl x, k), k`: x's low bits extended, as the shl read x */
            return in->is_unsigned ? s->shl_z[in->lhs] : s->shl_s[in->lhs];
        }
        if (!tracked(s, in->lhs) || s->bits[in->lhs] != bits) {
            return full;
        }
        a = current(s, in->lhs, walking);
        if (!in->is_unsigned || a.lo >= 0) {
            a.lo = asr(a.lo, (int)in->imm);
            a.hi = asr(a.hi, (int)in->imm);
            return a;
        }
        a.lo = 0;
        a.hi = (long long)((bits >= 64 ? ~0ULL : (1ULL << bits) - 1) >> in->imm);
        return a;
    case CC_SSA_DIV:
        /* by a positive constant, truncating: in order */
        if (!tracked(s, in->lhs) || !tracked(s, in->rhs) || s->bits[in->lhs] != bits || s->bits[in->rhs] != bits) {
            return full;
        }
        a = current(s, in->lhs, walking);
        b = current(s, in->rhs, walking);
        if (b.lo != b.hi || b.lo <= 0 || (in->is_unsigned && a.lo < 0)) {
            return full;
        }
        a.lo /= b.lo;
        a.hi /= b.lo;
        return a;
    default:
        return full;
    }
}

/**
 * Whether the compare of intervals @p a and @p b of kind @p kind (unsigned if @p uns) answers one way, and
 * how (@p out).
 */
static int decide(cc_cmp_kind_t kind, int uns, vrange_t a, vrange_t b, long long *out) {
    int lt;  /* every a < every b */
    int gt;  /* every a > every b */
    int le;
    int ge;

    if (is_empty(a) || is_empty(b)) {
        return 0;
    }
    if (uns && ((b.lo == 0 && b.hi == 0 && (kind == CC_CMP_GE || kind == CC_CMP_LT)) ||
                (a.lo == 0 && a.hi == 0 && (kind == CC_CMP_LE || kind == CC_CMP_GT)))) {
        /* nothing is below 0 unsigned: i386's (long long)x >= 0 tests the low word so */
        *out = kind == CC_CMP_GE || kind == CC_CMP_LE;
        return 1;
    }
    if (uns) {
        /* unsigned order is the signed one within either half; every value of the lower half is below */
        int an = a.lo >= 0;
        int bn = b.lo >= 0;
        int aneg = a.hi < 0;
        int bneg = b.hi < 0;

        if (an && bneg) {
            *out = kind == CC_CMP_NE || kind == CC_CMP_LT || kind == CC_CMP_LE;
            return 1;
        }
        if (aneg && bn) {
            *out = kind == CC_CMP_NE || kind == CC_CMP_GT || kind == CC_CMP_GE;
            return 1;
        }
        if (!((an && bn) || (aneg && bneg))) {
            return 0;
        }
    }
    lt = a.hi < b.lo;
    gt = a.lo > b.hi;
    le = a.hi <= b.lo;
    ge = a.lo >= b.hi;
    switch (kind) {
    case CC_CMP_EQ:
    case CC_CMP_NE:
        if (a.lo == a.hi && b.lo == b.hi && a.lo == b.lo) {
            *out = kind == CC_CMP_EQ;
            return 1;
        }
        if (lt || gt) {
            *out = kind == CC_CMP_NE;
            return 1;
        }
        return 0;
    case CC_CMP_LT:
        if (lt || ge) {
            *out = lt;
            return 1;
        }
        return 0;
    case CC_CMP_LE:
        if (le || gt) {
            *out = le;
            return 1;
        }
        return 0;
    case CC_CMP_GT:
        if (gt || le) {
            *out = gt;
            return 1;
        }
        return 0;
    case CC_CMP_GE:
        if (ge || lt) {
            *out = ge;
            return 1;
        }
        return 0;
    }
    return 0;
}

/**
 * What `a kind c` being @p truth teaches of a, within @p r, for a constant c, in @p bits bits; 0 when it
 * teaches nothing an interval can say (a != c inside r, an unsigned bound that spans the sign).
 */
static int refine(cc_cmp_kind_t kind, int uns, int truth, long long c, int bits, vrange_t *r) {
    vrange_t full = full_range(bits);
    vrange_t t = full;

    if (!truth) {
        switch (kind) {
        case CC_CMP_EQ: kind = CC_CMP_NE; break;
        case CC_CMP_NE: kind = CC_CMP_EQ; break;
        case CC_CMP_LT: kind = CC_CMP_GE; break;
        case CC_CMP_LE: kind = CC_CMP_GT; break;
        case CC_CMP_GT: kind = CC_CMP_LE; break;
        case CC_CMP_GE: kind = CC_CMP_LT; break;
        }
    }
    switch (kind) {
    case CC_CMP_EQ:
        t = point_range(c);
        break;
    case CC_CMP_NE:
        if (r->lo == c && c < full.hi) {
            t.lo = c + 1;
        } else if (r->hi == c && c > full.lo) {
            t.hi = c - 1;
        } else {
            return 0;
        }
        break;
    case CC_CMP_LT:
    case CC_CMP_LE:
        if (kind == CC_CMP_LT && c == (uns ? 0 : full.lo)) {
            return 0; /* nothing is below: the edge cannot be taken */
        }
        t.hi = kind == CC_CMP_LT ? c - 1 : c;
        if (uns) {
            /* below c unsigned: from 0, if c is not negative */
            if (c < 0) {
                return 0;
            }
            t.lo = 0;
        }
        break;
    case CC_CMP_GT:
    case CC_CMP_GE:
        if (kind == CC_CMP_GT && c == (uns ? -1 : full.hi)) {
            return 0;
        }
        t.lo = kind == CC_CMP_GT ? c + 1 : c;
        if (uns) {
            /* above c unsigned: up to -1, if c is negative */
            if (c >= 0) {
                return 0;
            }
            t.hi = -1;
        }
        break;
    }
    if (t.lo > t.hi) {
        return 0;
    }
    *r = meet(*r, t);
    return 1;
}

/** Forget what the walk knows of value @p v: it is defined again. */
static void kill_facts(range_t *s, int v) {
    int i = 0;

    while (i < s->nfacts) {
        if (s->facts[i].v == v) {
            s->facts[i] = s->facts[--s->nfacts];
        } else {
            i++;
        }
    }
}

/** Know @p fact from here on. @return 0, or -1 when out of memory */
static int add_fact(range_t *s, const vfact_t *fact) {
    if (s->nfacts == s->cap) {
        int cap = s->cap > 0 ? s->cap * 2 : 16;
        vfact_t *nf = (vfact_t *)realloc(s->facts, (size_t)cap * sizeof(*nf));

        if (nf == NULL) {
            return -1;
        }
        s->facts = nf;
        s->cap = cap;
    }
    s->facts[s->nfacts++] = *fact;
    return 0;
}

/** Whether an instruction strictly between @p from and @p to is a label or defines @p v (-1: none). */
static int disturbed(const range_t *s, long from, long to, int v) {
    long i;

    for (i = from + 1; i < to; ++i) {
        const cc_ssa_instr_t *in = &s->f->instrs[i];
        size_t k;

        if (in->op == CC_SSA_LABEL || (v >= 0 && in->dst == v)) {
            return 1;
        }
        for (k = 0; v >= 0 && in->op == CC_SSA_ASM && k < in->asm_out_count; ++k) {
            int ov = in->asm_out_values[k];

            if (CC_SSA_ASM_MEM_INDIRECT_P(ov)) {
                ov = CC_SSA_ASM_MEM_INDIRECT_DECODE(ov);
            }
            if (ov == v) {
                return 1;
            }
        }
    }
    return 0;
}

/**
 * Note for the label @p label that the one branch into it, at @p at, teaches that value @p v (its content,
 * or its low bits extended) is within @p r.
 */
static void edge_fact(range_t *s, int label, int v, int bits, int sign, vrange_t r) {
    vfact_t *e;

    if (label < 0 || label >= s->f->label_count || s->edge_count[label] != 1 || s->edge_n[label] >= EDGE_FACTS ||
        !tracked(s, v)) {
        return;
    }
    e = &s->edge[(size_t)label * EDGE_FACTS + (size_t)s->edge_n[label]++];
    e->v = v;
    e->bits = bits;
    e->sign = sign;
    e->r = r;
    e->start = 0;
}

/**
 * What the branch @p br, at @p at, on a compare of a value with a constant teaches its two labels: the value,
 * a variable it copies, and the variable whose low bits it extends, are within what the compare allows on
 * each way -- where nothing redefines them between their reading and the branch.
 */
static void branch_facts(range_t *s, const cc_ssa_instr_t *br, long at) {
    cc_ssa_function_t *f = s->f;
    const cc_ssa_instr_t *c;
    long ci;
    int way;

    if (br->lhs < 0 || br->lhs >= s->nv || s->def_at[br->lhs] < 0 || br->true_label == br->false_label) {
        return;
    }
    ci = s->def_at[br->lhs];
    c = &f->instrs[ci];
    if (c->op != CC_SSA_CMP || ci >= at || c->lhs < 0 || c->rhs < 0 || disturbed(s, ci, at, -1)) {
        return;
    }
    for (way = 0; way < 2; ++way) {
        int label = way == 0 ? br->true_label : br->false_label;
        int side;

        for (side = 0; side < 2; ++side) {
            int x = side == 0 ? c->lhs : c->rhs;
            int y = side == 0 ? c->rhs : c->lhs;
            cc_cmp_kind_t kind = c->cmp_kind;
            vrange_t yr;
            vrange_t r;
            int bits;
            int n;
            int sign;

            if (!tracked(s, x) || !tracked(s, y) || s->bits[x] != s->bits[y]) {
                continue;
            }
            yr = current(s, y, 1);
            if (yr.lo != yr.hi) {
                continue;
            }
            if (side == 1) {
                /* c kind x: x mirror-kind c */
                switch (kind) {
                case CC_CMP_LT: kind = CC_CMP_GT; break;
                case CC_CMP_LE: kind = CC_CMP_GE; break;
                case CC_CMP_GT: kind = CC_CMP_LT; break;
                case CC_CMP_GE: kind = CC_CMP_LE; break;
                default: break;
                }
            }
            bits = s->bits[x];
            r = current(s, x, 1);
            if (!refine(kind, c->is_unsigned, way == 0, yr.lo, bits, &r) || disturbed(s, ci, at, x)) {
                continue;
            }
            edge_fact(s, label, x, 0, 0, r);
            /*
             * a copy's source, and an extension's -- its low bits -- and their sources in turn: the variable
             * the code after reads again
             */
            n = 0;
            sign = 0;
            for (;;) {
                const cc_ssa_instr_t *d;
                long di;
                int en;

                if (x < 0 || x >= s->nv || s->def_at[x] < 0) {
                    break;
                }
                di = s->def_at[x];
                d = &f->instrs[di];
                if (d->op == CC_SSA_MOV && tracked(s, d->lhs) && s->bits[d->lhs] == s->bits[x] &&
                    !disturbed(s, di, at, d->lhs)) {
                    x = d->lhs;
                    edge_fact(s, label, x, n, sign, r);
                    continue;
                }
                if (n == 0 && d->op == CC_SSA_AND) {
                    int src = -1;

                    en = mask_ext_bits(s, d, &src);
                    if (en > 0 && !disturbed(s, di, at, src)) {
                        x = src;
                        n = en;
                        sign = 0;
                        edge_fact(s, label, x, n, sign, r);
                        continue;
                    }
                }
                if (n == 0 && d->op == CC_SSA_SHR && d->rhs < 0 && ext_of(s, d->lhs, (int)d->imm, &en)) {
                    long si = s->def_at[d->lhs];
                    int src = f->instrs[si].lhs;

                    if (!disturbed(s, si, at, src)) {
                        x = src;
                        n = en;
                        sign = !d->is_unsigned;
                        edge_fact(s, label, x, n, sign, r);
                        continue;
                    }
                }
                break;
            }
        }
    }
}

/** Free what @p s holds. */
static void range_free(range_t *s) {
    free(s->bits);
    free(s->pinned);
    free(s->ndefs);
    free(s->def_at);
    free(s->r);
    free(s->grew);
    free(s->shl_s);
    free(s->shl_z);
    free(s->label_at);
    free(s->edge_min);
    free(s->edge_max);
    free(s->edge_count);
    free(s->edge);
    free(s->edge_n);
    free(s->facts);
}

/** Note value @p v as reached otherwise than by its name (cc_ssa_for_each_use() callback). */
static void note_pinned(void *ctx, int v, int role) {
    range_t *s = (range_t *)ctx;

    (void)role;
    if (v >= 0 && v < s->nv) {
        s->pinned[v] = 1;
    }
}

/** Note an edge from instruction @p from into @p label. */
static void note_edge(range_t *s, int label, long from) {
    if (label < 0 || label >= s->f->label_count) {
        return;
    }
    if (s->edge_count[label] == 0 || from < s->edge_min[label]) {
        s->edge_min[label] = from;
    }
    if (s->edge_count[label] == 0 || from > s->edge_max[label]) {
        s->edge_max[label] = from;
    }
    s->edge_count[label]++;
}

/**
 * Set up @p s for @p f: each value's width, definitions and pinning, each label's edges.
 *
 * @return 0, or -1 when out of memory
 */
static int range_init(range_t *s, cc_ssa_function_t *f) {
    size_t nv = (size_t)(f->value_count > 0 ? f->value_count : 1);
    size_t nl = (size_t)(f->label_count > 0 ? f->label_count : 1);
    int word32 = cc_target_pointer_size() == 4;
    size_t i;
    int v;

    memset(s, 0, sizeof(*s));
    s->f = f;
    s->nv = f->value_count;
    s->bits = (unsigned char *)calloc(nv, 1);
    s->pinned = (unsigned char *)calloc(nv, 1);
    s->ndefs = (int *)calloc(nv, sizeof(int));
    s->def_at = (long *)malloc(nv * sizeof(long));
    s->r = (vrange_t *)malloc(nv * sizeof(vrange_t));
    s->grew = (unsigned char *)calloc(nv, 1);
    s->shl_s = (vrange_t *)malloc(nv * sizeof(vrange_t));
    s->shl_z = (vrange_t *)malloc(nv * sizeof(vrange_t));
    s->label_at = (long *)malloc(nl * sizeof(long));
    s->edge_min = (long *)malloc(nl * sizeof(long));
    s->edge_max = (long *)malloc(nl * sizeof(long));
    s->edge_count = (int *)calloc(nl, sizeof(int));
    s->edge = (vfact_t *)malloc(nl * EDGE_FACTS * sizeof(vfact_t));
    s->edge_n = (int *)calloc(nl, sizeof(int));
    if (s->bits == NULL || s->pinned == NULL || s->ndefs == NULL || s->def_at == NULL || s->r == NULL ||
        s->grew == NULL || s->shl_s == NULL || s->shl_z == NULL || s->label_at == NULL || s->edge_min == NULL ||
        s->edge_max == NULL || s->edge_count == NULL || s->edge == NULL || s->edge_n == NULL) {
        return -1;
    }
    for (v = 0; v < f->value_count; ++v) {
        uint32_t size = cc_ssa_value_size(f, v);

        s->def_at[v] = -1;
        if (f->value_ty[v].kind == CC_TY_INT) {
            s->bits[v] = (unsigned char)(word32 && size < 8 ? 32 : 64);
        }
        s->r[v].lo = 1; /* empty: no definition seen yet */
        s->r[v].hi = 0;
        s->shl_s[v] = s->r[v];
        s->shl_z[v] = s->r[v];
    }
    for (i = 0; i < nl; ++i) {
        s->label_at[i] = -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        size_t k;

        if (in->dst >= 0 && in->dst < f->value_count) {
            s->ndefs[in->dst]++;
            s->def_at[in->dst] = s->ndefs[in->dst] == 1 ? (long)i : -1;
        }
        if (in->op == CC_SSA_ADDR && in->lhs >= 0 && in->lhs < f->value_count) {
            s->pinned[in->lhs] = 1;
        } else if (in->op == CC_SSA_ASM) {
            cc_ssa_for_each_use(in, note_pinned, s);
            for (k = 0; k < in->asm_goto_count; ++k) {
                note_edge(s, in->asm_goto_labels[k], (long)i);
            }
        }
        switch (in->op) {
        case CC_SSA_LABEL:
            if (in->label >= 0 && in->label < f->label_count) {
                s->label_at[in->label] = (long)i;
                if (i > 0 && !cc_ssa_opcode_is_terminator(f->instrs[i - 1].op)) {
                    note_edge(s, in->label, (long)i - 1);
                }
            }
            break;
        case CC_SSA_BR:
            note_edge(s, in->label, (long)i);
            break;
        case CC_SSA_BR_COND:
            note_edge(s, in->true_label, (long)i);
            note_edge(s, in->false_label, (long)i);
            break;
        case CC_SSA_LADDR:
            /* a label whose address is taken is reached from anywhere */
            note_edge(s, in->label, -1);
            note_edge(s, in->label, -1);
            break;
        default:
            break;
        }
    }
    return 0;
}

/**
 * The whole-function step: each value's interval from its definitions', until nothing changes.
 *
 * @return 1 when it settled, 0 when it did not within RANGE_ROUNDS rounds
 */
static int range_settle(range_t *s) {
    cc_ssa_function_t *f = s->f;
    int round;

    for (round = 0; round < RANGE_ROUNDS; ++round) {
        int changed = 0;
        size_t i;

        for (i = 0; i < f->instr_count; ++i) {
            const cc_ssa_instr_t *in = &f->instrs[i];
            int d = in->dst;
            int n = shl_ext_bits(s, in);
            vrange_t r;
            vrange_t nr;

            if (n > 0) {
                s->shl_s[d] = current_ext(s, in->lhs, n, 1, 0);
                s->shl_z[d] = current_ext(s, in->lhs, n, 0, 0);
            }
            if (!tracked(s, d)) {
                continue;
            }
            r = eval(s, in, s->bits[d], 0);
            nr = join(s->r[d], r);
            if (nr.lo != s->r[d].lo || nr.hi != s->r[d].hi) {
                if (s->r[d].lo <= s->r[d].hi && ++s->grew[d] > RANGE_GROWTH) {
                    nr = full_range(s->bits[d]);
                }
                if (nr.lo != s->r[d].lo || nr.hi != s->r[d].hi) {
                    s->r[d] = nr;
                    changed = 1;
                }
            }
        }
        if (!changed) {
            int v;

            /* a value no definition of which gave it anything (one only ever read uninitialized) holds anything */
            for (v = 0; v < s->nv; ++v) {
                if (tracked(s, v) && is_empty(s->r[v])) {
                    s->r[v] = full_range(s->bits[v]);
                }
                if (is_empty(s->shl_s[v]) || is_empty(s->shl_z[v])) {
                    s->shl_s[v] = full_range(64);
                    s->shl_z[v] = full_range(64);
                }
            }
            return 1;
        }
    }
    return 0;
}

/**
 * Report the direct call @p in to s->on_call: each argument's interval where the walk is (@p walking), or
 * every value where nothing is known.
 *
 * @return 0, or -1 when out of memory
 */
static int report_call(range_t *s, const cc_ssa_instr_t *in, int walking) {
    cc_range_t *args = (cc_range_t *)malloc((in->arg_count > 0 ? in->arg_count : 1) * sizeof(*args));
    size_t a;

    if (args == NULL) {
        return -1;
    }
    for (a = 0; a < in->arg_count; ++a) {
        int v = in->args[a];

        args[a] = full_range(64);
        if (walking && tracked(s, v)) {
            args[a] = current(s, v, 1);
            if (is_empty(args[a])) {
                args[a] = full_range(64);
            }
        }
    }
    s->on_call(s->ctx, in, args);
    free(args);
    return 0;
}

/**
 * The walk along the stream: the facts branches teach, the intervals they give what is defined under them,
 * and the compares decided.
 *
 * @return 1 when a compare was folded, 0 when none was, or -1 when out of memory
 */
static int range_walk(range_t *s) {
    cc_ssa_function_t *f = s->f;
    int changed = 0;
    size_t i;

    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *in = &f->instrs[i];
        int d = in->dst;
        vrange_t r;
        int k;

        if (in->op == CC_SSA_LABEL) {
            int l = in->label;
            int keep = l >= 0 && l < f->label_count && s->edge_count[l] > 0 && s->edge_max[l] < (long)i;

            /* what held before holds on only if every way in comes from where it held */
            k = 0;
            while (k < s->nfacts) {
                if (s->facts[k].start != FACT_ALWAYS && (!keep || s->edge_min[l] < s->facts[k].start)) {
                    s->facts[k] = s->facts[--s->nfacts];
                } else {
                    k++;
                }
            }
            for (k = 0; keep && k < s->edge_n[l]; ++k) {
                vfact_t fact = s->edge[(size_t)l * EDGE_FACTS + (size_t)k];

                fact.start = (long)i;
                if (add_fact(s, &fact) != 0) {
                    return -1;
                }
            }
            continue;
        }
        if (in->op == CC_SSA_BR_COND) {
            branch_facts(s, in, (long)i);
            continue;
        }
        if (in->op == CC_SSA_ASM) {
            for (k = 0; k < (int)in->asm_out_count; ++k) {
                int ov = in->asm_out_values[k];

                if (CC_SSA_ASM_MEM_INDIRECT_P(ov)) {
                    ov = CC_SSA_ASM_MEM_INDIRECT_DECODE(ov);
                }
                kill_facts(s, ov);
            }
        }
        if (in->op == CC_SSA_CALL && s->on_call != NULL && report_call(s, in, 1) != 0) {
            return -1;
        }
        if (d < 0 || d >= s->nv) {
            continue;
        }
        if (in->op == CC_SSA_CMP && s->def_at[d] >= 0 && tracked(s, in->lhs) && tracked(s, in->rhs) &&
            s->bits[in->lhs] == s->bits[in->rhs]) {
            long long out;

            if (decide(in->cmp_kind, in->is_unsigned, current(s, in->lhs, 1), current(s, in->rhs, 1), &out)) {
                in->op = CC_SSA_CONST;
                in->imm = out;
                in->lhs = -1;
                in->rhs = -1;
                changed = 1;
            }
        }
        if (shl_ext_bits(s, in) > 0) {
            int n = shl_ext_bits(s, in);

            s->shl_s[d] = meet(s->shl_s[d], current_ext(s, in->lhs, n, 1, 1));
            s->shl_z[d] = meet(s->shl_z[d], current_ext(s, in->lhs, n, 0, 1));
        }
        kill_facts(s, d);
        if (!tracked(s, d)) {
            continue;
        }
        r = eval(s, in, s->bits[d], 1);
        if (in->op == CC_SSA_MOV && tracked(s, in->lhs) && s->bits[in->lhs] == s->bits[d]) {
            /* a copy's low bits are its source's: what is known of them, from here, or everywhere if defined once */
            int nf = s->nfacts;

            for (k = 0; k < nf; ++k) {
                if (s->facts[k].v == in->lhs && s->facts[k].bits != 0) {
                    vfact_t fact = s->facts[k];

                    fact.v = d;
                    fact.start = s->def_at[d] >= 0 ? FACT_ALWAYS : (long)i;
                    if (add_fact(s, &fact) != 0) {
                        return -1;
                    }
                }
            }
        }
        if (s->def_at[d] >= 0) {
            /* defined once, here: what holds here holds wherever it is read */
            s->r[d] = meet(s->r[d], r);
        } else if (!is_full(r, s->bits[d])) {
            vfact_t fact;

            fact.v = d;
            fact.bits = 0;
            fact.sign = 0;
            fact.r = r;
            fact.start = (long)i;
            if (add_fact(s, &fact) != 0) {
                return -1;
            }
        }
    }
    return changed;
}

/**
 * Fold each compare of @p f that the ranges of its operands decide (see the head of this file), with what
 * @p params says every call passes each parameter; report each direct call to @p on_call.
 *
 * @return 1 when a compare was folded, 0 when none was, -1 when out of memory
 */
int cc_fold_ranges(cc_ssa_function_t *f, const cc_range_t *params, cc_range_call_fn on_call, void *ctx) {
    range_t s;
    size_t i;
    int rc = 0;

    /* (a function that compares nothing has nothing to fold, unless its calls are asked for) */
    i = 0;
    while (on_call == NULL && i < f->instr_count && f->instrs[i].op != CC_SSA_CMP) {
        i++;
    }
    if (on_call == NULL && (i == f->instr_count || f->value_count <= 0)) {
        return 0;
    }
    if (range_init(&s, f) != 0) {
        range_free(&s);
        return -1;
    }
    s.params = params;
    s.on_call = on_call;
    s.ctx = ctx;
    if (f->value_count > 0 && range_settle(&s)) {
        rc = range_walk(&s);
    } else if (on_call != NULL) {
        /* nothing known: every call passes anything */
        for (i = 0; i < f->instr_count && rc == 0; ++i) {
            if (f->instrs[i].op == CC_SSA_CALL) {
                rc = report_call(&s, &f->instrs[i], 0);
            }
        }
    }
    range_free(&s);
    return rc;
}
