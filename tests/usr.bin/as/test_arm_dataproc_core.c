#include "as_arm_dataproc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static uint32_t read32le(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void run_case(const as_arm_dataproc_insn_t *in, uint32_t exp_word, int exp_thumb,
                     const char *name) {
    uint8_t out[8];
    size_t out_len = 0;
    int thumb32 = 0;
    char err[128];
    uint32_t got;

    if (as_arm_encode_dataproc(in, out, sizeof(out), &out_len, &thumb32, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }
    if (out_len != 4) {
        fail("unexpected output length");
    }
    got = read32le(out);
    if (got != exp_word || thumb32 != exp_thumb) {
        fprintf(stderr, "%s mismatch: got=0x%08x thumb=%d exp=0x%08x thumb=%d\n",
                name, got, thumb32, exp_word, exp_thumb);
        fail("word mismatch");
    }
}

int main(void) {
    as_arm_dataproc_insn_t in;
    const struct {
        const char *mnemonic;
        uint32_t word;
        int thumb32;
    } canonical[] = {
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
    size_t i;

    memset(&in, 0, sizeof(in));
    in.mnemonic = "add";
    in.cond = AS_ARM_COND_AL;
    in.src_kind = AS_ARM_DP_SRC_REG;
    in.rd = 1;
    in.rn = 2;
    in.rm = 3;
    run_case(&in, 0xe0821003u, 0, "add reg");

    in.src_kind = AS_ARM_DP_SRC_IMM;
    in.imm = 5;
    run_case(&in, 0xe2821005u, 0, "add imm");

    in.src_kind = AS_ARM_DP_SRC_SHIFTED;
    in.rm = 3;
    in.shift.kind = AS_ARM_SHIFT_LSL;
    in.shift.by_reg = 0;
    in.shift.amount = 2;
    run_case(&in, 0xe0821103u, 0, "add shifted");

    in.cond = AS_ARM_COND_NE;
    in.src_kind = AS_ARM_DP_SRC_REG;
    run_case(&in, 0x10821003u, 0, "add cond");

    in.mnemonic = "cmp";
    in.cond = AS_ARM_COND_AL;
    in.src_kind = AS_ARM_DP_SRC_IMM;
    in.rn = 1;
    in.imm = 7;
    run_case(&in, 0xe3510007u, 0, "cmp imm");

    in.mnemonic = "mov";
    in.src_kind = AS_ARM_DP_SRC_IMM;
    in.rd = 1;
    in.imm = 255;
    run_case(&in, 0xe3a010ffu, 0, "mov imm");

    in.mnemonic = "movw";
    in.src_kind = AS_ARM_DP_SRC_IMM;
    in.rd = 1;
    in.imm = 0x1234;
    run_case(&in, 0xe3011234u, 0, "movw");

    in.mnemonic = "orn";
    in.src_kind = AS_ARM_DP_SRC_REG;
    in.rd = 1;
    in.rn = 2;
    in.rm = 3;
    run_case(&in, 0x0103ea62u, 1, "orn thumb");

    memset(&in, 0, sizeof(in));
    in.cond = AS_ARM_COND_AL;
    in.src_kind = AS_ARM_DP_SRC_CANONICAL;

    for (i = 0; i < sizeof(canonical) / sizeof(canonical[0]); ++i) {
        in.mnemonic = canonical[i].mnemonic;
        run_case(&in, canonical[i].word, canonical[i].thumb32, canonical[i].mnemonic);
    }

    puts("ok");
    return 0;
}
