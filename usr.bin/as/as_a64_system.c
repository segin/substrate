#include "as_a64_system.h"

#include "as_a64_encode.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint32_t word;
} a64_sys_desc_t;

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

static const a64_sys_desc_t k_desc[] = {
    {"mrs", 0xd53b4200u},
    {"msr", 0xd51b4201u},

    {"nop", 0xd503201fu},
    {"yield", 0xd503203fu},
    {"wfe", 0xd503205fu},
    {"wfi", 0xd503207fu},
    {"sev", 0xd503209fu},
    {"sevl", 0xd50320bfu},

    {"dmb", 0xd5033fbfu},
    {"dsb", 0xd5033a9fu},
    {"isb", 0xd5033fdfu},

    {"clrex", 0xd5033f5fu},

    {"sys", 0xd5087802u},
    {"sysl", 0xd5287803u},

    {"dc", 0xd50b7424u},
    {"ic", 0xd50b7525u},
    {"at", 0xd5087806u},
    {"tlbi", 0xd508871fu},

    {"hint", 0xd50320ffu},
};

int as_a64_encode_system(const as_a64_system_insn_t *insn, uint8_t *out, size_t out_cap,
                         size_t *out_len, char *errbuf, size_t errbuf_sz) {
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "invalid A64 system encode inputs");
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

    return set_err(errbuf, errbuf_sz, "unsupported A64 system mnemonic: %s", insn->mnemonic);
}
