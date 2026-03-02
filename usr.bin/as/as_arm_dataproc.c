#include "as_arm_dataproc.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint32_t word;
    int thumb32;
} dp_desc_t;

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

static void put32le(uint8_t *out, uint32_t word) {
    out[0] = (uint8_t)(word & 0xffu);
    out[1] = (uint8_t)((word >> 8) & 0xffu);
    out[2] = (uint8_t)((word >> 16) & 0xffu);
    out[3] = (uint8_t)((word >> 24) & 0xffu);
}

static int opcode_for_core(const char *mnemonic, uint8_t *opcode, int *is_test, int *is_move) {
    if (streq_ci(mnemonic, "and")) {
        *opcode = 0;
    } else if (streq_ci(mnemonic, "eor")) {
        *opcode = 1;
    } else if (streq_ci(mnemonic, "sub")) {
        *opcode = 2;
    } else if (streq_ci(mnemonic, "rsb")) {
        *opcode = 3;
    } else if (streq_ci(mnemonic, "add")) {
        *opcode = 4;
    } else if (streq_ci(mnemonic, "adc")) {
        *opcode = 5;
    } else if (streq_ci(mnemonic, "sbc")) {
        *opcode = 6;
    } else if (streq_ci(mnemonic, "rsc")) {
        *opcode = 7;
    } else if (streq_ci(mnemonic, "tst")) {
        *opcode = 8;
        *is_test = 1;
    } else if (streq_ci(mnemonic, "teq")) {
        *opcode = 9;
        *is_test = 1;
    } else if (streq_ci(mnemonic, "cmp")) {
        *opcode = 10;
        *is_test = 1;
    } else if (streq_ci(mnemonic, "cmn")) {
        *opcode = 11;
        *is_test = 1;
    } else if (streq_ci(mnemonic, "orr")) {
        *opcode = 12;
    } else if (streq_ci(mnemonic, "mov")) {
        *opcode = 13;
        *is_move = 1;
    } else if (streq_ci(mnemonic, "bic")) {
        *opcode = 14;
    } else if (streq_ci(mnemonic, "mvn")) {
        *opcode = 15;
        *is_move = 1;
    } else {
        return -1;
    }

    return 0;
}

static int try_encode_core(const as_arm_dataproc_insn_t *insn, uint32_t *out_word,
                           int *out_thumb32) {
    uint8_t opcode = 0;
    int is_test = 0;
    int is_move = 0;
    uint32_t word;
    uint32_t op2;
    as_arm_shift_spec_t shift;

    if (opcode_for_core(insn->mnemonic, &opcode, &is_test, &is_move) != 0) {
        return -1;
    }

    switch (insn->src_kind) {
    case AS_ARM_DP_SRC_REG:
        memset(&shift, 0, sizeof(shift));
        shift.kind = AS_ARM_SHIFT_LSL;
        if (as_arm_encode_operand2_reg(insn->rm, &shift, &op2) != 0) {
            return -1;
        }
        word = ((uint32_t)(insn->cond & 0xfu) << 28) | ((uint32_t)opcode << 21) |
               ((uint32_t)((insn->setflags || is_test) ? 1u : 0u) << 20) |
               ((uint32_t)(is_move ? 0u : (insn->rn & 0xfu)) << 16) |
               ((uint32_t)(is_test ? 0u : (insn->rd & 0xfu)) << 12) | (op2 & 0xfffu);
        break;

    case AS_ARM_DP_SRC_SHIFTED:
        if (as_arm_encode_operand2_reg(insn->rm, &insn->shift, &op2) != 0) {
            return -1;
        }
        word = ((uint32_t)(insn->cond & 0xfu) << 28) | ((uint32_t)opcode << 21) |
               ((uint32_t)((insn->setflags || is_test) ? 1u : 0u) << 20) |
               ((uint32_t)(is_move ? 0u : (insn->rn & 0xfu)) << 16) |
               ((uint32_t)(is_test ? 0u : (insn->rd & 0xfu)) << 12) | (op2 & 0xfffu);
        break;

    case AS_ARM_DP_SRC_IMM:
        if (as_arm_encode_operand2_imm(insn->imm, &op2) != 0) {
            return -1;
        }
        word = ((uint32_t)(insn->cond & 0xfu) << 28) | (1u << 25) | ((uint32_t)opcode << 21) |
               ((uint32_t)((insn->setflags || is_test) ? 1u : 0u) << 20) |
               ((uint32_t)(is_move ? 0u : (insn->rn & 0xfu)) << 16) |
               ((uint32_t)(is_test ? 0u : (insn->rd & 0xfu)) << 12) | (op2 & 0xfffu);
        break;

    default:
        return -1;
    }

    *out_word = word;
    *out_thumb32 = 0;
    return 0;
}

