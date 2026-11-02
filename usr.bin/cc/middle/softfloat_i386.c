/*
 * i386 soft-float: floating point without the x87 (-mno-80387, -msoft-float, -mgeneral-regs-only), as gcc
 * compiles it -- every floating operation a call of libgcc's soft-float routines (__adddf3, __ltdf2,
 * __fixdfdi, __extendsfdf2, ...), and floating values held as their bits in the general registers, by
 * gcc's -mno-80387 ABI: a float in %eax, a double in %edx:%eax, a long double's 12 bytes in
 * %eax:%edx:%ecx; on the stack as ever.
 *
 * The IR keeps a float as a double, rounded by FROUND32, and a long double as a value of 10 bytes or more.
 * This pass, run on a function before legalization, gives each of them an integer's class:
 *
 *  - a double (and a float, as the double it is held as) becomes the 8 bytes of its bits, which i386
 *    legalization then takes as a pair of words;
 *  - a long double becomes the address of a 16-byte slot of the frame holding its 12, written by each
 *    instruction that defined it and read by each that used it: its operations take it by address as a
 *    structure (CC_CALL_ARG_ABI_AGG_MEM) and their results come back in %eax:%edx:%ecx
 *    (CC_ABI_RET_EAX_EDX of 12 bytes) into a slot of their own, copied to the value's.
 *
 * A value crosses a call, a return, a load or a store as that says -- a double's 8 bytes, a float's 4, a
 * long double's 12 -- whatever it is held as: lowering leaves a long double that is converted to double to be
 * rounded where the x87 would round it, as it is stored or passed.  An operation with a long double operand
 * is done in long double, and a double result rounded.
 *
 * Float arithmetic stays double arithmetic rounded to float, exact for + - * / (a double has more than
 * twice a float's precision); only conversions that would round twice -- a 64-bit integer or a long double
 * to float -- call the routine that goes straight there.  Negation needs no call: lowering already flips
 * the sign bit in memory.  An asm operand of floating type, which would be the x87's or a register's
 * reinterpretation, and _Float16 (which libgcc passes in SSE registers) are refused.
 *
 * It applies only where the x87 would otherwise do the work: not when SSE does the doubles
 * (-mfpmath=sse, or SSE2 without -mfpmath=387), whose long doubles the backend still refuses.
 *
 * Under -mno-fp-ret-in-387 alone (CC_SOFTFLOAT_RETURNS) the x87 computes, and only the results move, as gcc
 * returns them there: a function's floating result, and a call's, cross in the general registers by the same
 * ABI, through a slot of the frame -- stored (rounded to the size returned), loaded as its words, and back.
 */
#include "cc_diag.h"
#include "cc_middle.h"
#include "cc_options.h"
#include "cc_target.h"
#include "cc_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** What a value of the function as lowered is: no floating value, a double (or float), a long double. */
enum { SF_NONE = 0, SF_D, SF_LD };

/** How a floating parameter came, which the prologue turns into the value the body reads. */
enum { SF_FIX_FLOAT = 1, SF_FIX_D, SF_FIX_LD };

/** The pass's state for one function. */
typedef struct {
    cc_ssa_function_t *f;
    cc_ssa_instr_t *out; /**< the rewritten instructions */
    size_t count;
    size_t cap;
    unsigned char *kind; /**< each value's SF_* as lowered (the values the pass adds are SF_NONE) */
    int nvals;           /**< the values there were */
    int *uses;           /**< how many operands read each of those values */
    int *defs;           /**< how many instructions write each of them */
    int *fbits;          /**< for a double written once from a float: the float's bits (4 bytes), else -1 */
    unsigned char *nat;  /**< each value the pass leaves floating (an x87 one under CC_SOFTFLOAT_RETURNS): 1 */
    int zero_ld;         /**< a long double 0.0's slot, or -1 */
    cc_softfloat_mode_t mode;
    cc_diag_t *diag;
    int failed;
} sf_t;

/** An argument of a call the pass writes: its value, ABI byte and size in bytes. */
typedef struct {
    int v;
    unsigned char abi;
    uint32_t size;
} sf_arg_t;

/**
 * What functions are compiled with: on i386 under -mno-80387, soft-float when the x87, not SSE, would do the
 * doubles (the backend's rule, i386.c); under -mno-fp-ret-in-387 with the x87, its results in the general
 * registers.
 */
cc_softfloat_mode_t cc_softfloat_i386_mode(void) {
    const cc_options_t *o = cc_options_current();
    const cc_target_desc_t *tgt = cc_target_current();

    if (o == NULL || tgt == NULL || tgt->id != CC_TARGET_I386) {
        return CC_SOFTFLOAT_NONE;
    }
    if (!o->no_80387) {
        return o->no_fp_ret_in_387 ? CC_SOFTFLOAT_RETURNS : CC_SOFTFLOAT_NONE;
    }
    return (o->i386_fpmath == CC_FPMATH_SSE || (o->i386_fpmath != CC_FPMATH_387 && o->i386_sse2)) ? CC_SOFTFLOAT_NONE
                                                                                                 : CC_SOFTFLOAT_ALL;
}

/** Fail with message @p msg, naming the function; the first failure's message is kept. */
static void sf_fail(sf_t *z, const char *msg) {
    char buf[256];

    if (z->failed) {
        return;
    }
    z->failed = 1;
    snprintf(buf, sizeof(buf), "%s: %s", z->f->name != NULL ? z->f->name : "<function>", msg);
    cc_diag_set(z->diag, "", 0, 0, buf);
}

/** The SF_* kind of value @p v (SF_NONE for one the pass added, or none). */
static int sf_kind(const sf_t *z, int v) {
    return (v >= 0 && v < z->nvals) ? z->kind[v] : SF_NONE;
}

/** Append @p in to the rewritten instructions. */
static void sf_put(sf_t *z, const cc_ssa_instr_t *in) {
    if (z->failed) {
        return;
    }
    if (z->count == z->cap) {
        size_t ncap = z->cap == 0 ? 64 : z->cap * 2;
        cc_ssa_instr_t *next = (cc_ssa_instr_t *)realloc(z->out, ncap * sizeof(*next));

        if (next == NULL) {
            sf_fail(z, "out of memory in soft-float rewriting");
            return;
        }
        z->out = next;
        z->cap = ncap;
    }
    z->out[z->count++] = *in;
}

