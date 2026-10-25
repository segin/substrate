/*
 * mir_i386.c: i386's part of selection into machine IR (mir_sel.c): the
 * psABI's parameters, calls and returns, with -mregparm and
 * -freg-struct-return, and va_start.
 *
 * Arguments are pushed last to first, the registers of regparm loaded
 * after; a 64-bit integer is two words, %eax:%edx when returned.  A
 * structure result comes back in %eax:%edx (-freg-struct-return), else
 * through a hidden pointer the callee pops -- or, under regparm, that
 * takes %eax.  linscan.c gives out %eax, %ecx, %edx, %ebx, %esi and %edi.
 *
 * Floating arguments are stored in their words on the stack -- a float's
 * one in single precision, a double's two, a long double's three -- and a
 * floating result comes back in %st0.  mir_fp.c holds every floating
 * value on the x87, but under -mfpmath=sse (or -msse2) floats and doubles
 * in %xmm0-7, as i386.c computes them.
 *
 * Under -fPIC the GOT's address is made once at entry (XI_GOTBASE, a pc
 * thunk); a symbol is reached off it, @GOTOFF where it binds within the
 * object, else through its @GOT entry, and a call that may bind outside
 * goes through @PLT with the GOT copied into %ebx, as i386.c does.
 */

#include "cc_x86_mir.h"

#include "cc_options.h"
#include "cc_x86.h"

#include <stdlib.h>
#include <string.h>

/** The allocation order: the caller-saved registers first, which cost no save. */
static const int i386_order[] = {XR_AX,       XR_CX,       XR_DX,       XR_BX,       XR_SI,       XR_DI,       XR_XMM0,
                                 XR_XMM0 + 1, XR_XMM0 + 2, XR_XMM0 + 3, XR_XMM0 + 4, XR_XMM0 + 5, XR_XMM0 + 6, XR_XMM0 + 7};

/** The callee-saved registers, in the order the prologue pushes them. */
static const int i386_saved[] = {XR_BX, XR_SI, XR_DI};

/** The registers a call clobbers: three general ones, and every vector one. */
#define I386_CALLER_SAVED (XR_BIT(XR_AX) | XR_BIT(XR_CX) | XR_BIT(XR_DX) | XR_XMM_MASK(8))

/** The general registers the allocator gives out. */
#define I386_GPRS (XR_BIT(XR_AX) | XR_BIT(XR_CX) | XR_BIT(XR_DX) | XR_BIT(XR_BX) | XR_BIT(XR_SI) | XR_BIT(XR_DI))

/**
 * Whether doubles are SSE's, as i386.c chooses: under -mfpmath=sse, or with SSE2 and no -mfpmath=387; else the
 * x87's.
 */
static int i386_sse_fp(void) {
    const cc_options_t *o = cc_options_current();

    return o->i386_fpmath == CC_FPMATH_SSE || (o->i386_fpmath != CC_FPMATH_387 && o->i386_sse2);
}

/** The regparm registers, in the order arguments take them. */
static const int i386_rp_regs[3] = {XR_AX, XR_DX, XR_CX};

/** Whether this compilation is position-independent (-fPIC, -fPIE). */
static int i386_pic_on(void) {
    return cc_options_current()->pic;
}

/**
 * The name a direct call of @p sym calls: under -fPIC, @p sym@PLT for one that may bind outside the object --
 * whose PLT entry finds the GOT in %ebx, loaded here from the function's GOT register (*@p used gains it) --
 * else @p sym itself, as i386.c calls it.  @p known_local: a declaration made it bind within the object.
 */
static const char *call_name(xsel_t *s, const char *sym, int known_local, unsigned *used) {
    const char *n;

    if (s->got == XR_NONE || known_local || module_symbol_binds_locally(s->m, sym)) {
        return sym;
    }
    n = xs_reloc_name(sym, "PLT");
    if (n == NULL) {
        s->xf.oom = 1;
        return sym;
    }
    {
        xop_t g = xop_reg(s->got, 4);
        xop_t ebx = xop_reg(XR_BX, 4);

        xs_emit(s, XI_MOV, 4, 2, &g, &ebx, NULL);
        *used |= XR_BIT(XR_BX);
    }
    return n;
}

/**
 * Call memcpy(@p dst, @p src, @p size) as the compiler's own calls are made: under -mregparm in %eax, %edx and
 * %ecx as far as it reaches, the rest pushed.
 */
