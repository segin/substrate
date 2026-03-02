#ifndef SUBSTRATE_AS_ARM_SYSTEM_H
#define SUBSTRATE_AS_ARM_SYSTEM_H

#include "as_arm_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *mnemonic;
    as_arm_cond_t cond;
    uint32_t imm;
    int has_imm;
} as_arm_system_insn_t;

int as_arm_encode_system(const as_arm_system_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