/** An instruction of operation @p op that names no value, label or parameter yet. */
static cc_ssa_instr_t sf_blank(cc_ssa_opcode_t op) {
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

/**
 * A new integer value of @p size bytes, held zero-extended if @p is_unsigned.
 *
 * @return the value, or -1 (the pass failed)
 */
static int sf_value(sf_t *z, uint32_t size, int is_unsigned) {
    int v;

    if (z->failed) {
        return -1;
    }
    v = cc_ssa_function_new_value(z->f, CC_VAL_I64);
    if (v < 0) {
        sf_fail(z, "out of memory in soft-float rewriting");
        return -1;
    }
    cc_ssa_value_set_size(z->f, v, size);
    cc_ssa_value_set_unsigned(z->f, v, is_unsigned);
    return v;
}

/** A constant @p imm of @p size bytes: its value, or -1. */
static int sf_const(sf_t *z, long long imm, uint32_t size) {
    int v = sf_value(z, size, 0);
    cc_ssa_instr_t in = sf_blank(CC_SSA_CONST);

    in.dst = v;
    in.imm = imm;
    sf_put(z, &in);
    return v;
}

/** A frame slot of 16 bytes for a long double: its address, or -1. */
static int sf_slot(sf_t *z, int v) {
    cc_ssa_instr_t in = sf_blank(CC_SSA_STACKALLOC);

    if (v < 0) {
        v = sf_value(z, 4, 1);
    }
    in.dst = v;
    in.imm = 16;
    sf_put(z, &in);
    return v;
}

/** The address @p off bytes past address @p base: it, for 0, else a new value, or -1. */
static int sf_at(sf_t *z, int base, long off) {
    cc_ssa_instr_t in;
    int k;

    if (off == 0) {
        return base;
    }
    k = sf_const(z, off, 4);
    in = sf_blank(CC_SSA_ADD);
    in.dst = sf_value(z, 4, 1);
    in.lhs = base;
    in.rhs = k;
    in.is_unsigned = 1;
    sf_put(z, &in);
    return in.dst;
}

/** Copy @p bytes (2 to 12) of a long double from address @p src (segment @p sseg) to @p dst (@p dseg). */
static void sf_copy_ld(sf_t *z, int dst, unsigned char dseg, int src, unsigned char sseg, long bytes) {
    long off;

    if (bytes > 12) {
        bytes = 12;
    }
    for (off = 0; off < bytes && !z->failed; off += 4) {
        long w = bytes - off >= 4 ? 4 : bytes - off;
        cc_ssa_instr_t ld = sf_blank(CC_SSA_LOAD);
        cc_ssa_instr_t st = sf_blank(CC_SSA_STORE);

        ld.dst = sf_value(z, (uint32_t)w, 1);
        ld.lhs = sf_at(z, src, off);
        ld.imm = w;
        ld.is_unsigned = 1;
        ld.seg = sseg;
        sf_put(z, &ld);
        st.lhs = sf_at(z, dst, off);
        st.rhs = ld.dst;
        st.imm = w;
        st.seg = dseg;
        sf_put(z, &st);
    }
}

/** Store the x87 bytes of long double @p ld into the slot at @p dst, its 12 bytes as three words. */
static void sf_store_ld_const(sf_t *z, int dst, long double ld) {
    unsigned char raw[16];
    long off;

    memset(raw, 0, sizeof(raw));
    /* the host's long double is the x87's, as the backend has it for data (common.c emit_float_data()) */
    memcpy(raw, &ld, sizeof(ld) < 10 ? sizeof(ld) : 10);
    for (off = 0; off < 12 && !z->failed; off += 4) {
        cc_ssa_instr_t st = sf_blank(CC_SSA_STORE);
        unsigned long long w = (unsigned long long)raw[off] | ((unsigned long long)raw[off + 1] << 8) |
                               ((unsigned long long)raw[off + 2] << 16) | ((unsigned long long)raw[off + 3] << 24);

        st.lhs = sf_at(z, dst, off);
        st.rhs = sf_const(z, (long long)(int)(unsigned)w, 4);
        st.imm = 4;
        sf_put(z, &st);
    }
}

/**
 * Store long double CONST @p in into the slot at @p dst: its value, or one of the special constants lowering
 * marks (is_unsigned with imm 1 LDBL_MIN, 2 its smallest denormal, 3 LDBL_MAX), whose bits are known exactly.
 */
static void sf_ld_const(sf_t *z, int dst, const cc_ssa_instr_t *in) {
    static const unsigned char special[3][10] = {
        {0, 0, 0, 0, 0, 0, 0, 0x80, 0x01, 0x00}, /* LDBL_MIN */
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0},          /* the smallest denormal */
        {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x7f} /* LDBL_MAX */
    };

    if (in->is_unsigned && in->imm >= 1 && in->imm <= 3) {
        const unsigned char *b = special[in->imm - 1];
        long off;

        for (off = 0; off < 12 && !z->failed; off += 4) {
            cc_ssa_instr_t st = sf_blank(CC_SSA_STORE);
            unsigned w = 0;
            long k;

            for (k = 0; k < 4; ++k) {
                if (off + k < 10) {
                    w |= (unsigned)b[off + k] << (8 * k);
                }
            }
            st.lhs = sf_at(z, dst, off);
            st.rhs = sf_const(z, (long long)(int)w, 4);
            st.imm = 4;
            sf_put(z, &st);
        }
        return;
    }
    sf_store_ld_const(z, dst, in->fimm_ld);
}

/** Whether @p v is a floating value the pass leaves floating (computed by the x87 or SSE). */
static int sf_is_nat(const sf_t *z, int v) {
    return v >= 0 && v < z->nvals && z->nat[v];
}

/**
 * The bits of floating value @p v left floating, as @p size bytes (a float's 4, a double's 8; 12, a long
 * double's in a frame slot): stored into a slot of the frame, rounded to that size as a store rounds it.
 *
 * @return a new integer value of the bits (of 4 or 8 bytes), or for 12 the slot's address; -1 if the pass failed
 */
static int sf_nat_bits(sf_t *z, int v, long size) {
    int slot = sf_slot(z, -1);
    cc_ssa_instr_t st = sf_blank(CC_SSA_STORE);
    cc_ssa_instr_t ld = sf_blank(CC_SSA_LOAD);

    st.lhs = slot;
    st.rhs = v;
    st.imm = size;
    sf_put(z, &st);
    if (size >= 10) {
        return slot;
    }
    ld.dst = sf_value(z, (uint32_t)size, 0);
    ld.lhs = slot;
    ld.imm = size;
    sf_put(z, &ld);
    return ld.dst;
}

