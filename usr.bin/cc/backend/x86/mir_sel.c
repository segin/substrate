/*
 * mir_sel.c: instruction selection from legal IR into machine IR
 * (cc_x86_mir.h), what i386 and x86-64 share; mir_i386.c and
 * mir_x86_64.c are the targets' own parts (xsel_target_t): parameters,
 * calls, returns, va_start, and which functions they take.
 *
 * Each IR word is a virtual register (an i386 64-bit integer two, its low
 * and high words); the operands of idiv, mul and shifts are in the
 * physical registers the instructions name.  A constant defined once is an
 * immediate wherever it is read (on x86-64, one that fits 32 bits); so is a
 * global's or a string's address on i386, which x86-64 makes off %rip; a
 * stack object's address is folded into the loads and stores that use it,
 * and so is a pointer plus a constant (a structure's field), when the
 * pointer cannot change in between.  A shift left and back right by the
 * same count is the extension it makes.  A compare that only a branch
 * right after it reads is that branch's flags.  A value whose address is
 * taken lives in its frame object.  Inline asm reads its operands from
 * frame slots and writes its outputs there (emit_inline_asm()).
 *
 * linscan.c then gives out the target's registers; the frame pointer is
 * kept.  The frame is laid out afterwards: the callee-saved registers the
 * function uses pushed under the frame pointer, then the frame objects,
 * the busiest nearest.
 *
 * A function the path does not handle -- floating point, alloca,
 * computed goto and what each target turns down -- returns 1 before
 * anything is written, and the target's emitter emits it its own way.
 */

#include "cc_x86_mir.h"

#include "cc_options.h"
#include "cc_target.h"
#include "cc_x86.h"

#include <stdlib.h>
#include <string.h>

/** Give up on the function: something it does is not handled here. */
#define NOPE(s) XS_NOPE(s)

/** Whether @p k fits a sign-extended 32-bit immediate or displacement. */
static int fits32(long long k) {
    return k >= -2147483647LL - 1 && k <= 2147483647LL;
}

int xs_is_pair(const xsel_t *s, int v) {
    return s->word == 4 && v >= 0 && s->f->value_ty[v].kind == CC_TY_INT && s->f->value_ty[v].size == 8 &&
           !s->is_addr[v];
}

int xs_const_of(const xsel_t *s, int v, long long *k) {
    const cc_ssa_instr_t *d;

    if (v < 0 || s->def_at[v] < 0 || s->addr_taken[v]) {
        return 0;
    }
    d = &s->f->instrs[s->def_at[v]];
    if (d->op != CC_SSA_CONST || cc_ssa_value_class(s->f, v) != CC_VAL_I64) {
        return 0;
    }
    *k = d->imm;
    return 1;
}

/** The label of the string literal instruction @p j makes. */
static void str_label(const xsel_t *s, size_t j, char *buf, size_t size) {
    snprintf(buf, size, ".L__cc_str_%zu_%zu", s->fn_index, j);
}

/** Names made for operands to point at -- string literals' labels, GOT references -- kept until printed. */
typedef struct {
    char **names;
    size_t n;
    size_t cap;
} strpool_t;

static strpool_t sel_names;

/** A copy of @p a followed by @p b that lives until the function is printed, or NULL when out of memory. */
static const char *keep_name(const char *a, const char *b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char *c = (char *)malloc(la + lb + 1);

    if (c == NULL) {
        return NULL;
    }
    memcpy(c, a, la);
    memcpy(c + la, b, lb + 1);
    if (sel_names.n == sel_names.cap) {
        size_t ncap = sel_names.cap > 0 ? sel_names.cap * 2 : 16;
        char **nn = (char **)realloc(sel_names.names, ncap * sizeof(*nn));

        if (nn == NULL) {
            free(c);
            return NULL;
        }
        sel_names.names = nn;
        sel_names.cap = ncap;
    }
    sel_names.names[sel_names.n++] = c;
    return c;
}

/** Free the names keep_name() made. */
static void free_names(void) {
    size_t i;

    for (i = 0; i < sel_names.n; ++i) {
        free(sel_names.names[i]);
    }
    sel_names.n = 0;
}

int xs_sym_of(xsel_t *s, int v, const char **sym) {
    const cc_ssa_instr_t *d;

    if (v < 0 || s->def_at[v] < 0 || s->addr_taken[v]) {
        return 0;
    }
    d = &s->f->instrs[s->def_at[v]];
    if (d->op == CC_SSA_GADDR && d->sym != NULL) {
        *sym = d->sym;
        return 1;
    }
    if (d->op == CC_SSA_STR) {
        char buf[96];

        str_label(s, (size_t)s->def_at[v], buf, sizeof(buf));
        *sym = keep_name(buf, "");
        if (*sym == NULL) {
            s->xf.oom = 1;
            return 0;
        }
        return 1;
    }
    return 0;
}

int xs_vreg_of(xsel_t *s, int v, int part) {
    int *slot = part ? &s->hi[v] : &s->lo[v];

    if (*slot == XR_NONE) {
        *slot = xf_new_vreg(&s->xf);
        if (*slot != XR_NONE && s->addr_taken[v]) {
            xvreg_t *r = xf_vreg(&s->xf, *slot);

            /* its address is taken: it lives in its frame object, a word each */
            r->memory = 1;
            r->home_obj = s->obj[v];
            r->home_off = part ? 4 : 0;
        }
    }
    return *slot;
}

void xs_emit(xsel_t *s, xi_op_t op, int w, int n, const xop_t *a, const xop_t *b, const xop_t *c) {
    xinst_t in = xi_make(op, w, n, a, b, c);

    xf_emit(&s->xf, &in);
}

xop_t xs_temp(xsel_t *s) {
    return xop_reg(xf_new_vreg(&s->xf), s->word);
}

/** The address of symbol @p sym in a new register: off %rip, or read from the GOT. */
static xop_t sym_reg(xsel_t *s, const char *sym) {
    xop_t t = xs_temp(s);
    xop_t m = xop_mem(XR_NONE, 0, s->word);

    if (s->t->sym_got != NULL && s->t->sym_got(s, sym)) {
        m.sym = keep_name(sym, "@GOTPCREL");
        if (m.sym == NULL) {
            s->xf.oom = 1;
            return t;
        }
        xs_emit(s, XI_MOV, s->word, 2, &m, &t, NULL);
    } else {
        m.sym = sym;
        xs_emit(s, XI_LEA, s->word, 2, &m, &t, NULL);
    }
    return t;
}

xop_t xs_src_word(xsel_t *s, int v, int part) {
    long long k;
    const char *sym;
    int w = s->word;

    if (xs_const_of(s, v, &k)) {
        if (w == 4) {
            return xop_imm(part ? (long long)(int)(k >> 32) : (long long)(int)k, 4);
        }
        if (fits32(k)) {
            return xop_imm(k, 8);
        }
        {
            /* movabsq */
            xop_t t = xs_temp(s);
            xop_t i = xop_imm(k, 8);

            xs_emit(s, XI_MOV, 8, 2, &i, &t, NULL);
            return t;
        }
    }
    if (xs_sym_of(s, v, &sym)) {
        if (part) {
            return xop_imm(0, w);
        }
        return s->t->sym_imm ? xop_sym(sym, 0, w) : sym_reg(s, sym);
    }
    if (!part && s->frame_val[v] >= 0) {
        xop_t t = xs_temp(s);
        xop_t m = xop_frame(s->frame_val[v], 0, w);

        xs_emit(s, XI_LEA, w, 2, &m, &t, NULL);
        return t;
    }
    return xop_reg(xs_vreg_of(s, v, part), w);
}

xop_t xs_reg_word(xsel_t *s, int v, int part) {
    xop_t o = xs_src_word(s, v, part);

    if (o.kind == XO_IMM) {
        xop_t t = xs_temp(s);

        xs_emit(s, XI_MOV, s->word, 2, &o, &t, NULL);
        return t;
    }
    return o;
}

/** Whether IR value @p v keeps its value from where it is written to every later read: it is written once. */
static int stable(const xsel_t *s, int v) {
    return v >= 0 && s->ndefs[v] == 1 && s->def_at[v] >= 0;
}

/** xs_mem_at(), @p depth additions folded so far. */
static xop_t mem_at(xsel_t *s, int v, long long disp, int size, int depth) {
    long long k;
    const char *sym;
    xop_t o;

    if (s->frame_val[v] >= 0) {
        return xop_frame(s->frame_val[v], disp, size);
    }
    if (xs_sym_of(s, v, &sym)) {
        if (s->t->sym_got != NULL && s->t->sym_got(s, sym)) {
            xop_t r = sym_reg(s, sym);

            return xop_mem(r.reg, disp, size);
        }
        o = xop_mem(XR_NONE, disp, size);
        o.sym = sym;
        return o;
    }
    if (xs_const_of(s, v, &k)) {
        return xop_mem(XR_NONE, (s->word == 4 ? (long long)(int)k : k) + disp, size);
    }
    if (depth < 8 && stable(s, v)) {
        const cc_ssa_instr_t *d = &s->f->instrs[s->def_at[v]];

        if (d->op == CC_SSA_ADD && d->lhs >= 0 && d->rhs >= 0 && !xs_is_pair(s, v)) {
            if (xs_const_of(s, d->rhs, &k) && (stable(s, d->lhs) || s->frame_val[d->lhs] >= 0)) {
                return mem_at(s, d->lhs, disp + (s->word == 4 ? (long long)(int)k : k), size, depth + 1);
            }
            if (xs_const_of(s, d->lhs, &k) && (stable(s, d->rhs) || s->frame_val[d->rhs] >= 0)) {
                return mem_at(s, d->rhs, disp + (s->word == 4 ? (long long)(int)k : k), size, depth + 1);
            }
            if (stable(s, d->lhs) && stable(s, d->rhs) && s->frame_val[d->lhs] < 0 && s->frame_val[d->rhs] < 0 &&
                !xs_const_of(s, d->lhs, &k) && !xs_const_of(s, d->rhs, &k) && !xs_sym_of(s, d->lhs, &sym) &&
                !xs_sym_of(s, d->rhs, &sym)) {
                o = xop_mem(xs_vreg_of(s, d->lhs, 0), disp, size);
                o.index = xs_vreg_of(s, d->rhs, 0);
                return o;
            }
        }
        if (d->op == CC_SSA_SUB && d->lhs >= 0 && d->rhs >= 0 && !xs_is_pair(s, v) && xs_const_of(s, d->rhs, &k) &&
            (stable(s, d->lhs) || s->frame_val[d->lhs] >= 0)) {
            return mem_at(s, d->lhs, disp - (s->word == 4 ? (long long)(int)k : k), size, depth + 1);
        }
    }
    return xop_mem(xs_vreg_of(s, v, 0), disp, size);
}

