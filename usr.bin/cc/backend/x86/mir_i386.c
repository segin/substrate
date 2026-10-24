/*
 * mir_i386.c: i386 code through machine IR and the register allocator.
 *
 * Instruction selection from legal IR into machine IR (cc_x86_mir.h):
 * each IR word a virtual register (a 64-bit integer two, its low and high
 * words), the arguments and results of calls and the operands of mull,
 * idivl and shifts in the physical registers the psABI and the
 * instructions name.  A constant defined once is an immediate wherever
 * it is read; so is a global's or a string's address; a stack object's
 * address is folded into the loads and stores that use it, and so is a
 * pointer plus a constant (a structure's field), when the pointer cannot
 * change in between.  A compare that only a branch right after it reads
 * is that branch's flags.  A value whose address is taken lives in its
 * frame object; one passed on the stack and never written again lives in
 * its incoming slot when it is spilled.
 *
 * linscan.c gives out %eax, %ecx, %edx, %ebx, %esi and %edi; %ebp is the
 * frame pointer.  The frame is laid out afterwards: the callee-saved
 * registers the function uses pushed under %ebp, then the frame objects,
 * the busiest nearest.
 *
 * What it does not handle yet -- floating point, structures passed or
 * returned by value, inline asm, va_start, alloca, -fPIC and the rest --
 * returns 1 before anything is written, and i386.c emits the function
 * its own way.
 */

#include "cc_x86_mir.h"

#include "cc_options.h"
#include "cc_target.h"
#include "cc_x86.h"

#include <stdlib.h>
#include <string.h>

/** The allocation order: the caller-saved registers first, which cost no save. */
static const int i386_order[] = {XR_AX, XR_CX, XR_DX, XR_BX, XR_SI, XR_DI};

/** The registers a call clobbers. */
#define I386_CALLER_SAVED (XR_BIT(XR_AX) | XR_BIT(XR_CX) | XR_BIT(XR_DX))

/** The regparm registers, in the order arguments take them. */
static const int i386_rp_regs[3] = {XR_AX, XR_DX, XR_CX};

/** Selection state for one function. */
typedef struct {
    xfunc_t xf;
    const cc_ssa_module_t *m;
    const cc_ssa_function_t *f;
    size_t fn_index;
    int *lo;                  /**< per IR value: the virtual register of its (low) word, or XR_NONE */
    int *hi;                  /**< per IR value: of its high word (a 64-bit integer), or XR_NONE */
    int *ndefs;               /**< per IR value: how many instructions write it */
    int *nuses;               /**< and read it */
    int *def_at;              /**< per IR value written once: that instruction, else -1 */
    unsigned char *addr_taken;/**< per IR value: an ADDR takes its address */
    unsigned char *is_addr;   /**< per IR value: a STACKALLOC writes it -- an address, a word whatever its type */
    int *obj;                 /**< per IR value: its frame object (a stack allocation's, or the memory it lives in) */
    int *frame_val;           /**< per IR value: the frame object it is the address of, when written once; else -1 */
    int *param_lo;            /**< per parameter that came in a register: the register holding it from entry */
    int *param_hi;
    int unsupported;          /**< the line of this file that gave up on the function, for CC_MIR_STATS; or 0 */
    struct asm_bind *bind;    /**< each inline asm's operands and where they are (XI_ASM's aux: its first) */
    int nbind;
    int bindcap;
    int *asm_slot;            /**< the frame objects inline asms take their operands in, the k-th operand in the k-th */
    int nasm_slot;
    int has_goto;             /**< the function has an asm goto */
    int hidden;               /**< a structure result: the register holding the caller's buffer's address */
} sel_t;

/** Where an inline asm finds an operand's value: its frame object.  A value of -1 ends an asm's list. */
typedef struct asm_bind {
    int v;
    int obj;
    unsigned char in;  /**< copied there before the asm */
    unsigned char out; /**< and back after it */
} asm_bind_t;

/** Give up on the function: something it does is not handled here. */
#define NOPE(s)                         \
    do {                                \
        (s)->unsupported = __LINE__;    \
        return;                         \
    } while (0)

/** Whether IR value @p v is a 64-bit integer: two words. */
static int is_pair(const sel_t *s, int v) {
    return v >= 0 && s->f->value_ty[v].kind == CC_TY_INT && s->f->value_ty[v].size == 8 && !s->is_addr[v];
}

/** Whether IR value @p v is defined once by an integer constant, which is then *@p k. */
static int const_of(const sel_t *s, int v, long long *k) {
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
static void str_label(const sel_t *s, size_t j, char *buf, size_t size) {
    snprintf(buf, size, ".L__cc_str_%zu_%zu", s->fn_index, j);
}

/** Interned names of string literals' labels, which operands point at. */
typedef struct {
    char **names;
    size_t n;
    size_t cap;
} strpool_t;

static strpool_t i386_strs;

/** A copy of @p name that lives until the function is printed, or NULL when out of memory. */
static const char *keep_name(const char *name) {
    size_t len = strlen(name);
    char *c = (char *)malloc(len + 1);

    if (c == NULL) {
        return NULL;
    }
    memcpy(c, name, len + 1);
    if (i386_strs.n == i386_strs.cap) {
        size_t ncap = i386_strs.cap > 0 ? i386_strs.cap * 2 : 16;
        char **nn = (char **)realloc(i386_strs.names, ncap * sizeof(*nn));

        if (nn == NULL) {
            free(c);
            return NULL;
        }
        i386_strs.names = nn;
        i386_strs.cap = ncap;
    }
    i386_strs.names[i386_strs.n++] = c;
    return c;
}

/** Free the names keep_name() made. */
static void free_names(void) {
    size_t i;

    for (i = 0; i < i386_strs.n; ++i) {
        free(i386_strs.names[i]);
    }
    i386_strs.n = 0;
}

/**
 * Whether IR value @p v is defined once as a symbol's address (a global's, or a string literal's): the symbol
 * goes to *@p sym.
 */
static int sym_of(sel_t *s, int v, const char **sym) {
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
        *sym = keep_name(buf);
        if (*sym == NULL) {
            s->xf.oom = 1;
            return 0;
        }
        return 1;
    }
    return 0;
}

