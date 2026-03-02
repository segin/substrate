#include "as_arm_vfp.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint32_t word;
    int cond_mutable;
} vfp_desc_t;

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

static int try_encode_vmrs_vmsr(const as_arm_vfp_insn_t *insn, uint32_t *out_word) {
    if (insn->rt > 15) {
        return -1;
    }

    if (insn->kind == AS_ARM_VFP_KIND_VMRS_FPSCR) {
        *out_word = ((uint32_t)(insn->cond & 0xfu) << 28) | 0x0ef10a10u |
                    ((uint32_t)(insn->rt & 0xfu) << 12);
        return 0;
    }

    if (insn->kind == AS_ARM_VFP_KIND_VMSR_FPSCR) {
        *out_word = ((uint32_t)(insn->cond & 0xfu) << 28) | 0x0ee10a10u |
                    ((uint32_t)(insn->rt & 0xfu) << 12);
        return 0;
    }

    return -1;
}

static const vfp_desc_t k_desc[] = {
    {"vadd.f32", 0xee300a81u, 1},
    {"vadd.f64", 0xee310b02u, 1},
    {"vsub.f32", 0xee721a62u, 1},
    {"vmul.f64", 0xee243b05u, 1},
    {"vdiv.f32", 0xee833a84u, 1},
    {"vnmul.f64", 0xee276b48u, 1},
    {"vmla.f32", 0xee454a25u, 1},
    {"vmls.f64", 0xee0a9b4bu, 1},
    {"vnmla.f32", 0xee166ac7u, 1},
    {"vnmls.f64", 0xee1dcb0eu, 1},
    {"vfma.f32", 0xeee87a28u, 1},
    {"vfms.f64", 0xeea0fbe1u, 1},
    {"vfnma.f32", 0xee999acau, 1},
    {"vfnms.f64", 0xeed32ba4u, 1},
    {"vmov.f32", 0xeef7aa00u, 1},
    {"vmov.f64", 0xeef05b00u, 1},
    {"vmov.s", 0xeeb0ba6bu, 1},
    {"vmov.arm_to_s", 0xee1c0a10u, 1},
    {"vmov.s_to_arm", 0xee0c1a90u, 1},
    {"vmov.arm2_to_d", 0xec532b38u, 1},
    {"vmov.d_to_arm2", 0xec454b39u, 1},
    {"vcmp.f32", 0xeeb4da6du, 1},
    {"vcmpe.f64", 0xeef4abebu, 1},
    {"vcvt.s32.f32", 0xeebdeaeeu, 1},
    {"vcvt.u32.f64", 0xeebcfbeeu, 1},
    {"vcvt.f32.s32", 0xeef8facfu, 1},
    {"vcvt.f64.u32", 0xeef8fb6fu, 1},
    {"vcvt.f32.f64", 0xeeb70bc0u, 1},
    {"vcvt.f64.f32", 0xeeb71ae0u, 1},
    {"vcvt.s32.f32.fix", 0xeebe0ac8u, 1},
    {"vcvt.f32.u32.fix", 0xeebb1accu, 1},
    {"vcvtb.f16.f32", 0xeeb32a62u, 1},
    {"vcvtt.f16.f32", 0xeeb33ae3u, 1},
    {"vabs.f32", 0xeeb04ae4u, 1},
    {"vneg.f64", 0xeeb18b49u, 1},
    {"vsqrt.f32", 0xeeb15ae5u, 1},
    {"vldr", 0xed906a04u, 1},
    {"vstr", 0xed81cb08u, 1},
    {"vldmia", 0xecf26a04u, 1},
    {"vstmia", 0xeca3db06u, 1},
    {"vpush", 0xed6d8a04u, 1},
    {"vpop", 0xecfd1b04u, 1},
    {"vcvta.s32.f32", 0xfebc0ae0u, 0},
    {"vcvtm.s32.f32", 0xfebf1ae1u, 0},
    {"vcvtn.s32.f32", 0xfebd2ae2u, 0},
    {"vcvtp.s32.f32", 0xfebe3ae3u, 0},
};

int as_arm_encode_vfp(const as_arm_vfp_insn_t *insn, uint8_t *out, size_t out_cap,
                      size_t *out_len, char *errbuf, size_t errbuf_sz) {
    uint32_t word;
    size_t i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    if (insn == NULL || insn->mnemonic == NULL || out == NULL || out_cap < 4) {
        return set_err(errbuf, errbuf_sz, "invalid VFP encode inputs");
    }

    if (try_encode_vmrs_vmsr(insn, &word) == 0) {
        put32le(out, word);
        if (out_len != NULL) {
            *out_len = 4;
        }
        return 0;
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

    return set_err(errbuf, errbuf_sz, "unsupported ARM VFP mnemonic: %s", insn->mnemonic);
}
