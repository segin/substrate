#ifndef SUBSTRATE_AS_ARM_NEON_H
#define SUBSTRATE_AS_ARM_NEON_H

#include "as_arm_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *mnemonic;
    as_arm_cond_t cond;
} as_arm_neon_insn_t;

int as_arm_encode_neon(const as_arm_neon_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
