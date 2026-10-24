/*
 * inline.c: inlining small static inline functions into their callers.
 *
 * gcc inlines a `static inline` function at -O1 and up, and then folds
 * what the inlined body makes constant.  Code relies on it: Linux guards
 * a call with a helper that is constant in one configuration --
 * `if (efi_enabled(EFI_BOOT)) efi_init();`, efi_enabled() returning false
 * without CONFIG_EFI -- and leaves the function called undefined, so a
 * compiler that keeps the call cannot link it.
 *
 * gcc also inlines, from -O1, a static function called from one place
 * only (-finline-functions-called-once), and code relies on that too:
 * Linux's head32.c calls zap_early_initrd_mapping(), which reads __initdata,
 * from its __init code alone, and modpost rejects a copy of it left in
 * .text.  opt.c marks such functions, leaving out those on a cycle of
 * calls; they are inlined at a larger size, unless that grows the caller's
 * frame past gcc's limit (frame_growth_ok()), and the copy, called by
 * nothing then, is dropped with the other unused static functions.
 *
 * A call of a function of the module that is static, inline (by its
 * storage or always_inline) and not noinline, small, and whose interface
 * is plain values (no aggregate passed or returned in memory, no
 * varargs) is replaced by a copy of the function's body: each parameter
 * is a move of its argument, each return a move into the call's result
 * and a branch to the end.  Its values and labels are renumbered into
 * the caller's.  A body that uses the frame (alloca, the static chain,
 * __builtin_frame_address) or label addresses stays a call.  opt.c inlines
 * a module bottom-up, the callees first, so a callee has its own calls
 * inlined and its body folded before it is copied.
 */

#include "cc_frontend.h"
#include "cc_middle.h"
#include "cc_ssa.h"
#include "cc_target.h"
#include "cc_util.h"

#include <stdlib.h>
#include <string.h>

/** The most instructions an inline function's folded body may have to be inlined. */
#define INLINE_MAX_INSTRS 160
/** The same for an always_inline function, which gcc inlines whatever its size. */
#define ALWAYS_INLINE_MAX_INSTRS 4000
/** The same for a static function called from one place, which gcc inlines unless very large. */
#define CALLED_ONCE_MAX_INSTRS 4000
/** gcc's --param large-stack-frame: a frame no larger may grow by inlining a function called once. */
#define LARGE_STACK_FRAME 256
/** gcc's --param large-stack-frame-growth: a larger one by at most this percentage of what it was. */
#define LARGE_STACK_FRAME_GROWTH 1000
/**
 * And never past this, which gcc has no need of: it shares one slot among
 * locals not live at once, cc gives each its own, so what is inlined adds
 * all its locals to the caller's frame.  Linux i386's FRAME_WARN.
 */
#define CALLED_ONCE_MAX_FRAME 1024
/**
 * What each value of an inlined body is reckoned to add to the frame: the
 * i386 back end gives one live across blocks a slot of its own, a word.
 */
#define VALUE_SLOT_BYTES 4
/** No more is inlined into a function grown this large. */
#define CALLER_MAX_INSTRS 60000

/** The new instruction list of the caller being rewritten. */
typedef struct {
    cc_ssa_instr_t *at;
    size_t count;
    size_t cap;
} ivec_t;

/**
 * Append @p in, whose pointers the list now owns.
 *
 * @return 0, or -1 when out of memory
 */
static int ivec_put(ivec_t *v, const cc_ssa_instr_t *in) {
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 64 : v->cap * 2;
        cc_ssa_instr_t *next = (cc_ssa_instr_t *)realloc(v->at, ncap * sizeof(*next));

        if (next == NULL) {
            return -1;
        }
        v->at = next;
        v->cap = ncap;
    }
    v->at[v->count++] = *in;
    return 0;
}

