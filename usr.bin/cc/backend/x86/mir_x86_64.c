/*
 * mir_x86_64.c: x86-64's part of selection into machine IR (mir_sel.c):
 * the System V psABI's parameters, calls and returns, as x86_64.c's own
 * classification (abi64_param_loc(), abi64_classify_call_args()) places
 * them.
 *
 * Integer arguments go in %rdi, %rsi, %rdx, %rcx, %r8 and %r9, the rest
 * stored on the stack below them, 16-aligned at the call; a variadic call
 * says in %al how many vector registers it used (none: this path takes no
 * floating point).  A value comes back in %rax, a structure of two
 * eightbytes in %rax:%rdx, a bigger one through a hidden pointer in %rdi.
 * Symbols' addresses are made off %rip, or read from the GOT under -fPIC
 * and for a global declared extern, as x86_64.c does.  linscan.c gives
 * out every general register but %rsp and %rbp, the caller-saved first.
 *
 * A variadic function saves its argument registers at entry, and va_start
 * fills the record its va_lists point at, as the psABI lays both out.
 *
 * A structure returned through memory is copied to the buffer whose
 * address came in %rdi, and that address returned in %rax.
 *
 * Floats and doubles are vector registers (mir_fp.c): passed in %xmm0..7
 * and returned in %xmm0, a float in single precision across the call; a
 * long double travels on the stack and comes back in %st0.  A structure's
 * SSE eightbytes go in vector registers, a 16-byte vector whole in one.
 */

#include "cc_x86_mir.h"

#include "cc_options.h"
#include "cc_x86.h"

#include <stdlib.h>
#include <string.h>

/** The allocation order: the caller-saved registers first, which cost no save. */
static const int x64_order[] = {XR_AX,       XR_CX,       XR_DX,       XR_SI,       XR_DI,       XR_R8,
                                XR_R9,       XR_R10,      XR_R11,      XR_BX,       XR_R12,      XR_R13,
                                XR_R14,      XR_R15,      XR_XMM0,     XR_XMM0 + 1, XR_XMM0 + 2, XR_XMM0 + 3,
                                XR_XMM0 + 4, XR_XMM0 + 5, XR_XMM0 + 6, XR_XMM0 + 7, XR_XMM0 + 8, XR_XMM0 + 9,
                                XR_XMM0 + 10, XR_XMM0 + 11, XR_XMM0 + 12, XR_XMM0 + 13, XR_XMM0 + 14, XR_XMM15};

/** The callee-saved registers, in the order the prologue pushes them. */
static const int x64_saved[] = {XR_BX, XR_R12, XR_R13, XR_R14, XR_R15};

/** The registers a call clobbers: nine general ones and every vector one. */
#define X64_CALLER_SAVED                                                                                          \
    (XR_BIT(XR_AX) | XR_BIT(XR_CX) | XR_BIT(XR_DX) | XR_BIT(XR_SI) | XR_BIT(XR_DI) | XR_BIT(XR_R8) |              \
     XR_BIT(XR_R9) | XR_BIT(XR_R10) | XR_BIT(XR_R11) | XR_XMM_MASK(16))

/** The integer argument registers, in order. */
static const int x64_arg_regs[6] = {XR_DI, XR_SI, XR_DX, XR_CX, XR_R8, XR_R9};

/** Whether this compilation is position-independent, as emit_x86_64() was told. */
static int x64_pic;

/**
 * Extend @p bytes bytes of @p src into the 64-bit register @p d, as an integer of that size is held: sign- or
 * zero-extended (@p is_unsigned); 8 bytes are moved as they are.
 */
static void extend_to(xsel_t *s, const xop_t *src, const xop_t *d, int bytes, int is_unsigned) {
    xop_t a = *src;
    xop_t r = *d;

    if (bytes >= 8 || bytes <= 0) {
        a.size = 8;
        xs_emit(s, XI_MOV, 8, 2, &a, &r, NULL);
        return;
    }
    a.size = (unsigned char)bytes;
    /* zero-extended into the 32-bit register, which clears the rest */
    r.size = (unsigned char)(is_unsigned ? 4 : 8);
    xs_emit(s, is_unsigned ? XI_MOVZX : XI_MOVSX, 8, 2, &a, &r, NULL);
}

/** Copy @p size bytes from memory @p from to memory @p to, the biggest pieces first, through a register. */
static void copy_bytes(xsel_t *s, xop_t from, xop_t to, long size) {
    static const int pieces[4] = {8, 4, 2, 1};
    long off = 0;
    int p;

    for (p = 0; p < 4; ++p) {
        while (size - off >= pieces[p]) {
            xop_t t = xs_temp(s);
            xop_t a = from;
            xop_t b = to;

            a.disp += off;
            b.disp += off;
            a.size = (unsigned char)pieces[p];
            b.size = (unsigned char)pieces[p];
            t.size = (unsigned char)pieces[p];
            xs_emit(s, XI_MOV, pieces[p], 2, &a, &t, NULL);
            xs_emit(s, XI_MOV, pieces[p], 2, &t, &b, NULL);
            off += pieces[p];
        }
    }
}

/**
 * The size of the structure @p f returns through a hidden pointer -- the largest its returns or its callers in
 * the module say, as x86_64.c decides it -- or 0 when it returns none so.
 */
static long sret_size(const xsel_t *s) {
    long a = x86_64_function_sret_size(s->f);
    long b = x86_64_infer_sret_size_from_calls(s->m, s->f);

    return a > b ? a : b;
}

