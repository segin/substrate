/*
 * mir_fp.c: floating point in machine IR (mir_sel.c), what the targets
 * share.
 *
 * A floating value is held one of two ways (xs_fp_kind()).  Where the
 * target computes doubles with SSE (x86-64, and i386 under -mfpmath=sse),
 * a float or a double is a virtual vector register, which linscan.c gives
 * a %xmm register like any other -- a float held as the double it rounds
 * to, as lowering keeps it (CC_SSA_FROUND32 rounds it).  Anything else --
 * a long double, every floating value of i386's x87 code -- lives in a
 * frame object of its own, its home, 8 bytes for a double and 16 for a
 * long double in x87's 80-bit form.  Within a block the x87 stack holds
 * them as well (the stack cache, xsel_t's fq): an operation takes its
 * operands from the stack where they are on it -- the register itself
 * when this is the block's last read of the value, fxch bringing it to
 * the top, else a copy, fld %st(i), or in place, fop %st(i), %st, when
 * the block reads it again -- and from the home where not, a double's
 * read by the operation itself (fopl), and leaves its result on the
 * stack, its home written only when a later block may read it there
 * (fq_global) and only when it leaves the stack.  A result rounded to a
 * double, which a store to its 8-byte home rounds, stays unrounded until
 * something reads it otherwise (xfq_t's r), then is rounded where it is,
 * once.  A copy of a value with the same value in it (a widening) is
 * another name of its register (xfq_t's al); a float or a double loaded
 * for one read later in the block is loaded there, or read from memory
 * by the operation, unless a store or a variable written again comes
 * first (xsel_t's fqm); a constant is fldz, fld1 or the function's own
 * in .rodata (pool_const()).  The
 * stack is emptied before anything the cache does not follow -- a call,
 * an asm, a branch, a label, and the floating operations that keep to
 * the homes (xs_fp_begin()) -- so that the allocator, which never sees
 * it, and the calls need not know of it; integer instructions between
 * leave it be.  A vector register goes through a scratch slot where x87
 * code reads or writes it.
 *
 * A compare sets the flags as ucomisd does (fucompp, fnstsw and sahf on
 * the x87, which has no fucomip below the Pentium Pro) and is the setcc
 * of a condition that is false when unordered; == and != read the parity
 * flag as well.  Conversions to and from integers truncate as C does,
 * an unsigned 64-bit one by the usual halving or offsetting by 2^63.
 *
 * The targets' parameters, calls and returns (mir_x86_64.c, mir_i386.c)
 * use xs_fp_push(), xs_fp_pop(), xs_fp_to_mem() and xs_fp_from_mem().
 */

#include "cc_x86_mir.h"

#include "cc_options.h"
#include "cc_x86.h"

#include <float.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int xs_fp_kind(const xsel_t *s, int v) {
    if (v < 0 || v >= s->f->value_count || cc_ssa_value_class(s->f, v) != CC_VAL_F64) {
        return XFP_NONE;
    }
    if (s->t->sse_fp && value_size_bytes(s->f, v, 8) <= 8) {
        return XFP_SSE;
    }
    return XFP_X87;
}

/** Whether floating value @p v is a long double: 80 bits, kept in 16. */
static int fp_wide(const xsel_t *s, int v) {
    return value_size_bytes(s->f, v, 8) >= 10;
}

xop_t xs_fp_reg(xsel_t *s, int v) {
    return xop_reg(xs_vreg_of(s, v, 0), 8);
}

xop_t xs_fp_home(xsel_t *s, int v) {
    int bytes = fp_wide(s, v) ? 16 : 8;

    if (s->fhome[v] < 0) {
        s->fhome[v] = xf_new_obj(&s->xf, XF_LOCAL, bytes, bytes, 0);
    }
    return xop_frame(s->fhome[v], 0, bytes);
}

/** Scratch memory @p bytes bytes long at the start of the function's 16-byte scratch object (made the first time). */
static xop_t scratch(xsel_t *s, int bytes) {
    if (s->fscratch < 0) {
        s->fscratch = xf_new_obj(&s->xf, XF_LOCAL, 16, 8, 0);
    }
    return xop_frame(s->fscratch, 0, bytes);
}

/** The second scratch object, for the x87 control word and its constants (16 bytes). */
static xop_t scratch_cw(xsel_t *s) {
    if (s->fcw < 0) {
        s->fcw = xf_new_obj(&s->xf, XF_LOCAL, 16, 8, 0);
    }
    return xop_frame(s->fcw, 0, 2);
}

/** Append instruction @p op of one memory operand @p m. */
static void emit1(xsel_t *s, xi_op_t op, const xop_t *m) {
    xs_emit(s, op, s->word, 1, m, NULL, NULL);
}

/* ---- the x87 stack cache ---- */

static void fq_put(xsel_t *s, int v);
static void fqm_load_all(xsel_t *s);

/** Append an x87 instruction @p op on stack register @p i (XI_FLDST, XI_FXCH) or on none. */
static void fq_emit_st(xsel_t *s, xi_op_t op, int i) {
    xinst_t x = xi_make(op, s->word, 0, NULL, NULL, NULL);

    x.cc = (unsigned char)i;
    xf_emit(&s->xf, &x);
}

/** The model: push register (@p v, contents exact in @p e bytes, wanting rounding to @p r) on the stack. */
static void fq_push_model(xsel_t *s, int v, int e, int r) {
    if (s->fqn >= XFQ_DEPTH) {
        s->unsupported = __LINE__;
        s->unsupported_file = __FILE__;
        return;
    }
    memmove(&s->fq[1], &s->fq[0], (size_t)s->fqn * sizeof(s->fq[0]));
    s->fq[0].v = v;
    s->fq[0].al[0] = XFQ_DEAD;
    s->fq[0].al[1] = XFQ_DEAD;
    s->fq[0].dirty = 0;
    s->fq[0].e = (unsigned char)e;
    s->fq[0].r = (unsigned char)r;
    s->fqn++;
}

/** The model: pop the stack. */
static void fq_pop_model(xsel_t *s) {
    if (s->fqn > 0) {
        s->fqn--;
        memmove(&s->fq[0], &s->fq[1], (size_t)s->fqn * sizeof(s->fq[0]));
    }
}

/** Whether stack register @p q holds IR value @p v, under its own name or a copy's. */
static int fq_has_name(const xfq_t *q, int v) {
    return q->v == v || (v >= 0 && (q->al[0] == v || q->al[1] == v));
}

/** The stack register holding IR value @p v, under any of its names, or -1. */
static int fq_find(const xsel_t *s, int v) {
    int i;

    for (i = 0; i < s->fqn; ++i) {
        if (fq_has_name(&s->fq[i], v)) {
            return i;
        }
    }
    return -1;
}

/** IR value @p v is written anew: the register that held it before does not any more. */
static void fq_unname(xsel_t *s, int v) {
    int i = fq_find(s, v);
    int k;

    if (i < 0) {
        return;
    }
    if (s->fq[i].v == v) {
        s->fq[i].v = XFQ_DEAD;
    }
    for (k = 0; k < 2; ++k) {
        if (s->fq[i].al[k] == v) {
            s->fq[i].al[k] = XFQ_DEAD;
        }
    }
}

/** Whether stack register @p q is read again in the block under a name other than @p v. */
static int fq_others_read(const xsel_t *s, const xfq_t *q, int v) {
    int k;

    if (q->v >= 0 && q->v != v && s->fq_left[q->v] > 0) {
        return 1;
    }
    for (k = 0; k < 2; ++k) {
        if (q->al[k] >= 0 && q->al[k] != v && s->fq_left[q->al[k]] > 0) {
            return 1;
        }
    }
    return 0;
}

/** Bring stack register @p i to the top: fxch, the model's two exchanged. */
static void fq_fxch(xsel_t *s, int i) {
    xfq_t t;

    if (i <= 0) {
        return;
    }
    if (s->xf.nins > 0 && s->xf.ins[s->xf.nins - 1].op == XI_FXCH && s->xf.ins[s->xf.nins - 1].cc == i) {
        s->xf.nins--; /* undoes the one before */
    } else {
        fq_emit_st(s, XI_FXCH, i);
    }
    t = s->fq[0];
    s->fq[0] = s->fq[i];
    s->fq[i] = t;
}

