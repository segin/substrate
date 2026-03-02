#include "as_a64_branch.h"

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

static void run_case(const as_a64_branch_insn_t *in, uint32_t exp, const char *name) {
    uint8_t out[8];
    size_t out_len = 0;
    char err[128];

    if (as_a64_encode_branch(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
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
    as_a64_branch_insn_t in;

    memset(&in, 0, sizeof(in));

    in.mnemonic = "b";
    in.imm = 16;
    run_case(&in, 0x14000004u, "b #16");

    in.mnemonic = "bl";
    in.imm = -16;
    run_case(&in, 0x97fffffcu, "bl #-16");

    in.mnemonic = "b.cond";
    in.cond = AS_A64_COND_EQ;
    in.imm = 32;
    run_case(&in, 0x54000100u, "b.eq #32");

    in.mnemonic = "br";
    in.rn = 3;
    run_case(&in, 0xd61f0060u, "br x3");

    in.mnemonic = "blr";
    in.rn = 4;
    run_case(&in, 0xd63f0080u, "blr x4");

    in.mnemonic = "ret";
    in.rn = 5;
    run_case(&in, 0xd65f00a0u, "ret x5");

    in.mnemonic = "cbz";
    in.is64 = 1;
    in.rt = 6;
    in.imm = 20;
    run_case(&in, 0xb40000a6u, "cbz x6,#20");

    in.mnemonic = "cbnz";
    in.is64 = 0;
    in.rt = 7;
    in.imm = -20;
    run_case(&in, 0x35ffff67u, "cbnz w7,#-20");

    in.mnemonic = "tbz";
    in.is64 = 1;
    in.rt = 8;
    in.bit = 5;
    in.imm = 28;
    run_case(&in, 0x362800e8u, "tbz x8,#5,#28");

    in.mnemonic = "tbnz";
    in.is64 = 0;
    in.rt = 9;
    in.bit = 12;
    in.imm = -28;
    run_case(&in, 0x3767ff29u, "tbnz w9,#12,#-28");

    in.mnemonic = "svc";
    in.imm16 = 0x1234;
    run_case(&in, 0xd4024681u, "svc #0x1234");

    in.mnemonic = "hvc";
    in.imm16 = 0x2345;
    run_case(&in, 0xd40468a2u, "hvc #0x2345");

    in.mnemonic = "smc";
    in.imm16 = 0x3456;
    run_case(&in, 0xd4068ac3u, "smc #0x3456");

    in.mnemonic = "brk";
    in.imm16 = 0x4567;
    run_case(&in, 0xd428ace0u, "brk #0x4567");

    in.mnemonic = "hlt";
    in.imm16 = 0x5678;
    run_case(&in, 0xd44acf00u, "hlt #0x5678");

    puts("ok");
    return 0;
}
