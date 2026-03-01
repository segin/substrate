#include "as_x86_bmi2.h"

#include "as_x86_vex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

static int encode_vex_with_optional_imm(as_x86_reg_t modrm_reg, as_x86_reg_t src1,
                                        const as_x86_operand_t *src2, uint8_t opcode,
                                        as_vex_map_t map, as_vex_pp_t pp, int vex_w,
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
    vex.vex_l = 0;
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

int as_x86_encode_bmi2(const as_x86_bmi2_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz) {
    int vex_w;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || out == NULL || insn->mnemonic == NULL ||
        (insn->width_bits != 32 && insn->width_bits != 64)) {
        return -1;
    }

    vex_w = (insn->width_bits == 64) ? 1 : 0;

    if (streq_ci(insn->mnemonic, "bzhi")) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op3.kind != AS_X86_OP_REG || insn->op2.kind == AS_X86_OP_NONE ||
            insn->has_imm8) {
            return -1;
        }
        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op3.u.reg, &insn->op2,
                                            0xf5, AS_VEX_MAP_0F38, AS_VEX_PP_NONE, vex_w,
                                            0, 0, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "mulx")) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            insn->has_imm8) {
            return -1;
        }
        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            0xf6, AS_VEX_MAP_0F38, AS_VEX_PP_F2, vex_w,
                                            0, 0, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "pdep") || streq_ci(insn->mnemonic, "pext")) {
        as_vex_pp_t pp = streq_ci(insn->mnemonic, "pdep") ? AS_VEX_PP_F2 : AS_VEX_PP_F3;

        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE ||
            insn->has_imm8) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            0xf5, AS_VEX_MAP_0F38, pp, vex_w,
                                            0, 0, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "sarx") || streq_ci(insn->mnemonic, "shlx") ||
        streq_ci(insn->mnemonic, "shrx")) {
        as_vex_pp_t pp;

        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op3.kind != AS_X86_OP_REG || insn->op2.kind == AS_X86_OP_NONE ||
            insn->has_imm8) {
            return -1;
        }

        if (streq_ci(insn->mnemonic, "sarx")) {
            pp = AS_VEX_PP_F3;
        } else if (streq_ci(insn->mnemonic, "shlx")) {
            pp = AS_VEX_PP_66;
        } else {
            pp = AS_VEX_PP_F2;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, insn->op3.u.reg, &insn->op2,
                                            0xf7, AS_VEX_MAP_0F38, pp, vex_w,
                                            0, 0, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "rorx")) {
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind == AS_X86_OP_NONE || !insn->has_imm8) {
            return -1;
        }

        return encode_vex_with_optional_imm(insn->op1.u.reg, AS_X86_REG_RAX, &insn->op2,
                                            0xf0, AS_VEX_MAP_0F3A, AS_VEX_PP_F2, vex_w,
                                            1, insn->imm8, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    return set_err(errbuf, errbuf_sz, "unsupported BMI2 mnemonic: %s", insn->mnemonic);
}
