#include "as_arm_encode.h"

#include <string.h>

typedef struct {
    const char *name;
    as_arm_cond_t cond;
} cond_name_t;

static int streq_ci(const char *a, const char *b) {
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }

    for (i = 0; a[i] != '\0' && b[i] != '\0'; ++i) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return 0;
        }
    }

    return a[i] == '\0' && b[i] == '\0';
}

static uint32_t rol32(uint32_t v, unsigned n) {
    n &= 31u;
    return (v << n) | (v >> ((32u - n) & 31u));
}

int as_arm_cond_from_string(const char *name, as_arm_cond_t *out_cond) {
    static const cond_name_t table[] = {
        {"eq", AS_ARM_COND_EQ},
        {"ne", AS_ARM_COND_NE},
        {"cs", AS_ARM_COND_CS},
        {"hs", AS_ARM_COND_CS},
        {"cc", AS_ARM_COND_CC},
        {"lo", AS_ARM_COND_CC},
        {"mi", AS_ARM_COND_MI},
        {"pl", AS_ARM_COND_PL},
        {"vs", AS_ARM_COND_VS},
        {"vc", AS_ARM_COND_VC},
        {"hi", AS_ARM_COND_HI},
        {"ls", AS_ARM_COND_LS},
        {"ge", AS_ARM_COND_GE},
        {"lt", AS_ARM_COND_LT},
        {"gt", AS_ARM_COND_GT},
        {"le", AS_ARM_COND_LE},
        {"al", AS_ARM_COND_AL},
        {"nv", AS_ARM_COND_NV},
    };
    size_t i;

    if (name == NULL || out_cond == NULL) {
        return -1;
    }

    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (streq_ci(name, table[i].name)) {
            *out_cond = table[i].cond;
            return 0;
        }
    }

    return -1;
}

int as_arm_cond_invert(as_arm_cond_t cond, as_arm_cond_t *out_cond) {
    if (out_cond == NULL) {
        return -1;
    }

    switch (cond) {
    case AS_ARM_COND_EQ:
        *out_cond = AS_ARM_COND_NE;
        return 0;
    case AS_ARM_COND_NE:
        *out_cond = AS_ARM_COND_EQ;
        return 0;
    case AS_ARM_COND_CS:
        *out_cond = AS_ARM_COND_CC;
        return 0;
    case AS_ARM_COND_CC:
        *out_cond = AS_ARM_COND_CS;
        return 0;
    case AS_ARM_COND_MI:
        *out_cond = AS_ARM_COND_PL;
        return 0;
    case AS_ARM_COND_PL:
        *out_cond = AS_ARM_COND_MI;
        return 0;
    case AS_ARM_COND_VS:
        *out_cond = AS_ARM_COND_VC;
        return 0;
    case AS_ARM_COND_VC:
        *out_cond = AS_ARM_COND_VS;
        return 0;
    case AS_ARM_COND_HI:
        *out_cond = AS_ARM_COND_LS;
        return 0;
    case AS_ARM_COND_LS:
        *out_cond = AS_ARM_COND_HI;
        return 0;
    case AS_ARM_COND_GE:
        *out_cond = AS_ARM_COND_LT;
        return 0;
    case AS_ARM_COND_LT:
        *out_cond = AS_ARM_COND_GE;
        return 0;
    case AS_ARM_COND_GT:
        *out_cond = AS_ARM_COND_LE;
        return 0;
    case AS_ARM_COND_LE:
        *out_cond = AS_ARM_COND_GT;
        return 0;
    default:
        return -1;
    }
}

