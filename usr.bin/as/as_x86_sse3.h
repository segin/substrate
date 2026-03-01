#ifndef SUBSTRATE_AS_X86_SSE3_H
#define SUBSTRATE_AS_X86_SSE3_H

#include "as_x86_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *mnemonic;
    int width_bits; /* used by fisttp: 16/32/64 */
    as_x86_operand_t dst;
    as_x86_operand_t src;
    size_t op_count;
} as_x86_sse3_insn_t;

int as_x86_encode_sse3(const as_x86_sse3_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
