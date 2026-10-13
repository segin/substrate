#include "as_x86_encode.h"
#include "as_util.h"
#include "as_x86_sse3.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *out;
    size_t out_cap;
    size_t at;
    const as_x86_insn_t *insn;
    unsigned default_bits;
    unsigned addr_bits;
    /* Set by emit_prefixes(): whether it wrote 66, and where it ended. */
    int opsize_written;
    size_t prefix_end;
    char *errbuf;
    size_t errbuf_sz;
} enc_ctx_t;

static void set_unsupported_form_named(enc_ctx_t *ctx, const char *mnemonic) {
    set_err(ctx, "unsupported operand form for '%s'", mnemonic != NULL ? mnemonic : "<null>");
}

static void set_unsupported_form(enc_ctx_t *ctx, const as_x86_insn_t *insn) {
    set_unsupported_form_named(ctx, insn != NULL ? insn->mnemonic : NULL);
}

/*
 * The one instruction of an extension that is not a row of the table of
 * as_x86_vec.c: `fisttp`, whose opcode is its operand's size, which
 * Intel's syntax says on the operand.  (This file asked eleven encoders,
 * one for each extension, about every instruction they had between them;
 * the table was asked first and has all of those.)
 */
static int is_x86_sse3_mnemonic(const char *mnemonic) {
    return streq_ci(mnemonic, "fisttp");
}

static int try_encode_x86_sse3_insn(const as_x86_insn_t *insn, uint8_t *out, size_t out_cap, size_t *out_len,
                                    char *errbuf, size_t errbuf_sz) {
    as_x86_sse3_insn_t s3;

    if (insn == NULL || out == NULL) {
        return -1;
    }
    memset(&s3, 0, sizeof(s3));
    s3.mnemonic = insn->mnemonic;
    s3.op_count = insn->op_count >= 2 ? 2 : insn->op_count;
    if (insn->op_count >= 1) {
        s3.dst = insn->ops[0];
    }
    if (insn->op_count >= 2) {
        s3.src = insn->ops[1];
    }
    if (streq_ci(insn->mnemonic, "fisttp")) {
        if (insn->op_count >= 1 && insn->ops[0].kind == AS_X86_OP_MEM) {
            s3.width_bits = (int)insn->ops[0].u.mem.size_bits;
        }
        s3.op_count = 1;
    }
    return as_x86_encode_sse3(&s3, out, out_cap, out_len, errbuf, errbuf_sz);
}

size_t as_x86_outer_prefixes(const as_x86_insn_t *insn, uint8_t out[AS_X86_OUTER_PREFIX_MAX]) {
    size_t n = 0;
    uint8_t seg = 0;

    if (insn == NULL || out == NULL) {
        return 0;
    }
    switch (insn->seg_override) {
    case AS_X86_SEG_CS: seg = 0x2e; break;
    case AS_X86_SEG_DS: seg = 0x3e; break;
    case AS_X86_SEG_ES: seg = 0x26; break;
    case AS_X86_SEG_FS: seg = 0x64; break;
    case AS_X86_SEG_GS: seg = 0x65; break;
    case AS_X86_SEG_SS: seg = 0x36; break;
    default: break;
    }
    if (seg != 0) {
        out[n++] = seg;
    }
    if (insn->address_size_override) {
        out[n++] = 0x67;
    }
    if (insn->lock_prefix) {
        out[n++] = 0xf0;
    }
    return n;
}

/*
 * The extension encoders were handed the start of the buffer, over
 * whatever prefixes had been written there: `lock cmpxchg16b (%rbx)`
 * lost its lock and was not atomic, and `popcnt %fs:(%rbx), %eax` and
 * every SSE3 to SSE4.2 instruction with a segment read from the wrong
 * place.
 */
int as_x86_encode_ext(as_x86_ext_encode_fn fn, const as_x86_insn_t *insn, uint8_t *out, size_t out_cap,
                      size_t *out_len, char *errbuf, size_t errbuf_sz) {
    uint8_t prefixes[AS_X86_OUTER_PREFIX_MAX];
    size_t n;
    size_t len = 0;

    if (fn == NULL || insn == NULL || out == NULL) {
        return -1;
    }
    /* Nor do they know {load} or {rex}; an instruction of theirs written
     * with one is refused rather than encoded without it. */
    if (insn->prefer_load || insn->rex_wanted) {
        return -1;
    }
    /* (An address through %bx or %si is theirs too: each goes to
     * as_x86_modrm16() for it.) */
    n = as_x86_outer_prefixes(insn, prefixes);
    if (n >= out_cap) {
        return -1;
    }
    if (fn(insn, out + n, out_cap - n, &len, errbuf, errbuf_sz) != 0) {
        return -1;
    }
    /* Nothing written is "not one of mine" from some of them. */
    if (len != 0) {
        memcpy(out, prefixes, n);
        len += n;
    }
    if (out_len != NULL) {
        *out_len = len;
    }
    return 0;
}

static int emit8(enc_ctx_t *ctx, uint8_t v) {
    /*
     * The operand-size prefix is one prefix however many parts of the
     * encoder ask for it.  emit_prefixes() writes it for the statement
     * -- a `w` suffix, a 16-bit register -- and many of the instruction
     * cases below write one of their own on finding a 16-bit operand;
     * a 66 that would stand directly after the prefixes, when they
     * have it already, is that same prefix and is not written again.
     */
    if (v == 0x66 && ctx->opsize_written && ctx->at == ctx->prefix_end) {
        return 0;
    }
    if (ctx->at >= ctx->out_cap) {
        set_err(ctx, "encoding overflow");
        return -1;
    }
    ctx->out[ctx->at++] = v;
    return 0;
}

static int emit16(enc_ctx_t *ctx, uint16_t v) {
    if (emit8(ctx, (uint8_t)(v & 0xffu)) != 0 || emit8(ctx, (uint8_t)((v >> 8) & 0xffu)) != 0) {
        return -1;
    }
    return 0;
}

static int emit32(enc_ctx_t *ctx, uint32_t v) {
    if (emit8(ctx, (uint8_t)(v & 0xffu)) != 0 || emit8(ctx, (uint8_t)((v >> 8) & 0xffu)) != 0 ||
        emit8(ctx, (uint8_t)((v >> 16) & 0xffu)) != 0 || emit8(ctx, (uint8_t)((v >> 24) & 0xffu)) != 0) {
        return -1;
    }
    return 0;
}

static int emit64(enc_ctx_t *ctx, uint64_t v) {
    if (emit32(ctx, (uint32_t)(v & 0xffffffffu)) != 0 ||
        emit32(ctx, (uint32_t)((v >> 32) & 0xffffffffu)) != 0) {
        return -1;
    }
    return 0;
}

static int mem_is_single_base_reg(const as_x86_operand_t *op, as_x86_reg_t reg) {
    const as_x86_mem_t *m;

    if (op == NULL || op->kind != AS_X86_OP_MEM) {
        return 0;
    }
    m = &op->u.mem;
    return m->has_base && m->base == reg && !m->has_index && !m->has_disp;
}

static int reg_is_exact_low3(const as_x86_operand_t *op, as_x86_reg_t reg) {
    if (op == NULL || op->kind != AS_X86_OP_REG) {
        return 0;
    }
    /* (The register itself, and not the one of %r8 to %r15 whose low
     * three bits are its: %r9 passed for %rcx, so that `shrdq $7, %r8,
     * %r9` had its destination taken for the count and was refused, and
     * %r8 passed for the accumulator and %r10w for %dx.) */
    return op->u.reg == reg;
}

static int is_high8_reg(as_x86_reg_t r) {
    return r == AS_X86_REG_AH || r == AS_X86_REG_CH ||
           r == AS_X86_REG_DH || r == AS_X86_REG_BH;
}

static uint8_t reg_code3(as_x86_reg_t r) {
    switch (r) {
    case AS_X86_REG_AH:
        return 4;
    case AS_X86_REG_CH:
        return 5;
    case AS_X86_REG_DH:
        return 6;
    case AS_X86_REG_BH:
        return 7;
    default:
        return (uint8_t)(r & 7);
    }
}

static int is_dx_port_operand(const as_x86_operand_t *op) {
    return reg_is_exact_low3(op, AS_X86_REG_EDX) || mem_is_single_base_reg(op, AS_X86_REG_EDX);
}

static int mem_operand_bits(const as_x86_operand_t *op) {
    if (op == NULL || op->kind != AS_X86_OP_MEM) {
        return 0;
    }
    return (int)op->u.mem.size_bits;
}

static int operand_bits(const as_x86_operand_t *op) {
    if (op == NULL) {
        return 0;
    }
    if (op->kind == AS_X86_OP_MEM) {
        return (int)op->u.mem.size_bits;
    }
    return (int)op->size_bits;
}

/*
 * The x87 instructions that take no operand: constants, the functions of
 * the top of the stack, and control.  A compiler loads 0.0 with fldz and
 * 1.0 with fld1, and neither was here; nor were the rest.  The same bytes
 * in every mode.
 */
static const struct {
    const char *mnemonic;
    uint8_t len;
    uint8_t bytes[3];
} x87_no_operand[] = {
    { "fld1",    2, { 0xd9, 0xe8 } }, { "fldl2t",  2, { 0xd9, 0xe9 } },
    { "fldl2e",  2, { 0xd9, 0xea } }, { "fldpi",   2, { 0xd9, 0xeb } },
    { "fldlg2",  2, { 0xd9, 0xec } }, { "fldln2",  2, { 0xd9, 0xed } },
    { "fldz",    2, { 0xd9, 0xee } },
    { "fchs",    2, { 0xd9, 0xe0 } }, { "fabs",    2, { 0xd9, 0xe1 } },
    { "ftst",    2, { 0xd9, 0xe4 } }, { "fxam",    2, { 0xd9, 0xe5 } },
    { "f2xm1",   2, { 0xd9, 0xf0 } }, { "fyl2x",   2, { 0xd9, 0xf1 } },
    { "fptan",   2, { 0xd9, 0xf2 } }, { "fpatan",  2, { 0xd9, 0xf3 } },
    { "fxtract", 2, { 0xd9, 0xf4 } }, { "fprem1",  2, { 0xd9, 0xf5 } },
    { "fdecstp", 2, { 0xd9, 0xf6 } }, { "fincstp", 2, { 0xd9, 0xf7 } },
    { "fprem",   2, { 0xd9, 0xf8 } }, { "fyl2xp1", 2, { 0xd9, 0xf9 } },
    { "fsqrt",   2, { 0xd9, 0xfa } }, { "fsincos", 2, { 0xd9, 0xfb } },
    { "frndint", 2, { 0xd9, 0xfc } }, { "fscale",  2, { 0xd9, 0xfd } },
    { "fsin",    2, { 0xd9, 0xfe } }, { "fcos",    2, { 0xd9, 0xff } },
    { "fnop",    2, { 0xd9, 0xd0 } },
    { "fcompp",  2, { 0xde, 0xd9 } }, { "fucompp", 2, { 0xda, 0xe9 } },
    { "fnclex",  2, { 0xdb, 0xe2 } }, { "fninit",  2, { 0xdb, 0xe3 } },
    { "fclex",   3, { 0x9b, 0xdb, 0xe2 } }, { "finit", 3, { 0x9b, 0xdb, 0xe3 } },
    { "fwait",   1, { 0x9b } },       { "wait",    1, { 0x9b } },
    /* Of the 8087 and the 80287, and nothing to what came after. */
    { "fneni",   2, { 0xdb, 0xe0 } }, { "fndisi",  2, { 0xdb, 0xe1 } },
    { "fnsetpm", 2, { 0xdb, 0xe4 } }, { "frstpm",  2, { 0xdb, 0xe5 } },
    { "feni",    3, { 0x9b, 0xdb, 0xe0 } }, { "fdisi", 3, { 0x9b, 0xdb, 0xe1 } },
    { "fsetpm",  3, { 0x9b, 0xdb, 0xe4 } },
};

/* 1 if `insn` is one of those and was emitted, 0 if it is not one, -1 on
 * a full buffer. */
static int emit_x87_no_operand(enc_ctx_t *ctx, const as_x86_insn_t *insn) {
    size_t i;
    uint8_t j;

    if (insn->mnemonic == NULL || insn->op_count != 0) {
        return 0;
    }
    for (i = 0; i < sizeof(x87_no_operand) / sizeof(x87_no_operand[0]); ++i) {
        if (!streq_ci(insn->mnemonic, x87_no_operand[i].mnemonic)) {
            continue;
        }
        for (j = 0; j < x87_no_operand[i].len; ++j) {
            if (emit8(ctx, x87_no_operand[i].bytes[j]) != 0) {
                return -1;
            }
        }
        return 1;
    }
    return 0;
}

/*
 * An operand that is not there.  The encoders name their operands a, b
 * and c and many a branch reads a->kind, or a register out of a, before
 * it has counted them: `inc` alone on a line dereferenced a null pointer.
 * An absent operand is this one, of no kind, which every test of kind
 * refuses.
 */
static const as_x86_operand_t no_operand;

