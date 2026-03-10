#include "as_x86_fma.h"

#include "as_x86_vex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    uint8_t base_opcode;
    int packed_only;
} fma_family_t;

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

static uint8_t imm8_with_implicit_reg(as_x86_reg_t reg) {
    return (uint8_t)(((((unsigned)reg & 7u) | 0x8u) << 4));
}

static int encode_vex_with_imm8(as_x86_reg_t modrm_reg, as_x86_reg_t src1,
                                const as_x86_operand_t *src2, uint8_t opcode,
                                as_vex_map_t map, as_vex_pp_t pp, int vex_w, int vex_l,
                                uint8_t imm8, uint8_t *out, size_t out_cap, size_t *out_len,
                                char *errbuf, size_t errbuf_sz) {
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
    if (n >= out_cap) {
        return set_err(errbuf, errbuf_sz, "encoding overflow");
    }
    out[n++] = imm8;
    if (out_len != NULL) {
        *out_len = n;
    }
    return 0;
}

static size_t ascii_lower_copy(char *dst, size_t dst_cap, const char *src) {
    size_t i;

    if (dst_cap == 0) {
        return 0;
    }

    for (i = 0; src[i] != '\0' && i + 1 < dst_cap; ++i) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        dst[i] = c;
    }
    dst[i] = '\0';
    return i;
}

static int parse_form_offset(const char *p, uint8_t *offset, const char **rest) {
    if (p[0] == '1' && p[1] == '3' && p[2] == '2') {
        *offset = 0x00;
        *rest = p + 3;
        return 0;
    }
    if (p[0] == '2' && p[1] == '1' && p[2] == '3') {
        *offset = 0x10;
        *rest = p + 3;
        return 0;
    }
    if (p[0] == '2' && p[1] == '3' && p[2] == '1') {
        *offset = 0x20;
        *rest = p + 3;
        return 0;
    }
    return -1;
}

int as_x86_encode_fma(const as_x86_fma_insn_t *insn, uint8_t *out, size_t out_cap,
                      size_t *out_len, char *errbuf, size_t errbuf_sz) {
    static const fma_family_t families[] = {
        {"fmaddsub", 0x96, 1},
        {"fmsubadd", 0x97, 1},
        {"fmadd", 0x98, 0},
        {"fmsub", 0x9a, 0},
        {"fnmadd", 0x9c, 0},
        {"fnmsub", 0x9e, 0},
    };
    char mn[64];
    const fma_family_t *fam = NULL;
    uint8_t form_offset = 0;
    const char *p;
    const char *suffix;
    size_t i;
    uint8_t opcode;
    int vex_w = 0;
    int vex_l = 0;
    as_x86_vex_insn_t vex;
    static const struct {
        const char *name;
        uint8_t opcode;
    } fma4[] = {
        {"vfmaddsubps", 0x5c}, {"vfmaddsubpd", 0x5d},
        {"vfmsubaddps", 0x5e}, {"vfmsubaddpd", 0x5f},
        {"vfmaddps", 0x68},    {"vfmaddpd", 0x69},
        {"vfmaddss", 0x6a},    {"vfmaddsd", 0x6b},
        {"vfmsubps", 0x6c},    {"vfmsubpd", 0x6d},
        {"vfmsubss", 0x6e},    {"vfmsubsd", 0x6f},
        {"vfnmaddps", 0x78},   {"vfnmaddpd", 0x79},
        {"vfnmaddss", 0x7a},   {"vfnmaddsd", 0x7b},
        {"vfnmsubps", 0x7c},   {"vfnmsubpd", 0x7d},
        {"vfnmsubss", 0x7e},   {"vfnmsubsd", 0x7f},
    };

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || out == NULL || insn->mnemonic == NULL) {
        return -1;
    }

    if (insn->op_count == 4) {
        for (i = 0; i < sizeof(fma4) / sizeof(fma4[0]); ++i) {
            if (strcmp(insn->mnemonic, fma4[i].name) == 0) {
                if (insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
                    insn->op3.kind == AS_X86_OP_NONE || !insn->has_imm_reg) {
                    return -1;
                }
                if (insn->vector_bits == 128) {
                    vex_l = 0;
                } else if (insn->vector_bits == 256) {
                    vex_l = 1;
                } else {
                    return -1;
                }
                return encode_vex_with_imm8(insn->op1.u.reg, insn->op2.u.reg, &insn->op3,
                                            fma4[i].opcode, AS_VEX_MAP_0F3A, AS_VEX_PP_66, insn->vex_w,
                                            vex_l, imm8_with_implicit_reg(insn->imm_reg), out,
                                            out_cap, out_len, errbuf, errbuf_sz);
            }
        }
        return set_err(errbuf, errbuf_sz, "unsupported FMA mnemonic: %s", insn->mnemonic);
    }

    if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG ||
        insn->op2.kind != AS_X86_OP_REG || insn->op3.kind == AS_X86_OP_NONE) {
        return -1;
    }

    ascii_lower_copy(mn, sizeof(mn), insn->mnemonic);
    if (mn[0] != 'v') {
        return -1;
    }

    p = mn + 1;
    for (i = 0; i < sizeof(families) / sizeof(families[0]); ++i) {
        size_t n = strlen(families[i].name);
        if (strncmp(p, families[i].name, n) == 0) {
            fam = &families[i];
            p += n;
            break;
        }
    }
    if (fam == NULL) {
        return set_err(errbuf, errbuf_sz, "unsupported FMA mnemonic: %s", insn->mnemonic);
    }

    if (parse_form_offset(p, &form_offset, &suffix) != 0) {
        return -1;
    }

    if (strcmp(suffix, "ps") == 0) {
        if (insn->vector_bits == 128) {
            vex_l = 0;
        } else if (insn->vector_bits == 256) {
            vex_l = 1;
        } else {
            return -1;
        }
        vex_w = 0;
        opcode = (uint8_t)(fam->base_opcode + form_offset);
    } else if (strcmp(suffix, "pd") == 0) {
        if (insn->vector_bits == 128) {
            vex_l = 0;
        } else if (insn->vector_bits == 256) {
            vex_l = 1;
        } else {
            return -1;
        }
        vex_w = 1;
        opcode = (uint8_t)(fam->base_opcode + form_offset);
    } else if (strcmp(suffix, "ss") == 0 || strcmp(suffix, "sd") == 0) {
        if (fam->packed_only || insn->vector_bits != 128) {
            return -1;
        }
        vex_l = 0;
        vex_w = (suffix[1] == 'd') ? 1 : 0;
        opcode = (uint8_t)(fam->base_opcode + form_offset + 1);
    } else {
        return -1;
    }

    memset(&vex, 0, sizeof(vex));
    vex.opcode = opcode;
    vex.map = AS_VEX_MAP_0F38;
    vex.pp = AS_VEX_PP_66;
    vex.vex_w = vex_w;
    vex.vex_l = vex_l;
    vex.dst = insn->op1.u.reg;
    vex.src1 = insn->op2.u.reg;
    vex.src2 = insn->op3;

    return as_x86_encode_vex_3op(&vex, out, out_cap, out_len, errbuf, errbuf_sz);
}