/** The first argument register the parameters take: past %rdi when it holds a structure result's address. */
static size_t gpr_bias(const xsel_t *s) {
    return s->hidden != XR_NONE ? 1 : 0;
}

/**
 * Whether ABI byte @p abi passes a structure in registers: AGG_REG, each eightbyte in a general register or (SSE)
 * a vector one, or a 16-byte vector whole in one.
 */
static int agg_in_regs(unsigned char abi) {
    return CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_REG;
}

/** Whether floating ABI byte @p abi is one this path passes: a double, a float in single precision, a long double. */
static int fp_abi_ok(unsigned char abi) {
    return abi == CC_CALL_ARG_ABI_DEFAULT || abi == CC_CALL_ARG_ABI_FLOAT || abi == CC_CALL_ARG_ABI_LDOUBLE;
}

/** The bytes floating ABI byte @p abi passes in memory: 16 for a long double, 4 for a float, else 8. */
static int fp_abi_bytes(unsigned char abi) {
    return abi == CC_CALL_ARG_ABI_LDOUBLE ? 16 : abi == CC_CALL_ARG_ABI_FLOAT ? 4 : 8;
}

/** Vector argument register @p k, %xmm0 .. %xmm7. */
static int xmm_arg(size_t k) {
    return XR_XMM0 + (int)k;
}

/** Whether an eightbyte of @p bytes bytes is one access (1, 2, 4 or 8 bytes). */
static int whole_piece(long bytes) {
    return bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8;
}

/**
 * Load the eightbytes of the structure at IR address @p v into the registers @p loc names: each general one
 * zero-extended past its bytes, as the old emitter loads them, each SSE one by movss or movsd, a vector whole by
 * movups.
 */
static void load_agg_regs(xsel_t *s, int v, const abi_loc_t *loc, unsigned *used) {
    long size = (long)loc->size;
    size_t gi = loc->index;
    size_t xi = loc->xmm_index;
    long k;

    if ((loc->sse & ABI_SSE_WHOLE) != 0) {
        xop_t m = xs_mem_at(s, v, 0, 16);
        xop_t d = xop_reg(xmm_arg(xi), 8);

        xs_emit(s, XI_MOV, 8, 2, &m, &d, NULL);
        *used |= XR_BIT(xmm_arg(xi));
        return;
    }
    for (k = 0; k * 8 < size; ++k) {
        long bytes = size - k * 8 > 8 ? 8 : size - k * 8;
        int reg;
        xop_t m = xs_mem_at(s, v, 8 * k, (int)bytes);
        xop_t d;

        if ((loc->sse & (1u << k)) != 0) {
            reg = xmm_arg(xi++);
            d = xop_reg(reg, 8);
            m.size = (unsigned char)(bytes > 4 ? 8 : 4);
            xs_emit(s, XI_MOV, 8, 2, &m, &d, NULL);
            *used |= XR_BIT(reg);
            continue;
        }
        reg = x64_arg_regs[gi++];
        d = xop_reg(reg, bytes == 8 ? 8 : 4);
        if (bytes == 8 || bytes == 4) {
            xs_emit(s, XI_MOV, (int)bytes, 2, &m, &d, NULL);
        } else if (bytes <= 2) {
            xs_emit(s, XI_MOVZX, 4, 2, &m, &d, NULL);
        } else {
            /* 3, 5, 6 or 7 bytes: in pieces, by way of a register of its own */
            xop_t t = xs_temp(s);

            xs_load_bytes(s, m, t, (int)bytes);
            d.size = 8;
            xs_emit(s, XI_MOV, 8, 2, &t, &d, NULL);
        }
        *used |= XR_BIT(reg);
    }
}

/** The agg_class bits a structure result in registers may have. */
#define X64_RET_CLASSES                                                                                            \
    (CC_ABI_SSE0 | CC_ABI_SSE1 | CC_ABI_RET_VEC128 | CC_ABI_RET_X87 | CC_ABI_RET_X87_COMPLEX | CC_ABI_RET_MEMORY)

/**
 * The registers a structure of @p size bytes and return classes @p cls comes back in, from %rax and %rdx, %xmm0
 * and %xmm1 by each eightbyte's class; none for the x87's.
 */
static unsigned agg_ret_regs(long size, int cls) {
    unsigned m = 0;
    int gi = 0;
    int xi = 0;
    long k;

    if ((cls & (CC_ABI_RET_X87 | CC_ABI_RET_X87_COMPLEX)) != 0) {
        return 0;
    }
    if ((cls & CC_ABI_RET_VEC128) != 0) {
        return XR_BIT(XR_XMM0);
    }
    for (k = 0; k * 8 < size && k < 2; ++k) {
        if ((cls & (k == 0 ? CC_ABI_SSE0 : CC_ABI_SSE1)) != 0) {
            m |= XR_BIT(XR_XMM0 + xi++);
        } else {
            m |= XR_BIT(gi++ == 0 ? XR_AX : XR_DX);
        }
    }
    return m;
}

/**
 * A call: stack arguments stored below the call's %rsp, register ones moved in -- a double in its vector
 * register, a float rounded there to single precision, a long double on the stack -- and the result out of %rax,
 * %xmm0 or the x87's %st0.
 */
