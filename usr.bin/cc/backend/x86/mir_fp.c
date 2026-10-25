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
 * frame object of its own, 8 bytes for a double and 16 for a long double
 * in x87's 80-bit form, and each operation is a short x87 sequence that
 * pushes its operands from there and pops the result back: nothing stays
 * on the x87 stack from one IR instruction to the next, so the allocator,
 * which never sees it, and the calls between need not know of it.  A
 * vector register goes through a scratch slot where x87 code reads or
 * writes it.
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
        s->fscratch = xf_new_obj(&s->xf, XF_LOCAL, 16, 16, 0);
    }
    return xop_frame(s->fscratch, 0, bytes);
}

/** The second scratch object, for the x87 control word and its constants (16 bytes). */
static xop_t scratch_cw(xsel_t *s) {
    if (s->fcw < 0) {
        s->fcw = xf_new_obj(&s->xf, XF_LOCAL, 16, 16, 0);
    }
    return xop_frame(s->fcw, 0, 2);
}

/** Append instruction @p op of one memory operand @p m. */
static void emit1(xsel_t *s, xi_op_t op, const xop_t *m) {
    xs_emit(s, op, s->word, 1, m, NULL, NULL);
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

/** A floating constant: 0.0 as xorps, else its bits through a general register; a long double's 10 bytes. */
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
        store_bytes_const(s, xs_fp_home(s, in->dst), raw, 16);
        return;
    }
    memcpy(raw, &in->fimm, sizeof(in->fimm));
    if (xs_fp_kind(s, in->dst) == XFP_X87) {
        store_bytes_const(s, xs_fp_home(s, in->dst), raw, 8);
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
            xop_t m = scratch(s, 8);

            store_bytes_const(s, m, raw, 8);
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
