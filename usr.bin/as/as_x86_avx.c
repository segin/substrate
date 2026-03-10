#include "as_x86_avx.h"

#include "as_x86_vex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint8_t opcode;
    as_vex_map_t map;
    as_vex_pp_t pp;
} avx_promoted_desc_t;

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

static int vector_bits_to_l(unsigned vector_bits, int *vex_l) {
    if (vector_bits == 128) {
        *vex_l = 0;
        return 0;
    }
    if (vector_bits == 256) {
        *vex_l = 1;
        return 0;
    }
    return -1;
}

static uint8_t imm8_with_implicit_reg(uint8_t imm8_low, as_x86_reg_t reg) {
    return (uint8_t)((imm8_low & 0x0fu) | ((((unsigned)reg & 7u) | 0x8u) << 4));
}

static int encode_vex_with_optional_imm(as_x86_reg_t modrm_reg, as_x86_reg_t src1,
                                        const as_x86_operand_t *src2, uint8_t opcode,
                                        as_vex_map_t map, as_vex_pp_t pp, int vex_w, int vex_l,
                                        int has_imm8, uint8_t imm8, uint8_t *out,
                                        size_t out_cap, size_t *out_len, char *errbuf,
                                        size_t errbuf_sz) {
    as_x86_vex_insn_t vex;
    size_t n = 0;

    memset(&vex, 0, sizeof(vex));
    vex.opcode = opcode;
    vex.map = map;
    vex.pp = pp;
    vex.vex_w = vex_w;
    vex.vex_l = vex_l;
    vex.dst = modrm_reg;
    vex.src1 = src1;
    vex.src2 = *src2;

    if (as_x86_encode_vex_3op(&vex, out, out_cap, &n, errbuf, errbuf_sz) != 0) {
        return -1;
    }

    if (has_imm8) {
        if (n >= out_cap) {
            return set_err(errbuf, errbuf_sz, "encoding overflow");
        }
        out[n++] = imm8;
    }

    if (out_len != NULL) {
        *out_len = n;
    }

    return 0;
}

