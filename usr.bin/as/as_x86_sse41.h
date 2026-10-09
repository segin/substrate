#ifndef SUBSTRATE_AS_X86_SSE41_H
#define SUBSTRATE_AS_X86_SSE41_H

#include "as_x86_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *mnemonic;
    as_x86_operand_t dst;
    as_x86_operand_t src;
    int has_imm8;
    uint8_t imm8;
    size_t op_count;
} as_x86_sse41_insn_t;

int as_x86_encode_sse41(const as_x86_sse41_insn_t *insn, uint8_t *out, size_t out_cap,
                        size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
