#ifndef SUBSTRATE_AS_X86_BMI1_H
#define SUBSTRATE_AS_X86_BMI1_H

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
    unsigned width_bits;
    size_t op_count;
} as_x86_bmi1_insn_t;

int as_x86_encode_bmi1(const as_x86_bmi1_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
