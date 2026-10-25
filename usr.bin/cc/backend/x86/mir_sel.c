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
 * alloca and a variable-length array move the stack pointer
 * (sel_dynalloc()); the epilogue makes it again from the frame pointer.
 *
 * A label's address (a computed goto's) is a local symbol's.
 *
 * A function the path does not handle -- floating point and what each
 * target turns down -- returns 1 before
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

static const char *keep_name(const char *a, const char *b);

const char *xs_keep_name(const char *a, const char *b) {
    return keep_name(a, b);
}

const char *xs_reloc_name(const char *sym, const char *kind) {
    size_t n = strcspn(sym, "+-");
    char head[256];
    char rel[32];
    const char *h;

    if (n >= sizeof(head)) {
        return NULL;
    }
    memcpy(head, sym, n);
    head[n] = '\0';
    snprintf(rel, sizeof(rel), "@%s", kind);
    h = keep_name(head, rel);
    return h != NULL ? keep_name(h, sym + n) : NULL;
}

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
    if (d->op == CC_SSA_LADDR) {
        /* a label's address (&&label), as emit_local_label() names it: what a computed goto compares */
        char buf[32];

        snprintf(buf, sizeof(buf), "_%d", d->label);
        *sym = keep_name(".L", s->f->name);
        *sym = *sym != NULL ? keep_name(*sym, buf) : NULL;
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
        /* a float or a double held in SSE: a vector register */
        *slot = xs_fp_kind(s, v) == XFP_SSE ? xf_new_xvreg(&s->xf) : xf_new_vreg(&s->xf);
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

/**
 * The address of symbol @p sym in a new register: off %rip, or read from the GOT; on i386 under -fPIC off the
 * GOT's address, @GOTOFF for what binds within the object, else read from its @GOT entry, the offset added.
 */
static xop_t sym_reg(xsel_t *s, const char *sym) {
    xop_t t = xs_temp(s);
    xop_t m = xop_mem(XR_NONE, 0, s->word);

    if (s->got != XR_NONE) {
        m.base = s->got;
        if (s->t->sym_got(s, sym)) {
            size_t n = strcspn(sym, "+-");
            char head[256];

            /* the entry holds the symbol's own address: the offset is added after */
            if (n >= sizeof(head)) {
                s->unsupported = __LINE__;
                s->unsupported_file = __FILE__;
                return t;
            }
            memcpy(head, sym, n);
            head[n] = '\0';
            m.sym = xs_reloc_name(head, "GOT");
            if (m.sym == NULL) {
                s->xf.oom = 1;
                return t;
            }
            xs_emit(s, XI_MOV, 4, 2, &m, &t, NULL);
            if (sym[n] != '\0') {
                xop_t k = xop_imm(strtol(sym + n, NULL, 10), 4);

                xs_emit(s, XI_ADD, 4, 2, &k, &t, NULL);
            }
        } else {
            m.sym = xs_reloc_name(sym, "GOTOFF");
            if (m.sym == NULL) {
                s->xf.oom = 1;
                return t;
            }
            xs_emit(s, XI_LEA, 4, 2, &m, &t, NULL);
        }
        return t;
    }
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

/**
 * Whether IR value @p v is a register's value scaled by 1, 2, 4 or 8 -- a multiply by one of them or a shift
 * left by 0 to 3 of a value written once -- which an address takes as its index (*@p idx, *@p scale).
 */
static int scaled_index(xsel_t *s, int v, int *idx, int *scale) {
    const cc_ssa_instr_t *d;
    const char *sym;
    long long k = 0;
    long long c;
    int x = -1;

    if (!stable(s, v) || xs_is_pair(s, v)) {
        return 0;
    }
    d = &s->f->instrs[s->def_at[v]];
    if (d->op == CC_SSA_MUL && d->lhs >= 0 && d->rhs >= 0) {
        if (xs_const_of(s, d->rhs, &k)) {
            x = d->lhs;
        } else if (xs_const_of(s, d->lhs, &k)) {
            x = d->rhs;
        }
    } else if (d->op == CC_SSA_SHL && d->lhs >= 0) {
        long long sh = d->imm;

        if ((d->rhs < 0 || xs_const_of(s, d->rhs, &sh)) && sh >= 0 && sh <= 3) {
            k = 1LL << sh;
            x = d->lhs;
        }
    }
    if (x < 0 || (k != 1 && k != 2 && k != 4 && k != 8) || !stable(s, x) || s->frame_val[x] >= 0 ||
        xs_const_of(s, x, &c) || xs_sym_of(s, x, &sym) || xs_is_pair(s, x) || xs_fp_kind(s, x) != XFP_NONE) {
        return 0;
    }
    *idx = x;
    *scale = (int)k;
    return 1;
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
        o = xop_mem(s->got, disp, size);
        o.sym = s->got != XR_NONE ? xs_reloc_name(sym, "GOTOFF") : sym;
        if (o.sym == NULL) {
            s->xf.oom = 1;
        }
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
                int base = d->lhs;
                int idx = d->rhs;
                int scale = 1;
                int x;
                int sc;

                /* a scaled index, as an array's element is: (base, index, scale) */
                if (scaled_index(s, d->rhs, &x, &sc)) {
                    idx = x;
                    scale = sc;
                } else if (scaled_index(s, d->lhs, &x, &sc)) {
                    base = d->rhs;
                    idx = x;
                    scale = sc;
                }
                o = xop_mem(xs_vreg_of(s, base, 0), disp, size);
                o.index = xs_vreg_of(s, idx, 0);
                o.scale = (unsigned char)scale;
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

static int cmp32(const xsel_t *s, const cc_ssa_instr_t *in);
static const cc_ssa_instr_t *def_of(const xsel_t *s, int v);

/**
 * Set the flags for `x & k` against 0, @p mv that mask (or a copy of it), read only here: one test of k against x,
 * in the fewest bytes that hold k -- a testb on x86-64, whose every register has a byte, else a testl when k is no
 * more than 31 bits.  The and and the test against 0 were two instructions; gcc's is one.
 *
 * @return 1 when @p mv is such a mask and the flags are set, else 0
 */
static int sel_mask_test(xsel_t *s, int mv) {
    const cc_ssa_function_t *f = s->f;
    long long z;
    long long k = 0;
    int x;
    int hops;
    const cc_ssa_instr_t *a = def_of(s, mv);
    const char *sym;
    int w;
    xop_t imm;
    xop_t r;

    for (hops = 0; hops < 4 && a != NULL && a->op == CC_SSA_MOV && a->lhs >= 0 && s->nuses[mv] == 1; ++hops) {
        mv = a->lhs;
        a = def_of(s, mv);
    }
    if (a == NULL || a->op != CC_SSA_AND || s->nuses[mv] != 1 || xs_is_pair(s, mv) || a->lhs < 0 || a->rhs < 0) {
        return 0;
    }
    x = xs_const_of(s, a->rhs, &k) ? a->lhs : xs_const_of(s, a->lhs, &k) ? a->rhs : -1;
    for (hops = 0; hops < 3 && x >= 0; ++hops) {
        /* a mask of a mask, read only by it (an unsigned int's extension of one): one mask, of both */
        const cc_ssa_instr_t *in2 = def_of(s, x);
        long long k2;
        int x2;
        int y = x;
        int h2;

        /* (through the copies an inlined function's result is made of) */
        for (h2 = 0; h2 < 4 && in2 != NULL && in2->op == CC_SSA_MOV && in2->lhs >= 0 && s->nuses[y] == 1; ++h2) {
            y = in2->lhs;
            in2 = def_of(s, y);
        }
        if (in2 == NULL || in2->op != CC_SSA_AND || s->nuses[y] != 1 || s->nuses[x] != 1 || in2->lhs < 0 ||
            in2->rhs < 0) {
            break;
        }
        x2 = xs_const_of(s, in2->rhs, &k2) ? in2->lhs : xs_const_of(s, in2->lhs, &k2) ? in2->rhs : -1;
        if (x2 < 0) {
            break;
        }
        k &= k2;
        x = x2;
    }
    if (x < 0 || k == 0 || xs_is_pair(s, x) || xs_const_of(s, x, &z) || xs_sym_of(s, x, &sym) ||
        cc_ssa_value_class(f, x) != CC_VAL_I64) {
        return 0;
    }
    if (s->word == 4) {
        k = (long long)(unsigned)k;
    }
    if (s->word == 8 && k > 0 && k < 256) {
        w = 1;
    } else if (k > 0 && k <= 0x7fffffffLL) {
        w = 4;
    } else if (s->word == 4 && k > 0 && k <= 0xffffffffLL) {
        w = 4;
        k = (long long)(int)(unsigned)k;
    } else if (s->word == 8 && k >= -0x80000000LL && k < 0) {
        w = 8;
    } else {
        return 0;
    }
    r = xs_reg_word(s, x, 0);
    if (r.kind != XO_REG) {
        return 0;
    }
    r.size = (unsigned char)w;
    imm = xop_imm(k, w);
    xs_emit(s, XI_TEST, w, 2, &imm, &r, NULL);
    return 1;
}

static int ext_of(const xsel_t *s, const cc_ssa_instr_t *in, int *src);
static int zero_test_operand(const xsel_t *s, const cc_ssa_instr_t *in);
static int br_test_bytes(const xsel_t *s, int v);

/**
 * Set the flags for an extension @p v of the low bytes of a value, read only here, against 0: a test of those
 * bytes (on x86-64, every register having a byte), the extension unmade -- a bool a call returned, tested, was a
 * movzbl and a test of the word.
 *
 * @return 1 when @p v is such an extension and the flags are set, else 0
 */
static int sel_ext_test(xsel_t *s, int v) {
    const cc_ssa_instr_t *d = def_of(s, v);
    int src;
    int bytes;
    xop_t r;

    if (d == NULL || s->nuses[v] != 1 || (bytes = ext_of(s, d, &src)) <= 0 || bytes > 4 || bytes >= s->word ||
        xs_is_pair(s, src) || !stable(s, src)) {
        return 0;
    }
    r = xs_reg_word(s, src, 0);
    if (r.kind != XO_REG) {
        return 0;
    }
    r.size = (unsigned char)bytes;
    xs_emit(s, XI_TEST, bytes, 2, &r, &r, NULL);
    return 1;
}

/**
 * Set the flags for compare @p in (lhs against rhs), its operands swapped when the left one is a constant;
 * the condition to test them by goes to *@p cc.  0, or 1 when both are constants and *@p known is the result.
 */
static int compare(xsel_t *s, const cc_ssa_instr_t *in, xcc_t *cc, int *known) {
    xop_t a = xs_src_word(s, in->lhs, 0);
    xop_t b = xs_src_word(s, in->rhs, 0);
    int w = s->word;
    int narrow = cmp32(s, in); /* of ints: their 32-bit registers, whatever their upper halves are */

    *cc = cmp_cc(in->cmp_kind, in->is_unsigned);
    if ((in->cmp_kind == CC_CMP_EQ || in->cmp_kind == CC_CMP_NE) && in->lhs >= 0 && in->rhs >= 0) {
        /* (x & k) == 0, != 0 */
        long long z;

        int x;

        if ((xs_const_of(s, in->rhs, &z) && z == 0 && (sel_mask_test(s, in->lhs) || sel_ext_test(s, in->lhs))) ||
            (xs_const_of(s, in->lhs, &z) && z == 0 && (sel_mask_test(s, in->rhs) || sel_ext_test(s, in->rhs)))) {
            return 0;
        }
        if ((x = zero_test_operand(s, in)) >= 0) {
            /* of its own bits, compute_demand() having said that is all that is read of it */
            int bytes = br_test_bytes(s, x);
            xop_t r = xs_reg_word(s, x, 0);

            if (r.kind == XO_REG) {
                r.size = (unsigned char)bytes;
                xs_emit(s, XI_TEST, bytes, 2, &r, &r, NULL);
                return 0;
            }
        }
    }
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
    if (narrow && b.kind == XO_IMM && (b.sym != NULL || b.disp != (long long)(int)b.disp)) {
        narrow = 0;
    }
    if (narrow && a.kind == XO_IMM) {
        a = xs_reg_word(s, in->lhs, 0);
    }
    if (narrow) {
        w = 4;
        a.size = 4;
        b.size = 4;
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
 * Whether compare @p in reads its operands' low 32 bits alone: on x86-64, a compare of integers of no more than four
 * bytes, made of their 32-bit registers, which holds whatever their upper halves hold.
 */
/** Whether value @p v is, for a 32-bit compare, an int: no more than four bytes, or a constant of 32 bits. */
static int int32_operand(const xsel_t *s, int v, int is_unsigned) {
    long long k;

    if (xs_const_of((xsel_t *)s, v, &k)) {
        return is_unsigned ? (k >= 0 && k <= 0xffffffffLL) : (k >= -0x80000000LL && k <= 0x7fffffffLL);
    }
    return cc_ssa_value_size(s->f, v) <= 4 && !s->addr_taken[v];
}

static int cmp32(const xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;

    return s->word == 8 && in->op == CC_SSA_CMP && in->lhs >= 0 && in->rhs >= 0 &&
           cc_ssa_value_class(f, in->lhs) == CC_VAL_I64 && cc_ssa_value_class(f, in->rhs) == CC_VAL_I64 &&
           int32_operand(s, in->lhs, in->is_unsigned) && int32_operand(s, in->rhs, in->is_unsigned) &&
           (cc_ssa_value_size(f, in->lhs) <= 4 || cc_ssa_value_size(f, in->rhs) <= 4);
}

int xs_extends_taken(const xsel_t *s, int v, int bytes) {
    return s->dbits == NULL || v < 0 || v >= s->f->value_count || bytes >= s->word || s->dbits[v] > 8 * bytes;
}

/**
 * The bytes a branch on value @p v tests: an integer narrower than the word -- four bytes or less on x86-64 -- is
 * nonzero by its own bits (test %al, %al; test %eax, %eax), whatever is above them; 0 for the whole word.
 */
static int br_test_bytes(const xsel_t *s, int v) {
    const cc_ssa_function_t *f = s->f;
    uint32_t n;

    if (v < 0 || v >= f->value_count || cc_ssa_value_class(f, v) != CC_VAL_I64 ||
        f->value_ty[v].kind == CC_TY_AGG || s->addr_taken[v] || xs_is_pair(s, v)) {
        return 0;
    }
    n = cc_ssa_value_size(f, v);
    if (s->word == 4) {
        /* (an int is the word already; a byte's register is one of the four that have one) */
        return n == 1 || n == 2 ? (int)n : 0;
    }
    return n == 1 || n == 2 || n == 4 ? (int)n : 0;
}

/**
 * Of compare @p in, `x == 0` or `x != 0`, x an integer br_test_bytes() tests by its own bits: x; else -1.
 */
static int zero_test_operand(const xsel_t *s, const cc_ssa_instr_t *in) {
    long long z;
    int x;

    if (in->op != CC_SSA_CMP || (in->cmp_kind != CC_CMP_EQ && in->cmp_kind != CC_CMP_NE) || in->lhs < 0 ||
        in->rhs < 0) {
        return -1;
    }
    if (xs_const_of((xsel_t *)s, in->rhs, &z) && z == 0) {
        x = in->lhs;
    } else if (xs_const_of((xsel_t *)s, in->lhs, &z) && z == 0) {
        x = in->rhs;
    } else {
        return -1;
    }
    return br_test_bytes(s, x) > 0 && !xs_const_of((xsel_t *)s, x, &z) ? x : -1;
}

/** The mask extension @p in is -- an and with 0xff, 0xffff or (x86-64) 0xffffffff -- its byte count, its source *@p src. */
static int mask_ext_of(const xsel_t *s, const cc_ssa_instr_t *in, int *src) {
    long long m = 0;
    int other;
    int bytes;

    if (in->op != CC_SSA_AND || in->lhs < 0 || in->rhs < 0 || xs_is_pair(s, in->dst)) {
        return 0;
    }
    other = xs_const_of(s, in->rhs, &m) ? in->lhs : xs_const_of(s, in->lhs, &m) ? in->rhs : -1;
    bytes = m == 0xff ? 1 : m == 0xffff ? 2 : (s->word == 8 && m == 0xffffffffLL) ? 4 : 0;
    if (other < 0 || bytes == 0 || xs_const_of(s, other, &m)) {
        return 0;
    }
    *src = other;
    return bytes;
}

typedef struct {
    xsel_t *s;
    int changed;
} demand_ctx_t;

/** Raise what is read of value @p v to @p bits. */
static void demand(demand_ctx_t *c, int v, int bits) {
    if (v >= 0 && v < c->s->f->value_count && bits > c->s->dbits[v]) {
        c->s->dbits[v] = (unsigned char)bits;
        c->changed = 1;
    }
}

/** x86_for_each_use() callback: all of a value is read. */
static void demand_all(int v, void *ctx) {
    demand_ctx_t *c = (demand_ctx_t *)ctx;

    demand(c, v, 8 * c->s->word);
}

/**
 * Work out, for each IR value, the most low bits anything reads of it (dbits), back from its readers: the low bits
 * of a sum, a difference, a product, a mask or a shift left by a constant are those of its operands', a copy reads
 * what is read of it, a store what it stores, a 32-bit compare (cmp32()) 32, an extension at most its source's
 * width, and anything else all of a value.  Then an extension whose result is read no further than its source's
 * width -- an int extended to a long only for a 32-bit compare -- is a copy (ext_copy), as the bits it would set
 * are never read.
 */
static int compute_demand(xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    int W = 8 * s->word;
    demand_ctx_t c;
    int pass;
    size_t i;

    s->dbits = (unsigned char *)calloc((size_t)(f->value_count > 0 ? f->value_count : 1), 1);
    s->ext_copy = (unsigned char *)calloc(f->instr_count > 0 ? f->instr_count : 1, 1);
    s->done = (unsigned char *)calloc(f->instr_count > 0 ? f->instr_count : 1, 1);
    if (s->dbits == NULL || s->ext_copy == NULL || s->done == NULL) {
        return -1;
    }
    c.s = s;
    for (pass = 0; pass < 32; ++pass) {
        c.changed = 0;
        for (i = 0; i < f->instr_count; ++i) {
            const cc_ssa_instr_t *in = &f->instrs[i];
            int d = in->dst;
            int D = d >= 0 && d < f->value_count && in->op != CC_SSA_STORE ? s->dbits[d] : 0;
            int narrow = d >= 0 && d < f->value_count && in->op != CC_SSA_STORE &&
                         cc_ssa_value_class(f, d) == CC_VAL_I64 && !xs_is_pair(s, d) && !s->addr_taken[d];
            int src;
            int bytes;
            long long k;

            if (narrow && (bytes = ext_of(s, in, &src)) > 0) {
                demand(&c, src, D < 8 * bytes ? D : 8 * bytes);
                continue;
            }
            if (narrow && (bytes = mask_ext_of(s, in, &src)) > 0) {
                demand(&c, src, D < 8 * bytes ? D : 8 * bytes);
                continue;
            }
            switch (in->op) {
            case CC_SSA_ADD:
            case CC_SSA_SUB:
            case CC_SSA_MUL:
            case CC_SSA_AND:
            case CC_SSA_OR:
            case CC_SSA_XOR:
                if (narrow) {
                    demand(&c, in->lhs, D);
                    demand(&c, in->rhs, D);
                    continue;
                }
                break;
            case CC_SSA_SHL:
                if (narrow && (in->rhs < 0 || xs_const_of(s, in->rhs, &k))) {
                    demand(&c, in->lhs, D);
                    demand(&c, in->rhs, W);
                    continue;
                }
                break;
            case CC_SSA_MOV:
                if (narrow) {
                    demand(&c, in->lhs, D);
                    continue;
                }
                break;
            case CC_SSA_CMP:
                if (zero_test_operand(s, in) >= 0) {
                    /* x == 0, x != 0 of an integer of four bytes or less: its own bits */
                    int x = zero_test_operand(s, in);

                    demand(&c, x, 8 * br_test_bytes(s, x));
                    continue;
                }
                if (cmp32(s, in)) {
                    demand(&c, in->lhs, 32);
                    demand(&c, in->rhs, 32);
                    continue;
                }
                break;
            case CC_SSA_BR_COND:
                /* a branch on an integer of four bytes or less, on x86-64: whether its own bits are 0 (br_test_bytes()) */
                if (br_test_bytes(s, in->lhs) > 0) {
                    demand(&c, in->lhs, 8 * br_test_bytes(s, in->lhs));
                    continue;
                }
                break;
            case CC_SSA_CALL:
                /*
                 * an integer argument of four bytes or less, on x86-64: what the psABI has the callee read of it,
                 * its own bits -- extended to 32 where it is narrower, as clang's callees read a char or a short
                 */
                if (s->word == 8 && in->sym != NULL && in->call_chain == 0 && !(in->rhs >= 0 && in->rhs == in->dst)) {
                    size_t a;
                    int ok = 1;

                    for (a = 0; a < in->arg_count && ok; ++a) {
                        int v = in->args[a];
                        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;

                        if (v < 0 || v >= f->value_count) {
                            continue;
                        }
                        if (abi == CC_CALL_ARG_ABI_DEFAULT && cc_ssa_value_class(f, v) == CC_VAL_I64 &&
                            f->value_ty[v].kind != CC_TY_AGG && cc_ssa_value_size(f, v) > 0 &&
                            cc_ssa_value_size(f, v) <= 4) {
                            demand(&c, v, 32);
                        } else {
                            demand(&c, v, W);
                        }
                    }
                    continue;
                }
                break;
            case CC_SSA_RET:
                /* an integer result of four bytes or less: the caller reads its own bits, extended to 32 */
                if (s->word == 8 && in->lhs >= 0 && in->lhs < f->value_count && in->rhs < 0 && in->agg_class == 0 &&
                    cc_ssa_value_class(f, in->lhs) == CC_VAL_I64 && f->value_ty[in->lhs].kind != CC_TY_AGG &&
                    cc_ssa_value_size(f, in->lhs) > 0 && cc_ssa_value_size(f, in->lhs) <= 4 &&
                    (f->ret_abi & ~CC_ABI_REGPARM_MASK) == CC_CALL_ARG_ABI_DEFAULT) {
                    demand(&c, in->lhs, 32);
                    continue;
                }
                break;
            case CC_SSA_STORE:
                if (in->rhs >= 0 && in->rhs < f->value_count && cc_ssa_value_class(f, in->rhs) == CC_VAL_I64 &&
                    !xs_is_pair(s, in->rhs) && in->imm > 0 && in->imm < s->word) {
                    demand(&c, in->rhs, 8 * (int)in->imm);
                    demand(&c, in->lhs, W);
                    continue;
                }
                break;
            default:
                break;
            }
            x86_for_each_use(f, in, demand_all, &c);
        }
        if (!c.changed) {
            break;
        }
    }
    if (c.changed) {
        /* not settled: every extension stays */
        return 0;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int src;
        int bytes = ext_of(s, in, &src);

        if (bytes == 0) {
            bytes = mask_ext_of(s, in, &src);
        }
        if (bytes > 0 && in->dst >= 0 && in->dst < f->value_count && !s->addr_taken[in->dst] &&
            s->dbits[in->dst] <= 8 * bytes) {
            s->ext_copy[i] = 1;
        }
    }
    return 0;
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
        if (s->addr_taken[v] || cc_ssa_value_class(f, v) != CC_VAL_I64 || xs_is_pair(s, v) ||
            f->value_ty[v].kind == CC_TY_AGG) {
            /* (a small structure in a register is its bytes, not an extended integer) */
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
            if (s->ext_copy != NULL && s->ext_copy[i]) {
                /* an extension made a copy: what its source is */
                int from;

                if (ext_of(s, in, &from) == 0) {
                    (void)mask_ext_of(s, in, &from);
                }
                sb = s->sbits[from];
                zb = s->zbits[from];
                if (sb > s->sbits[d]) {
                    s->sbits[d] = (unsigned char)sb;
                    changed = 1;
                }
                if (zb > s->zbits[d]) {
                    s->zbits[d] = (unsigned char)zb;
                    changed = 1;
                }
                continue;
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
                /* (extended as it is taken, unless nothing reads past its bits: xs_extends_taken()) */
                if (s->t->extends_params && xs_extends_taken(s, d, in->imm > 0 ? (int)in->imm : s->word)) {
                    held_bits(in->imm > 0 ? (int)in->imm : s->word, in->is_unsigned, s->word, &sb, &zb);
                }
                break;
            case CC_SSA_CALL:
            case CC_SSA_CALLI:
                if ((s->t->extends_params || in->imm == 1 || in->imm == 2) &&
                    xs_extends_taken(s, d, in->imm > 0 ? (int)in->imm : s->word)) {
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

/** The instruction that writes value @p v once, or NULL. */
static const cc_ssa_instr_t *def_of(const xsel_t *s, int v) {
    return v >= 0 && v < s->f->value_count && stable(s, v) ? &s->f->instrs[s->def_at[v]] : NULL;
}

/**
 * Add @p in into @p d as one lea when one operand is an index scaled by 2, 4 or 8 (scaled_index()) that nothing
 * else reads: `x + i * 4` was imul $4 and an add, as Linux's pte_offset() makes a table entry's address it keeps.
 *
 * @return 1 if it was, else 0
 */
static int sel_scaled_add(xsel_t *s, const cc_ssa_instr_t *in, const xop_t *d) {
    int k;

    for (k = 0; k < 2; ++k) {
        int sv = k == 0 ? in->rhs : in->lhs;
        int bv = k == 0 ? in->lhs : in->rhs;
        int idx;
        int scale;
        long long c;
        const char *sym;
        xop_t m;

        if (sv < 0 || bv < 0 || sv == bv || s->nuses[sv] != 1 || !scaled_index(s, sv, &idx, &scale) || scale == 1 ||
            xs_is_pair(s, bv) || xs_const_of(s, bv, &c) || xs_sym_of(s, bv, &sym) || s->frame_val[bv] >= 0 ||
            xs_fp_kind(s, bv) != XFP_NONE) {
            continue;
        }
        m = xop_mem(xs_vreg_of(s, bv, 0), 0, s->word);
        m.index = xs_vreg_of(s, idx, 0);
        m.scale = (unsigned char)scale;
        xs_emit(s, XI_LEA, s->word, 2, &m, d, NULL);
        return 1;
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

        if (bytes > 0 && s->ext_copy[in - s->f->instrs]) {
            /* read no further than its source's bits: a copy */
            xs_move_word_to(s, src, 0, &d);
            return;
        }
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
        /* a mask of a mask: one, of both (Linux's pte_flags(pte) & _PAGE_RW is (pte & 0xfff) & 2) */
        long long m1 = 0;
        long long m2 = 0;
        int inner = xs_const_of(s, in->rhs, &m2) ? in->lhs : xs_const_of(s, in->lhs, &m2) ? in->rhs : -1;
        const cc_ssa_instr_t *id = inner >= 0 ? def_of(s, inner) : NULL;
        int x = -1;
        int hops;

        /* (through the copies an inlined function's result is made of) */
        for (hops = 0; hops < 4 && id != NULL && id->op == CC_SSA_MOV && id->lhs >= 0; ++hops) {
            inner = id->lhs;
            id = def_of(s, inner);
        }

        if (id != NULL && id->op == CC_SSA_AND && id->lhs >= 0 && id->rhs >= 0 && !xs_is_pair(s, inner)) {
            x = xs_const_of(s, id->rhs, &m1) ? id->lhs : xs_const_of(s, id->lhs, &m1) ? id->rhs : -1;
        }
        if (x >= 0 && stable(s, x) && !xs_is_pair(s, x)) {
            long long c;
            long long m = m1 & m2;

            if (!xs_const_of(s, x, &c) && (w == 4 || fits32(m))) {
                xop_t k = xop_imm(w == 4 ? (long long)(int)m : m, w);

                xs_move_word_to(s, x, 0, &d);
                xs_emit(s, XI_AND, w, 2, &k, &d, NULL);
                return;
            }
        }
    }
    if (op == XI_AND) {
        /* a mask of the low byte, half or (x86-64) word: a zero extension */
        long long m = 0;
        int other = xs_const_of(s, in->rhs, &m) ? in->lhs : xs_const_of(s, in->lhs, &m) ? in->rhs : -1;
        int bytes = m == 0xff ? 1 : m == 0xffff ? 2 : (w == 8 && m == 0xffffffffLL) ? 4 : 0;

        if (other >= 0 && bytes > 0 && !xs_const_of(s, other, &m)) {
            if (s->ext_copy[in - s->f->instrs]) {
                xs_move_word_to(s, other, 0, &d);
                return;
            }
            sel_ext(s, in->dst, other, bytes, 0);
            return;
        }
    }
    if (op == XI_ADD && sel_scaled_add(s, in, &d)) {
        return;
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
            unsigned long long m = (unsigned long long)b.disp & (w == 4 ? 0xffffffffULL : ~0ULL);
            int sh = 0;

            while (sh < 8 * w - 1 && (1ULL << sh) < m) {
                sh++;
            }
            if (a.kind != XO_IMM && m > 1 && (1ULL << sh) == m &&
                (m >= 128 || (a.kind == XO_REG && a.reg == d.reg) || in->dst == in->lhs || in->dst == in->rhs)) {
                /*
                 * by a power of two, a shift: imul $4096 takes 32 bits of constant where shl $12 takes 8, and
                 * one into the register it reads is no larger
                 */
                xop_t c = xop_imm(sh, 1);

                if (!(a.kind == XO_REG && a.reg == d.reg)) {
                    xs_emit(s, XI_MOV, w, 2, &a, &d, NULL);
                }
                xs_emit(s, XI_SHL, w, 2, &c, &d, NULL);
                return;
            }
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

void xs_load_bytes(xsel_t *s, xop_t m, xop_t d, int bytes) {
    int w = s->word;
    int off = 0;
    int p;

    d.size = (unsigned char)w;
    for (p = w; p >= 1; p /= 2) {
        if (bytes - off < p) {
            continue;
        }
        {
            xop_t a = m;
            xop_t t = off == 0 ? d : xs_temp(s);
            xop_t t4 = t;

            a.disp += off;
            a.size = (unsigned char)p;
            t4.size = (unsigned char)(p == 8 ? 8 : 4);
            if (p >= 4) {
                xs_emit(s, XI_MOV, p, 2, &a, &t4, NULL);
            } else {
                /* zero-extended into the 32-bit register, which clears the rest of a 64-bit one */
                xs_emit(s, XI_MOVZX, w, 2, &a, &t4, NULL);
            }
            if (off > 0) {
                xop_t sh = xop_imm(8 * off, 1);

                xs_emit(s, XI_SHL, w, 2, &sh, &t, NULL);
                xs_emit(s, XI_OR, w, 2, &t, &d, NULL);
            }
            off += p;
        }
    }
}

void xs_store_bytes(xsel_t *s, xop_t v, xop_t m, int bytes) {
    int w = s->word;
    xop_t t = xs_temp(s);
    int off = 0;
    int p;

    v.size = (unsigned char)w;
    xs_emit(s, XI_MOV, w, 2, &v, &t, NULL);
    for (p = w; p >= 1; p /= 2) {
        if (bytes - off < p) {
            continue;
        }
        {
            xop_t a = m;
            xop_t tp = t;

            a.disp += off;
            a.size = (unsigned char)p;
            if (p == 1 && w == 4) {
                /* a byte register: a copy in one */
                xop_t b = xs_temp(s);

                xs_emit(s, XI_MOV, 4, 2, &t, &b, NULL);
                tp = b;
            }
            tp.size = (unsigned char)p;
            xs_emit(s, XI_MOV, p, 2, &tp, &a, NULL);
            off += p;
            if (off < bytes) {
                xop_t sh = xop_imm(8 * p, 1);

                xs_emit(s, XI_SHR, w, 2, &sh, &t, NULL);
            }
        }
    }
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
    if (size > 0 && size < w && size != 1 && size != 2 && size != 4) {
        /* a structure of 3, 5, 6 or 7 bytes, into one register */
        xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), w);

        xs_load_bytes(s, xs_mem_at(s, in->lhs, 0, 1), d, size);
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
    if (size > 0 && size < w && size != 1 && size != 2 && size != 4) {
        /* a structure of 3, 5, 6 or 7 bytes from one register */
        xs_store_bytes(s, xs_src_word(s, in->rhs, 0), xs_mem_at(s, in->lhs, 0, 1), size);
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

/** Whether output @p k of inline asm @p in is a flag output ("=@ccz"). */
static int c_is_flag(const cc_ssa_instr_t *in, size_t k) {
    const char *c = in->asm_out_constraints != NULL ? in->asm_out_constraints[k] : NULL;

    return c != NULL && strncmp(c, "=@cc", 4) == 0;
}

/** The value of inline asm @p in's operand @p k (outputs then inputs), its address for one given so, or -1. */
static int asm_operand_value(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, size_t k) {
    int raw = k < in->asm_out_count ? in->asm_out_values[k] : in->asm_in_values[k - in->asm_out_count];
    int v = CC_SSA_ASM_MEM_INDIRECT_P(raw) ? CC_SSA_ASM_MEM_INDIRECT_DECODE(raw) : raw;

    return v >= 0 && v < f->value_count ? v : -1;
}

/**
 * Whether value @p v is, through copies, what register read @p r has: then an asm operand bound to that register
 * has it already -- Linux's ASM_CALL_CONSTRAINT, "+r"(current_stack_pointer), is %esp as it is.
 */
static int reads_register(const xsel_t *s, int v, int r) {
    int hops;

    for (hops = 0; hops < 8; ++hops) {
        const cc_ssa_instr_t *d = def_of(s, v);

        if (d == NULL) {
            return 0;
        }
        if (d->op == CC_SSA_MOV && d->rhs < 0) {
            v = d->lhs;
            continue;
        }
        if (d->op == CC_SSA_REGREAD && d->sym != NULL) {
            const char *n = d->sym[0] == '%' ? d->sym + 1 : d->sym;

            return r == XR_SP && (strcmp(n, "esp") == 0 || strcmp(n, "rsp") == 0);
        }
        return 0;
    }
    return 0;
}

/**
 * Whether inline asm @p in may have its operands in registers by @p plan (x86_asm_plan()): no value of a register
 * operand in two words or floating, which the plan's general registers would not hold, and none a new stack
 * pointer an operand bound to %esp is given.
 */
static int asm_bindable(xsel_t *s, const cc_ssa_instr_t *in, const x86_asm_plan_t *plan) {
    size_t out_n = in->asm_out_count;
    size_t k;

    for (k = 0; k < plan->n; ++k) {
        int v = asm_operand_value(s->f, in, k);
        const char *c = k < out_n ? (in->asm_out_constraints != NULL ? in->asm_out_constraints[k] : NULL) : NULL;

        if (v >= 0 && (xs_is_pair(s, v) || xs_fp_kind(s, v) != XFP_NONE)) {
            return 0;
        }
        if (v >= 0 && plan->kind[k] == X86_ASM_REG && plan->reg[k] == XR_SP &&
            (k >= out_n || (c != NULL && strchr(c, '+') != NULL)) && !reads_register(s, v, XR_SP)) {
            /* a new stack pointer the asm is given: the unbound way */
            return 0;
        }
    }
    return 1;
}

/**
 * The condition flag output constraint @p c names ("=@ccz", "=@ccnc"), into *@p cc; 0 for one with no condition
 * of the selector's (overflow).
 */
static int flag_output_cc(const char *c, xcc_t *cc) {
    static const struct {
        const char *name;
        xcc_t cc;
    } names[] = {
        {"e", XC_E},    {"z", XC_E},    {"ne", XC_NE},  {"nz", XC_NE},  {"l", XC_L},    {"nge", XC_L},
        {"le", XC_LE},  {"ng", XC_LE},  {"g", XC_G},    {"nle", XC_G},  {"ge", XC_GE},  {"nl", XC_GE},
        {"b", XC_B},    {"c", XC_B},    {"nae", XC_B},  {"be", XC_BE},  {"na", XC_BE},  {"a", XC_A},
        {"nbe", XC_A},  {"ae", XC_AE},  {"nb", XC_AE},  {"nc", XC_AE},  {"s", XC_S},    {"ns", XC_NS},
        {"p", XC_P},    {"pe", XC_P},   {"np", XC_NP},  {"po", XC_NP},
    };
    size_t i;

    if (c == NULL || strncmp(c, "=@cc", 4) != 0) {
        return 0;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(c + 4, names[i].name) == 0) {
            *cc = names[i].cc;
            return 1;
        }
    }
    return 0;
}

/**
 * Of @p plan for inline asm @p in, IR instruction @p j, a flag output read only by a branch after it -- through
 * copies read once, and constants, nothing between that sets the flags -- left in the flags (X86_ASM_FLAGS): the
 * branch is on the asm's condition itself (s->flag_br).  Linux's atomic_dec_and_test() and test_bit() were
 * `decl; sete %cl; movzbl %cl, %ecx; test %ecx, %ecx; je` where gcc has `decl; je`.
 */
static void asm_flag_branch(xsel_t *s, size_t j, const cc_ssa_instr_t *in, x86_asm_plan_t *plan) {
    const cc_ssa_function_t *f = s->f;
    size_t k;

    for (k = 0; k < in->asm_out_count && k < plan->n && k < 8; ++k) {
        int v = asm_operand_value(f, in, k);
        xcc_t cc;
        size_t b;

        if (v < 0 || !c_is_flag(in, k) || plan->kind[k] != X86_ASM_REG || s->nuses[v] != 1 || s->ndefs[v] > 1 /* (the asm's own write is not counted) */ ||
            !flag_output_cc(in->asm_out_constraints[k], &cc)) {
            continue;
        }
        for (b = j + 1; b < f->instr_count; ++b) {
            const cc_ssa_instr_t *x = &f->instrs[b];

            if (x->op == CC_SSA_BR_COND && x->lhs == v) {
                plan->kind[k] = X86_ASM_FLAGS;
                s->flag_br = (long)b;
                s->flag_cc = (unsigned char)cc;
                return;
            }
            if (x->op == CC_SSA_MOV && x->rhs < 0 && x->lhs == v && x->dst >= 0 && x->dst < f->value_count &&
                s->nuses[x->dst] == 1 && s->ndefs[x->dst] == 1 && !xs_is_pair(s, x->dst)) {
                v = x->dst;
                continue;
            }
            if (x->op == CC_SSA_CONST) {
                continue;
            }
            break;
        }
    }
}

/**
 * Of @p plan for inline asm @p in, each memory operand given by its address that is a symbol's -- one the code
 * reaches without the GOT -- written as the symbol (X86_ASM_SYM), as gcc writes `"m" (var)`: its address was made
 * in a register (lea var(%rip), %r10) for the asm to read through.
 */
static void asm_plan_symbols(xsel_t *s, const cc_ssa_instr_t *in, x86_asm_plan_t *plan) {
    size_t k;

    for (k = 0; k < plan->n; ++k) {
        int v = asm_operand_value(s->f, in, k);
        const char *sym;

        if (plan->kind[k] == X86_ASM_ADDR && v >= 0 && xs_sym_of(s, v, &sym) && s->got == XR_NONE &&
            !(s->t->sym_got != NULL && s->t->sym_got(s, sym))) {
            plan->kind[k] = X86_ASM_SYM;
            plan->sym[k] = sym;
        }
    }
}

/**
 * Inline asm @p in, IR instruction @p j, with its register operands in the registers @p plan gives them: each
 * input moved there before, each output moved out after (zero-extended from its size, as emit_inline_asm() left
 * it), the rest -- memory operands -- in slots as sel_asm() binds them.  The asm reads, writes and clobbers those
 * registers alone, so the function keeps its values in registers across it, and saves a callee-saved one it uses
 * in its own prologue.
 */
static void sel_asm_bound(xsel_t *s, size_t j, const cc_ssa_instr_t *in, const x86_asm_plan_t *plan) {
    const cc_ssa_function_t *f = s->f;
    size_t out_n = in->asm_out_count;
    int first = s->nbind;
    int w = s->word;
    unsigned uses = 0;
    unsigned defs = 0;
    xinst_t x;
    size_t k;
    int b;

    /* the memory operands' slots */
    for (k = 0; k < plan->n; ++k) {
        int is_out = k < out_n;
        const char *c = is_out ? (in->asm_out_constraints != NULL ? in->asm_out_constraints[k] : NULL)
                               : (in->asm_in_constraints != NULL ? in->asm_in_constraints[k - out_n] : NULL);
        int v = asm_operand_value(f, in, k);
        xasm_bind_t *bd = NULL;

        if (v < 0 || plan->kind[k] != X86_ASM_SLOT) {
            continue;
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
        bd->in |= (unsigned char)(!is_out || (c != NULL && strchr(c, '+') != NULL));
        bd->out |= (unsigned char)is_out;
    }
    for (b = first; b < s->nbind; ++b) {
        if (s->bind[b].in) {
            xop_t m = xop_frame(s->bind[b].obj, 0, w);

            xs_move_word_to(s, s->bind[b].v, 0, &m);
        }
    }
    /* the register operands' values, into their registers */
    for (k = 0; k < plan->n; ++k) {
        int v = asm_operand_value(f, in, k);
        int is_out = k < out_n;
        const char *c = is_out ? (in->asm_out_constraints != NULL ? in->asm_out_constraints[k] : NULL) : NULL;
        int r = plan->reg[k];
        xop_t d;

        if (v < 0 || r < 0) {
            continue;
        }
        d = xop_reg(r, w);
        if (r == XR_SP) {
            /* the stack pointer has itself already (asm_bindable()); an output of it is read after as any is */
            continue;
        }
        if (plan->kind[k] == X86_ASM_ADDR || (!is_out && plan->kind[k] == X86_ASM_REG) ||
            (!is_out && plan->kind[k] == X86_ASM_IMM) /* a tied input: into its output's register */ ||
            (is_out && plan->kind[k] == X86_ASM_REG && c != NULL && strchr(c, '+') != NULL)) {
            xs_move_word_to(s, v, 0, &d);
            uses |= XR_BIT(r);
        }
        if (is_out && plan->kind[k] == X86_ASM_REG) {
            defs |= XR_BIT(r);
        }
    }
    x = xi_make(XI_ASM, w, 0, NULL, NULL, NULL);
    x.ir_index = (int)j;
    x.aux = first;
    for (k = 0; k < out_n && k < 8; ++k) {
        if (plan->kind[k] == X86_ASM_FLAGS) {
            x.cc |= (unsigned char)(1u << k);
        }
    }
    x.imp_use = uses;
    x.imp_def = defs;
    x.clobber = x86_asm_clobbered_gprs(in) & ~(XR_BIT(XR_SP) | XR_BIT(XR_BP));
    xf_emit(&s->xf, &x);
    /* the outputs, out of their registers: of their size, the rest zero */
    for (k = 0; k < out_n; ++k) {
        int v = asm_operand_value(f, in, k);
        int osz = in->asm_out_sizes != NULL && in->asm_out_sizes[k] > 0 ? (int)in->asm_out_sizes[k] : w;
        xop_t r;
        xop_t d;

        if (v < 0 || plan->kind[k] != X86_ASM_REG) {
            continue;
        }
        d = xop_reg(xs_vreg_of(s, v, 0), w);
        if (c_is_flag(in, k)) {
            /* emit_inline_asm() set it and cleared the rest */
            r = xop_reg(plan->reg[k], w);
            xs_emit(s, XI_MOV, w, 2, &r, &d, NULL);
        } else if (osz < 4) {
            r = xop_reg(plan->reg[k], osz);
            xs_emit(s, XI_MOVZX, w, 2, &r, &d, NULL);
        } else if (osz == 4 && w == 8) {
            r = xop_reg(plan->reg[k], 4);
            d.size = 4;
            xs_emit(s, XI_MOV, 4, 2, &r, &d, NULL);
        } else {
            r = xop_reg(plan->reg[k], w);
            xs_emit(s, XI_MOV, w, 2, &r, &d, NULL);
        }
    }
    for (b = first; b < s->nbind; ++b) {
        if (s->bind[b].out) {
            xop_t m = xop_frame(s->bind[b].obj, 0, w);
            xop_t d = xop_reg(xs_vreg_of(s, s->bind[b].v, 0), w);

            xs_emit(s, XI_MOV, w, 2, &m, &d, NULL);
        }
    }
    {
        xasm_bind_t *end = add_bind(s);

        if (end != NULL) {
            end->v = -1;
        }
    }
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

    {
        x86_asm_plan_t plan;
        int rc = x86_asm_plan(f, in, j, w == 8, &plan);

        if (rc < 0) {
            s->xf.oom = 1;
            return;
        }
        if (rc == 0) {
            int ok = asm_bindable(s, in, &plan);

            if (ok) {
                asm_plan_symbols(s, in, &plan);
                asm_flag_branch(s, j, in, &plan);
                sel_asm_bound(s, j, in, &plan);
            }
            x86_asm_plan_free(&plan);
            if (ok) {
                return;
            }
        }
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
            /* an x87 floating value lives in its home, which the asm reads and writes as its slot */
            int o = xs_fp_kind(s, v) == XFP_X87 ? xs_fp_home(s, v).obj
                    : s->addr_taken[v]          ? s->obj[v]
                                                : asm_slot(s, s->nbind - first);

            bd = add_bind(s);
            if (bd == NULL || o < 0) {
                return;
            }
            bd->v = v;
            bd->obj = o;
        }
        if (s->addr_taken[v] || xs_fp_kind(s, v) == XFP_X87) {
            continue; /* it lives there */
        }
        bd->in |= (unsigned char)need_in;
        bd->out |= (unsigned char)(is_out && !indirect);
    }
    for (b = first; b < s->nbind; ++b) {
        if (s->bind[b].in && xs_fp_kind(s, s->bind[b].v) == XFP_SSE) {
            /* a double in a vector register: its 8 bytes */
            xop_t m = xop_frame(s->bind[b].obj, 0, 8);
            xop_t r = xs_fp_reg(s, s->bind[b].v);

            xs_emit(s, XI_MOV, 8, 2, &r, &m, NULL);
        } else if (s->bind[b].in) {
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
        if (s->bind[b].out && xs_fp_kind(s, s->bind[b].v) == XFP_SSE) {
            xop_t m = xop_frame(s->bind[b].obj, 0, 8);
            xop_t r = xs_fp_reg(s, s->bind[b].v);

            xs_emit(s, XI_MOV, 8, 2, &m, &r, NULL);
        } else if (s->bind[b].out) {
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

/**
 * A dynamic stack allocation (alloca, a variable-length array): lhs bytes, rounded up to 16, off the stack, the
 * block's 16-aligned address the value.  With a save slot (rhs) the stack pointer first goes back to where the
 * previous execution found it, and is recorded there, so that an array declared in a loop reuses its storage, as
 * emit_dynalloc() does.  The frame is addressed from the frame pointer, and a call's stack arguments from the
 * stack pointer as it is then, so moving it disturbs neither; the epilogue makes it again from the frame pointer.
 */
static void sel_dynalloc(xsel_t *s, const cc_ssa_instr_t *in) {
    int w = s->word;
    xop_t sp = xop_reg(XR_SP, w);
    xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), w);
    xop_t n = xs_reg_word(s, in->lhs, 0);
    xop_t k15 = xop_imm(15, w);
    xop_t km16 = xop_imm(-16, w);
    xop_t t = xs_temp(s);

    if (in->rhs >= 0) {
        int o = s->obj[in->rhs];
        xop_t save = xop_frame(o, 0, w);
        xop_t old = xs_temp(s);
        xinst_t x;
        int skip = xf_new_label(&s->xf);

        if (o < 0) {
            NOPE(s);
        }
        xs_emit(s, XI_MOV, w, 2, &save, &old, NULL);
        xs_emit(s, XI_TEST, w, 2, &old, &old, NULL);
        x = xi_make(XI_JCC, w, 0, NULL, NULL, NULL);
        x.cc = XC_E;
        x.label = skip;
        xf_emit(&s->xf, &x);
        xs_emit(s, XI_MOV, w, 2, &old, &sp, NULL);
        x = xi_make(XI_LABEL, w, 0, NULL, NULL, NULL);
        x.label = skip;
        xf_emit(&s->xf, &x);
        xs_emit(s, XI_MOV, w, 2, &sp, &save, NULL);
    }
    xs_emit(s, XI_MOV, w, 2, &n, &t, NULL);
    xs_emit(s, XI_ADD, w, 2, &k15, &t, NULL);
    xs_emit(s, XI_AND, w, 2, &km16, &t, NULL);
    xs_emit(s, XI_SUB, w, 2, &t, &sp, NULL);
    /* i386 does not keep the stack pointer 16-aligned: the block itself is */
    xs_emit(s, XI_AND, w, 2, &km16, &sp, NULL);
    xs_emit(s, XI_MOV, w, 2, &sp, &d, NULL);
    s->has_dynalloc = 1;
}

/**
 * The branch that integer compare @p j's value is read by, later in its block -- directly, or through copies each
 * of which only the next reads (a variable set to the compare) -- when nothing between them writes the compare's
 * operands.  The compare is then made at the branch: the value it reads has its compare noted in cmp_at, and the
 * copies, made nowhere, def_at -2.  -1 when there is no such branch.
 */
static int branch_reads_later(xsel_t *s, size_t j) {
    const cc_ssa_function_t *f = s->f;
    const cc_ssa_instr_t *c = &f->instrs[j];
    int cur = c->dst;
    int copies[8];
    int ncopies = 0;
    size_t k;

    for (k = j + 1; k < f->instr_count; ++k) {
        const cc_ssa_instr_t *in = &f->instrs[k];
        size_t o;

        if (in->op == CC_SSA_BR_COND && in->lhs == cur) {
            int m;

            s->cmp_at[cur] = (int)j;
            s->def_at[cur] = -2;
            for (m = 0; m < ncopies; ++m) {
                s->def_at[copies[m]] = -2;
            }
            return (int)k;
        }
        if (in->op == CC_SSA_MOV && in->lhs == cur && in->dst >= 0 && in->dst != cur && s->nuses[cur] == 1 &&
            s->ndefs[in->dst] == 1 && s->nuses[in->dst] == 1 && ncopies < 8 && !s->addr_taken[in->dst]) {
            copies[ncopies++] = in->dst;
            cur = in->dst;
            continue;
        }
        if (in->op == CC_SSA_LABEL || in->op == CC_SSA_BR || in->op == CC_SSA_BR_COND || in->op == CC_SSA_RET ||
            in->op == CC_SSA_TRAP || (in->op == CC_SSA_ASM && in->asm_is_goto)) {
            return -1;
        }
        if (in->dst >= 0 && in->op != CC_SSA_STORE && (in->dst == c->lhs || in->dst == c->rhs)) {
            return -1;
        }
        for (o = 0; in->op == CC_SSA_ASM && o < in->asm_out_count; ++o) {
            int ov = in->asm_out_values[o];

            if (ov == c->lhs || ov == c->rhs || (CC_SSA_ASM_MEM_INDIRECT_P(ov) &&
                                                 (CC_SSA_ASM_MEM_INDIRECT_DECODE(ov) == c->lhs ||
                                                  CC_SSA_ASM_MEM_INDIRECT_DECODE(ov) == c->rhs))) {
                return -1;
            }
        }
    }
    return -1;
}

/** Select instruction @p j of the function. */
/** Whether value @p v, written once, has its value by instruction @p j: written before it, or a constant. */
static int ready_at(xsel_t *s, int v, size_t j) {
    long long k;
    const char *sym;

    return v >= 0 && v < s->f->value_count &&
           (xs_const_of(s, v, &k) || xs_sym_of(s, v, &sym) || (stable(s, v) && (size_t)s->def_at[v] < j));
}

/**
 * An i386 two-word add or subtract as legalization writes it, from its low word's instruction @p j: `lo = a + b`,
 * the carry `lo <u a` (the borrow `a <u b`), `h1 = ah + bh`, `hi = h1 + carry` -- each but the low word read only
 * by the next.  Selected as one add (sub) and an adc (sbb), the carry the flags between them: the compare, the
 * setcc and the extra add go.  The instructions of the high word are marked done.
 *
 * @return 1 when @p j is such a low word and is selected, else 0
 */
static int sel_carry_pair(xsel_t *s, size_t j) {
    const cc_ssa_function_t *f = s->f;
    const cc_ssa_instr_t *lo = &f->instrs[j];
    int is_add = lo->op == CC_SSA_ADD;
    size_t c_at = 0;
    size_t h_at = 0;
    size_t x_at;
    const cc_ssa_instr_t *c = NULL;
    const cc_ssa_instr_t *h = NULL;
    const cc_ssa_instr_t *x;
    size_t i;
    int carry;
    int other;
    xop_t ob, obh, dlo, dhi;

    if (s->word != 4 || (lo->op != CC_SSA_ADD && lo->op != CC_SSA_SUB) || lo->dst < 0 || lo->lhs < 0 ||
        lo->rhs < 0 || !stable(s, lo->dst) || xs_is_pair(s, lo->dst) || s->addr_taken[lo->dst] ||
        cc_ssa_value_size(f, lo->dst) != 4) {
        return 0;
    }
    /* the carry, a little after it in the same straight run */
    for (i = j + 1; i < f->instr_count && i <= j + 6 && c == NULL; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_LABEL || in->op == CC_SSA_BR || in->op == CC_SSA_BR_COND || in->op == CC_SSA_RET) {
            break;
        }
        if (in->op == CC_SSA_CMP && in->cmp_kind == CC_CMP_LT && in->is_unsigned && in->dst >= 0 &&
            stable(s, in->dst) && s->nuses[in->dst] == 1 &&
            (is_add ? in->lhs == lo->dst && (in->rhs == lo->lhs || in->rhs == lo->rhs)
                    : in->lhs == lo->lhs && in->rhs == lo->rhs)) {
            c = in;
            c_at = i;
        }
    }
    if (c == NULL) {
        return 0;
    }
    carry = c->dst;
    /* the high word: one more add (sub) of the carry to the sum (difference) of the high words */
    for (i = c_at + 1; i < f->instr_count && i <= c_at + 6 && h == NULL; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_LABEL || in->op == CC_SSA_BR || in->op == CC_SSA_BR_COND || in->op == CC_SSA_RET) {
            break;
        }
        if (in->op == lo->op && in->dst >= 0 && stable(s, in->dst) && !s->addr_taken[in->dst] &&
            (in->rhs == carry || (is_add && in->lhs == carry))) {
            h = in;
            h_at = i;
        }
    }
    if (h == NULL) {
        return 0;
    }
    other = h->rhs == carry ? h->lhs : h->rhs;
    x = def_of(s, other);
    if (x == NULL || x->op != lo->op || s->nuses[other] != 1 || (x_at = (size_t)s->def_at[other]) <= j ||
        x->lhs < 0 || x->rhs < 0 || !ready_at(s, x->lhs, j) || !ready_at(s, x->rhs, j) ||
        xs_is_pair(s, x->lhs) || xs_is_pair(s, x->rhs) || xs_is_pair(s, lo->lhs) || xs_is_pair(s, lo->rhs)) {
        return 0;
    }
    /* the operands first: what makes them (a symbol's address) may write the flags */
    ob = xs_src_word(s, lo->rhs, 0);
    obh = xs_src_word(s, x->rhs, 0);
    dlo = xop_reg(xs_vreg_of(s, lo->dst, 0), 4);
    dhi = xop_reg(xs_vreg_of(s, h->dst, 0), 4);
    xs_move_word_to(s, lo->lhs, 0, &dlo);
    xs_move_word_to(s, x->lhs, 0, &dhi);
    {
        xinst_t low = xi_make(is_add ? XI_ADD : XI_SUB, 4, 2, &ob, &dlo, NULL);

        low.aux = XI_AUX_CARRY_OUT;
        xf_emit(&s->xf, &low);
    }
    xs_emit(s, is_add ? XI_ADC : XI_SBB, 4, 2, &obh, &dhi, NULL);
    s->done[c_at] = 1;
    s->done[x_at] = 1;
    s->done[h_at] = 1;
    return 1;
}

/**
 * Whether `x << k | y >> (word - k)` (or `x >> k | y << (word - k)`), the shifts each read only here, of a word:
 * a two-word shift's or rotate's word as legalization writes it -- one shld (shrd) of a copy of x.
 *
 * @return 1 when @p in is such an or and is selected, else 0
 */
static int sel_double_shift(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    int side;

    if (in->op != CC_SSA_OR || in->dst < 0 || in->lhs < 0 || in->rhs < 0 || xs_is_pair(s, in->dst) ||
        cc_ssa_value_size(f, in->dst) != (uint32_t)s->word) {
        return 0;
    }
    for (side = 0; side < 2; ++side) {
        int pv = side ? in->rhs : in->lhs;
        int qv = side ? in->lhs : in->rhs;
        const cc_ssa_instr_t *p = def_of(s, pv);
        const cc_ssa_instr_t *q = def_of(s, qv);
        long long kp;
        long long kq;
        xop_t d;
        xop_t y;
        xop_t c;

        if (p == NULL || q == NULL || s->nuses[pv] != 1 || s->nuses[qv] != 1 ||
            (p->op != CC_SSA_SHL && !(p->op == CC_SSA_SHR && p->is_unsigned)) ||
            (q->op != CC_SSA_SHL && !(q->op == CC_SSA_SHR && q->is_unsigned)) || p->op == q->op || p->lhs < 0 ||
            q->lhs < 0 || xs_is_pair(s, p->lhs) || xs_is_pair(s, q->lhs) ||
            cc_ssa_value_size(f, pv) != (uint32_t)s->word || cc_ssa_value_size(f, qv) != (uint32_t)s->word) {
            continue;
        }
        kp = p->imm;
        kq = q->imm;
        if ((p->rhs >= 0 && !xs_const_of(s, p->rhs, &kp)) || (q->rhs >= 0 && !xs_const_of(s, q->rhs, &kq)) ||
            kp <= 0 || kq <= 0 || kp + kq != 8 * s->word) {
            continue;
        }
        d = xop_reg(xs_vreg_of(s, in->dst, 0), s->word);
        y = xs_reg_word(s, q->lhs, 0);
        c = xop_imm(kp, 1);
        if (y.kind == XO_REG && y.reg == d.reg) {
            xop_t t = xs_temp(s);

            xs_emit(s, XI_MOV, s->word, 2, &y, &t, NULL);
            y = t;
        }
        xs_move_word_to(s, p->lhs, 0, &d);
        xs_emit(s, p->op == CC_SSA_SHL ? XI_SHLD : XI_SHRD, s->word, 3, &c, &y, &d);
        return 1;
    }
    return 0;
}

/** Fewest cases a switch's chain of compares has to be a jump through a table. */
#define SWITCH_TABLE_MIN_CASES 4
/** A table's entries at most this many per case, and never more than SWITCH_TABLE_MAX. */
#define SWITCH_TABLE_DENSITY 8
#define SWITCH_TABLE_MAX 4096

/** One case of a switch's chain of compares (switch_case()). */
typedef struct {
    long long k;  /**< the value compared with, at the value's width */
    int target;   /**< the label it branches to */
} switch_case_t;

/**
 * Of branch @p j of @p s, a case of a switch as lowering makes it: a branch on `icmp eq x, k` of a constant, the
 * compare read only there -- x into *@p v, k (at x's width) into *@p k, where it goes when equal into *@p target,
 * where when not into *@p next.
 *
 * @return 1 if it is one, else 0
 */
static int switch_case(const xsel_t *s, size_t j, int *v, long long *k, int *target, int *next) {
    const cc_ssa_function_t *f = s->f;
    const cc_ssa_instr_t *br = &f->instrs[j];
    const cc_ssa_instr_t *c;
    int x;
    long long kk;

    if (br->op != CC_SSA_BR_COND || br->lhs < 0 || br->lhs >= f->value_count || s->nuses[br->lhs] != 1) {
        return 0;
    }
    c = s->def_at[br->lhs] == -2 ? &f->instrs[s->cmp_at[br->lhs]] : def_of(s, br->lhs);
    if (c == NULL || c->op != CC_SSA_CMP || c->cmp_kind != CC_CMP_EQ || c->lhs < 0 || c->rhs < 0) {
        return 0;
    }
    if (xs_const_of(s, c->rhs, &kk) && !xs_const_of(s, c->lhs, &kk)) {
        x = c->lhs;
        (void)xs_const_of(s, c->rhs, &kk);
    } else if (xs_const_of(s, c->lhs, &kk) && !xs_const_of(s, c->rhs, &kk)) {
        x = c->rhs;
        (void)xs_const_of(s, c->lhs, &kk);
    } else {
        return 0;
    }
    if (xs_is_pair(s, x) || xs_fp_kind(s, x) != XFP_NONE || cc_ssa_value_class(f, x) != CC_VAL_I64 ||
        (cc_ssa_value_size(f, x) > 4 && cc_ssa_value_size(f, x) != (uint32_t)s->word)) {
        return 0;
    }
    *v = x;
    switch (cc_ssa_value_size(f, x)) {
    case 1:
        /* (a narrow value as its type extends it, the cases the same) */
        *k = cc_ssa_value_is_unsigned(f, x) ? (long long)(unsigned char)kk : (long long)(signed char)kk;
        break;
    case 2:
        *k = cc_ssa_value_is_unsigned(f, x) ? (long long)(unsigned short)kk : (long long)(short)kk;
        break;
    case 4:
        *k = (long long)(int)kk;
        break;
    default:
        *k = kk;
        break;
    }
    *target = br->true_label;
    *next = br->false_label;
    return 1;
}

/**
 * Whether IR instructions @p from up to @p to (exclusive) of @p s are only what a case's block holds besides its
 * branch: its constants, read nowhere else, and the compare the branch reads.
 */
static int switch_case_block(const xsel_t *s, size_t from, size_t to) {
    const cc_ssa_function_t *f = s->f;
    size_t i;

    for (i = from; i < to; ++i) {
        const cc_ssa_instr_t *x = &f->instrs[i];

        if (x->op == CC_SSA_CONST && x->dst >= 0 && s->nuses[x->dst] <= 1) {
            continue;
        }
        if (x->op == CC_SSA_CMP && x->dst >= 0 && s->nuses[x->dst] == 1 && x->dst == f->instrs[to].lhs) {
            continue;
        }
        return 0;
    }
    return 1;
}

/** Fill in s->label_refs and s->label_at, once. */
static int switch_labels(xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    size_t n = f->label_count > 0 ? (size_t)f->label_count : 1;
    size_t i;

    if (s->label_refs != NULL) {
        return 0;
    }
    s->label_refs = (int *)calloc(n, sizeof(int));
    s->label_at = (long *)malloc(n * sizeof(long));
    if (s->label_refs == NULL || s->label_at == NULL) {
        s->xf.oom = 1;
        return -1;
    }
    for (i = 0; i < n; ++i) {
        s->label_at[i] = -1;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        size_t g;

#define REF(l) do { if ((l) >= 0 && (l) < f->label_count) s->label_refs[(l)]++; } while (0)
        if (in->op == CC_SSA_LABEL && in->label >= 0 && in->label < f->label_count) {
            s->label_at[in->label] = (long)i;
        } else if (in->op == CC_SSA_BR || in->op == CC_SSA_LADDR) {
            REF(in->label);
        } else if (in->op == CC_SSA_BR_COND) {
            REF(in->true_label);
            REF(in->false_label);
        } else if (in->op == CC_SSA_ASM) {
            for (g = 0; g < in->asm_goto_count; ++g) {
                REF(in->asm_goto_labels[g]);
            }
        }
#undef REF
    }
    return 0;
}

/**
 * At branch @p j, a switch's chain of compares of one value -- each case's block right after the last, reached only
 * from it -- as gcc makes one of enough cases dense enough: the value less the smallest case, checked against the
 * table's size (above it, the default), and a jump through the table, which print_function() writes in .rodata.
 * The chain's blocks after the first are skipped (s->skip_to).  Linux's ia32_sys_call(), a switch of 471 system
 * calls, was 471 compares and branches.  The entries are absolute on i386 and under -mcmodel=kernel, offsets from
 * the table in other x86-64 code, which may be linked position-independent; none under -fPIC on i386, nor under
 * -fno-jump-tables (Linux's code that runs before it is relocated).
 *
 * @return 1 if it was, else 0
 */
static int sel_switch_table(xsel_t *s, size_t j) {
    const cc_ssa_function_t *f = s->f;
    const cc_options_t *opts = cc_options_current();
    switch_case_t *cs = NULL;
    size_t n = 0;
    size_t cap = 0;
    size_t at = j;
    size_t last = j; /* the last case's branch */
    int v = -1;
    int def = -1;
    long long lo = 0;
    long long hi = 0;
    long long range;
    size_t i;
    int vsz;
    int w = s->word;

    if ((opts != NULL && ((opts->pic && w == 4) || opts->no_jump_tables)) || switch_labels(s) != 0) {
        return 0;
    }
    for (;;) {
        int x;
        long long k;
        int target;
        int next;
        long li;
        size_t b;

        if (!switch_case(s, at, &x, &k, &target, &next) || (v >= 0 && x != v)) {
            break;
        }
        if (n == cap) {
            switch_case_t *ncs;

            cap = cap > 0 ? cap * 2 : 16;
            ncs = (switch_case_t *)realloc(cs, cap * sizeof(*cs));
            if (ncs == NULL) {
                free(cs);
                s->xf.oom = 1;
                return 0;
            }
            cs = ncs;
        }
        v = x;
        cs[n].k = k;
        cs[n].target = target;
        n++;
        def = next;
        last = at;
        /* the next case: the block at next, right after this branch, reached only from it */
        if (next < 0 || next >= f->label_count || s->label_refs[next] != 1 || (li = s->label_at[next]) != (long)at + 1) {
            break;
        }
        for (b = (size_t)li + 1; b < f->instr_count && f->instrs[b].op != CC_SSA_BR_COND; ++b) {
            const cc_ssa_instr_t *x2 = &f->instrs[b];

            if (x2->op != CC_SSA_CONST && x2->op != CC_SSA_CMP) {
                break;
            }
        }
        if (b >= f->instr_count || f->instrs[b].op != CC_SSA_BR_COND || !switch_case_block(s, (size_t)li + 1, b)) {
            break;
        }
        at = b;
    }
    if (n < SWITCH_TABLE_MIN_CASES) {
        free(cs);
        return 0;
    }
    lo = hi = cs[0].k;
    for (i = 1; i < n; ++i) {
        lo = cs[i].k < lo ? cs[i].k : lo;
        hi = cs[i].k > hi ? cs[i].k : hi;
    }
    range = hi - lo + 1;
    vsz = (int)cc_ssa_value_size(f, v);
    if (range <= 0 || range > SWITCH_TABLE_MAX || range > (long long)n * SWITCH_TABLE_DENSITY ||
        !fits32(lo) || !fits32(range - 1)) {
        free(cs);
        return 0;
    }
    {
        size_t base = s->njt;
        int *tab;
        xop_t src = xs_src_word(s, v, 0);
        xop_t t = xs_temp(s);
        xop_t t4 = t;
        xop_t k;
        xinst_t x;

        if (s->njt + (size_t)range > s->jtcap) {
            size_t ncap = s->jtcap > 0 ? s->jtcap : 256;
            int *nj;

            while (ncap < s->njt + (size_t)range) {
                ncap *= 2;
            }
            nj = (int *)realloc(s->jt, ncap * sizeof(int));
            if (nj == NULL) {
                free(cs);
                s->xf.oom = 1;
                return 0;
            }
            s->jt = nj;
            s->jtcap = ncap;
        }
        tab = s->jt + base;
        for (i = 0; i < (size_t)range; ++i) {
            tab[i] = def;
        }
        /* the first case of a value wins, as the chain's compares have it */
        for (i = n; i-- > 0;) {
            tab[cs[i].k - lo] = cs[i].target;
        }
        s->njt += (size_t)range;
        free(cs);
        /* the index: the value less the smallest case, at its own width (a 32-bit move clears the rest) */
        if (vsz < 4) {
            /* a byte or a half, extended as its type is */
            t4.size = 4;
            if (src.kind == XO_REG) {
                src.size = (unsigned char)vsz;
            } else if (src.kind == XO_IMM) {
                src.size = 4;
            }
            {
                int uns = cc_ssa_value_is_unsigned(f, v);
                /* (or only moved, where every definition leaves it so extended: a byte zbl-loaded) */
                int done = (uns ? s->zbits[v] : s->sbits[v]) <= 8 * vsz;

                if (done && src.kind == XO_REG) {
                    src.size = 4;
                }
                xs_emit(s, src.kind == XO_IMM || done ? XI_MOV : uns ? XI_MOVZX : XI_MOVSX, 4, 2, &src, &t4, NULL);
            }
            vsz = 4;
        } else if (vsz == 4) {
            t4.size = 4;
            if (src.kind == XO_REG) {
                src.size = 4;
            }
            xs_emit(s, XI_MOV, 4, 2, &src, &t4, NULL);
        } else {
            xs_emit(s, XI_MOV, w, 2, &src, &t, NULL);
        }
        if (lo != 0) {
            k = xop_imm(vsz == 4 ? (long long)(int)lo : lo, vsz == 4 ? 4 : w);
            xs_emit(s, XI_SUB, vsz == 4 ? 4 : w, 2, &k, vsz == 4 ? &t4 : &t, NULL);
        }
        k = xop_imm(range - 1, vsz == 4 ? 4 : w);
        xs_emit(s, XI_CMP, vsz == 4 ? 4 : w, 2, &k, vsz == 4 ? &t4 : &t, NULL);
        x = xi_make(XI_JCC, w, 0, NULL, NULL, NULL);
        x.cc = XC_A;
        x.label = def;
        xf_emit(&s->xf, &x);
        x = xi_make(XI_JMPT, w, 1, &t, NULL, NULL);
        x.aux = (int)base;
        x.ntargets = (int)range;
        if (w == 8 && !(opts != NULL && opts->cmodel_kernel)) {
            /* (the table's entries offsets from it: the code may be linked position-independent) */
            x.cc = 1;
            x.clobber = XR_BIT(XR_R11);
        }
        xf_emit(&s->xf, &x);
        s->skip_to = last + 1;
    }
    return 1;
}

static void sel_instr(xsel_t *s, size_t j) {
    const cc_ssa_function_t *f = s->f;
    const cc_ssa_instr_t *in = &f->instrs[j];
    int w = s->word;
    long long k;
    const char *sym;

    if (j < s->skip_to) {
        return;
    }

    if (s->done != NULL && s->done[j]) {
        return; /* selected with an earlier instruction */
    }
    if (xs_fp_select(s, j)) {
        return; /* floating point (mir_fp.c) */
    }
    if (in->dst >= 0 && cc_ssa_value_class(f, in->dst) == CC_VAL_F64 && in->op != CC_SSA_PARAM &&
        in->op != CC_SSA_CALL && in->op != CC_SSA_CALLI && in->op != CC_SSA_STORE) {
        /* (a store's dst is no value it writes) */
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
    case CC_SSA_LADDR:
        if (in->op == CC_SSA_GADDR && in->sym == NULL) {
            NOPE(s);
        }
        if (xs_sym_of(s, in->dst, &sym)) {
            break; /* an immediate, or a %rip-relative address, where it is read */
        }
        if (in->op == CC_SSA_LADDR) {
            char buf[32];

            snprintf(buf, sizeof(buf), "_%d", in->label);
            sym = keep_name(".L", f->name);
            sym = sym != NULL ? keep_name(sym, buf) : NULL;
            if (sym == NULL) {
                s->xf.oom = 1;
                break;
            }
        } else if (in->op == CC_SSA_STR) {
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

        if (in->dst == in->lhs || (in->dst >= 0 && s->def_at[in->dst] == -2)) {
            break; /* (a copy of a compare made at the branch that reads it) */
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
        if (sel_carry_pair(s, j) || sel_double_shift(s, in)) {
            break;
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
        if (s->nuses[in->dst] == 1 && s->ndefs[in->dst] == 1 && branch_reads_later(s, j) > (int)j) {
            /* only a branch later in the block reads it, nothing between writing its operands: the compare is
             * made there, and the branch is on the flags */
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
        if (s->flag_br != (long)j && sel_switch_table(s, j)) {
            break;
        }
        if (s->flag_br == (long)j) {
            /* an asm's flag output: on the flags it left (asm_flag_branch()) */
            s->flag_br = -1;
            branch(s, j, (xcc_t)s->flag_cc, in->true_label, in->false_label);
            break;
        }
        if (in->lhs >= 0 && s->def_at[in->lhs] == -2) {
            /* the compare deferred to here */
            const cc_ssa_instr_t *c = &f->instrs[s->cmp_at[in->lhs]];
            xcc_t cc;
            int known = 0;

            if (compare(s, c, &cc, &known)) {
                jump(s, j, known ? in->true_label : in->false_label);
            } else {
                branch(s, j, cc, in->true_label, in->false_label);
            }
            break;
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
        if (sel_mask_test(s, in->lhs)) {
            /* if (x & k) */
            branch(s, j, XC_NE, in->true_label, in->false_label);
            break;
        }
        {
            int bytes = br_test_bytes(s, in->lhs);
            xop_t v = xop_reg(xs_vreg_of(s, in->lhs, 0), bytes > 0 ? bytes : w);

            /* (of its own bits: what is above them may be anything, compute_demand() having said so) */
            xs_emit(s, XI_TEST, bytes > 0 ? bytes : w, 2, &v, &v, NULL);
            branch(s, j, XC_NE, in->true_label, in->false_label);
        }
        break;

    case CC_SSA_LOAD:
        if (cc_ssa_value_class(f, in->dst) == CC_VAL_F64 ||
            (f->value_ty[in->dst].kind == CC_TY_AGG && (in->imm <= 0 || in->imm > w))) {
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

    case CC_SSA_CHAIN: {
        /* a nested function's environment, in the static chain register as the function was entered */
        xop_t r = xop_reg(s->t->chain_reg, w);
        xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), w);

        xs_emit(s, XI_MOV, w, 2, &r, &d, NULL);
        break;
    }

    case CC_SSA_DYNALLOC:
        if (in->lhs < 0 || xs_is_pair(s, in->lhs)) {
            NOPE(s);
        }
        sel_dynalloc(s, in);
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

    (void)v;
    for (i = 0; i < f->instr_count; ++i) {
        switch (f->instrs[i].op) {
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
    s->got = XR_NONE;
    s->flag_br = -1;
    s->lo = (int *)malloc(n * sizeof(int));
    s->hi = (int *)malloc(n * sizeof(int));
    s->ndefs = (int *)calloc(n, sizeof(int));
    s->nuses = (int *)calloc(n, sizeof(int));
    s->def_at = (int *)malloc(n * sizeof(int));
    s->addr_taken = (unsigned char *)calloc(n, 1);
    s->is_addr = (unsigned char *)calloc(n, 1);
    s->obj = (int *)malloc(n * sizeof(int));
    s->fhome = (int *)malloc(n * sizeof(int));
    s->cmp_at = (int *)malloc(n * sizeof(int));
    s->frame_val = (int *)malloc(n * sizeof(int));
    s->param_lo = (int *)malloc(np * sizeof(int));
    s->param_hi = (int *)malloc(np * sizeof(int));
    s->param_obj = (int *)malloc(np * sizeof(int));
    if (s->lo == NULL || s->hi == NULL || s->ndefs == NULL || s->nuses == NULL || s->def_at == NULL ||
        s->addr_taken == NULL || s->is_addr == NULL || s->obj == NULL || s->fhome == NULL || s->cmp_at == NULL ||
        s->frame_val == NULL ||
        s->param_lo == NULL ||
        s->param_hi == NULL || s->param_obj == NULL) {
        return -1;
    }
    for (v = 0; v < (int)n; ++v) {
        s->lo[v] = XR_NONE;
        s->hi[v] = XR_NONE;
        s->def_at[v] = -1;
        s->obj[v] = -1;
        s->fhome[v] = -1;
        s->cmp_at[v] = -1;
        s->frame_val[v] = -1;
    }
    s->fscratch = -1;
    s->fcw = -1;
    for (i = 0; i < np; ++i) {
        s->param_lo[i] = XR_NONE;
        s->param_hi[i] = XR_NONE;
        s->param_obj[i] = -1;
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
        if (in->op == CC_SSA_ASM && in->asm_is_goto) {
            /*
             * an asm goto's output holds on every way out of it, its labels' as well as falling through: it
             * lives in its frame object, which the asm writes, as a value whose address is taken does
             */
            size_t o;

            for (o = 0; o < in->asm_out_count; ++o) {
                int ov = in->asm_out_values[o];

                if (!CC_SSA_ASM_MEM_INDIRECT_P(ov) && ov >= 0 && ov < f->value_count) {
                    s->addr_taken[ov] = 1;
                }
            }
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
        if (s->addr_taken[v] && xs_fp_kind(s, v) != XFP_NONE) {
            /* a floating value: its double's 8 bytes, or a long double's 16 -- an x87 one's home */
            int bytes = value_size_bytes(f, v, 8) >= 10 ? 16 : 8;

            s->obj[v] = xf_new_obj(&s->xf, XF_LOCAL, bytes, bytes, 0);
            if (xs_fp_kind(s, v) == XFP_X87) {
                s->fhome[v] = s->obj[v];
            }
        } else if (s->addr_taken[v]) {
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
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_DYNALLOC && in->rhs >= 0 && in->rhs < f->value_count && s->obj[in->rhs] >= 0) {
            /* a save slot: no earlier allocation to give back the first time */
            xop_t z = xop_imm(0, s->word);
            xop_t m = xop_frame(s->obj[in->rhs], 0, s->word);

            xs_emit(s, XI_MOV, s->word, 2, &z, &m, NULL);
        }
    }
    if (compute_demand(s) != 0 || compute_ext(s) != 0) {
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
    free(s->jt);
    free(s->label_refs);
    free(s->label_at);
    free(s->sbits);
    free(s->dbits);
    free(s->ext_copy);
    free(s->done);
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
    free(s->fhome);
    free(s->cmp_at);
    free(s->frame_val);
    free(s->param_lo);
    free(s->param_hi);
    free(s->param_obj);
    xf_free(&s->xf);
}

/** Whether the flags may be read after instruction @p i before anything sets them again. */
static int flags_live_after(const xfunc_t *xf, int i) {
    int k;

    for (k = i + 1; k < xf->nins; ++k) {
        switch (xf->ins[k].op) {
        case XI_JCC:
        case XI_SETCC:
        case XI_ADC:
        case XI_SBB:
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
 * Whether the flags instruction @p i of @p xf leaves are read, up to what sets them again, only by tests of the zero
 * and sign flags (je, jne, js, jns and their setcc): those an add or a subtract sets as a test of its result would.
 */
static int flags_read_zs_only(const xfunc_t *xf, int i) {
    int k;

    for (k = i + 1; k < xf->nins; ++k) {
        const xinst_t *in = &xf->ins[k];

        switch (in->op) {
        case XI_JCC:
        case XI_SETCC:
            if (in->cc != XC_E && in->cc != XC_NE && in->cc != XC_S && in->cc != XC_NS) {
                return 0;
            }
            continue;
        case XI_MOV:
        case XI_LEA:
        case XI_MOVZX:
        case XI_MOVSX:
        case XI_PUSH:
            continue;
        case XI_ADC:
        case XI_SBB:
        case XI_LABEL:
            return 0; /* (a label: a reader past it may be reached with these flags too) */
        default:
            return 1; /* sets them again, or no reader is left */
        }
    }
    return 1;
}

/**
 * Whether `test r, r`, instruction @p i of @p xf, tests what the instruction before it -- past copies that leave r
 * be -- has just set the flags from as the test would: an and, or or xor of r always, an add or subtract when only
 * the zero and sign flags are read.  Then the test is not needed: `and $k, r; test r, r` was two flag settings.
 */
static int test_redundant(const xfunc_t *xf, int i) {
    const xinst_t *t = &xf->ins[i];
    int r;
    int k;

    if (t->op != XI_TEST || t->nops != 2 || t->o[0].kind != XO_REG || t->o[1].kind != XO_REG ||
        t->o[0].reg != t->o[1].reg || t->o[0].size != t->o[1].size || XR_IS_XMM(t->o[0].reg)) {
        return 0;
    }
    r = t->o[0].reg;
    for (k = i - 1; k >= 0; --k) {
        const xinst_t *p = &xf->ins[k];

        if (p->op == XI_MOV || p->op == XI_LEA || p->op == XI_MOVZX || p->op == XI_MOVSX) {
            /* a copy leaves the flags be: past it, unless it writes r */
            if (p->nops >= 2 && p->o[p->nops - 1].kind == XO_REG && p->o[p->nops - 1].reg == r) {
                return 0;
            }
            continue;
        }
        if ((p->op == XI_AND || p->op == XI_OR || p->op == XI_XOR || p->op == XI_ADD || p->op == XI_SUB) &&
            p->nops == 2 && p->o[1].kind == XO_REG && p->o[1].reg == r && p->o[1].size == t->o[1].size &&
            p->w == t->w) {
            return p->op == XI_AND || p->op == XI_OR || p->op == XI_XOR || flags_read_zs_only(xf, i);
        }
        return 0;
    }
    return 0;
}

/** Whether @p label is one of the labels at instruction @p i of @p xf on, before the next one not a label. */
static int label_here(const xfunc_t *xf, int i, int label) {
    for (; i < xf->nins && xf->ins[i].op == XI_LABEL; ++i) {
        if (xf->ins[i].label == label) {
            return 1;
        }
    }
    return 0;
}

/**
 * After allocation, when copies have gone and blocks are empty that were not at selection: a label nothing jumps
 * to goes (none does with @p labels_taken: a computed goto may reach any), code after a jump, a return or a trap
 * up to the next label left; a jump or a branch to a label that comes next goes; and a branch over a jump,
 * `jcc 1f; jmp 2f; 1:`, is the one branch the other way, `jncc 2f` -- over a return, print_function()'s branch to
 * the epilogue.
 */
static void tidy_branches(xfunc_t *xf, int labels_taken) {
    int *refs = (int *)calloc((size_t)(xf->nlabels > 0 ? xf->nlabels : 1), sizeof(int));
    int changed = 1;
    int round;

    for (round = 0; changed && round < 8; ++round) {
        int i;
        int j = 0;

        changed = 0;
        if (refs != NULL) {
            memset(refs, 0, (size_t)(xf->nlabels > 0 ? xf->nlabels : 1) * sizeof(int));
            for (i = 0; i < xf->nins; ++i) {
                const xinst_t *in = &xf->ins[i];
                int k;

                if ((in->op == XI_JMP || in->op == XI_JCC) && in->label >= 0 && in->label < xf->nlabels) {
                    refs[in->label]++;
                }
                for (k = 0; (in->op == XI_ASM || in->op == XI_JMPT) && k < in->ntargets; ++k) {
                    if (in->targets[k] >= 0 && in->targets[k] < xf->nlabels) {
                        refs[in->targets[k]]++;
                    }
                }
            }
        }
        for (i = 0; i < xf->nins; ++i) {
            xinst_t *in = &xf->ins[i];

            if (in->op == XI_LABEL && !labels_taken && refs != NULL && in->label >= 0 && in->label < xf->nlabels &&
                refs[in->label] == 0) {
                /* (nothing jumps to it: what it separates are one block) */
                changed = 1;
                continue;
            }
            if ((in->op == XI_JMP || in->op == XI_JCC) && label_here(xf, i + 1, in->label)) {
                changed = 1;
                continue;
            }
            if (in->op == XI_JCC && i + 2 < xf->nins && xf->ins[i + 1].op == XI_JMP &&
                xf->ins[i + 1].label != in->label && label_here(xf, i + 2, in->label)) {
                in->cc = (unsigned char)xcc_negate((xcc_t)in->cc);
                in->label = xf->ins[i + 1].label;
                xf->ins[j++] = *in;
                ++i;
                changed = 1;
                continue;
            }
            xf->ins[j++] = *in;
            if (in->op == XI_JMP || in->op == XI_JMPT || in->op == XI_RET || in->op == XI_TRAP) {
                while (i + 1 < xf->nins) {
                    const xinst_t *nx = &xf->ins[i + 1];

                    if (nx->op == XI_LABEL && (labels_taken || refs == NULL || nx->label < 0 ||
                                               nx->label >= xf->nlabels || refs[nx->label] > 0)) {
                        break;
                    }
                    ++i;
                    changed = 1;
                }
            }
        }
        xf->nins = j;
    }
    free(refs);
}

/** Whether function @p f takes a label's address (&&label), which a computed goto may then jump to. */
static int takes_label_address(const cc_ssa_function_t *f) {
    size_t j;

    for (j = 0; j < f->instr_count; ++j) {
        if (f->instrs[j].op == CC_SSA_LADDR) {
            return 1;
        }
    }
    return 0;
}

/**
 * After allocation: branches tidied (tidy_branches(), @p labels_taken as it has it), a copy and an add become one
 * lea, a move of 0 into a register an xorl, and on x86-64 a move of a constant that fits 32 bits unsigned the
 * 32-bit move, which zero-extends.
 */
static void peephole(xfunc_t *xf, int word, int labels_taken) {
    int i;
    int j = 0;

    tidy_branches(xf, labels_taken);

    /* first, on its own: a test of what the flags were just set from (what follows may read them) */
    for (i = 0; i < xf->nins; ++i) {
        if (!test_redundant(xf, i)) {
            xf->ins[j++] = xf->ins[i];
        }
    }
    xf->nins = j;
    j = 0;
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
    const int *sv = s->fpo ? s->fp_saved : t->saved;
    int nsv = s->fpo ? s->fp_nsaved : t->nsaved;
    int nsaved = 0;
    int r;

    for (r = 0; r < nsv; ++r) {
        nsaved += (saved & XR_BIT(sv[r])) != 0;
    }
    if (emit_debug) {
        fputs("\t.cfi_remember_state\n", fp);
    }
    if (s->fpo) {
        /* no frame pointer: the frame given back, the saved registers popped */
        if (frame > 0) {
            fprintf(fp, "\tadd%c $%d, %s\n", sfx, frame, sp);
        }
        for (r = nsv - 1; r >= 0; --r) {
            if ((saved & XR_BIT(sv[r])) != 0) {
                fprintf(fp, "\tpop%c %s\n", sfx, xr_name(sv[r], w));
            }
        }
    } else if (nsaved == 0) {
        if (frame > 0 || reset) {
            fputs("\tleave\n", fp);
        } else {
            fprintf(fp, "\tpop%c %s\n", sfx, bp);
        }
    } else {
        if (frame > 0 || reset) {
            fprintf(fp, "\tlea%c %d(%s), %s\n", sfx, -w * nsaved, bp, sp);
        }
        for (r = nsv - 1; r >= 0; --r) {
            if ((saved & XR_BIT(sv[r])) != 0) {
                fprintf(fp, "\tpop%c %s\n", sfx, xr_name(sv[r], w));
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
    {
        /* bound as the selector bound it: the same plan, made again */
        const cc_ssa_instr_t *ai = &f->instrs[in->ir_index];
        x86_asm_plan_t plan;
        int prc = x86_asm_plan(f, ai, (size_t)in->ir_index, s->word == 8, &plan);
        int bound = prc == 0 && asm_bindable((xsel_t *)s, ai, &plan);

        if (bound) {
            size_t k;

            asm_plan_symbols((xsel_t *)s, ai, &plan);
            for (k = 0; k < plan.n && k < 8; ++k) {
                if ((in->cc & (1u << k)) != 0) {
                    plan.kind[k] = X86_ASM_FLAGS;
                }
            }
        }

        rc = prc < 0 ? -1
                     : emit_inline_asm(fp, f, &lay, ai, s->fn_index, (size_t)in->ir_index, s->word == 8,
                                       bound ? &plan : NULL, diag);
        if (prc == 0) {
            x86_asm_plan_free(&plan);
        }
    }
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
    const int *sv = s->fpo ? s->fp_saved : t->saved; /* the callee-saved registers, in the order pushed */
    int nsv = s->fpo ? s->fp_nsaved : t->nsaved;
    unsigned callee = 0;
    unsigned saved = 0;
    int nsaved = 0;
    int frame = 0;
    int last_ret;
    int nrets;
    int share;
    size_t j;
    int i;
    int r;

    for (r = 0; r < nsv; ++r) {
        callee |= XR_BIT(sv[r]);
    }
    for (i = 0; i < s->xf.nins; ++i) {
        const xinst_t *in = &s->xf.ins[i];
        int k;

        /* (and what an asm reads, writes or clobbers: its registers are its operands') */
        saved |= in->imp_use | in->imp_def | in->clobber;
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
    for (r = 0; r < nsv; ++r) {
        nsaved += (saved & XR_BIT(sv[r])) != 0;
    }
    /* (with no frame pointer, the first register saved where it would have been) */
    if (layout_frame(s, &s->xf, w * nsaved - (s->fpo ? w : 0), &frame) != 0) {
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
    if (!s->fpo) {
        fprintf(fp, "\tpush%c %s\n\tmov%c %s, %s\n", sfx, xr_name(XR_BP, w), sfx, xr_name(XR_SP, w),
                xr_name(XR_BP, w));
    }
    if (emit_debug) {
        fprintf(fp, "\t.cfi_def_cfa_register %s\n", xr_name(XR_BP, w));
        if (src_path != NULL) {
            fputs("\t.loc 1 1 0\n", fp);
        }
    }
    {
        int off = -2 * w;

        for (r = 0; r < nsv; ++r) {
            if ((saved & XR_BIT(sv[r])) != 0) {
                off -= w;
                fprintf(fp, "\tpush%c %s\n", sfx, xr_name(sv[r], w));
                if (emit_debug) {
                    fprintf(fp, "\t.cfi_offset %s, %d\n", xr_name(sv[r], w), off);
                }
            }
        }
    }
    if (frame > 0) {
        fprintf(fp, "\tsub%c $%d, %s\n", sfx, frame, xr_name(XR_SP, w));
    }
    /* frame objects off the stack pointer: where the frame pointer would be is this far above it */
    s->xf.fp_omit = s->fpo;
    s->xf.fp_bias = s->fpo ? (long)(nsaved - 1) * w + frame : 0;
    s->xf.sp_depth = 0;
    /*
     * one epilogue, at the last return, the others jumping to it, as gcc keeps it -- when it is more than the
     * two bytes of `leave; ret` a jump takes as well
     */
    last_ret = -1;
    nrets = 0;
    for (i = 0; i < s->xf.nins; ++i) {
        if (s->xf.ins[i].op == XI_RET) {
            last_ret = i;
            nrets++;
        }
    }
    share = nrets > 1 && nsaved > 0;
    for (i = 0; i < s->xf.nins; ++i) {
        const xinst_t *in = &s->xf.ins[i];

        if (in->op == XI_LABEL) {
            emit_local_label(fp, f->name, in->label);
            fputs(":\n", fp);
            s->xf.sp_depth = 0; /* (a call's pushes never span a label) */
        } else if (in->op == XI_JCC && share && i + 1 < last_ret && s->xf.ins[i + 1].op == XI_RET &&
                   label_here(&s->xf, i + 2, in->label)) {
            /* a branch over a return: the other way to the epilogue */
            fprintf(fp, "\tj%s .L%s_epilogue\n", xcc_name(xcc_negate((xcc_t)in->cc)), f->name);
            ++i;
        } else if (in->op == XI_RET && share && i != last_ret) {
            int k = i + 1;

            while (k < last_ret && s->xf.ins[k].op == XI_LABEL) {
                ++k;
            }
            if (k != last_ret) {
                fprintf(fp, "\tjmp .L%s_epilogue\n", f->name);
            }
        } else if (in->op == XI_RET) {
            if (share) {
                fprintf(fp, ".L%s_epilogue:\n", f->name);
            }
            print_epilogue(fp, s, saved, frame, s->has_goto || s->has_dynalloc, in->aux, emit_debug);
        } else if (in->op == XI_ASM) {
            if (print_asm(fp, s, in, diag) != 0) {
                return -1;
            }
        } else if (in->op == XI_TRAP) {
            fputs(w == 8 || i386_has_ud2() ? "\tud2\n" : "\tint3\n", fp);
        } else {
            xi_print(fp, &s->xf, in, w, f->name);
        }
        /* what moved the stack pointer: a push, an adjustment, the arguments a callee pops */
        if (in->op == XI_PUSH) {
            s->xf.sp_depth += w;
        } else if (in->op == XI_ADDSP) {
            s->xf.sp_depth -= (long)in->o[0].disp;
        } else if (in->op == XI_CALL) {
            s->xf.sp_depth -= in->aux;
        }
    }
    if (emit_debug) {
        fputs("\t.cfi_endproc\n", fp);
    }
    fprintf(fp, ".size %s, .-%s\n", f->name, f->name);
    for (i = 0; i < s->xf.nins; ++i) {
        /* the switches' jump tables, in .rodata as gcc puts them */
        const xinst_t *in = &s->xf.ins[i];
        int k;

        if (in->op != XI_JMPT) {
            continue;
        }
        fprintf(fp, "\t.pushsection .rodata\n\t.p2align %d\n.L%s_jt%d:\n", w == 8 && !in->cc ? 3 : 2, f->name,
                in->aux);
        for (k = 0; k < in->ntargets; ++k) {
            fprintf(fp, "\t%s ", w == 8 && !in->cc ? ".quad" : ".long");
            emit_local_label(fp, f->name, in->targets[k]);
            if (in->cc) {
                fprintf(fp, "-.L%s_jt%d", f->name, in->aux);
            }
            fputc('\n', fp);
        }
        fputs("\t.popsection\n", fp);
    }
    if ((f->attr_flags & CC_ATTR_CONSTRUCTOR) != 0) {
        emit_lifecycle_attr(fp, ".init_array", "init_array", f->name, w == 8);
    }
    if ((f->attr_flags & CC_ATTR_DESTRUCTOR) != 0) {
        emit_lifecycle_attr(fp, ".fini_array", "fini_array", f->name, w == 8);
    }
    return 0;
}

/** Whether register read @p in reads the stack pointer, `register unsigned long sp asm("esp")`. */
static int reads_stack_pointer(const cc_ssa_instr_t *in) {
    const char *r = in->sym;

    if (r == NULL) {
        return 0;
    }
    if (r[0] == '%') {
        r++;
    }
    return strcmp(r, "esp") == 0 || strcmp(r, "rsp") == 0 || strcmp(r, "sp") == 0;
}

/**
 * Whether the function @p s selected may do without a frame pointer: nothing reads the frame's address or the frame
 * pointer itself (__builtin_frame_address, a register variable of %ebp, va_start's area), nothing moves the stack
 * pointer but calls (alloca, a variable-length array), and each inline asm has its operands in registers where
 * the plan put them (an asm goto, one bound through slots, or a memory operand in a slot is printed off the frame
 * pointer).
 */
static int fpo_ok(xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    size_t j;
    int i;

    if (s->has_dynalloc || s->has_goto || f->is_variadic) {
        return 0;
    }
    for (j = 0; j < f->instr_count; ++j) {
        const cc_ssa_instr_t *in = &f->instrs[j];

        if (in->op == CC_SSA_FRAMEADDR || in->op == CC_SSA_VA_START) {
            return 0;
        }
        if (in->op == CC_SSA_REGREAD && !reads_stack_pointer(in)) {
            /* (the stack pointer read is the register as it is; the frame pointer is another) */
            return 0;
        }
        if (in->op == CC_SSA_ASM) {
            x86_asm_plan_t plan;
            int rc = x86_asm_plan(f, in, j, s->word == 8, &plan);
            int ok = rc == 0 && asm_bindable(s, in, &plan);
            size_t k;

            /* (and none in a slot: emit_inline_asm() writes a slot off the frame pointer) */
            for (k = 0; ok && k < plan.n; ++k) {
                ok = plan.kind[k] != X86_ASM_SLOT;
            }
            if (rc == 0) {
                x86_asm_plan_free(&plan);
            }
            if (!ok) {
                return 0;
            }
        }
    }
    for (i = 0; i < s->xf.nins; ++i) {
        const xinst_t *in = &s->xf.ins[i];
        int k;

        if (((in->imp_use | in->imp_def | in->clobber) & XR_BIT(XR_BP)) != 0) {
            return 0;
        }
        for (k = 0; k < in->nops; ++k) {
            const xop_t *o = &in->o[k];

            if ((o->kind == XO_REG && o->reg == XR_BP) ||
                (o->kind == XO_MEM && (o->base == XR_BP || o->index == XR_BP))) {
                return 0;
            }
        }
    }
    return 1;
}

/**
 * Whether @p nofp, a function allocated with no frame pointer, comes out smaller than @p fp, the same allocated
 * with one, by an estimate in bytes: an instruction about four, a frame access off the stack pointer one more (its
 * SIB byte), and the frame pointer's push, move and pop -- four bytes on i386, five on x86-64 -- saved, but for
 * the push and pop of %ebp (%rbp) when it is allocated.  On i386, short of registers, its seventh is worth more
 * than that costs; on x86-64 the stack pointer's byte on every frame access often is not.
 */
static int fpo_smaller(const xfunc_t *fp, const xfunc_t *nofp, int word) {
    long frame_refs = 0;
    int bp_used = 0;
    int i;
    int k;

    for (i = 0; i < nofp->nins; ++i) {
        const xinst_t *in = &nofp->ins[i];

        for (k = 0; k < in->nops; ++k) {
            const xop_t *o = &in->o[k];

            frame_refs += o->kind == XO_MEM && o->base == XR_FRAME;
            bp_used |= (o->kind == XO_REG && o->reg == XR_BP) ||
                       (o->kind == XO_MEM && (o->base == XR_BP || o->index == XR_BP));
        }
    }
    return 4L * nofp->nins + frame_refs + (bp_used ? 2 : 0) < 4L * fp->nins + (word == 8 ? 5 : 4);
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
    for (j = 0; rc == 0 && j < (size_t)s.xf.nins; ++j) {
        /* (the tables grow as they are made: a jump through one points into it now) */
        if (s.xf.ins[j].op == XI_JMPT) {
            s.xf.ins[j].targets = s.jt + s.xf.ins[j].aux;
        }
    }
    if (rc == 0 && (s.unsupported || s.xf.oom)) {
        rc = s.xf.oom ? -1 : 1;
    }
    if (rc == 0) {
        xfunc_t alt;
        int try_fpo = cc_options_current()->omit_frame_pointer && !emit_debug && fpo_ok(&s) &&
                      xf_copy(&alt, &s.xf) == 0;

        rc = x86_mir_allocate(&s.xf, t->alloc);
        if (try_fpo) {
            /* the frame pointer's register allocated too, last, saved by the prologue as the others are */
            xtarget_t ta = *t->alloc;
            int order[XR_NPHYS + 1];
            int k;
            int arc;

            for (k = 0; k < ta.norder && k < XR_NPHYS; ++k) {
                order[k] = ta.order[k];
            }
            order[k++] = XR_BP;
            ta.order = order;
            ta.norder = k;
            ta.alloc_mask |= XR_BIT(XR_BP);
            ta.callee_saved |= XR_BIT(XR_BP);
            arc = x86_mir_allocate(&alt, &ta);
            if (arc == 0 && (rc != 0 || fpo_smaller(&s.xf, &alt, t->word))) {
                xf_free(&s.xf);
                s.xf = alt;
                rc = 0;
                s.fpo = 1;
                for (k = 0; k < t->nsaved; ++k) {
                    s.fp_saved[k] = t->saved[k];
                }
                s.fp_saved[k++] = XR_BP;
                s.fp_nsaved = k;
            } else {
                xf_free(&alt);
            }
        }
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
        peephole(&s.xf, t->word, takes_label_address(f));
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