static const avx_promoted_desc_t *lookup_promoted(const char *mnemonic) {
    static const avx_promoted_desc_t table[] = {
        {"vmovhlps", 0x12, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vmovlps", 0x12, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vunpcklps", 0x14, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vunpckhps", 0x15, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vmovlhps", 0x16, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vmovhps", 0x16, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vaddps", 0x58, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vaddpd", 0x58, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vsubps", 0x5c, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vsubpd", 0x5c, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vmulps", 0x59, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vmulpd", 0x59, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vdivps", 0x5e, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vdivpd", 0x5e, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vandps", 0x54, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vandnps", 0x55, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vandpd", 0x54, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vandnpd", 0x55, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vorps", 0x56, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vorpd", 0x56, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vxorps", 0x57, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vxorpd", 0x57, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vminps", 0x5d, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vminpd", 0x5d, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vmaxps", 0x5f, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vmaxpd", 0x5f, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vmovlpd", 0x12, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vunpcklpd", 0x14, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vunpckhpd", 0x15, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vmovhpd", 0x16, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpunpcklbw", 0x60, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpunpcklwd", 0x61, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpunpckldq", 0x62, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpacksswb", 0x63, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpcmpgtb", 0x64, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpcmpgtw", 0x65, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpcmpgtd", 0x66, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpackuswb", 0x67, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpunpckhbw", 0x68, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpunpckhwd", 0x69, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpunpckhdq", 0x6a, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpackssdw", 0x6b, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpunpcklqdq", 0x6c, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpunpckhqdq", 0x6d, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpcmpeqb", 0x74, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpcmpeqw", 0x75, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpcmpeqd", 0x76, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpand", 0xdb, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpor", 0xeb, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpxor", 0xef, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vaddsubpd", 0xd0, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vaddsubps", 0xd0, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vpsrlw", 0xd1, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsrld", 0xd2, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsrlq", 0xd3, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpaddq", 0xd4, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpmullw", 0xd5, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsubusb", 0xd8, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsubusw", 0xd9, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpminub", 0xda, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vhaddpd", 0x7c, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vhaddps", 0x7c, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vhsubpd", 0x7d, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vhsubps", 0x7d, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vpaddusb", 0xdc, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpaddusw", 0xdd, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpmaxub", 0xde, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpandn", 0xdf, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpavgb", 0xe0, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsraw", 0xe1, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsrad", 0xe2, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpavgw", 0xe3, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpmulhuw", 0xe4, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpmulhw", 0xe5, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsubsb", 0xe8, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsubsw", 0xe9, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpminsw", 0xea, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpaddsb", 0xec, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpaddsw", 0xed, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpmaxsw", 0xee, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsllw", 0xf1, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpslld", 0xf2, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsllq", 0xf3, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpmuludq", 0xf4, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpmaddwd", 0xf5, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsadbw", 0xf6, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsubb", 0xf8, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsubw", 0xf9, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsubd", 0xfa, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpsubq", 0xfb, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpaddb", 0xfc, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpaddw", 0xfd, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpaddd", 0xfe, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vmovss", 0x10, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vcvtsi2ss", 0x2a, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vsqrtss", 0x51, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vrsqrtss", 0x52, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vrcpss", 0x53, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vaddss", 0x58, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vmulss", 0x59, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vcvtss2sd", 0x5a, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vsubss", 0x5c, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vminss", 0x5d, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vdivss", 0x5e, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vmaxss", 0x5f, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vmovsd", 0x10, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vcvtsi2sd", 0x2a, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vsqrtsd", 0x51, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vaddsd", 0x58, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vmulsd", 0x59, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vcvtsd2ss", 0x5a, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vsubsd", 0x5c, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vminsd", 0x5d, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vdivsd", 0x5e, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vmaxsd", 0x5f, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vpdpbuud", 0x50, AS_VEX_MAP_0F38, AS_VEX_PP_NONE},
        {"vpdpbuuds", 0x51, AS_VEX_MAP_0F38, AS_VEX_PP_NONE},
        {"vpdpbusd", 0x50, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpdpbusds", 0x51, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpdpwssd", 0x52, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpdpwssds", 0x53, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpdpbsud", 0x50, AS_VEX_MAP_0F38, AS_VEX_PP_F3},
        {"vpdpbsuds", 0x51, AS_VEX_MAP_0F38, AS_VEX_PP_F3},
        {"vpdpbssd", 0x50, AS_VEX_MAP_0F38, AS_VEX_PP_F2},
        {"vpdpbssds", 0x51, AS_VEX_MAP_0F38, AS_VEX_PP_F2},
        {"vpshufb", 0x00, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vphaddw", 0x01, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vphaddd", 0x02, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vphaddsw", 0x03, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpmaddubsw", 0x04, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vphsubw", 0x05, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vphsubd", 0x06, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vphsubsw", 0x07, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpsignb", 0x08, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpsignw", 0x09, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpsignd", 0x0a, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpmulhrsw", 0x0b, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpermilps", 0x0c, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vpermilpd", 0x0d, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vpmuldq", 0x28, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpcmpeqq", 0x29, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpackusdw", 0x2b, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpcmpgtq", 0x37, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpminsb", 0x38, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpminsd", 0x39, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpminuw", 0x3a, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpminud", 0x3b, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpmaxsb", 0x3c, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpmaxsd", 0x3d, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpmaxuw", 0x3e, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpmaxud", 0x3f, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpmulld", 0x40, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpmadd52luq", 0xb4, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vpmadd52huq", 0xb5, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vgf2p8mulb", 0xcf, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vaesenc", 0xdc, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vaesenclast", 0xdd, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vaesdec", 0xde, AS_VEX_MAP_0F38, AS_VEX_PP_66},
        {"vaesdeclast", 0xdf, AS_VEX_MAP_0F38, AS_VEX_PP_66},
    };
    size_t i;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (streq_ci(mnemonic, table[i].mnemonic)) {
            return &table[i];
        }
    }

    return NULL;
}