/** Whether stack register @p q is read again in the block, under any name. */
static int fq_read_again(const xsel_t *s, const xfq_t *q) {
    return fq_others_read(s, q, XFQ_RAW);
}

/** Whether stack register @p q's value must be written to its home before the register goes. */
static int fq_home_wanted(const xsel_t *s, const xfq_t *q) {
    return q->v >= 0 && q->dirty && (s->fq_global[q->v] || s->fq_left[q->v] > 0);
}

/** Round the top of the stack to the precision it wants, through scratch memory of that size. */
static void fq_round_top(xsel_t *s) {
    xfq_t *q = &s->fq[0];

    if (q->r != 0 && q->e > q->r) {
        xop_t m = scratch(s, q->r);

        emit1(s, XI_FSTP, &m);
        emit1(s, XI_FLD, &m);
        q->e = q->r;
    }
    q->r = 0;
}

/**
 * Write the value at the top of the stack to the home of @p v, one of its names, the register popped unless
 * @p keep: a double's store rounds it as it wants, other roundings are made first.
 */
static void fq_store_top_as(xsel_t *s, int v, int keep) {
    xfq_t *q = &s->fq[0];
    xop_t m = xs_fp_home(s, v);

    if (q->r != 0 && q->e > q->r && !(m.size == 8 && q->r == 8)) {
        fq_round_top(s);
    }
    if (!keep) {
        emit1(s, XI_FSTP, &m);
        fq_pop_model(s);
        return;
    }
    if (m.size == 8) {
        emit1(s, XI_FST, &m);
    } else {
        /* (no 80-bit store that keeps the register) */
        fq_emit_st(s, XI_FLDST, 0);
        emit1(s, XI_FSTP, &m);
    }
    if (v == q->v) {
        q->dirty = 0;
    }
}

/** fq_store_top_as() the top's own value. */
static void fq_store_top(xsel_t *s, int keep) {
    fq_store_top_as(s, s->fq[0].v, keep);
}

/** Pop the top of the stack, its value written to its homes if anything may read it there. */
static void fq_drop_top(xsel_t *s) {
    int k;

    for (k = 0; k < 2; ++k) {
        /* a copy's home, read later from there */
        int a = s->fq[0].al[k];

        if (a >= 0 && s->fq_left[a] > 0) {
            fq_store_top_as(s, a, 1);
        }
    }
    if (fq_home_wanted(s, &s->fq[0])) {
        fq_store_top(s, 0);
        return;
    }
    fq_emit_st(s, XI_FSTP0, 0);
    fq_pop_model(s);
}

/** Empty the stack, each value written to its home where it is wanted there. */
static void fq_flush(xsel_t *s) {
    fqm_load_all(s);
    while (s->fqn > 0) {
        fq_drop_top(s);
    }
}

/** Pop what the top of the stack holds while nothing in the block reads it again. */
static void fq_tidy(xsel_t *s) {
    while (s->fqn > 0 && s->fq[0].v != XFQ_RAW && !fq_read_again(s, &s->fq[0])) {
        fq_drop_top(s);
    }
}

/**
 * Make room on the stack for @p n more registers: one nothing reads again goes first, the deepest, else the deepest
 * -- never @p a or @p b, the instruction's own operands, which its reads, counted already, leave unread.
 */
static void fq_room(xsel_t *s, int n, int a, int b) {
    while (s->fqn + n > XFQ_DEPTH) {
        int k = -1;
        int i;

        for (i = s->fqn - 1; i >= 0; --i) {
            if (s->fq[i].v == XFQ_RAW || fq_has_name(&s->fq[i], a) || fq_has_name(&s->fq[i], b)) {
                continue;
            }
            if (k < 0) {
                k = i;
            }
            if (!fq_read_again(s, &s->fq[i])) {
                k = i;
                break;
            }
        }
        fq_fxch(s, k);
        fq_drop_top(s);
    }
}

/** The deferred load of floating value @p v (xsel_t's fqm), or -1. */
static int fqm_find(const xsel_t *s, int v) {
    int k;

    for (k = 0; k < s->nfqm; ++k) {
        if (s->fqm[k].v == v) {
            return k;
        }
    }
    return -1;
}

/** Forget deferred load @p k, its memory operand returned: the reader reads it there. */
static xop_t fqm_take(xsel_t *s, int k) {
    xop_t m = s->fqm[k].m;

    s->nfqm--;
    memmove(&s->fqm[k], &s->fqm[k + 1], (size_t)(s->nfqm - k) * sizeof(s->fqm[0]));
    return m;
}

/** Make deferred load @p k: its value pushed on the stack, as any loaded value. */
static void fqm_load(xsel_t *s, int k) {
    int v = s->fqm[k].v;
    xop_t m = fqm_take(s, k);

    fq_room(s, 1, v, v);
    emit1(s, XI_FLD, &m);
    fq_push_model(s, XFQ_RAW, m.size, 0);
    fq_put(s, v);
}

/** Make every deferred load: before what may write the memory or the registers they read. */
static void fqm_load_all(xsel_t *s) {
    while (s->nfqm > 0) {
        fqm_load(s, 0);
    }
}

/**
 * Push floating value @p v as an operand (XFQ_RAW) on the stack: its register when nothing reads it again (@p more
 * the reads of it the same instruction still makes), a copy of it when something does, or its home when it is not
 * on the stack -- and kept there as well when it is read again and @p after more operands leave room.  With
 * @p exact the operand is the value, rounded as it wants; else it may want rounding still (its r).
 */
static void fq_take(xsel_t *s, int v, int more, int after, int exact) {
    int i;
    int again = more > 0 || s->fq_left[v] > 0;

    if (fqm_find(s, v) >= 0) {
        fqm_load(s, fqm_find(s, v));
    }
    i = s->addr_taken[v] ? -1 : fq_find(s, v);

    if (i >= 0 && !again) {
        /* its register is given up only when its other names are not read again either */
        again = fq_others_read(s, &s->fq[i], v);
    }
    if (i < 0) {
        xop_t m = xs_fp_home(s, v);
        int e = m.size == 8 ? 8 : 10;

        emit1(s, XI_FLD, &m);
        if (again && !s->addr_taken[v] && s->fqn + 2 + after <= XFQ_DEPTH) {
            fq_push_model(s, v, e, 0);
            fq_emit_st(s, XI_FLDST, 0);
        }
        fq_push_model(s, XFQ_RAW, e, 0);
    } else if (again) {
        xfq_t q;

        if (s->fq[i].r != 0 && s->fq[i].e > s->fq[i].r) {
            /* rounded where it is, once, rather than each copy (read more than once, it is seldom only stored) */
            fq_fxch(s, i);
            fq_round_top(s);
            i = 0;
        }
        q = s->fq[i];
        fq_emit_st(s, XI_FLDST, i);
        fq_push_model(s, XFQ_RAW, q.e, q.r);
    } else {
        if (fq_home_wanted(s, &s->fq[i])) {
            fq_fxch(s, i);
            fq_store_top(s, 1);
            i = 0;
        }
        fq_fxch(s, i);
        s->fq[0].v = XFQ_RAW;
        s->fq[0].al[0] = XFQ_DEAD;
        s->fq[0].al[1] = XFQ_DEAD;
        s->fq[0].dirty = 0;
    }
    if (exact) {
        fq_round_top(s);
    }
}

/** The operand at the top of the stack is now floating value @p v, which holds it rounded to its home's size. */
static void fq_put(xsel_t *s, int v) {
    xfq_t *q = &s->fq[0];

    if (!fp_wide(s, v) && q->r == 0 && q->e > 8) {
        q->r = 8;
    }
    fq_unname(s, v); /* what held it before */
    q->v = v;
    q->al[0] = XFQ_DEAD;
    q->al[1] = XFQ_DEAD;
    q->dirty = 1;
    if (s->addr_taken[v]) {
        /* read through pointers as well: its home, always */
        fq_store_top(s, 0);
    }
}

/**
 * Bring the two operands in flight to st(1) (@p first, taken first) and st(0), the one taken second on top
 * already: 1 when they end the other way round, as two fxch make it cheaper, else 0.
 */
static int fq_arrange(xsel_t *s) {
    int d;

    for (d = 1; d < s->fqn && s->fq[d].v != XFQ_RAW; ++d) {
    }
    if (d == 1 || d >= s->fqn) {
        return 0;
    }
    /* st(0) the second, st(d) the first: the second to st(1), then the first to st(0) */
    fq_fxch(s, 1);
    fq_fxch(s, d);
    return 1;
}

