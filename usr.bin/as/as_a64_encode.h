#ifndef SUBSTRATE_AS_A64_ENCODE_H
#define SUBSTRATE_AS_A64_ENCODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_A64_COND_EQ = 0x0,
    AS_A64_COND_NE = 0x1,
    AS_A64_COND_CS = 0x2,
    AS_A64_COND_CC = 0x3,
    AS_A64_COND_MI = 0x4,
    AS_A64_COND_PL = 0x5,
    AS_A64_COND_VS = 0x6,
    AS_A64_COND_VC = 0x7,
    AS_A64_COND_HI = 0x8,
    AS_A64_COND_LS = 0x9,
    AS_A64_COND_GE = 0xa,
    AS_A64_COND_LT = 0xb,
    AS_A64_COND_GT = 0xc,
    AS_A64_COND_LE = 0xd,
    AS_A64_COND_AL = 0xe,
    AS_A64_COND_NV = 0xf,
} as_a64_cond_t;

typedef enum {
    AS_A64_REGCLASS_X = 0,
    AS_A64_REGCLASS_W,
    AS_A64_REGCLASS_SP,
    AS_A64_REGCLASS_WSP,
    AS_A64_REGCLASS_XZR,
    AS_A64_REGCLASS_WZR,
} as_a64_regclass_t;

typedef struct {
    as_a64_regclass_t regclass;
    uint8_t index;
} as_a64_reg_t;

int as_a64_cond_from_string(const char *name, as_a64_cond_t *out_cond);
int as_a64_parse_reg(const char *name, as_a64_reg_t *out_reg);

int as_a64_encode_logical_imm(uint64_t imm, unsigned reg_bits, uint8_t *out_n,
                              uint8_t *out_immr, uint8_t *out_imms);

int as_a64_encode_movwide_shift(unsigned shift, int is64, uint8_t *out_hw);

int as_a64_encode_adr_imm(int64_t byte_delta, uint8_t *out_immlo, uint32_t *out_immhi);
int as_a64_encode_adrp_imm(int64_t byte_delta, uint8_t *out_immlo, uint32_t *out_immhi);

void as_a64_put32le(uint8_t *out, uint32_t v);

#ifdef __cplusplus
}
#endif

#endif
