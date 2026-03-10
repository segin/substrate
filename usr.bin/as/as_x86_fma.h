#ifndef SUBSTRATE_AS_X86_FMA_H
#define SUBSTRATE_AS_X86_FMA_H

#include "as_x86_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *mnemonic;
    as_x86_operand_t op1;
    as_x86_operand_t op2;
    as_x86_operand_t op3;
    int has_imm_reg;
    as_x86_reg_t imm_reg;
    int vex_w;
    unsigned vector_bits;
    size_t op_count;
} as_x86_fma_insn_t;

int as_x86_encode_fma(const as_x86_fma_insn_t *insn, uint8_t *out, size_t out_cap,
                      size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
