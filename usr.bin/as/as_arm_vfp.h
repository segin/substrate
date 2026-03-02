#ifndef SUBSTRATE_AS_ARM_VFP_H
#define SUBSTRATE_AS_ARM_VFP_H

#include "as_arm_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_ARM_VFP_KIND_CANONICAL = 0,
    AS_ARM_VFP_KIND_VMRS_FPSCR,
    AS_ARM_VFP_KIND_VMSR_FPSCR,
} as_arm_vfp_kind_t;

typedef struct {
    const char *mnemonic;
    as_arm_cond_t cond;
    as_arm_vfp_kind_t kind;
    uint8_t rt;
} as_arm_vfp_insn_t;

int as_arm_encode_vfp(const as_arm_vfp_insn_t *insn, uint8_t *out, size_t out_cap,
                      size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
