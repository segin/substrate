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
 * Not taken yet: floating point, and structures passed or returned in
 * registers other than general ones.
 */

#include "cc_x86_mir.h"

#include "cc_options.h"
#include "cc_x86.h"

#include <stdlib.h>
#include <string.h>

/** The allocation order: the caller-saved registers first, which cost no save. */
static const int x64_order[] = {XR_AX,  XR_CX,  XR_DX,  XR_SI,  XR_DI,  XR_R8,  XR_R9,
                                XR_R10, XR_R11, XR_BX,  XR_R12, XR_R13, XR_R14, XR_R15};

/** The callee-saved registers, in the order the prologue pushes them. */
static const int x64_saved[] = {XR_BX, XR_R12, XR_R13, XR_R14, XR_R15};

/** The registers a call clobbers. */
#define X64_CALLER_SAVED                                                                                          \
    (XR_BIT(XR_AX) | XR_BIT(XR_CX) | XR_BIT(XR_DX) | XR_BIT(XR_SI) | XR_BIT(XR_DI) | XR_BIT(XR_R8) |              \
     XR_BIT(XR_R9) | XR_BIT(XR_R10) | XR_BIT(XR_R11))

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

/** Whether ABI byte @p abi passes a structure in general registers only: AGG_REG with no SSE eightbyte. */
static int agg_in_gprs(unsigned char abi) {
    return CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_AGG_REG && (abi & (CC_ABI_SSE0 | CC_ABI_SSE1 | CC_ABI_VEC128)) == 0;
}

/** Whether an eightbyte of @p bytes bytes is one access (1, 2, 4 or 8 bytes). */
static int whole_piece(long bytes) {
    return bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8;
}

/**
 * Load the eightbytes of the structure at IR address @p v, @p size bytes, into the general registers from
 * argument register @p first on: each zero-extended past its bytes, as the old emitter loads them.
 */
