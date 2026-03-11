#include "as_x86_avx512f.h"

#include "as_x86_evex.h"
#include "as_x86_vex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    AVX512F_FORM_RRR = 0,
    AVX512F_FORM_RR,
    AVX512F_FORM_RRI,
    AVX512F_FORM_RRRI,
    AVX512F_FORM_EXTRACT,
    AVX512F_FORM_COMPRESS,
    AVX512F_FORM_EXPAND,
    AVX512F_FORM_GATHER,
    AVX512F_FORM_SCATTER,
    AVX512F_FORM_KCMP,
    AVX512F_FORM_REV_RR,
} avx512f_form_t;

typedef struct {
    const char *mnemonic;
    avx512f_form_t form;
    uint8_t opcode;
    as_evex_map_t map;
    as_evex_pp_t pp;
    int evex_w;
    int fixed_l2;
    int require_imm8;
    int require_mem_src;
    int require_mem_dst;
    int require_opmask;
} avx512f_desc_t;

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

static int startswith_ci(const char *s, const char *pfx) {
    size_t i;

    if (s == NULL || pfx == NULL) {
        return 0;
    }

    for (i = 0; pfx[i] != '\0'; ++i) {
        char a = s[i];
        char b = pfx[i];

        if (a == '\0') {
            return 0;
        }
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b + ('a' - 'A'));
        }
        if (a != b) {
            return 0;
        }
    }

    return 1;
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

static int encode_evex_desc(const avx512f_desc_t *desc, const as_x86_avx512f_insn_t *insn,
                            uint8_t *out, size_t out_cap, size_t *out_len,
                            char *errbuf, size_t errbuf_sz) {
    as_x86_evex_insn_t ev;
    uint8_t l2;
    as_x86_reg_t dst;
    as_x86_reg_t src1;
    as_x86_operand_t src2;

    if (desc == NULL || insn == NULL || out == NULL) {
        return -1;
    }

    if (desc->fixed_l2 >= 0) {
        l2 = (uint8_t)desc->fixed_l2;
    } else {
        unsigned vb = (insn->vector_bits == 0) ? 512 : insn->vector_bits;
        if (l2_from_vector_bits(vb, &l2) != 0) {
            return set_err(errbuf, errbuf_sz, "invalid vector width for %s", insn->mnemonic);
        }
    }

    if (!!insn->has_imm8 != !!desc->require_imm8) {
        return set_err(errbuf, errbuf_sz, "immediate form mismatch for %s", insn->mnemonic);
    }
    if (desc->require_opmask && insn->opmask == 0) {
        return set_err(errbuf, errbuf_sz, "opmask required for %s", insn->mnemonic);
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
    ev.evex_l2 = l2;

    switch (desc->form) {
    case AVX512F_FORM_RRR:
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            insn->op3.kind == AS_X86_OP_NONE) {
            return -1;
        }
        dst = insn->op1.u.reg;
        src1 = insn->op2.u.reg;
        src2 = insn->op3;
        break;

    case AVX512F_FORM_RR:
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind == AS_X86_OP_NONE) {
            return -1;
        }
        if (desc->require_mem_src && insn->op2.kind != AS_X86_OP_MEM) {
            return -1;
        }
        dst = insn->op1.u.reg;
        src1 = AS_X86_REG_RAX;
        src2 = insn->op2;
        break;

    case AVX512F_FORM_RRI:
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind == AS_X86_OP_NONE) {
            return -1;
        }
        dst = insn->op1.u.reg;
        src1 = AS_X86_REG_RAX;
        src2 = insn->op2;
        break;

    case AVX512F_FORM_RRRI:
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            insn->op3.kind == AS_X86_OP_NONE) {
            return -1;
        }
        dst = insn->op1.u.reg;
        src1 = insn->op2.u.reg;
        src2 = insn->op3;
        break;

    case AVX512F_FORM_EXTRACT:
        if (insn->op_count != 2 || insn->op1.kind == AS_X86_OP_NONE || insn->op2.kind != AS_X86_OP_REG) {
            return -1;
        }
        if (desc->require_mem_dst && insn->op1.kind != AS_X86_OP_MEM) {
            return -1;
        }
        dst = insn->op2.u.reg;
        src1 = AS_X86_REG_RAX;
        src2 = insn->op1;
        break;

    case AVX512F_FORM_COMPRESS:
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_MEM || insn->op2.kind != AS_X86_OP_REG) {
            return -1;
        }
        dst = insn->op2.u.reg;
        src1 = AS_X86_REG_RAX;
        src2 = insn->op1;
        break;

    case AVX512F_FORM_EXPAND:
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_MEM) {
            return -1;
        }
        dst = insn->op1.u.reg;
        src1 = AS_X86_REG_RAX;
        src2 = insn->op2;
        break;

    case AVX512F_FORM_GATHER:
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_MEM) {
            return -1;
        }
        dst = insn->op1.u.reg;
        src1 = AS_X86_REG_RAX;
        src2 = insn->op2;
        break;

    case AVX512F_FORM_SCATTER:
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_MEM || insn->op2.kind != AS_X86_OP_REG) {
            return -1;
        }
        dst = insn->op2.u.reg;
        src1 = AS_X86_REG_RAX;
        src2 = insn->op1;
        break;

    case AVX512F_FORM_KCMP:
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            insn->op3.kind == AS_X86_OP_NONE) {
            return -1;
        }
        dst = insn->op1.u.reg;
        src1 = insn->op2.u.reg;
        src2 = insn->op3;
        break;

    case AVX512F_FORM_REV_RR:
        if (insn->op_count != 2 || insn->op1.kind == AS_X86_OP_NONE || insn->op2.kind != AS_X86_OP_REG) {
            return -1;
        }
        dst = insn->op2.u.reg;
        src1 = AS_X86_REG_RAX;
        src2 = insn->op1;
        break;

    default:
        return -1;
    }

    ev.dst = dst;
    ev.src1 = src1;
    ev.src2 = src2;

    if (as_x86_encode_evex_3op(&ev, out, out_cap, out_len, errbuf, errbuf_sz) != 0) {
        return -1;
    }

    if (desc->require_imm8) {
        return append_imm8(out, out_cap, out_len, insn->imm8, errbuf, errbuf_sz);
    }

    return 0;
}