/**
 * Floating value @p dst, left floating, made from the bits @p bits of @p size bytes: of a float (4) or a double
 * (8) an integer value, stored into a slot of the frame; of a long double (12) the address of a slot holding
 * them.  Then loaded as the floating value, widened from a float as a load widens it.
 */
static void sf_nat_from_bits(sf_t *z, int dst, int bits, long size) {
    int slot = bits;
    cc_ssa_instr_t ld = sf_blank(CC_SSA_LOAD);

    if (size < 10) {
        cc_ssa_instr_t st = sf_blank(CC_SSA_STORE);

        slot = sf_slot(z, -1);
        st.lhs = slot;
        st.rhs = bits;
        st.imm = size;
        sf_put(z, &st);
    }
    ld.dst = dst;
    ld.lhs = slot;
    ld.imm = size;
    sf_put(z, &ld);
}

/**
 * Call libgcc's routine @p name with @p n arguments.  A long double result (@p ret_ld) comes back in
 * %eax:%edx:%ecx into a slot of its own, copied into the slot at @p dst; any other result is @p dst itself,
 * of @p ret_size bytes (a 1- or 2-byte one taken from %al or %ax, extended as @p ret_unsigned says).
 */
static void sf_call(sf_t *z, const char *name, int dst, int ret_ld, long ret_size, int ret_unsigned,
                    const sf_arg_t *args, size_t n) {
    cc_ssa_instr_t in = sf_blank(CC_SSA_CALL);
    size_t i;
    int tmp = -1;

    if (z->failed) {
        return;
    }
    in.sym = cc_strdup(name);
    in.args = (int *)calloc(n, sizeof(*in.args));
    in.call_arg_abi = (unsigned char *)calloc(n, sizeof(*in.call_arg_abi));
    in.call_arg_sizes = (uint32_t *)calloc(n, sizeof(*in.call_arg_sizes));
    if (in.sym == NULL || in.args == NULL || in.call_arg_abi == NULL || in.call_arg_sizes == NULL) {
        free(in.sym);
        free(in.args);
        free(in.call_arg_abi);
        free(in.call_arg_sizes);
        sf_fail(z, "out of memory in soft-float rewriting");
        return;
    }
    for (i = 0; i < n; ++i) {
        in.args[i] = args[i].v;
        in.call_arg_abi[i] = args[i].abi;
        in.call_arg_sizes[i] = args[i].size;
    }
    in.arg_count = n;
    in.call_fixed_count = (int)n;
    if (ret_ld) {
        /* into a slot of its own, defined by its STACKALLOC and the call alone, as a structure's result */
        tmp = sf_slot(z, -1);
        in.dst = tmp;
        in.rhs = tmp;
        in.imm = 12;
        in.is_unsigned = 1;
        in.agg_class = CC_ABI_RET_EAX_EDX;
    } else {
        in.dst = dst;
        in.imm = ret_size;
        in.is_unsigned = ret_unsigned;
    }
    sf_put(z, &in);
    if (ret_ld) {
        sf_copy_ld(z, dst, 0, tmp, 0, 12);
    }
}

/** A double argument, by its bits. */
static sf_arg_t sf_arg_d(int v) {
    sf_arg_t a = {v, CC_CALL_ARG_ABI_DEFAULT, 8};

    return a;
}

/** A long double argument: its 12 bytes, copied from the slot at @p v. */
static sf_arg_t sf_arg_ld(int v) {
    sf_arg_t a = {v, CC_CALL_ARG_ABI_AGG_MEM, 12};

    return a;
}

/** An integer argument of @p size bytes (a float's bits are one of 4). */
static sf_arg_t sf_arg_i(int v, uint32_t size) {
    sf_arg_t a = {v, CC_CALL_ARG_ABI_DEFAULT, size};

    return a;
}

/** The argument floating value @p v (a double or a long double) is passed as. */
static sf_arg_t sf_arg(const sf_t *z, int v) {
    return sf_kind(z, v) == SF_LD ? sf_arg_ld(v) : sf_arg_d(v);
}

/** The routine named @p op, the mode of value @p v (df or xf), and @p suffix: "__adddf3". */
static void sf_name(const sf_t *z, char *buf, size_t sz, const char *op, int v, const char *suffix) {
    snprintf(buf, sz, "__%s%s%s", op, sf_kind(z, v) == SF_LD ? "xf" : "df", suffix);
}

/** Double @p v rounded to float: the value of the float's bits (4 bytes), made now or known already. */
static int sf_to_float(sf_t *z, int v) {
    int t;
    sf_arg_t a = sf_arg(z, v);

    if (v >= 0 && v < z->nvals && z->fbits[v] >= 0) {
        return z->fbits[v];
    }
    t = sf_value(z, 4, 0);
    sf_call(z, sf_kind(z, v) == SF_LD ? "__truncxfsf2" : "__truncdfsf2", t, 0, 4, 0, &a, 1);
    return t;
}

/**
 * Note that double @p v holds the value of float bits @p t: so when @p v is written nowhere else, whose one
 * definition is here, before every read of it.
 */
static void sf_note_fbits(sf_t *z, int v, int t) {
    if (v >= 0 && v < z->nvals && z->defs[v] == 1 && sf_kind(z, v) == SF_D) {
        z->fbits[v] = t;
    }
}

/** The bits of float @p t (4 bytes) as a double, into @p dst. */
static void sf_from_float(sf_t *z, int dst, int t) {
    sf_arg_t a = sf_arg_i(t, 4);

    sf_call(z, sf_kind(z, dst) == SF_LD ? "__extendsfxf2" : "__extendsfdf2", dst, sf_kind(z, dst) == SF_LD, 8, 0,
            &a, 1);
    sf_note_fbits(z, dst, t);
}

/** Integer @p v of @p bytes bytes as a 4-byte one, extended as @p is_unsigned says. */
static int sf_widen32(sf_t *z, int v, long bytes, int is_unsigned) {
    cc_ssa_instr_t in;

    if (bytes >= 4) {
        return v;
    }
    in = sf_blank(CC_SSA_EXT);
    in.lhs = v;
    in.imm = bytes * 8;
    in.is_unsigned = is_unsigned ? 1 : 0;
    if (cc_ssa_ext_needs_scratch((int)in.imm, in.is_unsigned, 4, 4)) {
        in.rhs = sf_value(z, 4, 0);
    }
    in.dst = sf_value(z, 4, in.is_unsigned);
    sf_put(z, &in);
    return in.dst;
}

/**
 * An integer to floating (I2F): through the 32-bit routine for a value of 4 bytes or fewer (extended to 4),
 * the 64-bit one for 8; @p to_float names a float result (its bits, 4 bytes) for @p dst = float(v).
 */