static int operand_is_xmm_reg(const as_x86_operand_t *op) {
    return op != NULL && op->kind == AS_X86_OP_REG && op->size_bits == 128 && op->u.reg <= AS_X86_REG_R15;
}

static int merge_string_op_bits(const as_x86_operand_t *a, const as_x86_operand_t *b) {
    int ab = mem_operand_bits(a);
    int bb = mem_operand_bits(b);

    if (ab != 0 && bb != 0 && ab != bb) {
        return -1;
    }
    if (ab != 0) {
        return ab;
    }
    return bb;
}

static int default_string_op_bits(const as_x86_insn_t *insn) {
    if (insn == NULL) {
        return 0;
    }
    if (insn->byte_op) {
        return 8;
    }
    if (insn->rex_w) {
        return 64;
    }
    if (insn->default_bits == 16u) {
        return insn->operand_size_override ? 32 : 16;
    }
    return insn->operand_size_override ? 16 : 32;
}

/*
 * The operand-size prefix of a 16-bit string instruction in 32-bit code.
 * Nothing when the suffix has had it written already (`movsw`): written
 * here as well it was there twice, `66 66 a5`.
 */
static int string_op_size_prefix(enc_ctx_t *ctx, const as_x86_insn_t *insn) {
    if (insn->operand_size_override || insn->default_bits == 16u) {
        return 0;
    }
    return emit8(ctx, 0x66);
}

static int resolved_string_op_bits(const as_x86_insn_t *insn, const as_x86_operand_t *a, const as_x86_operand_t *b) {
    int bits = merge_string_op_bits(a, b);

    if (bits != 0) {
        return bits;
    }
    return default_string_op_bits(insn);
}

static int match_reg_mem_pair(const as_x86_operand_t *a, const as_x86_operand_t *b,
                              as_x86_reg_t reg, as_x86_reg_t base,
                              const as_x86_operand_t **reg_op,
                              const as_x86_operand_t **mem_op) {
    if (reg_is_exact_low3(a, reg) && mem_is_single_base_reg(b, base)) {
        if (reg_op != NULL) {
            *reg_op = a;
        }
        if (mem_op != NULL) {
            *mem_op = b;
        }
        return 1;
    }
    if (reg_is_exact_low3(b, reg) && mem_is_single_base_reg(a, base)) {
        if (reg_op != NULL) {
            *reg_op = b;
        }
        if (mem_op != NULL) {
            *mem_op = a;
        }
        return 1;
    }
    return 0;
}

static int is_disp8(int32_t v) {
    return v >= -128 && v <= 127;
}


static unsigned effective_i386_operand_bits(const as_x86_insn_t *insn) {
    unsigned defbits;

    if (insn == NULL) {
        return 32;
    }
    if (insn->byte_op) {
        return 8;
    }
    defbits = insn->default_bits == 16u ? 16u : 32u;
    if (insn->operand_size_override) {
        return defbits == 16u ? 32u : 16u;
    }
    return defbits;
}

static unsigned effective_i386_addr_bits(const as_x86_insn_t *insn) {
    unsigned defbits;
    size_t i;

    if (insn == NULL) {
        return 32;
    }
    defbits = insn->default_bits == 16u ? 16u : 32u;
    for (i = 0; i < insn->op_count; ++i) {
        if (insn->ops[i].kind == AS_X86_OP_MEM &&
            (insn->ops[i].u.mem.addr_bits == 16u || insn->ops[i].u.mem.addr_bits == 32u)) {
            return insn->ops[i].u.mem.addr_bits;
        }
    }
    return defbits;
}

static int emit_i386_imm_or_rel(enc_ctx_t *ctx, uint32_t v, unsigned bits) {
    /* Eight bits are eight bits: asked for one byte this wrote four, and
     * `movb $0, %al` in 64-bit code was followed by three bytes of zeros
     * that the processor then ran. */
    if (bits == 8u) {
        return emit8(ctx, (uint8_t)v);
    }
    if (bits == 16u) {
        return emit16(ctx, (uint16_t)v);
    }
    return emit32(ctx, v);
}

static int resolve_rel_target(const enc_ctx_t *ctx, int32_t target, unsigned inst_len, long long *disp_out) {
    if (ctx == NULL || disp_out == NULL) {
        return -1;
    }
    if (ctx->insn != NULL && ctx->insn->rel_is_disp) {
        *disp_out = (long long)target;
        return 0;
    }
    if (ctx->insn != NULL && ctx->insn->has_section_offset) {
        *disp_out = (long long)target - ((long long)ctx->insn->section_offset + (long long)inst_len);
    } else {
        *disp_out = (long long)target;
    }
    return 0;
}

/* Mnemonic allow-lists for the LOCK and REP/REPNE prefixes — the CPU
 * raises #UD if either is paired with the wrong instruction, so an
 * accepted-but-malformed encoding here corrupts the .o silently and
 * fails at runtime (or worse, decodes as something else).  Tables
 * mirror the Intel SDM Vol 2 prefix-encoding sections. */
static int mnemonic_lock_compatible(const char *mn) {
    if (mn == NULL) return 0;
    return streq_ci(mn, "add")  || streq_ci(mn, "adc")  ||
           streq_ci(mn, "and")  || streq_ci(mn, "btc")  ||
           streq_ci(mn, "btr")  || streq_ci(mn, "bts")  ||
           streq_ci(mn, "cmpxchg") || streq_ci(mn, "cmpxchg8b") ||
           streq_ci(mn, "cmpxchg16b") ||
           streq_ci(mn, "dec")  || streq_ci(mn, "inc")  ||
           streq_ci(mn, "neg")  || streq_ci(mn, "not")  ||
           streq_ci(mn, "or")   || streq_ci(mn, "sbb")  ||
           streq_ci(mn, "sub")  || streq_ci(mn, "xor")  ||
           streq_ci(mn, "xadd") || streq_ci(mn, "xchg");
}

static int mnemonic_rep_compatible(const char *mn) {
    if (mn == NULL) return 0;
    /* REP / REPE / REPZ / REPNE / REPNZ all gate on the same set of
     * string ops; we don't distinguish here because the prefix byte
     * was already chosen by the caller. */
    return streq_ci(mn, "movs")  || streq_ci(mn, "movsb") ||
           streq_ci(mn, "movsw") || streq_ci(mn, "movsd") ||
           streq_ci(mn, "movsl") || streq_ci(mn, "movsq") ||
           streq_ci(mn, "lods")  || streq_ci(mn, "lodsb") ||
           streq_ci(mn, "lodsw") || streq_ci(mn, "lodsd") ||
           streq_ci(mn, "lodsl") || streq_ci(mn, "lodsq") ||
           streq_ci(mn, "stos")  || streq_ci(mn, "stosb") ||
           streq_ci(mn, "stosw") || streq_ci(mn, "stosd") ||
           streq_ci(mn, "stosl") || streq_ci(mn, "stosq") ||
           streq_ci(mn, "scas")  || streq_ci(mn, "scasb") ||
           streq_ci(mn, "scasw") || streq_ci(mn, "scasd") ||
           streq_ci(mn, "scasl") || streq_ci(mn, "scasq") ||
           streq_ci(mn, "cmps")  || streq_ci(mn, "cmpsb") ||
           streq_ci(mn, "cmpsw") || streq_ci(mn, "cmpsd") ||
           streq_ci(mn, "cmpsl") || streq_ci(mn, "cmpsq") ||
           streq_ci(mn, "ins")   || streq_ci(mn, "insb")  ||
           streq_ci(mn, "insw")  || streq_ci(mn, "insd")  ||
           streq_ci(mn, "insl")  ||
           streq_ci(mn, "outs")  || streq_ci(mn, "outsb") ||
           streq_ci(mn, "outsw") || streq_ci(mn, "outsd") ||
           streq_ci(mn, "outsl");
}

/*
 * The instructions that are not string instructions and that rep is
 * written before all the same, as GNU as allows: `rep nop`, which is
 * how pause was written before it had a name and is how compilers and
 * kernels still write it; `rep ret`, which compilers emit for a
 * processor that mispredicts a ret that is a branch's target; and
 * `rep bsf` and `rep bsr`, which are tzcnt and lzcnt to a processor
 * that has them and the plain instruction to one that has not.
 */
/*
 * The four wide Key Locker instructions are one opcode, F3 0F 38 D8,
 * told apart by ModRM.reg: its value, or -1.  The two encryptions were
 * both /0 in 32-bit code -- so aesencwide256kl was aesencwide128kl --
 * and the decryptions, and all four in 64-bit code, were not known.
 */
static int keylocker_wide_ext(const char *mn) {
    if (streq_ci(mn, "aesencwide128kl")) return 0;
    if (streq_ci(mn, "aesdecwide128kl")) return 1;
    if (streq_ci(mn, "aesencwide256kl")) return 2;
    if (streq_ci(mn, "aesdecwide256kl")) return 3;
    return -1;
}

/* The instructions of VMX that take no operand: 0F 01 and this byte; 0
 * for a name that is none of them. */
static uint8_t vmx_no_operand_byte(const char *mn) {
    static const struct {
        const char *name;
        uint8_t byte;
    } rows[] = {{"vmcall", 0xc1}, {"vmlaunch", 0xc2}, {"vmresume", 0xc3}, {"vmxoff", 0xc4}, {"vmfunc", 0xd4}};
    size_t i;

    for (i = 0; mn != NULL && i < sizeof(rows) / sizeof(rows[0]); ++i) {
        if (streq_ci(mn, rows[i].name)) {
            return rows[i].byte;
        }
    }
    return 0;
}

static int rep_is_idiom(const as_x86_insn_t *insn) {
    const char *mn = insn->mnemonic;

    if (mn == NULL) {
        return 0;
    }
    if (streq_ci(mn, "nop")) {
        return insn->op_count == 0;
    }
    return streq_ci(mn, "ret") || streq_ci(mn, "retl") || streq_ci(mn, "retq") || streq_ci(mn, "retw") ||
           streq_ci(mn, "bsf") || streq_ci(mn, "bsr");
}

/*
 * The prefixes, in the order GNU as writes them: segment, address size,
 * operand size, rep, lock.  A processor takes them in any order, but an
 * object is compared with GNU's byte for byte, by tests and by people,
 * and `rep movsw` is `66 f3 a5` there.  (They were written lock, rep,
 * 66, 67, segment.)
 */
static int emit_prefixes(enc_ctx_t *ctx, const as_x86_insn_t *insn) {
    uint8_t seg = 0;

    if (insn->lock_prefix && !mnemonic_lock_compatible(insn->mnemonic)) {
        set_err(ctx, "lock prefix not allowed on '%s'",
                insn->mnemonic ? insn->mnemonic : "(null)");
        return -1;
    }
    /* xacquire and xrelease are the repeat prefixes' bytes on what a
     * lock is taken or dropped by: an instruction that is locked, an
     * exchange, and for xrelease a store. */
    if (insn->hle_prefix) {
        int store = insn->rep_prefix == 1 && insn->mnemonic != NULL && strncasecmp(insn->mnemonic, "mov", 3) == 0 &&
                    insn->op_count == 2 && insn->ops[0].kind == AS_X86_OP_MEM;

        if (!(insn->lock_prefix && mnemonic_lock_compatible(insn->mnemonic)) &&
            !(insn->mnemonic != NULL && strncasecmp(insn->mnemonic, "xchg", 4) == 0) && !store) {
            set_err(ctx, "xacquire or xrelease on '%s', which takes neither", insn->mnemonic ? insn->mnemonic : "(null)");
            return -1;
        }
    } else if (insn->rep_prefix != 0 && !mnemonic_rep_compatible(insn->mnemonic) && !rep_is_idiom(insn)) {
        set_err(ctx, "rep/repne prefix not allowed on '%s'",
                insn->mnemonic ? insn->mnemonic : "(null)");
        return -1;
    }
    switch (insn->seg_override) {
    case AS_X86_SEG_CS: seg = 0x2e; break;
    case AS_X86_SEG_DS: seg = 0x3e; break;
    case AS_X86_SEG_ES: seg = 0x26; break;
    case AS_X86_SEG_FS: seg = 0x64; break;
    case AS_X86_SEG_GS: seg = 0x65; break;
    case AS_X86_SEG_SS: seg = 0x36; break;
    default: break;
    }
    if (seg != 0 && emit8(ctx, seg) != 0) {
        return -1;
    }
    if (insn->address_size_override && emit8(ctx, 0x67) != 0) {
        return -1;
    }
    if (insn->operand_size_override && emit8(ctx, 0x66) != 0) {
        return -1;
    }
    if (insn->rep_prefix == 1 && emit8(ctx, 0xf3) != 0) {
        return -1;
    }
    if (insn->rep_prefix == 2 && emit8(ctx, 0xf2) != 0) {
        return -1;
    }
    if (insn->lock_prefix && emit8(ctx, 0xf0) != 0) {
        return -1;
    }
    ctx->opsize_written = insn->operand_size_override != 0;
    ctx->prefix_end = ctx->at;
    return 0;
}

