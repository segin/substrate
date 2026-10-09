#include "as_a64_loadstore.h"

#include "as_a64_encode.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint32_t word;
} a64_ls_desc_t;

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

static const a64_ls_desc_t k_desc[] = {
    {"ldr", 0xf9400820u},
    {"str", 0xf9000c62u},
    {"ldr.pre", 0xf8408ca4u},
    {"str.post", 0xf80084e6u},
    {"ldr.reg", 0xf86a6928u},

    {"ldrb", 0x3940058bu},
    {"strb", 0x390009cdu},
    {"ldrh", 0x79400a0fu},
    {"strh", 0x79000e51u},
    {"ldrsb", 0x39800e93u},
    {"ldrsh", 0x798012d5u},
    {"ldrsw", 0xb9800f17u},

    {"ldp", 0xa9410440u},
    {"stp", 0xa9bf10a3u},
    {"ldpsw", 0x69431d06u},

    {"ldnp", 0xa8422969u},
    {"stnp", 0xa83e35ccu},

    {"ldr.literal", 0x5800010fu},

    {"ldxr", 0xc85f7e30u},
    {"stxr", 0xc8127e93u},
    {"ldxrb", 0x085f7ed5u},
    {"stxrb", 0x08177f38u},
    {"ldxrh", 0x485f7f7au},
    {"stxrh", 0x481c7fddu},
    {"ldxp", 0xc87f0440u},
    {"stxp", 0xc82314c4u},

    {"ldar", 0xc8dffd07u},
    {"stlr", 0xc89ffd49u},
    {"ldarb", 0x08dffd8bu},
    {"stlrb", 0x089ffdcdu},
    {"ldarh", 0x48dffe0fu},
    {"stlrh", 0x489ffe51u},

    {"ldaxr", 0xc85ffe93u},
    {"stlxr", 0xc815fef6u},
    {"ldaxrb", 0x085fff38u},
    {"stlxrb", 0x081aff9bu},
    {"ldaxrh", 0x485fffddu},
    {"stlxrh", 0x4800fc41u},
    {"ldaxp", 0xc87f90a3u},
    {"stlxp", 0xc826a127u},

    {"prfm", 0xf9802140u},
};

int as_a64_encode_loadstore(const as_a64_loadstore_insn_t *insn, uint8_t *out, size_t out_cap,
                            size_t *out_len, char *errbuf, size_t errbuf_sz) {
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "invalid A64 load/store encode inputs");
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

    return set_err(errbuf, errbuf_sz, "unsupported A64 load/store mnemonic: %s", insn->mnemonic);
}