/** An instruction of operation @p op and no operands. */
static cc_ssa_instr_t plain_instr(cc_ssa_opcode_t op) {
    cc_ssa_instr_t in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.dst = -1;
    in.lhs = -1;
    in.rhs = -1;
    in.label = -1;
    in.true_label = -1;
    in.false_label = -1;
    in.param_index = -1;
    return in;
}

/** Whether ABI byte @p abi passes a plain value: the default, in any register under regparm. */
static int abi_is_plain(unsigned char abi) {
    return (abi & ~CC_ABI_REGPARM_MASK) == CC_CALL_ARG_ABI_DEFAULT;
}

/**
 * Whether the body of @p g may be copied into a caller: no more than
 * @p max_instrs instructions, its interface plain values, and nothing in
 * it that depends on its own frame.
 *
 * @param g           the function
 * @param max_instrs  the most instructions it may have
 * @return            1 if it may, else 0
 */
static int inline_body_ok(const cc_ssa_function_t *g, size_t max_instrs) {
    size_t i;

    if (g->is_variadic || g->attr_alias != NULL || g->sret_reg || !abi_is_plain(g->ret_abi) || g->instrs == NULL ||
        g->instr_count > max_instrs) {
        return 0;
    }
    for (i = 0; i < g->param_count; ++i) {
        if (g->param_abi != NULL && !abi_is_plain(g->param_abi[i])) {
            return 0;
        }
    }
    for (i = 0; i < g->instr_count; ++i) {
        const cc_ssa_instr_t *in = &g->instrs[i];

        switch (in->op) {
        case CC_SSA_DYNALLOC:
        case CC_SSA_CHAIN:
        case CC_SSA_FRAMEADDR:
        case CC_SSA_VA_START:
        case CC_SSA_LADDR:
            return 0;
        case CC_SSA_RET:
            if (in->agg_class != 0) {
                return 0;
            }
            break;
        default:
            break;
        }
    }
    return 1;
}

int cc_inline_candidate(const cc_ssa_function_t *g) {
    int always = (g->attr_flags & CC_ATTR_ALWAYS_INLINE) != 0;

    if ((g->storage & CC_STORAGE_STATIC) == 0 || (g->attr_flags & CC_ATTR_NOINLINE) != 0 ||
        ((g->storage & CC_STORAGE_INLINE) == 0 && !always)) {
        return 0;
    }
    return inline_body_ok(g, (size_t)(always ? ALWAYS_INLINE_MAX_INSTRS : INLINE_MAX_INSTRS));
}

/** Whether values @p a of @p fa and @p b of @p fb have the same class and size, so a move copies one to the other. */
static int same_shape(const cc_ssa_function_t *fa, int a, const cc_ssa_function_t *fb, int b) {
    return cc_ssa_value_class(fa, a) == cc_ssa_value_class(fb, b) && cc_ssa_value_size(fa, a) == cc_ssa_value_size(fb, b);
}

/**
 * Whether integer value @p b of @p fb, returned, converts to call result @p a of @p fa as the call
 * would read it: a `_Bool` function returns a compare's word and its call's result is a byte; a
 * `signed char` one returns a byte its call reads as a word and extends.  A narrower result is
 * narrowed (put_narrow()); a wider one, within a word, is the same word, a narrow value being held
 * extended.
 */
static int converts_to(const cc_ssa_function_t *fa, int a, const cc_ssa_function_t *fb, int b) {
    uint32_t to = cc_ssa_value_size(fa, a);
    uint32_t from = cc_ssa_value_size(fb, b);

    if (cc_ssa_value_class(fa, a) != CC_VAL_I64 || cc_ssa_value_class(fb, b) != CC_VAL_I64) {
        return 0;
    }
    if (to > from) {
        return to <= (uint32_t)cc_target_pointer_size();
    }
    return (to == 1 || to == 2 || to == 4) && from <= 8;
}

