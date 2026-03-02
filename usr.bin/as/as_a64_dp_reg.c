#include "as_a64_dp_reg.h"

#include "as_a64_encode.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint32_t word;
} a64_dp_reg_desc_t;

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

static const a64_dp_reg_desc_t k_desc[] = {
    {"add", 0x8b020020u},
    {"add.shift", 0x8b050c83u},
    {"sub.shift", 0xcb8814e6u},
    {"adds", 0xab0b0149u},
    {"subs", 0xeb0e01acu},

    {"add.ext", 0x8b314a0fu},
    {"sub.ext", 0xcb34c272u},

    {"and", 0x8a1702d5u},
    {"orr", 0xaa5a1338u},
    {"eor", 0xca9d079bu},
    {"orn", 0xaa220020u},
    {"eon", 0xca250083u},
    {"bic", 0x8a2800e6u},
    {"bics", 0xea2b0149u},
    {"ands", 0xea0e01acu},

    {"adc", 0x9a11020fu},
    {"adcs", 0xba140272u},
    {"sbc", 0xda1702d5u},
    {"sbcs", 0xfa1a0338u},

    {"madd", 0x9b020c20u},
    {"msub", 0x9b069ca4u},
    {"mul", 0x9b0a7d28u},
    {"mneg", 0x9b0dfd8bu},

    {"smaddl", 0x9b3045eeu},
    {"smsubl", 0x9b34d672u},
    {"umaddl", 0x9bb866f6u},
    {"umsubl", 0x9bbcf77au},
    {"smull", 0x9b227c20u},
    {"umull", 0x9ba57c83u},

    {"smulh", 0x9b487ce6u},
    {"umulh", 0x9bcb7d49u},

    {"sdiv", 0x9ace0dacu},
    {"udiv", 0x9ad10a0fu},

    {"cls", 0xdac01672u},
    {"clz", 0xdac012b4u},
    {"rbit", 0xdac002f6u},
    {"rev", 0xdac00f38u},
    {"rev16", 0xdac0077au},
    {"rev32", 0xdac00bbcu},

    {"csel", 0x9a820020u},
    {"csinc", 0x9a851483u},
    {"csinv", 0xda8820e6u},
    {"csneg", 0xda8b3549u},
    {"cinc", 0x9a8d55acu},
    {"cinv", 0xda8f41eeu},
    {"cneg", 0xda917630u},
    {"cset", 0x9a9f67f2u},
    {"csetm", 0xda9f93f3u},

    {"ccmn", 0xba559285u},
    {"ccmp", 0xfa57a2c9u},
};

int as_a64_encode_dp_reg(const as_a64_dp_reg_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz) {
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "invalid A64 dp-reg encode inputs");
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

    return set_err(errbuf, errbuf_sz, "unsupported A64 dp-reg mnemonic: %s", insn->mnemonic);
}
