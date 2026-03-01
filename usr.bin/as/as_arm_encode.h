#ifndef SUBSTRATE_AS_ARM_ENCODE_H
#define SUBSTRATE_AS_ARM_ENCODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_ARM_MODE_ARM = 0,
    AS_ARM_MODE_THUMB,
} as_arm_mode_t;

typedef enum {
    AS_ARM_THUMB_WIDTH_AUTO = 0,
    AS_ARM_THUMB_WIDTH_NARROW,
    AS_ARM_THUMB_WIDTH_WIDE,
} as_arm_thumb_width_t;

typedef enum {
    AS_ARM_COND_EQ = 0x0,
    AS_ARM_COND_NE = 0x1,
    AS_ARM_COND_CS = 0x2,
    AS_ARM_COND_CC = 0x3,
    AS_ARM_COND_MI = 0x4,
    AS_ARM_COND_PL = 0x5,
    AS_ARM_COND_VS = 0x6,
    AS_ARM_COND_VC = 0x7,
    AS_ARM_COND_HI = 0x8,
    AS_ARM_COND_LS = 0x9,
    AS_ARM_COND_GE = 0xa,
    AS_ARM_COND_LT = 0xb,
    AS_ARM_COND_GT = 0xc,
    AS_ARM_COND_LE = 0xd,
    AS_ARM_COND_AL = 0xe,
    AS_ARM_COND_NV = 0xf,
} as_arm_cond_t;

typedef enum {
    AS_ARM_SHIFT_LSL = 0,
    AS_ARM_SHIFT_LSR,
    AS_ARM_SHIFT_ASR,
    AS_ARM_SHIFT_ROR,
    AS_ARM_SHIFT_RRX,
} as_arm_shift_t;

typedef struct {
    as_arm_shift_t kind;
    int by_reg;
    uint8_t amount;
} as_arm_shift_spec_t;

typedef struct {
    uint8_t rn;
    int pre_indexed;
    int add;
    int writeback;
    int byte;
    int is_reg_offset;
    uint16_t imm12;
    uint8_t rm;
    as_arm_shift_spec_t shift;
} as_arm_addr_mode2_t;

typedef enum {
    AS_ARM_LDM_IA = 0,
    AS_ARM_LDM_IB,
    AS_ARM_LDM_DA,
    AS_ARM_LDM_DB,
    AS_ARM_LDM_FD,
    AS_ARM_LDM_FA,
    AS_ARM_LDM_ED,
    AS_ARM_LDM_EA,
} as_arm_ldm_mode_t;

typedef struct {
    as_arm_mode_t mode;
    int unified_syntax;
    int thumb_func;
    int it_active;
    as_arm_cond_t it_cond;
    uint8_t it_pattern;
    uint8_t it_length;
    uint8_t it_index;
} as_arm_state_ctx_t;

int as_arm_cond_from_string(const char *name, as_arm_cond_t *out_cond);
int as_arm_cond_invert(as_arm_cond_t cond, as_arm_cond_t *out_cond);

int as_arm_split_mnemonic(const char *mnemonic, char *base_out, size_t base_out_sz,
                          as_arm_cond_t *cond_out, int *setflags_out);

int as_arm_pack_cond_class(as_arm_cond_t cond, uint8_t class_bits, uint32_t payload,
                           uint32_t *out_word);

int as_arm_encode_operand2_reg(uint8_t rm, const as_arm_shift_spec_t *shift, uint32_t *out_bits);
int as_arm_encode_operand2_imm(uint32_t imm32, uint32_t *out_bits);

int as_arm_thumb_parse_unified(const char *mnemonic, char *base_out, size_t base_out_sz,
                               as_arm_thumb_width_t *out_width);
int as_arm_thumb_encode_add_imm(uint8_t rd, uint8_t rn, uint16_t imm12, as_arm_thumb_width_t width,
                                uint32_t *out_word, int *out_is_wide);

int as_arm_encode_addr_mode2(const as_arm_addr_mode2_t *mode, uint32_t *out_bits);
int as_arm_ldm_mode_to_pu(as_arm_ldm_mode_t mode, uint8_t *out_p, uint8_t *out_u);

void as_arm_state_init(as_arm_state_ctx_t *ctx);
int as_arm_apply_directive(as_arm_state_ctx_t *ctx, const char *directive);

int as_arm_encode_bx(as_arm_cond_t cond, uint8_t rm, uint32_t *out_word);
int as_arm_encode_blx(as_arm_cond_t cond, uint8_t rm, uint32_t *out_word);

int as_arm_it_start(as_arm_state_ctx_t *ctx, as_arm_cond_t cond, const char *pattern);
int as_arm_it_next_cond(as_arm_state_ctx_t *ctx, as_arm_cond_t *out_cond);

#ifdef __cplusplus
}
#endif

#endif