/**
 * Whether @p call, in @p f, can be replaced by @p g's body: its arguments are plain values of the
 * parameters' shapes, and its result the shape of what @p g returns.
 */
static int call_site_inlinable(const cc_ssa_function_t *f, const cc_ssa_instr_t *call, const cc_ssa_function_t *g) {
    size_t i;

    /* a structure result the call writes to memory (rhs, imm bytes) as well: the copied body would not */
    if (call->op != CC_SSA_CALL || call->call_chain != 0 || call->call_is_variadic || call->call_ret_x87 ||
        call->agg_class != 0 || call->arg_count != g->param_count || (call->rhs >= 0 && call->imm > 0)) {
        return 0;
    }
    for (i = 0; i < g->instr_count; ++i) {
        const cc_ssa_instr_t *in = &g->instrs[i];

        if (in->op == CC_SSA_PARAM) {
            /* an integer argument of another width is converted as the parameter takes it (a bool's compare) */
            if (in->param_index < 0 || (size_t)in->param_index >= call->arg_count || in->dst < 0 ||
                call->args[in->param_index] < 0 ||
                (!same_shape(f, call->args[in->param_index], g, in->dst) &&
                 !converts_to(g, in->dst, f, call->args[in->param_index]))) {
                return 0;
            }
        } else if (in->op == CC_SSA_RET && in->lhs >= 0 && call->dst >= 0 && !same_shape(f, call->dst, g, in->lhs) &&
                   !converts_to(f, call->dst, g, in->lhs)) {
            return 0;
        }
    }
    for (i = 0; i < call->arg_count; ++i) {
        if (call->call_arg_abi != NULL && !abi_is_plain(call->call_arg_abi[i])) {
            return 0;
        }
    }
    return 1;
}

/** A copy of @p n bytes at @p src, or NULL (also for NULL or none). */
static void *dup_bytes(const void *src, size_t n) {
    void *p;

    if (src == NULL || n == 0) {
        return NULL;
    }
    p = malloc(n);
    if (p != NULL) {
        memcpy(p, src, n);
    }
    return p;
}

/**
 * A copy of the @p n strings at @p src.
 *
 * @return 0, or -1 when out of memory (and *out is NULL)
 */
static int dup_strings(char **src, size_t n, char ***out) {
    size_t i;

    *out = NULL;
    if (src == NULL || n == 0) {
        return 0;
    }
    *out = (char **)calloc(n, sizeof(**out));
    if (*out == NULL) {
        return -1;
    }
    for (i = 0; i < n; ++i) {
        if (src[i] != NULL && ((*out)[i] = cc_strdup(src[i])) == NULL) {
            for (; i > 0; --i) {
                free((*out)[i - 1]);
            }
            free(*out);
            *out = NULL;
            return -1;
        }
    }
    return 0;
}

/** Value @p v of the inlined body renumbered by @p vmap; -1 stays -1. */
static int map_value(const int *vmap, int v) {
    return v >= 0 ? vmap[v] : v;
}

/** Asm operand @p v renumbered, a memory operand by its address kept encoded so. */
static int map_asm_value(const int *vmap, int v) {
    if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
        return CC_SSA_ASM_MEM_INDIRECT_ENCODE(vmap[CC_SSA_ASM_MEM_INDIRECT_DECODE(v)]);
    }
    return map_value(vmap, v);
}

/**
 * A deep copy of @p src with its values renumbered by @p vmap and its labels moved up by @p label_base.
 *
 * @return 0, or -1 when out of memory (and nothing is left allocated)
 */