static void sf_i2f(sf_t *z, int dst, int v, int is_unsigned, int to_float) {
    long bytes = cc_ssa_value_size(z->f, v);
    char name[32];
    sf_arg_t a;

    if (bytes > 8) {
        bytes = 8;
    }
    if (bytes <= 4) {
        a = sf_arg_i(sf_widen32(z, v, bytes, is_unsigned), 4);
    } else {
        a = sf_arg_i(v, 8);
    }
    if (to_float) {
        int t = sf_value(z, 4, 0);

        snprintf(name, sizeof(name), "__float%s%ssf", is_unsigned ? "un" : "", bytes <= 4 ? "si" : "di");
        sf_call(z, name, t, 0, 4, 0, &a, 1);
        sf_from_float(z, dst, t);
        return;
    }
    snprintf(name, sizeof(name), "__float%s%s%s", is_unsigned ? "un" : "", bytes <= 4 ? "si" : "di",
             sf_kind(z, dst) == SF_LD ? "xf" : "df");
    sf_call(z, name, dst, sf_kind(z, dst) == SF_LD, 8, 0, &a, 1);
}

/**
 * Floating to integer (F2I), truncated, by the routine gcc calls: to an integer narrower than 64 bits whose
 * type lowering noted (cc_ssa_instr_t.f2i_to), the 32-bit __fix*si, or __fixuns*si for an unsigned one (a
 * char's and a short's the low bits of it); to a 64-bit one, __fix*di or __fixuns*di; from a float whose bits
 * are known, the float's own (__fixsf*), else the double's or the long double's.  A narrow one whose type is
 * not known takes the low bits of __fix*di's, right for either signedness, as the x87's fistp of 64 bits.
 */
static void sf_f2i(sf_t *z, const cc_ssa_instr_t *in) {
    long db = cc_ssa_value_size(z->f, in->dst);
    int wide = db >= 8 || in->is_unsigned || in->f2i_to == 0;
    int uns = wide ? db >= 8 && (in->is_unsigned || cc_ssa_value_is_unsigned(z->f, in->dst))
                   : in->f2i_to == CC_SSA_F2I_TO_UNSIGNED;
    int lhs = in->lhs;
    const char *mode = sf_kind(z, lhs) == SF_LD ? "xf" : "df";
    char name[32];
    sf_arg_t a = sf_arg(z, lhs);

    if (db > 8) {
        db = 8;
    }
    if (sf_kind(z, lhs) == SF_D && lhs < z->nvals && z->fbits[lhs] >= 0) {
        a = sf_arg_i(z->fbits[lhs], 4);
        mode = "sf";
    }
    snprintf(name, sizeof(name), "__fix%s%s%s", uns ? "uns" : "", mode, wide ? "di" : "si");
    sf_call(z, name, in->dst, 0, db, cc_ssa_value_is_unsigned(z->f, in->dst), &a, 1);
}

/**
 * Floating value @p v as a long double: itself, or a double widened into a slot of its own (an operation of
 * a long double and a double, as the x87 widens it), whose address is returned.
 */
static int sf_as_ld(sf_t *z, int v) {
    int slot;
    sf_arg_t a;

    if (sf_kind(z, v) == SF_LD) {
        return v;
    }
    slot = sf_slot(z, -1);
    a = sf_arg_d(v);
    sf_call(z, "__extenddfxf2", slot, 1, 12, 0, &a, 1);
    return slot;
}

/**
 * Floating value @p v as a double: itself, or a long double rounded to one in a new value (where the x87
 * would round it: stored as 8 bytes, passed or returned as a double).
 */
static int sf_as_d(sf_t *z, int v) {
    int t;
    sf_arg_t a;

    if (sf_kind(z, v) != SF_LD) {
        return v;
    }
    t = sf_value(z, 8, 0);
    a = sf_arg_ld(v);
    sf_call(z, "__truncxfdf2", t, 0, 8, 0, &a, 1);
    return t;
}

/** A compare (CMP) of two floating values: libgcc's, then its int against 0, as the routine's contract is. */
static void sf_cmp(sf_t *z, const cc_ssa_instr_t *in) {
    static const char *const names[] = {"eq", "ne", "lt", "le", "gt", "ge"};
    char name[32];
    sf_arg_t a[2];
    int r = sf_value(z, 4, 0);
    cc_ssa_instr_t c = *in;
    int ld = sf_kind(z, in->lhs) == SF_LD || sf_kind(z, in->rhs) == SF_LD;

    a[0] = ld ? sf_arg_ld(sf_as_ld(z, in->lhs)) : sf_arg_d(in->lhs);
    a[1] = ld ? sf_arg_ld(sf_as_ld(z, in->rhs)) : sf_arg_d(in->rhs);
    snprintf(name, sizeof(name), "__%s%s2", names[in->cmp_kind], ld ? "xf" : "df");
    sf_call(z, name, r, 0, 4, 0, a, 2);
    c.lhs = r;
    c.rhs = sf_const(z, 0, 4);
    c.is_unsigned = 0;
    sf_put(z, &c);
}

/** The value floating @p v is tested against for a branch's truth: 0.0 of its kind. */
static int sf_zero(sf_t *z, int v) {
    if (sf_kind(z, v) == SF_LD) {
        return z->zero_ld;
    }
    return sf_const(z, 0, 8);
}

/** Whether operation @p op writes its dst (an instruction that defines none may leave a stale one there). */
static int sf_defines(cc_ssa_opcode_t op) {
    return op != CC_SSA_STORE && op != CC_SSA_LABEL && op != CC_SSA_BR && op != CC_SSA_BR_COND && op != CC_SSA_TRAP &&
           op != CC_SSA_RET && op != CC_SSA_ASM;
}

/** The state of a walk over an instruction's operands: the pass, and what was found. */
typedef struct {
    sf_t *z;
    int found;
} sf_walk_t;

/** cc_ssa_for_each_use() callback: note an operand that is floating. */
static void sf_note_float_use(void *ctx, int v, int role) {
    sf_walk_t *w = (sf_walk_t *)ctx;

    (void)role;
    if (sf_kind(w->z, v) != SF_NONE) {
        w->found = 1;
    }
}

/** cc_ssa_for_each_use() callback: count a read of a value of the function as lowered. */
static void sf_note_use(void *ctx, int v, int role) {
    sf_t *z = (sf_t *)ctx;

    (void)role;
    if (v >= 0 && v < z->nvals) {
        z->uses[v]++;
    }
}