static int k_suffix_to_pp_w(char sfx, as_vex_pp_t *pp, int *w) {
    if (sfx == 'b') {
        *pp = AS_VEX_PP_66;
        *w = 0;
        return 0;
    }
    if (sfx == 'w') {
        *pp = AS_VEX_PP_NONE;
        *w = 0;
        return 0;
    }
    if (sfx == 'd') {
        *pp = AS_VEX_PP_66;
        *w = 1;
        return 0;
    }
    if (sfx == 'q') {
        *pp = AS_VEX_PP_NONE;
        *w = 1;
        return 0;
    }
    return -1;
}

static int encode_kop(const as_x86_avx512f_insn_t *insn, uint8_t *out, size_t out_cap,
                      size_t *out_len, char *errbuf, size_t errbuf_sz) {
    as_x86_vex_insn_t vex;
    as_vex_pp_t pp;
    int w;
    char sfx;
    uint8_t opcode;
    int has_imm = 0;

    if (insn == NULL || insn->mnemonic == NULL) {
        return -1;
    }

    memset(&vex, 0, sizeof(vex));
    vex.map = AS_VEX_MAP_0F;
    vex.dst = AS_X86_REG_RAX;
    vex.src1 = AS_X86_REG_RAX;

    if (startswith_ci(insn->mnemonic, "kmov")) {
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG) {
            return -1;
        }
        sfx = insn->mnemonic[4];
        if (k_suffix_to_pp_w(sfx, &pp, &w) != 0) {
            return -1;
        }
        vex.opcode = 0x90;
        vex.pp = pp;
        vex.vex_w = w;
        vex.vex_l = 0;
        vex.dst = insn->op1.u.reg;
        vex.src1 = AS_X86_REG_RAX;
        vex.src2 = insn->op2;
    } else if (startswith_ci(insn->mnemonic, "knot")) {
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG) {
            return -1;
        }
        sfx = insn->mnemonic[4];
        if (k_suffix_to_pp_w(sfx, &pp, &w) != 0) {
            return -1;
        }
        vex.opcode = 0x44;
        vex.pp = pp;
        vex.vex_w = w;
        vex.vex_l = 0;
        vex.dst = insn->op1.u.reg;
        vex.src1 = AS_X86_REG_RAX;
        vex.src2 = insn->op2;
    } else if (startswith_ci(insn->mnemonic, "kortest")) {
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG) {
            return -1;
        }
        sfx = insn->mnemonic[7];
        if (k_suffix_to_pp_w(sfx, &pp, &w) != 0) {
            return -1;
        }
        vex.opcode = 0x98;
        vex.pp = pp;
        vex.vex_w = w;
        vex.vex_l = 0;
        vex.dst = insn->op1.u.reg;
        vex.src1 = AS_X86_REG_RAX;
        vex.src2 = insn->op2;
    } else if (startswith_ci(insn->mnemonic, "ktest")) {
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG) {
            return -1;
        }
        sfx = insn->mnemonic[5];
        if (k_suffix_to_pp_w(sfx, &pp, &w) != 0) {
            return -1;
        }
        vex.opcode = 0x99;
        vex.pp = pp;
        vex.vex_w = w;
        vex.vex_l = 0;
        vex.dst = insn->op1.u.reg;
        vex.src1 = AS_X86_REG_RAX;
        vex.src2 = insn->op2;
    } else if (startswith_ci(insn->mnemonic, "kshiftl") || startswith_ci(insn->mnemonic, "kshiftr")) {
        if (insn->op_count != 2 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            !insn->has_imm8) {
            return -1;
        }
        sfx = insn->mnemonic[7];
        if (sfx != 'b' && sfx != 'w' && sfx != 'd' && sfx != 'q') {
            return -1;
        }
        vex.map = AS_VEX_MAP_0F3A;
        vex.opcode = startswith_ci(insn->mnemonic, "kshiftl") ? 0x32 : 0x30;
        vex.pp = AS_VEX_PP_66;
        vex.vex_w = (sfx == 'w' || sfx == 'q') ? 1 : 0;
        vex.vex_l = 0;
        vex.dst = insn->op1.u.reg;
        vex.src1 = AS_X86_REG_RAX;
        vex.src2 = insn->op2;
        has_imm = 1;
    } else if (startswith_ci(insn->mnemonic, "kunpck")) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            insn->op3.kind != AS_X86_OP_REG) {
            return -1;
        }
        if (streq_ci(insn->mnemonic, "kunpckbw")) {
            sfx = 'b';
        } else if (streq_ci(insn->mnemonic, "kunpckwd")) {
            sfx = 'w';
        } else if (streq_ci(insn->mnemonic, "kunpckdq")) {
            sfx = 'q';
        } else {
            return -1;
        }
        if (k_suffix_to_pp_w(sfx, &pp, &w) != 0) {
            return -1;
        }
        vex.opcode = 0x4b;
        vex.pp = pp;
        vex.vex_w = w;
        vex.vex_l = 1;
        vex.dst = insn->op1.u.reg;
        vex.src1 = insn->op2.u.reg;
        vex.src2 = insn->op3;
    } else if (startswith_ci(insn->mnemonic, "kadd")) {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            insn->op3.kind != AS_X86_OP_REG) {
            return -1;
        }
        sfx = insn->mnemonic[4];
        if (sfx == 'b') {
            return -1;
        }
        if (k_suffix_to_pp_w(sfx, &pp, &w) != 0) {
            return -1;
        }
        vex.opcode = 0x4a;
        vex.pp = pp;
        vex.vex_w = w;
        vex.vex_l = 1;
        vex.dst = insn->op1.u.reg;
        vex.src1 = insn->op2.u.reg;
        vex.src2 = insn->op3;
    } else {
        if (insn->op_count != 3 || insn->op1.kind != AS_X86_OP_REG || insn->op2.kind != AS_X86_OP_REG ||
            insn->op3.kind != AS_X86_OP_REG) {
            return -1;
        }

        if (startswith_ci(insn->mnemonic, "kandn")) {
            opcode = 0x42;
            sfx = insn->mnemonic[5];
        } else if (startswith_ci(insn->mnemonic, "kand")) {
            opcode = 0x41;
            sfx = insn->mnemonic[4];
        } else if (startswith_ci(insn->mnemonic, "kxor")) {
            opcode = 0x47;
            sfx = insn->mnemonic[4];
        } else if (startswith_ci(insn->mnemonic, "kxnor")) {
            opcode = 0x46;
            sfx = insn->mnemonic[5];
        } else if (startswith_ci(insn->mnemonic, "kor")) {
            opcode = 0x45;
            sfx = insn->mnemonic[3];
        } else {
            return -1;
        }

        if (k_suffix_to_pp_w(sfx, &pp, &w) != 0) {
            return -1;
        }

        vex.opcode = opcode;
        vex.pp = pp;
        vex.vex_w = w;
        vex.vex_l = 1;
        vex.dst = insn->op1.u.reg;
        vex.src1 = insn->op2.u.reg;
        vex.src2 = insn->op3;
    }

    if (as_x86_encode_vex_3op(&vex, out, out_cap, out_len, errbuf, errbuf_sz) != 0) {
        return -1;
    }

    if (has_imm) {
        return append_imm8(out, out_cap, out_len, insn->imm8, errbuf, errbuf_sz);
    }

    return 0;
}

