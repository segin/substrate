#ifndef SUBSTRATE_AS_X86_AVX512DQ_H
#define SUBSTRATE_AS_X86_AVX512DQ_H

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
    uint8_t opmask;
    int zeroing;
    int broadcast;
    int sae;
    int rounding_mode; /* -1 or 0..3 (rn,rd,ru,rz) */
    int has_imm8;
    uint8_t imm8;
    unsigned vector_bits;
    size_t op_count;
} as_x86_avx512dq_insn_t;

int as_x86_encode_avx512dq(const as_x86_avx512dq_insn_t *insn, uint8_t *out, size_t out_cap,
                           size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
