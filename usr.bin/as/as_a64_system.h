#ifndef SUBSTRATE_AS_A64_SYSTEM_H
#define SUBSTRATE_AS_A64_SYSTEM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *mnemonic;
} as_a64_system_insn_t;

int as_a64_encode_system(const as_a64_system_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