int as_x86_encode_avx512f(const as_x86_avx512f_insn_t *insn, uint8_t *out, size_t out_cap,
                          size_t *out_len, char *errbuf, size_t errbuf_sz) {
    static const avx512f_desc_t descs[] = {
        {"vaddps", AVX512F_FORM_RRR, 0x58, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vaddpd", AVX512F_FORM_RRR, 0x58, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vsubps", AVX512F_FORM_RRR, 0x5c, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vsubpd", AVX512F_FORM_RRR, 0x5c, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vsub", AVX512F_FORM_RRR, 0x5c, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vmulps", AVX512F_FORM_RRR, 0x59, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vmulpd", AVX512F_FORM_RRR, 0x59, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vmul", AVX512F_FORM_RRR, 0x59, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vdivps", AVX512F_FORM_RRR, 0x5e, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vdivpd", AVX512F_FORM_RRR, 0x5e, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vdiv", AVX512F_FORM_RRR, 0x5e, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vmaxps", AVX512F_FORM_RRR, 0x5f, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vmaxpd", AVX512F_FORM_RRR, 0x5f, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vmax", AVX512F_FORM_RRR, 0x5f, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vcvtusi2ss", AVX512F_FORM_RRR, 0x7b, AS_EVEX_MAP_0F, AS_EVEX_PP_F3, 0, 0, 0, 0, 0, 0},
        {"vcvtusi2sd", AVX512F_FORM_RRR, 0x7b, AS_EVEX_MAP_0F, AS_EVEX_PP_F2, 1, 0, 0, 0, 0, 0},
        {"vminps", AVX512F_FORM_RRR, 0x5d, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vminpd", AVX512F_FORM_RRR, 0x5d, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vmin", AVX512F_FORM_RRR, 0x5d, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vsqrtps", AVX512F_FORM_RR, 0x51, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vsqrtpd", AVX512F_FORM_RR, 0x51, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vsqrt", AVX512F_FORM_RR, 0x51, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vrsqrt14ps", AVX512F_FORM_RR, 0x4e, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vrsqrt14pd", AVX512F_FORM_RR, 0x4e, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vrcp14ps", AVX512F_FORM_RR, 0x4c, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vrcp14pd", AVX512F_FORM_RR, 0x4c, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vfmadd132ps", AVX512F_FORM_RRR, 0x98, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vfmadd132pd", AVX512F_FORM_RRR, 0x98, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vcvtps2dq", AVX512F_FORM_RR, 0x5b, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vcvtps2udq", AVX512F_FORM_RR, 0x79, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vcvtpd2dq", AVX512F_FORM_RR, 0xe6, AS_EVEX_MAP_0F, AS_EVEX_PP_F2, 1, -1, 0, 0, 0, 0},
        {"vcvtpd2udq", AVX512F_FORM_RR, 0x79, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 1, -1, 0, 0, 0, 0},
        {"vcvtdq2ps", AVX512F_FORM_RR, 0x5b, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vcvtudq2ps", AVX512F_FORM_RR, 0x7a, AS_EVEX_MAP_0F, AS_EVEX_PP_F2, 0, -1, 0, 0, 0, 0},
        {"vbroadcastss", AVX512F_FORM_RR, 0x18, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0, 0},
        {"vbroadcastsd", AVX512F_FORM_RR, 0x19, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 1, 0, 0},
        {"vbroadcastf32x4", AVX512F_FORM_RR, 0x1a, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0, 0},
        {"vbroadcastf64x4", AVX512F_FORM_RR, 0x1b, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 1, 0, 0},
        {"vinsertf32x4", AVX512F_FORM_RRRI, 0x18, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vinsertf64x2", AVX512F_FORM_RRRI, 0x18, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vinsertf32x8", AVX512F_FORM_RRRI, 0x1a, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vinsertf64x4", AVX512F_FORM_RRRI, 0x1a, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vextractf32x4", AVX512F_FORM_EXTRACT, 0x19, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vextractf64x4", AVX512F_FORM_EXTRACT, 0x1b, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vpermps", AVX512F_FORM_RRR, 0x16, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpermpd", AVX512F_FORM_RRR, 0x16, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vpermpd", AVX512F_FORM_RRI, 0x01, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vpermd", AVX512F_FORM_RRR, 0x36, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpermq", AVX512F_FORM_RRR, 0x36, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vpermq", AVX512F_FORM_RRI, 0x00, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vpermi2ps", AVX512F_FORM_RRR, 0x77, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpermi2pd", AVX512F_FORM_RRR, 0x77, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vpermi2d", AVX512F_FORM_RRR, 0x76, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpermi2q", AVX512F_FORM_RRR, 0x76, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vpermt2ps", AVX512F_FORM_RRR, 0x7f, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpermt2pd", AVX512F_FORM_RRR, 0x7f, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vpermt2d", AVX512F_FORM_RRR, 0x7e, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpermt2q", AVX512F_FORM_RRR, 0x7e, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vshuff32x4", AVX512F_FORM_RRRI, 0x23, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vshuff64x2", AVX512F_FORM_RRRI, 0x23, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vshufi32x4", AVX512F_FORM_RRRI, 0x43, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vshufi64x2", AVX512F_FORM_RRRI, 0x43, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vpclmulqdq", AVX512F_FORM_RRRI, 0x44, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vcompressps", AVX512F_FORM_COMPRESS, 0x8a, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 1, 1},
        {"vcompresspd", AVX512F_FORM_COMPRESS, 0x8a, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 1, 1},
        {"vpcompressd", AVX512F_FORM_COMPRESS, 0x8b, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 1, 1},
        {"vpcompressq", AVX512F_FORM_COMPRESS, 0x8b, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 1, 1},
        {"vexpandps", AVX512F_FORM_EXPAND, 0x88, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0, 1},
        {"vexpandpd", AVX512F_FORM_EXPAND, 0x88, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 1, 0, 1},
        {"vpexpandd", AVX512F_FORM_EXPAND, 0x89, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0, 1},
        {"vpexpandq", AVX512F_FORM_EXPAND, 0x89, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 1, 0, 1},
        {"vgetexpps", AVX512F_FORM_RR, 0x42, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vgetexppd", AVX512F_FORM_RR, 0x42, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vgetexpss", AVX512F_FORM_RRR, 0x43, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, 0, 0, 0, 0, 0},
        {"vgetexpsd", AVX512F_FORM_RRR, 0x43, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 0, 0, 0, 0, 0},
        {"vgetmantps", AVX512F_FORM_RRI, 0x26, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vgetmantpd", AVX512F_FORM_RRI, 0x26, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vgetmantss", AVX512F_FORM_RRRI, 0x27, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, 0, 1, 0, 0, 0},
        {"vgetmantsh", AVX512F_FORM_RRRI, 0x27, AS_EVEX_MAP_0F3A, AS_EVEX_PP_NONE, 0, 0, 1, 0, 0, 0},
        {"vgetmantsd", AVX512F_FORM_RRRI, 0x27, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, 0, 1, 0, 0, 0},
        {"vscalefps", AVX512F_FORM_RRR, 0x2c, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vscalefpd", AVX512F_FORM_RRR, 0x2c, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vscalefss", AVX512F_FORM_RRR, 0x2d, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, 0, 0, 0, 0, 0},
        {"vscalefsd", AVX512F_FORM_RRR, 0x2d, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 0, 0, 0, 0, 0},
        {"vrcp14ss", AVX512F_FORM_RRR, 0x4d, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, 0, 0, 0, 0, 0},
        {"vrcp14sd", AVX512F_FORM_RRR, 0x4d, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 0, 0, 0, 0, 0},
        {"vrsqrt14ss", AVX512F_FORM_RRR, 0x4f, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, 0, 0, 0, 0, 0},
        {"vrsqrt14sd", AVX512F_FORM_RRR, 0x4f, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 0, 0, 0, 0, 0},
        {"vrcp28ss", AVX512F_FORM_RRR, 0xcb, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, 0, 0, 0, 0, 0},
        {"vrcp28sd", AVX512F_FORM_RRR, 0xcb, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 0, 0, 0, 0, 0},
        {"vrsqrt28ss", AVX512F_FORM_RRR, 0xcd, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, 0, 0, 0, 0, 0},
        {"vrsqrt28sd", AVX512F_FORM_RRR, 0xcd, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, 0, 0, 0, 0, 0},
        {"valignd", AVX512F_FORM_RRRI, 0x03, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vpdpbusd", AVX512F_FORM_RRR, 0x50, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpdpbusds", AVX512F_FORM_RRR, 0x51, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpdpwssd", AVX512F_FORM_RRR, 0x52, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpdpwssds", AVX512F_FORM_RRR, 0x53, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vdpphps", AVX512F_FORM_RRR, 0x52, AS_EVEX_MAP_0F38, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vcvtbiasph2bf8", AVX512F_FORM_RRR, 0x74, AS_EVEX_MAP_0F38, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vcvt2ps2phx", AVX512F_FORM_RRR, 0x67, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vcvt2ph2bf8", AVX512F_FORM_RRR, 0x74, AS_EVEX_MAP_0F38, AS_EVEX_PP_F2, 0, -1, 0, 0, 0, 0},
        {"vpdpwuud", AVX512F_FORM_RRR, 0xd2, AS_EVEX_MAP_0F38, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vpdpwuuds", AVX512F_FORM_RRR, 0xd3, AS_EVEX_MAP_0F38, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vpdpwusd", AVX512F_FORM_RRR, 0xd2, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpdpwusds", AVX512F_FORM_RRR, 0xd3, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpdpwsud", AVX512F_FORM_RRR, 0xd2, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0, 0},
        {"vpdpwsuds", AVX512F_FORM_RRR, 0xd3, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0, 0},
        {"vsm3msg1", AVX512F_FORM_RRR, 0xda, AS_EVEX_MAP_0F38, AS_EVEX_PP_NONE, 0, -1, 0, 0, 0, 0},
        {"vsm3msg2", AVX512F_FORM_RRR, 0xda, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vsm4key4", AVX512F_FORM_RRR, 0xda, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0, 0},
        {"vsm4rnds4", AVX512F_FORM_RRR, 0xda, AS_EVEX_MAP_0F38, AS_EVEX_PP_F2, 0, -1, 0, 0, 0, 0},
        {"vdpbf16ps", AVX512F_FORM_RRR, 0x52, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0, 0},
        {"vminmaxph", AVX512F_FORM_RRRI, 0x52, AS_EVEX_MAP_0F3A, AS_EVEX_PP_NONE, 0, -1, 1, 0, 0, 0},
        {"vminmaxps", AVX512F_FORM_RRRI, 0x52, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vminmaxbf16", AVX512F_FORM_RRRI, 0x52, AS_EVEX_MAP_0F3A, AS_EVEX_PP_F2, 0, -1, 1, 0, 0, 0},
        {"vminmaxpd", AVX512F_FORM_RRRI, 0x52, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vminmaxsd", AVX512F_FORM_RRRI, 0x53, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, 0, 1, 0, 0, 0},
        {"vblendmps", AVX512F_FORM_RRR, 0x65, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vblendmpd", AVX512F_FORM_RRR, 0x65, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vcvtne2ps2bf16", AVX512F_FORM_RRR, 0x72, AS_EVEX_MAP_0F38, AS_EVEX_PP_F2, 0, -1, 0, 0, 0, 0},
        {"vpshldvd", AVX512F_FORM_RRR, 0x71, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpshrdvd", AVX512F_FORM_RRR, 0x73, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vfixupimmps", AVX512F_FORM_RRRI, 0x54, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vfixupimmpd", AVX512F_FORM_RRRI, 0x54, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vfixupimmss", AVX512F_FORM_RRRI, 0x55, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, 0, 1, 0, 0, 0},
        {"vfixupimmsd", AVX512F_FORM_RRRI, 0x55, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, 0, 1, 0, 0, 0},
        {"vreducesh", AVX512F_FORM_RRRI, 0x57, AS_EVEX_MAP_0F3A, AS_EVEX_PP_NONE, 0, 0, 1, 0, 0, 0},
        {"vreducess", AVX512F_FORM_RRRI, 0x57, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, 0, 1, 0, 0, 0},
        {"vrndscaleps", AVX512F_FORM_RRI, 0x08, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vrndscalepd", AVX512F_FORM_RRI, 0x09, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vrndscaless", AVX512F_FORM_RRRI, 0x0a, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, 0, 1, 0, 0, 0},
        {"vrndscalesh", AVX512F_FORM_RRRI, 0x0a, AS_EVEX_MAP_0F3A, AS_EVEX_PP_NONE, 0, 0, 1, 0, 0, 0},
        {"vrndscalesd", AVX512F_FORM_RRRI, 0x0b, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, 0, 1, 0, 0, 0},
        {"vcmpps", AVX512F_FORM_KCMP, 0xc2, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 1, 0, 0, 0},
        {"vcmppd", AVX512F_FORM_KCMP, 0xc2, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vcmpss", AVX512F_FORM_KCMP, 0xc2, AS_EVEX_MAP_0F, AS_EVEX_PP_F3, 0, 0, 1, 0, 0, 0},
        {"vcmpsh", AVX512F_FORM_KCMP, 0xc2, AS_EVEX_MAP_0F, AS_EVEX_PP_F3, 0, 0, 1, 0, 0, 0},
        {"vcmpph", AVX512F_FORM_KCMP, 0xc2, AS_EVEX_MAP_0F, AS_EVEX_PP_NONE, 0, -1, 1, 0, 0, 0},
        {"vcmpsd", AVX512F_FORM_KCMP, 0xc2, AS_EVEX_MAP_0F, AS_EVEX_PP_F2, 1, 0, 1, 0, 0, 0},
        {"vpcmpgtd", AVX512F_FORM_KCMP, 0x66, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpcmpeqd", AVX512F_FORM_KCMP, 0x76, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vptestmd", AVX512F_FORM_KCMP, 0x27, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vptestnmd", AVX512F_FORM_KCMP, 0x27, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0, 0},
        {"vp2intersectd", AVX512F_FORM_KCMP, 0x68, AS_EVEX_MAP_0F38, AS_EVEX_PP_F2, 0, -1, 0, 0, 0, 0},
        {"vpcmpd", AVX512F_FORM_KCMP, 0x1f, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vpcmpq", AVX512F_FORM_KCMP, 0x1f, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vpcmpud", AVX512F_FORM_KCMP, 0x1e, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vpcmpuq", AVX512F_FORM_KCMP, 0x1e, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vpshldd", AVX512F_FORM_RRRI, 0x71, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vpshrdd", AVX512F_FORM_RRRI, 0x73, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vpandd", AVX512F_FORM_RRR, 0xdb, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpandnd", AVX512F_FORM_RRR, 0xdf, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpord", AVX512F_FORM_RRR, 0xeb, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpxord", AVX512F_FORM_RRR, 0xef, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpblendmd", AVX512F_FORM_RRR, 0x64, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vpblendmq", AVX512F_FORM_RRR, 0x64, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vpternlogd", AVX512F_FORM_RRRI, 0x25, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 0, -1, 1, 0, 0, 0},
        {"vpternlogq", AVX512F_FORM_RRRI, 0x25, AS_EVEX_MAP_0F3A, AS_EVEX_PP_66, 1, -1, 1, 0, 0, 0},
        {"vpmovdb", AVX512F_FORM_REV_RR, 0x31, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0, 0},
        {"vpmovdw", AVX512F_FORM_REV_RR, 0x33, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0, 0},
        {"vpmovqb", AVX512F_FORM_REV_RR, 0x32, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0, 0},
        {"vpmovqd", AVX512F_FORM_REV_RR, 0x35, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0, 0},
        {"vpmovqw", AVX512F_FORM_REV_RR, 0x34, AS_EVEX_MAP_0F38, AS_EVEX_PP_F3, 0, -1, 0, 0, 0, 0},
        {"vmovdqa32", AVX512F_FORM_RR, 0x6f, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 0, -1, 0, 0, 0, 0},
        {"vmovdqa64", AVX512F_FORM_RR, 0x6f, AS_EVEX_MAP_0F, AS_EVEX_PP_66, 1, -1, 0, 0, 0, 0},
        {"vmovdqu8", AVX512F_FORM_RR, 0x6f, AS_EVEX_MAP_0F, AS_EVEX_PP_F2, 0, -1, 0, 0, 0, 0},
        {"vmovdqu16", AVX512F_FORM_RR, 0x6f, AS_EVEX_MAP_0F, AS_EVEX_PP_F2, 1, -1, 0, 0, 0, 0},
        {"vmovdqu32", AVX512F_FORM_RR, 0x6f, AS_EVEX_MAP_0F, AS_EVEX_PP_F3, 0, -1, 0, 0, 0, 0},
        {"vmovdqu64", AVX512F_FORM_RR, 0x6f, AS_EVEX_MAP_0F, AS_EVEX_PP_F3, 1, -1, 0, 0, 0, 0},
        {"vgatherdps", AVX512F_FORM_GATHER, 0x92, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0, 1},
        {"vgatherdpd", AVX512F_FORM_GATHER, 0x92, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 1, 0, 1},
        {"vgatherqps", AVX512F_FORM_GATHER, 0x93, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0, 1},
        {"vgatherqpd", AVX512F_FORM_GATHER, 0x93, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 1, 0, 1},
        {"vpgatherdd", AVX512F_FORM_GATHER, 0x90, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0, 1},
        {"vpgatherdq", AVX512F_FORM_GATHER, 0x90, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 1, 0, 1},
        {"vpgatherqd", AVX512F_FORM_GATHER, 0x91, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 1, 0, 1},
        {"vpgatherqq", AVX512F_FORM_GATHER, 0x91, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 1, 0, 1},
        {"vscatterdps", AVX512F_FORM_SCATTER, 0xa2, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 1, 1},
        {"vscatterdpd", AVX512F_FORM_SCATTER, 0xa2, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 1, 1},
        {"vscatterqps", AVX512F_FORM_SCATTER, 0xa3, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 1, 1},
        {"vscatterqpd", AVX512F_FORM_SCATTER, 0xa3, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 1, 1},
        {"vpscatterdd", AVX512F_FORM_SCATTER, 0xa0, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 1, 1},
        {"vpscatterdq", AVX512F_FORM_SCATTER, 0xa0, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 1, 1},
        {"vpscatterqd", AVX512F_FORM_SCATTER, 0xa1, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 0, -1, 0, 0, 1, 1},
        {"vpscatterqq", AVX512F_FORM_SCATTER, 0xa1, AS_EVEX_MAP_0F38, AS_EVEX_PP_66, 1, -1, 0, 0, 1, 1},
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
            return encode_evex_desc(&descs[i], insn, out, out_cap, out_len, errbuf, errbuf_sz);
        }
    }

    if (insn->mnemonic[0] == 'k' || insn->mnemonic[0] == 'K') {
        return encode_kop(insn, out, out_cap, out_len, errbuf, errbuf_sz);
    }

    return set_err(errbuf, errbuf_sz, "unsupported AVX-512F mnemonic: %s", insn->mnemonic);
}
