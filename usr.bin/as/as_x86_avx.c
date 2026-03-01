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
        {"vaddps", 0x58, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vaddpd", 0x58, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vsubps", 0x5c, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vsubpd", 0x5c, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vmulps", 0x59, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vmulpd", 0x59, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vdivps", 0x5e, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vdivpd", 0x5e, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vandps", 0x54, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vandpd", 0x54, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vorps", 0x56, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vorpd", 0x56, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vxorps", 0x57, AS_VEX_MAP_0F, AS_VEX_PP_NONE},
        {"vxorpd", 0x57, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpand", 0xdb, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpor", 0xeb, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vpxor", 0xef, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vaddsubpd", 0xd0, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vaddsubps", 0xd0, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vhaddpd", 0x7c, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vhaddps", 0x7c, AS_VEX_MAP_0F, AS_VEX_PP_F2},
        {"vhsubpd", 0x7d, AS_VEX_MAP_0F, AS_VEX_PP_66},
        {"vhsubps", 0x7d, AS_VEX_MAP_0F, AS_VEX_PP_F2},
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