static void x64_call(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    int agg = in->dst >= 0 && in->rhs == in->dst && in->imm > 0;
    /* an empty structure result: nothing comes back, and the value is its storage's address (rhs) */
    int empty = in->dst >= 0 && in->rhs >= 0 && in->imm == 0 && in->agg_class != 0;
    int agg_obj = agg ? s->obj[in->dst] : -1;
    int sret = agg && x86_64_agg_ret_in_memory(in->imm, in->agg_class);
    int agg_x87 = agg && (in->agg_class & (CC_ABI_RET_X87 | CC_ABI_RET_X87_COMPLEX)) != 0;
    int fp_dst = !agg && xs_fp_kind(s, in->dst) != XFP_NONE;
    abi_loc_t *locs = NULL;
    size_t stack_bytes = 0;
    size_t xmm = 0;
    long total;
    unsigned used = 0;
    size_t a;
    xinst_t c;
    xop_t target;
    int indirect = 0;

    if ((agg && agg_obj < 0) || (agg && !sret && (in->agg_class & ~(int)X64_RET_CLASSES) != 0) ||
        (!agg && !empty && (in->agg_class & ~(int)CC_ABI_RET_FLOAT) != 0) ||
        (in->dst >= 0 && !agg && !fp_dst && f->value_ty[in->dst].kind == CC_TY_AGG &&
         f->value_ty[in->dst].size > 8)) {
        XS_NOPE(s);
    }
    if (agg && !sret && !agg_x87 && in->imm > 16) {
        XS_NOPE(s);
    }
    if (in->arg_count > 0) {
        locs = (abi_loc_t *)calloc(in->arg_count, sizeof(*locs));
        if (locs == NULL) {
            s->xf.oom = 1;
            return;
        }
        abi64_classify_call_args(f, in, sret ? 1 : 0, locs, &stack_bytes, &xmm);
    }
    for (a = 0; a < in->arg_count; ++a) {
        int v = in->args[a];
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;
        int bad;

        if (locs[a].kind == ABI_LOC_NONE) {
            continue;
        }
        if (v >= 0 && xs_fp_kind(s, v) != XFP_NONE) {
            bad = !fp_abi_ok(abi) || (locs[a].kind != ABI_LOC_XMM && locs[a].kind != ABI_LOC_STACK) ||
                  (locs[a].kind == ABI_LOC_XMM && (locs[a].index >= 8 || xs_fp_kind(s, v) != XFP_SSE));
        } else {
            bad = v < 0 || locs[a].kind == ABI_LOC_XMM ||
                  (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT && CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_MEM &&
                   !agg_in_regs(abi)) ||
                  (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_DEFAULT && f->value_ty[v].kind == CC_TY_AGG &&
                   !s->is_addr[v] && f->value_ty[v].size > 8);
        }
        if (bad) {
            free(locs);
            XS_NOPE(s);
        }
    }
    total = (long)align_up_size(stack_bytes, 16);
    if (total > 0) {
        xop_t k = xop_imm(-total, 8);

        xs_emit(s, XI_ADDSP, 8, 1, &k, NULL, NULL);
    }
    for (a = 0; a < in->arg_count; ++a) {
        int v = in->args[a];
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;
        xop_t at;

        if (locs[a].kind != ABI_LOC_STACK) {
            continue;
        }
        at = xop_mem(XR_SP, (long long)locs[a].index, 8);
        if (xs_fp_kind(s, v) != XFP_NONE) {
            xs_fp_to_mem(s, v, at, fp_abi_bytes(abi));
            if (abi == CC_CALL_ARG_ABI_LDOUBLE) {
                /* the two bytes past the 80, deterministic, as x86_64.c leaves them */
                xop_t z = xop_imm(0, 1);
                xop_t p = at;

                p.size = 1;
                p.disp += 10;
                xs_emit(s, XI_MOV, 1, 2, &z, &p, NULL);
                p.disp += 1;
                xs_emit(s, XI_MOV, 1, 2, &z, &p, NULL);
            }
        } else if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM || agg_in_regs(abi)) {
            /* a structure passed by value: a copy of it */
            copy_bytes(s, xs_mem_at(s, v, 0, 8), at, (long)locs[a].size);
        } else {
            xs_move_word_to(s, v, 0, &at);
        }
    }
    if (in->op == CC_SSA_CALLI) {
        const char *sym;

        if (xs_sym_of(s, in->lhs, &sym) && strpbrk(sym, "+-") == NULL && sym[0] != '.') {
            target = xop_sym(sym, 0, 8);
        } else {
            target = xs_reg_word(s, in->lhs, 0);
            indirect = 1;
        }
    }
    for (a = 0; a < in->arg_count; ++a) {
        unsigned char abi = in->call_arg_abi != NULL ? in->call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;
        xop_t d;

        if (locs[a].kind == ABI_LOC_XMM) {
            /* a double in its register; a float in single precision */
            xop_t r = xs_fp_reg(s, in->args[a]);

            d = xop_reg(xmm_arg(locs[a].index), 8);
            xs_emit(s, abi == CC_CALL_ARG_ABI_FLOAT ? XI_CVTSD2SS : XI_MOV, 8, 2, &r, &d, NULL);
            used |= XR_BIT(d.reg);
            continue;
        }
        if (locs[a].kind != ABI_LOC_GPR) {
            continue;
        }
        if (agg_in_regs(abi)) {
            /* a structure in registers: its eightbytes, from where the value points */
            load_agg_regs(s, in->args[a], &locs[a], &used);
            continue;
        }
        d = xop_reg(x64_arg_regs[locs[a].index], 8);
        xs_move_word_to(s, in->args[a], 0, &d);
        used |= XR_BIT(x64_arg_regs[locs[a].index]);
    }
    free(locs);
    if (sret) {
        xop_t m = xop_frame(agg_obj, 0, 8);
        xop_t rdi = xop_reg(XR_DI, 8);

        xs_emit(s, XI_LEA, 8, 2, &m, &rdi, NULL);
        used |= XR_BIT(XR_DI);
    }
    if (in->call_chain > 0) {
        /* a GNU nested function's environment, in the static chain register */
        xop_t r10 = xop_reg(XR_R10, 8);

        xs_move_word_to(s, in->call_chain - 1, 0, &r10);
        used |= XR_BIT(XR_R10);
    }
    if (in->call_is_variadic) {
        /* %al: the vector registers the arguments took */
        xop_t n = xop_imm((long long)xmm, 4);
        xop_t eax = xop_reg(XR_AX, 4);

        xs_emit(s, XI_MOV, 4, 2, &n, &eax, NULL);
        used |= XR_BIT(XR_AX);
    }
    if (indirect) {
        c = xi_make(XI_CALL, 8, 1, &target, NULL, NULL);
    } else {
        c = xi_make(XI_CALL, 8, 0, NULL, NULL, NULL);
        c.sym = in->op == CC_SSA_CALLI ? target.sym : in->sym;
    }
    c.imp_use = used;
    c.clobber = X64_CALLER_SAVED;
    if (agg && !sret) {
        c.imp_def = agg_ret_regs(in->imm, in->agg_class);
    } else if (fp_dst && !in->call_ret_x87) {
        c.imp_def = XR_BIT(XR_XMM0);
    } else if (in->dst >= 0 && !empty) {
        c.imp_def = XR_BIT(XR_AX);
    }
    xf_emit(&s->xf, &c);
    if (total > 0) {
        xop_t k = xop_imm(total, 8);

        xs_emit(s, XI_ADDSP, 8, 1, &k, NULL, NULL);
    }
    if (empty) {
        if (in->dst != in->rhs) {
            xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), 8);

            xs_move_word_to(s, in->rhs, 0, &d);
        }
        return;
    }
    if (agg) {
        long k;
        int gi = 0;
        int xi = 0;

        if (sret) {
            return; /* the callee filled the storage */
        }
        if (agg_x87) {
            /* a long double in %st0, a complex one's imaginary part in %st1 */
            xop_t m = xop_frame(agg_obj, 0, 16);

            xs_emit(s, XI_FSTP, 8, 1, &m, NULL, NULL);
            if ((in->agg_class & CC_ABI_RET_X87_COMPLEX) != 0) {
                m.disp = 16;
                xs_emit(s, XI_FSTP, 8, 1, &m, NULL, NULL);
            }
            return;
        }
        if ((in->agg_class & CC_ABI_RET_VEC128) != 0) {
            xop_t x0 = xop_reg(XR_XMM0, 8);
            xop_t m = xop_frame(agg_obj, 0, 16);

            xs_emit(s, XI_MOV, 8, 2, &x0, &m, NULL);
            return;
        }
        /* its eightbytes from %rax and %rdx, %xmm0 and %xmm1, each by its class and size */
        for (k = 0; k * 8 < in->imm; ++k) {
            long bytes = in->imm - k * 8 > 8 ? 8 : in->imm - k * 8;
            xop_t m = xop_frame(agg_obj, 8 * k, (int)bytes);
            xop_t r;

            if ((in->agg_class & (k == 0 ? CC_ABI_SSE0 : CC_ABI_SSE1)) != 0) {
                r = xop_reg(XR_XMM0 + xi++, 8);
                m.size = (unsigned char)(bytes > 4 ? 8 : 4);
                xs_emit(s, XI_MOV, 8, 2, &r, &m, NULL);
                continue;
            }
            r = xop_reg(gi++ == 0 ? XR_AX : XR_DX, (int)bytes);
            if (!whole_piece(bytes)) {
                r.size = 8;
                xs_store_bytes(s, r, m, (int)bytes);
                continue;
            }
            xs_emit(s, XI_MOV, (int)bytes, 2, &r, &m, NULL);
        }
        return;
    }
    if (fp_dst || (in->dst < 0 && in->call_ret_x87)) {
        if (in->call_ret_x87) {
            xs_fp_pop(s, in->dst); /* (thrown away when nothing takes it) */
        } else if (in->dst >= 0) {
            xop_t x0 = xop_reg(XR_XMM0, 8);
            xop_t d = xs_fp_reg(s, in->dst);

            xs_emit(s, (in->agg_class & CC_ABI_RET_FLOAT) != 0 ? XI_CVTSS2SD : XI_MOV, 8, 2, &x0, &d, NULL);
        }
        return;
    }
    if (in->dst >= 0) {
        /* the callee need not have extended it */
        xop_t rax = xop_reg(XR_AX, 8);
        xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), 8);

        extend_to(s, &rax, &d, in->imm > 0 && f->value_ty[in->dst].kind != CC_TY_AGG ? (int)in->imm : 8,
                  in->is_unsigned);
    }
}

