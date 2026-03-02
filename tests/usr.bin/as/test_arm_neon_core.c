#include "as_arm_neon.h"

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

static void run_case(const as_arm_neon_insn_t *in, uint32_t exp, const char *name) {
    uint8_t out[8];
    size_t out_len = 0;
    char err[128];

    if (as_arm_encode_neon(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
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
    as_arm_neon_insn_t in;
    const struct {
        const char *mnemonic;
        uint32_t word;
    } cases[] = {
        {"vadd.i64", 0xf2320844u},
        {"vadd.u32", 0xf2242846u},
        {"vadd.u64", 0xf2364848u},
        {"vadd.s8", 0xf208684au},
        {"vadd.s32", 0xf22a884cu},
        {"vadd.s64", 0xf23ca84eu},
        {"vmul.p8", 0xf300e9f2u},
        {"vadd.i8", 0xf2020844u},
        {"vsub.i16", 0xf3142846u},
        {"vmul.i32", 0xf2264958u},
        {"vmla.i16", 0xf218694au},
        {"vmls.i16", 0xf31a894cu},
        {"vaba.u8", 0xf30ca75eu},
        {"vabd.s16", 0xf21ec760u},
        {"vpadd.i16", 0xf2110b12u},
        {"vpmax.u8", 0xf3021a03u},
        {"vpmin.s16", 0xf2132a14u},
        {"vmax.f32", 0xf2020f44u},
        {"vmin.s16", 0xf2142656u},
        {"vhadd.s16", 0xf2164048u},
        {"vhsub.u16", 0xf318624au},
        {"vrhadd.u8", 0xf30a814cu},
        {"vqadd.s16", 0xf21ca05eu},
        {"vqsub.u16", 0xf31ec270u},
        {"vmull.s16", 0xf29eec0fu},
        {"vmlal.s16", 0xf2d008a1u},
        {"vmlsl.s16", 0xf2d22aa3u},
        {"vqdmull.s16", 0xf2d44da5u},
        {"vqdmlal.s16", 0xf2d669a7u},
        {"vqdmlsl.s16", 0xf2d88ba9u},
        {"vqdmulh.s16", 0xf25cabeeu},
        {"vqrdmulh.s16", 0xf3120b44u},
        {"vrshl.s16", 0xf2162544u},
        {"vqrshl.s16", 0xf2184556u},
        {"vshl.s16", 0xf21a6448u},
        {"vqshl.s16", 0xf21c845au},
        {"vshr.s16", 0xf29da05cu},
        {"vrshr.u16", 0xf39ec25eu},
        {"vsra.s16", 0xf29fe170u},
        {"vrsra.u16", 0xf3df0372u},
        {"vsli.16", 0xf3d42574u},
        {"vsri.16", 0xf3db4476u},
        {"vqshlu.s16", 0xf3d36678u},
        {"vshll.s8", 0xf2ca8a3au},
        {"vshrn.i16", 0xf28c0812u},
        {"vqshrn.s16", 0xf28d1914u},
        {"vqrshrn.s16", 0xf28e2956u},
        {"vqshrun.s16", 0xf38d3818u},
        {"vqrshrun.s16", 0xf38c485au},
        {"vmovn.i16", 0xf3b2520cu},
        {"vqmovn.s16", 0xf3b2628eu},
        {"vqmovun.s16", 0xf3b27260u},
        {"vmovl.s8", 0xf2c82a32u},
        {"vand", 0xf2020154u},
        {"vorr", 0xf2242156u},
        {"veor", 0xf3064158u},
        {"vbic", 0xf218615au},
        {"vorn", 0xf23a815cu},
        {"vbit", 0xf32ca15eu},
        {"vbif", 0xf33ec170u},
        {"vbsl", 0xf310e1f2u},
        {"vmov", 0xf26201f2u},
        {"vmvn", 0xf3f025e4u},
        {"vceq.i8", 0xf3020854u},
        {"vcge.s16", 0xf2142356u},
        {"vcgt.u16", 0xf3164348u},
        {"vcle.s16", 0xf3b561c8u},
        {"vclt.s16", 0xf3b5824au},
        {"vacge.f32", 0xf30cae5eu},
        {"vacgt.f32", 0xf32ece70u},
        {"vtst.8", 0xf200e8f2u},
        {"vtbl.8", 0xf3b10802u},
        {"vtbx.8", 0xf3b43845u},
        {"vtrn.16", 0xf3b600c2u},
        {"vuzp.8", 0xf3b22144u},
        {"vzip.32", 0xf3ba41c6u},
        {"vswp", 0xf3b20001u},
        {"vext.8", 0xf2b8634au},
        {"vrev16.8", 0xf3b0814au},
        {"vrev32.16", 0xf3b4a0ccu},
        {"vrev64.32", 0xf3b8c04eu},
        {"vld1.8", 0xf420070fu},
        {"vld2.8", 0xf421180fu},
        {"vld3.8", 0xf422340fu},
        {"vld4.8", 0xf423600fu},
        {"vst1.8", 0xf404a70fu},
        {"vst2.8", 0xf405b80fu},
        {"vst3.8", 0xf406d40fu},
        {"vst4.8", 0xf447000fu},
        {"vdup.8", 0xf3b10c41u},
        {"vdup.16", 0xeea20b30u},
        {"vcvt.f32.s32", 0xf3bb4646u},
        {"vcvt.s32.f32", 0xf3bb6748u},
        {"vrecpe.f32", 0xf3bb854au},
        {"vrecps.f32", 0xf20caf5eu},
        {"vrsqrte.f32", 0xf3bbc5ceu},
        {"vrsqrts.f32", 0xf220eff2u},
        {"vpadal.s8", 0xf3f00662u},
        {"vpaddl.u16", 0xf3f422e4u},
        {"vcnt.8", 0xf3f04566u},
        {"vclz.i16", 0xf3f464e8u},
        {"vcls.s16", 0xf3f4846au},
        {"vmull.p8", 0xf28eee0fu},
    };
    size_t i;

    memset(&in, 0, sizeof(in));
    in.cond = AS_ARM_COND_AL;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        in.mnemonic = cases[i].mnemonic;
        run_case(&in, cases[i].word, cases[i].mnemonic);
    }

    in.mnemonic = "vdup.16";
    in.cond = AS_ARM_COND_NE;
    run_case(&in, 0x1ea20b30u, "vdup.16 cond");

    puts("ok");
    return 0;
}