xop_t xs_mem_at(xsel_t *s, int v, long long disp, int size) {
    xop_t o = mem_at(s, v, disp, size, 0);

    if (!fits32(o.disp)) {
        /* a displacement x86-64 cannot encode: the address made in a register */
        xop_t r = xs_reg_word(s, v, 0);

        o = xop_mem(r.reg, disp, size);
        if (!fits32(disp)) {
            s->unsupported = __LINE__;
            s->unsupported_file = __FILE__;
        }
    }
    return o;
}

void xs_move_word_to(xsel_t *s, int v, int part, const xop_t *d) {
    if (!part && s->frame_val[v] >= 0 && d->kind == XO_REG) {
        xop_t m = xop_frame(s->frame_val[v], 0, s->word);

        xs_emit(s, XI_LEA, s->word, 2, &m, d, NULL);
        return;
    }
    {
        xop_t o = xs_src_word(s, v, part);

        xs_emit(s, XI_MOV, s->word, 2, &o, d, NULL);
    }
}

/** The condition of an integer compare. */
static xcc_t cmp_cc(cc_cmp_kind_t k, int is_unsigned) {
    switch (k) {
    case CC_CMP_EQ:
        return XC_E;
    case CC_CMP_NE:
        return XC_NE;
    case CC_CMP_LT:
        return is_unsigned ? XC_B : XC_L;
    case CC_CMP_LE:
        return is_unsigned ? XC_BE : XC_LE;
    case CC_CMP_GT:
        return is_unsigned ? XC_A : XC_G;
    default:
        return is_unsigned ? XC_AE : XC_GE;
    }
}

int xs_label_follows(const xsel_t *s, size_t j, int label) {
    size_t k;

    for (k = j + 1; k < s->f->instr_count && s->f->instrs[k].op == CC_SSA_LABEL; ++k) {
        if (s->f->instrs[k].label == label) {
            return 1;
        }
    }
    return 0;
}

/** Jump to @p label unless it comes next. */
static void jump(xsel_t *s, size_t j, int label) {
    if (!xs_label_follows(s, j, label)) {
        xinst_t in = xi_make(XI_JMP, s->word, 0, NULL, NULL, NULL);

        in.label = label;
        xf_emit(&s->xf, &in);
    }
}

/** Branch on the flags: to @p t when @p cc holds, else to @p fl; the one that comes next is fallen into. */
static void branch(xsel_t *s, size_t j, xcc_t cc, int t, int fl) {
    xinst_t in = xi_make(XI_JCC, s->word, 0, NULL, NULL, NULL);

    if (t == fl) {
        jump(s, j, t);
        return;
    }
    if (xs_label_follows(s, j, t)) {
        in.cc = (unsigned char)xcc_negate(cc);
        in.label = fl;
        xf_emit(&s->xf, &in);
        return;
    }
    in.cc = (unsigned char)cc;
    in.label = t;
    xf_emit(&s->xf, &in);
    jump(s, j, fl);
}

/** Whether condition @p cc holds of constants @p x and @p y, as words of @p w bytes. */
static int cc_holds(xcc_t cc, long long x, long long y, int w) {
    long long sx = w == 4 ? (long long)(int)x : x;
    long long sy = w == 4 ? (long long)(int)y : y;
    unsigned long long ux = w == 4 ? (unsigned long long)(unsigned)x : (unsigned long long)x;
    unsigned long long uy = w == 4 ? (unsigned long long)(unsigned)y : (unsigned long long)y;

    switch (cc) {
    case XC_E:
        return ux == uy;
    case XC_NE:
        return ux != uy;
    case XC_L:
        return sx < sy;
    case XC_LE:
        return sx <= sy;
    case XC_G:
        return sx > sy;
    case XC_GE:
        return sx >= sy;
    case XC_B:
        return ux < uy;
    case XC_BE:
        return ux <= uy;
    case XC_A:
        return ux > uy;
    default:
        return ux >= uy;
    }
}

/**
 * Set the flags for compare @p in (lhs against rhs), its operands swapped when the left one is a constant;
 * the condition to test them by goes to *@p cc.  0, or 1 when both are constants and *@p known is the result.
 */
static int compare(xsel_t *s, const cc_ssa_instr_t *in, xcc_t *cc, int *known) {
    xop_t a = xs_src_word(s, in->lhs, 0);
    xop_t b = xs_src_word(s, in->rhs, 0);
    int w = s->word;

    *cc = cmp_cc(in->cmp_kind, in->is_unsigned);
    if (a.kind == XO_IMM && b.kind == XO_IMM) {
        if (a.sym == NULL && b.sym == NULL) {
            *known = cc_holds(*cc, a.disp, b.disp, w);
            return 1;
        }
        a = xs_reg_word(s, in->lhs, 0);
    }
    if (a.kind == XO_IMM) {
        xop_t t = a;

        a = b;
        b = t;
        *cc = xcc_swap(*cc);
    }
    if (b.kind == XO_IMM && b.sym == NULL && b.disp == 0 && a.kind == XO_REG) {
        /* against 0: test sets the flags every condition reads as cmp $0 would */
        xs_emit(s, XI_TEST, w, 2, &a, &a, NULL);
    } else {
        xs_emit(s, XI_CMP, w, 2, &b, &a, NULL);
    }
    return 0;
}

/**
 * Whether shift @p in is the right half of an extension -- a shift left by the same constant count, which only it
 * reads -- of the low 1, 2 or 4 bytes of a value: then that value goes to *@p src and the byte count is returned;
 * else 0.  Legalization writes every integer conversion that way.
 */
static int ext_of(const xsel_t *s, const cc_ssa_instr_t *in, int *src) {
    const cc_ssa_instr_t *d;
    long long k;
    long long k2;
    int bytes;

    if ((in->op != CC_SSA_SHR) || in->lhs < 0 || !stable(s, in->lhs) || s->nuses[in->lhs] != 1 ||
        xs_is_pair(s, in->dst)) {
        return 0;
    }
    if (in->rhs < 0) {
        k = in->imm;
    } else if (!xs_const_of(s, in->rhs, &k)) {
        return 0;
    }
    d = &s->f->instrs[s->def_at[in->lhs]];
    if (d->op != CC_SSA_SHL || d->lhs < 0 || xs_is_pair(s, d->lhs)) {
        return 0;
    }
    if (d->rhs < 0) {
        k2 = d->imm;
    } else if (!xs_const_of(s, d->rhs, &k2)) {
        return 0;
    }
    if (k != k2 || k <= 0 || k >= 8 * s->word) {
        return 0;
    }
    bytes = (8 * s->word - (int)k) / 8;
    if ((8 * s->word - (int)k) % 8 != 0 || (bytes != 1 && bytes != 2 && bytes != 4) || bytes >= s->word) {
        return 0;
    }
    *src = d->lhs;
    return bytes;
}

/**
 * Extend the low @p bytes bytes of IR value @p src into IR value @p dst, signed or not (@p sign): ext_of()'s pair of
 * shifts, or an and with 0xff, 0xffff or 0xffffffff.
 */
static void sel_ext(xsel_t *s, int dst, int src, int bytes, int sign) {
    xop_t d = xop_reg(xs_vreg_of(s, dst, 0), s->word);
    xop_t a;

    if ((sign ? s->sbits[src] : s->zbits[src]) <= 8 * bytes) {
        /* it is held so already */
        xs_move_word_to(s, src, 0, &d);
        return;
    }
    a = xs_reg_word(s, src, 0);

    if (bytes == 1 && s->word == 4) {
        /* from a byte register: a copy in one, so that the value itself may live anywhere */
        xop_t t = xs_temp(s);

        xs_emit(s, XI_MOV, 4, 2, &a, &t, NULL);
        a = t;
    }
    a.size = (unsigned char)bytes;
    /* zero-extended into the 32-bit register, which clears the rest of a 64-bit one */
    d.size = (unsigned char)(sign ? s->word : 4);
    xs_emit(s, sign ? XI_MOVSX : XI_MOVZX, s->word, 2, &a, &d, NULL);
}

/** The fewest bits @p k is the sign extension of, as a word of @p w bytes. */
static int const_sbits(long long k, int w) {
    long long v = w == 4 ? (long long)(int)k : k;
    int n;

    for (n = 1; n < 8 * w; ++n) {
        long long lo = -(1LL << (n - 1));
        long long hi = (1LL << (n - 1)) - 1;

        if (v >= lo && v <= hi) {
            return n;
        }
    }
    return 8 * w;
}

/** The fewest bits @p k is the zero extension of, as a word of @p w bytes. */
static int const_zbits(long long k, int w) {
    unsigned long long v = w == 4 ? (unsigned long long)(unsigned)k : (unsigned long long)k;
    int n = 0;

    while (n < 8 * w && (v >> n) != 0) {
        n++;
    }
    return n > 0 ? n : 1;
}