static int clone_instr(const cc_ssa_instr_t *src, const int *vmap, int label_base, cc_ssa_instr_t *out) {
    size_t i;
    int fail = 0;

    *out = *src;
    out->sym = NULL;
    out->args = NULL;
    out->call_arg_abi = NULL;
    out->call_arg_sizes = NULL;
    out->asm_out_values = NULL;
    out->asm_out_sizes = NULL;
    out->asm_out_constraints = NULL;
    out->asm_out_names = NULL;
    out->asm_in_values = NULL;
    out->asm_in_sizes = NULL;
    out->asm_in_constraints = NULL;
    out->asm_in_names = NULL;
    out->asm_clobbers = NULL;
    out->asm_goto_labels = NULL;
    out->asm_goto_names = NULL;

    out->dst = map_value(vmap, src->dst);
    out->lhs = map_value(vmap, src->lhs);
    out->rhs = map_value(vmap, src->rhs);
    out->call_chain = src->call_chain > 0 ? vmap[src->call_chain - 1] + 1 : src->call_chain;
    out->label = src->label >= 0 ? src->label + label_base : src->label;
    out->true_label = src->true_label >= 0 ? src->true_label + label_base : src->true_label;
    out->false_label = src->false_label >= 0 ? src->false_label + label_base : src->false_label;

    if (src->sym != NULL && (out->sym = cc_strdup(src->sym)) == NULL) {
        fail = 1;
    }
    if (!fail && src->arg_count > 0) {
        out->args = (int *)dup_bytes(src->args, src->arg_count * sizeof(*src->args));
        out->call_arg_abi = (unsigned char *)dup_bytes(src->call_arg_abi, src->arg_count * sizeof(*src->call_arg_abi));
        out->call_arg_sizes = (uint32_t *)dup_bytes(src->call_arg_sizes, src->arg_count * sizeof(*src->call_arg_sizes));
        if ((src->args != NULL && out->args == NULL) || (src->call_arg_abi != NULL && out->call_arg_abi == NULL) ||
            (src->call_arg_sizes != NULL && out->call_arg_sizes == NULL)) {
            fail = 1;
        } else if (out->args != NULL) {
            for (i = 0; i < src->arg_count; ++i) {
                out->args[i] = map_value(vmap, src->args[i]);
            }
        }
    }
    if (!fail && src->asm_out_count > 0) {
        out->asm_out_values = (int *)dup_bytes(src->asm_out_values, src->asm_out_count * sizeof(int));
        out->asm_out_sizes = (unsigned char *)dup_bytes(src->asm_out_sizes, src->asm_out_count);
        if ((src->asm_out_values != NULL && out->asm_out_values == NULL) ||
            (src->asm_out_sizes != NULL && out->asm_out_sizes == NULL) ||
            dup_strings(src->asm_out_constraints, src->asm_out_count, &out->asm_out_constraints) != 0 ||
            dup_strings(src->asm_out_names, src->asm_out_count, &out->asm_out_names) != 0) {
            fail = 1;
        } else if (out->asm_out_values != NULL) {
            for (i = 0; i < src->asm_out_count; ++i) {
                out->asm_out_values[i] = map_asm_value(vmap, src->asm_out_values[i]);
            }
        }
    }
    if (!fail && src->asm_in_count > 0) {
        out->asm_in_values = (int *)dup_bytes(src->asm_in_values, src->asm_in_count * sizeof(int));
        out->asm_in_sizes = (unsigned char *)dup_bytes(src->asm_in_sizes, src->asm_in_count);
        if ((src->asm_in_values != NULL && out->asm_in_values == NULL) ||
            (src->asm_in_sizes != NULL && out->asm_in_sizes == NULL) ||
            dup_strings(src->asm_in_constraints, src->asm_in_count, &out->asm_in_constraints) != 0 ||
            dup_strings(src->asm_in_names, src->asm_in_count, &out->asm_in_names) != 0) {
            fail = 1;
        } else if (out->asm_in_values != NULL) {
            for (i = 0; i < src->asm_in_count; ++i) {
                out->asm_in_values[i] = map_asm_value(vmap, src->asm_in_values[i]);
            }
        }
    }
    if (!fail && dup_strings(src->asm_clobbers, src->asm_clobber_count, &out->asm_clobbers) != 0) {
        fail = 1;
    }
    if (!fail && src->asm_goto_count > 0) {
        out->asm_goto_labels = (int *)dup_bytes(src->asm_goto_labels, src->asm_goto_count * sizeof(int));
        if ((src->asm_goto_labels != NULL && out->asm_goto_labels == NULL) ||
            dup_strings(src->asm_goto_names, src->asm_goto_count, &out->asm_goto_names) != 0) {
            fail = 1;
        } else if (out->asm_goto_labels != NULL) {
            for (i = 0; i < src->asm_goto_count; ++i) {
                out->asm_goto_labels[i] += label_base;
            }
        }
    }
    if (fail) {
        cc_ssa_instr_free(out);
        return -1;
    }
    return 0;
}

