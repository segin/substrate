#ifndef SUBSTRATE_AS_A64_BRANCH_H
#define SUBSTRATE_AS_A64_BRANCH_H

#include "as_a64_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *mnemonic;
    as_a64_cond_t cond;
    int64_t imm;    /* byte displacement */
    uint8_t rn;     /* BR/BLR/RET */
    uint8_t rt;     /* CBZ/CBNZ/TBZ/TBNZ */
    uint8_t bit;    /* TBZ/TBNZ bit index */
    uint16_t imm16; /* SVC/HVC/SMC/BRK/HLT immediate */
    int is64;       /* CBZ/CBNZ and TBZ/TBNZ register class */
} as_a64_branch_insn_t;

int as_a64_encode_branch(const as_a64_branch_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