/** sbits, zbits of an integer of @p bytes bytes held extended as @p is_unsigned says, in a word of @p w bytes. */
static void held_bits(int bytes, int is_unsigned, int w, int *sb, int *zb) {
    int W = 8 * w;

    if (bytes <= 0 || 8 * bytes >= W) {
        *sb = W;
        *zb = W;
    } else if (is_unsigned) {
        *zb = 8 * bytes;
        *sb = 8 * bytes + 1;
    } else {
        *sb = 8 * bytes;
        *zb = W;
    }
}

/**
 * Work out, for each IR value, the bits its definitions leave it extended from (sbits, zbits): a load of a narrower
 * integer, an extension, a compare, a mask, a constant, a copy of one, and on x86-64 a parameter or a call's result
 * as they are taken.  Every definition must say so; anything else says nothing.  Over and over until it settles,
 * from the most a value could be, so that copies around a loop agree.
 */
static int compute_ext(xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    int W = 8 * s->word;
    int changed;
    int pass;
    size_t i;
    int v;

    s->sbits = (unsigned char *)malloc((size_t)(f->value_count > 0 ? f->value_count : 1));
    s->zbits = (unsigned char *)malloc((size_t)(f->value_count > 0 ? f->value_count : 1));
    if (s->sbits == NULL || s->zbits == NULL) {
        return -1;
    }
    for (v = 0; v < f->value_count; ++v) {
        /* defined: the least, raised by each definition; never defined: nothing known */
        s->sbits[v] = (unsigned char)(s->ndefs[v] > 0 ? 1 : W);
        s->zbits[v] = (unsigned char)(s->ndefs[v] > 0 ? 1 : W);
    }
    for (v = 0; v < f->value_count; ++v) {
        if (s->addr_taken[v] || cc_ssa_value_class(f, v) != CC_VAL_I64 || xs_is_pair(s, v)) {
            s->sbits[v] = (unsigned char)W;
            s->zbits[v] = (unsigned char)W;
        }
    }
    for (pass = 0; pass < 32; ++pass) {
        changed = 0;
        for (i = 0; i < f->instr_count; ++i) {
            const cc_ssa_instr_t *in = &f->instrs[i];
            int d = in->dst;
            int sb = W;
            int zb = W;
            long long k;
            int src;
            int bytes;

            if (in->op == CC_SSA_ASM) {
                size_t o;

                for (o = 0; o < in->asm_out_count; ++o) {
                    int ov = in->asm_out_values[o];

                    if (ov >= 0 && ov < f->value_count && (s->sbits[ov] != W || s->zbits[ov] != W)) {
                        s->sbits[ov] = (unsigned char)W;
                        s->zbits[ov] = (unsigned char)W;
                        changed = 1;
                    }
                }
                continue;
            }
            if (d < 0 || d >= f->value_count || (s->sbits[d] == W && s->zbits[d] == W)) {
                continue;
            }
            if ((in->op == CC_SSA_CALL || in->op == CC_SSA_CALLI) && in->rhs == d) {
                continue; /* it writes the structure, not the value */
            }
            switch (in->op) {
            case CC_SSA_CONST:
                sb = const_sbits(in->imm, s->word);
                zb = const_zbits(in->imm, s->word);
                break;
            case CC_SSA_LOAD:
                held_bits((int)in->imm, in->is_unsigned, s->word, &sb, &zb);
                break;
            case CC_SSA_CMP:
                sb = 2;
                zb = 1;
                break;
            case CC_SSA_MOV:
                if (in->lhs >= 0) {
                    sb = s->sbits[in->lhs];
                    zb = s->zbits[in->lhs];
                }
                break;
            case CC_SSA_AND:
                if (in->lhs >= 0 && in->rhs >= 0) {
                    zb = s->zbits[in->lhs] < s->zbits[in->rhs] ? s->zbits[in->lhs] : s->zbits[in->rhs];
                    if (xs_const_of(s, in->rhs, &k) && const_zbits(k, s->word) < zb) {
                        zb = const_zbits(k, s->word);
                    }
                    sb = zb < W ? zb + 1 : W;
                }
                break;
            case CC_SSA_SHR:
                bytes = ext_of(s, in, &src);
                if (bytes > 0) {
                    held_bits(bytes, in->is_unsigned, s->word, &sb, &zb);
                } else if (in->is_unsigned && (in->rhs < 0 || xs_const_of(s, in->rhs, &k))) {
                    if (in->rhs < 0) {
                        k = in->imm;
                    }
                    k &= W - 1;
                    zb = k > 0 ? W - (int)k : W;
                    sb = zb < W ? zb + 1 : W;
                }
                break;
            case CC_SSA_PARAM:
                if (s->t->extends_params) {
                    held_bits(in->imm > 0 ? (int)in->imm : s->word, in->is_unsigned, s->word, &sb, &zb);
                }
                break;
            case CC_SSA_CALL:
            case CC_SSA_CALLI:
                if (s->t->extends_params || in->imm == 1 || in->imm == 2) {
                    held_bits(in->imm > 0 ? (int)in->imm : s->word, in->is_unsigned, s->word, &sb, &zb);
                }
                break;
            default:
                break;
            }
            if (sb > s->sbits[d]) {
                s->sbits[d] = (unsigned char)sb;
                changed = 1;
            }
            if (zb > s->zbits[d]) {
                s->zbits[d] = (unsigned char)zb;
                changed = 1;
            }
        }
        if (!changed) {
            return 0;
        }
    }
    /* not settled: nothing known */
    for (v = 0; v < f->value_count; ++v) {
        s->sbits[v] = (unsigned char)W;
        s->zbits[v] = (unsigned char)W;
    }
    return 0;
}

/** A word binary operation: add, sub, and, or, xor, mul, shl, shr. */
static void sel_binop(xsel_t *s, const cc_ssa_instr_t *in) {
    int w = s->word;
    xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), w);
    xop_t a;
    xop_t b;
    xi_op_t op;
    int commutes = 0;

    switch (in->op) {
    case CC_SSA_ADD:
        op = XI_ADD;
        commutes = 1;
        break;
    case CC_SSA_SUB:
        op = XI_SUB;
        break;
    case CC_SSA_AND:
        op = XI_AND;
        commutes = 1;
        break;
    case CC_SSA_OR:
        op = XI_OR;
        commutes = 1;
        break;
    case CC_SSA_XOR:
        op = XI_XOR;
        commutes = 1;
        break;
    case CC_SSA_MUL:
        op = XI_IMUL;
        commutes = 1;
        break;
    case CC_SSA_SHL:
        op = XI_SHL;
        break;
    default:
        op = in->is_unsigned ? XI_SHR : XI_SAR;
        break;
    }
    if (op == XI_SHL || op == XI_SHR || op == XI_SAR) {
        long long k;
        int src;
        int bytes = ext_of(s, in, &src);

        if (bytes > 0) {
            sel_ext(s, in->dst, src, bytes, !in->is_unsigned);
            return;
        }
        if (in->rhs < 0 || xs_const_of(s, in->rhs, &k)) {
            xop_t c;

            if (in->rhs < 0) {
                k = in->imm;
            }
            c = xop_imm(k & (8 * w - 1), 1);
            if (in->dst != in->lhs) {
                xs_move_word_to(s, in->lhs, 0, &d);
            }
            xs_emit(s, op, w, 2, &c, &d, NULL);
            return;
        }
        /* the count in %cl */
        {
            xop_t cl = xop_reg(XR_CX, 1);
            xop_t ecx = xop_reg(XR_CX, w);
            xop_t t = d;

            if (in->dst == in->rhs && in->dst != in->lhs) {
                t = xs_temp(s);
            }
            if (in->dst != in->lhs) {
                xs_move_word_to(s, in->lhs, 0, &t);
            }
            b = xs_src_word(s, in->rhs, 0);
            xs_emit(s, XI_MOV, w, 2, &b, &ecx, NULL);
            xs_emit(s, op, w, 2, &cl, &t, NULL);
            if (t.reg != d.reg) {
                xs_emit(s, XI_MOV, w, 2, &t, &d, NULL);
            }
            return;
        }
    }
    if (op == XI_AND) {
        /* a mask of the low byte, half or (x86-64) word: a zero extension */
        long long m = 0;
        int other = xs_const_of(s, in->rhs, &m) ? in->lhs : xs_const_of(s, in->lhs, &m) ? in->rhs : -1;
        int bytes = m == 0xff ? 1 : m == 0xffff ? 2 : (w == 8 && m == 0xffffffffLL) ? 4 : 0;

        if (other >= 0 && bytes > 0 && !xs_const_of(s, other, &m)) {
            sel_ext(s, in->dst, other, bytes, 0);
            return;
        }
    }
    a = xs_src_word(s, in->lhs, 0);
    b = xs_src_word(s, in->rhs, 0);
    if (a.kind == XO_IMM && a.sym == NULL && b.kind == XO_IMM && b.sym == NULL) {
        /* two constants: the result, as a word */
        unsigned long long x = (unsigned long long)a.disp;
        unsigned long long y = (unsigned long long)b.disp;
        unsigned long long r;
        xop_t v;

        switch (op) {
        case XI_ADD:
            r = x + y;
            break;
        case XI_SUB:
            r = x - y;
            break;
        case XI_AND:
            r = x & y;
            break;
        case XI_OR:
            r = x | y;
            break;
        case XI_XOR:
            r = x ^ y;
            break;
        default:
            r = x * y;
            break;
        }
        v = xop_imm(w == 4 ? (long long)(int)(unsigned)r : (long long)r, w);
        xs_emit(s, XI_MOV, w, 2, &v, &d, NULL);
        return;
    }
    if (op == XI_IMUL) {
        if (a.kind == XO_IMM && a.sym == NULL) {
            xop_t t = a;

            a = b;
            b = t;
        }
        if (b.kind == XO_IMM && b.sym == NULL) {
            if (a.kind == XO_IMM) {
                /* a symbol's address times a constant */
                xop_t t = xs_temp(s);

                xs_emit(s, XI_MOV, w, 2, &a, &t, NULL);
                a = t;
            }
            xs_emit(s, XI_IMUL3, w, 3, &b, &a, &d);
            return;
        }
    }
    if (a.kind == XO_IMM && commutes && b.kind != XO_IMM) {
        xop_t t = a;

        a = b;
        b = t;
        if (in->dst == in->rhs) {
            /* now d is the left operand */
            xs_emit(s, op, w, 2, &b, &d, NULL);
            return;
        }
    }
    if (op == XI_IMUL && b.kind == XO_IMM) {
        b = xs_reg_word(s, in->rhs, 0);
    }
    if (in->dst == in->lhs) {
        xs_emit(s, op, w, 2, &b, &d, NULL);
    } else if (in->dst == in->rhs) {
        if (commutes) {
            xs_emit(s, op, w, 2, &a, &d, NULL);
        } else {
            xop_t t = xs_temp(s);

            xs_emit(s, XI_MOV, w, 2, &a, &t, NULL);
            xs_emit(s, op, w, 2, &b, &t, NULL);
            xs_emit(s, XI_MOV, w, 2, &t, &d, NULL);
        }
    } else {
        xs_emit(s, XI_MOV, w, 2, &a, &d, NULL);
        xs_emit(s, op, w, 2, &b, &d, NULL);
    }
}

