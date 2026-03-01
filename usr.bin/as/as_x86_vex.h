#ifndef SUBSTRATE_AS_X86_VEX_H
#define SUBSTRATE_AS_X86_VEX_H

#include "as_x86_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_VEX_MAP_0F = 1,
    AS_VEX_MAP_0F38 = 2,
    AS_VEX_MAP_0F3A = 3,
} as_vex_map_t;

typedef enum {
    AS_VEX_PP_NONE = 0,
    AS_VEX_PP_66 = 1,
    AS_VEX_PP_F3 = 2,
    AS_VEX_PP_F2 = 3,
} as_vex_pp_t;

typedef struct {
    const char *mnemonic;
    uint8_t opcode;
    as_vex_map_t map;
    as_vex_pp_t pp;
    int vex_w;
    int vex_l;
    as_x86_reg_t dst;
    as_x86_reg_t src1;
    as_x86_operand_t src2;
} as_x86_vex_insn_t;

int as_x86_encode_vex_3op(const as_x86_vex_insn_t *insn, uint8_t *out, size_t out_cap,
                          size_t *out_len, char *errbuf, size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
