#include "as_x86_avx512bw.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static as_x86_operand_t reg_op(as_x86_reg_t r) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_REG;
    op.u.reg = r;
    return op;
}

static void expect_bytes(const char *name, const uint8_t *got, size_t got_len,
                         const uint8_t *exp, size_t exp_len) {
    if (got_len != exp_len || memcmp(got, exp, exp_len) != 0) {
        size_t i;
        fprintf(stderr, "mismatch for %s\n  got:", name);
        for (i = 0; i < got_len; ++i) {
            fprintf(stderr, " %02x", got[i]);
        }
        fprintf(stderr, "\n  exp:");
        for (i = 0; i < exp_len; ++i) {
            fprintf(stderr, " %02x", exp[i]);
        }
        fprintf(stderr, "\n");
        fail("byte mismatch");
    }
}

static void run_case(const as_x86_avx512bw_insn_t *in, const uint8_t *exp, size_t exp_len,
                     const char *name) {
    uint8_t out[64];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_avx512bw(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    expect_bytes(name, out, out_len, exp, exp_len);
}

int main(void) {
    as_x86_avx512bw_insn_t in;

    memset(&in, 0, sizeof(in));
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);

    in.mnemonic = "vpaddb";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6d, 0x48, 0xfc, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpaddb");
    }

    in.mnemonic = "vpackuswb";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6d, 0x48, 0x67, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpackuswb");
    }

    in.mnemonic = "vpunpcklbw";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6d, 0x48, 0x60, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpunpcklbw");
    }

    in.mnemonic = "vpshufb";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x6d, 0x48, 0x00, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpshufb");
    }

    in.mnemonic = "vpsllvw";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xed, 0x48, 0x12, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpsllvw");
    }

    in.mnemonic = "vpsrlvw";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xed, 0x48, 0x10, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpsrlvw");
    }

    in.mnemonic = "vpsravw";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xed, 0x48, 0x11, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpsravw");
    }

    in.mnemonic = "vdbpsadbw";
    in.has_imm8 = 1;
    in.imm8 = 1;
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x6d, 0x48, 0x42, 0xcb, 0x01};
        run_case(&in, exp, sizeof(exp), "vdbpsadbw");
    }

    in.mnemonic = "vpcmpb";
    in.op1 = reg_op(AS_X86_REG_RCX); /* k1 */
    in.op2 = reg_op(AS_X86_REG_RCX);
    in.op3 = reg_op(AS_X86_REG_RDX);
    in.imm8 = 1;
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x75, 0x48, 0x3f, 0xca, 0x01};
        run_case(&in, exp, sizeof(exp), "vpcmpb");
    }

    in.mnemonic = "vpcmpuw";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0xf5, 0x48, 0x3e, 0xca, 0x01};
        run_case(&in, exp, sizeof(exp), "vpcmpuw");
    }

    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX); /* k1 */
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3.kind = AS_X86_OP_NONE;
    in.has_imm8 = 0;

    in.mnemonic = "vpmovb2m";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7e, 0x48, 0x29, 0xca};
        run_case(&in, exp, sizeof(exp), "vpmovb2m");
    }

    in.mnemonic = "vpmovw2m";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xfe, 0x48, 0x29, 0xca};
        run_case(&in, exp, sizeof(exp), "vpmovw2m");
    }

    in.mnemonic = "vpmovm2b";
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX); /* k2 */
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7e, 0x48, 0x28, 0xca};
        run_case(&in, exp, sizeof(exp), "vpmovm2b");
    }

    in.mnemonic = "vpmovm2w";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xfe, 0x48, 0x28, 0xca};
        run_case(&in, exp, sizeof(exp), "vpmovm2w");
    }

    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);

    in.mnemonic = "vpermw";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xed, 0x48, 0x8d, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpermw");
    }

    in.mnemonic = "vpermi2w";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xed, 0x48, 0x75, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpermi2w");
    }

    in.mnemonic = "vpermt2w";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xed, 0x48, 0x7d, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpermt2w");
    }

    in.mnemonic = "vpblendmb";
    in.opmask = 1;
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x6d, 0x49, 0x66, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpblendmb");
    }

    in.mnemonic = "vpblendmw";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xed, 0x49, 0x66, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpblendmw");
    }

    in.opmask = 0;
    in.mnemonic = "vptestnmb";
    in.op1 = reg_op(AS_X86_REG_RCX); /* k1 */
    in.op2 = reg_op(AS_X86_REG_RCX);
    in.op3 = reg_op(AS_X86_REG_RDX);
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x76, 0x48, 0x26, 0xca};
        run_case(&in, exp, sizeof(exp), "vptestnmb");
    }

    in.mnemonic = "vptestnmw";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xf6, 0x48, 0x26, 0xca};
        run_case(&in, exp, sizeof(exp), "vptestnmw");
    }

    puts("ok");
    return 0;
}