/** A parameter: from the register it came in (moved out at entry), else its stack slot; extended as it is held. */
static void x64_param(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    int pi = in->param_index;
    unsigned char abi = f->param_abi != NULL ? f->param_abi[pi] : CC_CALL_ARG_ABI_DEFAULT;
    abi_loc_t loc = abi64_param_loc(f, pi, gpr_bias(s));
    int bytes = in->imm > 0 ? (int)in->imm : 8;
    xop_t d;

    if (abi == CC_CALL_ARG_ABI_ZERO) {
        return;
    }
    if (xs_fp_kind(s, in->dst) != XFP_NONE) {
        /* a double from its vector register, a float from its single precision; else from the stack */
        if (loc.kind == ABI_LOC_XMM) {
            xop_t r = xop_reg(s->param_lo[pi], 8);

            if (r.reg == XR_NONE || xs_fp_kind(s, in->dst) != XFP_SSE) {
                XS_NOPE(s);
            }
            d = xs_fp_reg(s, in->dst);
            xs_emit(s, abi == CC_CALL_ARG_ABI_FLOAT ? XI_CVTSS2SD : XI_MOV, 8, 2, &r, &d, NULL);
        } else if (loc.kind == ABI_LOC_STACK) {
            int o = xf_new_obj(&s->xf, XF_FIXED, 16, 8, 16 + (long)loc.index);

            xs_fp_from_mem(s, in->dst, xop_frame(o, 0, 8), fp_abi_bytes(abi));
        } else {
            XS_NOPE(s);
        }
        return;
    }
    if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_MEM) {
        /* a structure passed by value: its copy on the stack, whose address is the value */
        if (s->frame_val[in->dst] < 0) {
            int o = xf_new_obj(&s->xf, XF_FIXED, 8, 8, 16 + (long)loc.index);
            xop_t m = xop_frame(o, 0, 8);

            d = xop_reg(xs_vreg_of(s, in->dst, 0), 8);
            xs_emit(s, XI_LEA, 8, 2, &m, &d, NULL);
        }
        return;
    }
    if (agg_in_regs(abi)) {
        /*
         * a structure in registers: its eightbytes into the storage lowering gave it (lhs), whose address is
         * the value -- struct timespec64 is passed so throughout Linux, and every function that took one kept
         * all its values in memory, frames past the 16 KiB of a kernel stack among them
         */
        int o = in->lhs >= 0 ? s->obj[in->lhs] : -1;
        xop_t m;

        if (o < 0) {
            XS_NOPE(s);
        }
        if (loc.kind == ABI_LOC_GPR && (loc.sse & ABI_SSE_WHOLE) != 0) {
            /* a vector, whole in one register */
            xop_t r = xop_reg(s->param_lo[pi], 8);
            xop_t to = xop_frame(o, 0, 16);

            if (r.reg == XR_NONE) {
                XS_NOPE(s);
            }
            xs_emit(s, XI_MOV, 8, 2, &r, &to, NULL);
        } else if (loc.kind == ABI_LOC_GPR) {
            long k;

            for (k = 0; k * 8 < bytes; ++k) {
                long part = bytes - k * 8 > 8 ? 8 : bytes - k * 8;
                xop_t r = xop_reg(k == 0 ? s->param_lo[pi] : s->param_hi[pi], (int)part);
                xop_t to = xop_frame(o, 8 * k, (int)part);

                if (r.reg == XR_NONE) {
                    XS_NOPE(s);
                }
                if ((loc.sse & (1u << k)) != 0) {
                    /* an SSE eightbyte: a float's 4 bytes or a double's 8 */
                    r.size = 8;
                    to.size = (unsigned char)(part > 4 ? 8 : 4);
                    xs_emit(s, XI_MOV, 8, 2, &r, &to, NULL);
                    continue;
                }
                if (!whole_piece(part)) {
                    xs_store_bytes(s, r, to, (int)part);
                    continue;
                }
                xs_emit(s, XI_MOV, (int)part, 2, &r, &to, NULL);
            }
        } else if (loc.kind == ABI_LOC_STACK) {
            int st = xf_new_obj(&s->xf, XF_FIXED, 8, 8, 16 + (long)loc.index);

            copy_bytes(s, xop_frame(st, 0, 8), xop_frame(o, 0, 8), bytes);
        } else {
            XS_NOPE(s);
        }
        m = xop_frame(o, 0, 8);
        d = xop_reg(xs_vreg_of(s, in->dst, 0), 8);
        xs_emit(s, XI_LEA, 8, 2, &m, &d, NULL);
        return;
    }
    if (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT) {
        XS_NOPE(s);
    }
    d = xop_reg(xs_vreg_of(s, in->dst, 0), 8);
    if (f->value_ty[in->dst].kind == CC_TY_AGG) {
        /* a small structure as its bytes, in the word as it came */
        bytes = 8;
    }
    if (loc.kind == ABI_LOC_GPR) {
        xop_t r = xop_reg(s->param_lo[pi], 8);

        if (r.reg == XR_NONE) {
            XS_NOPE(s);
        }
        extend_to(s, &r, &d, bytes, in->is_unsigned);
    } else if (loc.kind == ABI_LOC_STACK) {
        int o = xf_new_obj(&s->xf, XF_FIXED, 8, 8, 16 + (long)loc.index);
        xop_t m = xop_frame(o, 0, 8);

        if (bytes >= 8 && s->ndefs[in->dst] == 1) {
            /* never written again: spilled, it stays where it came */
            xvreg_t *r = xf_vreg(&s->xf, d.reg);

            if (!r->memory) {
                r->home_obj = o;
                r->home_off = 0;
            }
        }
        extend_to(s, &m, &d, bytes, in->is_unsigned);
    } else {
        XS_NOPE(s);
    }
}