static void sel_memcpy(xsel_t *s, const xop_t *dst, const xop_t *src, long size) {
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

            xs_emit(s, XI_ADDSP, 4, 1, &k, NULL, NULL);
            pushed += pad;
        }
    }
    for (k = 2; k >= 0; --k) {
        if (k >= nregs) {
            xs_emit(s, XI_PUSH, 4, 1, args[k], NULL, NULL);
            pushed += 4;
        }
    }
    for (k = 0; k < 3 && k < nregs; ++k) {
        xop_t r = xop_reg(i386_rp_regs[k], 4);

        xs_emit(s, XI_MOV, 4, 2, args[k], &r, NULL);
        used |= XR_BIT(i386_rp_regs[k]);
    }
    c = xi_make(XI_CALL, 4, 0, NULL, NULL, NULL);
    c.sym = call_name(s, "memcpy", 0, &used);
    c.imp_use = used;
    c.clobber = I386_CALLER_SAVED;
    xf_emit(&s->xf, &c);
    if (pushed > 0) {
        xop_t k = xop_imm(pushed, 4);

        xs_emit(s, XI_ADDSP, 4, 1, &k, NULL, NULL);
    }
}

/** Whether floating ABI byte @p abi is one this path passes: a double, a float in single precision, a long double. */
static int i386_fp_abi_ok(unsigned char abi) {
    return abi == CC_CALL_ARG_ABI_DEFAULT || abi == CC_CALL_ARG_ABI_FLOAT || abi == CC_CALL_ARG_ABI_LDOUBLE;
}

/**
 * The words argument @p a of call @p in takes: by the argument's size (an int may be passed a 64-bit value's
 * low word), one for an address -- a stack object's or an aggregate's -- whatever its type says; 0 when it is
 * one this path does not pass.
 */
static int arg_words(const xsel_t *s, const cc_ssa_instr_t *in, size_t a) {
    const cc_ssa_function_t *f = s->f;
    int v = in->args[a];
    long bytes;

    if (v < 0) {
        return 0;
    }
    if (cc_ssa_value_class(f, v) == CC_VAL_F64) {
        /* a float's single word, a double's two, a long double's three */
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;

        return abi == CC_CALL_ARG_ABI_FLOAT ? 1 : abi == CC_CALL_ARG_ABI_LDOUBLE ? 3 : abi == 0 ? 2 : 0;
    }
    if (in->call_arg_abi != NULL && CC_ABI_KIND(in->call_arg_abi[a]) == CC_CALL_ARG_ABI_AGG_MEM) {
        /* a structure passed by value: a copy of its words on the stack */
        bytes = call_arg_size_bytes(in, a, 4);
        return bytes <= 4 ? 1 : (int)((bytes + 3) / 4);
    }
    if (s->is_addr[v] || f->value_ty[v].kind == CC_TY_AGG) {
        return 1;
    }
    bytes = call_arg_size_bytes(in, a, value_size_bytes(f, v, 4));
    if (bytes > 8 || (bytes > 4 && !xs_is_pair(s, v))) {
        return 0;
    }
    return bytes > 4 ? 2 : 1;
}

/**
 * The stack bytes argument @p a of call @p in takes: none for one in a register (regparm, a vector) or an empty
 * structure; a float's 4, a double's 8, a long double's 12; a structure's copy, whole words; else its words.
 */
static long arg_bytes(const xsel_t *s, const cc_ssa_instr_t *in, size_t a) {
    unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;

    if (abi == CC_CALL_ARG_ABI_ZERO || i386_call_arg_regparm(s->f, in, a) > 0 ||
        CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_REG) {
        return 0;
    }
    if (xs_fp_kind(s, in->args[a]) != XFP_NONE) {
        return abi == CC_CALL_ARG_ABI_FLOAT ? 4 : abi == CC_CALL_ARG_ABI_LDOUBLE ? 12 : 8;
    }
    if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM) {
        return i386_param_slot_bytes(call_arg_size_bytes(in, a, 4));
    }
    return 4L * arg_words(s, in, a);
}

/**
 * Store argument @p a of call @p in at the top of the stack, taking @p bytes of it, pushed or (a floating one)
 * stored after the stack pointer moves; *@p pushed counts them.
 */
static void push_arg(xsel_t *s, const cc_ssa_instr_t *in, size_t a, long bytes, long *pushed) {
    int v = in->args[a];
    unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;
    long k;

    if (xs_fp_kind(s, v) != XFP_NONE) {
        /* a float in single precision, a long double's 80 bits in its 12 */
        xop_t n = xop_imm(-bytes, 4);
        xop_t at = xop_mem(XR_SP, 0, 4);

        xs_emit(s, XI_ADDSP, 4, 1, &n, NULL, NULL);
        *pushed += bytes;
        xs_fp_to_mem(s, v, at, bytes == 4 ? 4 : bytes == 8 ? 8 : 16);
        return;
    }
    for (k = bytes / 4 - 1; k >= 0; --k) {
        /* a structure's words from its storage, from the last; else the value's */
        xop_t o = CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM ? xs_mem_at(s, v, 4 * k, 4) : xs_src_word(s, v, (int)k);

        xs_emit(s, XI_PUSH, 4, 1, &o, NULL, NULL);
        *pushed += 4;
    }
}