/** Whether instruction @p in reads or writes a floating value of the function as lowered. */
static int sf_touches_float(sf_t *z, const cc_ssa_instr_t *in) {
    sf_walk_t w;
    size_t i;

    if (sf_defines(in->op) && sf_kind(z, in->dst) != SF_NONE) {
        return 1;
    }
    /* a floating result returned or received, though the pass leaves the value floating, or thrown away */
    if ((in->op == CC_SSA_CALL || in->op == CC_SSA_CALLI) &&
        (sf_is_nat(z, in->dst) || (in->dst < 0 && in->call_ret_x87))) {
        return 1;
    }
    if (in->op == CC_SSA_RET && sf_is_nat(z, in->lhs)) {
        return 1;
    }
    w.z = z;
    w.found = 0;
    cc_ssa_for_each_use(in, sf_note_float_use, &w);
    for (i = 0; in->op == CC_SSA_ASM && in->asm_out_values != NULL && i < in->asm_out_count; ++i) {
        int v = in->asm_out_values[i];

        if (!CC_SSA_ASM_MEM_INDIRECT_P(v) && sf_kind(z, v) != SF_NONE) {
            w.found = 1;
        }
    }
    return w.found;
}

/** Count, in z->uses, the operands that read each value. */
static void sf_count_uses(sf_t *z) {
    size_t i;

    for (i = 0; i < z->f->instr_count; ++i) {
        cc_ssa_for_each_use(&z->f->instrs[i], sf_note_use, z);
    }
}

/**
 * Whether instruction @p i is a conversion to float that @p next (at i + 1) rounds and nothing else reads,
 * and that would round twice through a double: of a 64-bit integer (I2F), or of a long double (FROUND64).
 */
static int sf_rounds_twice(const sf_t *z, const cc_ssa_instr_t *in, const cc_ssa_instr_t *next) {
    if (next == NULL || next->op != CC_SSA_FROUND32 || next->lhs != in->dst || in->dst < 0 ||
        in->dst >= z->nvals || z->uses[in->dst] != 1 || sf_kind(z, next->dst) != SF_D) {
        return 0;
    }
    if (in->op == CC_SSA_I2F) {
        return in->lhs >= 0 && cc_ssa_value_size(z->f, in->lhs) >= 8;
    }
    return in->op == CC_SSA_FROUND64 && sf_kind(z, in->lhs) == SF_LD;
}

/** Rewrite one call (CALL, CALLI): floating arguments and result by the -mno-80387 ABI. */
static void sf_rewrite_call(sf_t *z, const cc_ssa_instr_t *in0) {
    cc_ssa_instr_t in = *in0;
    size_t a;

    for (a = 0; a < in.arg_count && !z->failed; ++a) {
        int v = in.args[a];
        unsigned char abi = in.call_arg_abi != NULL ? in.call_arg_abi[a] : CC_CALL_ARG_ABI_DEFAULT;

        if (sf_kind(z, v) == SF_NONE) {
            continue;
        }
        if (in.call_arg_abi == NULL || in.call_arg_sizes == NULL) {
            sf_fail(z, "a floating argument without its ABI");
            return;
        }
        /* passed as its ABI says, whatever the value is held as: a long double, a float, else a double */
        if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_LDOUBLE) {
            in.args[a] = sf_as_ld(z, v);
            in.call_arg_abi[a] = (unsigned char)((abi & ~0x0f) | CC_CALL_ARG_ABI_AGG_MEM);
            in.call_arg_sizes[a] = 12;
        } else if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_FLOAT) {
            /* a prototyped float: its bits, one word */
            in.args[a] = sf_to_float(z, v);
            in.call_arg_abi[a] = (unsigned char)((abi & ~0x0f) | CC_CALL_ARG_ABI_DEFAULT);
            in.call_arg_sizes[a] = 4;
        } else if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_DEFAULT) {
            in.args[a] = sf_as_d(z, v);
            in.call_arg_sizes[a] = 8;
        } else {
            sf_fail(z, "a floating argument of an ABI soft-float does not pass");
            return;
        }
    }
    in.call_ret_x87 = 0;
    if (sf_is_nat(z, in.dst)) {
        /* a result the pass leaves floating, by the ABI in the general registers: through a slot into it */
        int dst = in.dst;
        long size = in.imm >= 10 ? 12 : (in.agg_class & CC_ABI_RET_FLOAT) != 0 ? 4 : 8;

        in.agg_class &= ~CC_ABI_RET_FLOAT;
        if (size == 12) {
            in.dst = sf_slot(z, -1);
            in.rhs = in.dst;
            in.is_unsigned = 1;
            in.agg_class = CC_ABI_RET_EAX_EDX;
        } else {
            in.dst = sf_value(z, (uint32_t)size, 0);
        }
        in.imm = size;
        sf_put(z, &in);
        sf_nat_from_bits(z, dst, in.dst, size);
        return;
    }
    if (sf_kind(z, in.dst) != SF_NONE && in.imm >= 10) {
        /* a long double result, in %eax:%edx:%ecx: into a slot of its own, then the value's (rounded, if the
         * value is a double) */
        int tmp = sf_slot(z, -1);
        int dst = in.dst;

        in.dst = tmp;
        in.rhs = tmp;
        in.imm = 12;
        in.is_unsigned = 1;
        in.agg_class = CC_ABI_RET_EAX_EDX;
        sf_put(z, &in);
        if (sf_kind(z, dst) == SF_LD) {
            sf_copy_ld(z, dst, 0, tmp, 0, 12);
        } else {
            sf_arg_t a1 = sf_arg_ld(tmp);

            sf_call(z, "__truncxfdf2", dst, 0, 8, 0, &a1, 1);
        }
        return;
    }
    if (sf_kind(z, in.dst) != SF_NONE) {
        int dst = in.dst;

        if ((in.agg_class & CC_ABI_RET_FLOAT) != 0) {
            /* a float's bits in %eax */
            in.dst = sf_value(z, 4, 0);
            in.imm = 4;
            in.agg_class &= ~CC_ABI_RET_FLOAT;
            sf_put(z, &in);
            sf_from_float(z, dst, in.dst);
            return;
        }
        /* a double's bits in %edx:%eax (widened, if the value is a long double) */
        in.imm = 8;
        if (sf_kind(z, dst) == SF_LD) {
            sf_arg_t a1;

            in.dst = sf_value(z, 8, 0);
            sf_put(z, &in);
            a1 = sf_arg_d(in.dst);
            sf_call(z, "__extenddfxf2", dst, 1, 12, 0, &a1, 1);
            return;
        }
    }
    sf_put(z, &in);
}