/** A word division: the quotient of %edx:%eax (%rdx:%rax) by the divisor. */
static void sel_div(xsel_t *s, const cc_ssa_instr_t *in) {
    int w = s->word;
    xop_t eax = xop_reg(XR_AX, w);
    xop_t edx = xop_reg(XR_DX, 4);
    xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), w);
    xop_t b = xs_reg_word(s, in->rhs, 0);
    xinst_t x;

    xs_move_word_to(s, in->lhs, 0, &eax);
    if (in->is_unsigned) {
        /* (xorl clears all of %rdx) */
        xs_emit(s, XI_XOR, 4, 2, &edx, &edx, NULL);
    } else {
        x = xi_make(XI_CDQ, w, 0, NULL, NULL, NULL);
        x.imp_use = XR_BIT(XR_AX);
        x.imp_def = XR_BIT(XR_DX);
        xf_emit(&s->xf, &x);
    }
    x = xi_make(in->is_unsigned ? XI_DIV : XI_IDIV, w, 1, &b, NULL, NULL);
    x.imp_use = XR_BIT(XR_AX) | XR_BIT(XR_DX);
    x.imp_def = XR_BIT(XR_AX) | XR_BIT(XR_DX);
    xf_emit(&s->xf, &x);
    xs_emit(s, XI_MOV, w, 2, &eax, &d, NULL);
}

/** A load of @p in->imm bytes, extended as the result is held. */
static void sel_load(xsel_t *s, const cc_ssa_instr_t *in) {
    int size = (int)in->imm;
    int w = s->word;

    if (size == 8 && xs_is_pair(s, in->dst)) {
        xop_t m0 = xs_mem_at(s, in->lhs, 0, 4);
        xop_t m1 = xs_mem_at(s, in->lhs, 4, 4);
        xop_t d0 = xop_reg(xs_vreg_of(s, in->dst, 0), 4);
        xop_t d1 = xop_reg(xs_vreg_of(s, in->dst, 1), 4);

        xs_emit(s, XI_MOV, 4, 2, &m0, &d0, NULL);
        xs_emit(s, XI_MOV, 4, 2, &m1, &d1, NULL);
        return;
    }
    if (size != 1 && size != 2 && size != 4 && !(size == 8 && w == 8)) {
        NOPE(s);
    }
    {
        xop_t m = xs_mem_at(s, in->lhs, 0, size);
        xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), w);

        if (size == w) {
            xs_emit(s, XI_MOV, w, 2, &m, &d, NULL);
        } else if (in->is_unsigned) {
            /* zero-extended into the 32-bit register, which clears the rest of a 64-bit one */
            d.size = 4;
            xs_emit(s, XI_MOVZX, w, 2, &m, &d, NULL);
        } else {
            xs_emit(s, XI_MOVSX, w, 2, &m, &d, NULL);
        }
    }
}

/** A store of @p in->imm bytes of the value rhs at address lhs. */
static void sel_store(xsel_t *s, const cc_ssa_instr_t *in) {
    int size = (int)in->imm;
    int w = s->word;

    if (cc_ssa_value_class(s->f, in->rhs) == CC_VAL_F64) {
        NOPE(s);
    }
    if (size == 8 && w == 4) {
        int k;

        if (!xs_is_pair(s, in->rhs)) {
            NOPE(s);
        }
        for (k = 0; k < 2; ++k) {
            xop_t m = xs_mem_at(s, in->lhs, 4 * k, 4);
            xop_t v = xs_src_word(s, in->rhs, k);

            xs_emit(s, XI_MOV, 4, 2, &v, &m, NULL);
        }
        return;
    }
    if (size != 1 && size != 2 && size != 4 && !(size == 8 && w == 8)) {
        NOPE(s);
    }
    {
        xop_t m = xs_mem_at(s, in->lhs, 0, size);
        xop_t v = xs_src_word(s, in->rhs, 0);

        if (v.kind == XO_IMM) {
            if (size < 4 && v.sym != NULL) {
                v = xs_reg_word(s, in->rhs, 0);
            } else if (size == 1) {
                v.disp = (signed char)v.disp;
            } else if (size == 2) {
                v.disp = (short)v.disp;
            } else if (size == 4) {
                v.disp = (int)v.disp;
            }
        }
        if (v.kind == XO_REG && size == 1 && w == 4) {
            /* a byte register: a copy of the value in one, so that the value itself may live anywhere */
            xop_t t = xs_temp(s);

            xs_emit(s, XI_MOV, 4, 2, &v, &t, NULL);
            v = t;
        }
        v.size = (unsigned char)size;
        xs_emit(s, XI_MOV, size, 2, &v, &m, NULL);
    }
}

/** Append a binding of an asm operand; NULL when out of memory. */
static xasm_bind_t *add_bind(xsel_t *s) {
    if (s->nbind == s->bindcap) {
        int ncap = s->bindcap > 0 ? s->bindcap * 2 : 32;
        xasm_bind_t *n = (xasm_bind_t *)realloc(s->bind, (size_t)ncap * sizeof(*n));

        if (n == NULL) {
            s->xf.oom = 1;
            return NULL;
        }
        s->bind = n;
        s->bindcap = ncap;
    }
    memset(&s->bind[s->nbind], 0, sizeof(s->bind[0]));
    return &s->bind[s->nbind++];
}

/** The frame object of an inline asm's @p k-th operand, made the first time; -1 when out of memory. */
static int asm_slot(xsel_t *s, int k) {
    while (s->nasm_slot <= k) {
        int *n = (int *)realloc(s->asm_slot, (size_t)(s->nasm_slot + 1) * sizeof(int));
        int o;

        if (n == NULL) {
            s->xf.oom = 1;
            return -1;
        }
        s->asm_slot = n;
        o = xf_new_obj(&s->xf, XF_LOCAL, 8, s->word, 0);
        if (o < 0) {
            return -1;
        }
        s->asm_slot[s->nasm_slot++] = o;
    }
    return s->asm_slot[k];
}

/**
 * An inline asm: emit_inline_asm() reads its operands from frame slots and writes its outputs there, so each is
 * copied into a slot of its own first and back out after -- a value whose address is taken is in its frame object
 * already.  It may use the caller-saved registers, and saves what else it uses; but an asm goto's jump skips the
 * restores, so it keeps nothing in any register.
 */
static void sel_asm(xsel_t *s, size_t j, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    size_t total = in->asm_out_count + in->asm_in_count;
    int first = s->nbind;
    int w = s->word;
    xinst_t x;
    size_t i;
    int b;
    int part;

    if (in->asm_is_goto && in->asm_out_count > 0) {
        NOPE(s);
    }
    for (i = 0; i < total; ++i) {
        int is_out = i < in->asm_out_count;
        int raw = is_out ? in->asm_out_values[i] : in->asm_in_values[i - in->asm_out_count];
        const char *c = is_out ? (in->asm_out_constraints != NULL ? in->asm_out_constraints[i] : NULL)
                               : (in->asm_in_constraints != NULL ? in->asm_in_constraints[i - in->asm_out_count] : NULL);
        int indirect = CC_SSA_ASM_MEM_INDIRECT_P(raw);
        int v = indirect ? CC_SSA_ASM_MEM_INDIRECT_DECODE(raw) : raw;
        xasm_bind_t *bd = NULL;
        int need_in;

        if (v < 0 || v >= f->value_count) {
            continue;
        }
        if (cc_ssa_value_class(f, v) == CC_VAL_F64) {
            NOPE(s);
        }
        /* read: an input not written as an immediate, a `+` output, the address of a memory output; a 64-bit
         * output too, whose word the asm does not write keeps its value */
        need_in = !is_out || indirect || (c != NULL && strchr(c, '+') != NULL) || xs_is_pair(s, v);
        if (!is_out && x86_asm_input_is_immediate(f, in, i - in->asm_out_count, j)) {
            need_in = 0;
        }
        for (b = first; b < s->nbind; ++b) {
            if (s->bind[b].v == v) {
                bd = &s->bind[b];
            }
        }
        if (bd == NULL) {
            int o = s->addr_taken[v] ? s->obj[v] : asm_slot(s, s->nbind - first);

            bd = add_bind(s);
            if (bd == NULL || o < 0) {
                return;
            }
            bd->v = v;
            bd->obj = o;
        }
        if (s->addr_taken[v]) {
            continue; /* it lives there */
        }
        bd->in |= (unsigned char)need_in;
        bd->out |= (unsigned char)(is_out && !indirect);
    }
    for (b = first; b < s->nbind; ++b) {
        if (s->bind[b].in) {
            for (part = 0; part < (xs_is_pair(s, s->bind[b].v) ? 2 : 1); ++part) {
                xop_t m = xop_frame(s->bind[b].obj, 4 * part, w);

                xs_move_word_to(s, s->bind[b].v, part, &m);
            }
        }
    }
    x = xi_make(XI_ASM, w, 0, NULL, NULL, NULL);
    x.ir_index = (int)j;
    x.aux = first;
    x.clobber = s->t->caller_saved;
    if (in->asm_is_goto) {
        x.clobber = s->t->alloc->alloc_mask;
        x.targets = in->asm_goto_labels;
        x.ntargets = (int)in->asm_goto_count;
        s->has_goto = 1;
    }
    xf_emit(&s->xf, &x);
    for (b = first; b < s->nbind; ++b) {
        if (s->bind[b].out) {
            for (part = 0; part < (xs_is_pair(s, s->bind[b].v) ? 2 : 1); ++part) {
                xop_t m = xop_frame(s->bind[b].obj, 4 * part, w);
                xop_t d = xop_reg(xs_vreg_of(s, s->bind[b].v, part), w);

                xs_emit(s, XI_MOV, w, 2, &m, &d, NULL);
            }
        }
    }
    {
        xasm_bind_t *end = add_bind(s);

        if (end != NULL) {
            end->v = -1;
        }
    }
}