/**
 * A call: stack arguments at their offsets (a vector's copy aligned to its size from 16, a _Complex _Float128's
 * to 16), pushed last to first with the gaps between, as i386.c lays them out; vectors in %xmm0-2 or %mm0-2;
 * register ones moved in last; the result taken out of %eax:%edx, %st0, %xmm0 or %mm0.
 */
static void i386_call(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    long pos = 0;
    long cur;
    long pushed = 0;
    long callee_pops = 0; /* the hidden pointer pushed, which the callee pops */
    unsigned rp_used = 0;
    long a;
    long *offs;
    int nx = 0;
    int nm = 0;
    xinst_t c;
    xop_t target;
    int indirect = 0;
    /* a structure result: in %eax:%edx (-freg-struct-return), a vector's in %xmm0 or %mm0, one of a float or a
     * double in %st0, else through a hidden pointer to its storage, pushed first or (regparm) in %eax */
    int agg = in->dst >= 0 && in->rhs == in->dst;
    int ret_vec = agg && (in->agg_class & (CC_ABI_RET_VEC128 | CC_ABI_RET_VEC64)) != 0;
    int ret_x87 = agg && (in->agg_class & CC_ABI_RET_X87) != 0;
    int in_regs = agg && in->agg_class == CC_ABI_RET_EAX_EDX;
    int sret_reg = agg && in->agg_class == CC_ABI_RET_SRET_REG;
    int sret = agg && in->agg_class == 0;
    int agg_obj = agg ? s->obj[in->dst] : -1;

    if ((agg && !in_regs && !sret_reg && !sret && !ret_vec && !ret_x87) || (agg && agg_obj < 0) ||
        (!agg && (in->agg_class & ~(int)CC_ABI_RET_FLOAT) != 0) ||
        (in->dst >= 0 && !agg && cc_ssa_value_class(f, in->dst) != CC_VAL_F64 &&
         f->value_ty[in->dst].kind == CC_TY_AGG)) {
        XS_NOPE(s);
    }
    for (a = 0; a < (long)in->arg_count; ++a) {
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;
        int words = arg_words(s, in, (size_t)a);
        int r = i386_call_arg_regparm(f, in, (size_t)a);
        int ok;

        if (abi == CC_CALL_ARG_ABI_ZERO) {
            continue;
        }
        if (in->args[a] < 0) {
            ok = 0;
        } else if (xs_fp_kind(s, in->args[a]) != XFP_NONE) {
            ok = i386_fp_abi_ok(abi);
        } else if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_REG) {
            ok = (abi & (CC_ABI_VEC128 | CC_ABI_VEC64)) != 0;
        } else {
            ok = (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_DEFAULT || CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM) &&
                 words > 0 && !(r > 0 && r - 1 + words > 3);
        }
        if (!ok) {
            XS_NOPE(s);
        }
    }
    offs = (long *)malloc((in->arg_count > 0 ? in->arg_count : 1) * sizeof(long));
    if (offs == NULL) {
        s->xf.oom = 1;
        return;
    }
    pos = sret ? 4 : 0;
    for (a = 0; a < (long)in->arg_count; ++a) {
        long bytes = arg_bytes(s, in, (size_t)a);
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;

        if (bytes == 0) {
            offs[a] = -1;
            continue;
        }
        pos = (long)align_up_size((size_t)pos, (size_t)i386_stack_align(abi, call_arg_size_bytes(in, (size_t)a, 4)));
        offs[a] = pos;
        pos += bytes;
    }
    /* the body runs with %esp on the stack boundary; it is so again at the call */
    cur = (long)align_up_size((size_t)pos, (size_t)i386_stack_boundary());
    for (a = (long)in->arg_count - 1; a >= -1; --a) {
        long start = a >= 0 ? offs[a] : (sret ? 0 : -1);
        long end = a >= 0 ? offs[a] + arg_bytes(s, in, (size_t)a) : (sret ? 4 : 0);

        if (start < 0 && a >= 0) {
            continue;
        }
        if (cur > end) {
            xop_t k = xop_imm(-(cur - end), 4);

            xs_emit(s, XI_ADDSP, 4, 1, &k, NULL, NULL);
            pushed += cur - end;
        }
        if (a >= 0) {
            push_arg(s, in, (size_t)a, end - start, &pushed);
        } else if (sret) {
            /* the hidden pointer, the first argument; the callee pops it */
            xop_t m = xop_frame(agg_obj, 0, 4);
            xop_t t = xs_temp(s);

            xs_emit(s, XI_LEA, 4, 2, &m, &t, NULL);
            xs_emit(s, XI_PUSH, 4, 1, &t, NULL, NULL);
            callee_pops = 4;
        }
        cur = start < 0 ? 0 : start;
    }
    free(offs);
    for (a = 0; a < (long)in->arg_count; ++a) {
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;

        if (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_REG) {
            continue;
        }
        /* a vector, from its storage, into the next %xmm or %mm register */
        if ((abi & CC_ABI_VEC128) != 0) {
            xop_t m = xs_mem_at(s, in->args[a], 0, 16);
            xop_t x = xop_reg(XR_XMM0 + nx++, 8);

            xs_emit(s, XI_MOV, 8, 2, &m, &x, NULL);
        } else {
            xop_t m = xs_mem_at(s, in->args[a], 0, 8);
            xinst_t x = xi_make(XI_MMLOAD, 4, 1, &m, NULL, NULL);

            x.cc = (unsigned char)nm++;
            xf_emit(&s->xf, &x);
        }
    }
    if (in->op == CC_SSA_CALLI) {
        const char *sym;

        if (xs_sym_of(s, in->lhs, &sym) && strpbrk(sym, "+-") == NULL) {
            target = xop_sym(sym, 0, 4);
        } else {
            target = xs_reg_word(s, in->lhs, 0);
            indirect = 1;
        }
    }
    for (a = 0; a < (long)in->arg_count; ++a) {
        int v = in->args[a];
        int r = i386_call_arg_regparm(f, in, (size_t)a);
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;
        int k;

        if (r == 0 || abi == CC_CALL_ARG_ABI_ZERO) {
            continue;
        }
        for (k = 0; k < arg_words(s, in, (size_t)a); ++k) {
            xop_t d = xop_reg(i386_rp_regs[r - 1 + k], 4);

            if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM) {
                /* a structure's words, from its storage */
                xop_t m = xs_mem_at(s, v, 4 * k, 4);

                xs_emit(s, XI_MOV, 4, 2, &m, &d, NULL);
            } else {
                xs_move_word_to(s, v, k, &d);
            }
            rp_used |= XR_BIT(i386_rp_regs[r - 1 + k]);
        }
    }
    if (sret_reg) {
        xop_t m = xop_frame(agg_obj, 0, 4);
        xop_t eax = xop_reg(XR_AX, 4);

        xs_emit(s, XI_LEA, 4, 2, &m, &eax, NULL);
        rp_used |= XR_BIT(XR_AX);
    }
    if (in->call_chain > 0) {
        /* a GNU nested function's environment, in the static chain register -- which regparm's third is too */
        xop_t ecx = xop_reg(XR_CX, 4);

        if ((rp_used & XR_BIT(XR_CX)) != 0) {
            XS_NOPE(s);
        }
        xs_move_word_to(s, in->call_chain - 1, 0, &ecx);
        rp_used |= XR_BIT(XR_CX);
    }
    if (indirect) {
        c = xi_make(XI_CALL, 4, 1, &target, NULL, NULL);
    } else {
        c = xi_make(XI_CALL, 4, 0, NULL, NULL, NULL);
        c.sym = call_name(s, in->op == CC_SSA_CALLI ? target.sym : in->sym, in->call_binds_local, &rp_used);
    }
    c.imp_use = rp_used;
    c.clobber = I386_CALLER_SAVED;
    c.aux = (int)callee_pops; /* (what the stack pointer moves by at the call itself) */
    if (in_regs) {
        c.imp_def = XR_BIT(XR_AX) | XR_BIT(XR_DX);
    } else if (in->dst >= 0 && !agg && xs_fp_kind(s, in->dst) == XFP_NONE) {
        c.imp_def = XR_BIT(XR_AX) | (xs_is_pair(s, in->dst) ? XR_BIT(XR_DX) : 0);
    }
    xf_emit(&s->xf, &c);
    if (pushed > 0) {
        xop_t k = xop_imm(pushed, 4);

        xs_emit(s, XI_ADDSP, 4, 1, &k, NULL, NULL);
    }
    if (ret_vec) {
        if ((in->agg_class & CC_ABI_RET_VEC128) != 0) {
            xop_t x0 = xop_reg(XR_XMM0, 8);
            xop_t m = xop_frame(agg_obj, 0, 16);

            xs_emit(s, XI_MOV, 8, 2, &x0, &m, NULL);
        } else {
            xop_t m = xop_frame(agg_obj, 0, 8);
            xinst_t x = xi_make(XI_MMSTORE, 4, 1, &m, NULL, NULL);

            xf_emit(&s->xf, &x);
        }
        return;
    }
    if (ret_x87) {
        /* -freg-struct-return: a structure of one float or double came back in %st0 */
        xop_t m = xop_frame(agg_obj, 0, in->imm == 4 ? 4 : 8);

        xs_emit(s, XI_FSTP, 4, 1, &m, NULL, NULL);
        return;
    }
    if (in_regs) {
        /* by its size: a 1- or 2-byte structure has no room for all of %eax */
        int size = in->imm == 1 ? 1 : in->imm == 2 ? 2 : 4;
        xop_t eax = xop_reg(XR_AX, size);
        xop_t m = xop_frame(agg_obj, 0, size);

        xs_emit(s, XI_MOV, size, 2, &eax, &m, NULL);
        if (in->imm > 4) {
            xop_t edx = xop_reg(XR_DX, 4);
            xop_t m1 = xop_frame(agg_obj, 4, 4);

            xs_emit(s, XI_MOV, 4, 2, &edx, &m1, NULL);
        }
        return;
    }
    if (agg) {
        return; /* the callee filled the storage */
    }
    if (xs_fp_kind(s, in->dst) != XFP_NONE || (in->dst < 0 && in->call_ret_x87)) {
        /* a floating result, in %st0: popped into the value, or thrown away */
        xs_fp_pop(s, in->dst);
        return;
    }
    if (in->dst >= 0) {
        long ret_bytes = in->imm > 0 ? in->imm : 4;
        xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), 4);

        if (xs_is_pair(s, in->dst)) {
            xop_t eax = xop_reg(XR_AX, 4);
            xop_t edx = xop_reg(XR_DX, 4);
            xop_t d1 = xop_reg(xs_vreg_of(s, in->dst, 1), 4);

            xs_emit(s, XI_MOV, 4, 2, &eax, &d, NULL);
            xs_emit(s, XI_MOV, 4, 2, &edx, &d1, NULL);
        } else if ((ret_bytes == 1 || ret_bytes == 2) && xs_extends_taken(s, in->dst, (int)ret_bytes)) {
            /* the callee need not have extended it */
            xop_t r = xop_reg(XR_AX, (int)ret_bytes);

            xs_emit(s, in->is_unsigned ? XI_MOVZX : XI_MOVSX, 4, 2, &r, &d, NULL);
        } else {
            xop_t eax = xop_reg(XR_AX, 4);

            xs_emit(s, XI_MOV, 4, 2, &eax, &d, NULL);
        }
    }
}