/** Round stack register @p i in place to the precision it wants, if it wants one: its copies are exact then. */
static void fq_round_at(xsel_t *s, int i) {
    if (s->fq[i].r != 0 && s->fq[i].e > s->fq[i].r) {
        fq_fxch(s, i);
        fq_round_top(s);
    }
}

/** The stack register of floating value @p v when the block reads it again after this instruction, else -1. */
static int fq_kept(const xsel_t *s, int v) {
    int i = s->addr_taken[v] ? -1 : fq_find(s, v);

    return i >= 0 && s->fq_left[v] > 0 ? i : -1;
}

/** Whether floating value @p v is not on the stack and its home is a double, which an x87 operation may read. */
static int fq_in_home8(const xsel_t *s, int v) {
    return (s->addr_taken[v] || fq_find(s, v) < 0) && fqm_find(s, v) < 0 && !fp_wide(s, v);
}

/** st(0) = st(0) op memory @p m (4 or 8 bytes), @p k the op as fq_op_st()'s. */
static void fq_op_mem(xsel_t *s, int k, xop_t m) {
    emit1(s, XI_FOPM, &m);
    s->xf.ins[s->xf.nins - 1].cc = (unsigned char)k;
}

/** st(0) = st(0) op st(@p i), @p k the op (with bit 2 the operands the other way round). */
static void fq_op_st(xsel_t *s, int k, int i) {
    xinst_t x = xi_make(XI_FOPST, s->word, 0, NULL, NULL, NULL);

    x.cc = (unsigned char)k;
    x.aux = i;
    xf_emit(&s->xf, &x);
}

/**
 * x87 value @p d = @p a op @p b, @p k the op (XI_FARITH's), the result left on the stack.  One operand the block
 * reads again stays where it is and the operation reads it there (fop %st(i), %st), the other brought to the top;
 * one not on the stack whose home is a double is read from it (fopl); else both are brought to the top and popped
 * together (fop{,r}p).
 */
static void fq_binop(xsel_t *s, int d, int a, int b, int k) {
    int i;

    fq_room(s, 2, a, b);
    if (a == b || ((i = fq_find(s, a)) >= 0 && i == fq_find(s, b))) {
        /* one register, both operands: copies of one value */
        fq_take(s, a, 0, 0, 1);
        fq_op_st(s, k, 0);
    } else if ((i = fqm_find(s, b)) >= 0) {
        /* read where it was to be loaded from */
        xop_t m = fqm_take(s, i);

        fq_take(s, a, 0, 0, 1);
        fq_op_mem(s, k, m);
    } else if ((i = fqm_find(s, a)) >= 0) {
        xop_t m = fqm_take(s, i);

        fq_take(s, b, 0, 0, 1);
        fq_op_mem(s, k | 4, m);
    } else if ((i = fq_kept(s, b)) >= 0) {
        fq_round_at(s, i);
        fq_take(s, a, 0, 0, 1);
        fq_op_st(s, k, fq_find(s, b));
    } else if ((i = fq_kept(s, a)) >= 0) {
        fq_round_at(s, i);
        fq_take(s, b, 0, 0, 1);
        fq_op_st(s, k | 4, fq_find(s, a));
    } else if (fq_in_home8(s, b)) {
        fq_take(s, a, 0, 0, 1);
        fq_op_mem(s, k, xs_fp_home(s, b));
    } else if (fq_in_home8(s, a)) {
        fq_take(s, b, 0, 0, 1);
        fq_op_mem(s, k | 4, xs_fp_home(s, a));
    } else {
        int rev;

        fq_take(s, a, 0, 1, 1);
        fq_take(s, b, 0, 0, 1);
        rev = fq_arrange(s);
        fq_emit_st(s, XI_FARITH, k | (rev ? 4 : 0));
        fq_pop_model(s);
    }
    s->fq[0].e = 10;
    s->fq[0].r = 0;
    fq_put(s, d);
    fq_tidy(s);
}

/** Whether a floating value is x87's, kept in a home the stack cache may hold. */
static int fq_x87(const xsel_t *s, int v) {
    return xs_fp_kind(s, v) == XFP_X87;
}

/** Whether IR instruction @p in leaves the x87 stack as it is or is one the cache does itself. */
static int fq_follows(const xsel_t *s, const cc_ssa_instr_t *in) {
    int fl = xs_fp_kind(s, in->lhs) != XFP_NONE;
    int fr = xs_fp_kind(s, in->rhs) != XFP_NONE;
    int fd = xs_fp_kind(s, in->dst) != XFP_NONE;

    switch (in->op) {
    case CC_SSA_STR:
    case CC_SSA_GADDR:
    case CC_SSA_LADDR:
    case CC_SSA_ADDR:
    case CC_SSA_EXT:
    case CC_SSA_LO32:
    case CC_SSA_HI32:
    case CC_SSA_PAIR:
    case CC_SSA_UMULW:
    case CC_SSA_UMULH:
    case CC_SSA_STACKALLOC:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
        return !fl && !fr && !fd;
    case CC_SSA_CONST:
        return !fd || fq_x87(s, in->dst);
    case CC_SSA_MOV:
        return (!fl && !fd) || (fq_x87(s, in->lhs) && fq_x87(s, in->dst));
    case CC_SSA_LOAD:
        return !fd || (fq_x87(s, in->dst) && (in->imm == 4 || in->imm == 8 || in->imm >= 10));
    case CC_SSA_STORE:
        return !fr || (fq_x87(s, in->rhs) && (in->imm == 4 || in->imm == 8 || in->imm >= 10));
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_DIV:
        return (!fl && !fr && !fd) || (fq_x87(s, in->lhs) && fq_x87(s, in->rhs) && fq_x87(s, in->dst));
    case CC_SSA_CMP:
        return (!fl && !fr) || (fq_x87(s, in->lhs) && fq_x87(s, in->rhs));
    case CC_SSA_FROUND32:
    case CC_SSA_FROUND64:
        return fq_x87(s, in->lhs) && fq_x87(s, in->dst);
    default:
        return 0;
    }
}

/** x86_for_each_use() callback: one read of the value more left in the block. */
static void fq_count_read(int v, void *ctx) {
    xsel_t *s = (xsel_t *)ctx;

    if (v >= 0 && v < s->f->value_count) {
        s->fq_left[v]++;
    }
}

/** x86_for_each_use() callback: one read of the value fewer. */
static void fq_uncount_read(int v, void *ctx) {
    xsel_t *s = (xsel_t *)ctx;

    if (v >= 0 && v < s->f->value_count && s->fq_left[v] > 0) {
        s->fq_left[v]--;
    }
}

typedef struct {
    xsel_t *s;
    int *def_block; /**< per value: the block it was last written in, or -1 */
    int block;
} fq_global_ctx_t;

/** x86_for_each_use() callback: a read of a value not written earlier in its block reads its home. */
static void fq_mark_global(int v, void *ctx) {
    fq_global_ctx_t *c = (fq_global_ctx_t *)ctx;

    if (v >= 0 && v < c->s->f->value_count && c->def_block[v] != c->block) {
        c->s->fq_global[v] = 1;
    }
}

int xs_fp_init(xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    size_t n = (size_t)(f->value_count > 0 ? f->value_count : 1);
    fq_global_ctx_t c;
    size_t i;
    int v;

    s->fqn = 0;
    s->fq_any = 0;
    for (v = 0; v < f->value_count; ++v) {
        if (xs_fp_kind(s, v) == XFP_X87) {
            s->fq_any = 1;
        }
    }
    if (!s->fq_any) {
        return 0;
    }
    s->fq_left = (int *)calloc(n, sizeof(int));
    s->fq_global = (unsigned char *)calloc(n, 1);
    c.def_block = (int *)malloc(n * sizeof(int));
    if (s->fq_left == NULL || s->fq_global == NULL || c.def_block == NULL) {
        free(c.def_block);
        return -1;
    }
    for (v = 0; v < (int)n; ++v) {
        c.def_block[v] = -1;
    }
    /*
     * A value read in a block before the block writes it is read from its home: global, its home written before
     * it leaves the stack.  Any other's home is dead once its block has read it for the last time.
     */
    c.s = s;
    c.block = 0;
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_LABEL) {
            c.block++;
        }
        x86_for_each_use(f, in, fq_mark_global, &c);
        if (in->dst >= 0 && in->dst < f->value_count && in->op != CC_SSA_STORE &&
            !((in->op == CC_SSA_CALL || in->op == CC_SSA_CALLI) && in->rhs == in->dst)) {
            c.def_block[in->dst] = c.block;
        }
    }
    free(c.def_block);
    return 0;
}