int as_arm_split_mnemonic(const char *mnemonic, char *base_out, size_t base_out_sz,
                          as_arm_cond_t *cond_out, int *setflags_out) {
    size_t len;
    size_t end;
    as_arm_cond_t cond = AS_ARM_COND_AL;
    int setflags = 0;
    char suffix[3];
    size_t i;

    if (mnemonic == NULL || base_out == NULL || base_out_sz == 0 || cond_out == NULL ||
        setflags_out == NULL) {
        return -1;
    }

    len = strlen(mnemonic);
    if (len == 0) {
        return -1;
    }

    end = len;
    if (end > 0) {
        char c = mnemonic[end - 1];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        if (c == 's') {
            setflags = 1;
            --end;
        }
    }

    if (end >= 2) {
        suffix[0] = mnemonic[end - 2];
        suffix[1] = mnemonic[end - 1];
        suffix[2] = '\0';
        if (as_arm_cond_from_string(suffix, &cond) == 0) {
            end -= 2;
        }
    }

    if (end == 0 || end + 1 > base_out_sz) {
        return -1;
    }

    for (i = 0; i < end; ++i) {
        char c = mnemonic[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        base_out[i] = c;
    }
    base_out[end] = '\0';

    *cond_out = cond;
    *setflags_out = setflags;
    return 0;
}

int as_arm_pack_cond_class(as_arm_cond_t cond, uint8_t class_bits, uint32_t payload,
                           uint32_t *out_word) {
    if (out_word == NULL || class_bits > 7 || payload > 0x1ffffffu) {
        return -1;
    }

    *out_word = (((uint32_t)cond & 0xfu) << 28) | (((uint32_t)class_bits & 0x7u) << 25) |
                (payload & 0x1ffffffu);
    return 0;
}

int as_arm_encode_operand2_reg(uint8_t rm, const as_arm_shift_spec_t *shift, uint32_t *out_bits) {
    uint32_t bits;
    uint32_t type;
    uint32_t amount;

    if (out_bits == NULL || rm > 15) {
        return -1;
    }

    if (shift == NULL || shift->kind == AS_ARM_SHIFT_LSL) {
        if (shift == NULL || (!shift->by_reg && shift->amount == 0)) {
            *out_bits = rm;
            return 0;
        }
    }

    switch (shift->kind) {
    case AS_ARM_SHIFT_LSL:
        type = 0;
        break;
    case AS_ARM_SHIFT_LSR:
        type = 1;
        break;
    case AS_ARM_SHIFT_ASR:
        type = 2;
        break;
    case AS_ARM_SHIFT_ROR:
        type = 3;
        break;
    case AS_ARM_SHIFT_RRX:
        if (shift->by_reg) {
            return -1;
        }
        *out_bits = (3u << 5) | (uint32_t)rm;
        return 0;
    default:
        return -1;
    }

    if (shift->by_reg) {
        amount = shift->amount;
        if (amount > 15) {
            return -1;
        }
        bits = (amount << 8) | (type << 5) | (1u << 4) | (uint32_t)rm;
    } else {
        amount = shift->amount;
        if (amount > 31) {
            return -1;
        }
        bits = (amount << 7) | (type << 5) | (uint32_t)rm;
    }

    *out_bits = bits;
    return 0;
}

int as_arm_encode_operand2_imm(uint32_t imm32, uint32_t *out_bits) {
    unsigned rot;

    if (out_bits == NULL) {
        return -1;
    }

    for (rot = 0; rot < 16; ++rot) {
        uint32_t candidate = rol32(imm32, rot * 2u);
        if ((candidate & ~0xffu) == 0) {
            *out_bits = ((uint32_t)rot << 8) | candidate;
            return 0;
        }
    }

    return -1;
}

int as_arm_encode_addr_mode2(const as_arm_addr_mode2_t *mode, uint32_t *out_bits) {
    uint32_t bits;
    uint32_t off;

    if (mode == NULL || out_bits == NULL || mode->rn > 15) {
        return -1;
    }

    bits = 0;
    bits |= (mode->pre_indexed ? 1u : 0u) << 24;
    bits |= (mode->add ? 1u : 0u) << 23;
    bits |= (mode->byte ? 1u : 0u) << 22;
    bits |= (mode->writeback ? 1u : 0u) << 21;
    bits |= ((uint32_t)(mode->rn & 0xfu)) << 16;

    if (!mode->is_reg_offset) {
        if (mode->imm12 > 0x0fffu) {
            return -1;
        }
        bits |= (uint32_t)mode->imm12;
        *out_bits = bits;
        return 0;
    }

    if (as_arm_encode_operand2_reg(mode->rm, &mode->shift, &off) != 0) {
        return -1;
    }

    bits |= 1u << 25;
    bits |= off & 0xfffu;
    *out_bits = bits;
    return 0;
}

int as_arm_ldm_mode_to_pu(as_arm_ldm_mode_t mode, uint8_t *out_p, uint8_t *out_u) {
    uint8_t p;
    uint8_t u;

    if (out_p == NULL || out_u == NULL) {
        return -1;
    }

    switch (mode) {
    case AS_ARM_LDM_IA:
    case AS_ARM_LDM_EA:
        p = 0;
        u = 1;
        break;
    case AS_ARM_LDM_IB:
    case AS_ARM_LDM_FA:
        p = 1;
        u = 1;
        break;
    case AS_ARM_LDM_DA:
    case AS_ARM_LDM_ED:
        p = 0;
        u = 0;
        break;
    case AS_ARM_LDM_DB:
    case AS_ARM_LDM_FD:
        p = 1;
        u = 0;
        break;
    default:
        return -1;
    }

    *out_p = p;
    *out_u = u;
    return 0;
}

void as_arm_state_init(as_arm_state_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->mode = AS_ARM_MODE_ARM;
}

int as_arm_apply_directive(as_arm_state_ctx_t *ctx, const char *directive) {
    if (ctx == NULL || directive == NULL) {
        return -1;
    }

    if (streq_ci(directive, ".arm")) {
        ctx->mode = AS_ARM_MODE_ARM;
        ctx->thumb_func = 0;
        ctx->it_active = 0;
        ctx->it_length = 0;
        ctx->it_index = 0;
        ctx->it_pattern = 0;
        return 0;
    }
    if (streq_ci(directive, ".thumb")) {
        ctx->mode = AS_ARM_MODE_THUMB;
        return 0;
    }
    if (streq_ci(directive, ".thumb_func")) {
        ctx->mode = AS_ARM_MODE_THUMB;
        ctx->thumb_func = 1;
        return 0;
    }

    return -1;
}

int as_arm_encode_bx(as_arm_cond_t cond, uint8_t rm, uint32_t *out_word) {
    if (out_word == NULL || rm > 15) {
        return -1;
    }

    *out_word = (((uint32_t)cond & 0xfu) << 28) | 0x012fff10u | (uint32_t)rm;
    return 0;
}

int as_arm_encode_blx(as_arm_cond_t cond, uint8_t rm, uint32_t *out_word) {
    if (out_word == NULL || rm > 15) {
        return -1;
    }

    *out_word = (((uint32_t)cond & 0xfu) << 28) | 0x012fff30u | (uint32_t)rm;
    return 0;
}

int as_arm_it_start(as_arm_state_ctx_t *ctx, as_arm_cond_t cond, const char *pattern) {
    size_t i;
    size_t len;
    uint8_t p = 1u;

    if (ctx == NULL || pattern == NULL || ctx->mode != AS_ARM_MODE_THUMB ||
        cond == AS_ARM_COND_AL || cond == AS_ARM_COND_NV) {
        return -1;
    }

    len = strlen(pattern);
    if (len > 3) {
        return -1;
    }

    for (i = 0; i < len; ++i) {
        char c = pattern[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        if (c != 't' && c != 'e') {
            return -1;
        }
        if (c == 't') {
            p |= (uint8_t)(1u << (i + 1u));
        }
    }

    ctx->it_active = 1;
    ctx->it_cond = cond;
    ctx->it_pattern = p;
    ctx->it_length = (uint8_t)(1u + len);
    ctx->it_index = 0;
    return 0;
}

int as_arm_it_next_cond(as_arm_state_ctx_t *ctx, as_arm_cond_t *out_cond) {
    as_arm_cond_t inv;
    uint8_t bit;

    if (ctx == NULL || out_cond == NULL || !ctx->it_active || ctx->it_index >= ctx->it_length) {
        return -1;
    }

    bit = (uint8_t)((ctx->it_pattern >> ctx->it_index) & 1u);
    if (bit) {
        *out_cond = ctx->it_cond;
    } else {
        if (as_arm_cond_invert(ctx->it_cond, &inv) != 0) {
            return -1;
        }
        *out_cond = inv;
    }

    ctx->it_index++;
    if (ctx->it_index >= ctx->it_length) {
        ctx->it_active = 0;
    }

    return 0;
}