/** The virtual register of word @p part of IR value @p v, made the first time it is asked for. */
static int vreg_of(sel_t *s, int v, int part) {
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

/** Append an instruction. */
static void emit(sel_t *s, xi_op_t op, int w, int n, const xop_t *a, const xop_t *b, const xop_t *c) {
    xinst_t in = xi_make(op, w, n, a, b, c);

    xf_emit(&s->xf, &in);
}

/** A new virtual register as a 4-byte operand. */
static xop_t temp(sel_t *s) {
    return xop_reg(xf_new_vreg(&s->xf), 4);
}

/**
 * Word @p part of IR value @p v as a source operand: an immediate for a constant or a symbol's address, else a
 * register (a stack object's address made into one with leal).
 */
static xop_t src_word(sel_t *s, int v, int part) {
    long long k;
    const char *sym;

    if (const_of(s, v, &k)) {
        return xop_imm(part ? (long long)(int)(k >> 32) : (long long)(int)k, 4);
    }
    if (!part && sym_of(s, v, &sym)) {
        return xop_sym(sym, 0, 4);
    }
    if (part && sym_of(s, v, &sym)) {
        return xop_imm(0, 4);
    }
    if (!part && s->frame_val[v] >= 0) {
        xop_t t = temp(s);
        xop_t m = xop_frame(s->frame_val[v], 0, 4);

        emit(s, XI_LEA, 4, 2, &m, &t, NULL);
        return t;
    }
    return xop_reg(vreg_of(s, v, part), 4);
}

/** The same in a register: an immediate moved into a new one. */
static xop_t reg_word(sel_t *s, int v, int part) {
    xop_t o = src_word(s, v, part);

    if (o.kind == XO_IMM) {
        xop_t t = temp(s);

        emit(s, XI_MOV, 4, 2, &o, &t, NULL);
        return t;
    }
    return o;
}

/** Whether IR value @p v keeps its value from where it is written to every later read: it is written once. */
static int stable(const sel_t *s, int v) {
    return v >= 0 && s->ndefs[v] == 1 && s->def_at[v] >= 0;
}

/**
 * The memory at IR address value @p v plus @p disp, @p size bytes: off the frame for a stack object or a value
 * in memory, a symbol for a global, a constant address, or a pointer plus a constant folded into the
 * displacement when the pointer cannot change; else off the address's register.
 */
static xop_t mem_at(sel_t *s, int v, long long disp, int size, int depth) {
    long long k;
    const char *sym;
    xop_t o;

    if (s->frame_val[v] >= 0) {
        return xop_frame(s->frame_val[v], disp, size);
    }
    if (sym_of(s, v, &sym)) {
        o = xop_mem(XR_NONE, disp, size);
        o.sym = sym;
        return o;
    }
    if (const_of(s, v, &k)) {
        return xop_mem(XR_NONE, (long long)(int)k + disp, size);
    }
    if (depth < 8 && stable(s, v)) {
        const cc_ssa_instr_t *d = &s->f->instrs[s->def_at[v]];

        if (d->op == CC_SSA_ADD && d->lhs >= 0 && d->rhs >= 0 && !is_pair(s, v)) {
            if (const_of(s, d->rhs, &k) && (stable(s, d->lhs) || s->frame_val[d->lhs] >= 0)) {
                return mem_at(s, d->lhs, disp + (long long)(int)k, size, depth + 1);
            }
            if (const_of(s, d->lhs, &k) && (stable(s, d->rhs) || s->frame_val[d->rhs] >= 0)) {
                return mem_at(s, d->rhs, disp + (long long)(int)k, size, depth + 1);
            }
            if (stable(s, d->lhs) && stable(s, d->rhs) && s->frame_val[d->lhs] < 0 && s->frame_val[d->rhs] < 0 &&
                !const_of(s, d->lhs, &k) && !const_of(s, d->rhs, &k) && !sym_of(s, d->lhs, &sym) &&
                !sym_of(s, d->rhs, &sym)) {
                o = xop_mem(vreg_of(s, d->lhs, 0), disp, size);
                o.index = vreg_of(s, d->rhs, 0);
                return o;
            }
        }
        if (d->op == CC_SSA_SUB && d->lhs >= 0 && d->rhs >= 0 && !is_pair(s, v) && const_of(s, d->rhs, &k) &&
            (stable(s, d->lhs) || s->frame_val[d->lhs] >= 0)) {
            return mem_at(s, d->lhs, disp - (long long)(int)k, size, depth + 1);
        }
    }
    return xop_mem(vreg_of(s, v, 0), disp, size);
}

/** Write word @p part of IR value @p v to operand @p d. */
static void move_word_to(sel_t *s, int v, int part, const xop_t *d) {
    if (!part && s->frame_val[v] >= 0 && d->kind == XO_REG) {
        xop_t m = xop_frame(s->frame_val[v], 0, 4);

        emit(s, XI_LEA, 4, 2, &m, d, NULL);
        return;
    }
    {
        xop_t o = src_word(s, v, part);

        emit(s, XI_MOV, 4, 2, &o, d, NULL);
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

/** Whether label @p label comes right after IR instruction @p j, with only labels between. */
static int label_follows(const sel_t *s, size_t j, int label) {
    size_t k;

    for (k = j + 1; k < s->f->instr_count && s->f->instrs[k].op == CC_SSA_LABEL; ++k) {
        if (s->f->instrs[k].label == label) {
            return 1;
        }
    }
    return 0;
}

/** Jump to @p label unless it comes next. */
static void jump(sel_t *s, size_t j, int label) {
    if (!label_follows(s, j, label)) {
        xinst_t in = xi_make(XI_JMP, 4, 0, NULL, NULL, NULL);

        in.label = label;
        xf_emit(&s->xf, &in);
    }
}

/** Branch on the flags: to @p t when @p cc holds, else to @p fl; the one that comes next is fallen into. */
static void branch(sel_t *s, size_t j, xcc_t cc, int t, int fl) {
    xinst_t in = xi_make(XI_JCC, 4, 0, NULL, NULL, NULL);

    if (t == fl) {
        jump(s, j, t);
        return;
    }
    if (label_follows(s, j, t)) {
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

/**
 * Set the flags for compare @p in (lhs against rhs), its operands swapped when the left one is a constant;
 * the condition to test them by goes to *@p cc.  0, or 1 when both are constants and *@p known is the result.
 */
static int compare(sel_t *s, const cc_ssa_instr_t *in, xcc_t *cc, int *known) {
    xop_t a = src_word(s, in->lhs, 0);
    xop_t b = src_word(s, in->rhs, 0);

    *cc = cmp_cc(in->cmp_kind, in->is_unsigned);
    if (a.kind == XO_IMM && b.kind == XO_IMM) {
        if (a.sym == NULL && b.sym == NULL) {
            long long x = a.disp;
            long long y = b.disp;
            unsigned long long ux = (unsigned)x;
            unsigned long long uy = (unsigned)y;
            int r;

            switch (*cc) {
            case XC_E:
                r = x == y;
                break;
            case XC_NE:
                r = x != y;
                break;
            case XC_L:
                r = (int)x < (int)y;
                break;
            case XC_LE:
                r = (int)x <= (int)y;
                break;
            case XC_G:
                r = (int)x > (int)y;
                break;
            case XC_GE:
                r = (int)x >= (int)y;
                break;
            case XC_B:
                r = ux < uy;
                break;
            case XC_BE:
                r = ux <= uy;
                break;
            case XC_A:
                r = ux > uy;
                break;
            default:
                r = ux >= uy;
                break;
            }
            *known = r;
            return 1;
        }
        a = reg_word(s, in->lhs, 0);
    }
    if (a.kind == XO_IMM) {
        xop_t t = a;

        a = b;
        b = t;
        *cc = xcc_swap(*cc);
    }
    if (b.kind == XO_IMM && b.sym == NULL && b.disp == 0 && a.kind == XO_REG) {
        /* against 0: test sets the flags every condition reads as cmp $0 would */
        emit(s, XI_TEST, 4, 2, &a, &a, NULL);
    } else {
        emit(s, XI_CMP, 4, 2, &b, &a, NULL);
    }
    return 0;
}

/** A word binary operation: add, sub, and, or, xor, mul, shl, shr. */
static void sel_binop(sel_t *s, const cc_ssa_instr_t *in) {
    xop_t d = xop_reg(vreg_of(s, in->dst, 0), 4);
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

        if (in->rhs < 0 || const_of(s, in->rhs, &k)) {
            xop_t c;

            if (in->rhs < 0) {
                k = in->imm;
            }
            c = xop_imm(k & 31, 1);
            if (in->dst != in->lhs) {
                move_word_to(s, in->lhs, 0, &d);
            }
            emit(s, op, 4, 2, &c, &d, NULL);
            return;
        }
        /* the count in %cl */
        {
            xop_t cl = xop_reg(XR_CX, 1);
            xop_t ecx = xop_reg(XR_CX, 4);
            xop_t t = d;

            if (in->dst == in->rhs && in->dst != in->lhs) {
                t = temp(s);
            }
            if (in->dst != in->lhs) {
                move_word_to(s, in->lhs, 0, &t);
            }
            b = src_word(s, in->rhs, 0);
            emit(s, XI_MOV, 4, 2, &b, &ecx, NULL);
            emit(s, op, 4, 2, &cl, &t, NULL);
            if (t.reg != d.reg) {
                emit(s, XI_MOV, 4, 2, &t, &d, NULL);
            }
            return;
        }
    }
    a = src_word(s, in->lhs, 0);
    b = src_word(s, in->rhs, 0);
    if (a.kind == XO_IMM && a.sym == NULL && b.kind == XO_IMM && b.sym == NULL) {
        /* two constants: the result, as a word */
        unsigned x = (unsigned)a.disp;
        unsigned y = (unsigned)b.disp;
        unsigned r;
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
        v = xop_imm((long long)(int)r, 4);
        emit(s, XI_MOV, 4, 2, &v, &d, NULL);
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
                xop_t t = temp(s);

                emit(s, XI_MOV, 4, 2, &a, &t, NULL);
                a = t;
            }
            emit(s, XI_IMUL3, 4, 3, &b, &a, &d);
            return;
        }
    }
    if (a.kind == XO_IMM && commutes && b.kind != XO_IMM) {
        xop_t t = a;

        a = b;
        b = t;
        if (in->dst == in->rhs) {
            /* now d is the left operand */
            emit(s, op, 4, 2, &b, &d, NULL);
            return;
        }
    }
    if (op == XI_IMUL && b.kind == XO_IMM) {
        b = reg_word(s, in->rhs, 0);
    }
    if (in->dst == in->lhs) {
        emit(s, op, 4, 2, &b, &d, NULL);
    } else if (in->dst == in->rhs) {
        if (commutes) {
            emit(s, op, 4, 2, &a, &d, NULL);
        } else {
            xop_t t = temp(s);

            emit(s, XI_MOV, 4, 2, &a, &t, NULL);
            emit(s, op, 4, 2, &b, &t, NULL);
            emit(s, XI_MOV, 4, 2, &t, &d, NULL);
        }
    } else {
        emit(s, XI_MOV, 4, 2, &a, &d, NULL);
        emit(s, op, 4, 2, &b, &d, NULL);
    }
}

/** A word division: the quotient of %edx:%eax by the divisor. */
static void sel_div(sel_t *s, const cc_ssa_instr_t *in) {
    xop_t eax = xop_reg(XR_AX, 4);
    xop_t edx = xop_reg(XR_DX, 4);
    xop_t d = xop_reg(vreg_of(s, in->dst, 0), 4);
    xop_t b = reg_word(s, in->rhs, 0);
    xinst_t x;

    move_word_to(s, in->lhs, 0, &eax);
    if (in->is_unsigned) {
        emit(s, XI_XOR, 4, 2, &edx, &edx, NULL);
    } else {
        x = xi_make(XI_CDQ, 4, 0, NULL, NULL, NULL);
        x.imp_use = XR_BIT(XR_AX);
        x.imp_def = XR_BIT(XR_DX);
        xf_emit(&s->xf, &x);
    }
    x = xi_make(in->is_unsigned ? XI_DIV : XI_IDIV, 4, 1, &b, NULL, NULL);
    x.imp_use = XR_BIT(XR_AX) | XR_BIT(XR_DX);
    x.imp_def = XR_BIT(XR_AX) | XR_BIT(XR_DX);
    xf_emit(&s->xf, &x);
    emit(s, XI_MOV, 4, 2, &eax, &d, NULL);
}

/** A load of @p in->imm bytes, extended as the result is held. */
static void sel_load(sel_t *s, const cc_ssa_instr_t *in) {
    int size = (int)in->imm;

    if (size == 8 && is_pair(s, in->dst)) {
        xop_t m0 = mem_at(s, in->lhs, 0, 4, 0);
        xop_t m1 = mem_at(s, in->lhs, 4, 4, 0);
        xop_t d0 = xop_reg(vreg_of(s, in->dst, 0), 4);
        xop_t d1 = xop_reg(vreg_of(s, in->dst, 1), 4);

        emit(s, XI_MOV, 4, 2, &m0, &d0, NULL);
        emit(s, XI_MOV, 4, 2, &m1, &d1, NULL);
        return;
    }
    if (size != 1 && size != 2 && size != 4) {
        NOPE(s);
    }
    {
        xop_t m = mem_at(s, in->lhs, 0, size, 0);
        xop_t d = xop_reg(vreg_of(s, in->dst, 0), 4);

        if (size == 4) {
            emit(s, XI_MOV, 4, 2, &m, &d, NULL);
        } else {
            emit(s, in->is_unsigned ? XI_MOVZX : XI_MOVSX, 4, 2, &m, &d, NULL);
        }
    }
}

/** A store of @p in->imm bytes of the value rhs at address lhs. */
static void sel_store(sel_t *s, const cc_ssa_instr_t *in) {
    int size = (int)in->imm;

    if (cc_ssa_value_class(s->f, in->rhs) == CC_VAL_F64) {
        NOPE(s);
    }
    if (size == 8) {
        int k;

        if (!is_pair(s, in->rhs)) {
            NOPE(s);
        }
        for (k = 0; k < 2; ++k) {
            xop_t m = mem_at(s, in->lhs, 4 * k, 4, 0);
            xop_t v = src_word(s, in->rhs, k);

            emit(s, XI_MOV, 4, 2, &v, &m, NULL);
        }
        return;
    }
    if (size != 1 && size != 2 && size != 4) {
        NOPE(s);
    }
    {
        xop_t m = mem_at(s, in->lhs, 0, size, 0);
        xop_t v = src_word(s, in->rhs, 0);

        if (v.kind == XO_IMM) {
            if (size < 4 && v.sym != NULL) {
                v = reg_word(s, in->rhs, 0);
            } else if (size == 1) {
                v.disp = (signed char)v.disp;
            } else if (size == 2) {
                v.disp = (short)v.disp;
            }
        }
        if (v.kind == XO_REG && size == 1) {
            /* a byte register: a copy of the value in one, so that the value itself may live anywhere */
            xop_t t = temp(s);

            emit(s, XI_MOV, 4, 2, &v, &t, NULL);
            v = t;
        }
        v.size = (unsigned char)size;
        emit(s, XI_MOV, size, 2, &v, &m, NULL);
    }
}

/**
 * Call memcpy(@p dst, @p src, @p size) as the compiler's own calls are made: under -mregparm in %eax, %edx and
 * %ecx as far as it reaches, the rest pushed.
 */
static void sel_memcpy(sel_t *s, const xop_t *dst, const xop_t *src, long size) {
    const cc_options_t *o = cc_options_current();
    int nregs = o->i386_regparm;
    const xop_t *args[3];
    xop_t n = xop_imm(size, 4);
    long pushed = 0;
    unsigned used = 0;
    xinst_t c;
    int k;

    args[0] = dst;
    args[1] = src;
    args[2] = &n;
    if (nregs < 3 && i386_stack_boundary() >= 16) {
        long bytes = 4L * (3 - (nregs > 0 ? nregs : 0));
        long pad = (long)align_up_size((size_t)bytes, 16) - bytes;

        if (pad > 0) {
            xop_t k = xop_imm(-pad, 4);

            emit(s, XI_ADDSP, 4, 1, &k, NULL, NULL);
            pushed += pad;
        }
    }
    for (k = 2; k >= 0; --k) {
        if (k >= nregs) {
            emit(s, XI_PUSH, 4, 1, args[k], NULL, NULL);
            pushed += 4;
        }
    }
    for (k = 0; k < 3 && k < nregs; ++k) {
        xop_t r = xop_reg(i386_rp_regs[k], 4);

        emit(s, XI_MOV, 4, 2, args[k], &r, NULL);
        used |= XR_BIT(i386_rp_regs[k]);
    }
    c = xi_make(XI_CALL, 4, 0, NULL, NULL, NULL);
    c.sym = "memcpy";
    c.imp_use = used;
    c.clobber = I386_CALLER_SAVED;
    xf_emit(&s->xf, &c);
    if (pushed > 0) {
        xop_t k = xop_imm(pushed, 4);

        emit(s, XI_ADDSP, 4, 1, &k, NULL, NULL);
    }
}

/**
 * The words argument @p a of call @p in takes: by the argument's size (an int may be passed a 64-bit value's
 * low word), one for an address -- a stack object's or an aggregate's -- whatever its type says; 0 when it is
 * one this path does not pass.
 */
static int arg_words(const sel_t *s, const cc_ssa_instr_t *in, size_t a) {
    const cc_ssa_function_t *f = s->f;
    int v = in->args[a];
    long bytes;

    if (v < 0 || cc_ssa_value_class(f, v) == CC_VAL_F64) {
        return 0;
    }
    if (in->call_arg_abi != NULL && CC_ABI_KIND(in->call_arg_abi[a]) == CC_CALL_ARG_ABI_AGG_MEM) {
        /* a structure passed by value: a copy of its words on the stack */
        bytes = call_arg_size_bytes(in, a, 4);
        return (in->call_arg_abi[a] & (CC_ABI_STACK_VEC | CC_ABI_STACK_16)) != 0 || CC_ABI_REGPARM(in->call_arg_abi[a]) > 0
                   ? 0
                   : bytes <= 4 ? 1 : (int)((bytes + 3) / 4);
    }
    if (s->is_addr[v] || f->value_ty[v].kind == CC_TY_AGG) {
        return 1;
    }
    bytes = call_arg_size_bytes(in, a, value_size_bytes(f, v, 4));
    if (bytes > 8 || (bytes > 4 && !is_pair(s, v))) {
        return 0;
    }
    return bytes > 4 ? 2 : 1;
}

/** A call: stack arguments pushed last to first, register ones moved in, the result taken out of %eax:%edx. */
static void sel_call(sel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    long pos = 0;
    long cur;
    long pushed = 0;
    unsigned rp_used = 0;
    long a;
    xinst_t c;
    xop_t target;
    int indirect = 0;
    /* a structure result: in %eax:%edx (-freg-struct-return), else through a hidden pointer to its storage,
     * pushed first or (regparm) in %eax */
    int agg = in->dst >= 0 && in->rhs == in->dst;
    int in_regs = agg && in->agg_class == CC_ABI_RET_EAX_EDX;
    int sret_reg = agg && in->agg_class == CC_ABI_RET_SRET_REG;
    int sret = agg && in->agg_class == 0;
    int agg_obj = agg ? s->obj[in->dst] : -1;

    if (in->call_chain > 0 || in->call_ret_x87 || (agg && !in_regs && !sret_reg && !sret) ||
        (agg && agg_obj < 0) || (!agg && in->agg_class != 0) ||
        (in->dst >= 0 && !agg && (cc_ssa_value_class(f, in->dst) == CC_VAL_F64 ||
                                  f->value_ty[in->dst].kind == CC_TY_AGG))) {
        NOPE(s);
    }
    if (sret) {
        pos = 4;
    }
    for (a = 0; a < (long)in->arg_count; ++a) {
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;
        int words = arg_words(s, in, (size_t)a);
        int r = i386_call_arg_regparm(f, in, (size_t)a);

        if (abi == CC_CALL_ARG_ABI_ZERO) {
            continue;
        }
        if ((CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT && CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_MEM) || words == 0 ||
            (words == 2 && r == 3)) {
            NOPE(s);
        }
        if (r == 0) {
            pos += 4 * words;
        }
    }
    cur = (long)align_up_size((size_t)pos, (size_t)i386_stack_boundary());
    if (cur > pos) {
        xop_t k = xop_imm(-(cur - pos), 4);

        emit(s, XI_ADDSP, 4, 1, &k, NULL, NULL);
        pushed += cur - pos;
    }
    for (a = (long)in->arg_count - 1; a >= 0; --a) {
        int v = in->args[a];
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;
        int k;

        if (abi == CC_CALL_ARG_ABI_ZERO || i386_call_arg_regparm(f, in, (size_t)a) > 0) {
            continue;
        }
        if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM) {
            /* the structure's words, from the last */
            for (k = arg_words(s, in, (size_t)a) - 1; k >= 0; --k) {
                xop_t m = mem_at(s, v, 4 * k, 4, 0);

                emit(s, XI_PUSH, 4, 1, &m, NULL, NULL);
                pushed += 4;
            }
            continue;
        }
        for (k = arg_words(s, in, (size_t)a) - 1; k >= 0; --k) {
            xop_t o = src_word(s, v, k);

            emit(s, XI_PUSH, 4, 1, &o, NULL, NULL);
            pushed += 4;
        }
    }
    if (sret) {
        /* the hidden pointer, the first argument; the callee pops it */
        xop_t m = xop_frame(agg_obj, 0, 4);
        xop_t t = temp(s);

        emit(s, XI_LEA, 4, 2, &m, &t, NULL);
        emit(s, XI_PUSH, 4, 1, &t, NULL, NULL);
    }
    if (in->op == CC_SSA_CALLI) {
        const char *sym;

        if (sym_of(s, in->lhs, &sym) && strpbrk(sym, "+-") == NULL) {
            target = xop_sym(sym, 0, 4);
        } else {
            target = reg_word(s, in->lhs, 0);
            indirect = 1;
        }
    }
    for (a = 0; a < (long)in->arg_count; ++a) {
        int v = in->args[a];
        int r = i386_call_arg_regparm(f, in, (size_t)a);
        int k;

        if (r == 0 || (in->call_arg_abi != NULL && in->call_arg_abi[a] == CC_CALL_ARG_ABI_ZERO)) {
            continue;
        }
        for (k = 0; k < arg_words(s, in, (size_t)a); ++k) {
            xop_t d = xop_reg(i386_rp_regs[r - 1 + k], 4);

            move_word_to(s, v, k, &d);
            rp_used |= XR_BIT(i386_rp_regs[r - 1 + k]);
        }
    }
    if (sret_reg) {
        xop_t m = xop_frame(agg_obj, 0, 4);
        xop_t eax = xop_reg(XR_AX, 4);

        emit(s, XI_LEA, 4, 2, &m, &eax, NULL);
        rp_used |= XR_BIT(XR_AX);
    }
    if (indirect) {
        c = xi_make(XI_CALL, 4, 1, &target, NULL, NULL);
    } else {
        c = xi_make(XI_CALL, 4, 0, NULL, NULL, NULL);
        c.sym = in->op == CC_SSA_CALLI ? target.sym : in->sym;
    }
    c.imp_use = rp_used;
    c.clobber = I386_CALLER_SAVED;
    if (in_regs) {
        c.imp_def = XR_BIT(XR_AX) | XR_BIT(XR_DX);
    } else if (in->dst >= 0 && !agg) {
        c.imp_def = XR_BIT(XR_AX) | (is_pair(s, in->dst) ? XR_BIT(XR_DX) : 0);
    }
    xf_emit(&s->xf, &c);
    if (pushed > 0) {
        xop_t k = xop_imm(pushed, 4);

        emit(s, XI_ADDSP, 4, 1, &k, NULL, NULL);
    }
    if (in_regs) {
        /* by its size: a 1- or 2-byte structure has no room for all of %eax */
        int size = in->imm == 1 ? 1 : in->imm == 2 ? 2 : 4;
        xop_t eax = xop_reg(XR_AX, size);
        xop_t m = xop_frame(agg_obj, 0, size);

        emit(s, XI_MOV, size, 2, &eax, &m, NULL);
        if (in->imm > 4) {
            xop_t edx = xop_reg(XR_DX, 4);
            xop_t m1 = xop_frame(agg_obj, 4, 4);

            emit(s, XI_MOV, 4, 2, &edx, &m1, NULL);
        }
        return;
    }
    if (agg) {
        return; /* the callee filled the storage */
    }
    if (in->dst >= 0) {
        long ret_bytes = in->imm > 0 ? in->imm : 4;
        xop_t d = xop_reg(vreg_of(s, in->dst, 0), 4);

        if (is_pair(s, in->dst)) {
            xop_t eax = xop_reg(XR_AX, 4);
            xop_t edx = xop_reg(XR_DX, 4);
            xop_t d1 = xop_reg(vreg_of(s, in->dst, 1), 4);

            emit(s, XI_MOV, 4, 2, &eax, &d, NULL);
            emit(s, XI_MOV, 4, 2, &edx, &d1, NULL);
        } else if (ret_bytes == 1 || ret_bytes == 2) {
            /* the callee need not have extended it */
            xop_t r = xop_reg(XR_AX, (int)ret_bytes);

            emit(s, in->is_unsigned ? XI_MOVZX : XI_MOVSX, 4, 2, &r, &d, NULL);
        } else {
            xop_t eax = xop_reg(XR_AX, 4);

            emit(s, XI_MOV, 4, 2, &eax, &d, NULL);
        }
    }
}

/** Append a binding of an asm operand; NULL when out of memory. */
static asm_bind_t *add_bind(sel_t *s) {
    if (s->nbind == s->bindcap) {
        int ncap = s->bindcap > 0 ? s->bindcap * 2 : 32;
        asm_bind_t *n = (asm_bind_t *)realloc(s->bind, (size_t)ncap * sizeof(*n));

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
static int asm_slot(sel_t *s, int k) {
    while (s->nasm_slot <= k) {
        int *n = (int *)realloc(s->asm_slot, (size_t)(s->nasm_slot + 1) * sizeof(int));
        int o;

        if (n == NULL) {
            s->xf.oom = 1;
            return -1;
        }
        s->asm_slot = n;
        o = xf_new_obj(&s->xf, XF_LOCAL, 8, 4, 0);
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
 * already.  It may use %eax, %ecx and %edx, and saves what else it uses; but an asm goto's jump skips the restores,
 * so it keeps nothing in any register.
 */
static void sel_asm(sel_t *s, size_t j, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    size_t total = in->asm_out_count + in->asm_in_count;
    int first = s->nbind;
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
        asm_bind_t *bd = NULL;
        int need_in;

        if (v < 0 || v >= f->value_count) {
            continue;
        }
        if (cc_ssa_value_class(f, v) == CC_VAL_F64) {
            NOPE(s);
        }
        /* read: an input not written as an immediate, a `+` output, the address of a memory output; a 64-bit
         * output too, whose word the asm does not write keeps its value */
        need_in = !is_out || indirect || (c != NULL && strchr(c, '+') != NULL) || is_pair(s, v);
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
            for (part = 0; part < (is_pair(s, s->bind[b].v) ? 2 : 1); ++part) {
                xop_t m = xop_frame(s->bind[b].obj, 4 * part, 4);

                move_word_to(s, s->bind[b].v, part, &m);
            }
        }
    }
    x = xi_make(XI_ASM, 4, 0, NULL, NULL, NULL);
    x.ir_index = (int)j;
    x.aux = first;
    x.clobber = I386_CALLER_SAVED;
    if (in->asm_is_goto) {
        x.clobber = XR_BIT(XR_AX) | XR_BIT(XR_CX) | XR_BIT(XR_DX) | XR_BIT(XR_BX) | XR_BIT(XR_SI) | XR_BIT(XR_DI);
        x.targets = in->asm_goto_labels;
        x.ntargets = (int)in->asm_goto_count;
        s->has_goto = 1;
    }
    xf_emit(&s->xf, &x);
    for (b = first; b < s->nbind; ++b) {
        if (s->bind[b].out) {
            for (part = 0; part < (is_pair(s, s->bind[b].v) ? 2 : 1); ++part) {
                xop_t m = xop_frame(s->bind[b].obj, 4 * part, 4);
                xop_t d = xop_reg(vreg_of(s, s->bind[b].v, part), 4);

                emit(s, XI_MOV, 4, 2, &m, &d, NULL);
            }
        }
    }
    {
        asm_bind_t *end = add_bind(s);

        if (end != NULL) {
            end->v = -1;
        }
    }
}

/** Select instruction @p j of the function. */
static void sel_instr(sel_t *s, size_t j) {
    const cc_ssa_function_t *f = s->f;
    const cc_ssa_instr_t *in = &f->instrs[j];
    long long k;
    const char *sym;

    if (in->dst >= 0 && (cc_ssa_value_class(f, in->dst) == CC_VAL_F64)) {
        NOPE(s);
    }
    switch (in->op) {
    case CC_SSA_PARAM: {
        int pi = in->param_index;
        unsigned char abi;
        int kind;
        int part;

        if (pi < 0 || (size_t)pi >= f->param_count || in->dst < 0) {
            NOPE(s);
        }
        abi = f->param_abi != NULL ? f->param_abi[pi] : CC_CALL_ARG_ABI_DEFAULT;
        kind = CC_ABI_KIND(abi);
        if (abi == CC_CALL_ARG_ABI_ZERO) {
            break;
        }
        if (kind == CC_CALL_ARG_ABI_AGG_MEM && CC_ABI_REGPARM(abi) == 0) {
            /* a structure passed by value: its copy on the stack, whose address is the value */
            int o = xf_new_obj(&s->xf, XF_FIXED, 4, 4, i386_param_offset(f, pi));

            if (s->frame_val[in->dst] >= 0) {
                break;
            }
            {
                xop_t m = xop_frame(o, 0, 4);
                xop_t d = xop_reg(vreg_of(s, in->dst, 0), 4);

                emit(s, XI_LEA, 4, 2, &m, &d, NULL);
            }
            break;
        }
        if (kind != CC_CALL_ARG_ABI_DEFAULT) {
            NOPE(s);
        }
        for (part = 0; part < (is_pair(s, in->dst) ? 2 : 1); ++part) {
            xop_t d = xop_reg(vreg_of(s, in->dst, part), 4);

            if (CC_ABI_REGPARM(abi) > 0) {
                xop_t r = xop_reg(part ? s->param_hi[pi] : s->param_lo[pi], 4);

                if (r.reg == XR_NONE) {
                    NOPE(s);
                }

                emit(s, XI_MOV, 4, 2, &r, &d, NULL);
            } else {
                int o = xf_new_obj(&s->xf, XF_FIXED, 4, 4, i386_param_offset(f, pi) + 4 * part);
                xop_t m = xop_frame(o, 0, 4);

                if (s->ndefs[in->dst] == 1) {
                    /* never written again: spilled, it stays where it came */
                    xvreg_t *r = xf_vreg(&s->xf, d.reg);

                    if (!r->memory) {
                        r->home_obj = o;
                        r->home_off = 0;
                    }
                }
                emit(s, XI_MOV, 4, 2, &m, &d, NULL);
            }
        }
        break;
    }

    case CC_SSA_CONST:
        if (cc_ssa_value_class(f, in->dst) != CC_VAL_I64 || f->value_ty[in->dst].kind == CC_TY_AGG) {
            NOPE(s);
        }
        if (const_of(s, in->dst, &k)) {
            break; /* an immediate where it is read */
        }
        {
            int part;

            for (part = 0; part < (is_pair(s, in->dst) ? 2 : 1); ++part) {
                xop_t v = xop_imm(part ? (long long)(int)(in->imm >> 32) : (long long)(int)in->imm, 4);
                xop_t d = xop_reg(vreg_of(s, in->dst, part), 4);

                emit(s, XI_MOV, 4, 2, &v, &d, NULL);
            }
        }
        break;

    case CC_SSA_STR:
    case CC_SSA_GADDR:
        if (in->op == CC_SSA_GADDR && in->sym == NULL) {
            NOPE(s);
        }
        if (sym_of(s, in->dst, &sym)) {
            break; /* an immediate where it is read */
        }
        if (in->op == CC_SSA_STR) {
            char buf[96];

            str_label(s, j, buf, sizeof(buf));
            sym = keep_name(buf);
            if (sym == NULL) {
                s->xf.oom = 1;
                break;
            }
        } else {
            sym = in->sym;
        }
        {
            xop_t v = xop_sym(sym, 0, 4);
            xop_t d = xop_reg(vreg_of(s, in->dst, 0), 4);

            emit(s, XI_MOV, 4, 2, &v, &d, NULL);
        }
        break;

    case CC_SSA_STACKALLOC:
    case CC_SSA_ADDR:
        if (s->frame_val[in->dst] >= 0) {
            break; /* folded into what reads it */
        }
        {
            int o = in->op == CC_SSA_STACKALLOC ? s->obj[in->dst] : s->obj[in->lhs];
            xop_t m = xop_frame(o, 0, 4);
            xop_t d = xop_reg(vreg_of(s, in->dst, 0), 4);

            if (o < 0) {
                NOPE(s);
            }
            emit(s, XI_LEA, 4, 2, &m, &d, NULL);
        }
        break;

    case CC_SSA_MOV: {
        int part;

        if (in->dst == in->lhs) {
            break;
        }
        for (part = 0; part < (is_pair(s, in->dst) ? 2 : 1); ++part) {
            xop_t d = xop_reg(vreg_of(s, in->dst, part), 4);

            if (part == 1 && !is_pair(s, in->lhs)) {
                NOPE(s);
            }
            move_word_to(s, in->lhs, part, &d);
        }
        break;
    }

    case CC_SSA_LO32:
    case CC_SSA_HI32: {
        xop_t d = xop_reg(vreg_of(s, in->dst, 0), 4);

        if (!is_pair(s, in->lhs)) {
            NOPE(s);
        }
        move_word_to(s, in->lhs, in->op == CC_SSA_HI32, &d);
        break;
    }

    case CC_SSA_PAIR: {
        xop_t d0 = xop_reg(vreg_of(s, in->dst, 0), 4);
        xop_t d1 = xop_reg(vreg_of(s, in->dst, 1), 4);

        move_word_to(s, in->lhs, 0, &d0);
        move_word_to(s, in->rhs, 0, &d1);
        break;
    }

    case CC_SSA_UMULW: {
        xop_t eax = xop_reg(XR_AX, 4);
        xop_t edx = xop_reg(XR_DX, 4);
        xop_t b = reg_word(s, in->rhs, 0);
        xop_t d0 = xop_reg(vreg_of(s, in->dst, 0), 4);
        xop_t d1 = xop_reg(vreg_of(s, in->dst, 1), 4);
        xinst_t x;

        move_word_to(s, in->lhs, 0, &eax);
        x = xi_make(XI_MUL1, 4, 1, &b, NULL, NULL);
        x.imp_use = XR_BIT(XR_AX);
        x.imp_def = XR_BIT(XR_AX) | XR_BIT(XR_DX);
        xf_emit(&s->xf, &x);
        emit(s, XI_MOV, 4, 2, &eax, &d0, NULL);
        emit(s, XI_MOV, 4, 2, &edx, &d1, NULL);
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
        if (is_pair(s, in->dst) || f->value_ty[in->dst].kind == CC_TY_AGG || in->lhs < 0 ||
            (in->rhs < 0 && in->op != CC_SSA_SHL && in->op != CC_SSA_SHR)) {
            NOPE(s);
        }
        sel_binop(s, in);
        break;

    case CC_SSA_DIV:
        if (is_pair(s, in->dst) || in->lhs < 0 || in->rhs < 0) {
            NOPE(s);
        }
        sel_div(s, in);
        break;

    case CC_SSA_CMP: {
        xcc_t cc;
        int known = 0;

        if (in->lhs < 0 || in->rhs < 0 || cc_ssa_value_class(f, in->lhs) == CC_VAL_F64 ||
            cc_ssa_value_class(f, in->rhs) == CC_VAL_F64 || is_pair(s, in->lhs) || is_pair(s, in->rhs)) {
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
            xop_t d = xop_reg(vreg_of(s, in->dst, 0), 4);

            if (compare(s, in, &cc, &known)) {
                xop_t v = xop_imm(known, 4);

                emit(s, XI_MOV, 4, 2, &v, &d, NULL);
            } else {
                xop_t t = xop_reg(xf_new_vreg(&s->xf), 1);
                xinst_t x = xi_make(XI_SETCC, 1, 1, &t, NULL, NULL);

                x.cc = (unsigned char)cc;
                xf_emit(&s->xf, &x);
                emit(s, XI_MOVZX, 4, 2, &t, &d, NULL);
            }
        }
        break;
    }

    case CC_SSA_LABEL: {
        xinst_t x = xi_make(XI_LABEL, 4, 0, NULL, NULL, NULL);

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
        if (in->lhs < 0 || is_pair(s, in->lhs) || cc_ssa_value_class(f, in->lhs) == CC_VAL_F64) {
            NOPE(s);
        }
        if (const_of(s, in->lhs, &k)) {
            jump(s, j, (int)k != 0 ? in->true_label : in->false_label);
            break;
        }
        if (sym_of(s, in->lhs, &sym) || s->frame_val[in->lhs] >= 0) {
            jump(s, j, in->true_label);
            break;
        }
        {
            xop_t v = xop_reg(vreg_of(s, in->lhs, 0), 4);

            emit(s, XI_TEST, 4, 2, &v, &v, NULL);
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
        sel_call(s, in);
        break;

    case CC_SSA_ASM:
        sel_asm(s, j, in);
        break;

    case CC_SSA_RET: {
        xinst_t x = xi_make(XI_RET, 4, 0, NULL, NULL, NULL);

        if (f->ret_abi == CC_CALL_ARG_ABI_AGG_MEM) {
            /* copied to the caller's buffer, whose address is returned; the callee pops it if it was pushed */
            xop_t h = xop_reg(s->hidden, 4);
            xop_t eax = xop_reg(XR_AX, 4);

            if (in->agg_class != 0 && in->agg_class != CC_ABI_RET_SRET_REG) {
                NOPE(s);
            }
            if (in->lhs >= 0 && in->imm > 0) {
                xop_t src = reg_word(s, in->lhs, 0);

                sel_memcpy(s, &h, &src, (long)in->imm);
            }
            emit(s, XI_MOV, 4, 2, &h, &eax, NULL);
            x.imp_use = XR_BIT(XR_AX);
            x.aux = f->sret_reg ? 0 : 4;
            xf_emit(&s->xf, &x);
            break;
        }
        /* a structure returned in %eax:%edx (-freg-struct-return) is returned as its words */
        if (in->agg_class != 0 && in->agg_class != CC_ABI_RET_EAX_EDX) {
            NOPE(s);
        }
        if (in->lhs >= 0) {
            xop_t eax = xop_reg(XR_AX, 4);

            if (f->value_ty[in->lhs].kind == CC_TY_AGG || cc_ssa_value_class(f, in->lhs) == CC_VAL_F64) {
                NOPE(s);
            }
            move_word_to(s, in->lhs, 0, &eax);
            x.imp_use = XR_BIT(XR_AX);
            if (in->imm > 4 && in->imm <= 8) {
                xop_t edx = xop_reg(XR_DX, 4);

                if (in->rhs >= 0) {
                    move_word_to(s, in->rhs, 0, &edx);
                } else if (is_pair(s, in->lhs)) {
                    move_word_to(s, in->lhs, 1, &edx);
                } else {
                    NOPE(s);
                }
                x.imp_use |= XR_BIT(XR_DX);
            } else if (in->imm > 8) {
                NOPE(s);
            }
        }
        xf_emit(&s->xf, &x);
        break;
    }

    case CC_SSA_TRAP: {
        xinst_t x = xi_make(XI_TRAP, 4, 0, NULL, NULL, NULL);

        xf_emit(&s->xf, &x);
        break;
    }

    case CC_SSA_VA_START: {
        /* the address of the first variadic argument on the stack */
        int o = xf_new_obj(&s->xf, XF_FIXED, 4, 4, i386_variadic_start_offset(f, (int)in->imm));
        xop_t m = xop_frame(o, 0, 4);
        xop_t d = xop_reg(vreg_of(s, in->dst, 0), 4);

        emit(s, XI_LEA, 4, 2, &m, &d, NULL);
        break;
    }

    case CC_SSA_REGREAD: {
        xop_t d;
        xop_t r;

        if (in->sym == NULL || (strcmp(in->sym, "esp") != 0 && strcmp(in->sym, "ebp") != 0)) {
            NOPE(s);
        }
        d = xop_reg(vreg_of(s, in->dst, 0), 4);
        r = xop_reg(strcmp(in->sym, "esp") == 0 ? XR_SP : XR_BP, 4);
        emit(s, XI_MOV, 4, 2, &r, &d, NULL);
        break;
    }

    case CC_SSA_FRAMEADDR: {
        xop_t d = xop_reg(vreg_of(s, in->dst, 0), 4);
        xop_t bp = xop_reg(XR_BP, 4);
        long long n;

        emit(s, XI_MOV, 4, 2, &bp, &d, NULL);
        for (n = 0; n < in->imm; ++n) {
            xop_t m = xop_mem(d.reg, 0, 4);

            emit(s, XI_MOV, 4, 2, &m, &d, NULL);
        }
        break;
    }

    default:
        NOPE(s);
    }
}

/** Whether the function as a whole is one this path takes: NULL, or why not. */
static const char *function_ok(const cc_ssa_function_t *f) {
    const cc_options_t *o = cc_options_current();
    size_t i;
    int v;

    if (o->pic) {
        return "-fPIC";
    }
    if (f->ret_type == CC_VAL_F64 || (f->ret_abi != CC_CALL_ARG_ABI_DEFAULT && f->ret_abi != CC_CALL_ARG_ABI_AGG_MEM)) {
        return "its return";
    }
    for (v = 0; v < f->value_count; ++v) {
        if (f->value_ty[v].kind == CC_TY_FLOAT) {
            return "floating point";
        }
    }
    for (i = 0; i < f->param_count; ++i) {
        unsigned char abi = f->param_abi != NULL ? f->param_abi[i] : CC_CALL_ARG_ABI_DEFAULT;

        if (f->param_types[i] == CC_VAL_F64 ||
            (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT && abi != CC_CALL_ARG_ABI_ZERO &&
             !(CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM && CC_ABI_REGPARM(abi) == 0))) {
            return "a parameter's ABI";
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
    return NULL;
}

/** x86_for_each_use() callback: count a read. */
static void count_use(int v, void *ctx) {
    sel_t *s = (sel_t *)ctx;

    if (v >= 0 && v < s->f->value_count) {
        s->nuses[v]++;
    }
}

/** Set up @p s for @p f: the counts of definitions and uses, the frame objects, the parameters' registers. */
static int sel_init(sel_t *s, const cc_ssa_module_t *m, const cc_ssa_function_t *f, size_t fn_index) {
    size_t n = (size_t)(f->value_count > 0 ? f->value_count : 1);
    size_t np = f->param_count > 0 ? f->param_count : 1;
    size_t i;
    int v;

    memset(s, 0, sizeof(*s));
    xf_init(&s->xf);
    s->m = m;
    s->f = f;
    s->fn_index = fn_index;
    s->xf.nlabels = f->label_count;
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
            s->obj[v] = xf_new_obj(&s->xf, XF_LOCAL, is_pair(s, v) ? 8 : 4, 4, 0);
        }
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_STACKALLOC && in->dst >= 0 && in->dst < f->value_count) {
            int bytes = in->imm > 0 ? (int)in->imm : 1;

            s->obj[in->dst] = xf_new_obj(&s->xf, XF_LOCAL, (bytes + 3) & ~3, bytes >= 16 ? 16 : 4, 0);
            if (s->ndefs[in->dst] == 1) {
                s->frame_val[in->dst] = s->obj[in->dst];
            }
        } else if (in->op == CC_SSA_ADDR && in->dst >= 0 && in->dst < f->value_count && s->ndefs[in->dst] == 1 &&
                   in->lhs >= 0) {
            s->frame_val[in->dst] = s->obj[in->lhs];
        } else if (in->op == CC_SSA_PARAM && in->dst >= 0 && s->ndefs[in->dst] == 1 && f->param_abi != NULL &&
                   in->param_index >= 0 && (size_t)in->param_index < f->param_count &&
                   CC_ABI_KIND(f->param_abi[in->param_index]) == CC_CALL_ARG_ABI_AGG_MEM &&
                   CC_ABI_REGPARM(f->param_abi[in->param_index]) == 0) {
            s->frame_val[in->dst] = xf_new_obj(&s->xf, XF_FIXED, 4, 4, i386_param_offset(f, in->param_index));
        }
    }
    for (v = 0; v < f->value_count; ++v) {
        if (s->addr_taken[v] && s->frame_val[v] >= 0) {
            /* the address of a stack object taken again: not handled */
            return 1;
        }
    }
    /* a structure result: the caller's buffer's address, from %eax (regparm) or the stack */
    s->hidden = XR_NONE;
    if (f->ret_abi == CC_CALL_ARG_ABI_AGG_MEM) {
        xop_t h = temp(s);

        if (f->sret_reg) {
            xop_t eax = xop_reg(XR_AX, 4);

            emit(s, XI_MOV, 4, 2, &eax, &h, NULL);
        } else {
            int o = xf_new_obj(&s->xf, XF_FIXED, 4, 4, 8);
            xop_t m = xop_frame(o, 0, 4);

            xf_vreg(&s->xf, h.reg)->home_obj = o;
            emit(s, XI_MOV, 4, 2, &m, &h, NULL);
        }
        s->hidden = h.reg;
    }
    /* each register parameter out of its register first, before anything can touch them */
    for (i = 0; i < f->param_count; ++i) {
        unsigned char abi = f->param_abi != NULL ? f->param_abi[i] : CC_CALL_ARG_ABI_DEFAULT;
        int r = CC_ABI_REGPARM(abi);
        int pv = f->param_values != NULL ? f->param_values[i] : -1;
        int words = pv >= 0 && is_pair(s, pv) ? 2 : 1;
        int k;

        s->param_lo[i] = XR_NONE;
        s->param_hi[i] = XR_NONE;
        if (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT || r == 0) {
            continue;
        }
        if (r - 1 + words > 3) {
            return 1;
        }
        for (k = 0; k < words; ++k) {
            xop_t from = xop_reg(i386_rp_regs[r - 1 + k], 4);
            xop_t to = temp(s);

            emit(s, XI_MOV, 4, 2, &from, &to, NULL);
            if (k == 0) {
                s->param_lo[i] = to.reg;
            } else {
                s->param_hi[i] = to.reg;
            }
        }
    }
    return s->xf.oom ? -1 : 0;
}

static void sel_free(sel_t *s) {
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

/** After allocation: a copy and an add become one leal, a move of 0 into a register an xorl. */
static void peephole(xfunc_t *xf) {
    int i;
    int j = 0;

    for (i = 0; i < xf->nins; ++i) {
        xinst_t *in = &xf->ins[i];

        if (in->op == XI_MOV && in->w == 4 && i + 1 < xf->nins && in->o[0].kind == XO_REG &&
            in->o[1].kind == XO_REG && in->o[0].reg != in->o[1].reg && in->o[0].reg != XR_SP) {
            xinst_t *nx = &xf->ins[i + 1];

            if (nx->op == XI_ADD && nx->w == 4 && nx->o[1].kind == XO_REG && nx->o[1].reg == in->o[1].reg &&
                ((nx->o[0].kind == XO_IMM) || (nx->o[0].kind == XO_REG && nx->o[0].reg != in->o[1].reg)) &&
                !flags_live_after(xf, i + 1)) {
                /* mov a, d; add b, d: leal b(a), d */
                xop_t m = xop_mem(in->o[0].reg, 0, 4);
                xop_t d = in->o[1];

                if (nx->o[0].kind == XO_IMM) {
                    m.disp = nx->o[0].disp;
                    m.sym = nx->o[0].sym;
                } else {
                    m.index = nx->o[0].reg;
                }
                xf->ins[j++] = xi_make(XI_LEA, 4, 2, &m, &d, NULL);
                i++;
                continue;
            }
        }
        if (in->op == XI_MOV && in->o[0].kind == XO_IMM && in->o[0].sym == NULL && in->o[0].disp == 0 &&
            in->o[1].kind == XO_REG && !flags_live_after(xf, i)) {
            in->op = XI_XOR;
            in->o[0] = in->o[1];
        }
        xf->ins[j++] = *in;
    }
    xf->nins = j;
}

/**
 * Lay out the frame: below the saved registers (@p saved bytes), each object not at a fixed place, the busiest
 * first; a 16-byte one on a 16-byte boundary.  The bytes the prologue allocates go to *@p frame.
 */
static int layout_frame(xfunc_t *xf, int saved, int *frame) {
    int *order = (int *)malloc((size_t)(xf->nobj > 0 ? xf->nobj : 1) * sizeof(int));
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

        cur -= o->size;
        if (o->align >= 16) {
            /* %ebp is 8 past a 16-byte boundary */
            while (((cur - 8) % 16) != 0) {
                cur--;
            }
        } else {
            while ((cur % 4) != 0) {
                cur--;
            }
        }
        o->off = cur;
    }
    free(order);
    bytes = -cur - saved;
    if (i386_stack_boundary() >= 16) {
        /* the body's %esp on a 16-byte boundary: saved + frame is 8 past one */
        while (((saved + bytes) % 16) != 8) {
            bytes += 4;
        }
    } else {
        bytes = (long)align_up_size((size_t)bytes, (size_t)i386_stack_boundary());
    }
    *frame = (int)bytes;
    return 0;
}

/** The callee-saved registers the function uses, in the order the prologue pushes them. */
static const int i386_saved_regs[3] = {XR_BX, XR_SI, XR_DI};

/**
 * Print the epilogue and return; with @p reset, %esp is made again from %ebp, as an asm goto's jump past its
 * own restores may leave it.
 */
static void print_epilogue(FILE *fp, unsigned saved, int frame, int reset, int pop, int emit_debug) {
    int nsaved = 0;
    int r;

    for (r = 0; r < 3; ++r) {
        nsaved += (saved & XR_BIT(i386_saved_regs[r])) != 0;
    }
    if (emit_debug) {
        fputs("\t.cfi_remember_state\n", fp);
    }
    if (nsaved == 0) {
        fputs(frame > 0 || reset ? "\tleave\n" : "\tpopl %ebp\n", fp);
    } else {
        if (frame > 0 || reset) {
            fprintf(fp, "\tleal %d(%%ebp), %%esp\n", -4 * nsaved);
        }
        for (r = 2; r >= 0; --r) {
            if ((saved & XR_BIT(i386_saved_regs[r])) != 0) {
                fprintf(fp, "\tpopl %s\n", xr_name(i386_saved_regs[r], 4));
            }
        }
        fputs("\tpopl %ebp\n", fp);
    }
    if (emit_debug) {
        fputs("\t.cfi_def_cfa %esp, 4\n", fp);
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
static int print_asm(FILE *fp, const sel_t *s, const xinst_t *in, cc_diag_t *diag) {
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
    rc = emit_inline_asm(fp, f, &lay, &f->instrs[in->ir_index], s->fn_index, (size_t)in->ir_index, 0, diag);
    free(lay.slot_of);
    free(lay.stackalloc_off);
    return rc;
}

/** Print the function: its string literals, its header, prologue, body and trailer. */
static int print_function(FILE *fp, sel_t *s, const char *src_path, int emit_debug, cc_diag_t *diag) {
    const cc_ssa_function_t *f = s->f;
    const char *func_sec = (f->attr_section != NULL && f->attr_section[0] != '\0') ? f->attr_section : ".text";
    unsigned saved = 0;
    int nsaved = 0;
    int frame = 0;
    size_t j;
    int i;
    int r;

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
    saved &= XR_BIT(XR_BX) | XR_BIT(XR_SI) | XR_BIT(XR_DI);
    if (s->has_goto) {
        /* the asm saves what it uses, and its jump may skip the restore */
        saved = XR_BIT(XR_BX) | XR_BIT(XR_SI) | XR_BIT(XR_DI);
    }
    for (r = 0; r < 3; ++r) {
        nsaved += (saved & XR_BIT(i386_saved_regs[r])) != 0;
    }
    if (layout_frame(&s->xf, 4 * nsaved, &frame) != 0) {
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
        fputs("\t.cfi_startproc\n\t.cfi_def_cfa_offset 8\n\t.cfi_offset %ebp, -8\n", fp);
    }
    fputs("\tpushl %ebp\n\tmovl %esp, %ebp\n", fp);
    if (emit_debug) {
        fputs("\t.cfi_def_cfa_register %ebp\n", fp);
        if (src_path != NULL) {
            fputs("\t.loc 1 1 0\n", fp);
        }
    }
    {
        int off = -8;

        for (r = 0; r < 3; ++r) {
            if ((saved & XR_BIT(i386_saved_regs[r])) != 0) {
                off -= 4;
                fprintf(fp, "\tpushl %s\n", xr_name(i386_saved_regs[r], 4));
                if (emit_debug) {
                    fprintf(fp, "\t.cfi_offset %s, %d\n", xr_name(i386_saved_regs[r], 4), off);
                }
            }
        }
    }
    if (frame > 0) {
        fprintf(fp, "\tsubl $%d, %%esp\n", frame);
    }
    for (i = 0; i < s->xf.nins; ++i) {
        const xinst_t *in = &s->xf.ins[i];

        if (in->op == XI_LABEL) {
            emit_local_label(fp, f->name, in->label);
            fputs(":\n", fp);
        } else if (in->op == XI_RET) {
            print_epilogue(fp, saved, frame, s->has_goto, in->aux, emit_debug);
        } else if (in->op == XI_ASM) {
            if (print_asm(fp, s, in, diag) != 0) {
                return -1;
            }
        } else if (in->op == XI_TRAP) {
            fputs(i386_has_ud2() ? "\tud2\n" : "\tint3\n", fp);
        } else {
            xi_print(fp, &s->xf, in, 4, f->name);
        }
    }
    if (emit_debug) {
        fputs("\t.cfi_endproc\n", fp);
    }
    fprintf(fp, ".size %s, .-%s\n", f->name, f->name);
    if ((f->attr_flags & CC_ATTR_CONSTRUCTOR) != 0) {
        emit_lifecycle_attr(fp, ".init_array", "init_array", f->name, 0);
    }
    if ((f->attr_flags & CC_ATTR_DESTRUCTOR) != 0) {
        emit_lifecycle_attr(fp, ".fini_array", "fini_array", f->name, 0);
    }
    return 0;
}

int i386_mir_emit_function(FILE *out, const cc_ssa_module_t *m, const cc_ssa_function_t *f, size_t fn_index,
                           const char *src_path, int emit_debug, cc_diag_t *diag) {
    static const xtarget_t tgt = {
        XR_BIT(XR_AX) | XR_BIT(XR_CX) | XR_BIT(XR_DX) | XR_BIT(XR_BX) | XR_BIT(XR_SI) | XR_BIT(XR_DI),
        XR_BIT(XR_BX) | XR_BIT(XR_SI) | XR_BIT(XR_DI),
        XR_BIT(XR_AX) | XR_BIT(XR_CX) | XR_BIT(XR_DX) | XR_BIT(XR_BX),
        i386_order,
        (int)(sizeof(i386_order) / sizeof(i386_order[0])),
        4};
    static long taken; /* the functions this path has taken, for CC_MIR_LIMIT */
    const char *env = getenv("CC_NO_MIR");
    const char *limit = getenv("CC_MIR_LIMIT");
    const char *stats = getenv("CC_MIR_STATS");
    const char *why;
    sel_t s;
    size_t j;
    int rc;

    if (env != NULL && env[0] == '1') {
        return 1;
    }
    why = function_ok(f);
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
    rc = sel_init(&s, m, f, fn_index);
    for (j = 0; rc == 0 && j < f->instr_count && !s.unsupported && !s.xf.oom; ++j) {
        sel_instr(&s, j);
    }
    if (rc == 0 && (s.unsupported || s.xf.oom)) {
        rc = s.xf.oom ? -1 : 1;
    }
    if (rc == 0) {
        rc = x86_mir_allocate(&s.xf, &tgt);
    }
    if (stats != NULL && stats[0] == '1') {
        if (rc == 0) {
            fprintf(stderr, "mir: %s: taken\n", f->name);
        } else if (s.unsupported) {
            fprintf(stderr, "mir: %s: not taken: mir_i386.c:%d\n", f->name, s.unsupported);
        } else if (rc > 0) {
            fprintf(stderr, "mir: %s: not taken: allocation\n", f->name);
        }
    }
    if (rc == 0) {
        peephole(&s.xf);
        if (print_function(out, &s, src_path, emit_debug, diag) != 0) {
            rc = -1;
        }
    }
    free_names();
    sel_free(&s);
    if (rc < 0 && diag != NULL && diag->message[0] == '\0') {
        set_diag(diag, "out of memory selecting i386 instructions");
    }
    return rc;
}
