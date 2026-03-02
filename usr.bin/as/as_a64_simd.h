#ifndef SUBSTRATE_AS_A64_SIMD_H
#define SUBSTRATE_AS_A64_SIMD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *mnemonic;
} as_a64_simd_insn_t;

int as_a64_encode_simd(const as_a64_simd_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
