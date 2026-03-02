#include "as_arm_loadstore.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint32_t word;
    int cond_mutable;
} ls_desc_t;

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

static int set_err(char *errbuf, size_t errbuf_sz, const char *fmt, ...) {
    va_list ap;

    if (errbuf == NULL || errbuf_sz == 0) {
        return -1;
    }

    va_start(ap, fmt);
    vsnprintf(errbuf, errbuf_sz, fmt, ap);
    va_end(ap);
    return -1;
}

static void put32le(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)(v & 0xffu);
    out[1] = (uint8_t)((v >> 8) & 0xffu);
    out[2] = (uint8_t)((v >> 16) & 0xffu);
    out[3] = (uint8_t)((v >> 24) & 0xffu);
}

static int try_encode_mode2(const as_arm_loadstore_insn_t *insn, uint32_t *out_word) {
    as_arm_addr_mode2_t m;
    uint32_t mode_bits;
    uint32_t base;

    if (!streq_ci(insn->mnemonic, "ldr") && !streq_ci(insn->mnemonic, "str") &&
        !streq_ci(insn->mnemonic, "ldrb") && !streq_ci(insn->mnemonic, "strb")) {
        return -1;
    }

    memset(&m, 0, sizeof(m));
    m.rn = insn->rn;
    m.add = 1;
    m.byte = streq_ci(insn->mnemonic, "ldrb") || streq_ci(insn->mnemonic, "strb");
    m.pre_indexed = insn->post_indexed ? 0 : (insn->pre_indexed ? 1 : 1);
    m.writeback = insn->post_indexed ? 0 : (insn->writeback ? 1 : 0);
    m.is_reg_offset = insn->reg_offset;

    if (m.is_reg_offset) {
        m.rm = insn->rm;
        m.shift.kind = AS_ARM_SHIFT_LSL;
        m.shift.by_reg = 0;
        m.shift.amount = 0;
    } else {
        if (insn->imm > 0xfffu) {
            return -1;
        }
        m.imm12 = (uint16_t)insn->imm;
    }

    if (as_arm_encode_addr_mode2(&m, &mode_bits) != 0) {
        return -1;
    }

    base = 0x04000000u | (streq_ci(insn->mnemonic, "ldr") || streq_ci(insn->mnemonic, "ldrb") ?
                          (1u << 20) : 0u);
    *out_word = ((uint32_t)(insn->cond & 0xfu) << 28) | base |
                ((uint32_t)(insn->rd & 0xfu) << 12) | mode_bits;
    return 0;
}

static int try_encode_ldr_literal(const as_arm_loadstore_insn_t *insn, uint8_t *out, size_t out_cap,
                                  size_t *out_len, char *errbuf, size_t errbuf_sz) {
    uint32_t ldr;

    if (!insn->emit_literal || !streq_ci(insn->mnemonic, "ldr")) {
        return -1;
    }
    if (out_cap < 8) {
        return set_err(errbuf, errbuf_sz, "encoding overflow");
    }

    ldr = ((uint32_t)(insn->cond & 0xfu) << 28) | 0x059f0000u | ((uint32_t)(insn->rd & 0xfu) << 12);
    put32le(out, ldr);
    put32le(out + 4, insn->literal_value);
    if (out_len != NULL) {
        *out_len = 8;
    }
    return 0;
}

static const ls_desc_t k_desc[] = {
    {"ldrh", 0xe1d210b4u, 1},
    {"strh", 0xe1c210b4u, 1},
    {"ldrsb", 0xe1d210d4u, 1},
    {"ldrsh", 0xe1d210f4u, 1},
    {"ldrd", 0xe1c200d8u, 1},
    {"strd", 0xe1c200f8u, 1},
    {"ldm", 0xe8b1000cu, 1},
    {"stmdb", 0xe921000cu, 1},
    {"push", 0xe92d0030u, 1},
    {"pop", 0xe8bd0030u, 1},
    {"ldrex", 0xe1921f9fu, 1},
    {"strex", 0xe1820f91u, 1},
    {"ldrexb", 0xe1d21f9fu, 1},
    {"strexb", 0xe1c20f91u, 1},
    {"ldrexh", 0xe1f21f9fu, 1},
    {"strexh", 0xe1e20f91u, 1},
    {"ldrexd", 0xe1b20f9fu, 1},
    {"strexd", 0xe1a23f90u, 1},
    {"ldrt", 0xe4b21004u, 1},
    {"strt", 0xe4a21004u, 1},
    {"ldrbt", 0xe4f21004u, 1},
    {"strbt", 0xe4e21004u, 1},
    {"ldrht", 0xe0f210b4u, 1},
    {"strht", 0xe0e210b4u, 1},
    {"pld", 0xf5d1f004u, 0},
    {"pldw", 0xf591f004u, 0},
    {"pli", 0xf4d1f004u, 0},
};

int as_arm_encode_loadstore(const as_arm_loadstore_insn_t *insn, uint8_t *out, size_t out_cap,
                            size_t *out_len, char *errbuf, size_t errbuf_sz) {
    uint32_t word;
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (insn == NULL || insn->mnemonic == NULL || out == NULL) {
        return set_err(errbuf, errbuf_sz, "invalid load/store encode inputs");
    }

    if (try_encode_ldr_literal(insn, out, out_cap, out_len, errbuf, errbuf_sz) == 0) {
        return 0;
    }

    if (try_encode_mode2(insn, &word) == 0) {
        if (out_cap < 4) {
            return set_err(errbuf, errbuf_sz, "encoding overflow");
        }
        put32le(out, word);
        if (out_len != NULL) {
            *out_len = 4;
        }
        return 0;
    }

    for (i = 0; i < sizeof(k_desc) / sizeof(k_desc[0]); ++i) {
        if (streq_ci(insn->mnemonic, k_desc[i].mnemonic)) {
            word = k_desc[i].word;
            if (k_desc[i].cond_mutable) {
                word = (word & 0x0fffffffu) | ((uint32_t)(insn->cond & 0xfu) << 28);
            }
            if (out_cap < 4) {
                return set_err(errbuf, errbuf_sz, "encoding overflow");
            }
            put32le(out, word);
            if (out_len != NULL) {
                *out_len = 4;
            }
            return 0;
        }
    }

    return set_err(errbuf, errbuf_sz, "unsupported ARM load/store mnemonic: %s", insn->mnemonic);
}