static int modrm_sib_disp(enc_ctx_t *ctx, uint8_t reg_field, const as_x86_operand_t *rm_op) {
    uint8_t mod;
    uint8_t rm;
    uint8_t modrm;

    /*
     * The register's code, once, for every form below.  %ah to %bh are
     * numbered past the sixteen registers and their codes are 4 to 7;
     * the forms with an absolute or an index-only address took the low
     * three bits of the number, which are those of %al to %bl, so
     * `movb %ah, 0x1234` stored %al.
     */
    reg_field = reg_code3((as_x86_reg_t)reg_field);

    if (rm_op->kind == AS_X86_OP_REG) {
        modrm = (uint8_t)(0xc0u | (reg_code3((as_x86_reg_t)reg_field) << 3) | reg_code3(rm_op->u.reg));
        return emit8(ctx, modrm);
    }

    if (rm_op->kind != AS_X86_OP_MEM) {
        set_err(ctx, "expected r/m operand");
        return -1;
    }

    {
        const as_x86_mem_t *m = &rm_op->u.mem;
        int needs_sib = 0;
        uint8_t sib = 0;
        unsigned addr_bits = ctx->addr_bits == 16u ? 16u : 32u;

        if (addr_bits == 16u) {
            unsigned rm16 = 0;
            int have_rm16 = 0;

            if (m->scale != 0 && m->scale != 1) {
                set_err(ctx, "invalid 16-bit address scale %u", m->scale);
                return -1;
            }
            if (m->rip_relative) {
                set_err(ctx, "RIP-relative mode is x86-64 only");
                return -1;
            }
            if (m->disp_only) {
                modrm = (uint8_t)((0u << 6) | ((reg_field & 7u) << 3) | 6u);
                if (emit8(ctx, modrm) != 0 || emit16(ctx, (uint16_t)m->disp) != 0) {
                    return -1;
                }
                return 0;
            }
            if (!m->has_base && !m->has_index) {
                set_err(ctx, "memory operand missing base/index");
                return -1;
            }
            if (m->has_base && m->has_index) {
                if (m->base == AS_X86_REG_RBX && m->index == AS_X86_REG_RSI) {
                    rm16 = 0u;
                    have_rm16 = 1;
                } else if (m->base == AS_X86_REG_RBX && m->index == AS_X86_REG_RDI) {
                    rm16 = 1u;
                    have_rm16 = 1;
                } else if (m->base == AS_X86_REG_RBP && m->index == AS_X86_REG_RSI) {
                    rm16 = 2u;
                    have_rm16 = 1;
                } else if (m->base == AS_X86_REG_RBP && m->index == AS_X86_REG_RDI) {
                    rm16 = 3u;
                    have_rm16 = 1;
                }
            } else if (m->has_base) {
                if (m->base == AS_X86_REG_RSI) {
                    rm16 = 4u;
                    have_rm16 = 1;
                } else if (m->base == AS_X86_REG_RDI) {
                    rm16 = 5u;
                    have_rm16 = 1;
                } else if (m->base == AS_X86_REG_RBP) {
                    rm16 = 6u;
                    have_rm16 = 1;
                } else if (m->base == AS_X86_REG_RBX) {
                    rm16 = 7u;
                    have_rm16 = 1;
                }
            }
            if (!have_rm16) {
                set_err(ctx, "unsupported 16-bit addressing form");
                return -1;
            }
            if (m->force_disp32) {
                mod = 2u;
            } else if (!m->has_disp) {
                mod = (rm16 == 6u) ? 1u : 0u;
            } else if (is_disp8(m->disp) || (m->disp >= 0xff80 && m->disp <= 0xffff)) {
                /* (An address of 16 bits goes round at 64K: 0xffff is
                 * one less than nothing, and a byte says so.) */
                mod = 1u;
            } else {
                mod = 2u;
            }
            modrm = (uint8_t)((mod << 6) | ((reg_field & 7u) << 3) | rm16);
            if (emit8(ctx, modrm) != 0) {
                return -1;
            }
            if (mod == 1u) {
                return emit8(ctx, (uint8_t)(m->has_disp ? m->disp : 0));
            }
            if (mod == 2u || (!m->has_disp && rm16 == 6u)) {
                return emit16(ctx, (uint16_t)(m->has_disp ? m->disp : 0));
            }
            return 0;
        }

        if (m->scale != 0 && m->scale != 1 && m->scale != 2 && m->scale != 4 && m->scale != 8) {
            set_err(ctx, "invalid scale %u", m->scale);
            return -1;
        }
        if (m->rip_relative) {
            /* No real RIP in 32-bit. Emit as disp-only -- this only happens
             * in cc-emitted dead-code from x86_64-only static-inline
             * helpers; the linker's section gc drops the affected functions. */
            mod = 0;
            rm = 5;
            modrm = (uint8_t)((mod << 6) | ((reg_field & 7u) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit32(ctx, (uint32_t)m->disp) != 0) {
                return -1;
            }
            return 0;
        }
        if (m->has_index && m->index == AS_X86_REG_ESP) {
            set_err(ctx, "ESP cannot be used as SIB index");
            return -1;
        }

        if (m->disp_only) {
            mod = 0;
            rm = 5;
            modrm = (uint8_t)((mod << 6) | ((reg_field & 7u) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit32(ctx, (uint32_t)m->disp) != 0) {
                return -1;
            }
            return 0;
        }

        if (!m->has_base && !m->has_index) {
            set_err(ctx, "memory operand missing base/index");
            return -1;
        }

        if (m->has_index && !m->has_base) {
            mod = 0;
            rm = 4;
            needs_sib = 1;
            sib = (uint8_t)((m->scale == 8 ? 3 : (m->scale == 4 ? 2 : (m->scale == 2 ? 1 : 0))) << 6);
            sib |= (uint8_t)((m->index & 7) << 3);
            sib |= 5;
            modrm = (uint8_t)((mod << 6) | ((reg_field & 7u) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit8(ctx, sib) != 0 || emit32(ctx, (uint32_t)(m->has_disp ? m->disp : 0)) != 0) {
                return -1;
            }
            return 0;
        }

        if (m->force_disp32) {
            mod = 2;
        } else if (!m->has_disp && m->base != AS_X86_REG_EBP) {
            mod = 0;
        } else if (m->has_disp && is_disp8(m->disp)) {
            mod = 1;
        } else {
            mod = 2;
        }

        rm = (uint8_t)(m->base & 7);
        if (m->has_index || m->base == AS_X86_REG_ESP) {
            uint8_t scale_bits;
            uint8_t index_bits = 4;
            uint8_t base_bits = (uint8_t)(m->base & 7);

            needs_sib = 1;
            rm = 4;

            if (m->scale == 8) {
                scale_bits = 3;
            } else if (m->scale == 4) {
                scale_bits = 2;
            } else if (m->scale == 2) {
                scale_bits = 1;
            } else {
                scale_bits = 0;
            }
            if (m->has_index) {
                index_bits = (uint8_t)(m->index & 7);
            }
            sib = (uint8_t)((scale_bits << 6) | (index_bits << 3) | base_bits);
        }

        modrm = (uint8_t)((mod << 6) | (reg_code3((as_x86_reg_t)reg_field) << 3) | rm);
        if (emit8(ctx, modrm) != 0) {
            return -1;
        }
        if (needs_sib && emit8(ctx, sib) != 0) {
            return -1;
        }

        if (mod == 1) {
            return emit8(ctx, (uint8_t)m->disp);
        }
        if (mod == 2 || (mod == 0 && m->base == AS_X86_REG_EBP && !m->has_disp)) {
            return emit32(ctx, (uint32_t)(m->has_disp ? m->disp : 0));
        }
    }

    return 0;
}

static uint8_t reg_low3(as_x86_reg_t r) {
    return reg_code3(r);
}

static uint8_t reg_ext(as_x86_reg_t r) {
    if (is_high8_reg(r)) {
        return 0;
    }
    /* (And the fifth bit of one of APX's registers above it, for the
     * REX2 prefix: AS_X86_REX_EXT4.) */
    return (uint8_t)(((r >> 3) & 1) | (r >= AS_X86_REG_R16 ? AS_X86_REX_EXT4 : 0));
}

static int needs_rex_low8(as_x86_reg_t r) {
    uint8_t lo = reg_low3(r);
    if (is_high8_reg(r)) {
        return 0;
    }
    if (reg_ext(r)) {
        return 1;
    }
    return lo == AS_X86_REG_RSP || lo == AS_X86_REG_RBP || lo == AS_X86_REG_RSI || lo == AS_X86_REG_RDI;
}

static uint8_t scale_bits(unsigned scale) {
    if (scale == 8) {
        return 3;
    }
    if (scale == 4) {
        return 2;
    }
    if (scale == 2) {
        return 1;
    }
    return 0;
}

static int modrm_sib_disp64(enc_ctx_t *ctx, as_x86_reg_t reg_field, const as_x86_operand_t *rm_op,
                            uint8_t *rex_r, uint8_t *rex_x, uint8_t *rex_b) {
    uint8_t mod;
    uint8_t rm;
    uint8_t modrm;

    if (rm_op->kind == AS_X86_OP_REG) {
        *rex_r |= reg_ext(reg_field);
        *rex_b |= reg_ext(rm_op->u.reg);
        modrm = (uint8_t)(0xc0u | (reg_low3(reg_field) << 3) | reg_low3(rm_op->u.reg));
        return emit8(ctx, modrm);
    }
    if (rm_op->kind != AS_X86_OP_MEM) {
        set_err(ctx, "expected r/m operand");
        return -1;
    }

    {
        const as_x86_mem_t *m = &rm_op->u.mem;
        int needs_sib = 0;
        uint8_t sib = 0;

        *rex_r |= reg_ext(reg_field);

        if (m->scale != 0 && m->scale != 1 && m->scale != 2 && m->scale != 4 && m->scale != 8) {
            set_err(ctx, "invalid scale %u", m->scale);
            return -1;
        }
        /* %rsp alone: %r12 has its low three bits and a REX.X to tell it
         * apart, and is an index like any other.  It was refused. */
        if (m->has_index && m->index == AS_X86_REG_RSP) {
            set_err(ctx, "RSP cannot be used as SIB index");
            return -1;
        }

        if (m->rip_relative) {
            mod = 0;
            rm = 5;
            modrm = (uint8_t)((mod << 6) | (reg_low3(reg_field) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit32(ctx, (uint32_t)m->disp) != 0) {
                return -1;
            }
            return 0;
        }

        if (m->disp_only) {
            mod = 0;
            rm = 4;
            needs_sib = 1;
            sib = 0x25;
            modrm = (uint8_t)((mod << 6) | (reg_low3(reg_field) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit8(ctx, sib) != 0 || emit32(ctx, (uint32_t)m->disp) != 0) {
                return -1;
            }
            return 0;
        }

        if (!m->has_base && !m->has_index) {
            set_err(ctx, "memory operand missing base/index");
            return -1;
        }

        if (m->has_index && !m->has_base) {
            mod = 0;
            rm = 4;
            needs_sib = 1;
            *rex_x |= reg_ext(m->index);
            sib = (uint8_t)(scale_bits(m->scale) << 6);
            sib |= (uint8_t)(reg_low3(m->index) << 3);
            sib |= 5;
            modrm = (uint8_t)((mod << 6) | (reg_low3(reg_field) << 3) | rm);
            if (emit8(ctx, modrm) != 0 || emit8(ctx, sib) != 0 || emit32(ctx, (uint32_t)(m->has_disp ? m->disp : 0)) != 0) {
                return -1;
            }
            return 0;
        }

        if (m->force_disp32) {
            mod = 2;
        } else if (!m->has_disp && (m->base & 7) != AS_X86_REG_RBP) {
            mod = 0;
        } else if (!m->has_disp || is_disp8(m->disp)) {
            /* (A base whose low bits are %rbp's and no displacement --
             * %r21 and %r29, which are given none where %rbp and %r13
             * are -- has one of a byte, of nothing.) */
            mod = 1;
        } else {
            mod = 2;
        }

        rm = reg_low3(m->base);
        *rex_b |= reg_ext(m->base);
        if (m->has_index || reg_low3(m->base) == AS_X86_REG_RSP) {
            uint8_t index_bits = 4;
            uint8_t base_bits = reg_low3(m->base);

            needs_sib = 1;
            rm = 4;

            if (m->has_index) {
                index_bits = reg_low3(m->index);
                *rex_x |= reg_ext(m->index);
            }
            sib = (uint8_t)((scale_bits(m->scale) << 6) | (index_bits << 3) | base_bits);
        }

        modrm = (uint8_t)((mod << 6) | (reg_code3(reg_field) << 3) | rm);
        if (emit8(ctx, modrm) != 0) {
            return -1;
        }
        if (needs_sib && emit8(ctx, sib) != 0) {
            return -1;
        }
        if (mod == 1) {
            return emit8(ctx, (uint8_t)m->disp);
        }
        if (mod == 2 || (mod == 0 && reg_low3(m->base) == AS_X86_REG_RBP && !m->has_disp)) {
            return emit32(ctx, (uint32_t)(m->has_disp ? m->disp : 0));
        }
    }

    return 0;
}


static int encode_jcc_rel32(enc_ctx_t *ctx, const char *mnemonic, int32_t rel_target) {
    static const struct {
        const char *name;
        uint8_t cc;
    } map[] = {
        {"jo", 0x0},   {"jno", 0x1}, {"jb", 0x2},  {"jnae", 0x2}, {"jc", 0x2},   {"jnb", 0x3},
        {"jae", 0x3},  {"jnc", 0x3}, {"je", 0x4},  {"jz", 0x4},   {"jne", 0x5},  {"jnz", 0x5},
        {"jbe", 0x6},  {"jna", 0x6}, {"ja", 0x7},  {"jnbe", 0x7}, {"js", 0x8},   {"jns", 0x9},
        {"jp", 0xa},   {"jpe", 0xa}, {"jnp", 0xb}, {"jpo", 0xb},  {"jl", 0xc},   {"jnge", 0xc},
        {"jge", 0xd},  {"jnl", 0xd}, {"jle", 0xe}, {"jng", 0xe},  {"jg", 0xf},   {"jnle", 0xf},
    };
    size_t i;
    long long disp;
    unsigned near_bits;
    unsigned near_len;

    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (streq_ci(mnemonic, map[i].name)) {
            if (ctx->insn == NULL || (!ctx->insn->rel_is_disp && !ctx->insn->has_section_offset)) {
                /* To a symbol, whose distance a relocation fills in:
                 * of 16 bits in 16-bit code.  It was of 32 there too,
                 * and the two bytes after it were the next
                 * instruction's to the processor. */
                int near16 = ctx->insn != NULL && ctx->insn->default_bits == 16u;

                if (emit8(ctx, 0x0f) != 0 || emit8(ctx, (uint8_t)(0x80 | map[i].cc)) != 0 ||
                    (near16 ? emit16(ctx, (uint16_t)rel_target) : emit32(ctx, (uint32_t)rel_target)) != 0) {
                    return -1;
                }
                return 0;
            }
            if (!ctx->insn->force_rel32) {
                if (resolve_rel_target(ctx, rel_target, 2u, &disp) != 0) {
                    return -1;
                }
                if (disp >= -128 && disp <= 127) {
                    if (emit8(ctx, (uint8_t)(0x70 | map[i].cc)) != 0 || emit8(ctx, (uint8_t)((signed char)disp)) != 0) {
                        return -1;
                    }
                    return 0;
                }
            }
            near_bits = (ctx->insn != NULL && ctx->insn->default_bits == 16u) ? 16u : 32u;
            near_len = near_bits == 16u ? 4u : 6u;
            if (resolve_rel_target(ctx, rel_target, near_len, &disp) != 0) {
                return -1;
            }
            if (near_bits == 16u && (disp < -32768 || disp > 32767)) {
                set_err(ctx, "conditional branch target out of range");
                return -1;
            }
            if (emit8(ctx, 0x0f) != 0 || emit8(ctx, (uint8_t)(0x80 | map[i].cc)) != 0 ||
                emit_i386_imm_or_rel(ctx, (uint32_t)disp, near_bits) != 0) {
                return -1;
            }
            return 0;
        }
    }
    return -1;
}

static int is_reg_or_mem(const as_x86_operand_t *op) {
    return op->kind == AS_X86_OP_REG || op->kind == AS_X86_OP_MEM;
}

int as_x86_encode_i386(const as_x86_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz) {
    enc_ctx_t ctx;
    const as_x86_operand_t *a;
    const as_x86_operand_t *b;
    const as_x86_operand_t *c;
    int x87_rc;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (insn == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.out_cap = out_cap;
    ctx.insn = insn;
    ctx.default_bits = insn->default_bits == 16u ? 16u : 32u;
    ctx.addr_bits = effective_i386_addr_bits(insn);
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;

    /* A q suffix asks for 64-bit operands, and this is not 64-bit code:
     * `stosq` was assembled here as stosl. */
    if (insn->rex_w) {
        set_err(&ctx, "'%s' with a 64-bit operand size is only for 64-bit code",
                insn->mnemonic != NULL ? insn->mnemonic : "?");
        return -1;
    }

    /*
     * Nor has it the registers that need REX: %r8 to %r15 at any width,
     * and %spl, %bpl, %sil and %dil.  There is no bit to write them
     * with, and they were written as the registers their low three bits
     * name -- `movb %sil, %al` as `movb %dh, %al`.
     */
    {
        size_t i;

        for (i = 0; i < insn->op_count; ++i) {
            const as_x86_operand_t *op = &insn->ops[i];
            unsigned bits = op->size_bits;

            if (op->kind != AS_X86_OP_REG || is_high8_reg(op->u.reg) ||
                (bits != 8 && bits != 16 && bits != 32 && bits != 64)) {
                continue;
            }
            if (op->u.reg >= AS_X86_REG_R8 || (bits == 8 && op->u.reg >= AS_X86_REG_ESP)) {
                set_err(&ctx, "a register of '%s' is only for 64-bit code",
                        insn->mnemonic != NULL ? insn->mnemonic : "?");
                return -1;
            }
        }
    }

    if (emit_prefixes(&ctx, insn) != 0) {
        return -1;
    }

    a = insn->op_count > 0 ? &insn->ops[0] : &no_operand;
    b = insn->op_count > 1 ? &insn->ops[1] : &no_operand;
    c = insn->op_count > 2 ? &insn->ops[2] : &no_operand;

    if (streq_ci(insn->mnemonic, "movs")) {
        int bits;

        /* With no operands, as it is nearly always written, the suffix
         * says the size; with both, they say it or the suffix does. */
        if (insn->op_count != 0 &&
            (insn->op_count != 2 ||
             !mem_is_single_base_reg(a, AS_X86_REG_EDI) ||
             !mem_is_single_base_reg(b, AS_X86_REG_ESI))) {
            set_unsupported_form_named(&ctx, "movs");
            return -1;
        }
        bits = insn->op_count == 0 ? default_string_op_bits(insn) : resolved_string_op_bits(insn, a, b);
        if (bits == 8) {
            if (emit8(&ctx, 0xa4) != 0) return -1;
        } else if (bits == 16) {
            if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0xa5) != 0) return -1;
        } else if (bits == 32) {
            if (emit8(&ctx, 0xa5) != 0) return -1;
        } else {
            set_unsupported_form_named(&ctx, "movs");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsb")) {
        if (emit8(&ctx, 0xa4) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsd") || streq_ci(insn->mnemonic, "movsl") || streq_ci(insn->mnemonic, "movsw")) {
        if (emit8(&ctx, 0xa5) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stosb")) {
        if (emit8(&ctx, 0xaa) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stosd") || streq_ci(insn->mnemonic, "stosl") || streq_ci(insn->mnemonic, "stosw")) {
        if (emit8(&ctx, 0xab) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stos")) {
        if (emit8(&ctx, insn->byte_op ? 0xaa : 0xab) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpsb")) {
        if (emit8(&ctx, 0xa6) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpsd") || streq_ci(insn->mnemonic, "cmpsl") || streq_ci(insn->mnemonic, "cmpsw")) {
        if (emit8(&ctx, 0xa7) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmps")) {
        if (insn->op_count == 2) {
            int bits;

            if (!mem_is_single_base_reg(a, AS_X86_REG_ESI) ||
                !mem_is_single_base_reg(b, AS_X86_REG_EDI)) {
                set_unsupported_form_named(&ctx, "cmps");
                return -1;
            }
            bits = resolved_string_op_bits(insn, a, b);
            if (bits == 8) {
                if (emit8(&ctx, 0xa6) != 0) return -1;
            } else if (bits == 16) {
                if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0xa7) != 0) return -1;
            } else if (bits == 32) {
                if (emit8(&ctx, 0xa7) != 0) return -1;
            } else {
                set_unsupported_form_named(&ctx, "cmps");
                return -1;
            }
        } else if (emit8(&ctx, insn->byte_op ? 0xa6 : 0xa7) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lodsb")) {
        if (emit8(&ctx, 0xac) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lodsd") || streq_ci(insn->mnemonic, "lodsl") || streq_ci(insn->mnemonic, "lodsw")) {
        if (emit8(&ctx, 0xad) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lods")) {
        if (insn->op_count == 2) {
            int bits;

            if (!reg_is_exact_low3(a, AS_X86_REG_EAX) ||
                !mem_is_single_base_reg(b, AS_X86_REG_ESI)) {
                set_unsupported_form_named(&ctx, "lods");
                return -1;
            }
            bits = mem_operand_bits(b);
            if (bits == 0) {
                bits = default_string_op_bits(insn);
            }
            if (bits == 8) {
                if (emit8(&ctx, 0xac) != 0) return -1;
            } else if (bits == 16) {
                if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0xad) != 0) return -1;
            } else if (bits == 32) {
                if (emit8(&ctx, 0xad) != 0) return -1;
            } else {
                set_unsupported_form_named(&ctx, "lods");
                return -1;
            }
        } else if (emit8(&ctx, insn->byte_op ? 0xac : 0xad) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scasb")) {
        if (emit8(&ctx, 0xae) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scasd") || streq_ci(insn->mnemonic, "scasl") || streq_ci(insn->mnemonic, "scasw")) {
        if (emit8(&ctx, 0xaf) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scas")) {
        if (insn->op_count == 2) {
            int bits;

            if (!reg_is_exact_low3(a, AS_X86_REG_EAX) ||
                !mem_is_single_base_reg(b, AS_X86_REG_EDI)) {
                set_unsupported_form_named(&ctx, "scas");
                return -1;
            }
            bits = mem_operand_bits(b);
            if (bits == 0) {
                bits = default_string_op_bits(insn);
            }
            if (bits == 8) {
                if (emit8(&ctx, 0xae) != 0) return -1;
            } else if (bits == 16) {
                if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0xaf) != 0) return -1;
            } else if (bits == 32) {
                if (emit8(&ctx, 0xaf) != 0) return -1;
            } else {
                set_unsupported_form_named(&ctx, "scas");
                return -1;
            }
        } else if (emit8(&ctx, insn->byte_op ? 0xae : 0xaf) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ins")) {
        int bits = insn->byte_op ? 8 : 0;

        if (insn->op_count != 0 &&
            (insn->op_count != 2 ||
             !mem_is_single_base_reg(a, AS_X86_REG_EDI) ||
             !is_dx_port_operand(b))) {
            set_unsupported_form_named(&ctx, "ins");
            return -1;
        }
        if (bits == 0 && insn->op_count == 2) {
            bits = (int)a->u.mem.size_bits;
        }
        if (bits == 0) {
            bits = default_string_op_bits(insn);
        }
        if (bits == 8) {
            if (emit8(&ctx, 0x6c) != 0) return -1;
        } else if (bits == 16) {
            if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0x6d) != 0) return -1;
        } else if (bits == 32) {
            if (emit8(&ctx, 0x6d) != 0) return -1;
        } else {
            set_unsupported_form_named(&ctx, "ins");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "insb")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              mem_is_single_base_reg(a, AS_X86_REG_EDI) &&
              is_dx_port_operand(b))) {
            set_unsupported_form_named(&ctx, "insb");
            return -1;
        }
        if (emit8(&ctx, 0x6c) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "insw")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              mem_is_single_base_reg(a, AS_X86_REG_EDI) &&
              is_dx_port_operand(b))) {
            set_unsupported_form_named(&ctx, "insw");
            return -1;
        }
        if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0x6d) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "insd") || streq_ci(insn->mnemonic, "insl")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              mem_is_single_base_reg(a, AS_X86_REG_EDI) &&
              is_dx_port_operand(b))) {
            set_unsupported_form_named(&ctx, "insd");
            return -1;
        }
        if (emit8(&ctx, 0x6d) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outs")) {
        int bits = insn->byte_op ? 8 : 0;

        if (insn->op_count != 0 &&
            (insn->op_count != 2 ||
             !is_dx_port_operand(a) ||
             !mem_is_single_base_reg(b, AS_X86_REG_ESI))) {
            set_unsupported_form_named(&ctx, "outs");
            return -1;
        }
        if (bits == 0 && insn->op_count == 2) {
            bits = (int)b->u.mem.size_bits;
        }
        if (bits == 0) {
            bits = default_string_op_bits(insn);
        }
        if (bits == 8) {
            if (emit8(&ctx, 0x6e) != 0) return -1;
        } else if (bits == 16) {
            if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0x6f) != 0) return -1;
        } else if (bits == 32) {
            if (emit8(&ctx, 0x6f) != 0) return -1;
        } else {
            set_unsupported_form_named(&ctx, "outs");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outsb")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              is_dx_port_operand(a) &&
              mem_is_single_base_reg(b, AS_X86_REG_ESI))) {
            set_unsupported_form_named(&ctx, "outsb");
            return -1;
        }
        if (emit8(&ctx, 0x6e) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outsw")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              is_dx_port_operand(a) &&
              mem_is_single_base_reg(b, AS_X86_REG_ESI))) {
            set_unsupported_form_named(&ctx, "outsw");
            return -1;
        }
        if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0x6f) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outsd") || streq_ci(insn->mnemonic, "outsl")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              is_dx_port_operand(a) &&
              mem_is_single_base_reg(b, AS_X86_REG_ESI))) {
            set_unsupported_form_named(&ctx, "outsd");
            return -1;
        }
        if (emit8(&ctx, 0x6f) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "jmp")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REL) {
            unsigned rel_bits = effective_i386_operand_bits(insn);
            if (!insn->force_rel32 && (insn->byte_op || insn->has_section_offset || insn->rel_is_disp)) {
                long long disp;

                if (resolve_rel_target(&ctx, a->u.rel, 2u, &disp) != 0) {
                    return -1;
                }
                if (disp >= -128 && disp <= 127) {
                    if (emit8(&ctx, 0xeb) != 0 || emit8(&ctx, (uint8_t)((signed char)disp)) != 0) {
                        return -1;
                    }
                    return 0;
                }
                if (insn->byte_op) {
                    set_err(&ctx, "jump target out of range");
                    return -1;
                }
            }
            if (emit8(&ctx, 0xe9) != 0) {
                return -1;
            }
            {
                long long disp;
                unsigned inst_len = rel_bits == 16u ? 3u : 5u;
                if (resolve_rel_target(&ctx, a->u.rel, inst_len, &disp) != 0 ||
                    emit_i386_imm_or_rel(&ctx, (uint32_t)disp, rel_bits) != 0) {
                    return -1;
                }
            }
        } else if (insn->op_count == 1 && is_reg_or_mem(a)) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp(&ctx, 4, a) != 0) {
                return -1;
            }
        } else {
            set_unsupported_form_named(&ctx, "jmp");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "call")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REL) {
            unsigned rel_bits = effective_i386_operand_bits(insn);
            if (emit8(&ctx, 0xe8) != 0) {
                return -1;
            }
            {
                long long disp;
                unsigned inst_len = rel_bits == 16u ? 3u : 5u;
                if (resolve_rel_target(&ctx, a->u.rel, inst_len, &disp) != 0 ||
                    emit_i386_imm_or_rel(&ctx, (uint32_t)disp, rel_bits) != 0) {
                    return -1;
                }
            }
        } else if (insn->op_count == 1 && is_reg_or_mem(a)) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp(&ctx, 2, a) != 0) {
                return -1;
            }
        } else {
            set_unsupported_form_named(&ctx, "call");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "enter")) {
        if (insn->op_count == 2 && a->kind == AS_X86_OP_IMM && b->kind == AS_X86_OP_IMM) {
            /* The frame's size is sixteen bits and the nesting level
             * eight: a number that is neither, signed or unsigned, does
             * not fit. */
            if (a->u.imm < -0x8000 || a->u.imm > 0xffff || b->u.imm < -0x80 || b->u.imm > 0xff) {
                set_err(&ctx, "enter takes a size of sixteen bits and a level of eight");
                return -1;
            }
            if (emit8(&ctx, 0xc8) != 0 || emit8(&ctx, (uint8_t)(a->u.imm & 0xff)) != 0 ||
                emit8(&ctx, (uint8_t)((a->u.imm >> 8) & 0xff)) != 0 || emit8(&ctx, (uint8_t)(b->u.imm & 0xff)) != 0) {
                return -1;
            }
        } else {
            set_unsupported_form_named(&ctx, "enter");
            return -1;
        }
    } else if (vmx_no_operand_byte(insn->mnemonic) != 0) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 ||
            emit8(&ctx, vmx_no_operand_byte(insn->mnemonic)) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "arpl")) {
        if (insn->op_count != 2 || (a->kind != AS_X86_OP_REG && a->kind != AS_X86_OP_MEM) || b->kind != AS_X86_OP_REG) {
            set_unsupported_form_named(&ctx, "arpl");
            return -1;
        }
        if (emit8(&ctx, 0x63) != 0 || modrm_sib_disp(&ctx, b->u.reg, a) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lcall") || streq_ci(insn->mnemonic, "ljmp")) {
        /* The far call and jump to a place that is two numbers, the
         * segment and the offset in it.  (Through memory they are rows
         * of the table; this form is not one yet, and comes here.) */
        if (insn->op_count == 2 && a->kind == AS_X86_OP_IMM && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, streq_ci(insn->mnemonic, "lcall") ? 0x9a : 0xea) != 0) {
                return -1;
            }
            if (emit_i386_imm_or_rel(&ctx, (uint32_t)a->u.imm, effective_i386_operand_bits(insn)) != 0) {
                return -1;
            }
            if (emit8(&ctx, (uint8_t)(b->u.imm & 0xff)) != 0 || emit8(&ctx, (uint8_t)((b->u.imm >> 8) & 0xff)) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_MEM) {
            /* (And through memory where the table is not asked: with
             * `{load}` before it, of a name that is not yet all rows.) */
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp(&ctx, streq_ci(insn->mnemonic, "lcall") ? 3 : 5, a) != 0) {
                return -1;
            }
        } else {
            set_unsupported_form_named(&ctx, insn->mnemonic);
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "sahf")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x9e) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xbegin")) {
        if (insn->op_count != 1 || a->kind != AS_X86_OP_REL || emit8(&ctx, 0xc7) != 0 || emit8(&ctx, 0xf8) != 0 ||
            emit32(&ctx, (uint32_t)a->u.rel) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lahf")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x9f) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xlat") || streq_ci(insn->mnemonic, "xlatb")) {
        /* Bare, or with the table's address written out: the same byte. */
        if (insn->op_count > 1) {
            set_unsupported_form_named(&ctx, insn->mnemonic);
            return -1;
        }
        if (emit8(&ctx, 0xd7) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "loop") || streq_ci(insn->mnemonic, "loope") ||
               streq_ci(insn->mnemonic, "loopz") || streq_ci(insn->mnemonic, "loopne") ||
               streq_ci(insn->mnemonic, "loopnz") || streq_ci(insn->mnemonic, "jcxz") ||
               streq_ci(insn->mnemonic, "jecxz") ||
               streq_ci(insn->mnemonic, "jrcxz")) {
        uint8_t opcode;
        int32_t rel;
        long long disp;
        /* The counter that is not the mode's own is asked for by the
         * address-size prefix: without it `jcxz` in 32-bit code was
         * `jecxz`, and tested the other register. */
        int other_counter = (streq_ci(insn->mnemonic, "jcxz") && insn->default_bits != 16u) ||
                            (streq_ci(insn->mnemonic, "jecxz") && insn->default_bits == 16u);

        if (!(insn->op_count == 1 && a->kind == AS_X86_OP_REL)) {
            set_unsupported_form_named(&ctx, insn->mnemonic);
            return -1;
        }
        rel = a->u.rel;
        if (resolve_rel_target(&ctx, rel, other_counter ? 3u : 2u, &disp) != 0) {
            return -1;
        }
        if (other_counter && emit8(&ctx, 0x67) != 0) {
            return -1;
        }
        if (disp < -128 || disp > 127) {
            set_err(&ctx, "loop target out of range");
            return -1;
        }
        if (streq_ci(insn->mnemonic, "loop")) opcode = 0xe2;
        else if (streq_ci(insn->mnemonic, "loope") || streq_ci(insn->mnemonic, "loopz")) opcode = 0xe1;
        else if (streq_ci(insn->mnemonic, "loopne") || streq_ci(insn->mnemonic, "loopnz")) opcode = 0xe0;
        else opcode = 0xe3;
        if (emit8(&ctx, opcode) != 0 || emit8(&ctx, (uint8_t)((signed char)disp)) != 0) {
            return -1;
        }
    } else if (encode_jcc_rel32(&ctx, insn->mnemonic, (insn->op_count == 1 && a->kind == AS_X86_OP_REL) ? a->u.rel : 0) == 0) {
        if (!(insn->op_count == 1 && a->kind == AS_X86_OP_REL)) {
            set_unsupported_form_named(&ctx, insn->mnemonic);
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "monitor")) {
        if (insn->op_count != 3 || a->kind != AS_X86_OP_REG || b->kind != AS_X86_OP_REG || c->kind != AS_X86_OP_REG ||
            emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 || emit8(&ctx, 0xc8) != 0) {
            set_unsupported_form_named(&ctx, insn->mnemonic);
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xgetbv")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 || emit8(&ctx, 0xd0) != 0) {
            return -1;
        }
    } else if (keylocker_wide_ext(insn->mnemonic) >= 0) {
        if (insn->op_count != 1 || a->kind != AS_X86_OP_MEM) {
            set_unsupported_form(&ctx, insn);
            return -1;
        }
        if (emit8(&ctx, 0xf3) != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x38) != 0 || emit8(&ctx, 0xd8) != 0 ||
            modrm_sib_disp(&ctx, (uint8_t)keylocker_wide_ext(insn->mnemonic), a) != 0) {
            return -1;
        }
    } else if (vmx_no_operand_byte(insn->mnemonic) != 0) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 ||
            emit8(&ctx, vmx_no_operand_byte(insn->mnemonic)) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "emms")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x77) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ud2") || streq_ci(insn->mnemonic, "ud2a") ||
               streq_ci(insn->mnemonic, "ud2b")) {
        /* ud2a is ud2 by its old name.  ud2b is another opcode, 0F B9,
         * which is ud1 without the operands it has since been given. */
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 ||
            emit8(&ctx, streq_ci(insn->mnemonic, "ud2b") ? 0xb9 : 0x0b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "int")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_IMM && !a->imm_symbolic && a->u.imm == 3) {
            /* The third has an instruction of one byte to itself. */
            if (emit8(&ctx, 0xcc) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && a->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0xcd) != 0 || emit8(&ctx, (uint8_t)a->u.imm) != 0) {
                return -1;
            }
        } else {
            set_unsupported_form_named(&ctx, "int");
            return -1;
        }
    } else if (insn->op_count == 0 && (streq_ci(insn->mnemonic, "ds") || streq_ci(insn->mnemonic, "es") ||
                                        streq_ci(insn->mnemonic, "cs") || streq_ci(insn->mnemonic, "ss") ||
                                        streq_ci(insn->mnemonic, "fs") || streq_ci(insn->mnemonic, "gs"))) {
        /* Standalone segment-override prefix as a synthetic instruction.
         * The prefix byte was already emitted via emit_prefixes since
         * AS_PREFIX_SEG_OVERRIDE is set on the instruction; nothing
         * more to do here. */
    } else if ((x87_rc = emit_x87_no_operand(&ctx, insn)) != 0) {
        if (x87_rc < 0) {
            return -1;
        }
    } else {
        set_err(&ctx, "unsupported mnemonic: %s", insn->mnemonic != NULL ? insn->mnemonic : "<null>");
        return -1;
    }

    if (out_len != NULL) {
        *out_len = ctx.at;
    }
    return 0;
}