static const avx_promoted_desc_t *lookup_promoted_imm(const char *mnemonic) {
    static const avx_promoted_desc_t table[] = {
        {"vcmpps", 0xc2, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vcmppd", 0xc2, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vcmpss", 0xc2, AS_VEX_MAP_0F, AS_VEX_PP_F3},
        {"vcmpsd", 0xc2, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vroundss", 0x0a, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vroundsd", 0x0b, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vblendps", 0x0c, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vblendpd", 0x0d, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vpblendw", 0x0e, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vpalignr", 0x0f, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vpinsrb", 0x20, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vinsertps", 0x21, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vpinsrd", 0x22, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vpinsrw", 0xc4, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vdpps", 0x40, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vdppd", 0x41, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vmpsadbw", 0x42, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vpclmulqdq", 0x44, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vgf2p8affineqb", 0xce, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vgf2p8affineinvqb", 0xcf, AS_VEX_MAP_0F3A, AS_VEX_PP_66},
        {"vshufps", 0xc6, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vshufpd", 0xc6, AS_VEX_MAP_0F, AS_VEX_PP_66},
    };
    size_t i;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (streq_ci(mnemonic, table[i].mnemonic)) {
            return &table[i];
        }
    }

    return NULL;
}

int as_x86_encode_avx(const as_x86_avx_insn_t *insn, uint8_t *out, size_t out_cap,
                      size_t *out_len, char *errbuf, size_t errbuf_sz) {
    const avx_promoted_desc_t *promoted;
    const avx_promoted_desc_t *promoted_imm;
    int vex_l = 0;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL) {
        return -1;
    }

    promoted = lookup_promoted(insn->mnemonic);
    if (promoted != NULL) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            vector_bits_to_l(insn->vector_bits, &vex_l) != 0 || insn->has_imm8) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            promoted->opcode, promoted->map, promoted->pp, 0,
                                            vex_l, 0, 0, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    promoted_imm = lookup_promoted_imm(insn->mnemonic);
    if (promoted_imm != NULL) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            vector_bits_to_l(insn->vector_bits, &vex_l) != 0 || !insn->has_imm8) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            promoted_imm->opcode, promoted_imm->map,
                                            promoted_imm->pp, 0, vex_l, 1, insn->imm8,
                                            out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vpermil2ps") || streq_ci(insn->mnemonic, "vpermil2pd")) {
        if (insn->op_count != 4 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            !insn->has_imm8 || !insn->has_imm_reg ||
            vector_bits_to_l(insn->vector_bits, &vex_l) != 0) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            streq_ci(insn->mnemonic, "vpermil2ps") ? 0x48 : 0x49,
                                            AS_VEX_MAP_0F3A, AS_VEX_PP_66, insn->vex_w, vex_l, 1,
                                            imm8_with_implicit_reg(insn->imm8, insn->imm_reg),
                                            out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vblendvps") || streq_ci(insn->mnemonic, "vblendvpd") ||
        streq_ci(insn->mnemonic, "vpblendvb")) {
        uint8_t opcode;

        if (insn->op_count != 4 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            insn->has_imm8 || !insn->has_imm_reg ||
            vector_bits_to_l(insn->vector_bits, &vex_l) != 0) {
            return -1;
        }

        if (streq_ci(insn->mnemonic, "vblendvps")) {
            opcode = 0x4a;
        } else if (streq_ci(insn->mnemonic, "vblendvpd")) {
            opcode = 0x4b;
        } else {
            opcode = 0x4c;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            opcode, AS_VEX_MAP_0F3A, AS_VEX_PP_66, 0, vex_l,
                                            1, imm8_with_implicit_reg(0, insn->imm_reg),
                                            out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vbroadcastss") || streq_ci(insn->mnemonic, "vbroadcastsd") ||
        streq_ci(insn->mnemonic, "vbroadcastf128")) {
        uint8_t opcode;

        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_MEM || insn->has_imm8) {
            return -1;
        }

        if (streq_ci(insn->mnemonic, "vbroadcastf128")) {
            if (insn->vector_bits != 256) {
                return -1;
            }
            vex_l = 1;
            opcode = 0x1a;
        } else {
            if (vector_bits_to_l(insn->vector_bits, &vex_l) != 0) {
                return -1;
            }
            opcode = streq_ci(insn->mnemonic, "vbroadcastss") ? 0x18 : 0x19;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, AS_X86_REG_RAX, &insn->op2,
                                            opcode, AS_VEX_MAP_0F38, AS_VEX_PP_66, 0, vex_l,
                                            0, 0, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vextractf128")) {
        if (insn->op_count != 2 || insn->op2.kind != AS_X86_OP_REG || !insn->has_imm8 ||
            insn->vector_bits != 256) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op2.u.reg, AS_X86_REG_RAX, &insn->op1,
                                            0x19, AS_VEX_MAP_0F3A, AS_VEX_PP_66, 0, 1, 1,
                                            insn->imm8, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vinsertf128")) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            !insn->has_imm8 || insn->vector_bits != 256) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            0x18, AS_VEX_MAP_0F3A, AS_VEX_PP_66, 0, 1, 1,
                                            insn->imm8, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vmaskmovps") || streq_ci(insn->mnemonic, "vmaskmovpd")) {
        uint8_t load_opcode = streq_ci(insn->mnemonic, "vmaskmovps") ? 0x2c : 0x2d;
        uint8_t store_opcode = streq_ci(insn->mnemonic, "vmaskmovps") ? 0x2e : 0x2f;

        if (insn->op_count != 3 || insn->op2.kind != AS_X86_OP_REG || insn->has_imm8 ||
            vector_bits_to_l(insn->vector_bits, &vex_l) != 0) {
            return -1;
        }

        if (insn->op1.kind == AS_X86_OP_REG && insn->op3.kind == AS_X86_OP_MEM) {
            return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg,
                                                &insn->op3, load_opcode, AS_VEX_MAP_0F38,
                                                AS_VEX_PP_66, 0, vex_l, 0, 0, out, out_cap,
                                                out_len, errbuf, errbuf_sz);
        }

        if (insn->op1.kind == AS_X86_OP_MEM && insn->op3.kind == AS_X86_OP_REG) {
            return encode_vex_with_optional_imm(insn->op3.u.reg, insn->op2.u.reg,
                                                &insn->op1, store_opcode, AS_VEX_MAP_0F38,
                                                AS_VEX_PP_66, 0, vex_l, 0, 0, out, out_cap,
                                                out_len, errbuf, errbuf_sz);
        }

        return -1;
    }

    if (streq_ci(insn->mnemonic, "vperm2f128")) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            !insn->has_imm8 || insn->vector_bits != 256) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            0x06, AS_VEX_MAP_0F3A, AS_VEX_PP_66, 0, 1, 1,
                                            insn->imm8, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vpermilps") || streq_ci(insn->mnemonic, "vpermilpd")) {
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind == AS_X86_OP_NONE || !insn->has_imm8 ||
            vector_bits_to_l(insn->vector_bits, &vex_l) != 0) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, AS_X86_REG_RAX, &insn->op2,
                                            streq_ci(insn->mnemonic, "vpermilps") ? 0x04 : 0x05,
                                            AS_VEX_MAP_0F3A, AS_VEX_PP_66, 0, vex_l, 1,
                                            insn->imm8, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vtestps") || streq_ci(insn->mnemonic, "vtestpd")) {
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind == AS_X86_OP_NONE || insn->has_imm8 ||
            vector_bits_to_l(insn->vector_bits, &vex_l) != 0) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, AS_X86_REG_RAX, &insn->op2,
                                            streq_ci(insn->mnemonic, "vtestps") ? 0x0e : 0x0f,
                                            AS_VEX_MAP_0F38, AS_VEX_PP_66, 0, vex_l, 0, 0,
                                            out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vzeroupper") || streq_ci(insn->mnemonic, "vzeroall")) {
        if (insn->op_count != 0 || insn->has_imm8) {
            return -1;
        }
        if (out_cap < 3) {
            return -1;
        }
        out[0] = 0xc5;
        out[1] = streq_ci(insn->mnemonic, "vzeroall") ? 0xfc : 0xf8;
        out[2] = 0x77;
        if (out_len != NULL) {
            *out_len = 3;
        }
        return 0;
    }

    return set_err(errbuf, errbuf_sz, "unsupported AVX mnemonic: %s", insn->mnemonic);
}
