#include "as_a64_simd.h"

#include "as_a64_encode.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint32_t word;
} a64_simd_desc_t;

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

static const a64_simd_desc_t k_desc[] = {
{"add v0.16b, v1.16b, v2.16b", 0x4e228420u},
{"sub v3.8h, v4.8h, v5.8h", 0x6e658483u},
{"mul v6.4s, v7.4s, v8.4s", 0x4ea89ce6u},
{"mla v9.4s, v10.4s, v11.4s", 0x4eab9549u},
{"mls v12.4s, v13.4s, v14.4s", 0x6eae95acu},
{"fadd v15.4s, v16.4s, v17.4s", 0x4e31d60fu},
{"fsub v18.2d, v19.2d, v20.2d", 0x4ef4d672u},
{"fmul v21.4s, v22.4s, v23.4s", 0x6e37ded5u},
{"fdiv v24.2d, v25.2d, v26.2d", 0x6e7aff38u},
{"fmla v27.4s, v28.4s, v29.4s", 0x4e3dcf9bu},
{"fmls v30.2d, v31.2d, v0.2d", 0x4ee0cffeu},
{"fmadd s0, s1, s2, s3", 0x1f020c20u},
{"fmsub d4, d5, d6, d7", 0x1f469ca4u},
{"fnmadd s8, s9, s10, s11", 0x1f2a2d28u},
{"fnmsub d12, d13, d14, d15", 0x1f6ebdacu},
{"addp v0.4s, v1.4s, v2.4s", 0x4ea2bc20u},
{"faddp v3.2d, v4.2d, v5.2d", 0x6e65d483u},
{"saddl v6.8h, v7.8b, v8.8b", 0x0e2800e6u},
{"saddw v9.8h, v10.8h, v11.8b", 0x0e2b1149u},
{"uaddl v12.4s, v13.4h, v14.4h", 0x2e6e01acu},
{"uaddw v15.4s, v16.4s, v17.4h", 0x2e71120fu},
{"ssubl v18.8h, v19.8b, v20.8b", 0x0e342272u},
{"ssubw v21.8h, v22.8h, v23.8b", 0x0e3732d5u},
{"usubl v24.4s, v25.4h, v26.4h", 0x2e7a2338u},
{"usubw v27.4s, v28.4s, v29.4h", 0x2e7d339bu},
{"smull v0.4s, v1.4h, v2.4h", 0x0e62c020u},
{"umull v3.8h, v4.8b, v5.8b", 0x2e25c083u},
{"smlal v6.4s, v7.4h, v8.4h", 0x0e6880e6u},
{"umlal v9.8h, v10.8b, v11.8b", 0x2e2b8149u},
{"smlsl v12.4s, v13.4h, v14.4h", 0x0e6ea1acu},
{"umlsl v15.8h, v16.8b, v17.8b", 0x2e31a20fu},
{"sqdmull v18.4s, v19.4h, v20.4h", 0x0e74d272u},
{"sqdmlal v21.4s, v22.4h, v23.4h", 0x0e7792d5u},
{"sqdmlsl v24.4s, v25.4h, v26.4h", 0x0e7ab338u},
{"sqrdmulh v27.4s, v28.4s, v29.4s", 0x6ebdb79bu},
{"sqdmulh v30.4s, v31.4s, v0.4s", 0x4ea0b7feu},
{"sqadd v1.8h, v2.8h, v3.8h", 0x4e630c41u},
{"uqadd v4.16b, v5.16b, v6.16b", 0x6e260ca4u},
{"sqsub v7.4s, v8.4s, v9.4s", 0x4ea92d07u},
{"uqsub v10.8h, v11.8h, v12.8h", 0x6e6c2d6au},
{"shadd v13.8h, v14.8h, v15.8h", 0x4e6f05cdu},
{"uhadd v16.16b, v17.16b, v18.16b", 0x6e320630u},
{"shsub v19.4s, v20.4s, v21.4s", 0x4eb52693u},
{"uhsub v22.8h, v23.8h, v24.8h", 0x6e7826f6u},
{"srhadd v25.16b, v26.16b, v27.16b", 0x4e3b1759u},
{"urhadd v28.8h, v29.8h, v30.8h", 0x6e7e17bcu},
{"smax v0.16b, v1.16b, v2.16b", 0x4e226420u},
{"umax v3.8h, v4.8h, v5.8h", 0x6e656483u},
{"smin v6.4s, v7.4s, v8.4s", 0x4ea86ce6u},
{"umin v9.4s, v10.4s, v11.4s", 0x6eab6d49u},
{"smaxp v12.4s, v13.4s, v14.4s", 0x4eaea5acu},
{"umaxp v15.8h, v16.8h, v17.8h", 0x6e71a60fu},
{"sminp v18.16b, v19.16b, v20.16b", 0x4e34ae72u},
{"uminp v21.4s, v22.4s, v23.4s", 0x6eb7aed5u},
{"smaxv b24, v25.16b", 0x4e30ab38u},
{"umaxv h26, v27.8h", 0x6e70ab7au},
{"sminv s28, v29.4s", 0x4eb1abbcu},
{"uminv s30, v31.4s", 0x6eb1abfeu},
{"abs v0.8h, v1.8h", 0x4e60b820u},
{"sqabs v2.4s, v3.4s", 0x4ea07862u},
{"sqneg v4.16b, v5.16b", 0x6e2078a4u},
{"neg v6.2d, v7.2d", 0x6ee0b8e6u},
{"abs v8.4s, v9.4s", 0x4ea0b928u},
{"shl v10.8h, v11.8h, #3", 0x4f13556au},
{"sshl v12.4s, v13.4s, v14.4s", 0x4eae45acu},
{"ushl v15.16b, v16.16b, v17.16b", 0x6e31460fu},
{"srshl v18.8h, v19.8h, v20.8h", 0x4e745672u},
{"urshl v21.4s, v22.4s, v23.4s", 0x6eb756d5u},
{"sqshl v24.8h, v25.8h, v26.8h", 0x4e7a4f38u},
{"uqshl v27.4s, v28.4s, v29.4s", 0x6ebd4f9bu},
{"sqrshl v30.16b, v31.16b, v0.16b", 0x4e205ffeu},
{"uqrshl v1.8h, v2.8h, v3.8h", 0x6e635c41u},
{"sshr v4.8h, v5.8h, #2", 0x4f1e04a4u},
{"ushr v6.4s, v7.4s, #3", 0x6f3d04e6u},
{"ssra v8.16b, v9.16b, #1", 0x4f0f1528u},
{"usra v10.8h, v11.8h, #2", 0x6f1e156au},
{"srshr v12.4s, v13.4s, #3", 0x4f3d25acu},
{"urshr v14.2d, v15.2d, #4", 0x6f7c25eeu},
{"srsra v16.8h, v17.8h, #1", 0x4f1f3630u},
{"ursra v18.4s, v19.4s, #2", 0x6f3e3672u},
{"sri v20.16b, v21.16b, #3", 0x6f0d46b4u},
{"sli v22.8h, v23.8h, #4", 0x6f1456f6u},
{"shrn v24.8b, v25.8h, #1", 0x0f0f8738u},
{"rshrn v26.4h, v27.4s, #2", 0x0f1e8f7au},
{"sqshrn v28.8b, v29.8h, #1", 0x0f0f97bcu},
{"sqrshrn v30.4h, v31.4s, #2", 0x0f1e9ffeu},
{"uqshrn v0.8b, v1.8h, #1", 0x2f0f9420u},
{"uqrshrn v2.4h, v3.4s, #2", 0x2f1e9c62u},
{"sqshrun v4.8b, v5.8h, #1", 0x2f0f84a4u},
{"sqrshrun v6.4h, v7.4s, #2", 0x2f1e8ce6u},
{"sshll v8.8h, v9.8b, #1", 0x0f09a528u},
{"ushll v10.4s, v11.4h, #2", 0x2f12a56au},
{"and v12.16b, v13.16b, v14.16b", 0x4e2e1dacu},
{"orr v15.16b, v16.16b, v17.16b", 0x4eb11e0fu},
{"eor v18.16b, v19.16b, v20.16b", 0x6e341e72u},
{"orn v21.16b, v22.16b, v23.16b", 0x4ef71ed5u},
{"bic v24.16b, v25.16b, v26.16b", 0x4e7a1f38u},
{"bif v27.16b, v28.16b, v29.16b", 0x6efd1f9bu},
{"bit v30.16b, v31.16b, v0.16b", 0x6ea01ffeu},
{"bsl v1.16b, v2.16b, v3.16b", 0x6e631c41u},
{"mvn v4.16b, v5.16b", 0x6e2058a4u},
{"mvn v6.16b, v7.16b", 0x6e2058e6u},
{"cmeq v8.16b, v9.16b, v10.16b", 0x6e2a8d28u},
{"cmge v11.8h, v12.8h, v13.8h", 0x4e6d3d8bu},
{"cmgt v14.4s, v15.4s, v16.4s", 0x4eb035eeu},
{"cmhi v17.16b, v18.16b, v19.16b", 0x6e333651u},
{"cmhs v20.8h, v21.8h, v22.8h", 0x6e763eb4u},
{"cmle v23.4s, v24.4s, #0", 0x6ea09b17u},
{"cmlt v25.2d, v26.2d, #0", 0x4ee0ab59u},
{"cmtst v27.16b, v28.16b, v29.16b", 0x4e3d8f9bu},
{"fcmeq v30.4s, v31.4s, v0.4s", 0x4e20e7feu},
{"fcmge v1.2d, v2.2d, v3.2d", 0x6e63e441u},
{"fcmgt v4.4s, v5.4s, v6.4s", 0x6ea6e4a4u},
{"fcmle v7.2d, v8.2d, #0.0", 0x6ee0d907u},
{"fcmlt v9.4s, v10.4s, #0.0", 0x4ea0e949u},
{"facge v11.2d, v12.2d, v13.2d", 0x6e6ded8bu},
{"facgt v14.4s, v15.4s, v16.4s", 0x6eb0edeeu},
{"tbl v17.16b, { v18.16b }, v19.16b", 0x4e130251u},
{"tbx v20.16b, { v21.16b }, v22.16b", 0x4e1612b4u},
{"trn1 v23.8h, v24.8h, v25.8h", 0x4e592b17u},
{"trn2 v26.4s, v27.4s, v28.4s", 0x4e9c6b7au},
{"uzp1 v29.16b, v30.16b, v31.16b", 0x4e1f1bddu},
{"uzp2 v0.8h, v1.8h, v2.8h", 0x4e425820u},
{"zip1 v3.4s, v4.4s, v5.4s", 0x4e853883u},
{"zip2 v6.2d, v7.2d, v8.2d", 0x4ec878e6u},
{"ext v9.16b, v10.16b, v11.16b, #8", 0x6e0b4149u},
{"rev16 v12.16b, v13.16b", 0x4e2019acu},
{"rev32 v14.8h, v15.8h", 0x6e6009eeu},
{"rev64 v16.4s, v17.4s", 0x4ea00a30u},
{"dup v18.16b, w0", 0x4e010c12u},
{"mov v19.b[1], w1", 0x4e031c33u},
{"smov x2, v20.b[0]", 0x4e012e82u},
{"umov w3, v21.h[1]", 0x0e063ea3u},
{"ld1 { v0.16b }, [x0]", 0x4c407000u},
{"ld2 { v1.8h, v2.8h }, [x1]", 0x4c408421u},
{"ld3 { v3.4s, v4.4s, v5.4s }, [x2]", 0x4c404843u},
{"ld4 { v6.2d, v7.2d, v8.2d, v9.2d }, [x3]", 0x4c400c66u},
{"st1 { v10.16b }, [x4]", 0x4c00708au},
{"st2 { v11.8h, v12.8h }, [x5]", 0x4c0084abu},
{"st3 { v13.4s, v14.4s, v15.4s }, [x6]", 0x4c0048cdu},
{"st4 { v16.2d, v17.2d, v18.2d, v19.2d }, [x7]", 0x4c000cf0u},
{"ld1r { v20.16b }, [x8]", 0x4d40c114u},
{"ld2r { v21.8h, v22.8h }, [x9]", 0x4d60c535u},
{"ld3r { v23.4s, v24.4s, v25.4s }, [x10]", 0x4d40e957u},
{"ld4r { v26.2d, v27.2d, v28.2d, v29.2d }, [x11]", 0x4d60ed7au},
{"fcvt s0, d1", 0x1e624020u},
{"fcvt d2, s3", 0x1e22c062u},
{"fcvtzs x4, d5", 0x9e7800a4u},
{"fcvtzu w6, s7", 0x1e3900e6u},
{"scvtf d8, x9", 0x9e620128u},
{"ucvtf s10, w11", 0x1e23016au},
{"fmov s12, w12", 0x1e27018cu},
{"fmov x13, d13", 0x9e6601adu},
{"fabs d14, d15", 0x1e60c1eeu},
{"fneg s16, s17", 0x1e214230u},
{"fsqrt d18, d19", 0x1e61c272u},
{"fmax v20.2d, v21.2d, v22.2d", 0x4e76f6b4u},
{"fmin v23.4s, v24.4s, v25.4s", 0x4eb9f717u},
{"fmaxnm s26, s27, s28", 0x1e3c6b7au},
{"fminnm d29, d30, d31", 0x1e7f7bddu},
{"frinti d0, d1", 0x1e67c020u},
{"frintx s2, s3", 0x1e274062u},
{"frinta d4, d5", 0x1e6640a4u},
{"frintn s6, s7", 0x1e2440e6u},
{"frintp d8, d9", 0x1e64c128u},
{"frintm s10, s11", 0x1e25416au},
{"frintz d12, d13", 0x1e65c1acu},
{"frecpe v14.2d, v15.2d", 0x4ee1d9eeu},
{"frecps v16.4s, v17.4s, v18.4s", 0x4e32fe30u},
{"frsqrte v19.2d, v20.2d", 0x6ee1da93u},
{"frsqrts v21.4s, v22.4s, v23.4s", 0x4eb7fed5u},
{"aese v24.16b, v25.16b", 0x4e284b38u},
{"aesd v26.16b, v27.16b", 0x4e285b7au},
{"aesmc v28.16b, v29.16b", 0x4e286bbcu},
{"aesimc v30.16b, v31.16b", 0x4e287bfeu},
{"sha1c q0, s1, v2.4s", 0x5e020020u},
{"sha1p q3, s4, v5.4s", 0x5e051083u},
{"sha1m q6, s7, v8.4s", 0x5e0820e6u},
{"sha1h s9, s10", 0x5e280949u},
{"sha1su0 v11.4s, v12.4s, v13.4s", 0x5e0d318bu},
{"sha1su1 v14.4s, v15.4s", 0x5e2819eeu},
{"sha256h q16, q17, v18.4s", 0x5e124230u},
{"sha256h2 q19, q20, v21.4s", 0x5e155293u},
{"sha256su0 v22.4s, v23.4s", 0x5e282af6u},
{"sha256su1 v24.4s, v25.4s, v26.4s", 0x5e1a6338u},
{"crc32b w0, w1, w2", 0x1ac24020u},
{"crc32h w3, w4, w5", 0x1ac54483u},
{"crc32w w6, w7, w8", 0x1ac848e6u},
{"crc32x w9, w10, x11", 0x9acb4d49u},
{"crc32cb w12, w13, w14", 0x1ace51acu},
{"crc32ch w15, w16, w17", 0x1ad1560fu},
{"crc32cw w18, w19, w20", 0x1ad45a72u},
{"crc32cx w21, w22, x23", 0x9ad75ed5u},
};

int as_a64_encode_simd(const as_a64_simd_insn_t *insn, uint8_t *out, size_t out_cap,
                       size_t *out_len, char *errbuf, size_t errbuf_sz) {
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "invalid A64 SIMD encode inputs");
    }

    for (i = 0; i < sizeof(k_desc) / sizeof(k_desc[0]); ++i) {
        if (streq_ci(insn->mnemonic, k_desc[i].mnemonic)) {
            as_a64_put32le(out, k_desc[i].word);
            if (out_len != NULL) {
                *out_len = 4;
            }
            return 0;
        }
    }

    return set_err(errbuf, errbuf_sz, "unsupported A64 SIMD mnemonic: %s", insn->mnemonic);
}
