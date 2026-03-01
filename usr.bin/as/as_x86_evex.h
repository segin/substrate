#ifndef SUBSTRATE_AS_X86_EVEX_H
#define SUBSTRATE_AS_X86_EVEX_H

#include "as_x86_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_EVEX_MAP_0F = 1,
    AS_EVEX_MAP_0F38 = 2,
    AS_EVEX_MAP_0F3A = 3,
} as_evex_map_t;

typedef enum {
    AS_EVEX_PP_NONE = 0,
    AS_EVEX_PP_66 = 1,
    AS_EVEX_PP_F3 = 2,
    AS_EVEX_PP_F2 = 3,
} as_evex_pp_t;

typedef struct {
    const char *mnemonic;
    uint8_t opcode;
    as_evex_map_t map;
    as_evex_pp_t pp;
    int evex_w;
    as_x86_reg_t dst;
    as_x86_reg_t src1;
    as_x86_operand_t src2;
    uint8_t opmask;
    int zeroing;
    int broadcast;
    int sae;
    int rounding_mode; /* -1 or 0..3 (rn,rd,ru,rz) */
    uint8_t evex_l2;   /* 0..3 for L'L */
} as_x86_evex_insn_t;

int as_x86_encode_evex_3op(const as_x86_evex_insn_t *insn, uint8_t *out, size_t out_cap,
                           size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