/**
 * A new integer value of @p f, @p size bytes, held as @p is_unsigned says.
 *
 * @return the value, or -1 when out of memory
 */
static int new_int_value(cc_ssa_function_t *f, uint32_t size, int is_unsigned) {
    int v = cc_ssa_function_new_value(f, CC_VAL_I64);

    if (v >= 0) {
        cc_ssa_value_set_size(f, v, size);
        cc_ssa_value_set_unsigned(f, v, is_unsigned);
    }
    return v;
}

/**
 * Append `dst = lhs op imm` (op one of SHL, SHR, AND, or LO32 with no immediate) to @p out.
 *
 * @return 0, or -1 when out of memory
 */
static int put_op_imm(cc_ssa_function_t *f, ivec_t *out, cc_ssa_opcode_t op, int dst, int lhs, long long imm,
                      int is_unsigned) {
    cc_ssa_instr_t in = plain_instr(op);

    in.dst = dst;
    in.lhs = lhs;
    in.is_unsigned = is_unsigned;
    if (op == CC_SSA_AND) {
        /* a mask is a constant operand */
        cc_ssa_instr_t k = plain_instr(CC_SSA_CONST);

        k.dst = new_int_value(f, cc_ssa_value_size(f, lhs), 1);
        k.imm = imm;
        k.is_unsigned = 1;
        if (k.dst < 0 || ivec_put(out, &k) != 0) {
            return -1;
        }
        in.rhs = k.dst;
    } else {
        in.imm = imm;
    }
    return ivec_put(out, &in);
}

/**
 * Append the narrowing of returned value @p src into call result @p dst, a smaller integer, to @p out:
 * held as the call's result is, its low bytes zero- or sign-extended to the word it is held in.
 *
 * @return 0, or -1 when out of memory
 */
static int put_narrow(cc_ssa_function_t *f, ivec_t *out, int dst, int src) {
    uint32_t to = cc_ssa_value_size(f, dst);
    int is_unsigned = cc_ssa_value_is_unsigned(f, dst);
    int word_bits = cc_target_pointer_size() >= 8 ? 64 : 32;

    if (cc_ssa_value_size(f, src) > (uint32_t)(word_bits / 8)) {
        /* i386: a 64-bit value's low word */
        int lo = new_int_value(f, 4, 1);

        if (lo < 0 || put_op_imm(f, out, CC_SSA_LO32, lo, src, 0, 1) != 0) {
            return -1;
        }
        src = lo;
    }
    if (to * 8 >= (uint32_t)word_bits || cc_ssa_value_size(f, src) == to) {
        return put_op_imm(f, out, CC_SSA_MOV, dst, src, 0, is_unsigned);
    }
    if (is_unsigned) {
        return put_op_imm(f, out, CC_SSA_AND, dst, src, (long long)((1ULL << (to * 8)) - 1), 1);
    }
    {
        int up = new_int_value(f, cc_ssa_value_size(f, src), 0);

        return up < 0 || put_op_imm(f, out, CC_SSA_SHL, up, src, word_bits - (int)to * 8, 0) != 0 ||
                       put_op_imm(f, out, CC_SSA_SHR, dst, up, word_bits - (int)to * 8, 0) != 0
                   ? -1
                   : 0;
    }
}