/** A parameter: from the register it came in (moved out at entry), else its stack slot. */
static void i386_param(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    int pi = in->param_index;
    unsigned char abi = f->param_abi != NULL ? f->param_abi[pi] : CC_CALL_ARG_ABI_DEFAULT;
    int kind = CC_ABI_KIND(abi);
    int part;

    if (abi == CC_CALL_ARG_ABI_ZERO) {
        return;
    }
    if (xs_fp_kind(s, in->dst) != XFP_NONE) {
        /* a floating parameter, from its words on the stack: a float's one, a double's two, a long double's three */
        int o = xf_new_obj(&s->xf, XF_FIXED, 16, 4, i386_param_offset(f, pi));

        xs_fp_from_mem(s, in->dst, xop_frame(o, 0, 8),
                       abi == CC_CALL_ARG_ABI_FLOAT ? 4 : abi == CC_CALL_ARG_ABI_LDOUBLE ? 16 : 8);
        return;
    }
    if (kind == CC_CALL_ARG_ABI_AGG_REG) {
        /* a vector that came in %xmm0-2 or %mm0-2: into its storage (lhs), whose address is the value */
        int o = in->lhs >= 0 ? s->obj[in->lhs] : -1;
        int r = i386_vector_param_reg(f, pi);
        xop_t m;
        xop_t d;

        if (o < 0 || (abi & (CC_ABI_VEC128 | CC_ABI_VEC64)) == 0) {
            XS_NOPE(s);
        }
        if ((abi & CC_ABI_VEC128) != 0) {
            xop_t x = xop_reg(XR_XMM0 + r, 8);

            m = xop_frame(o, 0, 16);
            xs_emit(s, XI_MOV, 8, 2, &x, &m, NULL);
        } else {
            xinst_t x;

            m = xop_frame(o, 0, 8);
            x = xi_make(XI_MMSTORE, 4, 1, &m, NULL, NULL);
            x.cc = (unsigned char)r;
            xf_emit(&s->xf, &x);
        }
        m = xop_frame(o, 0, 4);
        d = xop_reg(xs_vreg_of(s, in->dst, 0), 4);
        xs_emit(s, XI_LEA, 4, 2, &m, &d, NULL);
        return;
    }
    if (kind == CC_CALL_ARG_ABI_AGG_MEM && CC_ABI_REGPARM(abi) > 0) {
        /* a structure that came in registers (regparm): their words, stored at entry, whose address is the value */
        xop_t m;
        xop_t d;

        if (s->frame_val[in->dst] >= 0) {
            return;
        }
        if (s->param_obj[pi] < 0) {
            XS_NOPE(s);
        }
        m = xop_frame(s->param_obj[pi], 0, 4);
        d = xop_reg(xs_vreg_of(s, in->dst, 0), 4);
        xs_emit(s, XI_LEA, 4, 2, &m, &d, NULL);
        return;
    }
    if (kind == CC_CALL_ARG_ABI_AGG_MEM && CC_ABI_REGPARM(abi) == 0) {
        /* a structure passed by value: its copy on the stack, whose address is the value */
        int o;

        if (s->frame_val[in->dst] >= 0) {
            return;
        }
        o = xf_new_obj(&s->xf, XF_FIXED, 4, 4, i386_param_offset(f, pi));
        {
            xop_t m = xop_frame(o, 0, 4);
            xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), 4);

            xs_emit(s, XI_LEA, 4, 2, &m, &d, NULL);
        }
        return;
    }
    if (kind != CC_CALL_ARG_ABI_DEFAULT) {
        XS_NOPE(s);
    }
    for (part = 0; part < (xs_is_pair(s, in->dst) ? 2 : 1); ++part) {
        xop_t d = xop_reg(xs_vreg_of(s, in->dst, part), 4);

        if (CC_ABI_REGPARM(abi) > 0) {
            xop_t r = xop_reg(part ? s->param_hi[pi] : s->param_lo[pi], 4);

            if (r.reg == XR_NONE) {
                XS_NOPE(s);
            }
            xs_emit(s, XI_MOV, 4, 2, &r, &d, NULL);
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
            xs_emit(s, XI_MOV, 4, 2, &m, &d, NULL);
        }
    }
}