/** The most words a call of memcpy or memset of a constant size is copied or set in without the call; memmove,
 *  which reads all before it writes, half as many. */
#define XS_INLINE_MEM_WORDS 8

/**
 * A call of memcpy, memmove or memset of a constant size up to a few words, the moves themselves -- as gcc makes
 * the library's string functions, and as code with no library to call needs: Linux's vDSO zeroes and copies its
 * structures so, through the calls lowering makes for them.  Words, then 4, 2 and 1 bytes; a memmove reads every
 * piece before it writes one; a memset stores its constant byte repeated.  The call's value is the destination.
 *
 * @return 1 when @p in is such a call and is selected; 0 when it is to be called
 */
static int sel_string_call(xsel_t *s, const cc_ssa_instr_t *in) {
    int w = s->word;
    int kind;
    long long n;
    long long c = 0;
    long offs[XS_INLINE_MEM_WORDS + 3];
    int sizes[XS_INLINE_MEM_WORDS + 3];
    xop_t vals[XS_INLINE_MEM_WORDS + 3];
    xop_t pattern;
    int have_pattern = 0;
    int np = 0;
    long off = 0;
    int p;
    int i;

    if (in->op != CC_SSA_CALL || in->sym == NULL || in->arg_count != 3 || in->call_chain > 0 || in->agg_class != 0 ||
        in->call_is_variadic || (in->dst >= 0 && in->rhs == in->dst)) {
        return 0;
    }
    if (strcmp(in->sym, "memcpy") == 0) {
        kind = 0;
    } else if (strcmp(in->sym, "memmove") == 0) {
        kind = 1;
    } else if (strcmp(in->sym, "memset") == 0) {
        kind = 2;
    } else {
        return 0;
    }
    if (!xs_const_of(s, in->args[2], &n) || n < 0 || n > (long long)w * (kind == 1 ? XS_INLINE_MEM_WORDS / 2
                                                                                   : XS_INLINE_MEM_WORDS) ||
        (kind == 2 && !xs_const_of(s, in->args[1], &c))) {
        return 0;
    }
    for (i = 0; i < 3; ++i) {
        if (in->args[i] < 0 || cc_ssa_value_class(s->f, in->args[i]) == CC_VAL_F64 || xs_is_pair(s, in->args[i])) {
            return 0;
        }
    }
    for (p = w; p >= 1; p /= 2) {
        while (n - off >= p) {
            offs[np] = off;
            sizes[np] = p;
            np++;
            off += p;
        }
    }
    for (i = 0; i < np; ++i) {
        if (kind == 2) {
            unsigned long long b = (unsigned long long)(c & 0xff);
            unsigned long long v = 0;
            int k;

            for (k = 0; k < sizes[i]; ++k) {
                v |= b << (8 * k);
            }
            vals[i] = xop_imm(sizes[i] == 4 ? (long long)(int)(unsigned)v : (long long)v, sizes[i]);
            if (sizes[i] == 8 && !(vals[i].disp >= -2147483647LL - 1 && vals[i].disp <= 2147483647LL)) {
                /* eight bytes no 32-bit immediate holds: made once in a register */
                if (!have_pattern) {
                    pattern = xs_temp(s);
                    xs_emit(s, XI_MOV, 8, 2, &vals[i], &pattern, NULL);
                    have_pattern = 1;
                }
                vals[i] = pattern;
            }
        } else {
            xop_t m = xs_mem_at(s, in->args[1], offs[i], sizes[i]);
            xop_t t = xs_temp(s);

            t.size = (unsigned char)sizes[i];
            xs_emit(s, XI_MOV, sizes[i], 2, &m, &t, NULL);
            vals[i] = t;
        }
        if (kind != 1) {
            xop_t d = xs_mem_at(s, in->args[0], offs[i], sizes[i]);

            xs_emit(s, XI_MOV, sizes[i], 2, &vals[i], &d, NULL);
        }
    }
    for (i = 0; kind == 1 && i < np; ++i) {
        xop_t d = xs_mem_at(s, in->args[0], offs[i], sizes[i]);

        xs_emit(s, XI_MOV, sizes[i], 2, &vals[i], &d, NULL);
    }
    if (in->dst >= 0) {
        xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), w);

        xs_move_word_to(s, in->args[0], 0, &d);
    }
    return 1;
}