int as_x86_encode_x86_64(const as_x86_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz) {
    enc_ctx_t ctx;
    const as_x86_operand_t *a;
    const as_x86_operand_t *b;
    const as_x86_operand_t *c;
    int x87_rc;
    size_t rex_pos;
    uint8_t rex_w = 0;
    uint8_t rex_r = 0;
    uint8_t rex_x = 0;
    uint8_t rex_b = 0;
    uint8_t rex;
    int force_rex = 0;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (insn == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.out = out;
    ctx.out_cap = out_cap;
    ctx.insn = insn;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;

    if (emit_prefixes(&ctx, insn) != 0) {
        return -1;
    }

    if (insn->explicit_rex) {
        force_rex = 1;
    }
    if ((insn->rex_bits & 0x8u) != 0) {
        rex_w = 1;
    }
    if ((insn->rex_bits & 0x4u) != 0) {
        rex_r = 1;
    }
    if ((insn->rex_bits & 0x2u) != 0) {
        rex_x = 1;
    }
    if ((insn->rex_bits & 0x1u) != 0) {
        rex_b = 1;
    }
    if (insn->rex_w) {
        rex_w = 1;
    }

    /* Reserve one byte for REX prefix; it will be removed if unused. */
    rex_pos = ctx.at;
    if (emit8(&ctx, 0x40) != 0) {
        return -1;
    }
    /* A 66 written next is still directly after the prefixes: the byte
     * between is the place kept for REX, which goes after them all. */
    ctx.prefix_end = ctx.at;

    a = insn->op_count > 0 ? &insn->ops[0] : &no_operand;
    b = insn->op_count > 1 ? &insn->ops[1] : &no_operand;
    c = insn->op_count > 2 ? &insn->ops[2] : &no_operand;

    if (insn->op_count == 0 && mnemonic_rep_compatible(insn->mnemonic)) {
        uint8_t opcode;
        int byte_op = insn->byte_op || streq_ci(insn->mnemonic, "movsb") || streq_ci(insn->mnemonic, "lodsb") ||
                      streq_ci(insn->mnemonic, "stosb") || streq_ci(insn->mnemonic, "scasb") ||
                      streq_ci(insn->mnemonic, "cmpsb") || streq_ci(insn->mnemonic, "insb") ||
                      streq_ci(insn->mnemonic, "outsb");

        if (streq_ci(insn->mnemonic, "movs") || streq_ci(insn->mnemonic, "movsw") ||
            streq_ci(insn->mnemonic, "movsd") || streq_ci(insn->mnemonic, "movsl") ||
            streq_ci(insn->mnemonic, "movsq")) {
            opcode = (uint8_t)(byte_op ? 0xa4u : 0xa5u);
        } else if (streq_ci(insn->mnemonic, "cmps") || streq_ci(insn->mnemonic, "cmpsw") ||
                   streq_ci(insn->mnemonic, "cmpsd") || streq_ci(insn->mnemonic, "cmpsl") ||
                   streq_ci(insn->mnemonic, "cmpsq")) {
            opcode = (uint8_t)(byte_op ? 0xa6u : 0xa7u);
        } else if (streq_ci(insn->mnemonic, "stos") || streq_ci(insn->mnemonic, "stosw") ||
                   streq_ci(insn->mnemonic, "stosd") || streq_ci(insn->mnemonic, "stosl") ||
                   streq_ci(insn->mnemonic, "stosq")) {
            opcode = (uint8_t)(byte_op ? 0xaau : 0xabu);
        } else if (streq_ci(insn->mnemonic, "lods") || streq_ci(insn->mnemonic, "lodsw") ||
                   streq_ci(insn->mnemonic, "lodsd") || streq_ci(insn->mnemonic, "lodsl") ||
                   streq_ci(insn->mnemonic, "lodsq")) {
            opcode = (uint8_t)(byte_op ? 0xacu : 0xadu);
        } else if (streq_ci(insn->mnemonic, "scas") || streq_ci(insn->mnemonic, "scasw") ||
                   streq_ci(insn->mnemonic, "scasd") || streq_ci(insn->mnemonic, "scasl") ||
                   streq_ci(insn->mnemonic, "scasq")) {
            opcode = (uint8_t)(byte_op ? 0xaeu : 0xafu);
        } else if (streq_ci(insn->mnemonic, "ins") || streq_ci(insn->mnemonic, "insw") ||
                   streq_ci(insn->mnemonic, "insd") || streq_ci(insn->mnemonic, "insl")) {
            opcode = (uint8_t)(byte_op ? 0x6cu : 0x6du);
        } else {
            opcode = (uint8_t)(byte_op ? 0x6eu : 0x6fu);
        }
        if (emit8(&ctx, opcode) != 0) {
            return -1;
        }
        goto finish;
    }

    if ((streq_ci(insn->mnemonic, "movnti") || streq_ci(insn->mnemonic, "movntil") ||
         streq_ci(insn->mnemonic, "movntiq")) &&
        insn->op_count == 2 && a != NULL && b != NULL && a->kind == AS_X86_OP_MEM && b->kind == AS_X86_OP_REG) {
        if (b->size_bits == 64 || insn->rex_w || streq_ci(insn->mnemonic, "movntiq")) {
            rex_w = 1;
        }
        if (emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xc3) != 0 ||
            modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
            return -1;
        }
        goto finish;
    }


    if ((streq_ci(insn->mnemonic, "call") || streq_ci(insn->mnemonic, "jmp")) && insn->op_count == 1) {
        if (a->kind == AS_X86_OP_REL) {
            long long disp;
            uint8_t op = streq_ci(insn->mnemonic, "call") ? 0xe8 : 0xe9;

            if (resolve_rel_target(&ctx, a->u.rel, 5u, &disp) != 0 || emit8(&ctx, op) != 0 ||
                emit32(&ctx, (uint32_t)disp) != 0) {
                return -1;
            }
            goto finish;
        }
        if (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) {
            as_x86_reg_t ext = streq_ci(insn->mnemonic, "call") ? AS_X86_REG_RDX : AS_X86_REG_RSP;

            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp64(&ctx, ext, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
            goto finish;
        }
        set_unsupported_form_named(&ctx, insn->mnemonic);
        return -1;
    }

    if (insn->op_count == 1 && a->kind == AS_X86_OP_REL && encode_jcc_rel32(&ctx, insn->mnemonic, a->u.rel) == 0) {
        goto finish;
    }


    if (streq_ci(insn->mnemonic, "mov") && insn->op_count == 2 &&
        (operand_is_xmm_reg(a) || operand_is_xmm_reg(b)) && insn->rex_w) {
        rex_w = 1;
        if (operand_is_xmm_reg(a) && (b->kind == AS_X86_OP_REG || b->kind == AS_X86_OP_MEM)) {
            if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x6e) != 0 ||
                modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
            goto finish;
        }
        if ((a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && operand_is_xmm_reg(b)) {
            if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x7e) != 0 ||
                modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
            goto finish;
        }
        set_unsupported_form(&ctx, insn);
        return -1;
    }

    if (streq_ci(insn->mnemonic, "mov") && insn->op_count == 2 &&
        a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_IMM) {
        unsigned width = insn->byte_op ? 8u : (insn->rex_w ? 64u : (insn->operand_size_override ? 16u : 32u));
        uint8_t op = width == 8u ? 0xb0u : 0xb8u;
        unsigned imm_width = width == 16u ? 2u : (width == 8u ? 1u : 4u);

        if (width == 16u && emit8(&ctx, 0x66) != 0) {
            return -1;
        }
        if (width == 64u) {
            rex_w = 1;
        }
        rex_b |= reg_ext(a->u.reg);
        /*
         * To a 64-bit register, B8+r with REX.W is the form that carries
         * all eight bytes of the value.  Written with four, as this did,
         * the processor takes the next four bytes of the program for the
         * rest of the number and goes on from the middle of the
         * instruction after: nothing the in-tree compiler made for
         * x86-64 ran.  A value that fits in 32 bits signed goes as
         * C7 /0, which is extended to 64; one that does not goes whole.
         */
        if (width == 64u) {
            if (b->u.imm >= (int64_t)INT32_MIN && b->u.imm <= (int64_t)INT32_MAX) {
                if (emit8(&ctx, 0xc7) != 0 || emit8(&ctx, (uint8_t)(0xc0u | (a->u.reg & 7u))) != 0 ||
                    emit_i386_imm_or_rel(&ctx, (uint32_t)b->u.imm, 32u) != 0) {
                    return -1;
                }
            } else if (emit8(&ctx, (uint8_t)(op + (a->u.reg & 7u))) != 0 ||
                       emit64(&ctx, (uint64_t)b->u.imm) != 0) {
                return -1;
            }
            goto finish;
        }
        /* (The register's code, which for %ah to %bh is not the low bits
         * of its number: `mov $1, %ah` was `mov $1, %r8b`.) */
        if (emit8(&ctx, (uint8_t)(op + reg_low3(a->u.reg))) != 0 ||
            emit_i386_imm_or_rel(&ctx, (uint32_t)b->u.imm, imm_width * 8u) != 0) {
            return -1;
        }
        goto finish;
    }

    if (streq_ci(insn->mnemonic, "mov") && insn->op_count == 2 && a->kind == AS_X86_OP_MEM &&
        b->kind == AS_X86_OP_IMM) {
        unsigned width = insn->byte_op ? 8u : (insn->rex_w ? 64u : (insn->operand_size_override ? 16u : 32u));

        if (width == 16u && emit8(&ctx, 0x66) != 0) {
            return -1;
        }
        if (width == 64u) {
            rex_w = 1;
        }
        if (width == 8u) {
            if (emit8(&ctx, 0xc6) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RAX, a, &rex_r, &rex_x, &rex_b) != 0 ||
                emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            if (emit8(&ctx, 0xc7) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RAX, a, &rex_r, &rex_x, &rex_b) != 0 ||
                emit_i386_imm_or_rel(&ctx, (uint32_t)b->u.imm, width == 16u ? 16u : 32u) != 0) {
                return -1;
            }
        }
        goto finish;
    }

    if (streq_ci(insn->mnemonic, "mov") && insn->op_count == 2 && a->kind == AS_X86_OP_REG &&
        (b->kind == AS_X86_OP_MEM || (b->kind == AS_X86_OP_REG && insn->prefer_load))) {
        unsigned width = insn->byte_op ? 8u : (insn->rex_w ? 64u : (insn->operand_size_override ? 16u : 32u));

        if (width == 16u && emit8(&ctx, 0x66) != 0) {
            return -1;
        }
        if (width == 64u) {
            rex_w = 1;
        }
        if (emit8(&ctx, width == 8u ? 0x8a : 0x8b) != 0 ||
            modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
            return -1;
        }
        goto finish;
    }

    if (streq_ci(insn->mnemonic, "mov") && insn->op_count == 2 && is_reg_or_mem(a) && b->kind == AS_X86_OP_REG) {
        unsigned width = insn->byte_op ? 8u : (insn->rex_w ? 64u : (insn->operand_size_override ? 16u : 32u));

        if (width == 16u && emit8(&ctx, 0x66) != 0) {
            return -1;
        }
        if (width == 64u) {
            rex_w = 1;
        }
        if (emit8(&ctx, width == 8u ? 0x88 : 0x89) != 0 ||
            modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
            return -1;
        }
        goto finish;
    }

    if (is_x86_sse3_mnemonic(insn->mnemonic)) {
        return as_x86_encode_ext(try_encode_x86_sse3_insn, insn, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "movabs")) {
        rex_w = 1;
        if (insn->op_count != 2 || a->kind != AS_X86_OP_REG || b->kind != AS_X86_OP_IMM) {
            set_unsupported_form_named(&ctx, "movabs");
            return -1;
        }
        rex_b |= reg_ext(a->u.reg);
        if (emit8(&ctx, (uint8_t)(0xb8u | reg_low3(a->u.reg))) != 0 || emit64(&ctx, (uint64_t)b->u.imm) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "mov")) {
        if (insn->byte_op) {
            if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_IMM) {
                rex_b |= reg_ext(a->u.reg);
                if (needs_rex_low8(a->u.reg)) {
                    force_rex = 1;
                }
                if (emit8(&ctx, (uint8_t)(0xb0u | reg_low3(a->u.reg))) != 0 || emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                    return -1;
                }
            } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && (b->kind == AS_X86_OP_REG || b->kind == AS_X86_OP_MEM)) {
                if (needs_rex_low8(a->u.reg)) {
                    force_rex = 1;
                }
                if (b->kind == AS_X86_OP_REG && needs_rex_low8(b->u.reg)) {
                    force_rex = 1;
                }
                if (emit8(&ctx, 0x8a) != 0 || modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
                    return -1;
                }
            } else if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_REG) {
                if (needs_rex_low8(b->u.reg)) {
                    force_rex = 1;
                }
                if (a->kind == AS_X86_OP_REG && needs_rex_low8(a->u.reg)) {
                    force_rex = 1;
                }
                if (emit8(&ctx, 0x88) != 0 || modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
                    return -1;
                }
            } else if (insn->op_count == 2 && a->kind == AS_X86_OP_MEM && b->kind == AS_X86_OP_IMM) {
                if (emit8(&ctx, 0xc6) != 0 ||
                    modrm_sib_disp64(&ctx, AS_X86_REG_RAX, a, &rex_r, &rex_x, &rex_b) != 0 ||
                    emit8(&ctx, (uint8_t)b->u.imm) != 0) {
                    return -1;
                }
            } else {
                set_unsupported_form_named(&ctx, "movb");
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && b->kind == AS_X86_OP_IMM) {
            int use_imm64 = 0;
            rex_b |= reg_ext(a->u.reg);
            if (operand_bits(a) == 64) {
                rex_w = 1;
            }
            if (rex_w) {
                if (b->u.imm < (int64_t)INT32_MIN || b->u.imm > (int64_t)INT32_MAX) {
                    use_imm64 = 1;
                }
            }
            if (use_imm64) {
                if (emit8(&ctx, (uint8_t)(0xb8u | reg_low3(a->u.reg))) != 0 ||
                    emit64(&ctx, (uint64_t)b->u.imm) != 0) {
                    return -1;
                }
            } else if (rex_w) {
                if (emit8(&ctx, 0xc7) != 0 || emit8(&ctx, (uint8_t)(0xc0u | reg_low3(a->u.reg))) != 0 ||
                    emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                    return -1;
                }
            } else {
                if (emit8(&ctx, (uint8_t)(0xb8u | reg_low3(a->u.reg))) != 0 ||
                    emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                    return -1;
                }
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_REG && (b->kind == AS_X86_OP_REG || b->kind == AS_X86_OP_MEM)) {
            if (emit8(&ctx, 0x8b) != 0 || modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM) && b->kind == AS_X86_OP_REG) {
            if (emit8(&ctx, 0x89) != 0 || modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else if (insn->op_count == 2 && a->kind == AS_X86_OP_MEM && b->kind == AS_X86_OP_IMM) {
            if (emit8(&ctx, 0xc7) != 0 ||
                modrm_sib_disp64(&ctx, AS_X86_REG_RAX, a, &rex_r, &rex_x, &rex_b) != 0 ||
                emit32(&ctx, (uint32_t)b->u.imm) != 0) {
                return -1;
            }
        } else {
            set_unsupported_form_named(&ctx, "mov");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsd")) {
        if (insn->op_count != 2) {
            set_err(&ctx, "unsupported x86_64 movsd form");
            return -1;
        }
        if (operand_is_xmm_reg(a) && (operand_is_xmm_reg(b) || b->kind == AS_X86_OP_MEM)) {
            if (emit8(&ctx, 0xf2) != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x10) != 0 ||
                modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else if (a->kind == AS_X86_OP_MEM && operand_is_xmm_reg(b)) {
            if (emit8(&ctx, 0xf2) != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x11) != 0 ||
                modrm_sib_disp64(&ctx, b->u.reg, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_err(&ctx, "unsupported x86_64 movsd form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cvtsi2sd")) {
        if (insn->op_count != 2 || !operand_is_xmm_reg(a) ||
            (b->kind != AS_X86_OP_REG && b->kind != AS_X86_OP_MEM)) {
            set_err(&ctx, "unsupported x86_64 cvtsi2sd form");
            return -1;
        }
        if (emit8(&ctx, 0xf2) != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x2a) != 0 ||
            modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpss") || (streq_ci(insn->mnemonic, "cmpsd") && insn->op_count == 3)) {
        /* cmpsd with three operands is the SSE2 comparison of doubles,
         * F2 0F C2; with none or two it is the string instruction, to
         * which this was given whatever its operands: `cmpsd $1, %xmm0,
         * %xmm1` came out as the one byte A7. */
        if (insn->op_count != 3 || !operand_is_xmm_reg(a) || c->kind != AS_X86_OP_IMM ||
            (!operand_is_xmm_reg(b) && b->kind != AS_X86_OP_MEM)) {
            set_err(&ctx, "unsupported x86_64 %s form", insn->mnemonic);
            return -1;
        }
        if (emit8(&ctx, streq_ci(insn->mnemonic, "cmpsd") ? 0xf2 : 0xf3) != 0 || emit8(&ctx, 0x0f) != 0 ||
            emit8(&ctx, 0xc2) != 0 ||
            modrm_sib_disp64(&ctx, a->u.reg, b, &rex_r, &rex_x, &rex_b) != 0 ||
            emit8(&ctx, (uint8_t)c->u.imm) != 0) {
            return -1;
        }
    } else if (keylocker_wide_ext(insn->mnemonic) >= 0) {
        if (insn->op_count != 1 || a->kind != AS_X86_OP_MEM) {
            set_unsupported_form(&ctx, insn);
            return -1;
        }
        if (emit8(&ctx, 0xf3) != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x38) != 0 || emit8(&ctx, 0xd8) != 0 ||
            modrm_sib_disp64(&ctx, (as_x86_reg_t)keylocker_wide_ext(insn->mnemonic), a, &rex_r, &rex_x, &rex_b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movs")) {
        int bits;

        if (insn->op_count != 2 ||
            !mem_is_single_base_reg(a, AS_X86_REG_RDI) ||
            !mem_is_single_base_reg(b, AS_X86_REG_RSI)) {
            set_err(&ctx, "unsupported x86_64 movs form");
            return -1;
        }
        bits = resolved_string_op_bits(insn, a, b);
        if (bits == 8) {
            if (emit8(&ctx, 0xa4) != 0) return -1;
        } else if (bits == 16) {
            if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0xa5) != 0) return -1;
        } else if (bits == 32) {
            if (emit8(&ctx, 0xa5) != 0) return -1;
        } else if (bits == 64) {
            rex_w = 1;
            if (emit8(&ctx, 0xa5) != 0) return -1;
        } else {
            set_err(&ctx, "unsupported x86_64 movs form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsb")) {
        if (emit8(&ctx, 0xa4) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsd") || streq_ci(insn->mnemonic, "movsl") || streq_ci(insn->mnemonic, "movsw")) {
        if (emit8(&ctx, 0xa5) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "movsq")) {
        rex_w = 1;
        if (emit8(&ctx, 0xa5) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stosb")) {
        if (emit8(&ctx, 0xaa) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stosd") || streq_ci(insn->mnemonic, "stosl") || streq_ci(insn->mnemonic, "stosw")) {
        if (emit8(&ctx, 0xab) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stosq")) {
        rex_w = 1;
        if (emit8(&ctx, 0xab) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "stos")) {
        if (insn->op_count == 2) {
            int bits;
            const as_x86_operand_t *mem_op = NULL;

            if (!match_reg_mem_pair(a, b, AS_X86_REG_RAX, AS_X86_REG_RDI, NULL, &mem_op)) {
                set_err(&ctx, "unsupported x86_64 stos form");
                return -1;
            }
            bits = mem_operand_bits(mem_op);
            if (bits == 0) {
                bits = default_string_op_bits(insn);
            }
            if (bits == 8) {
                if (emit8(&ctx, 0xaa) != 0) return -1;
            } else if (bits == 16) {
                if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0xab) != 0) return -1;
            } else if (bits == 32) {
                if (emit8(&ctx, 0xab) != 0) return -1;
            } else if (bits == 64) {
                rex_w = 1;
                if (emit8(&ctx, 0xab) != 0) return -1;
            } else {
                set_err(&ctx, "unsupported x86_64 stos form");
                return -1;
            }
        } else if (emit8(&ctx, insn->byte_op ? 0xaa : 0xab) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpsb")) {
        if (emit8(&ctx, 0xa6) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpsd") || streq_ci(insn->mnemonic, "cmpsl") || streq_ci(insn->mnemonic, "cmpsw")) {
        if (emit8(&ctx, 0xa7) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpsq")) {
        rex_w = 1;
        if (emit8(&ctx, 0xa7) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmps")) {
        if (insn->op_count == 2) {
            int bits;

            if (!mem_is_single_base_reg(a, AS_X86_REG_RSI) ||
                !mem_is_single_base_reg(b, AS_X86_REG_RDI)) {
                set_err(&ctx, "unsupported x86_64 cmps form");
                return -1;
            }
            bits = resolved_string_op_bits(insn, a, b);
            if (bits == 8) {
                if (emit8(&ctx, 0xa6) != 0) return -1;
            } else if (bits == 16) {
                if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0xa7) != 0) return -1;
            } else if (bits == 32) {
                if (emit8(&ctx, 0xa7) != 0) return -1;
            } else if (bits == 64) {
                rex_w = 1;
                if (emit8(&ctx, 0xa7) != 0) return -1;
            } else {
                set_err(&ctx, "unsupported x86_64 cmps form");
                return -1;
            }
        } else if (emit8(&ctx, insn->byte_op ? 0xa6 : 0xa7) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lodsb")) {
        if (emit8(&ctx, 0xac) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lodsd") || streq_ci(insn->mnemonic, "lodsl") || streq_ci(insn->mnemonic, "lodsw")) {
        if (emit8(&ctx, 0xad) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lodsq")) {
        rex_w = 1;
        if (emit8(&ctx, 0xad) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lods")) {
        if (insn->op_count == 2) {
            int bits;
            const as_x86_operand_t *mem_op = NULL;

            if (!match_reg_mem_pair(a, b, AS_X86_REG_RAX, AS_X86_REG_RSI, NULL, &mem_op)) {
                set_err(&ctx, "unsupported x86_64 lods form");
                return -1;
            }
            bits = mem_operand_bits(mem_op);
            if (bits == 0) {
                bits = default_string_op_bits(insn);
            }
            if (bits == 8) {
                if (emit8(&ctx, 0xac) != 0) return -1;
            } else if (bits == 16) {
                if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0xad) != 0) return -1;
            } else if (bits == 32) {
                if (emit8(&ctx, 0xad) != 0) return -1;
            } else if (bits == 64) {
                rex_w = 1;
                if (emit8(&ctx, 0xad) != 0) return -1;
            } else {
                set_err(&ctx, "unsupported x86_64 lods form");
                return -1;
            }
        } else if (emit8(&ctx, insn->byte_op ? 0xac : 0xad) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scasb")) {
        if (emit8(&ctx, 0xae) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scasd") || streq_ci(insn->mnemonic, "scasl") || streq_ci(insn->mnemonic, "scasw")) {
        if (emit8(&ctx, 0xaf) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scasq")) {
        rex_w = 1;
        if (emit8(&ctx, 0xaf) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "scas")) {
        if (insn->op_count == 2) {
            int bits;
            const as_x86_operand_t *mem_op = NULL;

            if (!match_reg_mem_pair(a, b, AS_X86_REG_RAX, AS_X86_REG_RDI, NULL, &mem_op)) {
                set_err(&ctx, "unsupported x86_64 scas form");
                return -1;
            }
            bits = mem_operand_bits(mem_op);
            if (bits == 0) {
                bits = default_string_op_bits(insn);
            }
            if (bits == 8) {
                if (emit8(&ctx, 0xae) != 0) return -1;
            } else if (bits == 16) {
                if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0xaf) != 0) return -1;
            } else if (bits == 32) {
                if (emit8(&ctx, 0xaf) != 0) return -1;
            } else if (bits == 64) {
                rex_w = 1;
                if (emit8(&ctx, 0xaf) != 0) return -1;
            } else {
                set_err(&ctx, "unsupported x86_64 scas form");
                return -1;
            }
        } else if (emit8(&ctx, insn->byte_op ? 0xae : 0xaf) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ins")) {
        int bits = insn->byte_op ? 8 : 0;

        if (insn->op_count != 2 ||
            !mem_is_single_base_reg(a, AS_X86_REG_RDI) ||
            !is_dx_port_operand(b)) {
            set_unsupported_form_named(&ctx, "ins");
            return -1;
        }
        if (bits == 0) {
            bits = (int)a->u.mem.size_bits;
        }
        if (bits == 0) {
            /* `insl (%dx), %es:(%rdi)`: the suffix says it. */
            bits = default_string_op_bits(insn);
        }
        if (bits == 8) {
            if (emit8(&ctx, 0x6c) != 0) return -1;
        } else if (bits == 16) {
            if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0x6d) != 0) return -1;
        } else if (bits == 32) {
            if (emit8(&ctx, 0x6d) != 0) return -1;
        } else {
            set_unsupported_form_named(&ctx, "ins");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "insb")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              mem_is_single_base_reg(a, AS_X86_REG_RDI) &&
              is_dx_port_operand(b))) {
            set_unsupported_form_named(&ctx, "insb");
            return -1;
        }
        if (emit8(&ctx, 0x6c) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "insw")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              mem_is_single_base_reg(a, AS_X86_REG_RDI) &&
              is_dx_port_operand(b))) {
            set_unsupported_form_named(&ctx, "insw");
            return -1;
        }
        if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0x6d) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "insd") || streq_ci(insn->mnemonic, "insl")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              mem_is_single_base_reg(a, AS_X86_REG_RDI) &&
              is_dx_port_operand(b))) {
            set_unsupported_form_named(&ctx, "insd");
            return -1;
        }
        if (emit8(&ctx, 0x6d) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outs")) {
        int bits = insn->byte_op ? 8 : 0;

        if (insn->op_count != 2 ||
            !is_dx_port_operand(a) ||
            !mem_is_single_base_reg(b, AS_X86_REG_RSI)) {
            set_unsupported_form_named(&ctx, "outs");
            return -1;
        }
        if (bits == 0) {
            bits = (int)b->u.mem.size_bits;
        }
        if (bits == 0) {
            bits = default_string_op_bits(insn);
        }
        if (bits == 8) {
            if (emit8(&ctx, 0x6e) != 0) return -1;
        } else if (bits == 16) {
            if (string_op_size_prefix(&ctx, insn) != 0 || emit8(&ctx, 0x6f) != 0) return -1;
        } else if (bits == 32) {
            if (emit8(&ctx, 0x6f) != 0) return -1;
        } else {
            set_unsupported_form_named(&ctx, "outs");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outsb")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              is_dx_port_operand(a) &&
              mem_is_single_base_reg(b, AS_X86_REG_RSI))) {
            set_unsupported_form_named(&ctx, "outsb");
            return -1;
        }
        if (emit8(&ctx, 0x6e) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outsw")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              is_dx_port_operand(a) &&
              mem_is_single_base_reg(b, AS_X86_REG_RSI))) {
            set_unsupported_form_named(&ctx, "outsw");
            return -1;
        }
        if (emit8(&ctx, 0x66) != 0 || emit8(&ctx, 0x6f) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "outsd") || streq_ci(insn->mnemonic, "outsl")) {
        if (insn->op_count != 0 &&
            !(insn->op_count == 2 &&
              is_dx_port_operand(a) &&
              mem_is_single_base_reg(b, AS_X86_REG_RSI))) {
            set_unsupported_form_named(&ctx, "outsd");
            return -1;
        }
        if (emit8(&ctx, 0x6f) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "jmp")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REL) {
            if (!insn->force_rel32 && (insn->byte_op || insn->has_section_offset || insn->rel_is_disp)) {
                long long disp;

                if (resolve_rel_target(&ctx, a->u.rel, 2u, &disp) != 0) {
                    return -1;
                }
                if (disp >= -128 && disp <= 127) {
                    if (emit8(&ctx, 0xeb) != 0 || emit8(&ctx, (uint8_t)((signed char)disp)) != 0) {
                        return -1;
                    }
                    return 0;
                }
                if (insn->byte_op) {
                    set_err(&ctx, "x86_64 jump target out of range");
                    return -1;
                }
            }
            {
                long long disp;
                if (resolve_rel_target(&ctx, a->u.rel, 5u, &disp) != 0 ||
                    emit8(&ctx, 0xe9) != 0 || emit32(&ctx, (uint32_t)disp) != 0) {
                    return -1;
                }
            }
        } else if (insn->op_count == 1 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM)) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RSP, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_unsupported_form_named(&ctx, "jmp");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xbegin")) {
        long long disp;

        /* (To a place not yet known, as a call is; it was no
         * instruction of 64-bit code at all.) */
        if (insn->op_count != 1 || a->kind != AS_X86_OP_REL || resolve_rel_target(&ctx, a->u.rel, 6u, &disp) != 0 ||
            emit8(&ctx, 0xc7) != 0 || emit8(&ctx, 0xf8) != 0 || emit32(&ctx, (uint32_t)disp) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "call")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_REL) {
            long long disp;
            if (resolve_rel_target(&ctx, a->u.rel, 5u, &disp) != 0 ||
                emit8(&ctx, 0xe8) != 0 || emit32(&ctx, (uint32_t)disp) != 0) {
                return -1;
            }
        } else if (insn->op_count == 1 && (a->kind == AS_X86_OP_REG || a->kind == AS_X86_OP_MEM)) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RDX, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_unsupported_form_named(&ctx, "call");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "lcall") || streq_ci(insn->mnemonic, "callf")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_MEM) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RBX, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_unsupported_form_named(&ctx, "lcall");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ljmp") || streq_ci(insn->mnemonic, "jmpf")) {
        if (insn->op_count == 1 && a->kind == AS_X86_OP_MEM) {
            if (emit8(&ctx, 0xff) != 0 || modrm_sib_disp64(&ctx, AS_X86_REG_RBP, a, &rex_r, &rex_x, &rex_b) != 0) {
                return -1;
            }
        } else {
            set_unsupported_form_named(&ctx, "ljmp");
            return -1;
        }
    } else if (encode_jcc_rel32(&ctx, insn->mnemonic, (insn->op_count == 1 && a->kind == AS_X86_OP_REL) ? a->u.rel : 0) == 0) {
        if (!(insn->op_count == 1 && a->kind == AS_X86_OP_REL)) {
            set_unsupported_form_named(&ctx, insn->mnemonic);
            return -1;
        }
    } else if (vmx_no_operand_byte(insn->mnemonic) != 0) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x01) != 0 ||
            emit8(&ctx, vmx_no_operand_byte(insn->mnemonic)) != 0) {
            return -1;
        }
    } else {
        /* Not one of the mnemonics above. */
        goto more_mnemonics;
    }
    /*
     * A branch above has encoded the instruction.  Without this, every
     * one of them went on into the chain below, matched nothing there, and
     * was refused by its final else as an unsupported mnemonic.
     */
    goto finish;