/** A return: the value in %eax (and %edx), or a structure copied to the caller's buffer. */
static void i386_ret(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    xinst_t x = xi_make(XI_RET, 4, 0, NULL, NULL, NULL);

    if (f->ret_abi == CC_CALL_ARG_ABI_AGG_MEM) {
        /* copied to the caller's buffer, whose address is returned; the callee pops it if it was pushed */
        xop_t h = xop_reg(s->hidden, 4);
        xop_t eax = xop_reg(XR_AX, 4);

        if (in->agg_class != 0 && in->agg_class != CC_ABI_RET_SRET_REG) {
            XS_NOPE(s);
        }
        if (in->lhs >= 0 && in->imm > 0) {
            xop_t src = xs_reg_word(s, in->lhs, 0);

            sel_memcpy(s, &h, &src, (long)in->imm);
        }
        xs_emit(s, XI_MOV, 4, 2, &h, &eax, NULL);
        x.imp_use = XR_BIT(XR_AX);
        x.aux = f->sret_reg ? 0 : 4;
        xf_emit(&s->xf, &x);
        return;
    }
    /* a structure returned in %eax:%edx (-freg-struct-return) is returned as its words */
    if (in->agg_class != 0 && in->agg_class != CC_ABI_RET_EAX_EDX &&
        (in->agg_class & (CC_ABI_RET_VEC128 | CC_ABI_RET_VEC64 | CC_ABI_RET_X87)) == 0) {
        XS_NOPE(s);
    }
    if (in->lhs >= 0 && (in->agg_class & (CC_ABI_RET_VEC128 | CC_ABI_RET_VEC64)) != 0) {
        /* a vector, from the storage lhs points at, to %xmm0 or %mm0 */
        if ((in->agg_class & CC_ABI_RET_VEC128) != 0) {
            xop_t m = xs_mem_at(s, in->lhs, 0, 16);
            xop_t x0 = xop_reg(XR_XMM0, 8);

            xs_emit(s, XI_MOV, 8, 2, &m, &x0, NULL);
        } else {
            xop_t m = xs_mem_at(s, in->lhs, 0, 8);
            xinst_t mm = xi_make(XI_MMLOAD, 4, 1, &m, NULL, NULL);

            xf_emit(&s->xf, &mm);
        }
        xf_emit(&s->xf, &x);
        return;
    }
    if (in->lhs >= 0 && (in->agg_class & CC_ABI_RET_X87) != 0) {
        /* -freg-struct-return: a structure of one float or double, as that value in %st0 */
        xop_t m = xs_mem_at(s, in->lhs, 0, in->imm == 4 ? 4 : 8);

        xs_emit(s, XI_FLD, 4, 1, &m, NULL, NULL);
        xf_emit(&s->xf, &x);
        return;
    }
    if (in->lhs >= 0 && xs_fp_kind(s, in->lhs) != XFP_NONE) {
        /* in %st0 */
        xs_fp_push(s, in->lhs);
        xf_emit(&s->xf, &x);
        return;
    }
    if (in->lhs >= 0) {
        xop_t eax = xop_reg(XR_AX, 4);

        if (f->value_ty[in->lhs].kind == CC_TY_AGG) {
            XS_NOPE(s);
        }
        xs_move_word_to(s, in->lhs, 0, &eax);
        x.imp_use = XR_BIT(XR_AX);
        if (in->imm > 4 && in->imm <= 8) {
            xop_t edx = xop_reg(XR_DX, 4);

            if (in->rhs >= 0) {
                xs_move_word_to(s, in->rhs, 0, &edx);
            } else if (xs_is_pair(s, in->lhs)) {
                xs_move_word_to(s, in->lhs, 1, &edx);
            } else {
                XS_NOPE(s);
            }
            x.imp_use |= XR_BIT(XR_DX);
        } else if (in->imm > 8) {
            XS_NOPE(s);
        }
    }
    xf_emit(&s->xf, &x);
}

