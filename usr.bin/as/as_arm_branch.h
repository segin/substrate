#ifndef SUBSTRATE_AS_ARM_BRANCH_H
#define SUBSTRATE_AS_ARM_BRANCH_H

#include "as_arm_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *mnemonic;
    as_arm_cond_t cond;
    int32_t imm; /* byte displacement */
    uint8_t rn;
    uint8_t rm;
} as_arm_branch_insn_t;

int as_arm_encode_branch(const as_arm_branch_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, int *out_thumb, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
