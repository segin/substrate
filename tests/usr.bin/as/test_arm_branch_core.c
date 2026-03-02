#include "as_arm_branch.h"

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

static uint16_t read16le(const uint8_t *b) {
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static void run32(const as_arm_branch_insn_t *in, uint32_t exp, int exp_thumb, const char *name) {
    uint8_t out[8];
    size_t out_len = 0;
    int thumb = 0;
    char err[128];

    if (as_arm_encode_branch(in, out, sizeof(out), &out_len, &thumb, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }
    if (out_len != 4 || thumb != exp_thumb || read32le(out) != exp) {
        fprintf(stderr, "%s mismatch: len=%zu thumb=%d word=0x%08x exp=0x%08x\n",
                name, out_len, thumb, read32le(out), exp);
        fail("32-bit mismatch");
    }
}

static void run16(const as_arm_branch_insn_t *in, uint16_t exp, int exp_thumb, const char *name) {
    uint8_t out[8];
    size_t out_len = 0;
    int thumb = 0;
    char err[128];

    if (as_arm_encode_branch(in, out, sizeof(out), &out_len, &thumb, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }
    if (out_len != 2 || thumb != exp_thumb || read16le(out) != exp) {
        fprintf(stderr, "%s mismatch: len=%zu thumb=%d half=0x%04x exp=0x%04x\n",
                name, out_len, thumb, read16le(out), exp);
        fail("16-bit mismatch");
    }
}

int main(void) {
    as_arm_branch_insn_t in;

    memset(&in, 0, sizeof(in));
    in.cond = AS_ARM_COND_AL;

    in.mnemonic = "b";
    in.imm = 32;
    run32(&in, 0xea000008u, 0, "b +32");

    in.mnemonic = "bl";
    run32(&in, 0xeb000008u, 0, "bl +32");

    in.mnemonic = "b";
    in.cond = AS_ARM_COND_NE;
    run32(&in, 0x1a000008u, 0, "bne +32");

    in.cond = AS_ARM_COND_AL;
    in.mnemonic = "b";
    in.imm = -4;
    run32(&in, 0xeaffffffu, 0, "b -4");

    in.mnemonic = "bx";
    in.rm = 3;
    run32(&in, 0xe12fff13u, 0, "bx r3");

    in.mnemonic = "blx";
    run32(&in, 0xe12fff33u, 0, "blx r3");

    in.mnemonic = "cbz";
    in.rn = 1;
    in.imm = 32;
    run16(&in, 0xb181u, 1, "cbz r1,#32");

    in.mnemonic = "cbnz";
    run16(&in, 0xb981u, 1, "cbnz r1,#32");

    in.mnemonic = "tbb";
    in.rn = 1;
    in.rm = 2;
    run32(&in, 0xf002e8d1u, 1, "tbb [r1,r2]");

    in.mnemonic = "tbh";
    run32(&in, 0xf012e8d1u, 1, "tbh [r1,r2,lsl#1]");

    puts("ok");
    return 0;
}