/** Select instruction @p j of the function. */
static void sel_instr(xsel_t *s, size_t j) {
    const cc_ssa_function_t *f = s->f;
    const cc_ssa_instr_t *in = &f->instrs[j];
    int w = s->word;
    long long k;
    const char *sym;

    if (in->dst >= 0 && (cc_ssa_value_class(f, in->dst) == CC_VAL_F64)) {
        NOPE(s);
    }
    switch (in->op) {
    case CC_SSA_PARAM:
        if (in->param_index < 0 || (size_t)in->param_index >= f->param_count || in->dst < 0) {
            NOPE(s);
        }
        s->t->param(s, in);
        break;

    case CC_SSA_CONST:
        if (cc_ssa_value_class(f, in->dst) != CC_VAL_I64 || f->value_ty[in->dst].kind == CC_TY_AGG) {
            NOPE(s);
        }
        if (xs_const_of(s, in->dst, &k)) {
            break; /* an immediate where it is read */
        }
        {
            int part;

            for (part = 0; part < (xs_is_pair(s, in->dst) ? 2 : 1); ++part) {
                xop_t v = w == 4 ? xop_imm(part ? (long long)(int)(in->imm >> 32) : (long long)(int)in->imm, 4)
                                 : xop_imm(in->imm, 8);
                xop_t d = xop_reg(xs_vreg_of(s, in->dst, part), w);

                xs_emit(s, XI_MOV, w, 2, &v, &d, NULL);
            }
        }
        break;

    case CC_SSA_STR:
    case CC_SSA_GADDR:
        if (in->op == CC_SSA_GADDR && in->sym == NULL) {
            NOPE(s);
        }
        if (xs_sym_of(s, in->dst, &sym)) {
            break; /* an immediate, or a %rip-relative address, where it is read */
        }
        if (in->op == CC_SSA_STR) {
            char buf[96];

            str_label(s, j, buf, sizeof(buf));
            sym = keep_name(buf, "");
            if (sym == NULL) {
                s->xf.oom = 1;
                break;
            }
        } else {
            sym = in->sym;
        }
        {
            xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), w);

            if (s->t->sym_imm) {
                xop_t v = xop_sym(sym, 0, w);

                xs_emit(s, XI_MOV, w, 2, &v, &d, NULL);
            } else {
                xop_t r = sym_reg(s, sym);

                xs_emit(s, XI_MOV, w, 2, &r, &d, NULL);
            }
        }
        break;

    case CC_SSA_STACKALLOC:
    case CC_SSA_ADDR:
        if (s->frame_val[in->dst] >= 0) {
            break; /* folded into what reads it */
        }
        {
            int o = in->op == CC_SSA_STACKALLOC ? s->obj[in->dst] : s->obj[in->lhs];
            xop_t m = xop_frame(o, 0, w);
            xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), w);

            if (o < 0) {
                NOPE(s);
            }
            xs_emit(s, XI_LEA, w, 2, &m, &d, NULL);
        }
        break;

    case CC_SSA_MOV: {
        int part;

        if (in->dst == in->lhs) {
            break;
        }
        for (part = 0; part < (xs_is_pair(s, in->dst) ? 2 : 1); ++part) {
            xop_t d = xop_reg(xs_vreg_of(s, in->dst, part), w);

            if (part == 1 && !xs_is_pair(s, in->lhs)) {
                NOPE(s);
            }
            xs_move_word_to(s, in->lhs, part, &d);
        }
        break;
    }

    case CC_SSA_LO32:
    case CC_SSA_HI32: {
        xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), 4);

        if (!xs_is_pair(s, in->lhs)) {
            NOPE(s);
        }
        xs_move_word_to(s, in->lhs, in->op == CC_SSA_HI32, &d);
        break;
    }

    case CC_SSA_PAIR: {
        xop_t d0 = xop_reg(xs_vreg_of(s, in->dst, 0), 4);
        xop_t d1 = xop_reg(xs_vreg_of(s, in->dst, 1), 4);

        if (w != 4) {
            NOPE(s);
        }
        xs_move_word_to(s, in->lhs, 0, &d0);
        xs_move_word_to(s, in->rhs, 0, &d1);
        break;
    }

    case CC_SSA_UMULW: {
        xop_t eax = xop_reg(XR_AX, 4);
        xop_t edx = xop_reg(XR_DX, 4);
        xop_t b;
        xop_t d0;
        xop_t d1;
        xinst_t x;

        if (w != 4) {
            NOPE(s);
        }
        b = xs_reg_word(s, in->rhs, 0);
        d0 = xop_reg(xs_vreg_of(s, in->dst, 0), 4);
        d1 = xop_reg(xs_vreg_of(s, in->dst, 1), 4);
        xs_move_word_to(s, in->lhs, 0, &eax);
        x = xi_make(XI_MUL1, 4, 1, &b, NULL, NULL);
        x.imp_use = XR_BIT(XR_AX);
        x.imp_def = XR_BIT(XR_AX) | XR_BIT(XR_DX);
        xf_emit(&s->xf, &x);
        xs_emit(s, XI_MOV, 4, 2, &eax, &d0, NULL);
        xs_emit(s, XI_MOV, 4, 2, &edx, &d1, NULL);
        break;
    }

    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
        if (xs_is_pair(s, in->dst) || f->value_ty[in->dst].kind == CC_TY_AGG || in->lhs < 0 ||
            (in->rhs < 0 && in->op != CC_SSA_SHL && in->op != CC_SSA_SHR)) {
            NOPE(s);
        }
        sel_binop(s, in);
        break;

    case CC_SSA_DIV:
        if (xs_is_pair(s, in->dst) || in->lhs < 0 || in->rhs < 0) {
            NOPE(s);
        }
        sel_div(s, in);
        break;

    case CC_SSA_CMP: {
        xcc_t cc;
        int known = 0;

        if (in->lhs < 0 || in->rhs < 0 || cc_ssa_value_class(f, in->lhs) == CC_VAL_F64 ||
            cc_ssa_value_class(f, in->rhs) == CC_VAL_F64 || xs_is_pair(s, in->lhs) || xs_is_pair(s, in->rhs)) {
            NOPE(s);
        }
        if (j + 1 < f->instr_count && f->instrs[j + 1].op == CC_SSA_BR_COND && f->instrs[j + 1].lhs == in->dst &&
            s->nuses[in->dst] == 1 && s->ndefs[in->dst] == 1) {
            /* only the branch after it reads it: the branch is on the flags */
            const cc_ssa_instr_t *br = &f->instrs[j + 1];

            if (compare(s, in, &cc, &known)) {
                jump(s, j + 1, known ? br->true_label : br->false_label);
            } else {
                branch(s, j + 1, cc, br->true_label, br->false_label);
            }
            /* the branch is done */
            s->def_at[in->dst] = -2;
            break;
        }
        {
            xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), w);

            if (compare(s, in, &cc, &known)) {
                xop_t v = xop_imm(known, w);

                xs_emit(s, XI_MOV, w, 2, &v, &d, NULL);
            } else {
                xop_t t = xop_reg(xf_new_vreg(&s->xf), 1);
                xinst_t x = xi_make(XI_SETCC, 1, 1, &t, NULL, NULL);

                x.cc = (unsigned char)cc;
                xf_emit(&s->xf, &x);
                d.size = 4;
                xs_emit(s, XI_MOVZX, w, 2, &t, &d, NULL);
            }
        }
        break;
    }

    case CC_SSA_LABEL: {
        xinst_t x = xi_make(XI_LABEL, w, 0, NULL, NULL, NULL);

        x.label = in->label;
        xf_emit(&s->xf, &x);
        break;
    }

    case CC_SSA_BR:
        jump(s, j, in->label);
        break;

    case CC_SSA_BR_COND:
        if (j > 0 && f->instrs[j - 1].op == CC_SSA_CMP && f->instrs[j - 1].dst == in->lhs && in->lhs >= 0 &&
            s->def_at[in->lhs] == -2) {
            break; /* the compare before it made it */
        }
        if (in->lhs < 0 || xs_is_pair(s, in->lhs) || cc_ssa_value_class(f, in->lhs) == CC_VAL_F64) {
            NOPE(s);
        }
        if (xs_const_of(s, in->lhs, &k)) {
            jump(s, j, (w == 4 ? (long long)(int)k : k) != 0 ? in->true_label : in->false_label);
            break;
        }
        if (xs_sym_of(s, in->lhs, &sym) || s->frame_val[in->lhs] >= 0) {
            jump(s, j, in->true_label);
            break;
        }
        {
            xop_t v = xop_reg(xs_vreg_of(s, in->lhs, 0), w);

            xs_emit(s, XI_TEST, w, 2, &v, &v, NULL);
            branch(s, j, XC_NE, in->true_label, in->false_label);
        }
        break;

    case CC_SSA_LOAD:
        if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64 || f->value_ty[in->dst].kind == CC_TY_AGG) {
            NOPE(s);
        }
        sel_load(s, in);
        break;

    case CC_SSA_STORE:
        sel_store(s, in);
        break;

    case CC_SSA_CALL:
    case CC_SSA_CALLI:
        if (!sel_string_call(s, in)) {
            s->t->call(s, in);
        }
        break;

    case CC_SSA_ASM:
        sel_asm(s, j, in);
        break;

    case CC_SSA_RET:
        s->t->ret(s, in);
        break;

    case CC_SSA_TRAP: {
        xinst_t x = xi_make(XI_TRAP, w, 0, NULL, NULL, NULL);

        xf_emit(&s->xf, &x);
        break;
    }

    case CC_SSA_VA_START:
        if (s->t->va_start == NULL) {
            NOPE(s);
        }
        s->t->va_start(s, in);
        break;

    case CC_SSA_REGREAD: {
        xop_t d;
        xop_t r;
        const char *sp = w == 8 ? "rsp" : "esp";
        const char *bp = w == 8 ? "rbp" : "ebp";

        if (in->sym == NULL || (strcmp(in->sym, sp) != 0 && strcmp(in->sym, bp) != 0)) {
            NOPE(s);
        }
        d = xop_reg(xs_vreg_of(s, in->dst, 0), w);
        r = xop_reg(strcmp(in->sym, sp) == 0 ? XR_SP : XR_BP, w);
        xs_emit(s, XI_MOV, w, 2, &r, &d, NULL);
        break;
    }

    case CC_SSA_FRAMEADDR: {
        xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), w);
        xop_t bp = xop_reg(XR_BP, w);
        long long n;

        xs_emit(s, XI_MOV, w, 2, &bp, &d, NULL);
        for (n = 0; n < in->imm; ++n) {
            xop_t m = xop_mem(d.reg, 0, w);

            xs_emit(s, XI_MOV, w, 2, &m, &d, NULL);
        }
        break;
    }

    default:
        NOPE(s);
    }
}

/** Whether the function as a whole is one this path takes, as far as both targets go: NULL, or why not. */
static const char *function_ok(const xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    size_t i;
    int v;

    for (v = 0; v < f->value_count; ++v) {
        if (f->value_ty[v].kind == CC_TY_FLOAT) {
            return "floating point";
        }
    }
    for (i = 0; i < f->instr_count; ++i) {
        switch (f->instrs[i].op) {
        case CC_SSA_DYNALLOC:
        case CC_SSA_LADDR:
        case CC_SSA_CHAIN:
        case CC_SSA_I2F:
        case CC_SSA_F2I:
        case CC_SSA_FROUND32:
        case CC_SSA_FROUND64:
        case CC_SSA_EXT:
            return ssa_op_name(f->instrs[i].op);
        default:
            break;
        }
    }
    return s->t->function_ok(s);
}

/** x86_for_each_use() callback: count a read. */
static void count_use(int v, void *ctx) {
    xsel_t *s = (xsel_t *)ctx;

    if (v >= 0 && v < s->f->value_count) {
        s->nuses[v]++;
    }
}