/** va_start: the address of the first variadic argument on the stack. */
static void i386_va_start(xsel_t *s, const cc_ssa_instr_t *in) {
    int o = xf_new_obj(&s->xf, XF_FIXED, 4, 4, i386_variadic_start_offset(s->f, (int)in->imm));
    xop_t m = xop_frame(o, 0, 4);
    xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), 4);

    xs_emit(s, XI_LEA, 4, 2, &m, &d, NULL);
}

/** Whether the function is one this path takes on i386: NULL, or why not. */
static const char *i386_function_ok(const xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    size_t i;

    if (f->ret_type == CC_VAL_F64 ? !i386_fp_abi_ok(f->ret_abi)
                                  : (f->ret_abi != CC_CALL_ARG_ABI_DEFAULT && f->ret_abi != CC_CALL_ARG_ABI_AGG_MEM)) {
        return "its return";
    }
    for (i = 0; i < f->param_count; ++i) {
        unsigned char abi = f->param_abi != NULL ? f->param_abi[i] : CC_CALL_ARG_ABI_DEFAULT;

        if (f->param_types[i] == CC_VAL_F64 ? !i386_fp_abi_ok(abi)
                                            : (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT &&
                                               abi != CC_CALL_ARG_ABI_ZERO &&
                                               CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_MEM &&
                                               !(CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_REG &&
                                                 (abi & (CC_ABI_VEC128 | CC_ABI_VEC64)) != 0))) {
            return "a parameter's ABI";
        }
    }
    return NULL;
}