void xs_fp_free(xsel_t *s) {
    free(s->fq_left);
    free(s->fq_global);
    free(s->fpool);
    s->fq_left = NULL;
    s->fq_global = NULL;
    s->fpool = NULL;
    s->nfpool = 0;
    s->fpoolcap = 0;
}

void xs_fp_begin(xsel_t *s, size_t j) {
    const cc_ssa_function_t *f = s->f;
    const cc_ssa_instr_t *in = &f->instrs[j];

    if (!s->fq_any) {
        return;
    }
    if (j == 0 || in->op == CC_SSA_LABEL) {
        /* a block: its reads of each value counted */
        size_t k;

        fq_flush(s);
        for (k = j; k < f->instr_count && (k == j || f->instrs[k].op != CC_SSA_LABEL); ++k) {
            x86_for_each_use(f, &f->instrs[k], fq_count_read, s);
        }
    }
    if (!fq_follows(s, in)) {
        /* (its reads counted still: it reads the homes) */
        fq_flush(s);
    } else if (s->nfqm > 0 && (in->op == CC_SSA_STORE || (in->dst >= 0 && in->dst < f->value_count &&
                                                           xs_fp_kind(s, in->dst) == XFP_NONE &&
                                                           s->ndefs[in->dst] != 1))) {
        /* a store may write what a deferred load reads, a variable written again be a register its address is in */
        fqm_load_all(s);
    }
    x86_for_each_use(f, in, fq_uncount_read, s);
}

void xs_fp_push(xsel_t *s, int v) {
    xop_t m;

    if (xs_fp_kind(s, v) == XFP_SSE) {
        xop_t r = xs_fp_reg(s, v);

        m = scratch(s, 8);
        xs_emit(s, XI_MOV, 8, 2, &r, &m, NULL);
    } else {
        m = xs_fp_home(s, v);
        m.size = (unsigned char)(fp_wide(s, v) ? 16 : 8);
    }
    emit1(s, XI_FLD, &m);
}

void xs_fp_pop(xsel_t *s, int v) {
    if (v < 0) {
        xinst_t x = xi_make(XI_FSTP0, s->word, 0, NULL, NULL, NULL);

        xf_emit(&s->xf, &x);
        return;
    }
    if (xs_fp_kind(s, v) == XFP_SSE) {
        xop_t m = scratch(s, 8);
        xop_t r = xs_fp_reg(s, v);

        emit1(s, XI_FSTP, &m);
        xs_emit(s, XI_MOV, 8, 2, &m, &r, NULL);
        return;
    }
    {
        xop_t m = xs_fp_home(s, v);

        emit1(s, XI_FSTP, &m);
    }
}

void xs_fp_from_mem(xsel_t *s, int v, xop_t m, int bytes) {
    if (xs_fp_kind(s, v) == XFP_SSE && bytes <= 8) {
        xop_t r = xs_fp_reg(s, v);

        m.size = (unsigned char)(bytes == 4 ? 4 : 8);
        xs_emit(s, bytes == 4 ? XI_CVTSS2SD : XI_MOV, 8, 2, &m, &r, NULL);
        return;
    }
    m.size = (unsigned char)(bytes == 4 ? 4 : bytes == 8 ? 8 : 16);
    emit1(s, XI_FLD, &m);
    xs_fp_pop(s, v);
}

void xs_fp_to_mem(xsel_t *s, int v, xop_t m, int bytes) {
    if (xs_fp_kind(s, v) == XFP_SSE && bytes <= 8) {
        xop_t r = xs_fp_reg(s, v);

        if (bytes == 4) {
            /* rounded to a float, its 4 bytes */
            xop_t t = xop_reg(xf_new_xvreg(&s->xf), 8);

            xs_emit(s, XI_CVTSD2SS, 8, 2, &r, &t, NULL);
            m.size = 4;
            xs_emit(s, XI_MOV, 4, 2, &t, &m, NULL);
            return;
        }
        m.size = 8;
        xs_emit(s, XI_MOV, 8, 2, &r, &m, NULL);
        return;
    }
    xs_fp_push(s, v);
    m.size = (unsigned char)(bytes == 4 ? 4 : bytes == 8 ? 8 : 16);
    emit1(s, XI_FSTP, &m);
}

/**
 * Write the @p n bytes at @p bytes into memory @p m, a word at a time through general registers (a move of a
 * 64-bit constant into memory has no encoding).
 */
static void store_bytes_const(xsel_t *s, xop_t m, const unsigned char *bytes, int n) {
    int w = s->word;
    int off;

    for (off = 0; off < n; off += w) {
        unsigned long long u = 0;
        int k;
        xop_t a = m;
        xop_t t = xs_temp(s);
        xop_t k64;

        for (k = 0; k < w && off + k < n; ++k) {
            u |= (unsigned long long)bytes[off + k] << (8 * k);
        }
        k64 = xop_imm(w == 4 ? (long long)(int)(unsigned)u : (long long)u, w);
        a.disp += off;
        a.size = (unsigned char)w;
        xs_emit(s, XI_MOV, w, 2, &k64, &t, NULL);
        xs_emit(s, XI_MOV, w, 2, &t, &a, NULL);
    }
}

/**
 * The @p n bytes at @p raw (8, a double; 16, a long double's 10 and padding) as a constant of the function's own in
 * .rodata, as a memory operand: one already there if it is, gcc's .LC constants.  A double built in memory from two
 * words' stores instead is read before they can be forwarded to the load.
 */
static xop_t pool_const(xsel_t *s, const unsigned char *raw, int n) {
    char name[32];
    xop_t o = xop_mem(s->got, 0, n);
    int k;

    for (k = 0; k < s->nfpool && (s->fpool[k].n != n || memcmp(s->fpool[k].b, raw, (size_t)n) != 0); ++k) {
    }
    if (k == s->nfpool) {
        if (s->nfpool == s->fpoolcap) {
            int cap = s->fpoolcap > 0 ? s->fpoolcap * 2 : 8;
            xfconst_t *p = (xfconst_t *)realloc(s->fpool, (size_t)cap * sizeof(*p));

            if (p == NULL) {
                s->xf.oom = 1;
                return o;
            }
            s->fpool = p;
            s->fpoolcap = cap;
        }
        memset(&s->fpool[k], 0, sizeof(s->fpool[k]));
        memcpy(s->fpool[k].b, raw, (size_t)n);
        s->fpool[k].n = n;
        s->nfpool++;
    }
    snprintf(name, sizeof(name), "_fc%d", k);
    o.sym = xs_keep_name(".L", s->f->name);
    o.sym = o.sym != NULL ? xs_keep_name(o.sym, name) : NULL;
    if (o.sym != NULL && s->got != XR_NONE) {
        o.sym = xs_reloc_name(o.sym, "GOTOFF");
    }
    if (o.sym == NULL) {
        s->xf.oom = 1;
    }
    return o;
}

void xs_fp_print_pool(FILE *fp, const xsel_t *s) {
    int k;
    int i;

    for (k = 0; k < s->nfpool; ++k) {
        const xfconst_t *c = &s->fpool[k];

        fprintf(fp, "\t.pushsection .rodata\n\t.p2align %d\n.L%s_fc%d:\n", c->n == 16 ? 4 : 3, s->f->name, k);
        for (i = 0; i < c->n; i += 4) {
            fprintf(fp, "\t.long %u\n",
                    (unsigned)c->b[i] | (unsigned)c->b[i + 1] << 8 | (unsigned)c->b[i + 2] << 16 |
                        (unsigned)c->b[i + 3] << 24);
        }
        fputs("\t.popsection\n", fp);
    }
}

/**
 * Floating constant @p raw (@p n bytes, 8 or 16) into x87 value @p v, on the stack: +0.0 and 1.0 the x87's own,
 * fldz and fld1, any other read from .rodata.
 */
