#ifndef SUBSTRATE_AS_X86_ENCODE_H
#define SUBSTRATE_AS_X86_ENCODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_X86_REG_EAX = 0,
    AS_X86_REG_RAX = 0,
    AS_X86_REG_ECX = 1,
    AS_X86_REG_RCX = 1,
    AS_X86_REG_EDX = 2,
    AS_X86_REG_RDX = 2,
    AS_X86_REG_EBX = 3,
    AS_X86_REG_RBX = 3,
    AS_X86_REG_ESP = 4,
    AS_X86_REG_RSP = 4,
    AS_X86_REG_EBP = 5,
    AS_X86_REG_RBP = 5,
    AS_X86_REG_ESI = 6,
    AS_X86_REG_RSI = 6,
    AS_X86_REG_EDI = 7,
    AS_X86_REG_RDI = 7,
    AS_X86_REG_R8 = 8,
    AS_X86_REG_R9 = 9,
    AS_X86_REG_R10 = 10,
    AS_X86_REG_R11 = 11,
    AS_X86_REG_R12 = 12,
    AS_X86_REG_R13 = 13,
    AS_X86_REG_R14 = 14,
    AS_X86_REG_R15 = 15,
    AS_X86_REG_AH = 16,
    AS_X86_REG_CH = 17,
    AS_X86_REG_DH = 18,
    AS_X86_REG_BH = 19,
} as_x86_reg_t;

typedef enum {
    AS_X86_SEG_NONE = 0,
    AS_X86_SEG_CS,
    AS_X86_SEG_DS,
    AS_X86_SEG_ES,
    AS_X86_SEG_FS,
    AS_X86_SEG_GS,
    AS_X86_SEG_SS,
} as_x86_seg_t;

typedef enum {
    AS_X86_OP_NONE = 0,
    AS_X86_OP_REG,
    AS_X86_OP_IMM,
    AS_X86_OP_MEM,
    AS_X86_OP_REL,
    AS_X86_OP_FPU,
} as_x86_operand_kind_t;

typedef struct {
    int rip_relative;
    int has_base;
    as_x86_reg_t base;
    int has_index;
    as_x86_reg_t index;
    /* The index is a vector register, of this as_x86_reg_class_t: the
     * address of a gather or a scatter.  0 for a general register. */
    unsigned char index_class;
    unsigned addr_bits;
    unsigned scale;
    unsigned size_bits;
    int has_disp;
    int force_disp32;
    int32_t disp;
    int disp_only;
    /*
     * With disp_only, in 32-bit code: the address is ModRM mod=00 rm=101
     * and four bytes.  64-bit code gives that form to %rip-relative
     * addressing and must write an absolute address with a SIB byte.
     */
    int abs_modrm;
} as_x86_mem_t;

typedef struct {
    as_x86_operand_kind_t kind;
    unsigned size_bits;
    /*
     * The immediate is a symbol's value, not known here: imm is 0 and a
     * relocation fills the field.  The field must then be the full
     * width of the operation and never the one-byte form that 0 fits,
     * or the relocation is written over the opcode.
     */
    int imm_symbolic;
    /*
     * The register is %mm0 to %mm7.  They are 64 bits wide and numbered
     * 0 to 7, as %rax to %rdi are, and nothing else told the two apart:
     * `movd %xmm0, %rax` was read as a move into %mm0.
     */
    int is_mmx;
    /*
     * What kind of register it is: an as_x86_reg_class_t (as_x86_regs.h),
     * 0 for an operand that is none.  With size_bits, which is its
     * width, it is what an instruction asks of a register operand; the
     * number in `reg` says which one of its kind, and is the same for
     * %eax, %xmm0, %es and %cr0.
     */
    unsigned char reg_class;
    union {
        as_x86_reg_t reg;
        int64_t imm;
        int32_t rel;
        as_x86_mem_t mem;
        unsigned fpu;
    } u;
} as_x86_operand_t;

typedef struct {
    const char *mnemonic;
    as_x86_seg_t seg_override;
    unsigned default_bits;
    unsigned rel_is_disp;
    unsigned force_rel32;
    unsigned has_section_offset;
    uint64_t section_offset;
    int lock_prefix;
    int rep_prefix;
    /* The rep prefix is xacquire's or xrelease's, and is no repeat. */
    int hle_prefix;
    int explicit_rex;
    /*
     * {load}: between two registers, the encoding whose destination is
     * ModRM.reg, which is not the one written otherwise.
     */
    int prefer_load;
    /*
     * {rex}: a REX prefix though none is needed -- unless a register is
     * %ah to %bh, which cannot be had with one, and then none.
     */
    int rex_wanted;
    int rex_w;
    uint8_t rex_bits;
    int byte_op;
    int operand_size_override;
    int address_size_override;
    as_x86_operand_t ops[3];
    size_t op_count;
} as_x86_insn_t;