/** The most bytes a structure result is copied to the caller's buffer in without a call of memcpy. */
#define X64_INLINE_RET_COPY 64

/**
 * A return: the value in %rax, a structure's second eightbyte in %rdx; a structure returned through memory copied
 * to the caller's buffer, whose address is returned.
 */
static void x64_ret(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    xinst_t x = xi_make(XI_RET, 8, 0, NULL, NULL, NULL);

    if (s->hidden != XR_NONE) {
        xop_t h = xop_reg(s->hidden, 8);
        xop_t rax = xop_reg(XR_AX, 8);
        long bytes = in->imm > 0 ? in->imm : sret_size(s);

        if (in->lhs >= 0 && bytes > 0) {
            if (bytes <= X64_INLINE_RET_COPY) {
                copy_bytes(s, xs_mem_at(s, in->lhs, 0, 8), xop_mem(s->hidden, 0, 8), bytes);
            } else {
                xop_t rdi = xop_reg(XR_DI, 8);
                xop_t rsi = xop_reg(XR_SI, 8);
                xop_t rdx = xop_reg(XR_DX, 8);
                xop_t n = xop_imm(bytes, 8);
                xinst_t c = xi_make(XI_CALL, 8, 0, NULL, NULL, NULL);

                xs_move_word_to(s, in->lhs, 0, &rsi);
                xs_emit(s, XI_MOV, 8, 2, &h, &rdi, NULL);
                xs_emit(s, XI_MOV, 8, 2, &n, &rdx, NULL);
                c.sym = "memcpy";
                c.imp_use = XR_BIT(XR_DI) | XR_BIT(XR_SI) | XR_BIT(XR_DX);
                c.clobber = X64_CALLER_SAVED;
                xf_emit(&s->xf, &c);
            }
        }
        xs_emit(s, XI_MOV, 8, 2, &h, &rax, NULL);
        x.imp_use = XR_BIT(XR_AX);
        xf_emit(&s->xf, &x);
        return;
    }
    if ((in->agg_class & ~(int)(X64_RET_CLASSES | CC_ABI_RET_FLOAT)) != 0 ||
        x86_64_agg_ret_in_memory(in->imm, in->agg_class)) {
        XS_NOPE(s);
    }
    if (in->lhs >= 0 && (in->agg_class & (CC_ABI_RET_X87 | CC_ABI_RET_X87_COMPLEX)) != 0) {
        /* lhs points at it: a long double in %st0, or a complex one's parts in %st0 and %st1 */
        if ((in->agg_class & CC_ABI_RET_X87_COMPLEX) != 0) {
            xop_t m = xs_mem_at(s, in->lhs, 16, 16);

            xs_emit(s, XI_FLD, 8, 1, &m, NULL, NULL);
        }
        {
            xop_t m = xs_mem_at(s, in->lhs, 0, 16);

            xs_emit(s, XI_FLD, 8, 1, &m, NULL, NULL);
        }
        xf_emit(&s->xf, &x);
        return;
    }
    if (in->lhs >= 0 && xs_fp_kind(s, in->lhs) != XFP_NONE) {
        if (f->ret_abi == CC_CALL_ARG_ABI_LDOUBLE || xs_fp_kind(s, in->lhs) == XFP_X87) {
            /* in %st0 */
            xs_fp_push(s, in->lhs);
        } else {
            /* in %xmm0, a float in single precision */
            xop_t r = xs_fp_reg(s, in->lhs);
            xop_t x0 = xop_reg(XR_XMM0, 8);

            xs_emit(s, f->ret_abi == CC_CALL_ARG_ABI_FLOAT ? XI_CVTSD2SS : XI_MOV, 8, 2, &r, &x0, NULL);
            x.imp_use = XR_BIT(XR_XMM0);
        }
        xf_emit(&s->xf, &x);
        return;
    }
    if (in->lhs >= 0 && (in->agg_class & CC_ABI_RET_VEC128) != 0) {
        /* a 16-byte vector, both eightbytes (lhs, rhs) in %xmm0 */
        int o = xf_new_obj(&s->xf, XF_LOCAL, 16, 16, 0);
        xop_t lo = xop_frame(o, 0, 8);
        xop_t hi = xop_frame(o, 8, 8);
        xop_t all = xop_frame(o, 0, 16);
        xop_t x0 = xop_reg(XR_XMM0, 8);

        xs_move_word_to(s, in->lhs, 0, &lo);
        if (in->rhs >= 0) {
            xs_move_word_to(s, in->rhs, 0, &hi);
        }
        xs_emit(s, XI_MOV, 8, 2, &all, &x0, NULL);
        x.imp_use = XR_BIT(XR_XMM0);
        xf_emit(&s->xf, &x);
        return;
    }
    if (in->lhs >= 0) {
        int gi = 0;
        int xi = 0;
        int k;

        if (f->value_ty[in->lhs].kind == CC_TY_AGG && !s->is_addr[in->lhs] && f->value_ty[in->lhs].size > 8) {
            XS_NOPE(s);
        }
        /* its eightbytes (lhs, rhs): to %rax then %rdx, the SSE ones to %xmm0 then %xmm1 */
        for (k = 0; k < 2; ++k) {
            int part = k == 0 ? in->lhs : in->rhs;
            int reg;
            xop_t d;

            if (k == 1 && (in->imm <= 8 || part < 0)) {
                break;
            }
            if ((in->agg_class & (k == 0 ? CC_ABI_SSE0 : CC_ABI_SSE1)) != 0) {
                reg = XR_XMM0 + xi++;
            } else {
                reg = gi++ == 0 ? XR_AX : XR_DX;
            }
            d = xop_reg(reg, 8);
            if (XR_IS_XMM(reg)) {
                xop_t g = xs_reg_word(s, part, 0);

                xs_emit(s, XI_MOV, 8, 2, &g, &d, NULL);
            } else {
                xs_move_word_to(s, part, 0, &d);
            }
            x.imp_use |= XR_BIT(reg);
        }
    }
    xf_emit(&s->xf, &x);
}

