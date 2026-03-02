#ifndef SUBSTRATE_AS_ARM_LOADSTORE_H
#define SUBSTRATE_AS_ARM_LOADSTORE_H

#include "as_arm_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *mnemonic;
    as_arm_cond_t cond;
    uint8_t rd;
    uint8_t rn;
    uint8_t rm;
    uint32_t imm;
    int pre_indexed;
    int post_indexed;
    int writeback;
    int reg_offset;
    uint32_t literal_value;
    int emit_literal;
} as_arm_loadstore_insn_t;

int as_arm_encode_loadstore(const as_arm_loadstore_insn_t *insn, uint8_t *out, size_t out_cap,
                            size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