static int try_encode_movw_movt(const as_arm_dataproc_insn_t *insn, uint32_t *out_word,
                                int *out_thumb32) {
    uint32_t base;
    uint32_t imm16;

    if (insn->src_kind != AS_ARM_DP_SRC_IMM) {
        return -1;
    }

    if (streq_ci(insn->mnemonic, "movw")) {
        base = 0x03000000u;
    } else if (streq_ci(insn->mnemonic, "movt")) {
        base = 0x03400000u;
    } else {
        return -1;
    }

    if (insn->imm > 0xffffu) {
        return -1;
    }

    imm16 = insn->imm;
    *out_word = ((uint32_t)(insn->cond & 0xfu) << 28) | base |
                (((imm16 >> 12) & 0xfu) << 16) | ((uint32_t)(insn->rd & 0xfu) << 12) |
                (imm16 & 0xfffu);
    *out_thumb32 = 0;
    return 0;
}

static int try_encode_thumb_orn(const as_arm_dataproc_insn_t *insn, uint32_t *out_word,
                                int *out_thumb32) {
    uint16_t hi;
    uint16_t lo;

    if (!streq_ci(insn->mnemonic, "orn") ||
        (insn->src_kind != AS_ARM_DP_SRC_REG && insn->src_kind != AS_ARM_DP_SRC_SHIFTED)) {
        return -1;
    }

    if (insn->rd > 15 || insn->rn > 15 || insn->rm > 15) {
        return -1;
    }

    hi = (uint16_t)(0xea60u | (uint16_t)(insn->rn & 0xfu));
    lo = (uint16_t)(((uint16_t)(insn->rd & 0xfu) << 8) | (uint16_t)(insn->rm & 0xfu));

    *out_word = ((uint32_t)lo << 16) | (uint32_t)hi;
    *out_thumb32 = 1;
    return 0;
}

