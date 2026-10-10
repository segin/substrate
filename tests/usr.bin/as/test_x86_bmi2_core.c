#include "as_x86_bmi2.h"

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

static void run_case(const as_x86_bmi2_insn_t *in, const uint8_t *exp, size_t exp_len,
                     const char *name) {
    uint8_t out[32];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_bmi2(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    expect_bytes(name, out, out_len, exp, exp_len);
}

int main(void) {
    as_x86_bmi2_insn_t in;

    memset(&in, 0, sizeof(in));
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);

    in.mnemonic = "bzhi";
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x60, 0xf5, 0xca};
        run_case(&in, exp, sizeof(exp), "bzhi32");
    }
    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xe0, 0xf5, 0xca};
        run_case(&in, exp, sizeof(exp), "bzhi64");
    }

    in.mnemonic = "mulx";
    in.op2 = reg_op(AS_X86_REG_RBX);
    in.op3 = reg_op(AS_X86_REG_RDX);
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x63, 0xf6, 0xca};
        run_case(&in, exp, sizeof(exp), "mulx32");
    }
    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xe3, 0xf6, 0xca};
        run_case(&in, exp, sizeof(exp), "mulx64");
    }

    in.mnemonic = "pdep";
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x6b, 0xf5, 0xcb};
        run_case(&in, exp, sizeof(exp), "pdep32");
    }
    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xeb, 0xf5, 0xcb};
        run_case(&in, exp, sizeof(exp), "pdep64");
    }

    in.mnemonic = "pext";
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x6a, 0xf5, 0xcb};
        run_case(&in, exp, sizeof(exp), "pext32");
    }
    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xea, 0xf5, 0xcb};
        run_case(&in, exp, sizeof(exp), "pext64");
    }

    in.mnemonic = "sarx";
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x62, 0xf7, 0xca};
        run_case(&in, exp, sizeof(exp), "sarx32");
    }
    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xe2, 0xf7, 0xca};
        run_case(&in, exp, sizeof(exp), "sarx64");
    }

    in.mnemonic = "shlx";
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x61, 0xf7, 0xca};
        run_case(&in, exp, sizeof(exp), "shlx32");
    }
    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xe1, 0xf7, 0xca};
        run_case(&in, exp, sizeof(exp), "shlx64");
    }

    in.mnemonic = "shrx";
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x63, 0xf7, 0xca};
        run_case(&in, exp, sizeof(exp), "shrx32");
    }
    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xe3, 0xf7, 0xca};
        run_case(&in, exp, sizeof(exp), "shrx64");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "rorx";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = 1;
    in.imm8 = 0x05;
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x7b, 0xf0, 0xca, 0x05};
        run_case(&in, exp, sizeof(exp), "rorx32");
    }
    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0xfb, 0xf0, 0xca, 0x05};
        run_case(&in, exp, sizeof(exp), "rorx64");
    }

    puts("ok");
    return 0;
}