/**
 * Write @p g's body in place of @p call into @p out, the rewrite of @p f.
 *
 * @return 0, or -1 when out of memory
 */
static int splice_body(cc_ssa_function_t *f, const cc_ssa_instr_t *call, const cc_ssa_function_t *g, ivec_t *out) {
    int *vmap;
    int label_base;
    int done_label;
    int v;
    size_t i;
    cc_ssa_instr_t in;

    vmap = (int *)malloc((size_t)(g->value_count > 0 ? g->value_count : 1) * sizeof(*vmap));
    if (vmap == NULL) {
        return -1;
    }
    for (v = 0; v < g->value_count; ++v) {
        vmap[v] = cc_ssa_function_new_value(f, cc_ssa_value_class(g, v));
        if (vmap[v] < 0) {
            free(vmap);
            return -1;
        }
        f->value_ty[vmap[v]] = g->value_ty[v];
    }
    label_base = f->label_count;
    f->label_count += g->label_count;
    done_label = f->label_count++;

    for (i = 0; i < g->instr_count; ++i) {
        const cc_ssa_instr_t *src = &g->instrs[i];

        if (src->op == CC_SSA_PARAM) {
            /* the parameter is its argument, narrowed when the parameter is narrower */
            int arg = call->args[src->param_index];
            int rc;

            if (same_shape(f, vmap[src->dst], f, arg) || cc_ssa_value_size(f, vmap[src->dst]) > cc_ssa_value_size(f, arg)) {
                in = plain_instr(CC_SSA_MOV);
                in.dst = vmap[src->dst];
                in.lhs = arg;
                in.is_unsigned = src->is_unsigned;
                rc = ivec_put(out, &in);
            } else {
                rc = put_narrow(f, out, vmap[src->dst], arg);
            }
            if (rc != 0) {
                free(vmap);
                return -1;
            }
            continue;
        }
        if (src->op == CC_SSA_RET) {
            /* the result is what it returns, and the caller goes on after the call */
            if (src->lhs >= 0 && call->dst >= 0) {
                int rc;

                if (same_shape(f, call->dst, f, vmap[src->lhs]) ||
                    cc_ssa_value_size(f, call->dst) > cc_ssa_value_size(f, vmap[src->lhs])) {
                    in = plain_instr(CC_SSA_MOV);
                    in.dst = call->dst;
                    in.lhs = vmap[src->lhs];
                    in.is_unsigned = call->is_unsigned;
                    rc = ivec_put(out, &in);
                } else {
                    rc = put_narrow(f, out, call->dst, vmap[src->lhs]);
                }
                if (rc != 0) {
                    free(vmap);
                    return -1;
                }
            }
            in = plain_instr(CC_SSA_BR);
            in.label = done_label;
            if (ivec_put(out, &in) != 0) {
                free(vmap);
                return -1;
            }
            continue;
        }
        if (clone_instr(src, vmap, label_base, &in) != 0 || ivec_put(out, &in) != 0) {
            free(vmap);
            return -1;
        }
    }
    free(vmap);
    in = plain_instr(CC_SSA_LABEL);
    in.label = done_label;
    return ivec_put(out, &in);
}

/** The bytes of stack @p f's slots take: cc gives each its own, so this is its frame less its values'. */
static long stack_bytes(const cc_ssa_function_t *f) {
    long total = 0;
    size_t i;

    for (i = 0; i < f->instr_count; ++i) {
        if (f->instrs[i].op == CC_SSA_STACKALLOC && f->instrs[i].imm > 0) {
            total += (long)f->instrs[i].imm;
        }
    }
    return total;
}

/** What inlining @p g is reckoned to add to a caller's frame: its slots, and a word per value. */
static long frame_cost(const cc_ssa_function_t *g) {
    return stack_bytes(g) + (long)g->value_count * VALUE_SLOT_BYTES;
}

