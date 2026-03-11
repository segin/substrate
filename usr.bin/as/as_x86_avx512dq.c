#include "as_x86_avx512dq.h"

#include "as_x86_evex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    DQ_FORM_RRR = 0,
    DQ_FORM_RR,
    DQ_FORM_RRI,
    DQ_FORM_RRRI,
    DQ_FORM_EXTRACT,
    DQ_FORM_KCMP,
} dq_form_t;

typedef struct {
    const char *mnemonic;
    dq_form_t form;
    uint8_t opcode;
    as_evex_map_t map;
    as_evex_pp_t pp;
    int evex_w;
    int fixed_l2;
    int require_imm8;
    int require_mem_src;
    int require_mem_dst;
} dq_desc_t;

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

static int l2_from_vector_bits(unsigned vector_bits, uint8_t *l2) {
    if (vector_bits == 128) {
        *l2 = 0;
        return 0;
    }
    if (vector_bits == 256) {
        *l2 = 1;
        return 0;
    }
    if (vector_bits == 512) {
        *l2 = 2;
        return 0;
    }
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

static int encode_desc(const dq_desc_t *desc, const as_x86_avx512dq_insn_t *insn,
                       uint8_t *out, size_t out_cap, size_t *out_len,
                       char *errbuf, size_t errbuf_sz) {
    as_x86_evex_insn_t ev;
    unsigned vb;

    if (!!insn->has_imm8 != !!desc->require_imm8) {
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

    if (desc->fixed_l2 >= 0) {
        ev.evex_l2 = (uint8_t)desc->fixed_l2;
    } else {
        vb = (insn->vector_bits == 0) ? 512 : insn->vector_bits;
        if (l2_from_vector_bits(vb, &ev.evex_l2) != 0) {
            return -1;
        }
    }

    switch (desc->form) {
    case DQ_FORM_RRR:
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            insn->op3.kind == AS_X86_OP_NONE) {
            return -1;
        }
        ev.dst = insn->op1.u.reg;
        ev.src1 = insn->op2.u.reg;
        ev.src2 = insn->op3;
        break;

    case DQ_FORM_RR:
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind == AS_X86_OP_NONE) {
            return -1;
        }
        if (desc->require_mem_src && insn->op2.kind != AS_X86_OP_MEM) {
            return -1;
        }
        ev.dst = insn->op1.u.reg;
        ev.src1 = AS_X86_REG_RAX;
        ev.src2 = insn->op2;
        break;

    case DQ_FORM_RRI:
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind == AS_X86_OP_NONE) {
            return -1;
        }
        ev.dst = insn->op1.u.reg;
        if (streq_ci(desc->mnemonic, "vprold") || streq_ci(desc->mnemonic, "vprolq")) {
            ev.src1 = AS_X86_REG_RCX;
        } else {
            ev.src1 = AS_X86_REG_RAX;
        }
        ev.src2 = insn->op2;
        break;

    case DQ_FORM_RRRI:
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            insn->op3.kind == AS_X86_OP_NONE) {
            return -1;
        }
        ev.dst = insn->op1.u.reg;
        ev.src1 = insn->op2.u.reg;
        ev.src2 = insn->op3;
        break;

    case DQ_FORM_EXTRACT:
        if (insn->op_count != 2 || insn->op1.kind == AS_X86_OP_NONE || insn->op2.kind != AS_X86_OP_REG) {
            return -1;
        }
        if (desc->require_mem_dst && insn->op1.kind != AS_X86_OP_MEM) {
            return -1;
        }
        ev.dst = insn->op2.u.reg;
        ev.src1 = AS_X86_REG_RAX;
        ev.src2 = insn->op1;
        break;

    case DQ_FORM_KCMP:
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