/** Rewrite instruction @p in, @p next the one after it (or NULL); *@p skip is set when that one is done too. */
static void sf_rewrite(sf_t *z, const cc_ssa_instr_t *in, const cc_ssa_instr_t *next, int *skip) {
    cc_ssa_instr_t c = *in;
    char name[32];
    sf_arg_t a[2];

    *skip = 0;
    if (!sf_touches_float(z, in)) {
        sf_put(z, in);
        return;
    }
    switch (in->op) {
    case CC_SSA_CONST:
        if (sf_kind(z, in->dst) == SF_LD) {
            sf_ld_const(z, in->dst, in);
        } else {
            union {
                double d;
                long long bits;
            } u;

            u.d = in->fimm;
            c.imm = u.bits;
            c.fimm = 0.0;
            c.fimm_ld = 0.0L;
            c.is_unsigned = 0;
            sf_put(z, &c);
        }
        return;
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_DIV: {
        static const char *const ops[] = {"add", "sub", "mul", "div"};

        int ld;

        if (in->rhs < 0 || sf_kind(z, in->dst) == SF_NONE || sf_kind(z, in->lhs) == SF_NONE ||
            sf_kind(z, in->rhs) == SF_NONE) {
            break;
        }
        ld = sf_kind(z, in->dst) == SF_LD || sf_kind(z, in->lhs) == SF_LD || sf_kind(z, in->rhs) == SF_LD;
        if (!ld) {
            a[0] = sf_arg_d(in->lhs);
            a[1] = sf_arg_d(in->rhs);
            snprintf(name, sizeof(name), "__%sdf3", ops[in->op - CC_SSA_ADD]);
            sf_call(z, name, in->dst, 0, 8, 0, a, 2);
            return;
        }
        /* in long double if either operand is one, as the x87 does: a double operand widened, a double
         * result rounded */
        a[0] = sf_arg_ld(sf_as_ld(z, in->lhs));
        a[1] = sf_arg_ld(sf_as_ld(z, in->rhs));
        snprintf(name, sizeof(name), "__%sxf3", ops[in->op - CC_SSA_ADD]);
        if (sf_kind(z, in->dst) == SF_LD) {
            sf_call(z, name, in->dst, 1, 12, 0, a, 2);
        } else {
            int t = sf_slot(z, -1);

            sf_call(z, name, t, 1, 12, 0, a, 2);
            a[0] = sf_arg_ld(t);
            sf_call(z, "__truncxfdf2", in->dst, 0, 8, 0, a, 1);
        }
        return;
    }
    case CC_SSA_CMP:
        if (sf_kind(z, in->lhs) == SF_NONE || sf_kind(z, in->rhs) == SF_NONE) {
            break;
        }
        sf_cmp(z, in);
        return;
    case CC_SSA_BR_COND: {
        /* true unless equal to 0.0: NaN is true, as __ne*f2 says of it */
        int r = sf_value(z, 4, 0);

        a[0] = sf_arg(z, in->lhs);
        a[1] = sf_arg(z, sf_zero(z, in->lhs));
        sf_name(z, name, sizeof(name), "ne", in->lhs, "2");
        sf_call(z, name, r, 0, 4, 0, a, 2);
        c.lhs = r;
        sf_put(z, &c);
        return;
    }
    case CC_SSA_I2F:
        if (sf_kind(z, in->dst) == SF_NONE || sf_kind(z, in->lhs) != SF_NONE) {
            break;
        }
        if (sf_rounds_twice(z, in, next)) {
            /* a 64-bit integer to float: straight there, not through a double */
            sf_i2f(z, next->dst, in->lhs, in->is_unsigned, 1);
            *skip = 1;
            return;
        }
        sf_i2f(z, in->dst, in->lhs, in->is_unsigned, 0);
        return;
    case CC_SSA_F2I:
        if (sf_kind(z, in->lhs) == SF_NONE || sf_kind(z, in->dst) != SF_NONE) {
            break;
        }
        sf_f2i(z, in);
        return;
    case CC_SSA_FROUND32:
        if (sf_kind(z, in->dst) != SF_D || sf_kind(z, in->lhs) == SF_NONE) {
            break;
        }
        if (in->lhs < z->nvals && z->fbits[in->lhs] >= 0) {
            /* a float already: a copy */
            c = sf_blank(CC_SSA_MOV);
            c.dst = in->dst;
            c.lhs = in->lhs;
            sf_put(z, &c);
            sf_note_fbits(z, in->dst, z->fbits[in->lhs]);
            return;
        }
        sf_from_float(z, in->dst, sf_to_float(z, in->lhs));
        return;
    case CC_SSA_FROUND64:
        if (sf_kind(z, in->dst) != SF_D || sf_kind(z, in->lhs) == SF_NONE) {
            break;
        }
        if (sf_kind(z, in->lhs) == SF_D) {
            /* a double already */
            c = sf_blank(CC_SSA_MOV);
            c.dst = in->dst;
            c.lhs = in->lhs;
            sf_put(z, &c);
            return;
        }
        if (sf_rounds_twice(z, in, next)) {
            /* a long double to float: straight there */
            sf_from_float(z, next->dst, sf_to_float(z, in->lhs));
            *skip = 1;
            return;
        }
        a[0] = sf_arg_ld(in->lhs);
        sf_call(z, "__truncxfdf2", in->dst, 0, 8, 0, a, 1);
        return;
    case CC_SSA_MOV: {
        int kd = sf_kind(z, in->dst);
        int ks = sf_kind(z, in->lhs);

        if (kd == SF_LD && ks == SF_LD) {
            sf_copy_ld(z, in->dst, 0, in->lhs, 0, 12);
            return;
        }
        if (kd == SF_LD && ks == SF_D) {
            a[0] = sf_arg_d(in->lhs);
            sf_call(z, "__extenddfxf2", in->dst, 1, 12, 0, a, 1);
            return;
        }
        if (kd == SF_D && ks == SF_LD) {
            a[0] = sf_arg_ld(in->lhs);
            sf_call(z, "__truncxfdf2", in->dst, 0, 8, 0, a, 1);
            return;
        }
        if (kd != SF_LD && ks != SF_LD && in->lhs >= 0 &&
            (kd == ks || cc_ssa_value_size(z->f, kd == SF_NONE ? in->dst : in->lhs) == 8)) {
            /* a double's bits, or a copy of an integer's 8 into one (a change of class only) */
            sf_put(z, in);
            return;
        }
        break;
    }
    case CC_SSA_LOAD:
        /* by the size read: a long double's bytes, a double's, a float's -- widened to what the value is */
        if (in->imm >= 10) {
            if (sf_kind(z, in->dst) == SF_LD) {
                sf_copy_ld(z, in->dst, 0, in->lhs, in->seg, (long)in->imm);
            } else {
                int t = sf_slot(z, -1);

                sf_copy_ld(z, t, 0, in->lhs, in->seg, (long)in->imm);
                a[0] = sf_arg_ld(t);
                sf_call(z, "__truncxfdf2", in->dst, 0, 8, 0, a, 1);
            }
            return;
        }
        if (in->imm == 8) {
            if (sf_kind(z, in->dst) == SF_LD) {
                c.dst = sf_value(z, 8, 0);
                sf_put(z, &c);
                a[0] = sf_arg_d(c.dst);
                sf_call(z, "__extenddfxf2", in->dst, 1, 12, 0, a, 1);
            } else {
                sf_put(z, in);
            }
            return;
        }
        if (in->imm == 4) {
            int t = sf_value(z, 4, 0);

            c.dst = t;
            c.is_unsigned = 0;
            sf_put(z, &c);
            sf_from_float(z, in->dst, t);
            return;
        }
        sf_fail(z, "_Float16 needs SSE, and the x87 is turned off (-mno-80387)");
        return;
    case CC_SSA_STORE:
        /* by the size written, the value rounded (or widened) to it */
        if (in->imm >= 10) {
            sf_copy_ld(z, in->lhs, in->seg, sf_as_ld(z, in->rhs), 0, (long)in->imm);
            return;
        }
        if (in->imm == 8) {
            c.rhs = sf_as_d(z, in->rhs);
            sf_put(z, &c);
            return;
        }
        if (in->imm == 4) {
            c.rhs = sf_to_float(z, in->rhs);
            sf_put(z, &c);
            return;
        }
        sf_fail(z, "_Float16 needs SSE, and the x87 is turned off (-mno-80387)");
        return;
    case CC_SSA_CALL:
    case CC_SSA_CALLI:
        sf_rewrite_call(z, in);
        return;
    case CC_SSA_RET:
        /* as the function returns it, whatever the value is held as */
        c.rhs = -1;
        if (sf_is_nat(z, in->lhs)) {
            /* a value the pass leaves floating: its bits through a slot, rounded to what is returned */
            long size = z->f->ret_abi == CC_CALL_ARG_ABI_LDOUBLE ? 12 : z->f->ret_abi == CC_CALL_ARG_ABI_FLOAT ? 4 : 8;

            c.lhs = sf_nat_bits(z, in->lhs, size);
            c.imm = size;
            if (size == 12) {
                c.agg_class = CC_ABI_RET_EAX_EDX;
            }
            sf_put(z, &c);
            return;
        }
        if (z->f->ret_abi == CC_CALL_ARG_ABI_LDOUBLE) {
            /* its 12 bytes in %eax:%edx:%ecx, from its slot */
            c.lhs = sf_as_ld(z, in->lhs);
            c.agg_class = CC_ABI_RET_EAX_EDX;
            c.imm = 12;
        } else if (z->f->ret_abi == CC_CALL_ARG_ABI_FLOAT) {
            c.lhs = sf_to_float(z, in->lhs);
            c.imm = 4;
        } else {
            c.lhs = sf_as_d(z, in->lhs);
            c.imm = 8;
        }
        sf_put(z, &c);
        return;
    case CC_SSA_ASM:
        sf_fail(z, "an asm operand of floating type needs the x87, which -mno-80387 turns off");
        return;
    default:
        break;
    }
    {
        char msg[128];

        snprintf(msg, sizeof(msg), "a floating operation soft-float (-mno-80387) does not do (IR operation %d)",
                 (int)in->op);
        sf_fail(z, msg);
    }
}