static void fq_const(xsel_t *s, int v, const unsigned char *raw, int n) {
    static const unsigned char one8[8] = {0, 0, 0, 0, 0, 0, 0xf0, 0x3f};
    static const unsigned char one16[10] = {0, 0, 0, 0, 0, 0, 0, 0x80, 0xff, 0x3f};
    static const unsigned char zero[16];

    fq_room(s, 1, v, v);
    if (memcmp(raw, zero, (size_t)(n == 16 ? 10 : 8)) == 0 ||
        memcmp(raw, n == 16 ? one16 : one8, (size_t)(n == 16 ? 10 : 8)) == 0) {
        fq_emit_st(s, XI_FLDK, raw[n == 16 ? 9 : 7] != 0);
    } else {
        xop_t m = pool_const(s, raw, n);

        emit1(s, XI_FLD, &m);
    }
    fq_push_model(s, XFQ_RAW, n == 16 ? 10 : 8, 0);
    fq_put(s, v);
    fq_tidy(s);
}

/**
 * A floating constant: on the x87 fq_const()'s; in a vector register 0.0 as xorps, else its bits through a general
 * register on x86-64 and from .rodata on i386.
 */
static void sel_fconst(xsel_t *s, const cc_ssa_instr_t *in) {
    unsigned char raw[16];

    memset(raw, 0, sizeof(raw));
    if (fp_wide(s, in->dst)) {
        long double cvt = (in->is_unsigned && in->imm >= 1 && in->imm <= 3) ? x86_64_special_ldouble_const(in->imm)
                                                                           : in->fimm_ld;
        size_t ncopy = sizeof(cvt) < sizeof(raw) ? sizeof(cvt) : sizeof(raw);

#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
        if (ncopy > 10) {
            ncopy = 10;
        }
#endif
        memcpy(raw, &cvt, ncopy);
        fq_const(s, in->dst, raw, 16);
        return;
    }
    memcpy(raw, &in->fimm, sizeof(in->fimm));
    if (xs_fp_kind(s, in->dst) == XFP_X87) {
        fq_const(s, in->dst, raw, 8);
        return;
    }
    {
        xop_t d = xs_fp_reg(s, in->dst);
        uint64_t u;

        memcpy(&u, raw, sizeof(u));
        if (u == 0) {
            xs_emit(s, XI_XORPS, 8, 2, &d, &d, NULL);
        } else if (s->word == 8) {
            xop_t t = xs_temp(s);
            xop_t k = xop_imm((long long)u, 8);

            xs_emit(s, XI_MOV, 8, 2, &k, &t, NULL);
            xs_emit(s, XI_MOV, 8, 2, &t, &d, NULL);
        } else {
            xop_t m = pool_const(s, raw, 8);

            xs_emit(s, XI_MOV, 8, 2, &m, &d, NULL);
        }
    }
}

/**
 * Copy the bits of general-register value @p v (a word, or i386's pair of them) into floating value @p d, or with
 * @p to_int the bits of floating @p v into integer @p d: a move that changes a value's class, not its bits.
 */
static void sel_fbits(xsel_t *s, int d, int v, int to_int) {
    xop_t m = scratch(s, 8);
    int fp = to_int ? v : d;
    int ints = to_int ? d : v;
    int words = xs_is_pair(s, ints) ? 2 : 1;
    int k;

    if (!to_int) {
        for (k = 0; k < words; ++k) {
            xop_t a = m;
            xop_t src = xs_src_word(s, ints, k);

            a.disp += 4 * k;
            a.size = (unsigned char)s->word;
            xs_emit(s, XI_MOV, s->word, 2, &src, &a, NULL);
        }
        xs_fp_from_mem(s, fp, m, 8);
        return;
    }
    xs_fp_to_mem(s, fp, m, 8);
    for (k = 0; k < words; ++k) {
        xop_t a = m;
        xop_t r = xop_reg(xs_vreg_of(s, ints, k), s->word);

        a.disp += 4 * k;
        a.size = (unsigned char)s->word;
        xs_emit(s, XI_MOV, s->word, 2, &a, &r, NULL);
    }
}

/** A move into a floating value: a copy, a conversion between a double and a long double, or a change of class. */
static void sel_fmov(xsel_t *s, const cc_ssa_instr_t *in) {
    int kd = xs_fp_kind(s, in->dst);
    int ks = xs_fp_kind(s, in->lhs);

    if (in->dst == in->lhs) {
        return;
    }
    if (ks == XFP_NONE || kd == XFP_NONE) {
        sel_fbits(s, in->dst, in->lhs, kd == XFP_NONE);
        return;
    }
    if (kd == XFP_SSE && ks == XFP_SSE) {
        xop_t a = xs_fp_reg(s, in->lhs);
        xop_t d = xs_fp_reg(s, in->dst);

        xs_emit(s, XI_MOV, 8, 2, &a, &d, NULL);
        return;
    }
    if (kd == XFP_X87 && ks == XFP_X87) {
        int i = s->addr_taken[in->lhs] ? -1 : fq_find(s, in->lhs);
        int k = fqm_find(s, in->lhs);

        if (k >= 0 && s->fq_left[in->lhs] == 0 && s->fq_left[in->dst] == 1 && !s->fq_global[in->dst] &&
            !s->addr_taken[in->dst]) {
            /* a float or a double not loaded yet, the same value in the other (a widening): its load deferred */
            int j = fqm_find(s, in->dst);

            if (j >= 0) {
                fqm_take(s, j);
                k = fqm_find(s, in->lhs);
            }
            fq_unname(s, in->dst);
            s->fqm[k].v = in->dst;
            return;
        }
        k = -1;
        if (i >= 0) {
            k = s->fq[i].al[0] < 0 ? 0 : s->fq[i].al[1] < 0 ? 1 : -1;
        }
        if (k >= 0 && fq_read_again(s, &s->fq[i]) && !s->fq_global[in->dst] && !s->addr_taken[in->dst] &&
            (fp_wide(s, in->dst) || s->fq[i].r != 0 || s->fq[i].e <= 8)) {
            /*
             * a copy of a value whose register is read again, the same value in it (a widening, or a double's
             * copy): the register is the block's own new value as well, under another name, no copy made
             */
            fq_unname(s, in->dst);
            s->fq[i].al[k] = in->dst;
            fq_tidy(s);
            return;
        }
        /* on the stack: its register, or a copy, is the other value now */
        fq_room(s, 1, in->lhs, in->lhs);
        fq_take(s, in->lhs, 0, 0, 0);
        fq_put(s, in->dst);
        fq_tidy(s);
        return;
    }
    xs_fp_push(s, in->lhs);
    xs_fp_pop(s, in->dst);
}

/** Floating add, sub, mul or div: SSE's two-operand form through a copy, or on the x87 stack. */
static void sel_fbinop(xsel_t *s, const cc_ssa_instr_t *in) {
    static const xi_op_t sse[4] = {XI_FADD, XI_FSUB, XI_FMUL, XI_FDIV};
    int k = in->op == CC_SSA_ADD ? 0 : in->op == CC_SSA_SUB ? 1 : in->op == CC_SSA_MUL ? 2 : 3;

    if (xs_fp_kind(s, in->dst) == XFP_SSE && xs_fp_kind(s, in->lhs) == XFP_SSE &&
        xs_fp_kind(s, in->rhs) == XFP_SSE) {
        xop_t a = xs_fp_reg(s, in->lhs);
        xop_t b = xs_fp_reg(s, in->rhs);
        xop_t d = xs_fp_reg(s, in->dst);
        xop_t t = xop_reg(xf_new_xvreg(&s->xf), 8);

        xs_emit(s, XI_MOV, 8, 2, &a, &t, NULL);
        xs_emit(s, sse[k], 8, 2, &b, &t, NULL);
        xs_emit(s, XI_MOV, 8, 2, &t, &d, NULL);
        return;
    }
    if (xs_fp_kind(s, in->dst) == XFP_X87 && xs_fp_kind(s, in->lhs) == XFP_X87 &&
        xs_fp_kind(s, in->rhs) == XFP_X87) {
        fq_binop(s, in->dst, in->lhs, in->rhs, k);
        return;
    }
    {
        xinst_t x = xi_make(XI_FARITH, s->word, 0, NULL, NULL, NULL);

        xs_fp_push(s, in->lhs);
        xs_fp_push(s, in->rhs);
        x.cc = (unsigned char)k;
        xf_emit(&s->xf, &x);
        xs_fp_pop(s, in->dst);
    }
}

