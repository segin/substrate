#include "as_arm_system.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint32_t word;
    int cond_mutable;
} sys_desc_t;

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

static int try_encode_immediate(const as_arm_system_insn_t *insn, uint32_t *out_word) {
    if (streq_ci(insn->mnemonic, "svc") || streq_ci(insn->mnemonic, "swi")) {
        if (!insn->has_imm || insn->imm > 0x00ffffffu) {
            return -1;
        }
        *out_word = ((uint32_t)(insn->cond & 0xfu) << 28) | 0x0f000000u | insn->imm;
        return 0;
    }

    if (streq_ci(insn->mnemonic, "bkpt")) {
        if (!insn->has_imm || insn->imm > 0xffffu) {
            return -1;
        }
        *out_word = 0xe1200070u | ((insn->imm & 0xfff0u) << 4) | (insn->imm & 0xfu);
        return 0;
    }

    if (streq_ci(insn->mnemonic, "hlt")) {
        if (!insn->has_imm || insn->imm > 0xffffu) {
            return -1;
        }
        *out_word = 0xe1000070u | ((insn->imm & 0xfff0u) << 4) | (insn->imm & 0xfu);
        return 0;
    }

    if (streq_ci(insn->mnemonic, "dbg")) {
        if (!insn->has_imm || insn->imm > 0xfu) {
            return -1;
        }
        *out_word = 0xe320f0f0u | (insn->imm & 0xfu);
        return 0;
    }

    return -1;
}

static const sys_desc_t k_desc[] = {
    {"mrs", 0xe10f1000u, 1},
    {"msr", 0xe121f001u, 1},
    {"cpsie", 0xf1080080u, 0},
    {"cpsid", 0xf10c0080u, 0},
    {"setend", 0xf1010200u, 0},
    {"dmb", 0xf57ff05fu, 0},
    {"dsb", 0xf57ff04fu, 0},
    {"isb", 0xf57ff06fu, 0},
    {"wfi", 0xe320f003u, 0},
    {"wfe", 0xe320f002u, 0},
    {"sev", 0xe320f004u, 0},
    {"yield", 0xe320f001u, 0},
    {"nop", 0xe320f000u, 0},
    {"cdp", 0xee021f03u, 1},
    {"cdp2", 0xfe021f03u, 0},
    {"mcr", 0xee021f13u, 1},
    {"mcr2", 0xfe021f13u, 0},
    {"mrc", 0xee121f13u, 1},
    {"mrc2", 0xfe121f13u, 0},
    {"mcrr", 0xec421f03u, 1},
    {"mcrr2", 0xfc421f03u, 0},
    {"mrrc", 0xec521f03u, 1},
    {"mrrc2", 0xfc521f03u, 0},
    {"ldc", 0xecb21f01u, 1},
    {"ldc2", 0xfcb21f01u, 0},
    {"stc", 0xeca21f01u, 1},
    {"stc2", 0xfca21f01u, 0},
    {"clrex", 0xf57ff01fu, 0},
};

int as_arm_encode_system(const as_arm_system_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz) {
    uint32_t word;
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "invalid system encode inputs");
    }

    if (try_encode_immediate(insn, &word) == 0) {
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
            put32le(out, word);
            if (out_len != NULL) {
                *out_len = 4;
            }
            return 0;
        }
    }

    return set_err(errbuf, errbuf_sz, "unsupported ARM system mnemonic: %s", insn->mnemonic);
}