/**
 * Whether a frame of @p was bytes may become @p now bytes by inlining a
 * function called once, by gcc's rule: up to LARGE_STACK_FRAME freely,
 * beyond it by at most LARGE_STACK_FRAME_GROWTH percent; and, as cc does
 * not share slots, never past CALLED_ONCE_MAX_FRAME.  A kernel thread of
 * Linux on i386 has 8 KiB of stack: inlining under gcc's rule alone took
 * the kernel's frames over 1 KiB from 21 to 52.  @p now counts what is
 * inlined by frame_cost(), values and all, since values are most of it.
 *
 * @param was  the caller's frame as it was
 * @param now  the caller's frame with what is inlined so far and this
 * @return     1 if it may, else 0
 */
static int frame_growth_ok(long was, long now) {
    return now <= LARGE_STACK_FRAME ||
           (now <= CALLED_ONCE_MAX_FRAME && now - was <= was * LARGE_STACK_FRAME_GROWTH / 100);
}

/** The index of the function of @p m named @p name, or -1. */
static long find_function(const cc_ssa_module_t *m, const char *name) {
    size_t i;

    for (i = 0; i < m->func_count; ++i) {
        if (m->funcs[i].name != NULL && strcmp(m->funcs[i].name, name) == 0) {
            return (long)i;
        }
    }
    return -1;
}

int cc_inline_calls(cc_ssa_module_t *m, size_t fi, const unsigned char *ready, const unsigned char *once) {
    cc_ssa_function_t *f = &m->funcs[fi];
    ivec_t out;
    size_t i;
    int inlined = 0;
    long frame = stack_bytes(f); /* its own slots */
    long grown = frame;          /* and those of what is inlined so far */

    memset(&out, 0, sizeof(out));
    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_t *call = &f->instrs[i];
        long gi = -1;
        const cc_ssa_function_t *g;

        if (call->op == CC_SSA_CALL && call->sym != NULL && out.count < CALLER_MAX_INSTRS) {
            gi = find_function(m, call->sym);
        }
        g = gi >= 0 ? &m->funcs[gi] : NULL;
        if (g != NULL && (size_t)gi != fi && ready[gi] &&
            (cc_inline_candidate(g) ||
             (once != NULL && once[gi] && inline_body_ok(g, CALLED_ONCE_MAX_INSTRS) &&
              frame_growth_ok(frame, grown + frame_cost(g)))) &&
            call_site_inlinable(f, call, g)) {
            grown += frame_cost(g);
            if (splice_body(f, call, g, &out) != 0) {
                break;
            }
            {
                /* what the callee keeps in the object, the caller keeps now too */
                size_t k;

                for (k = 0; k < g->keep_sym_count; ++k) {
                    if (cc_ssa_function_keep_sym(f, g->keep_syms[k]) != 0) {
                        break;
                    }
                }
                if (k < g->keep_sym_count) {
                    cc_ssa_instr_free(call);
                    i++;
                    break;
                }
            }
            cc_ssa_instr_free(call);
            inlined++;
            continue;
        }
        if (ivec_put(&out, call) != 0) {
            break;
        }
    }
    if (i < f->instr_count) {
        /* out of memory: the instructions moved are in out, the rest still in f */
        size_t k;

        for (k = 0; k < out.count; ++k) {
            cc_ssa_instr_free(&out.at[k]);
        }
        for (k = i; k < f->instr_count; ++k) {
            cc_ssa_instr_free(&f->instrs[k]);
        }
        free(out.at);
        free(f->instrs);
        f->instrs = NULL;
        f->instr_count = 0;
        f->instr_cap = 0;
        return -1;
    }
    free(f->instrs);
    f->instrs = out.at;
    f->instr_count = out.count;
    f->instr_cap = out.cap;
    return inlined;
}