/**
 * Set the flags from floating @p a against @p b, as `ucomisd b, a` does: CF when a < b, ZF when equal, all three
 * of CF, ZF and PF when unordered.
 */
static void fcompare(xsel_t *s, int a, int b) {
    if (xs_fp_kind(s, a) == XFP_SSE && xs_fp_kind(s, b) == XFP_SSE) {
        xop_t ra = xs_fp_reg(s, a);
        xop_t rb = xs_fp_reg(s, b);

        xs_emit(s, XI_UCOMI, 8, 2, &rb, &ra, NULL);
        return;
    }
    {
        xinst_t x = xi_make(XI_FUCOMPP, s->word, 0, NULL, NULL, NULL);

        /* fucompp compares st(0) against st(1) */
        if (xs_fp_kind(s, a) == XFP_X87 && xs_fp_kind(s, b) == XFP_X87) {
            fq_room(s, 2, a, b);
            fq_take(s, b, a == b, 1, 1);
            fq_take(s, a, 0, 0, 1);
            if (fq_arrange(s)) {
                fq_fxch(s, 1);
            }
            x.clobber = XR_BIT(XR_AX);
            xf_emit(&s->xf, &x);
            fq_pop_model(s);
            fq_pop_model(s);
            fq_tidy(s);
            return;
        }
        xs_fp_push(s, b);
        xs_fp_push(s, a);
        x.clobber = XR_BIT(XR_AX);
        xf_emit(&s->xf, &x);
    }
}

/** setcc @p cc into a new byte register, returned. */
static xop_t setcc_byte(xsel_t *s, xcc_t cc) {
    xop_t t = xop_reg(xf_new_vreg(&s->xf), 1);
    xinst_t x = xi_make(XI_SETCC, 1, 1, &t, NULL, NULL);

    x.cc = (unsigned char)cc;
    xf_emit(&s->xf, &x);
    return t;
}

/**
 * The truth of floating compare @p k of @p a and @p b into integer register @p d: false when unordered but for
 * !=.  > and >= are `above` of a against b, < and <= the same of b against a; == also wants no parity, != takes it.
 */
static void fcmp_to(xsel_t *s, cc_cmp_kind_t k, int a, int b, xop_t d) {
    xop_t t;

    switch (k) {
    case CC_CMP_GT:
    case CC_CMP_GE:
        fcompare(s, a, b);
        t = setcc_byte(s, k == CC_CMP_GT ? XC_A : XC_AE);
        break;
    case CC_CMP_LT:
    case CC_CMP_LE:
        fcompare(s, b, a);
        t = setcc_byte(s, k == CC_CMP_LT ? XC_A : XC_AE);
        break;
    default: {
        xop_t p;

        fcompare(s, a, b);
        t = setcc_byte(s, k == CC_CMP_EQ ? XC_E : XC_NE);
        p = setcc_byte(s, k == CC_CMP_EQ ? XC_NP : XC_P);
        xs_emit(s, k == CC_CMP_EQ ? XI_AND : XI_OR, 1, 2, &p, &t, NULL);
        break;
    }
    }
    d.size = 4;
    xs_emit(s, XI_MOVZX, s->word, 2, &t, &d, NULL);
}

/** A label of the selector's own, placed here. */
static void place_label(xsel_t *s, int label) {
    xinst_t x = xi_make(XI_LABEL, s->word, 0, NULL, NULL, NULL);

    x.label = label;
    xf_emit(&s->xf, &x);
}

/** A jump to @p label, on @p cc, or always for XC_COUNT. */
static void jump_to(xsel_t *s, int label, xcc_t cc) {
    xinst_t x = xi_make(cc == XC_COUNT ? XI_JMP : XI_JCC, s->word, 0, NULL, NULL, NULL);

    x.cc = (unsigned char)(cc == XC_COUNT ? 0 : cc);
    x.label = label;
    xf_emit(&s->xf, &x);
}

/**
 * Integer to floating.  SSE: cvtsi2sd of the value extended to a word, an unsigned 64-bit one of half of it,
 * rounding kept in the low bit, doubled when its top bit is set.  x87: fild of it in memory, extended to 64 bits
 * (an unsigned one with 2^64 added when its top bit is set).
 */
static void sel_i2f(xsel_t *s, const cc_ssa_instr_t *in) {
    int w = s->word;
    long bytes = xs_is_pair(s, in->lhs) ? 8 : value_size_bytes(s->f, in->lhs, w);
    int uns = in->is_unsigned;

    if (bytes > 8) {
        bytes = 8;
    }
    if (xs_fp_kind(s, in->dst) == XFP_SSE && w == 8) {
        xop_t r = xs_reg_word(s, in->lhs, 0);
        xop_t d = xs_fp_reg(s, in->dst);

        if (bytes < 8) {
            /* extended to the word, as the value is not held so */
            xop_t e = xs_temp(s);
            xop_t a = r;

            a.size = (unsigned char)bytes;
            e.size = (unsigned char)(uns ? 4 : 8);
            xs_emit(s, uns ? XI_MOVZX : XI_MOVSX, 8, 2, &a, &e, NULL);
            e.size = 8;
            xs_emit(s, XI_CVTSI2SD, 8, 2, &e, &d, NULL);
            return;
        }
        if (!uns) {
            xs_emit(s, XI_CVTSI2SD, 8, 2, &r, &d, NULL);
            return;
        }
        {
            int big = xf_new_label(&s->xf);
            int done = xf_new_label(&s->xf);
            xop_t t = xs_temp(s);
            xop_t u = xs_temp(s);
            xop_t one = xop_imm(1, 1);
            xop_t one8 = xop_imm(1, 8);

            xs_emit(s, XI_TEST, 8, 2, &r, &r, NULL);
            jump_to(s, big, XC_S);
            xs_emit(s, XI_CVTSI2SD, 8, 2, &r, &d, NULL);
            jump_to(s, done, XC_COUNT);
            place_label(s, big);
            xs_emit(s, XI_MOV, 8, 2, &r, &t, NULL);
            xs_emit(s, XI_SHR, 8, 2, &one, &t, NULL);
            xs_emit(s, XI_MOV, 8, 2, &r, &u, NULL);
            xs_emit(s, XI_AND, 8, 2, &one8, &u, NULL);
            xs_emit(s, XI_OR, 8, 2, &u, &t, NULL);
            xs_emit(s, XI_CVTSI2SD, 8, 2, &t, &d, NULL);
            xs_emit(s, XI_FADD, 8, 2, &d, &d, NULL);
            place_label(s, done);
        }
        return;
    }
    {
        /* through memory: 64 bits of it, sign- or zero-extended */
        xop_t m = scratch(s, 8);

        if (w == 8) {
            xop_t r = xs_reg_word(s, in->lhs, 0);
            xop_t e = xs_temp(s);
            xop_t a = r;

            if (bytes < 8) {
                a.size = (unsigned char)bytes;
                e.size = (unsigned char)(uns ? 4 : 8);
                xs_emit(s, uns ? XI_MOVZX : XI_MOVSX, 8, 2, &a, &e, NULL);
                e.size = 8;
            } else {
                xs_emit(s, XI_MOV, 8, 2, &r, &e, NULL);
            }
            m.size = 8;
            xs_emit(s, XI_MOV, 8, 2, &e, &m, NULL);
        } else {
            xop_t lo = xs_reg_word(s, in->lhs, 0);
            xop_t m1 = m;
            xop_t lo4 = xs_temp(s);

            m.size = 4;
            m1.disp += 4;
            m1.size = 4;
            if (bytes < 4) {
                xop_t a = lo;

                if (bytes == 1) {
                    /* from a byte register: a copy in one */
                    xop_t c = xs_temp(s);

                    xs_emit(s, XI_MOV, 4, 2, &lo, &c, NULL);
                    a = c;
                }
                a.size = (unsigned char)bytes;
                xs_emit(s, uns ? XI_MOVZX : XI_MOVSX, 4, 2, &a, &lo4, NULL);
            } else {
                xs_emit(s, XI_MOV, 4, 2, &lo, &lo4, NULL);
            }
            xs_emit(s, XI_MOV, 4, 2, &lo4, &m, NULL);
            if (bytes == 8) {
                xop_t hi = xs_src_word(s, in->lhs, 1);

                xs_emit(s, XI_MOV, 4, 2, &hi, &m1, NULL);
            } else if (uns) {
                xop_t z = xop_imm(0, 4);

                xs_emit(s, XI_MOV, 4, 2, &z, &m1, NULL);
            } else {
                /* the sign word */
                xop_t t = xs_temp(s);
                xop_t k31 = xop_imm(31, 1);

                xs_emit(s, XI_MOV, 4, 2, &lo4, &t, NULL);
                xs_emit(s, XI_SAR, 4, 2, &k31, &t, NULL);
                xs_emit(s, XI_MOV, 4, 2, &t, &m1, NULL);
            }
            m.size = 8;
        }
        if (uns && bytes == 8) {
            xop_t k = scratch_cw(s);
            xinst_t x;

            k.size = 4;
            x = xi_make(XI_FILDU64, w, 2, &m, &k, NULL);
            x.label = xf_new_label(&s->xf);
            xf_emit(&s->xf, &x);
        } else {
            emit1(s, XI_FILD, &m);
        }
        xs_fp_pop(s, in->dst);
    }
}