more_mnemonics:
    if (streq_ci(insn->mnemonic, "enter")) {
        if (insn->op_count == 2 && a->kind == AS_X86_OP_IMM && b->kind == AS_X86_OP_IMM) {
            /* The frame's size is sixteen bits and the nesting level
             * eight: a number that is neither, signed or unsigned, does
             * not fit. */
            if (a->u.imm < -0x8000 || a->u.imm > 0xffff || b->u.imm < -0x80 || b->u.imm > 0xff) {
                set_err(&ctx, "enter takes a size of sixteen bits and a level of eight");
                return -1;
            }
            if (emit8(&ctx, 0xc8) != 0 || emit8(&ctx, (uint8_t)(a->u.imm & 0xff)) != 0 ||
                emit8(&ctx, (uint8_t)((a->u.imm >> 8) & 0xff)) != 0 || emit8(&ctx, (uint8_t)(b->u.imm & 0xff)) != 0) {
                return -1;
            }
        } else {
            set_unsupported_form_named(&ctx, "enter");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "int")) {
        if (insn->op_count != 1 || a->kind != AS_X86_OP_IMM) {
            return -1;
        }
        /* The third has an instruction of one byte to itself. */
        if (!a->imm_symbolic && a->u.imm == 3) {
            if (emit8(&ctx, 0xcc) != 0) {
                return -1;
            }
        } else if (emit8(&ctx, 0xcd) != 0 || emit8(&ctx, (uint8_t)a->u.imm) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "loop") || streq_ci(insn->mnemonic, "loope") ||
               streq_ci(insn->mnemonic, "loopz") || streq_ci(insn->mnemonic, "loopne") ||
               streq_ci(insn->mnemonic, "loopnz") || streq_ci(insn->mnemonic, "jecxz") ||
               streq_ci(insn->mnemonic, "jrcxz")) {
        uint8_t opcode;
        int32_t rel;
        long long disp;

        if (!(insn->op_count == 1 && a->kind == AS_X86_OP_REL)) {
            set_err(&ctx, "unsupported x86_64 loop form");
            return -1;
        }
        rel = a->u.rel;
        /* (%ecx for the counter is asked for by the address-size
         * prefix: without it `jecxz` was `jrcxz`.) */
        if (resolve_rel_target(&ctx, rel, streq_ci(insn->mnemonic, "jecxz") ? 3u : 2u, &disp) != 0) {
            return -1;
        }
        if (disp < -128 || disp > 127) {
            set_err(&ctx, "x86_64 loop target out of range");
            return -1;
        }
        if (streq_ci(insn->mnemonic, "jecxz") && emit8(&ctx, 0x67) != 0) {
            return -1;
        }
        if (streq_ci(insn->mnemonic, "loop")) opcode = 0xe2;
        else if (streq_ci(insn->mnemonic, "loope") || streq_ci(insn->mnemonic, "loopz")) opcode = 0xe1;
        else if (streq_ci(insn->mnemonic, "loopne") || streq_ci(insn->mnemonic, "loopnz")) opcode = 0xe0;
        else opcode = 0xe3;
        if (emit8(&ctx, opcode) != 0 || emit8(&ctx, (uint8_t)((signed char)disp)) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "xlat") || streq_ci(insn->mnemonic, "xlatb")) {
        /* Bare, or with the table's address written out: the same byte. */
        if (insn->op_count > 1) {
            set_unsupported_form_named(&ctx, insn->mnemonic);
            return -1;
        }
        if (emit8(&ctx, 0xd7) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "emms")) {
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0x77) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "ud2") || streq_ci(insn->mnemonic, "ud2a") ||
               streq_ci(insn->mnemonic, "ud2b")) {
        /* ud2a is ud2 by its old name.  ud2b is another opcode, 0F B9,
         * which is ud1 without the operands it has since been given. */
        if (insn->op_count != 0 || emit8(&ctx, 0x0f) != 0 ||
            emit8(&ctx, streq_ci(insn->mnemonic, "ud2b") ? 0xb9 : 0x0b) != 0) {
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "cmpxchg16b")) {
        rex_w = 1;
        if (insn->op_count != 1 || a->kind != AS_X86_OP_MEM || emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xc7) != 0 ||
            modrm_sib_disp64(&ctx, AS_X86_REG_RCX, a, &rex_r, &rex_x, &rex_b) != 0) {
            set_err(&ctx, "unsupported cmpxchg16b form");
            return -1;
        }
    } else if (streq_ci(insn->mnemonic, "rdfsbase") || streq_ci(insn->mnemonic, "rdgsbase") ||
               streq_ci(insn->mnemonic, "wrfsbase") || streq_ci(insn->mnemonic, "wrgsbase")) {
        uint8_t subop;
        uint8_t rex = 0x40;
        if (insn->op_count != 1 || a == NULL || a->kind != AS_X86_OP_REG) {
            return -1;
        }
        if (streq_ci(insn->mnemonic, "rdfsbase")) subop = 0;
        else if (streq_ci(insn->mnemonic, "rdgsbase")) subop = 1;
        else if (streq_ci(insn->mnemonic, "wrfsbase")) subop = 2;
        else subop = 3;
        if (a->size_bits == 64) {
            rex |= 0x08;
        }
        if (a->u.reg >= 8) {
            rex |= 0x01;
        }
        if (emit8(&ctx, 0xf3) != 0 ||
            ((rex != 0x40 || a->size_bits == 64) && emit8(&ctx, rex) != 0) ||
            emit8(&ctx, 0x0f) != 0 || emit8(&ctx, 0xae) != 0 ||
            emit8(&ctx, (uint8_t)(0xc0 | (subop << 3) | (a->u.reg & 7))) != 0) {
            return -1;
        }
    } else if ((x87_rc = emit_x87_no_operand(&ctx, insn)) != 0) {
        if (x87_rc < 0) {
            return -1;
        }
    } else {
        set_err(&ctx, "unsupported x86_64 mnemonic: %s", insn->mnemonic != NULL ? insn->mnemonic : "<null>");
        return -1;
    }

