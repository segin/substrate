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
 * Not taken yet: floating point, variadic functions (their register save
 * area), structures passed or returned in registers other than general
 * ones, and functions returning a structure through a hidden pointer.
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

/** A call: stack arguments stored below the call's %rsp, register ones moved in, the result out of %rax. */
static void x64_call(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    int agg = in->dst >= 0 && in->rhs == in->dst && in->imm > 0;
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

    if (in->call_chain > 0 || in->call_ret_x87 || (agg && agg_obj < 0) ||
        (agg && !sret && (in->agg_class & ~(int)(CC_ABI_RET_MEMORY)) != 0) || (!agg && in->agg_class != 0) ||
        (in->dst >= 0 && !agg && (cc_ssa_value_class(f, in->dst) == CC_VAL_F64 ||
                                  f->value_ty[in->dst].kind == CC_TY_AGG))) {
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
            (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT && CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_MEM) ||
            (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_DEFAULT && f->value_ty[v].kind == CC_TY_AGG && !s->is_addr[v])) {
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
        if (in->call_arg_abi != NULL && CC_ABI_KIND(in->call_arg_abi[a]) == CC_CALL_ARG_ABI_AGG_MEM) {
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
    if (in->dst >= 0) {
        c.imp_def = XR_BIT(XR_AX) | (agg && !sret && in->imm > 8 ? XR_BIT(XR_DX) : 0);
    }
    xf_emit(&s->xf, &c);
    if (total > 0) {
        xop_t k = xop_imm(total, 8);

        xs_emit(s, XI_ADDSP, 8, 1, &k, NULL, NULL);
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

            if (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) {
                XS_NOPE(s);
            }
            xs_emit(s, XI_MOV, (int)bytes, 2, &r, &m, NULL);
        }
        return;
    }
    if (in->dst >= 0) {
        /* the callee need not have extended it */
        xop_t rax = xop_reg(XR_AX, 8);
        xop_t d = xop_reg(xs_vreg_of(s, in->dst, 0), 8);

        extend_to(s, &rax, &d, in->imm > 0 ? (int)in->imm : 8, in->is_unsigned);
    }
}

/** A parameter: from the register it came in (moved out at entry), else its stack slot; extended as it is held. */
static void x64_param(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    int pi = in->param_index;
    unsigned char abi = f->param_abi != NULL ? f->param_abi[pi] : CC_CALL_ARG_ABI_DEFAULT;
    abi_loc_t loc = abi64_param_loc(f, pi, 0);
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
    if (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT) {
        XS_NOPE(s);
    }
    d = xop_reg(xs_vreg_of(s, in->dst, 0), 8);
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

/** A return: the value in %rax, a structure's second eightbyte in %rdx. */
static void x64_ret(xsel_t *s, const cc_ssa_instr_t *in) {
    const cc_ssa_function_t *f = s->f;
    xinst_t x = xi_make(XI_RET, 8, 0, NULL, NULL, NULL);

    if ((in->agg_class & ~(int)CC_ABI_RET_MEMORY) != 0 || x86_64_agg_ret_in_memory(in->imm, in->agg_class)) {
        XS_NOPE(s);
    }
    if (in->lhs >= 0) {
        xop_t rax = xop_reg(XR_AX, 8);

        if (cc_ssa_value_class(f, in->lhs) == CC_VAL_F64 ||
            (f->value_ty[in->lhs].kind == CC_TY_AGG && !s->is_addr[in->lhs])) {
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
    if (x86_64_function_sret_size(f) > 0 || x86_64_infer_sret_size_from_calls(s->m, f) > 0) {
        return "a structure result";
    }
    if (f->is_variadic) {
        return "variadic";
    }
    for (i = 0; i < f->param_count; ++i) {
        unsigned char abi = f->param_abi != NULL ? f->param_abi[i] : CC_CALL_ARG_ABI_DEFAULT;

        if (f->param_types[i] == CC_VAL_F64 ||
            (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT && abi != CC_CALL_ARG_ABI_ZERO &&
             CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_AGG_MEM)) {
            return "a parameter's ABI";
        }
    }
    return NULL;
}

/** Before the first instruction: a structure parameter's stack copy; each register parameter out of its register. */
static int x64_begin(xsel_t *s) {
    const cc_ssa_function_t *f = s->f;
    size_t i;

    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];

        if (in->op == CC_SSA_PARAM && in->dst >= 0 && s->ndefs[in->dst] == 1 && f->param_abi != NULL &&
            in->param_index >= 0 && (size_t)in->param_index < f->param_count &&
            CC_ABI_KIND(f->param_abi[in->param_index]) == CC_CALL_ARG_ABI_AGG_MEM) {
            abi_loc_t loc = abi64_param_loc(f, in->param_index, 0);

            if (loc.kind != ABI_LOC_STACK) {
                return 1;
            }
            s->frame_val[in->dst] = xf_new_obj(&s->xf, XF_FIXED, 8, 8, 16 + (long)loc.index);
        }
    }
    for (i = 0; i < f->param_count; ++i) {
        unsigned char abi = f->param_abi != NULL ? f->param_abi[i] : CC_CALL_ARG_ABI_DEFAULT;
        abi_loc_t loc;

        if (CC_ABI_KIND(abi) != CC_CALL_ARG_ABI_DEFAULT) {
            continue;
        }
        loc = abi64_param_loc(f, (int)i, 0);
        if (loc.kind == ABI_LOC_GPR) {
            xop_t from = xop_reg(x64_arg_regs[loc.index], 8);
            xop_t to = xs_temp(s);

            xs_emit(s, XI_MOV, 8, 2, &from, &to, NULL);
            s->param_lo[i] = to.reg;
        }
    }
    return s->xf.oom ? -1 : 0;
}

/**
 * Whether symbol @p sym's address is read from the GOT: under -fPIC, or a global declared extern but under
 * -mcmodel=kernel, as x86_64.c decides it; never a label.
 */
static int x64_sym_got(const xsel_t *s, const char *sym) {
    return sym[0] != '.' &&
           (x64_pic || (module_symbol_is_extern_global(s->m, sym) && !cc_options_current()->cmodel_kernel));
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
    t.function_ok = x64_function_ok;
    t.begin = x64_begin;
    t.param = x64_param;
    t.call = x64_call;
    t.ret = x64_ret;
    t.va_start = NULL;
    t.sym_got = x64_sym_got;
    x64_pic = pic;
    return x86_mir_emit_function(out, &t, m, f, fn_index, src_path, emit_debug, diag);
}