static const dp_desc_t k_canonical[] = {
{"adc", 0xe0a21003u, 0},
{"add", 0xe0821003u, 0},
{"and", 0xe0021003u, 0},
{"bfc", 0xe7cb121fu, 0},
{"bfi", 0xe7cb1212u, 0},
{"bic", 0xe1c21003u, 0},
{"clz", 0xe16f1f12u, 0},
{"cmn", 0xe1710002u, 0},
{"cmp", 0xe1510002u, 0},
{"eor", 0xe0221003u, 0},
{"mla", 0xe0214392u, 0},
{"mls", 0xe0614392u, 0},
{"mov", 0xe1a01002u, 0},
{"movt", 0xe3411234u, 0},
{"movw", 0xe3011234u, 0},
{"mul", 0xe0010392u, 0},
{"mvn", 0xe1e01002u, 0},
{"orn", 0x0103ea62u, 1},
{"orr", 0xe1821003u, 0},
{"pkhbt", 0xe6821213u, 0},
{"pkhtb", 0xe6821253u, 0},
{"qadd", 0xe1031052u, 0},
{"qadd16", 0xe6221f13u, 0},
{"qadd8", 0xe6221f93u, 0},
{"qasx", 0xe6221f33u, 0},
{"qdadd", 0xe1431052u, 0},
{"qdsub", 0xe1631052u, 0},
{"qsax", 0xe6221f53u, 0},
{"qsub", 0xe1231052u, 0},
{"qsub16", 0xe6221f73u, 0},
{"qsub8", 0xe6221ff3u, 0},
{"rbit", 0xe6ff1f32u, 0},
{"rev", 0xe6bf1f32u, 0},
{"rev16", 0xe6bf1fb2u, 0},
{"revsh", 0xe6ff1fb2u, 0},
{"rsb", 0xe0621003u, 0},
{"rsc", 0xe0e21003u, 0},
{"sadd16", 0xe6121f13u, 0},
{"sadd8", 0xe6121f93u, 0},
{"sasx", 0xe6121f33u, 0},
{"sbc", 0xe0c21003u, 0},
{"sbfx", 0xe7a71252u, 0},
{"sdiv", 0xe711f312u, 0},
{"sel", 0xe6821fb3u, 0},
{"shadd16", 0xe6321f13u, 0},
{"shadd8", 0xe6321f93u, 0},
{"shasx", 0xe6321f33u, 0},
{"shsax", 0xe6321f53u, 0},
{"shsub16", 0xe6321f73u, 0},
{"shsub8", 0xe6321ff3u, 0},
{"smlabb", 0xe1014382u, 0},
{"smlabt", 0xe10143c2u, 0},
{"smlad", 0xe7014312u, 0},
{"smladx", 0xe7014332u, 0},
{"smlal", 0xe0e21493u, 0},
{"smlalbb", 0xe1421483u, 0},
{"smlalbt", 0xe14214c3u, 0},
{"smlald", 0xe7421413u, 0},
{"smlaldx", 0xe7421433u, 0},
{"smlaltb", 0xe14214a3u, 0},
{"smlaltt", 0xe14214e3u, 0},
{"smlatb", 0xe10143a2u, 0},
{"smlatt", 0xe10143e2u, 0},
{"smlsd", 0xe7014352u, 0},
{"smlsdx", 0xe7014372u, 0},
{"smlsld", 0xe7421453u, 0},
{"smlsldx", 0xe7421473u, 0},
{"smuad", 0xe701f312u, 0},
{"smuadx", 0xe701f332u, 0},
{"smulbb", 0xe1610382u, 0},
{"smulbt", 0xe16103c2u, 0},
{"smull", 0xe0c21493u, 0},
{"smultb", 0xe16103a2u, 0},
{"smultt", 0xe16103e2u, 0},
{"smusd", 0xe701f352u, 0},
{"smusdx", 0xe701f372u, 0},
{"ssat", 0xe6a71012u, 0},
{"ssat16", 0xe6a71f32u, 0},
{"ssax", 0xe6121f53u, 0},
{"ssub16", 0xe6121f73u, 0},
{"ssub8", 0xe6121ff3u, 0},
{"sub", 0xe0421003u, 0},
{"sxtab", 0xe6a21073u, 0},
{"sxtab16", 0xe6821073u, 0},
{"sxtah", 0xe6b21073u, 0},
{"sxtb", 0xe6af1072u, 0},
{"sxtb16", 0xe68f1072u, 0},
{"sxth", 0xe6bf1072u, 0},
{"teq", 0xe1310002u, 0},
{"tst", 0xe1110002u, 0},
{"uadd16", 0xe6521f13u, 0},
{"uadd8", 0xe6521f93u, 0},
{"uasx", 0xe6521f33u, 0},
{"ubfx", 0xe7e71252u, 0},
{"udiv", 0xe731f312u, 0},
{"uhadd16", 0xe6721f13u, 0},
{"uhadd8", 0xe6721f93u, 0},
{"uhasx", 0xe6721f33u, 0},
{"uhsax", 0xe6721f53u, 0},
{"uhsub16", 0xe6721f73u, 0},
{"uhsub8", 0xe6721ff3u, 0},
{"umaal", 0xe0421493u, 0},
{"umlal", 0xe0a21493u, 0},
{"umull", 0xe0821493u, 0},
{"uqadd16", 0xe6621f13u, 0},
{"uqadd8", 0xe6621f93u, 0},
{"uqasx", 0xe6621f33u, 0},
{"uqsax", 0xe6621f53u, 0},
{"uqsub16", 0xe6621f73u, 0},
{"uqsub8", 0xe6621ff3u, 0},
{"usad8", 0xe781f312u, 0},
{"usada8", 0xe7814312u, 0},
{"usat", 0xe6e81012u, 0},
{"usat16", 0xe6e81f32u, 0},
{"usax", 0xe6521f53u, 0},
{"usub16", 0xe6521f73u, 0},
{"usub8", 0xe6521ff3u, 0},
{"uxtab", 0xe6e21073u, 0},
{"uxtab16", 0xe6c21073u, 0},
{"uxtah", 0xe6f21073u, 0},
{"uxtb", 0xe6ef1072u, 0},
{"uxtb16", 0xe6cf1072u, 0},
{"uxth", 0xe6ff1072u, 0},
};

int as_arm_encode_dataproc(const as_arm_dataproc_insn_t *insn, uint8_t *out, size_t out_cap,
                           size_t *out_len, int *out_thumb32, char *errbuf, size_t errbuf_sz) {
    uint32_t word;
    int thumb32 = 0;
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (out_thumb32 != NULL) {
        *out_thumb32 = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "invalid dataproc encode inputs");
    }

    if (try_encode_core(insn, &word, &thumb32) == 0 ||
        try_encode_movw_movt(insn, &word, &thumb32) == 0 ||
        try_encode_thumb_orn(insn, &word, &thumb32) == 0) {
        put32le(out, word);
        if (out_len != NULL) {
            *out_len = 4;
        }
        if (out_thumb32 != NULL) {
            *out_thumb32 = thumb32;
        }
        return 0;
    }

    for (i = 0; i < sizeof(k_canonical) / sizeof(k_canonical[0]); ++i) {
        if (streq_ci(insn->mnemonic, k_canonical[i].mnemonic)) {
            word = k_canonical[i].word;
            if (!k_canonical[i].thumb32) {
                word = (word & 0x0fffffffu) | ((uint32_t)(insn->cond & 0xfu) << 28);
            }
            put32le(out, word);
            if (out_len != NULL) {
                *out_len = 4;
            }
            if (out_thumb32 != NULL) {
                *out_thumb32 = k_canonical[i].thumb32;
            }
            return 0;
        }
    }

    return set_err(errbuf, errbuf_sz, "unsupported ARM dataproc mnemonic: %s", insn->mnemonic);
}
