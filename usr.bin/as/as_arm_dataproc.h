#ifndef SUBSTRATE_AS_ARM_DATAPROC_H
#define SUBSTRATE_AS_ARM_DATAPROC_H

#include "as_arm_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_ARM_DP_SRC_CANONICAL = 0,
    AS_ARM_DP_SRC_REG,
    AS_ARM_DP_SRC_IMM,
    AS_ARM_DP_SRC_SHIFTED,
} as_arm_dp_src_kind_t;

typedef struct {
    const char *mnemonic;
    as_arm_cond_t cond;
    int setflags;
    as_arm_dp_src_kind_t src_kind;
    uint8_t rd;
    uint8_t rn;
    uint8_t rm;
    uint32_t imm;
    as_arm_shift_spec_t shift;
} as_arm_dataproc_insn_t;

int as_arm_encode_dataproc(const as_arm_dataproc_insn_t *insn, uint8_t *out, size_t out_cap,
                           size_t *out_len, int *out_thumb32, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
