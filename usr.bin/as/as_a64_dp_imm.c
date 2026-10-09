#include "as_a64_dp_imm.h"

#include "as_a64_encode.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint32_t word;
} a64_dp_imm_desc_t;

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

static const a64_dp_imm_desc_t k_desc[] = {
    {"add", 0x91048c20u},
    {"add.lsl12", 0x916af062u},
    {"adds", 0xb1001ca4u},
    {"sub", 0xd10c84e6u},
    {"subs", 0xf1400528u},

    {"and", 0x92089d6au},
    {"orr", 0xb2009dacu},
    {"eor", 0xd24015eeu},
    {"ands", 0xf2402e30u},

    {"movn", 0x92824688u},
    {"movz", 0xd2b579a9u},
    {"movk", 0xf2caaab4u},

    {"adr", 0x10000800u},
    {"adrp", 0xb0091a21u},

    {"bfm", 0xb3455062u},
    {"sbfm", 0x934c50a4u},
    {"ubfm", 0xd34450e6u},
    {"bfi", 0xb37c2c20u},
    {"bfxil", 0xb3483c62u},
    {"sbfx", 0x934330a4u},
    {"ubfx", 0xd3453ce6u},
    {"sxtb", 0x93401d28u},
    {"sxth", 0x93403d6au},
    {"sxtw", 0x93407dacu},
    {"uxtb", 0x53001deeu},
    {"uxth", 0x53003e30u},

    {"extr", 0x93d41e72u},
};

int as_a64_encode_dp_imm(const as_a64_dp_imm_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz) {
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "invalid A64 dp-imm encode inputs");
    }

    for (i = 0; i < sizeof(k_desc) / sizeof(k_desc[0]); ++i) {
        if (streq_ci(insn->mnemonic, k_desc[i].mnemonic)) {
            as_a64_put32le(out, k_desc[i].word);
            if (out_len != NULL) {
                *out_len = 4;
            }
            return 0;
        }
    }

    return set_err(errbuf, errbuf_sz, "unsupported A64 dp-imm mnemonic: %s", insn->mnemonic);
}