int as_x86_encode_i386(const as_x86_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz);
int as_x86_encode_x86_64(const as_x86_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz);

/*
 * One of the encoders of an instruction-set extension, behind a wrapper
 * that takes the whole instruction: it writes from the mandatory prefix
 * on, and knows nothing of the segment, the address size or lock.
 */
typedef int (*as_x86_ext_encode_fn)(const as_x86_insn_t *insn, uint8_t *out, size_t out_cap,
                                    size_t *out_len, char *errbuf, size_t errbuf_sz);

/* The most as_x86_outer_prefixes() writes. */
#define AS_X86_OUTER_PREFIX_MAX 3

/* The prefixes such an encoder leaves out, in GNU's order; their number. */
size_t as_x86_outer_prefixes(const as_x86_insn_t *insn, uint8_t out[AS_X86_OUTER_PREFIX_MAX]);

/* Run one, with those prefixes before what it writes. */
int as_x86_encode_ext(as_x86_ext_encode_fn fn, const as_x86_insn_t *insn, uint8_t *out, size_t out_cap,
                      size_t *out_len, char *errbuf, size_t errbuf_sz);

/*
 * ModRM and the displacement after it for an address of 16-bit
 * registers, which has a table of its own: %bx or %bp with %si or %di,
 * or one of the four alone, and a displacement of one byte or two.
 * `reg_field` is ModRM's middle three bits.  `disp8` is 0 where a
 * displacement of one byte is not that byte (EVEX scales it), and two
 * are written.  The bytes, at most three, go to `out` and their number
 * to `n`; -1 if the address is not one of that table.
 *
 * The encoders of the extensions each write ModRM themselves, and none
 * of them had this table: given `vaddps (%bx,%si), %xmm0, %xmm1` in
 * 32-bit code they wrote a SIB byte of %ebx and %esi.  They all come
 * here for such an address -- and each is a file that is built and
 * tested by itself, which is why this is in the header.
 */
static inline int as_x86_modrm16(const as_x86_mem_t *m, unsigned reg_field, int disp8, uint8_t out[3], size_t *n) {
    unsigned rm;
    unsigned mod;
    int32_t disp;

    if (m == NULL || out == NULL || n == NULL || m->rip_relative || (m->scale != 0 && m->scale != 1)) {
        return -1;
    }
    disp = m->has_disp || m->disp_only ? m->disp : 0;
    if (m->disp_only) {
        /* No register: mod 00 and r/m 110, and two bytes. */
        out[0] = (uint8_t)(((reg_field & 7u) << 3) | 6u);
        out[1] = (uint8_t)(disp & 0xff);
        out[2] = (uint8_t)((disp >> 8) & 0xff);
        *n = 3;
        return 0;
    }
    if (m->has_base && m->has_index && (m->base == AS_X86_REG_RBX || m->base == AS_X86_REG_RBP) &&
        (m->index == AS_X86_REG_RSI || m->index == AS_X86_REG_RDI)) {
        rm = (m->base == AS_X86_REG_RBP ? 2u : 0u) + (m->index == AS_X86_REG_RDI ? 1u : 0u);
    } else if (!m->has_base || m->has_index) {
        return -1;
    } else if (m->base == AS_X86_REG_RSI) {
        rm = 4;
    } else if (m->base == AS_X86_REG_RDI) {
        rm = 5;
    } else if (m->base == AS_X86_REG_RBP) {
        rm = 6;
    } else if (m->base == AS_X86_REG_RBX) {
        rm = 7;
    } else {
        return -1;
    }
    if (m->force_disp32) {
        mod = 2;
    } else if (disp == 0 && rm != 6) {
        mod = 0;
    } else if (disp8 && disp >= -128 && disp <= 127) {
        /* (%bp alone with no displacement would be the address with
         * no register, and is given a displacement of nothing.) */
        mod = 1;
    } else if (disp >= -32768 && disp <= 65535) {
        mod = 2;
    } else {
        return -1;
    }
    out[0] = (uint8_t)((mod << 6) | ((reg_field & 7u) << 3) | rm);
    *n = 1;
    if (mod == 1) {
        out[(*n)++] = (uint8_t)(disp & 0xff);
    } else if (mod == 2) {
        out[(*n)++] = (uint8_t)(disp & 0xff);
        out[(*n)++] = (uint8_t)((disp >> 8) & 0xff);
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif
