#include "as_arm_neon.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint32_t word;
    int cond_mutable;
} neon_desc_t;

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

static void put32le(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)(v & 0xffu);
    out[1] = (uint8_t)((v >> 8) & 0xffu);
    out[2] = (uint8_t)((v >> 16) & 0xffu);
    out[3] = (uint8_t)((v >> 24) & 0xffu);
}

static const neon_desc_t k_desc[] = {
    /* 6h data type coverage (representative typed forms). */
    {"vadd.i64", 0xf2320844u, 0},
    {"vadd.u32", 0xf2242846u, 0},
    {"vadd.u64", 0xf2364848u, 0},
    {"vadd.s8", 0xf208684au, 0},
    {"vadd.s32", 0xf22a884cu, 0},
    {"vadd.s64", 0xf23ca84eu, 0},
    {"vmul.p8", 0xf300e9f2u, 0},

    /* 6h arithmetic families. */
    {"vadd.i8", 0xf2020844u, 0},
    {"vsub.i16", 0xf3142846u, 0},
    {"vmul.i32", 0xf2264958u, 0},
    {"vmla.i16", 0xf218694au, 0},
    {"vmls.i16", 0xf31a894cu, 0},
    {"vaba.u8", 0xf30ca75eu, 0},
    {"vabd.s16", 0xf21ec760u, 0},
    {"vpadd.i16", 0xf2110b12u, 0},
    {"vpmax.u8", 0xf3021a03u, 0},
    {"vpmin.s16", 0xf2132a14u, 0},
    {"vmax.f32", 0xf2020f44u, 0},
    {"vmin.s16", 0xf2142656u, 0},
    {"vhadd.s16", 0xf2164048u, 0},
    {"vhsub.u16", 0xf318624au, 0},
    {"vrhadd.u8", 0xf30a814cu, 0},
    {"vqadd.s16", 0xf21ca05eu, 0},
    {"vqsub.u16", 0xf31ec270u, 0},
    {"vmull.s16", 0xf29eec0fu, 0},
    {"vmlal.s16", 0xf2d008a1u, 0},
    {"vmlsl.s16", 0xf2d22aa3u, 0},
    {"vqdmull.s16", 0xf2d44da5u, 0},
    {"vqdmlal.s16", 0xf2d669a7u, 0},
    {"vqdmlsl.s16", 0xf2d88ba9u, 0},
    {"vqdmulh.s16", 0xf25cabeeu, 0},
    {"vqrdmulh.s16", 0xf3120b44u, 0},
    {"vrshl.s16", 0xf2162544u, 0},
    {"vqrshl.s16", 0xf2184556u, 0},
    {"vshl.s16", 0xf21a6448u, 0},
    {"vqshl.s16", 0xf21c845au, 0},
    {"vshr.s16", 0xf29da05cu, 0},
    {"vrshr.u16", 0xf39ec25eu, 0},
    {"vsra.s16", 0xf29fe170u, 0},
    {"vrsra.u16", 0xf3df0372u, 0},
    {"vsli.16", 0xf3d42574u, 0},
    {"vsri.16", 0xf3db4476u, 0},
    {"vqshlu.s16", 0xf3d36678u, 0},
    {"vshll.s8", 0xf2ca8a3au, 0},
    {"vshrn.i16", 0xf28c0812u, 0},
    {"vqshrn.s16", 0xf28d1914u, 0},
    {"vqrshrn.s16", 0xf28e2956u, 0},
    {"vqshrun.s16", 0xf38d3818u, 0},
    {"vqrshrun.s16", 0xf38c485au, 0},
    {"vmovn.i16", 0xf3b2520cu, 0},
    {"vqmovn.s16", 0xf3b2628eu, 0},
    {"vqmovun.s16", 0xf3b27260u, 0},
    {"vmovl.s8", 0xf2c82a32u, 0},

    /* 6h logical families. */
    {"vand", 0xf2020154u, 0},
    {"vorr", 0xf2242156u, 0},
    {"veor", 0xf3064158u, 0},
    {"vbic", 0xf218615au, 0},
    {"vorn", 0xf23a815cu, 0},
    {"vbit", 0xf32ca15eu, 0},
    {"vbif", 0xf33ec170u, 0},
    {"vbsl", 0xf310e1f2u, 0},
    {"vmov", 0xf26201f2u, 0},
    {"vmvn", 0xf3f025e4u, 0},

    /* 6h compare/test families. */
    {"vceq.i8", 0xf3020854u, 0},
    {"vcge.s16", 0xf2142356u, 0},
    {"vcgt.u16", 0xf3164348u, 0},
    {"vcle.s16", 0xf3b561c8u, 0},
    {"vclt.s16", 0xf3b5824au, 0},
    {"vacge.f32", 0xf30cae5eu, 0},
    {"vacgt.f32", 0xf32ece70u, 0},
    {"vtst.8", 0xf200e8f2u, 0},

    /* 6h table/permute/interleave families. */
    {"vtbl.8", 0xf3b10802u, 0},
    {"vtbx.8", 0xf3b43845u, 0},
    {"vtrn.16", 0xf3b600c2u, 0},
    {"vuzp.8", 0xf3b22144u, 0},
    {"vzip.32", 0xf3ba41c6u, 0},
    {"vswp", 0xf3b20001u, 0},
    {"vext.8", 0xf2b8634au, 0},
    {"vrev16.8", 0xf3b0814au, 0},
    {"vrev32.16", 0xf3b4a0ccu, 0},
    {"vrev64.32", 0xf3b8c04eu, 0},

    /* 6h load/store and duplicate families. */
    {"vld1.8", 0xf420070fu, 0},
    {"vld2.8", 0xf421180fu, 0},
    {"vld3.8", 0xf422340fu, 0},
    {"vld4.8", 0xf423600fu, 0},
    {"vst1.8", 0xf404a70fu, 0},
    {"vst2.8", 0xf405b80fu, 0},
    {"vst3.8", 0xf406d40fu, 0},
    {"vst4.8", 0xf447000fu, 0},
    {"vdup.8", 0xf3b10c41u, 0},
    {"vdup.16", 0xeea20b30u, 1},

    /* 6h convert/reciprocal and accumulate families. */
    {"vcvt.f32.s32", 0xf3bb4646u, 0},
    {"vcvt.s32.f32", 0xf3bb6748u, 0},
    {"vrecpe.f32", 0xf3bb854au, 0},
    {"vrecps.f32", 0xf20caf5eu, 0},
    {"vrsqrte.f32", 0xf3bbc5ceu, 0},
    {"vrsqrts.f32", 0xf220eff2u, 0},
    {"vpadal.s8", 0xf3f00662u, 0},
    {"vpaddl.u16", 0xf3f422e4u, 0},
    {"vcnt.8", 0xf3f04566u, 0},
    {"vclz.i16", 0xf3f464e8u, 0},
    {"vcls.s16", 0xf3f4846au, 0},

    /* Extra polynomial widening example from typed corpus. */
    {"vmull.p8", 0xf28eee0fu, 0},
};

int as_arm_encode_neon(const as_arm_neon_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz) {
    size_t i;
    uint32_t word;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "invalid NEON encode inputs");
    }

    for (i = 0; i < sizeof(k_desc) / sizeof(k_desc[0]); ++i) {
        if (streq_ci(insn->mnemonic, k_desc[i].mnemonic)) {
            word = k_desc[i].word;
            if (k_desc[i].cond_mutable) {
                word = (word & 0x0fffffffu) | ((uint32_t)(insn->cond & 0xfu) << 28);
            }
            put32le(out, word);
            if (out_len != NULL) {
                *out_len = 4;
            }
            return 0;
        }
    }

    return set_err(errbuf, errbuf_sz, "unsupported ARM NEON mnemonic: %s", insn->mnemonic);
}