/**
 * Floating to integer, truncated.  SSE: cvttsd2si to 64 bits, which an unsigned 32-bit result's low word is; an
 * unsigned 64-bit one from 2^63 up converted less 2^63, its top bit set back.  x87: fistp of 64 bits under a
 * truncating control word (XI_FTOI), as i386.c does.
 */
static void sel_f2i(xsel_t *s, const cc_ssa_instr_t *in) {
    int w = s->word;
    long db = xs_is_pair(s, in->dst) ? 8 : value_size_bytes(s->f, in->dst, w);
    int uns = in->is_unsigned || cc_ssa_value_is_unsigned(s->f, in->dst);

    if (xs_fp_kind(s, in->lhs) == XFP_SSE && w == 8) {
        xop_t x = xs_fp_reg(s, in->lhs);
        xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), 8);

        if (db < 8 || !uns) {
            xs_emit(s, XI_CVTTSD2SI, 8, 2, &x, &d, NULL);
            return;
        }
        {
            int big = xf_new_label(&s->xf);
            int done = xf_new_label(&s->xf);
            xop_t g = xs_temp(s);
            xop_t kd = xop_reg(xf_new_xvreg(&s->xf), 8);
            xop_t t = xop_reg(xf_new_xvreg(&s->xf), 8);
            xop_t two63 = xop_imm(0x43e0000000000000LL, 8);
            xop_t top = xop_imm((long long)(1ULL << 63), 8);
            xop_t h = xs_temp(s);

            xs_emit(s, XI_MOV, 8, 2, &two63, &g, NULL);
            xs_emit(s, XI_MOV, 8, 2, &g, &kd, NULL);
            xs_emit(s, XI_UCOMI, 8, 2, &kd, &x, NULL);
            jump_to(s, big, XC_AE);
            xs_emit(s, XI_CVTTSD2SI, 8, 2, &x, &d, NULL);
            jump_to(s, done, XC_COUNT);
            place_label(s, big);
            xs_emit(s, XI_MOV, 8, 2, &x, &t, NULL);
            xs_emit(s, XI_FSUB, 8, 2, &kd, &t, NULL);
            xs_emit(s, XI_CVTTSD2SI, 8, 2, &t, &d, NULL);
            xs_emit(s, XI_MOV, 8, 2, &top, &h, NULL);
            xs_emit(s, XI_XOR, 8, 2, &h, &d, NULL);
            place_label(s, done);
        }
        return;
    }
    {
        xop_t out = scratch(s, 8);
        xop_t cw = scratch_cw(s);
        xinst_t x;
        int k;

        xs_fp_push(s, in->lhs);
        cw.size = 16;
        x = xi_make(XI_FTOI, w, 2, &out, &cw, NULL);
        x.cc = (unsigned char)(db >= 8 && uns);
        x.clobber = XR_BIT(XR_AX);
        xf_emit(&s->xf, &x);
        for (k = 0; k < (xs_is_pair(s, in->dst) ? 2 : 1); ++k) {
            xop_t a = out;
            xop_t d = xop_reg(xs_vreg_of(s, in->dst, k), w);

            a.disp += 4 * k;
            a.size = (unsigned char)w;
            xs_emit(s, XI_MOV, w, 2, &a, &d, NULL);
        }
    }
}

/** Round floating @p in->lhs to a float's precision (FROUND32) or a long double to a double's (FROUND64). */
static void sel_fround(xsel_t *s, const cc_ssa_instr_t *in) {
    int to = in->op == CC_SSA_FROUND32 ? 4 : 8;

    if (to == 4 && xs_fp_kind(s, in->lhs) == XFP_SSE && xs_fp_kind(s, in->dst) == XFP_SSE) {
        xop_t a = xs_fp_reg(s, in->lhs);
        xop_t d = xs_fp_reg(s, in->dst);
        xop_t t = xop_reg(xf_new_xvreg(&s->xf), 8);

        xs_emit(s, XI_CVTSD2SS, 8, 2, &a, &t, NULL);
        xs_emit(s, XI_CVTSS2SD, 8, 2, &t, &d, NULL);
        return;
    }
    if (to == 8 && xs_fp_kind(s, in->lhs) == XFP_SSE) {
        sel_fmov(s, in); /* a double already */
        return;
    }
    if (xs_fp_kind(s, in->lhs) == XFP_X87 && xs_fp_kind(s, in->dst) == XFP_X87) {
        /* left unrounded on the stack until something reads it but a store of its size */
        xfq_t *q;

        fq_room(s, 1, in->lhs, in->lhs);
        fq_take(s, in->lhs, 0, 0, 0);
        q = &s->fq[0];
        if (q->r != 0 && q->r != to) {
            fq_round_top(s);
        }
        if (q->e > to) {
            q->r = (unsigned char)to;
        }
        fq_put(s, in->dst);
        fq_tidy(s);
        return;
    }
    {
        xop_t m = scratch(s, to);

        xs_fp_push(s, in->lhs);
        emit1(s, XI_FSTP, &m);
        xs_fp_from_mem(s, in->dst, m, to);
    }
}

/**
 * A _Float16 at memory @p m into floating value @p v (@p load), or @p v stored there as one: libgcc's
 * __extendhfdf2 and __truncdfhf2, as x86_64.c and i386.c call them.  On x86-64 the half and the double both
 * travel in %xmm0; on i386 the half is pushed and the double comes back in %st0, the double is pushed and the
 * half comes back in %xmm0.
 */
static void half(xsel_t *s, int v, xop_t m, int load) {
    xop_t x0 = xop_reg(XR_XMM0, 8);
    xop_t g = xs_temp(s);
    xop_t t = scratch(s, 8);
    int w = s->word;

    g.size = 4;
    m.size = 2;
    if (load) {
        xs_emit(s, XI_MOVZX, w, 2, &m, &g, NULL);
        if (w == 8) {
            xs_emit(s, XI_MOV, 4, 2, &g, &x0, NULL);
            s->t->libcall(s, "__extendhfdf2", XR_BIT(XR_XMM0), XR_BIT(XR_XMM0));
            xs_emit(s, XI_MOV, 8, 2, &x0, &t, NULL);
            xs_fp_from_mem(s, v, t, 8);
        } else {
            xop_t pad = xop_imm(-12, 4);
            xop_t pop = xop_imm(16, 4);

            xs_emit(s, XI_ADDSP, 4, 1, &pad, NULL, NULL);
            xs_emit(s, XI_PUSH, 4, 1, &g, NULL, NULL);
            s->t->libcall(s, "__extendhfdf2", 0, 0);
            xs_emit(s, XI_ADDSP, 4, 1, &pop, NULL, NULL);
            xs_fp_pop(s, v);
        }
        return;
    }
    if (w == 8) {
        xs_fp_to_mem(s, v, t, 8);
        xs_emit(s, XI_MOV, 8, 2, &t, &x0, NULL);
        s->t->libcall(s, "__truncdfhf2", XR_BIT(XR_XMM0), XR_BIT(XR_XMM0));
    } else {
        xop_t pad = xop_imm(-16, 4);
        xop_t pop = xop_imm(16, 4);
        xop_t at = xop_mem(XR_SP, 0, 8);

        xs_fp_push(s, v);
        xs_emit(s, XI_ADDSP, 4, 1, &pad, NULL, NULL);
        xs_emit(s, XI_FSTP, 4, 1, &at, NULL, NULL);
        s->t->libcall(s, "__truncdfhf2", 0, 0);
        xs_emit(s, XI_ADDSP, 4, 1, &pop, NULL, NULL);
    }
    xs_emit(s, XI_MOV, 4, 2, &x0, &g, NULL);
    g.size = 2;
    xs_emit(s, XI_MOV, 2, 2, &g, &m, NULL);
}

