#include "as_arm_loadstore.h"

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

static void run4(const as_arm_loadstore_insn_t *in, uint32_t exp, const char *name) {
    uint8_t out[16];
    size_t out_len = 0;
    char err[128];

    if (as_arm_encode_loadstore(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }
    if (out_len != 4 || read32le(out) != exp) {
        fprintf(stderr, "%s mismatch: len=%zu word=0x%08x exp=0x%08x\n",
                name, out_len, read32le(out), exp);
        fail("4-byte mismatch");
    }
}

int main(void) {
    as_arm_loadstore_insn_t in;
    const struct {
        const char *mnemonic;
        uint32_t word;
    } descs[] = {
        {"ldrh", 0xe1d210b4u},
        {"strh", 0xe1c210b4u},
        {"ldrsb", 0xe1d210d4u},
        {"ldrsh", 0xe1d210f4u},
        {"ldrd", 0xe1c200d8u},
        {"strd", 0xe1c200f8u},
        {"ldm", 0xe8b1000cu},
        {"stmdb", 0xe921000cu},
        {"push", 0xe92d0030u},
        {"pop", 0xe8bd0030u},
        {"ldrex", 0xe1921f9fu},
        {"strex", 0xe1820f91u},
        {"ldrexb", 0xe1d21f9fu},
        {"strexb", 0xe1c20f91u},
        {"ldrexh", 0xe1f21f9fu},
        {"strexh", 0xe1e20f91u},
        {"ldrexd", 0xe1b20f9fu},
        {"strexd", 0xe1a23f90u},
        {"ldrt", 0xe4b21004u},
        {"strt", 0xe4a21004u},
        {"ldrbt", 0xe4f21004u},
        {"strbt", 0xe4e21004u},
        {"ldrht", 0xe0f210b4u},
        {"strht", 0xe0e210b4u},
        {"pld", 0xf5d1f004u},
        {"pldw", 0xf591f004u},
        {"pli", 0xf4d1f004u},
    };
    size_t i;

    memset(&in, 0, sizeof(in));
    in.cond = AS_ARM_COND_AL;
    in.rd = 1;
    in.rn = 2;
    in.imm = 4;

    in.mnemonic = "ldr";
    run4(&in, 0xe5921004u, "ldr off imm");

    in.pre_indexed = 1;
    in.writeback = 1;
    run4(&in, 0xe5b21004u, "ldr pre-index wb");

    in.pre_indexed = 0;
    in.writeback = 0;
    in.post_indexed = 1;
    run4(&in, 0xe4921004u, "ldr post-index");

    in.post_indexed = 0;
    in.reg_offset = 1;
    in.rm = 3;
    run4(&in, 0xe7921003u, "ldr reg offset");

    in.reg_offset = 0;
    in.pre_indexed = 0;
    in.writeback = 0;
    in.mnemonic = "str";
    run4(&in, 0xe5821004u, "str off imm");

    in.mnemonic = "ldrb";
    run4(&in, 0xe5d21004u, "ldrb off imm");

    in.mnemonic = "strb";
    run4(&in, 0xe5c21004u, "strb off imm");

    memset(&in, 0, sizeof(in));
    in.cond = AS_ARM_COND_AL;
    in.mnemonic = "ldr";
    in.rd = 1;
    in.emit_literal = 1;
    in.literal_value = 0x12345678u;
    {
        uint8_t out[16];
        size_t out_len = 0;
        char err[128];
        if (as_arm_encode_loadstore(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
            fprintf(stderr, "ldr literal encode error: %s\n", err);
            fail("literal encode failed");
        }
        if (out_len != 8 || read32le(out) != 0xe59f1000u || read32le(out + 4) != 0x12345678u) {
            fail("ldr literal mismatch");
        }
    }

    memset(&in, 0, sizeof(in));
    in.cond = AS_ARM_COND_AL;
    for (i = 0; i < sizeof(descs) / sizeof(descs[0]); ++i) {
        in.mnemonic = descs[i].mnemonic;
        run4(&in, descs[i].word, descs[i].mnemonic);
    }

    puts("ok");
    return 0;
}
