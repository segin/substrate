#include "as_x86_avx512cd.h"

#include "as_x86_evex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint8_t opcode;
    as_evex_pp_t pp;
    int evex_w;
} cd_desc_t;

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

int as_x86_encode_avx512cd(const as_x86_avx512cd_insn_t *insn, uint8_t *out, size_t out_cap,
                           size_t *out_len, char *errbuf, size_t errbuf_sz) {
    static const cd_desc_t descs[] = {
        {"vpconflictd", 0xc4, AS_EVEX_PP_66, 0},
        {"vpconflictq", 0xc4, AS_EVEX_PP_66, 1},
        {"vplzcntd", 0x44, AS_EVEX_PP_66, 0},
        {"vplzcntq", 0x44, AS_EVEX_PP_66, 1},
        {"vpbroadcastmb2q", 0x2a, AS_EVEX_PP_F3, 1},
        {"vpbroadcastmw2d", 0x3a, AS_EVEX_PP_F3, 0},
    };
    as_x86_evex_insn_t ev;
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || insn->op_count != 2 ||
        insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG) {
        return -1;
    }

    for (i = 0; i < sizeof(descs) / sizeof(descs[0]); ++i) {
        if (streq_ci(insn->mnemonic, descs[i].mnemonic)) {
            memset(&ev, 0, sizeof(ev));
            ev.mnemonic = insn->mnemonic;
            ev.opcode = descs[i].opcode;
            ev.map = AS_EVEX_MAP_0F38;
            ev.pp = descs[i].pp;
            ev.evex_w = descs[i].evex_w;
            ev.dst = insn->op1.u.reg;
            ev.src1 = AS_X86_REG_RAX;
            ev.src2 = insn->op2;
            ev.opmask = 0;
            ev.rounding_mode = -1;
            ev.evex_l2 = 2;
            return as_x86_encode_evex_3op(&ev, out, out_cap, out_len, errbuf, errbuf_sz);
        }
    }

    return set_err(errbuf, errbuf_sz, "unsupported AVX-512CD mnemonic: %s", insn->mnemonic);
}
