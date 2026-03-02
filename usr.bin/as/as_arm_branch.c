#include "as_arm_branch.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

static void put16le(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)(v & 0xffu);
    out[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void put32le(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)(v & 0xffu);
    out[1] = (uint8_t)((v >> 8) & 0xffu);
    out[2] = (uint8_t)((v >> 16) & 0xffu);
    out[3] = (uint8_t)((v >> 24) & 0xffu);
}

static int encode_a32_b_bl(const as_arm_branch_insn_t *insn, uint8_t *out, size_t out_cap,
                           size_t *out_len, int *out_thumb, char *errbuf, size_t errbuf_sz) {
    uint32_t op;
    int32_t imm24;
    uint32_t word;

    if (!streq_ci(insn->mnemonic, "b") && !streq_ci(insn->mnemonic, "bl")) {
        return -1;
    }

    if ((insn->imm & 3) != 0) {
        return set_err(errbuf, errbuf_sz, "branch displacement must be 4-byte aligned");
    }

    imm24 = insn->imm >> 2;
    if (imm24 < -(1 << 23) || imm24 > ((1 << 23) - 1)) {
        return set_err(errbuf, errbuf_sz, "branch displacement out of range");
    }

    op = streq_ci(insn->mnemonic, "bl") ? 0x0b000000u : 0x0a000000u;
    word = ((uint32_t)(insn->cond & 0xfu) << 28) | op | ((uint32_t)imm24 & 0x00ffffffu);

    if (out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "encoding overflow");
    }

    put32le(out, word);
    if (out_len != NULL) {
        *out_len = 4;
    }
    if (out_thumb != NULL) {
        *out_thumb = 0;
    }
    return 0;
}

static int encode_a32_bx_blx(const as_arm_branch_insn_t *insn, uint8_t *out, size_t out_cap,
                             size_t *out_len, int *out_thumb, char *errbuf, size_t errbuf_sz) {
    uint32_t word;

    if (!streq_ci(insn->mnemonic, "bx") && !streq_ci(insn->mnemonic, "blx")) {
        return -1;
    }
    if (insn->rm > 15) {
        return set_err(errbuf, errbuf_sz, "invalid register");
    }

    if (streq_ci(insn->mnemonic, "bx")) {
        if (as_arm_encode_bx(insn->cond, insn->rm, &word) != 0) {
            return -1;
        }
    } else {
        if (as_arm_encode_blx(insn->cond, insn->rm, &word) != 0) {
            return -1;
        }
    }

    if (out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "encoding overflow");
    }

    put32le(out, word);
    if (out_len != NULL) {
        *out_len = 4;
    }
    if (out_thumb != NULL) {
        *out_thumb = 0;
    }
    return 0;
}

static int encode_t16_cbz(const as_arm_branch_insn_t *insn, uint8_t *out, size_t out_cap,
                          size_t *out_len, int *out_thumb, char *errbuf, size_t errbuf_sz) {
    uint16_t op;
    uint16_t h;
    uint8_t imm6;

    if (!streq_ci(insn->mnemonic, "cbz") && !streq_ci(insn->mnemonic, "cbnz")) {
        return -1;
    }
    if (insn->rn > 7) {
        return set_err(errbuf, errbuf_sz, "cbz/cbnz require low register");
    }
    if ((insn->imm & 1) != 0 || insn->imm < 0 || insn->imm > 126) {
        return set_err(errbuf, errbuf_sz, "cbz/cbnz immediate out of range");
    }

    imm6 = (uint8_t)(insn->imm >> 1);
    op = streq_ci(insn->mnemonic, "cbnz") ? 1u : 0u;
    h = (uint16_t)(0xb100u | (op << 11) | (((imm6 >> 5) & 1u) << 9) |
                   ((uint16_t)(imm6 & 0x1fu) << 3) | (insn->rn & 7u));

    if (out_cap < 2) {
        return set_err(errbuf, errbuf_sz, "encoding overflow");
    }

    put16le(out, h);
    if (out_len != NULL) {
        *out_len = 2;
    }
    if (out_thumb != NULL) {
        *out_thumb = 1;
    }
    return 0;
}

static int encode_t32_tbb_tbh(const as_arm_branch_insn_t *insn, uint8_t *out, size_t out_cap,
                              size_t *out_len, int *out_thumb, char *errbuf, size_t errbuf_sz) {
    uint32_t word;

    if (!streq_ci(insn->mnemonic, "tbb") && !streq_ci(insn->mnemonic, "tbh")) {
        return -1;
    }
    if (insn->rn > 15 || insn->rm > 15) {
        return set_err(errbuf, errbuf_sz, "invalid register");
    }

    word = 0xf000e8d0u | (uint32_t)(insn->rn & 0xfu) | ((uint32_t)(insn->rm & 0xfu) << 16);
    if (streq_ci(insn->mnemonic, "tbh")) {
        word |= 1u << 20;
    }

    if (out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "encoding overflow");
    }

    put32le(out, word);
    if (out_len != NULL) {
        *out_len = 4;
    }
    if (out_thumb != NULL) {
        *out_thumb = 1;
    }
    return 0;
}

int as_arm_encode_branch(const as_arm_branch_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, int *out_thumb, char *errbuf, size_t errbuf_sz) {
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (out_thumb != NULL) {
        *out_thumb = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (insn == NULL || insn->mnemonic == NULL || out == NULL) {
        return set_err(errbuf, errbuf_sz, "invalid branch encode inputs");
    }

    if (encode_a32_b_bl(insn, out, out_cap, out_len, out_thumb, errbuf, errbuf_sz) == 0 ||
        encode_a32_bx_blx(insn, out, out_cap, out_len, out_thumb, errbuf, errbuf_sz) == 0 ||
        encode_t16_cbz(insn, out, out_cap, out_len, out_thumb, errbuf, errbuf_sz) == 0 ||
        encode_t32_tbb_tbh(insn, out, out_cap, out_len, out_thumb, errbuf, errbuf_sz) == 0) {
        return 0;
    }

    return set_err(errbuf, errbuf_sz, "unsupported ARM branch mnemonic: %s", insn->mnemonic);
}