/**
 * Before the first instruction: a structure parameter's stack copy, the caller's buffer's address for a structure
 * result, and each register parameter out of its register, before anything can touch them.
 */
static int i386_begin(xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    size_t i;

    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_PARAM && in->dst >= 0 && s->ndefs[in->dst] == 1 && f->param_abi != NULL &&
            in->param_index >= 0 && (size_t)in->param_index < f->param_count &&
            CC_ABI_KIND(f->param_abi[in->param_index]) == CC_CALL_ARG_ABI_AGG_MEM &&
            CC_ABI_REGPARM(f->param_abi[in->param_index]) == 0) {
            s->frame_val[in->dst] = xf_new_obj(&s->xf, XF_FIXED, 4, 4, i386_param_offset(f, in->param_index));
        }
    }
    if (i386_pic_on()) {
        /* the GOT's address, made once at entry; nothing reads it, the allocator drops it */
        xop_t g = xs_temp(s);
        xinst_t x = xi_make(XI_GOTBASE, 4, 1, &g, NULL, NULL);

        xf_emit(&s->xf, &x);
        s->got = g.reg;
    }
    /* a structure result: the caller's buffer's address, from %eax (regparm) or the stack */
    if (f->ret_abi == CC_CALL_ARG_ABI_AGG_MEM) {
        xop_t h = xs_temp(s);

        if (f->sret_reg) {
            xop_t eax = xop_reg(XR_AX, 4);

            xs_emit(s, XI_MOV, 4, 2, &eax, &h, NULL);
        } else {
            int o = xf_new_obj(&s->xf, XF_FIXED, 4, 4, 8);
            xop_t m = xop_frame(o, 0, 4);

            xf_vreg(&s->xf, h.reg)->home_obj = o;
            xs_emit(s, XI_MOV, 4, 2, &m, &h, NULL);
        }
        s->hidden = h.reg;
    }
    for (i = 0; i < f->param_count; ++i) {
        unsigned char abi = f->param_abi != NULL ? f->param_abi[i] : CC_CALL_ARG_ABI_DEFAULT;
        int r = CC_ABI_REGPARM(abi);
        int words;
        int k;
        size_t j;

        if (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_MEM || r == 0) {
            continue;
        }
        /* a structure in registers (regparm): its words stored at once into a frame object of its own */
        words = i386_param_slot_bytes(x86_64_param_size_bytes(f, (int)i, 4)) / 4;
        if (r - 1 + words > 3) {
            return 1;
        }
        s->param_obj[i] = xf_new_obj(&s->xf, XF_LOCAL, 4 * words, 4, 0);
        if (s->param_obj[i] < 0) {
            return -1;
        }
        for (k = 0; k < words; ++k) {
            xop_t from = xop_reg(i386_rp_regs[r - 1 + k], 4);
            xop_t to = xop_frame(s->param_obj[i], 4 * k, 4);

            xs_emit(s, XI_MOV, 4, 2, &from, &to, NULL);
        }
        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];

            if (in->op == CC_SSA_PARAM && in->param_index == (int)i && in->dst >= 0 && s->ndefs[in->dst] == 1) {
                s->frame_val[in->dst] = s->param_obj[i];
            }
        }
    }
    for (i = 0; i < f->param_count; ++i) {
        unsigned char abi = f->param_abi != NULL ? f->param_abi[i] : CC_CALL_ARG_ABI_DEFAULT;
        int r = CC_ABI_REGPARM(abi);
        int pv = f->param_values != NULL ? f->param_values[i] : -1;
        int words = pv >= 0 && xs_is_pair(s, pv) ? 2 : 1;
        int k;

        if (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT || r == 0) {
            continue;
        }
        if (r - 1 + words > 3) {
            return 1;
        }
        for (k = 0; k < words; ++k) {
            xop_t from = xop_reg(i386_rp_regs[r - 1 + k], 4);
            xop_t to = xs_temp(s);

            xs_emit(s, XI_MOV, 4, 2, &from, &to, NULL);
            if (k == 0) {
                s->param_lo[i] = to.reg;
            } else {
                s->param_hi[i] = to.reg;
            }
        }
    }
    return s->xf.oom ? -1 : 0;
}