/**
 * Rewrite parameter instruction @p in, taken as its ABI says: a double as its bits (two words), a float as its
 * bits (one), a long double as a structure of 12 bytes by value.  What the body reads is made after the
 * parameters (*@p fix): a float's double, a long double's slot copied, either converted if the value is held
 * as the other kind; a double held as such needs nothing.
 */
static void sf_param(sf_t *z, const cc_ssa_instr_t *in, cc_ssa_instr_t *fix, size_t *nfix) {
    cc_ssa_function_t *f = z->f;
    int pi = in->param_index;
    unsigned char abi = (f->param_abi != NULL && pi >= 0) ? f->param_abi[pi] : CC_CALL_ARG_ABI_DEFAULT;
    cc_ssa_instr_t c = *in;
    int k = sf_kind(z, in->dst);
    int came;

    if (k == SF_NONE) {
        sf_put(z, in);
        return;
    }
    if (pi < 0 || (size_t)pi >= f->param_count || f->param_abi == NULL) {
        sf_fail(z, "a floating parameter without its ABI");
        return;
    }
    f->param_types[pi] = CC_VAL_I64;
    if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_FLOAT) {
        came = SF_FIX_FLOAT;
        c.dst = sf_value(z, 4, 0);
        c.imm = 4;
        f->param_abi[pi] = (unsigned char)((abi & ~0x0f) | CC_CALL_ARG_ABI_DEFAULT);
    } else if (CC_ABI_KIND(abi) == CC_CALL_ARG_ABI_LDOUBLE) {
        /* (a value of 12 bytes is held as their address, as a structure's) */
        came = SF_FIX_LD;
        c.dst = sf_value(z, 12, 1);
        c.imm = 12;
        f->param_abi[pi] = (unsigned char)((abi & ~0x0f) | CC_CALL_ARG_ABI_AGG_MEM);
    } else if (k == SF_D) {
        c.imm = 8;
        sf_put(z, &c);
        return;
    } else {
        came = SF_FIX_D;
        c.dst = sf_value(z, 8, 0);
        c.imm = 8;
    }
    if (f->param_values != NULL) {
        f->param_values[pi] = c.dst;
    }
    sf_put(z, &c);
    fix[*nfix] = sf_blank(CC_SSA_MOV);
    fix[*nfix].dst = in->dst;
    fix[*nfix].lhs = c.dst;
    fix[*nfix].imm = came;
    ++*nfix;
}

/**
 * What comes after the parameters: each long double's slot, a long double 0.0's if a branch tests one, and
 * the float and long double parameters (and a double held as a long double) into the values the body reads.
 */
