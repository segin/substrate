#include "as_x86_f16c.h"

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

static as_x86_operand_t mem_op(as_x86_reg_t base) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.has_base = 1;
    op.u.mem.base = base;
    op.u.mem.scale = 1;
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

static void run_case(const as_x86_f16c_insn_t *in, const uint8_t *exp, size_t exp_len,
                     const char *name) {
    uint8_t out[32];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_f16c(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    expect_bytes(name, out, out_len, exp, exp_len);
}

int main(void) {
    as_x86_f16c_insn_t in;

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vcvtph2ps";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.vector_bits = 128;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x79, 0x13, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtph2ps reg 128");
    }

    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x7d, 0x13, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtph2ps reg 256");
    }

    in.op2 = mem_op(AS_X86_REG_RAX);
    in.vector_bits = 128;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x79, 0x13, 0x08};
        run_case(&in, exp, sizeof(exp), "vcvtph2ps mem 128");
    }

    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x7d, 0x13, 0x08};
        run_case(&in, exp, sizeof(exp), "vcvtph2ps mem 256");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vcvtps2ph";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = 1;
    in.imm8 = 0x05;
    in.vector_bits = 128;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x79, 0x1d, 0xd1, 0x05};
        run_case(&in, exp, sizeof(exp), "vcvtps2ph reg 128");
    }

    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x7d, 0x1d, 0xd1, 0x05};
        run_case(&in, exp, sizeof(exp), "vcvtps2ph reg 256");
    }

    in.op1 = mem_op(AS_X86_REG_RAX);
    in.vector_bits = 128;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x79, 0x1d, 0x10, 0x05};
        run_case(&in, exp, sizeof(exp), "vcvtps2ph mem 128");
    }

    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x7d, 0x1d, 0x10, 0x05};
        run_case(&in, exp, sizeof(exp), "vcvtps2ph mem 256");
    }

    puts("ok");
    return 0;
}
