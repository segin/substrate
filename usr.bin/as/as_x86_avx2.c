#include "as_x86_avx2.h"

#include "as_x86_vex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint8_t opcode;
    as_vex_map_t map;
    as_vex_pp_t pp;
    int vex_w;
} avx2_desc_t;

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

static const avx2_desc_t *lookup_promoted_int(const char *mnemonic) {
    static const avx2_desc_t table[] = {
        {"vpaddb", 0xfc, AS_VEX_MAP_0F, AS_VEX_PP_66, 0},
        {"vpaddw", 0xfd, AS_VEX_MAP_0F, AS_VEX_PP_66, 0},
        {"vpaddd", 0xfe, AS_VEX_MAP_0F, AS_VEX_PP_66, 0},
        {"vpaddq", 0xd4, AS_VEX_MAP_0F, AS_VEX_PP_66, 0},
        {"vpsubb", 0xf8, AS_VEX_MAP_0F, AS_VEX_PP_66, 0},
        {"vpsubw", 0xf9, AS_VEX_MAP_0F, AS_VEX_PP_66, 0},
        {"vpsubd", 0xfa, AS_VEX_MAP_0F, AS_VEX_PP_66, 0},
        {"vpsubq", 0xfb, AS_VEX_MAP_0F, AS_VEX_PP_66, 0},
        {"vpmullw", 0xd5, AS_VEX_MAP_0F, AS_VEX_PP_66, 0},
        {"vpmulld", 0x40, AS_VEX_MAP_0F38, AS_VEX_PP_66, 0},
    };
    size_t i;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (streq_ci(mnemonic, table[i].mnemonic)) {
            return &table[i];
        }
    }

    return NULL;
}