finish:
    /*
     * The byte registers.  With no REX, codes 4 to 7 are %ah, %ch, %dh
     * and %bh; with one they are %spl, %bpl, %sil and %dil.  So the
     * second four need a REX though it has no bit set -- without it
     * `movb %sil, %al` is `movb %dh, %al` -- and the first four cannot
     * be used in an instruction that has one for any other reason.
     */
    {
        int high8 = 0;
        size_t i;

        for (i = 0; i < insn->op_count; ++i) {
            const as_x86_operand_t *op = &insn->ops[i];

            if (op->kind != AS_X86_OP_REG || op->size_bits != 8) {
                continue;
            }
            if (is_high8_reg(op->u.reg)) {
                high8 = 1;
            } else if (needs_rex_low8(op->u.reg)) {
                force_rex = 1;
            }
        }
        if (insn->rex_wanted && !high8) {
            force_rex = 1;
        }
        if (high8 && (rex_w || rex_r || rex_x || rex_b || force_rex)) {
            set_err(&ctx, "%%ah, %%ch, %%dh and %%bh cannot be used in an instruction that needs REX");
            return -1;
        }
    }
    rex = (uint8_t)(0x40 | (rex_w ? 0x08 : 0) | ((rex_r & 1) ? 0x04 : 0) | ((rex_x & 1) ? 0x02 : 0) |
                    ((rex_b & 1) ? 0x01 : 0));
    if (((rex_r | rex_x | rex_b) & AS_X86_REX_EXT4) != 0) {
        /*
         * REX2, for %r16 to %r31: two bytes where REX is one, with the
         * fifth bits above REX's four and a bit that says the opcode is
         * of the two-byte map -- in place of the 0F that would say so,
         * and with no way to say the maps past it.
         */
        int two_byte;
        size_t i;

        while (rex_pos + 1 < ctx.at &&
               (ctx.out[rex_pos + 1] == 0x66 || ctx.out[rex_pos + 1] == 0xf2 || ctx.out[rex_pos + 1] == 0xf3)) {
            ctx.out[rex_pos] = ctx.out[rex_pos + 1];
            rex_pos++;
        }
        two_byte = rex_pos + 1 < ctx.at && ctx.out[rex_pos + 1] == 0x0f;
        if (two_byte && rex_pos + 2 < ctx.at && (ctx.out[rex_pos + 2] == 0x38 || ctx.out[rex_pos + 2] == 0x3a)) {
            set_err(&ctx, "'%s' has no prefix that says %%r16 to %%r31", insn->mnemonic);
            return -1;
        }
        if (!two_byte) {
            if (ctx.at >= ctx.out_cap) {
                set_err(&ctx, "instruction too long");
                return -1;
            }
            for (i = ctx.at; i > rex_pos + 1; --i) {
                ctx.out[i] = ctx.out[i - 1];
            }
            ctx.at++;
        }
        ctx.out[rex_pos] = 0xd5;
        ctx.out[rex_pos + 1] =
            (uint8_t)((two_byte ? 0x80 : 0) | ((rex_r & AS_X86_REX_EXT4) ? 0x40 : 0) |
                      ((rex_x & AS_X86_REX_EXT4) ? 0x20 : 0) | ((rex_b & AS_X86_REX_EXT4) ? 0x10 : 0) | (rex & 0x0f));
        if (out_len != NULL) {
            *out_len = ctx.at;
        }
        return 0;
    }
    if (rex == 0x40 && !force_rex) {
        size_t i;
        for (i = rex_pos + 1; i < ctx.at; ++i) {
            ctx.out[i - 1] = ctx.out[i];
        }
        ctx.at--;
    } else {
        /*
         * REX is the last prefix: the processor heeds it only when the
         * opcode follows it directly.  The place for it was kept before
         * the branches above ran, and many of them then write 66, F2 or
         * F3 -- so `pxor %xmm8, %xmm8` came out 45 66 0f ef c0, its REX
         * ignored, which is `pxor %xmm0, %xmm0`.  None of those three is
         * ever an opcode, so any that follow the place are prefixes, and
         * it moves down past them.
         */
        while (rex_pos + 1 < ctx.at &&
               (ctx.out[rex_pos + 1] == 0x66 || ctx.out[rex_pos + 1] == 0xf2 ||
                ctx.out[rex_pos + 1] == 0xf3)) {
            ctx.out[rex_pos] = ctx.out[rex_pos + 1];
            rex_pos++;
        }
        ctx.out[rex_pos] = rex;
    }

    if (out_len != NULL) {
        *out_len = ctx.at;
    }
    return 0;
}