static void load_agg_gprs(xsel_t *s, int v, long size, size_t first, unsigned *used) {
    long k;

    for (k = 0; k * 8 < size; ++k) {
        long bytes = size - k * 8 > 8 ? 8 : size - k * 8;
        int reg = x64_arg_regs[first + (size_t)k];
        xop_t m = xs_mem_at(s, v, 8 * k, (int)bytes);
        xop_t d = xop_reg(reg, bytes == 8 ? 8 : 4);

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

/** A call: stack arguments stored below the call's %rsp, register ones moved in, the result out of %rax. */
static void x64_call(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    int agg = in->dst >= 0 && in->rhs == in->dst && in->imm > 0;
    /* an empty structure result: nothing comes back, and the value is its storage's address (rhs) */
    int empty = in->dst >= 0 && in->rhs >= 0 && in->imm == 0 && in->agg_class != 0;
    int agg_obj = agg ? s->obj[in->dst] : -1;
    int sret = agg && x86_64_agg_ret_in_memory(in->imm, in->agg_class);
    abi_loc_t *locs = NULL;
    size_t stack_bytes = 0;
    size_t xmm = 0;
    long total;
    unsigned used = 0;
    size_t a;
    xinst_t c;
    xop_t target;
    int indirect = 0;

    if (in->call_ret_x87 || (agg && agg_obj < 0) ||
        (agg && !sret && (in->agg_class & ~(int)(CC_ABI_RET_MEMORY)) != 0) || (!agg && !empty && in->agg_class != 0) ||
        (in->dst >= 0 && !agg && (cc_ssa_value_class(f, in->dst) == CC_VAL_F64 ||
                                  (f->value_ty[in->dst].kind == CC_TY_AGG && f->value_ty[in->dst].size > 8)))) {
        XS_NOPE(s);
    }
    if (agg && !sret && in->imm > 16) {
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

        if (locs[a].kind == ABI_LOC_NONE) {
            continue;
        }
        if (v < 0 || cc_ssa_value_class(f, v) == CC_VAL_F64 || locs[a].kind == ABI_LOC_XMM ||
            (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT && CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_MEM &&
             !agg_in_gprs(abi)) ||
            (agg_in_gprs(abi) && locs[a].kind == ABI_LOC_GPR && locs[a].index + (locs[a].size + 7) / 8 > 6) ||
            (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_DEFAULT && f->value_ty[v].kind == CC_TY_AGG && !s->is_addr[v] &&
             f->value_ty[v].size > 8)) {
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
        xop_t at;

        if (locs[a].kind != ABI_LOC_STACK) {
            continue;
        }
        at = xop_mem(XR_SP, (long long)locs[a].index, 8);
        if (in->call_arg_abi != NULL && (CC_ABI_KIND(in->call_arg_abi[a]) == CC_CALL_ARG_ABI_AGG_MEM ||
                                         agg_in_gprs(in->call_arg_abi[a]))) {
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
        xop_t d;

        if (locs[a].kind != ABI_LOC_GPR) {
            continue;
        }
        if (in->call_arg_abi != NULL && agg_in_gprs(in->call_arg_abi[a])) {
            /* a structure in registers: its eightbytes, from where the value points */
            load_agg_gprs(s, in->args[a], (long)locs[a].size, locs[a].index, &used);
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
    if (in->dst >= 0 && !empty) {
        c.imp_def = XR_BIT(XR_AX) | (agg && !sret && in->imm > 8 ? XR_BIT(XR_DX) : 0);
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

        if (sret) {
            return; /* the callee filled the storage */
        }
        /* its eightbytes from %rax and %rdx, each by its size */
        for (k = 0; k * 8 < in->imm; ++k) {
            long bytes = in->imm - k * 8 > 8 ? 8 : in->imm - k * 8;
            xop_t r = xop_reg(k == 0 ? XR_AX : XR_DX, (int)bytes);
            xop_t m = xop_frame(agg_obj, 8 * k, (int)bytes);

            if (!whole_piece(bytes)) {
                xs_store_bytes(s, xop_reg(k == 0 ? XR_AX : XR_DX, 8), m, (int)bytes);
                continue;
            }
            xs_emit(s, XI_MOV, (int)bytes, 2, &r, &m, NULL);
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
    if (agg_in_gprs(abi)) {
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
        if (loc.kind == ABI_LOC_GPR) {
            long k;

            for (k = 0; k * 8 < bytes; ++k) {
                long part = bytes - k * 8 > 8 ? 8 : bytes - k * 8;
                xop_t r = xop_reg(k == 0 ? s->param_lo[pi] : s->param_hi[pi], (int)part);
                xop_t to = xop_frame(o, 8 * k, (int)part);

                if (r.reg == XR_NONE) {
                    XS_NOPE(s);
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
    if ((in->agg_class & ~(int)CC_ABI_RET_MEMORY) != 0 || x86_64_agg_ret_in_memory(in->imm, in->agg_class)) {
        XS_NOPE(s);
    }
    if (in->lhs >= 0) {
        xop_t rax = xop_reg(XR_AX, 8);

        if (cc_ssa_value_class(f, in->lhs) == CC_VAL_F64 ||
            (f->value_ty[in->lhs].kind == CC_TY_AGG && !s->is_addr[in->lhs] && f->value_ty[in->lhs].size > 8)) {
            XS_NOPE(s);
        }
        xs_move_word_to(s, in->lhs, 0, &rax);
        x.imp_use = XR_BIT(XR_AX);
        if (in->imm > 8 && in->rhs >= 0) {
            xop_t rdx = xop_reg(XR_DX, 8);

            xs_move_word_to(s, in->rhs, 0, &rdx);
            x.imp_use |= XR_BIT(XR_DX);
        }
    }
    xf_emit(&s->xf, &x);
}

/** Whether the function is one this path takes on x86-64: NULL, or why not. */
static const char *x64_function_ok(const xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    size_t i;

    if (f->ret_type == CC_VAL_F64 || f->ret_abi != CC_CALL_ARG_ABI_DEFAULT) {
        return "its return";
    }
    for (i = 0; i < f->param_count; ++i) {
        unsigned char abi = f->param_abi != NULL ? f->param_abi[i] : CC_CALL_ARG_ABI_DEFAULT;

        if (f->param_types[i] == CC_VAL_F64 ||
            (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT && abi != CC_CALL_ARG_ABI_ZERO &&
             CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_MEM && !agg_in_gprs(abi))) {
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
        x.imp_use = XR_BIT(XR_AX);
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

        if (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT && !agg_in_gprs(abi)) {
            continue;
        }
        loc = abi64_param_loc(f, (int)i, gpr_bias(s));
        if (loc.kind == ABI_LOC_GPR) {
            xop_t from = xop_reg(x64_arg_regs[loc.index], 8);
            xop_t to = xs_temp(s);

            xs_emit(s, XI_MOV, 8, 2, &from, &to, NULL);
            s->param_lo[i] = to.reg;
            if (agg_in_gprs(abi) && loc.size > 8) {
                /* a structure's second eightbyte, in the next register */
                from = xop_reg(x64_arg_regs[loc.index + 1], 8);
                to = xs_temp(s);
                xs_emit(s, XI_MOV, 8, 2, &from, &to, NULL);
                s->param_hi[i] = to.reg;
            }
        }
    }
    return s->xf.oom ? -1 : 0;
}

/** Whether symbol @p sym's address is read from the GOT, as x86_64.c decides it (x86_64_symbol_uses_got()). */
static int x64_sym_got(const xsel_t *s, const char *sym) {
    return x86_64_symbol_uses_got(s->m, sym, x64_pic);
}

int x86_64_mir_emit_function(FILE *out, const cc_ssa_module_t *m, const cc_ssa_function_t *f, size_t fn_index,
                             const char *src_path, int emit_debug, int pic, cc_diag_t *diag) {
    static const xtarget_t alloc = {
        X64_CALLER_SAVED | XR_BIT(XR_BX) | XR_BIT(XR_R12) | XR_BIT(XR_R13) | XR_BIT(XR_R14) | XR_BIT(XR_R15),
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
    t.chain_reg = XR_R10;
    t.function_ok = x64_function_ok;
    t.begin = x64_begin;
    t.param = x64_param;
    t.call = x64_call;
    t.ret = x64_ret;
    t.va_start = x64_va_start;
    t.sym_got = x64_sym_got;
    x64_pic = pic;
    return x86_mir_emit_function(out, &t, m, f, fn_index, src_path, emit_debug, diag);
}