/** Whether the function is one this path takes on x86-64: NULL, or why not. */
static const char *x64_function_ok(const xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    size_t i;

    if (f->ret_type == CC_VAL_F64 ? !fp_abi_ok(f->ret_abi) : f->ret_abi != CC_CALL_ARG_ABI_DEFAULT) {
        return "its return";
    }
    for (i = 0; i < f->param_count; ++i) {
        unsigned char abi = f->param_abi != NULL ? f->param_abi[i] : CC_CALL_ARG_ABI_DEFAULT;

        if (f->param_types[i] == CC_VAL_F64 ? !fp_abi_ok(abi)
                                            : (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT &&
                                               abi != CC_CALL_ARG_ABI_ZERO &&
                                               CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_MEM && !agg_in_regs(abi))) {
            return "a parameter's ABI";
        }
    }
    return NULL;
}

/**
 * A variadic function's entry: its register save area -- the six argument registers, then %xmm0..7 when the
 * caller passed any and SSE is on (never under -mno-sse: Linux's _printk() took #UD on movdqu) -- and the record
 * va_start fills, its va_lists pointing at it.
 */
static void save_va_regs(xsel_t *s) {
    size_t k;

    s->va_save = xf_new_obj(&s->xf, XF_LOCAL, 176, 16, 0);
    s->va_state = xf_new_obj(&s->xf, XF_LOCAL, 24, 8, 0);
    if (s->va_save < 0 || s->va_state < 0) {
        return;
    }
    for (k = 0; k < 6; ++k) {
        xop_t r = xop_reg(x64_arg_regs[k], 8);
        xop_t m = xop_frame(s->va_save, 8 * (long long)k, 8);

        xs_emit(s, XI_MOV, 8, 2, &r, &m, NULL);
    }
    if (!cc_options_current()->no_sse) {
        xop_t m = xop_frame(s->va_save, 48, 16);
        xinst_t x = xi_make(XI_SAVEXMM, 8, 1, &m, NULL, NULL);

        x.label = xf_new_label(&s->xf);
        x.imp_use = XR_BIT(XR_AX) | XR_XMM_MASK(8);
        xf_emit(&s->xf, &x);
    }
}