/** Whether instruction @p in reads or writes a floating value (its own operands, not a call's or an asm's). */
/** Load the float, double or long double (@p bytes 4, 8, 10 or 16) at memory @p m into x87 value @p v, on the stack. */
static void fq_load(xsel_t *s, int v, xop_t m, int bytes) {
    int k = fqm_find(s, v);

    if (k >= 0) {
        fqm_take(s, k); /* written anew */
    }
    if (bytes <= 8 && s->fq_left[v] == 1 && !s->fq_global[v] && !s->addr_taken[v]) {
        /* read once, later in the block: loaded there, or read there from memory by the operation */
        if (s->nfqm == (int)(sizeof(s->fqm) / sizeof(s->fqm[0]))) {
            fqm_load(s, 0);
        }
        fq_unname(s, v);
        m.size = (unsigned char)bytes;
        s->fqm[s->nfqm].v = v;
        s->fqm[s->nfqm].m = m;
        s->nfqm++;
        return;
    }
    fq_room(s, 1, v, v);
    m.size = (unsigned char)(bytes == 4 ? 4 : bytes == 8 ? 8 : 16);
    emit1(s, XI_FLD, &m);
    fq_push_model(s, XFQ_RAW, bytes == 4 ? 4 : bytes == 8 ? 8 : 10, 0);
    fq_put(s, v);
    fq_tidy(s);
}

/** Store x87 value @p v at memory @p m as a float, a double or a long double (@p bytes 4, 8, 10 or 16). */
static void fq_store(xsel_t *s, int v, xop_t m, int bytes) {
    xfq_t *q;

    m.size = (unsigned char)(bytes == 4 ? 4 : bytes == 8 ? 8 : 16);
    fq_room(s, 1, v, v);
    fq_take(s, v, 0, 0, 0);
    q = &s->fq[0];
    if (q->r != 0 && q->r != m.size) {
        /* (the store's own rounding is the one it wants only at its size) */
        fq_round_top(s);
    }
    emit1(s, XI_FSTP, &m);
    fq_pop_model(s);
    fq_tidy(s);
}

static int touches_fp(const xsel_t *s, const cc_ssa_instr_t *in) {
    return xs_fp_kind(s, in->dst) != XFP_NONE || xs_fp_kind(s, in->lhs) != XFP_NONE ||
           xs_fp_kind(s, in->rhs) != XFP_NONE;
}

int xs_fp_select(xsel_t *s, size_t j) {
    const cc_ssa_instr_t *in = &s->f->instrs[j];

    switch (in->op) {
    case CC_SSA_CONST:
        if (xs_fp_kind(s, in->dst) == XFP_NONE) {
            return 0;
        }
        sel_fconst(s, in);
        return 1;
    case CC_SSA_MOV:
        if (!touches_fp(s, in)) {
            return 0;
        }
        sel_fmov(s, in);
        return 1;
    case CC_SSA_LOAD:
        if (xs_fp_kind(s, in->dst) == XFP_NONE) {
            return 0;
        }
        if (in->imm == 2) {
            half(s, in->dst, xs_mem_at(s, in->lhs, 0, 2), 1);
            return 1;
        }
        if (in->imm != 4 && in->imm != 8 && in->imm < 10) {
            s->unsupported = __LINE__;
            s->unsupported_file = __FILE__;
            return 1;
        }
        if (xs_fp_kind(s, in->dst) == XFP_X87) {
            fq_load(s, in->dst, xs_mem_at(s, in->lhs, 0, 8), (int)in->imm);
            return 1;
        }
        xs_fp_from_mem(s, in->dst, xs_mem_at(s, in->lhs, 0, 8), (int)in->imm);
        return 1;
    case CC_SSA_STORE:
        if (xs_fp_kind(s, in->rhs) == XFP_NONE) {
            return 0;
        }
        if (in->imm == 2) {
            half(s, in->rhs, xs_mem_at(s, in->lhs, 0, 2), 0);
            return 1;
        }
        if (in->imm != 4 && in->imm != 8 && in->imm < 10) {
            s->unsupported = __LINE__;
            s->unsupported_file = __FILE__;
            return 1;
        }
        if (xs_fp_kind(s, in->rhs) == XFP_X87) {
            fq_store(s, in->rhs, xs_mem_at(s, in->lhs, 0, 8), (int)in->imm);
            return 1;
        }
        xs_fp_to_mem(s, in->rhs, xs_mem_at(s, in->lhs, 0, 8), (int)in->imm);
        return 1;
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_DIV:
        if (xs_fp_kind(s, in->dst) == XFP_NONE) {
            return 0;
        }
        if (in->lhs < 0 || in->rhs < 0 || xs_fp_kind(s, in->lhs) == XFP_NONE || xs_fp_kind(s, in->rhs) == XFP_NONE) {
            s->unsupported = __LINE__;
            s->unsupported_file = __FILE__;
            return 1;
        }
        sel_fbinop(s, in);
        return 1;
    case CC_SSA_CMP:
        if (xs_fp_kind(s, in->lhs) == XFP_NONE && xs_fp_kind(s, in->rhs) == XFP_NONE) {
            return 0;
        }
        if (xs_fp_kind(s, in->lhs) == XFP_NONE || xs_fp_kind(s, in->rhs) == XFP_NONE) {
            s->unsupported = __LINE__;
            s->unsupported_file = __FILE__;
            return 1;
        }
        fcmp_to(s, in->cmp_kind, in->lhs, in->rhs, xop_reg(xs_vreg_of(s, in->dst, 0), s->word));
        return 1;
    case CC_SSA_BR_COND:
        if (xs_fp_kind(s, in->lhs) == XFP_NONE) {
            return 0;
        }
        {
            /* a floating condition: true unless it compares equal to 0.0 */
            xop_t t = xs_temp(s);
            xinst_t x;

            if (xs_fp_kind(s, in->lhs) == XFP_SSE) {
                xop_t r = xs_fp_reg(s, in->lhs);
                xop_t zr = xop_reg(xf_new_xvreg(&s->xf), 8);

                xs_emit(s, XI_XORPS, 8, 2, &zr, &zr, NULL);
                xs_emit(s, XI_UCOMI, 8, 2, &zr, &r, NULL);
            } else {
                xinst_t c = xi_make(XI_FUCOMPP, s->word, 0, NULL, NULL, NULL);
                xop_t z = scratch(s, 8);
                unsigned char zero[8];

                memset(zero, 0, sizeof(zero));
                store_bytes_const(s, z, zero, 8);
                emit1(s, XI_FLD, &z);
                xs_fp_push(s, in->lhs);
                c.clobber = XR_BIT(XR_AX);
                xf_emit(&s->xf, &c);
            }
            {
                xop_t a = setcc_byte(s, XC_NE);
                xop_t p = setcc_byte(s, XC_P);

                xs_emit(s, XI_OR, 1, 2, &p, &a, NULL);
                t.size = 4;
                xs_emit(s, XI_MOVZX, s->word, 2, &a, &t, NULL);
                t.size = (unsigned char)s->word;
            }
            xs_emit(s, XI_TEST, s->word, 2, &t, &t, NULL);
            x = xi_make(XI_JCC, s->word, 0, NULL, NULL, NULL);
            x.cc = XC_NE;
            x.label = in->true_label;
            xf_emit(&s->xf, &x);
            jump_to(s, in->false_label, XC_COUNT);
        }
        return 1;
    case CC_SSA_I2F:
        sel_i2f(s, in);
        return 1;
    case CC_SSA_F2I:
        sel_f2i(s, in);
        return 1;
    case CC_SSA_FROUND32:
    case CC_SSA_FROUND64:
        sel_fround(s, in);
        return 1;
    default:
        return 0;
    }
}
