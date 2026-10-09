#include "as_x86_f16c.h"

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
                                        as_vex_map_t map, as_vex_pp_t pp, int vex_l,
                                        int has_imm8, uint8_t imm8, uint8_t *out,
                                        size_t out_cap, size_t *out_len, char *errbuf,
                                        size_t errbuf_sz) {
    as_x86_vex_insn_t vex;
    size_t n = 0;

    memset(&vex, 0, sizeof(vex));
    vex.opcode = opcode;
    vex.map = map;
    vex.pp = pp;
    vex.vex_w = 0;
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

int as_x86_encode_f16c(const as_x86_f16c_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz) {
    int vex_l;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || out == NULL || insn->mnemonic == NULL ||
        vector_bits_to_l(insn->vector_bits, &vex_l) != 0) {
        return -1;
    }

    if (streq_ci(insn->mnemonic, "vcvtph2ps")) {
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG ||
            insn->op2.kind == AS_X86_OP_NONE || insn->has_imm8) {
            return -1;
        }
        return encode_vex_with_optional_imm(insn->op1.u.reg, AS_X86_REG_RAX, &insn->op2,
                                            0x13, AS_VEX_MAP_0F38, AS_VEX_PP_66, vex_l,
                                            0, 0, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    if (streq_ci(insn->mnemonic, "vcvtps2ph")) {
        if (insn->op_count != 2 || insn->op2.kind != AS_X86_OP_REG || !insn->has_imm8 ||
            insn->op1.kind == AS_X86_OP_NONE) {
            return -1;
        }
        return encode_vex_with_optional_imm(insn->op2.u.reg, AS_X86_REG_RAX, &insn->op1,
                                            0x1d, AS_VEX_MAP_0F3A, AS_VEX_PP_66, vex_l,
                                            1, insn->imm8, out, out_cap, out_len, errbuf,
                                            errbuf_sz);
    }

    return set_err(errbuf, errbuf_sz, "unsupported F16C mnemonic: %s", insn->mnemonic);
}