int as_x86_encode_avx512dq(const as_x86_avx512dq_insn_t *insn, uint8_t *out, size_t out_cap,
                           size_t *out_len, char *errbuf, size_t errbuf_sz) {
    static const dq_desc_t descs[] = {
        {"vcvtps2qq", DQ_FORM_RR, 0x7b, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0},
        {"vcvtps2uqq", DQ_FORM_RR, 0x79, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0},
        {"vcvtpd2qq", DQ_FORM_RR, 0x7b, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vcvtpd2uqq", DQ_FORM_RR, 0x79, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vcvttps2qq", DQ_FORM_RR, 0x7a, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0},
        {"vcvttps2uqq", DQ_FORM_RR, 0x78, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0},
        {"vcvttpd2qq", DQ_FORM_RR, 0x7a, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vcvttpd2uqq", DQ_FORM_RR, 0x78, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vcvtqq2ps", DQ_FORM_RR, 0x5b, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 1, -1, 0, 0, 0},
        {"vcvtqq2pd", DQ_FORM_RR, 0xe6, AS_EVEX_MAP_0F, AS_EVEX_PP_F3, 1, -1, 0, 0, 0},
        {"vcvtuqq2ps", DQ_FORM_RR, 0x7a, AS_EVEX_MAP_0F, AS_EVEX_PP_F2, 1, -1, 0, 0, 0},
        {"vcvtuqq2pd", DQ_FORM_RR, 0x7a, AS_EVEX_MAP_0F, AS_EVEX_PP_F3, 1, -1, 0, 0, 0},
        {"vprord", DQ_FORM_RRI, 0x72, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 1, 0, 0},
        {"vprold", DQ_FORM_RRI, 0x72, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 1, 0, 0},
        {"vprorq", DQ_FORM_RRI, 0x72, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 1, 0, 0},
        {"vprolq", DQ_FORM_RRI, 0x72, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 1, 0, 0},
        {"vprorvd", DQ_FORM_RRR, 0x14, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0},
        {"vprolvd", DQ_FORM_RRR, 0x15, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0},
        {"vprorvq", DQ_FORM_RRR, 0x14, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vprolvq", DQ_FORM_RRR, 0x15, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpsrlvq", DQ_FORM_RRR, 0x45, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpsravq", DQ_FORM_RRR, 0x46, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpsllvq", DQ_FORM_RRR, 0x47, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpshldvq", DQ_FORM_RRR, 0x71, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpshrdvq", DQ_FORM_RRR, 0x73, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpminsq", DQ_FORM_RRR, 0x39, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpminuq", DQ_FORM_RRR, 0x3b, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpmaxsq", DQ_FORM_RRR, 0x3d, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpmaxuq", DQ_FORM_RRR, 0x3f, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpmadd52luq", DQ_FORM_RRR, 0xb4, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpmadd52huq", DQ_FORM_RRR, 0xb5, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},

        {"vpmullq", DQ_FORM_RRR, 0x40, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpmovm2d", DQ_FORM_RR, 0x38, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0},
        {"vpmovm2q", DQ_FORM_RR, 0x38, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 1, -1, 0, 0, 0},
        {"vpmovd2m", DQ_FORM_RR, 0x39, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0},
        {"vpmovq2m", DQ_FORM_RR, 0x39, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 1, -1, 0, 0, 0},
        {"vpcmpeqq", DQ_FORM_KCMP, 0x29, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpcmpgtq", DQ_FORM_KCMP, 0x37, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vptestmq", DQ_FORM_KCMP, 0x27, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vptestnmq", DQ_FORM_KCMP, 0x27, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 1, -1, 0, 0, 0},
        {"vp2intersectq", DQ_FORM_KCMP, 0x68, AS_EVEX_MAP_0F38, AS_EVEX_PP_F2, 1, -1, 0, 0, 0},
        {"vp2intersectq", DQ_FORM_KCMP, 0x68, AS_EVEX_MAP_0F38, AS_EVEX_PP_F2, 1, -1, 0, 0, 0},

        {"vinserti64x2", DQ_FORM_RRRI, 0x38, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0},
        {"vinserti32x4", DQ_FORM_RRRI, 0x38, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0},
        {"vextracti64x2", DQ_FORM_EXTRACT, 0x39, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0},
        {"vinserti32x8", DQ_FORM_RRRI, 0x3a, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0},
        {"vinserti64x4", DQ_FORM_RRRI, 0x3a, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0},
        {"vextracti32x8", DQ_FORM_EXTRACT, 0x3b, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0},

        {"valignq", DQ_FORM_RRRI, 0x03, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0},
        {"vrangeps", DQ_FORM_RRRI, 0x50, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0},
        {"vrangepd", DQ_FORM_RRRI, 0x50, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0},
        {"vrangess", DQ_FORM_RRRI, 0x51, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, 0, 1, 0, 0},
        {"vrangesd", DQ_FORM_RRRI, 0x51, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, 0, 1, 0, 0},
        {"vreduceps", DQ_FORM_RRI, 0x56, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0},
        {"vreducepd", DQ_FORM_RRI, 0x56, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0},
        {"vreducess", DQ_FORM_RRRI, 0x57, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, 0, 1, 0, 0},
        {"vreducesd", DQ_FORM_RRRI, 0x57, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, 0, 1, 0, 0},
        {"vpshldq", DQ_FORM_RRRI, 0x71, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0},
        {"vpshrdq", DQ_FORM_RRRI, 0x73, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0},
        {"vfpclassps", DQ_FORM_RRI, 0x66, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0},
        {"vfpclasspd", DQ_FORM_RRI, 0x66, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0},
        {"vfpclassss", DQ_FORM_RRI, 0x67, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, 0, 1, 0, 0},
        {"vfpclasssd", DQ_FORM_RRI, 0x67, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, 0, 1, 0, 0},

        {"vandps", DQ_FORM_RRR, 0x54, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0},
        {"vandpd", DQ_FORM_RRR, 0x54, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vorps", DQ_FORM_RRR, 0x56, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0},
        {"vorpd", DQ_FORM_RRR, 0x56, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vxorps", DQ_FORM_RRR, 0x57, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0},
        {"vxorpd", DQ_FORM_RRR, 0x57, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vandnps", DQ_FORM_RRR, 0x55, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0},
        {"vandnpd", DQ_FORM_RRR, 0x55, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpandq", DQ_FORM_RRR, 0xdb, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpandnq", DQ_FORM_RRR, 0xdf, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpsraq", DQ_FORM_RRR, 0xe2, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vporq", DQ_FORM_RRR, 0xeb, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},
        {"vpxorq", DQ_FORM_RRR, 0xef, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0},

        {"vbroadcastf32x2", DQ_FORM_RR, 0x19, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0},
        {"vbroadcastf32x8", DQ_FORM_RR, 0x1b, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0},
        {"vbroadcasti32x2", DQ_FORM_RR, 0x59, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0},
        {"vbroadcasti32x8", DQ_FORM_RR, 0x5b, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0},
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

    return set_err(errbuf, errbuf_sz, "unsupported AVX-512DQ mnemonic: %s", insn->mnemonic);
}
