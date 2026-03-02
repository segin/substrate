#include "as_arm_vfp.h"

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

static void run_case(const as_arm_vfp_insn_t *in, uint32_t exp, const char *name) {
    uint8_t out[8];
    size_t out_len = 0;
    char err[128];

    if (as_arm_encode_vfp(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }
    if (out_len != 4 || read32le(out) != exp) {
        fprintf(stderr, "%s mismatch: len=%zu word=0x%08x exp=0x%08x\n",
                name, out_len, read32le(out), exp);
        fail("word mismatch");
    }
}

int main(void) {
    as_arm_vfp_insn_t in;
    const struct {
        const char *mnemonic;
        uint32_t word;
    } canonical[] = {
        {"vadd.f32", 0xee300a81u},
        {"vadd.f64", 0xee310b02u},
        {"vsub.f32", 0xee721a62u},
        {"vmul.f64", 0xee243b05u},
        {"vdiv.f32", 0xee833a84u},
        {"vnmul.f64", 0xee276b48u},
        {"vmla.f32", 0xee454a25u},
        {"vmls.f64", 0xee0a9b4bu},
        {"vnmla.f32", 0xee166ac7u},
        {"vnmls.f64", 0xee1dcb0eu},
        {"vfma.f32", 0xeee87a28u},
        {"vfms.f64", 0xeea0fbe1u},
        {"vfnma.f32", 0xee999acau},
        {"vfnms.f64", 0xeed32ba4u},
        {"vmov.f32", 0xeef7aa00u},
        {"vmov.f64", 0xeef05b00u},
        {"vmov.s", 0xeeb0ba6bu},
        {"vmov.arm_to_s", 0xee1c0a10u},
        {"vmov.s_to_arm", 0xee0c1a90u},
        {"vmov.arm2_to_d", 0xec532b38u},
        {"vmov.d_to_arm2", 0xec454b39u},
        {"vcmp.f32", 0xeeb4da6du},
        {"vcmpe.f64", 0xeef4abebu},
        {"vcvt.s32.f32", 0xeebdeaeeu},
        {"vcvt.u32.f64", 0xeebcfbeeu},
        {"vcvt.f32.s32", 0xeef8facfu},
        {"vcvt.f64.u32", 0xeef8fb6fu},
        {"vcvt.f32.f64", 0xeeb70bc0u},
        {"vcvt.f64.f32", 0xeeb71ae0u},
        {"vcvt.s32.f32.fix", 0xeebe0ac8u},
        {"vcvt.f32.u32.fix", 0xeebb1accu},
        {"vcvtb.f16.f32", 0xeeb32a62u},
        {"vcvtt.f16.f32", 0xeeb33ae3u},
        {"vabs.f32", 0xeeb04ae4u},
        {"vneg.f64", 0xeeb18b49u},
        {"vsqrt.f32", 0xeeb15ae5u},
        {"vldr", 0xed906a04u},
        {"vstr", 0xed81cb08u},
        {"vldmia", 0xecf26a04u},
        {"vstmia", 0xeca3db06u},
        {"vpush", 0xed6d8a04u},
        {"vpop", 0xecfd1b04u},
        {"vcvta.s32.f32", 0xfebc0ae0u},
        {"vcvtm.s32.f32", 0xfebf1ae1u},
        {"vcvtn.s32.f32", 0xfebd2ae2u},
        {"vcvtp.s32.f32", 0xfebe3ae3u},
    };
    size_t i;

    memset(&in, 0, sizeof(in));
    in.cond = AS_ARM_COND_AL;
    in.kind = AS_ARM_VFP_KIND_VMRS_FPSCR;
    in.rt = 6;
    in.mnemonic = "vmrs";
    run_case(&in, 0xeef16a10u, "vmrs r6,fpscr");

    in.kind = AS_ARM_VFP_KIND_VMSR_FPSCR;
    in.rt = 7;
    in.mnemonic = "vmsr";
    run_case(&in, 0xeee17a10u, "vmsr fpscr,r7");

    in.kind = AS_ARM_VFP_KIND_VMRS_FPSCR;
    in.cond = AS_ARM_COND_NE;
    in.rt = 6;
    in.mnemonic = "vmrs";
    run_case(&in, 0x1ef16a10u, "vmrsne r6,fpscr");

    memset(&in, 0, sizeof(in));
    in.cond = AS_ARM_COND_AL;
    in.kind = AS_ARM_VFP_KIND_CANONICAL;

    for (i = 0; i < sizeof(canonical) / sizeof(canonical[0]); ++i) {
        in.mnemonic = canonical[i].mnemonic;
        run_case(&in, canonical[i].word, canonical[i].mnemonic);
    }

    puts("ok");
    return 0;
}
