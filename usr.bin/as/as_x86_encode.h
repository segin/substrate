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
    unsigned scale;
    unsigned size_bits;
    int has_disp;
    int32_t disp;
    int disp_only;
} as_x86_mem_t;

typedef struct {
    as_x86_operand_kind_t kind;
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
    int lock_prefix;
    int rep_prefix;
    int explicit_rex;
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

#ifdef __cplusplus
}
#endif

#endif