/** Set up @p s for @p f: the counts of definitions and uses, the frame objects; the target's own after. */
static int sel_init(xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    size_t n = (size_t)(f->value_count > 0 ? f->value_count : 1);
    size_t np = f->param_count > 0 ? f->param_count : 1;
    size_t i;
    int v;
    int rc;

    xf_init(&s->xf);
    s->xf.nlabels = f->label_count;
    s->hidden = XR_NONE;
    s->va_save = -1;
    s->va_state = -1;
    s->lo = (int *)malloc(n * sizeof(int));
    s->hi = (int *)malloc(n * sizeof(int));
    s->ndefs = (int *)calloc(n, sizeof(int));
    s->nuses = (int *)calloc(n, sizeof(int));
    s->def_at = (int *)malloc(n * sizeof(int));
    s->addr_taken = (unsigned char *)calloc(n, 1);
    s->is_addr = (unsigned char *)calloc(n, 1);
    s->obj = (int *)malloc(n * sizeof(int));
    s->frame_val = (int *)malloc(n * sizeof(int));
    s->param_lo = (int *)malloc(np * sizeof(int));
    s->param_hi = (int *)malloc(np * sizeof(int));
    if (s->lo == NULL || s->hi == NULL || s->ndefs == NULL || s->nuses == NULL || s->def_at == NULL ||
        s->addr_taken == NULL || s->is_addr == NULL || s->obj == NULL || s->frame_val == NULL || s->param_lo == NULL ||
        s->param_hi == NULL) {
        return -1;
    }
    for (v = 0; v < (int)n; ++v) {
        s->lo[v] = XR_NONE;
        s->hi[v] = XR_NONE;
        s->def_at[v] = -1;
        s->obj[v] = -1;
        s->frame_val[v] = -1;
    }
    for (i = 0; i < np; ++i) {
        s->param_lo[i] = XR_NONE;
        s->param_hi[i] = XR_NONE;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->dst >= 0 && in->dst < f->value_count &&
            !((in->op == CC_SSA_CALL || in->op == CC_SSA_CALLI) && in->rhs == in->dst)) {
            /* (a call that fills a structure result writes the storage, not the value: its address) */
            s->ndefs[in->dst]++;
            s->def_at[in->dst] = (int)i;
        }
        if (in->op == CC_SSA_ADDR && in->lhs >= 0 && in->lhs < f->value_count) {
            s->addr_taken[in->lhs] = 1;
        }
        if (in->op == CC_SSA_STACKALLOC && in->dst >= 0 && in->dst < f->value_count) {
            s->is_addr[in->dst] = 1;
        }
        x86_for_each_use(f, in, count_use, s);
    }
    for (v = 0; v < f->value_count; ++v) {
        if (s->ndefs[v] != 1) {
            s->def_at[v] = -1;
        }
        if (s->addr_taken[v]) {
            s->obj[v] = xf_new_obj(&s->xf, XF_LOCAL, xs_is_pair(s, v) ? 8 : s->word, s->word, 0);
        }
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_STACKALLOC && in->dst >= 0 && in->dst < f->value_count) {
            int bytes = in->imm > 0 ? (int)in->imm : 1;

            /* 16 bytes or more on a 16-byte boundary, as gcc-built code may use movaps on it; on x86-64 eight on 8 */
            s->obj[in->dst] = xf_new_obj(&s->xf, XF_LOCAL, (bytes + 3) & ~3,
                                         bytes >= 16 ? 16 : s->word == 8 && bytes >= 8 ? 8 : 4, 0);
            if (s->ndefs[in->dst] == 1) {
                s->frame_val[in->dst] = s->obj[in->dst];
            }
        } else if (in->op == CC_SSA_ADDR && in->dst >= 0 && in->dst < f->value_count && s->ndefs[in->dst] == 1 &&
                   in->lhs >= 0) {
            s->frame_val[in->dst] = s->obj[in->lhs];
        }
    }
    rc = s->t->begin(s);
    if (rc != 0) {
        return rc;
    }
    if (compute_ext(s) != 0) {
        return -1;
    }
    for (v = 0; v < f->value_count; ++v) {
        if (s->addr_taken[v] && s->frame_val[v] >= 0) {
            /* the address of a stack object taken again: not handled */
            return 1;
        }
    }
    return s->xf.oom ? -1 : 0;
}

static void sel_free(xsel_t *s) {
    free(s->sbits);
    free(s->zbits);
    free(s->bind);
    free(s->asm_slot);
    free(s->lo);
    free(s->hi);
    free(s->ndefs);
    free(s->nuses);
    free(s->def_at);
    free(s->addr_taken);
    free(s->is_addr);
    free(s->obj);
    free(s->frame_val);
    free(s->param_lo);
    free(s->param_hi);
    xf_free(&s->xf);
}

/** Whether the flags may be read after instruction @p i before anything sets them again. */
static int flags_live_after(const xfunc_t *xf, int i) {
    int k;

    for (k = i + 1; k < xf->nins; ++k) {
        switch (xf->ins[k].op) {
        case XI_JCC:
        case XI_SETCC:
            return 1;
        case XI_MOV:
        case XI_LEA:
        case XI_MOVZX:
        case XI_MOVSX:
        case XI_PUSH:
            continue;
        default:
            return 0;
        }
    }
    return 0;
}

/**
 * After allocation: a copy and an add become one lea, a move of 0 into a register an xorl, and on x86-64 a move
 * of a constant that fits 32 bits unsigned the 32-bit move, which zero-extends.
 */
static void peephole(xfunc_t *xf, int word) {
    int i;
    int j = 0;

    for (i = 0; i < xf->nins; ++i) {
        xinst_t *in = &xf->ins[i];

        if (in->op == XI_MOV && in->w == word && i + 1 < xf->nins && in->o[0].kind == XO_REG &&
            in->o[1].kind == XO_REG && in->o[0].reg != in->o[1].reg && in->o[0].reg != XR_SP) {
            xinst_t *nx = &xf->ins[i + 1];

            if (nx->op == XI_ADD && nx->w == word && nx->o[1].kind == XO_REG && nx->o[1].reg == in->o[1].reg &&
                ((nx->o[0].kind == XO_IMM) || (nx->o[0].kind == XO_REG && nx->o[0].reg != in->o[1].reg)) &&
                !flags_live_after(xf, i + 1)) {
                /* mov a, d; add b, d: lea b(a), d */
                xop_t m = xop_mem(in->o[0].reg, 0, word);
                xop_t d = in->o[1];

                if (nx->o[0].kind == XO_IMM) {
                    m.disp = nx->o[0].disp;
                    m.sym = nx->o[0].sym;
                } else {
                    m.index = nx->o[0].reg;
                }
                xf->ins[j++] = xi_make(XI_LEA, word, 2, &m, &d, NULL);
                i++;
                continue;
            }
        }
        if (in->op == XI_MOV && in->o[0].kind == XO_IMM && in->o[0].sym == NULL && in->o[0].disp == 0 &&
            in->o[1].kind == XO_REG && !flags_live_after(xf, i)) {
            in->op = XI_XOR;
            in->w = 4;
            in->o[1].size = 4;
            in->o[0] = in->o[1];
        } else if (in->op == XI_MOV && in->w == 8 && in->o[0].kind == XO_IMM && in->o[0].sym == NULL &&
                   in->o[0].disp > 0 && in->o[0].disp <= 0xffffffffLL && in->o[1].kind == XO_REG) {
            in->w = 4;
            in->o[0].size = 4;
            in->o[1].size = 4;
        }
        xf->ins[j++] = *in;
    }
    xf->nins = j;
}

/**
 * Lay out the frame: below the saved registers (@p saved bytes), each object not at a fixed place, the busiest
 * first; a 16-byte one on a 16-byte boundary.  The bytes the prologue allocates go to *@p frame.
 */
static int layout_frame(const xsel_t *s, xfunc_t *xf, int saved, int *frame) {
    int *order = (int *)malloc((size_t)(xf->nobj > 0 ? xf->nobj : 1) * sizeof(int));
    int residue = s->t->fp_residue;
    int n = 0;
    long cur = -saved;
    long bytes;
    int i;

    if (order == NULL) {
        return -1;
    }
    for (i = 0; i < xf->nobj; ++i) {
        if (xf->obj[i].kind != XF_FIXED && (xf->obj[i].uses > 0 || xf->obj[i].kind == XF_LOCAL)) {
            order[n++] = i;
        }
    }
    /* busiest first, the big (16-byte aligned) ones last; insertion sort, the order otherwise kept */
    for (i = 1; i < n; ++i) {
        int x = order[i];
        int k = i - 1;

        while (k >= 0) {
            const xobj_t *a = &xf->obj[order[k]];
            const xobj_t *b = &xf->obj[x];
            int abig = a->align >= 16;
            int bbig = b->align >= 16;

            if (abig > bbig || (abig == bbig && a->uses < b->uses)) {
                order[k + 1] = order[k];
                k--;
            } else {
                break;
            }
        }
        order[k + 1] = x;
    }
    for (i = 0; i < n; ++i) {
        xobj_t *o = &xf->obj[order[i]];
        int align = o->align >= 16 ? 16 : o->align;

        cur -= o->size;
        /* the frame pointer is fp_residue past a 16-byte boundary */
        while (((cur - (align >= 16 ? residue : 0)) % align) != 0) {
            cur--;
        }
        o->off = cur;
    }
    free(order);
    bytes = -cur - saved;
    if (s->t->stack_boundary >= 16) {
        /* the body's stack pointer on a 16-byte boundary: saved + frame is fp_residue past one */
        while (((saved + bytes) % 16) != residue) {
            bytes += 4;
        }
    } else {
        bytes = (long)align_up_size((size_t)bytes, (size_t)s->t->stack_boundary);
    }
    *frame = (int)bytes;
    return 0;
}

/**
 * Print the epilogue and return; with @p reset, the stack pointer is made again from the frame pointer, as an
 * asm goto's jump past its own restores may leave it; @p pop bytes of arguments popped.
 */
static void print_epilogue(FILE *fp, const xsel_t *s, unsigned saved, int frame, int reset, int pop, int emit_debug) {
    const xsel_target_t *t = s->t;
    int w = t->word;
    char sfx = w == 8 ? 'q' : 'l';
    const char *bp = w == 8 ? "%rbp" : "%ebp";
    const char *sp = w == 8 ? "%rsp" : "%esp";
    int nsaved = 0;
    int r;

    for (r = 0; r < t->nsaved; ++r) {
        nsaved += (saved & XR_BIT(t->saved[r])) != 0;
    }
    if (emit_debug) {
        fputs("\t.cfi_remember_state\n", fp);
    }
    if (nsaved == 0) {
        if (frame > 0 || reset) {
            fputs("\tleave\n", fp);
        } else {
            fprintf(fp, "\tpop%c %s\n", sfx, bp);
        }
    } else {
        if (frame > 0 || reset) {
            fprintf(fp, "\tlea%c %d(%s), %s\n", sfx, -w * nsaved, bp, sp);
        }
        for (r = t->nsaved - 1; r >= 0; --r) {
            if ((saved & XR_BIT(t->saved[r])) != 0) {
                fprintf(fp, "\tpop%c %s\n", sfx, xr_name(t->saved[r], w));
            }
        }
        fprintf(fp, "\tpop%c %s\n", sfx, bp);
    }
    if (emit_debug) {
        fprintf(fp, "\t.cfi_def_cfa %s, %d\n", sp, w);
    }
    if (pop > 0) {
        fprintf(fp, "\tret $%d\n", pop);
    } else {
        fputs("\tret\n", fp);
    }
    if (emit_debug) {
        fputs("\t.cfi_restore_state\n", fp);
    }
}

