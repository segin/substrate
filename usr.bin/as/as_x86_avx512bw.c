#include "as_x86_avx512bw.h"

#include "as_x86_evex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    BW_FORM_RRR = 0,
    BW_FORM_RR,
    BW_FORM_RRRI,
    BW_FORM_KCMP_IMM,
    BW_FORM_KCMP,
} bw_form_t;

typedef struct {
    const char *mnemonic;
    bw_form_t form;
    uint8_t opcode;
    as_evex_map_t map;
    as_evex_pp_t pp;
    int evex_w;
    int fixed_l2;
    int require_imm8;
    int require_opmask;
} bw_desc_t;

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

static int append_imm8(uint8_t *out, size_t out_cap, size_t *out_len, uint8_t imm8,
                       char *errbuf, size_t errbuf_sz) {
    if (out_len == NULL || *out_len >= out_cap) {
        return set_err(errbuf, errbuf_sz, "encoding overflow");
    }
    out[(*out_len)++] = imm8;
    return 0;
}

static int encode_desc(const bw_desc_t *desc, const as_x86_avx512bw_insn_t *insn,
                       uint8_t *out, size_t out_cap, size_t *out_len,
                       char *errbuf, size_t errbuf_sz) {
    as_x86_evex_insn_t ev;

    if (!!insn->has_imm8 != !!desc->require_imm8) {
        return -1;
    }
    if (desc->require_opmask && insn->opmask == 0) {
        return -1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.mnemonic = insn->mnemonic;
    ev.opcode = desc->opcode;
    ev.map = desc->map;
    ev.pp = desc->pp;
    ev.evex_w = desc->evex_w;
    ev.opmask = insn->opmask;
    ev.zeroing = insn->zeroing;
    ev.broadcast = insn->broadcast;
    ev.sae = insn->sae;
    if (insn->rounding_mode < -1 || insn->rounding_mode > 3) {
        return -1;
    }
    ev.rounding_mode = (insn->rounding_mode == 0 && !insn->sae) ? -1 : insn->rounding_mode;
    ev.evex_l2 = (uint8_t)desc->fixed_l2;

    switch (desc->form) {
    case BW_FORM_RRR:
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            insn->op3.kind == AS_X86_OP_NONE) {
            return -1;
        }
        ev.dst = insn->op1.u.reg;
        ev.src1 = insn->op2.u.reg;
        ev.src2 = insn->op3;
        break;

    case BW_FORM_RR:
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind == AS_X86_OP_NONE) {
            return -1;
        }
        ev.dst = insn->op1.u.reg;
        ev.src1 = AS_X86_REG_RAX;
        ev.src2 = insn->op2;
        break;

    case BW_FORM_RRRI:
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            insn->op3.kind == AS_X86_OP_NONE) {
            return -1;
        }
        ev.dst = insn->op1.u.reg;
        ev.src1 = insn->op2.u.reg;
        ev.src2 = insn->op3;
        break;

    case BW_FORM_KCMP_IMM:
    case BW_FORM_KCMP:
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            insn->op3.kind == AS_X86_OP_NONE) {
            return -1;
        }
        ev.dst = insn->op1.u.reg;
        ev.src1 = insn->op2.u.reg;
        ev.src2 = insn->op3;
        break;

    default:
        return -1;
    }

    if (as_x86_encode_evex_3op(&ev, out, out_cap, out_len, errbuf, errbuf_sz) != 0) {
        return -1;
    }

    if (desc->require_imm8) {
        return append_imm8(out, out_cap, out_len, insn->imm8, errbuf, errbuf_sz);
    }

    return 0;
}

int as_x86_encode_avx512bw(const as_x86_avx512bw_insn_t *insn, uint8_t *out, size_t out_cap,
                           size_t *out_len, char *errbuf, size_t errbuf_sz) {
    static const bw_desc_t descs[] = {
        {"vpaddb", BW_FORM_RRR, 0xfc, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, 2, 0, 0},
        {"vpaddw", BW_FORM_RRR, 0xfd, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, 2, 0, 0},
        {"vpackuswb", BW_FORM_RRR, 0x67, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, 2, 0, 0},
        {"vpunpcklbw", BW_FORM_RRR, 0x60, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, 2, 0, 0},
        {"vpshufb", BW_FORM_RRR, 0x00, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, 2, 0, 0},

        {"vpsllvw", BW_FORM_RRR, 0x12, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 2, 0, 0},
        {"vpsrlvw", BW_FORM_RRR, 0x10, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 2, 0, 0},
        {"vpsravw", BW_FORM_RRR, 0x11, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 2, 0, 0},
        {"vdbpsadbw", BW_FORM_RRRI, 0x42, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, 2, 1, 0},

        {"vpcmpb", BW_FORM_KCMP_IMM, 0x3f, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, 2, 1, 0},
        {"vpcmpw", BW_FORM_KCMP_IMM, 0x3f, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, 2, 1, 0},
        {"vpcmpub", BW_FORM_KCMP_IMM, 0x3e, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, 2, 1, 0},
        {"vpcmpuw", BW_FORM_KCMP_IMM, 0x3e, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, 2, 1, 0},

        {"vpmovb2m", BW_FORM_RR, 0x29, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, 2, 0, 0},
        {"vpmovw2m", BW_FORM_RR, 0x29, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 1, 2, 0, 0},
        {"vpmovm2b", BW_FORM_RR, 0x28, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, 2, 0, 0},
        {"vpmovm2w", BW_FORM_RR, 0x28, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 1, 2, 0, 0},

        {"vpermw", BW_FORM_RRR, 0x8d, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 2, 0, 0},
        {"vpermi2w", BW_FORM_RRR, 0x75, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 2, 0, 0},
        {"vpermt2w", BW_FORM_RRR, 0x7d, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 2, 0, 0},
        {"vpblendmb", BW_FORM_RRR, 0x66, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, 2, 0, 1},
        {"vpblendmw", BW_FORM_RRR, 0x66, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 2, 0, 1},

        {"vptestnmb", BW_FORM_KCMP, 0x26, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, 2, 0, 0},
        {"vptestnmw", BW_FORM_KCMP, 0x26, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 1, 2, 0, 0},
    };
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL) {
        return -1;
    }

    for (i = 0; i < sizeof(descs) / sizeof(descs[0]); ++i) {
        if (streq_ci(insn->mnemonic, descs[i].mnemonic)) {
            return encode_desc(&descs[i], insn, out, out_cap, out_len, errbuf, errbuf_sz);
        }
    }

    return set_err(errbuf, errbuf_sz, "unsupported AVX-512BW mnemonic: %s", insn->mnemonic);
}