static void sf_prologue(sf_t *z, const cc_ssa_instr_t *fix, size_t nfix, int need_zero_ld) {
    size_t i;
    int v;

    for (v = 0; v < z->nvals; ++v) {
        if (z->kind[v] == SF_LD) {
            sf_slot(z, v);
        }
    }
    if (need_zero_ld) {
        z->zero_ld = sf_slot(z, -1);
        sf_store_ld_const(z, z->zero_ld, 0.0L);
    }
    for (i = 0; i < nfix; ++i) {
        sf_arg_t a;

        if (fix[i].imm == SF_FIX_FLOAT) {
            sf_from_float(z, fix[i].dst, fix[i].lhs);
        } else if (fix[i].imm == SF_FIX_D) {
            a = sf_arg_d(fix[i].lhs);
            sf_call(z, "__extenddfxf2", fix[i].dst, 1, 12, 0, &a, 1);
        } else if (sf_kind(z, fix[i].dst) == SF_LD) {
            sf_copy_ld(z, fix[i].dst, 0, fix[i].lhs, 0, 12);
        } else {
            a = sf_arg_ld(fix[i].lhs);
            sf_call(z, "__truncxfdf2", fix[i].dst, 0, 8, 0, &a, 1);
        }
    }
}

int cc_softfloat_i386_function(cc_ssa_function_t *f, cc_diag_t *diag) {
    sf_t z;
    cc_ssa_instr_t *fix = NULL;
    size_t nfix = 0;
    size_t i;
    int any = f->ret_type == CC_VAL_F64;
    int need_zero_ld = 0;
    int prologue_done = 0;
    int v;

    for (i = 0; i < f->param_count && !any; ++i) {
        any = f->param_types[i] == CC_VAL_F64;
    }
    for (v = 0; v < f->value_count && !any; ++v) {
        any = cc_ssa_value_class(f, v) == CC_VAL_F64;
    }
    for (i = 0; i < f->instr_count && !any; ++i) {
        /* a long double result nothing reads, which would be popped from %st0 */
        any = f->instrs[i].call_ret_x87 && (f->instrs[i].op == CC_SSA_CALL || f->instrs[i].op == CC_SSA_CALLI);
    }
    if (!any) {
        return 0;
    }
    memset(&z, 0, sizeof(z));
    z.f = f;
    z.diag = diag;
    z.mode = cc_softfloat_i386_mode();
    z.nvals = f->value_count;
    z.zero_ld = -1;
    z.kind = (unsigned char *)calloc((size_t)z.nvals + 1, sizeof(*z.kind));
    z.nat = (unsigned char *)calloc((size_t)z.nvals + 1, sizeof(*z.nat));
    z.uses = (int *)calloc((size_t)z.nvals + 1, sizeof(*z.uses));
    z.defs = (int *)calloc((size_t)z.nvals + 1, sizeof(*z.defs));
    z.fbits = (int *)malloc(((size_t)z.nvals + 1) * sizeof(*z.fbits));
    fix = (cc_ssa_instr_t *)calloc(f->param_count + 1, sizeof(*fix));
    if (z.kind == NULL || z.nat == NULL || z.uses == NULL || z.defs == NULL || z.fbits == NULL || fix == NULL) {
        free(z.kind);
        free(z.nat);
        free(z.uses);
        free(z.defs);
        free(z.fbits);
        free(fix);
        cc_diag_set(diag, "", 0, 0, "out of memory in soft-float rewriting");
        return -1;
    }
    for (v = 0; v < z.nvals; ++v) {
        z.fbits[v] = -1;
        if (cc_ssa_value_class(f, v) != CC_VAL_F64) {
            continue;
        }
        if (z.mode == CC_SOFTFLOAT_ALL) {
            z.kind[v] = cc_ssa_value_size(f, v) >= 10 ? SF_LD : SF_D;
        } else {
            z.nat[v] = 1;
        }
    }
    sf_count_uses(&z);
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        size_t k;

        if (in->op == CC_SSA_BR_COND && sf_kind(&z, in->lhs) == SF_LD) {
            need_zero_ld = 1;
        }
        if (sf_defines(in->op) && in->dst >= 0 && in->dst < z.nvals) {
            z.defs[in->dst]++;
        }
        for (k = 0; in->op == CC_SSA_ASM && in->asm_out_values != NULL && k < in->asm_out_count; ++k) {
            int o = in->asm_out_values[k];

            if (!CC_SSA_ASM_MEM_INDIRECT_P(o) && o >= 0 && o < z.nvals) {
                z.defs[o]++;
            }
        }
    }
    for (i = 0; i < f->instr_count && !z.failed; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int skip = 0;

        if (!prologue_done && in->op != CC_SSA_PARAM && in->op != CC_SSA_CHAIN) {
            sf_prologue(&z, fix, nfix, need_zero_ld);
            prologue_done = 1;
        }
        if (in->op == CC_SSA_PARAM && !prologue_done) {
            sf_param(&z, in, fix, &nfix);
            continue;
        }
        if (in->op == CC_SSA_PARAM && sf_kind(&z, in->dst) != SF_NONE) {
            sf_fail(&z, "a floating parameter after the function's first instruction");
            break;
        }
        sf_rewrite(&z, in, i + 1 < f->instr_count ? &f->instrs[i + 1] : NULL, &skip);
        if (skip) {
            ++i;
        }
    }
    if (!prologue_done && !z.failed) {
        sf_prologue(&z, fix, nfix, need_zero_ld);
    }
    free(fix);
    free(z.nat);
    free(z.uses);
    free(z.defs);
    free(z.fbits);
    if (z.failed) {
        free(z.out);
        free(z.kind);
        return -1;
    }
    /* the old instructions' names, arguments and operands are the new ones' now (copied, not duplicated) */
    free(f->instrs);
    f->instrs = z.out;
    f->instr_count = z.count;
    f->instr_cap = z.cap;
    for (v = 0; v < z.nvals; ++v) {
        if (z.kind[v] == SF_D) {
            f->value_ty[v].kind = CC_TY_INT;
            f->value_ty[v].size = 8;
            f->value_ty[v].is_unsigned = 0;
        } else if (z.kind[v] == SF_LD) {
            f->value_ty[v].kind = CC_TY_INT;
            f->value_ty[v].size = 4;
            f->value_ty[v].is_unsigned = 1;
        }
    }
    free(z.kind);
    if (f->ret_type == CC_VAL_F64) {
        f->ret_type = CC_VAL_I64;
        f->ret_abi = CC_CALL_ARG_ABI_DEFAULT;
    }
    return 0;
}