/**
 * va_start: the record's gp_offset and fp_offset past the fixed parameters' registers, its overflow_arg_area at the
 * first variadic argument on the stack, its reg_save_area; the value is the record's address.  In a function that
 * is not variadic, the address of the stack word after parameter imm, as x86_64.c makes it.
 */
static void x64_va_start(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), 8);
    size_t gpr = 0;
    size_t xmm = 0;
    size_t stack = 0;
    int o;

    if (!f->is_variadic || s->va_state < 0) {
        o = xf_new_obj(&s->xf, XF_FIXED, 8, 8, 16 + 8 * (long)in->imm);
        if (o < 0) {
            return;
        }
        {
            xop_t m = xop_frame(o, 0, 8);

            xs_emit(s, XI_LEA, 8, 2, &m, &d, NULL);
        }
        return;
    }
    abi64_fixed_usage(f, (int)in->imm, gpr_bias(s), &gpr, &xmm, &stack);
    gpr = gpr > 6 ? 6 : gpr;
    xmm = xmm > 8 ? 8 : xmm;
    /* the overflow area on an eightbyte boundary, as the psABI keeps it */
    o = xf_new_obj(&s->xf, XF_FIXED, 8, 8, 16 + (long)align_up_size(stack, 8));
    if (o < 0) {
        return;
    }
    {
        xop_t gp = xop_imm((long long)(gpr * 8), 4);
        xop_t fp = xop_imm(48 + (long long)(xmm * 16), 4);
        xop_t m0 = xop_frame(s->va_state, 0, 4);
        xop_t m4 = xop_frame(s->va_state, 4, 4);
        xop_t m8 = xop_frame(s->va_state, 8, 8);
        xop_t m16 = xop_frame(s->va_state, 16, 8);
        xop_t over = xop_frame(o, 0, 8);
        xop_t save = xop_frame(s->va_save, 0, 8);
        xop_t rec = xop_frame(s->va_state, 0, 8);
        xop_t t = xs_temp(s);
        xop_t t2 = xs_temp(s);

        xs_emit(s, XI_MOV, 4, 2, &gp, &m0, NULL);
        xs_emit(s, XI_MOV, 4, 2, &fp, &m4, NULL);
        xs_emit(s, XI_LEA, 8, 2, &over, &t, NULL);
        xs_emit(s, XI_MOV, 8, 2, &t, &m8, NULL);
        xs_emit(s, XI_LEA, 8, 2, &save, &t2, NULL);
        xs_emit(s, XI_MOV, 8, 2, &t2, &m16, NULL);
        xs_emit(s, XI_LEA, 8, 2, &rec, &d, NULL);
    }
}