/**
 * Print inline asm @p in through emit_inline_asm(), its operands where the selector put them: a frame layout
 * whose slot for each is that frame object.
 */
static int print_asm(FILE *fp, const xsel_t *s, const xinst_t *in, cc_diag_t *diag) {
    const cc_ssa_function_t *f = s->f;
    slot_layout_t lay;
    int b;
    int rc;

    memset(&lay, 0, sizeof(lay));
    lay.value_count = f->value_count;
    lay.slot_size = 1;
    lay.slot_of = (int *)calloc((size_t)(f->value_count > 0 ? f->value_count : 1), sizeof(int));
    lay.stackalloc_off = (int *)calloc((size_t)(f->value_count > 0 ? f->value_count : 1), sizeof(int));
    if (lay.slot_of == NULL || lay.stackalloc_off == NULL) {
        free(lay.slot_of);
        free(lay.stackalloc_off);
        set_diag(diag, "out of memory binding inline asm operands");
        return -1;
    }
    for (b = in->aux; b < s->nbind && s->bind[b].v >= 0; ++b) {
        /* slot_off() is -(slot_of + 1) with a slot size of 1 and no bias */
        lay.slot_of[s->bind[b].v] = (int)(-s->xf.obj[s->bind[b].obj].off - 1);
    }
    rc = emit_inline_asm(fp, f, &lay, &f->instrs[in->ir_index], s->fn_index, (size_t)in->ir_index, s->word == 8,
                         diag);
    free(lay.slot_of);
    free(lay.stackalloc_off);
    return rc;
}

/** Print the function: its string literals, its header, prologue, body and trailer. */
static int print_function(FILE *fp, xsel_t *s, const char *src_path, int emit_debug, cc_diag_t *diag) {
    const cc_ssa_function_t *f = s->f;
    const xsel_target_t *t = s->t;
    int w = t->word;
    char sfx = w == 8 ? 'q' : 'l';
    const char *func_sec = (f->attr_section != NULL && f->attr_section[0] != '\0') ? f->attr_section : ".text";
    unsigned callee = 0;
    unsigned saved = 0;
    int nsaved = 0;
    int frame = 0;
    size_t j;
    int i;
    int r;

    for (r = 0; r < t->nsaved; ++r) {
        callee |= XR_BIT(t->saved[r]);
    }
    for (i = 0; i < s->xf.nins; ++i) {
        const xinst_t *in = &s->xf.ins[i];
        int k;

        for (k = 0; k < in->nops; ++k) {
            const xop_t *o = &in->o[k];

            if (o->kind == XO_REG && o->reg >= 0) {
                saved |= XR_BIT(o->reg);
            } else if (o->kind == XO_MEM) {
                if (o->base >= 0) {
                    saved |= XR_BIT(o->base);
                }
                if (o->index >= 0) {
                    saved |= XR_BIT(o->index);
                }
            }
        }
    }
    saved &= callee;
    if (s->has_goto) {
        /* the asm saves what it uses, and its jump may skip the restore */
        saved = callee;
    }
    for (r = 0; r < t->nsaved; ++r) {
        nsaved += (saved & XR_BIT(t->saved[r])) != 0;
    }
    if (layout_frame(s, &s->xf, w * nsaved, &frame) != 0) {
        s->xf.oom = 1;
        return -1;
    }
    for (j = 0; j < f->instr_count; ++j) {
        if (f->instrs[j].op == CC_SSA_STR) {
            emit_string_literal_label(fp, s->fn_index, j, f->instrs[j].sym, func_sec);
        }
    }
    fprintf(fp, "\n");
    emit_text_section(fp, f->attr_section != NULL && f->attr_section[0] != '\0' ? f->attr_section : NULL);
    if ((f->storage & CC_STORAGE_STATIC) == 0 &&
        !(((f->storage & CC_STORAGE_INLINE) != 0) && ((f->storage & CC_STORAGE_EXTERN) == 0))) {
        fprintf(fp, (f->attr_flags & CC_ATTR_WEAK) != 0 ? ".weak %s\n" : ".globl %s\n", f->name);
    }
    emit_visibility_attr(fp, f->name, f->attr_flags);
    if (f->attr_align > 1) {
        fprintf(fp, ".align %ld\n", f->attr_align);
    }
    fprintf(fp, ".type %s, @function\n", f->name);
    fprintf(fp, "%s:\n", f->name);
    if (emit_debug) {
        fprintf(fp, "\t.cfi_startproc\n\t.cfi_def_cfa_offset %d\n\t.cfi_offset %s, -%d\n", 2 * w,
                xr_name(XR_BP, w), 2 * w);
    }
    fprintf(fp, "\tpush%c %s\n\tmov%c %s, %s\n", sfx, xr_name(XR_BP, w), sfx, xr_name(XR_SP, w), xr_name(XR_BP, w));
    if (emit_debug) {
        fprintf(fp, "\t.cfi_def_cfa_register %s\n", xr_name(XR_BP, w));
        if (src_path != NULL) {
            fputs("\t.loc 1 1 0\n", fp);
        }
    }
    {
        int off = -2 * w;

        for (r = 0; r < t->nsaved; ++r) {
            if ((saved & XR_BIT(t->saved[r])) != 0) {
                off -= w;
                fprintf(fp, "\tpush%c %s\n", sfx, xr_name(t->saved[r], w));
                if (emit_debug) {
                    fprintf(fp, "\t.cfi_offset %s, %d\n", xr_name(t->saved[r], w), off);
                }
            }
        }
    }
    if (frame > 0) {
        fprintf(fp, "\tsub%c $%d, %s\n", sfx, frame, xr_name(XR_SP, w));
    }
    for (i = 0; i < s->xf.nins; ++i) {
        const xinst_t *in = &s->xf.ins[i];

        if (in->op == XI_LABEL) {
            emit_local_label(fp, f->name, in->label);
            fputs(":\n", fp);
        } else if (in->op == XI_RET) {
            print_epilogue(fp, s, saved, frame, s->has_goto, in->aux, emit_debug);
        } else if (in->op == XI_ASM) {
            if (print_asm(fp, s, in, diag) != 0) {
                return -1;
            }
        } else if (in->op == XI_TRAP) {
            fputs(w == 8 || i386_has_ud2() ? "\tud2\n" : "\tint3\n", fp);
        } else {
            xi_print(fp, &s->xf, in, w, f->name);
        }
    }
    if (emit_debug) {
        fputs("\t.cfi_endproc\n", fp);
    }
    fprintf(fp, ".size %s, .-%s\n", f->name, f->name);
    if ((f->attr_flags & CC_ATTR_CONSTRUCTOR) != 0) {
        emit_lifecycle_attr(fp, ".init_array", "init_array", f->name, w == 8);
    }
    if ((f->attr_flags & CC_ATTR_DESTRUCTOR) != 0) {
        emit_lifecycle_attr(fp, ".fini_array", "fini_array", f->name, w == 8);
    }
    return 0;
}

int x86_mir_emit_function(FILE *out, const xsel_target_t *t, const cc_ssa_module_t *m, const cc_ssa_function_t *f,
                          size_t fn_index, const char *src_path, int emit_debug, cc_diag_t *diag) {
    static long taken; /* the functions this path has taken, for CC_MIR_LIMIT */
    const char *env = getenv("CC_NO_MIR");
    const char *limit = getenv("CC_MIR_LIMIT");
    const char *stats = getenv("CC_MIR_STATS");
    const char *why;
    xsel_t s;
    size_t j;
    int rc;

    if (env != NULL && env[0] == '1') {
        return 1;
    }
    memset(&s, 0, sizeof(s));
    s.t = t;
    s.word = t->word;
    s.m = m;
    s.f = f;
    s.fn_index = fn_index;
    why = function_ok(&s);
    if (why != NULL) {
        /* CC_MIR_STATS=1: each function this path takes, and why it does not take the others */
        if (stats != NULL && stats[0] == '1') {
            fprintf(stderr, "mir: %s: not taken: %s\n", f->name, why);
        }
        return 1;
    }
    /* CC_MIR_LIMIT=n: only the first n functions it would take, to find one it miscompiles by bisection */
    if (limit != NULL && taken >= atol(limit)) {
        return 1;
    }
    taken++;
    rc = sel_init(&s);
    for (j = 0; rc == 0 && j < f->instr_count && !s.unsupported && !s.xf.oom; ++j) {
        sel_instr(&s, j);
    }
    if (rc == 0 && (s.unsupported || s.xf.oom)) {
        rc = s.xf.oom ? -1 : 1;
    }
    if (rc == 0) {
        rc = x86_mir_allocate(&s.xf, t->alloc);
    }
    if (stats != NULL && stats[0] == '1') {
        if (rc == 0) {
            fprintf(stderr, "mir: %s: taken\n", f->name);
        } else if (s.unsupported) {
            const char *base = strrchr(s.unsupported_file != NULL ? s.unsupported_file : "?", '/');

            fprintf(stderr, "mir: %s: not taken: %s:%d\n", f->name,
                    base != NULL ? base + 1 : s.unsupported_file, s.unsupported);
        } else if (rc > 0) {
            fprintf(stderr, "mir: %s: not taken: allocation\n", f->name);
        }
    }
    if (rc == 0) {
        peephole(&s.xf, t->word);
        if (print_function(out, &s, src_path, emit_debug, diag) != 0) {
            rc = -1;
        }
    }
    free_names();
    sel_free(&s);
    if (rc < 0 && diag != NULL && diag->message[0] == '\0') {
        set_diag(diag, "out of memory selecting instructions");
    }
    return rc;
}