int as_x86_encode_avx2(const as_x86_avx2_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz) {
    const avx2_desc_t *desc;
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

    desc = lookup_promoted_int(insn->mnemonic);
    if (desc != NULL) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            vector_bits_to_l(insn->vector_bits, &vex_l) != 0 || insn->has_imm8) {
            return -1;
        }
        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            desc->opcode, desc->map, desc->pp, desc->vex_w,
                                            vex_l, 0, 0, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vbroadcasti128")) {
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_MEM || insn->has_imm8 || insn->vector_bits != 256) {
            return -1;
        }
        return encode_vex_with_optional_imm(insn->op1.u.reg, AS_X86_REG_RAX, &insn->op2,
                                            0x5a, AS_VEX_MAP_0F38, AS_VEX_PP_66, 0, 1, 0,
                                            0, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vextracti128")) {
        if (insn->op_count != 2 || insn->op2.kind != AS_X86_OP_REG || !insn->has_imm8 ||
            insn->vector_bits != 256) {
            return -1;
        }
        return encode_vex_with_optional_imm(insn->op2.u.reg, AS_X86_REG_RAX, &insn->op1,
                                            0x39, AS_VEX_MAP_0F3A, AS_VEX_PP_66, 0, 1, 1,
                                            insn->imm8, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vinserti128")) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            !insn->has_imm8 || insn->vector_bits != 256) {
            return -1;
        }
        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            0x38, AS_VEX_MAP_0F3A, AS_VEX_PP_66, 0, 1, 1,
                                            insn->imm8, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vpblendd")) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            !insn->has_imm8 || vector_bits_to_l(insn->vector_bits, &vex_l) != 0) {
            return -1;
        }
        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            0x02, AS_VEX_MAP_0F3A, AS_VEX_PP_66, 0, vex_l,
                                            1, insn->imm8, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vpbroadcastb") || streq_ci(insn->mnemonic, "vpbroadcastw") ||
        streq_ci(insn->mnemonic, "vpbroadcastd") || streq_ci(insn->mnemonic, "vpbroadcastq")) {
        uint8_t opcode;

        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind == AS_X86_OP_NONE || insn->has_imm8 ||
            vector_bits_to_l(insn->vector_bits, &vex_l) != 0) {
            return -1;
        }

        if (streq_ci(insn->mnemonic, "vpbroadcastb")) {
            opcode = 0x78;
        } else if (streq_ci(insn->mnemonic, "vpbroadcastw")) {
            opcode = 0x79;
        } else if (streq_ci(insn->mnemonic, "vpbroadcastd")) {
            opcode = 0x58;
        } else {
            opcode = 0x59;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, AS_X86_REG_RAX, &insn->op2,
                                            opcode, AS_VEX_MAP_0F38, AS_VEX_PP_66, 0, vex_l,
                                            0, 0, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vpermd") || streq_ci(insn->mnemonic, "vpermps") ||
        streq_ci(insn->mnemonic, "vpsllvd") || streq_ci(insn->mnemonic, "vpsllvq") ||
        streq_ci(insn->mnemonic, "vpsrlvd") || streq_ci(insn->mnemonic, "vpsrlvq") ||
        streq_ci(insn->mnemonic, "vpsravd")) {
        uint8_t opcode;
        int vex_w = 0;

        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            insn->has_imm8 || insn->vector_bits != 256) {
            return -1;
        }

        if (streq_ci(insn->mnemonic, "vpermd")) {
            opcode = 0x36;
        } else if (streq_ci(insn->mnemonic, "vpermps")) {
            opcode = 0x16;
        } else if (streq_ci(insn->mnemonic, "vpsllvd")) {
            opcode = 0x47;
        } else if (streq_ci(insn->mnemonic, "vpsllvq")) {
            opcode = 0x47;
            vex_w = 1;
        } else if (streq_ci(insn->mnemonic, "vpsrlvd")) {
            opcode = 0x45;
        } else if (streq_ci(insn->mnemonic, "vpsrlvq")) {
            opcode = 0x45;
            vex_w = 1;
        } else {
            opcode = 0x46;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            opcode, AS_VEX_MAP_0F38, AS_VEX_PP_66, vex_w, 1,
                                            0, 0, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vpermpd") || streq_ci(insn->mnemonic, "vpermq")) {
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind == AS_X86_OP_NONE || !insn->has_imm8 ||
            insn->vector_bits != 256) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, AS_X86_REG_RAX, &insn->op2,
                                            streq_ci(insn->mnemonic, "vpermpd") ? 0x01 : 0x00,
                                            AS_VEX_MAP_0F3A, AS_VEX_PP_66, 1, 1, 1,
                                            insn->imm8, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vperm2i128")) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            !insn->has_imm8 || insn->vector_bits != 256) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            0x46, AS_VEX_MAP_0F3A, AS_VEX_PP_66, 0, 1, 1,
                                            insn->imm8, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vpmaskmovd") || streq_ci(insn->mnemonic, "vpmaskmovq")) {
        uint8_t load_opcode = 0x8c;
        uint8_t store_opcode = 0x8e;
        int vex_w = streq_ci(insn->mnemonic, "vpmaskmovq") ? 1 : 0;

        if (insn->op_count != 3 || insn->op2.kind != AS_X86_OP_REG || insn->has_imm8 ||
            insn->vector_bits != 256) {
            return -1;
        }

        if (insn->op1.kind == AS_X86_OP_REG && insn->op3.kind == AS_X86_OP_MEM) {
            return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg,
                                                &insn->op3, load_opcode, AS_VEX_MAP_0F38,
                                                AS_VEX_PP_66, vex_w, 1, 0, 0, out, out_cap,
                                                out_len, errbuf, errbuf_sz);
        }

        if (insn->op1.kind == AS_X86_OP_MEM && insn->op3.kind == AS_X86_OP_REG) {
            return encode_vex_with_optional_imm(insn->op3.u.reg, insn->op2.u.reg,
                                                &insn->op1, store_opcode, AS_VEX_MAP_0F38,
                                                AS_VEX_PP_66, vex_w, 1, 0, 0, out, out_cap,
                                                out_len, errbuf, errbuf_sz);
        }

        return -1;
    }

    if (streq_ci(insn->mnemonic, "vgatherdps") || streq_ci(insn->mnemonic, "vgatherdpd") ||
        streq_ci(insn->mnemonic, "vgatherqps") || streq_ci(insn->mnemonic, "vgatherqpd") ||
        streq_ci(insn->mnemonic, "vpgatherdd") || streq_ci(insn->mnemonic, "vpgatherdq") ||
        streq_ci(insn->mnemonic, "vpgatherqd") || streq_ci(insn->mnemonic, "vpgatherqq")) {
        uint8_t opcode;
        int vex_w = 0;

        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_MEM || insn->op3.kind != AS_X86_OP_REG ||
            insn->has_imm8 || vector_bits_to_l(insn->vector_bits, &vex_l) != 0) {
            return -1;
        }

        if (streq_ci(insn->mnemonic, "vgatherdps")) {
            opcode = 0x92;
        } else if (streq_ci(insn->mnemonic, "vgatherdpd")) {
            opcode = 0x92;
            vex_w = 1;
        } else if (streq_ci(insn->mnemonic, "vgatherqps")) {
            opcode = 0x93;
            vex_l = 0;
        } else if (streq_ci(insn->mnemonic, "vgatherqpd")) {
            opcode = 0x93;
            vex_w = 1;
        } else if (streq_ci(insn->mnemonic, "vpgatherdd")) {
            opcode = 0x90;
        } else if (streq_ci(insn->mnemonic, "vpgatherdq")) {
            opcode = 0x90;
            vex_w = 1;
        } else if (streq_ci(insn->mnemonic, "vpgatherqd")) {
            opcode = 0x91;
            vex_l = 0;
        } else {
            opcode = 0x91;
            vex_w = 1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op3.u.reg, &insn->op2,
                                            opcode, AS_VEX_MAP_0F38, AS_VEX_PP_66, vex_w,
                                            vex_l, 0, 0, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    return set_err(errbuf, errbuf_sz, "unsupported AVX2 mnemonic: %s", insn->mnemonic);
}