/** Before the first instruction: a structure parameter's stack copy; each register parameter out of its register. */
static int x64_begin(xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    size_t i;

    if (sret_size(s) > 0) {
        /* a structure result: the caller's buffer's address, from %rdi */
        xop_t rdi = xop_reg(XR_DI, 8);
        xop_t h = xs_temp(s);

        xs_emit(s, XI_MOV, 8, 2, &rdi, &h, NULL);
        s->hidden = h.reg;
    }
    if (f->is_variadic) {
        save_va_regs(s);
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_PARAM && in->dst >= 0 && s->ndefs[in->dst] == 1 && f->param_abi != NULL &&
            in->param_index >= 0 && (size_t)in->param_index < f->param_count &&
            CC_ABI_KIND(f->param_abi[in->param_index]) == CC_CALL_ARG_ABI_AGG_MEM) {
            abi_loc_t loc = abi64_param_loc(f, in->param_index, gpr_bias(s));

            if (loc.kind != ABI_LOC_STACK) {
                return 1;
            }
            s->frame_val[in->dst] = xf_new_obj(&s->xf, XF_FIXED, 8, 8, 16 + (long)loc.index);
        }
    }
    for (i = 0; i < f->param_count; ++i) {
        unsigned char abi = f->param_abi != NULL ? f->param_abi[i] : CC_CALL_ARG_ABI_DEFAULT;
        abi_loc_t loc;

        if (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT && abi != CC_CALL_ARG_ABI_FLOAT && !agg_in_regs(abi)) {
            continue;
        }
        loc = abi64_param_loc(f, (int)i, gpr_bias(s));
        if (loc.kind == ABI_LOC_XMM) {
            xop_t from = xop_reg(xmm_arg(loc.index), 8);
            xop_t to = xop_reg(xf_new_xvreg(&s->xf), 8);

            xs_emit(s, XI_MOV, 8, 2, &from, &to, NULL);
            s->param_lo[i] = to.reg;
        } else if (loc.kind == ABI_LOC_GPR) {
            /* each eightbyte from the next general or vector register its class names */
            size_t gi = loc.index;
            size_t xi = loc.xmm_index;
            int k;

            for (k = 0; k < (agg_in_regs(abi) && loc.size > 8 && (loc.sse & ABI_SSE_WHOLE) == 0 ? 2 : 1); ++k) {
                int sse = agg_in_regs(abi) && (loc.sse & (ABI_SSE_WHOLE | (1u << k))) != 0;
                xop_t from = xop_reg(sse ? xmm_arg(xi++) : x64_arg_regs[gi++], 8);
                xop_t to = sse ? xop_reg(xf_new_xvreg(&s->xf), 8) : xs_temp(s);

                xs_emit(s, XI_MOV, 8, 2, &from, &to, NULL);
                if (k == 0) {
                    s->param_lo[i] = to.reg;
                } else {
                    s->param_hi[i] = to.reg;
                }
            }
        }
    }
    return s->xf.oom ? -1 : 0;
}

/** A call of runtime routine @p sym (x86-64), as x86_64.c calls one: reading @p use, writing @p def. */
static void x64_libcall(xsel_t *s, const char *sym, unsigned use, unsigned def) {
    xinst_t c = xi_make(XI_CALL, 8, 0, NULL, NULL, NULL);

    c.sym = sym;
    c.imp_use = use;
    c.imp_def = def;
    c.clobber = X64_CALLER_SAVED;
    xf_emit(&s->xf, &c);
}

/** Whether symbol @p sym's address is read from the GOT, as x86_64.c decides it (x86_64_symbol_uses_got()). */
static int x64_sym_got(const xsel_t *s, const char *sym) {
    return x86_64_symbol_uses_got(s->m, sym, x64_pic);
}

int x86_64_mir_emit_function(FILE *out, const cc_ssa_module_t *m, const cc_ssa_function_t *f, size_t fn_index,
                             const char *src_path, int emit_debug, int pic, cc_diag_t *diag) {
    static const xtarget_t alloc = {
        X64_CALLER_SAVED | XR_BIT(XR_BX) | XR_BIT(XR_R12) | XR_BIT(XR_R13) | XR_BIT(XR_R14) | XR_BIT(XR_R15),
        XR_XMM_MASK(16),
        XR_BIT(XR_BX) | XR_BIT(XR_R12) | XR_BIT(XR_R13) | XR_BIT(XR_R14) | XR_BIT(XR_R15),
        X64_CALLER_SAVED | XR_BIT(XR_BX) | XR_BIT(XR_R12) | XR_BIT(XR_R13) | XR_BIT(XR_R14) | XR_BIT(XR_R15),
        x64_order,
        (int)(sizeof(x64_order) / sizeof(x64_order[0])),
        8};
    xsel_target_t t;

    memset(&t, 0, sizeof(t));
    t.word = 8;
    t.alloc = &alloc;
    t.caller_saved = X64_CALLER_SAVED;
    t.saved = x64_saved;
    t.nsaved = (int)(sizeof(x64_saved) / sizeof(x64_saved[0]));
    t.fp_residue = 0; /* %rbp is on a 16-byte boundary */
    t.stack_boundary = 16;
    t.sym_imm = 0;
    t.extends_params = 1;
    t.sse_fp = 1;
    t.chain_reg = XR_R10;
    t.function_ok = x64_function_ok;
    t.begin = x64_begin;
    t.param = x64_param;
    t.call = x64_call;
    t.ret = x64_ret;
    t.va_start = x64_va_start;
    t.sym_got = x64_sym_got;
    t.libcall = x64_libcall;
    x64_pic = pic;
    return x86_mir_emit_function(out, &t, m, f, fn_index, src_path, emit_debug, diag);
}
