#ifndef SUBSTRATE_AS_X86_V2_H
#define SUBSTRATE_AS_X86_V2_H

#include "as_x86_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width_bits; /* 16, 32, or 64 */
    as_x86_operand_t dst;
    as_x86_operand_t src;
} as_x86_popcnt_insn_t;

int as_x86_encode_lahf(uint8_t *out, size_t out_cap, size_t *out_len);
int as_x86_encode_sahf(uint8_t *out, size_t out_cap, size_t *out_len);
int as_x86_encode_cmpxchg16b(const as_x86_operand_t *mem, uint8_t *out, size_t out_cap,
                             size_t *out_len, char *errbuf, size_t errbuf_sz);
int as_x86_encode_popcnt(const as_x86_popcnt_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
