#include "as_x86_bmi1.h"

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

static void run_case(const as_x86_bmi1_insn_t *in, const uint8_t *exp, size_t exp_len,
                     const char *name) {
    uint8_t out[32];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_bmi1(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    expect_bytes(name, out, out_len, exp, exp_len);
}

int main(void) {
    as_x86_bmi1_insn_t in;

    memset(&in, 0, sizeof(in));
    in.mnemonic = "andn";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x68, 0xf2, 0xcb};
        run_case(&in, exp, sizeof(exp), "andn32");
    }

    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xe8, 0xf2, 0xcb};
        run_case(&in, exp, sizeof(exp), "andn64");
    }

    in.mnemonic = "bextr";
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x60, 0xf7, 0xca};
        run_case(&in, exp, sizeof(exp), "bextr32");
    }

    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xe0, 0xf7, 0xca};
        run_case(&in, exp, sizeof(exp), "bextr64");
    }

    memset(&in, 0, sizeof(in));
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);

    in.mnemonic = "blsi";
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x70, 0xf3, 0xda};
        run_case(&in, exp, sizeof(exp), "blsi32");
    }

    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xf0, 0xf3, 0xda};
        run_case(&in, exp, sizeof(exp), "blsi64");
    }

    in.mnemonic = "blsmsk";
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x70, 0xf3, 0xd2};
        run_case(&in, exp, sizeof(exp), "blsmsk32");
    }

    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xf0, 0xf3, 0xd2};
        run_case(&in, exp, sizeof(exp), "blsmsk64");
    }

    in.mnemonic = "blsr";
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x70, 0xf3, 0xca};
        run_case(&in, exp, sizeof(exp), "blsr32");
    }

    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xf0, 0xf3, 0xca};
        run_case(&in, exp, sizeof(exp), "blsr64");
    }

    in.mnemonic = "tzcnt";
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xf3, 0x0f, 0xbc, 0xca};
        run_case(&in, exp, sizeof(exp), "tzcnt32");
    }

    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xf3, 0x48, 0x0f, 0xbc, 0xca};
        run_case(&in, exp, sizeof(exp), "tzcnt64");
    }

    puts("ok");
    return 0;
}