/** A call of runtime routine @p sym (i386), through its PLT under -fPIC: reading @p use, writing @p def. */
static void i386_libcall(xsel_t *s, const char *sym, unsigned use, unsigned def) {
    xinst_t c = xi_make(XI_CALL, 4, 0, NULL, NULL, NULL);

    c.sym = call_name(s, sym, 0, &use);
    c.imp_use = use;
    c.imp_def = def;
    c.clobber = I386_CALLER_SAVED;
    xf_emit(&s->xf, &c);
}

/** Whether symbol @p sym's address is read from the GOT under -fPIC: it may bind outside the object. */
static int i386_sym_got(const xsel_t *s, const char *sym) {
    return s->got != XR_NONE && !module_symbol_binds_locally(s->m, sym);
}

int i386_mir_emit_function(FILE *out, const cc_ssa_module_t *m, const cc_ssa_function_t *f, size_t fn_index,
                           const char *src_path, int emit_debug, cc_diag_t *diag) {
    /* the x87's floating code: general registers only; SSE's: %xmm0-7 as well */
    static const xtarget_t alloc = {I386_GPRS,
                                    0,
                                    XR_BIT(XR_BX) | XR_BIT(XR_SI) | XR_BIT(XR_DI),
                                    XR_BIT(XR_AX) | XR_BIT(XR_CX) | XR_BIT(XR_DX) | XR_BIT(XR_BX),
                                    i386_order,
                                    6,
                                    4};
    static const xtarget_t alloc_sse = {I386_GPRS | XR_XMM_MASK(8),
                                        XR_XMM_MASK(8),
                                        XR_BIT(XR_BX) | XR_BIT(XR_SI) | XR_BIT(XR_DI),
                                        XR_BIT(XR_AX) | XR_BIT(XR_CX) | XR_BIT(XR_DX) | XR_BIT(XR_BX),
                                        i386_order,
                                        (int)(sizeof(i386_order) / sizeof(i386_order[0])),
                                        4};
    xsel_target_t t;

    memset(&t, 0, sizeof(t));
    t.word = 4;
    t.sse_fp = i386_sse_fp();
    t.alloc = t.sse_fp ? &alloc_sse : &alloc;
    t.caller_saved = I386_CALLER_SAVED;
    t.saved = i386_saved;
    t.nsaved = (int)(sizeof(i386_saved) / sizeof(i386_saved[0]));
    t.fp_residue = 8; /* %ebp is 8 past a 16-byte boundary */
    t.stack_boundary = i386_stack_boundary();
    t.sym_imm = !i386_pic_on();
    t.sym_got = i386_sym_got;
    t.libcall = i386_libcall;
    t.chain_reg = XR_CX;
    t.function_ok = i386_function_ok;
    t.begin = i386_begin;
    t.param = i386_param;
    t.call = i386_call;
    t.ret = i386_ret;
    t.va_start = i386_va_start;
    return x86_mir_emit_function(out, &t, m, f, fn_index, src_path, emit_debug, diag);
}
