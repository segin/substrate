#include "as_x86_avx.h"

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

static void run_case(const as_x86_avx_insn_t *in, const uint8_t *exp, size_t exp_len,
                     const char *name) {
    uint8_t out[32];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_avx(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    expect_bytes(name, out, out_len, exp, exp_len);
}

int main(void) {
    as_x86_avx_insn_t in;

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vaddps";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.vector_bits = 128;
    {
        const uint8_t exp[] = {0xc5, 0xe8, 0x58, 0xcb};
        run_case(&in, exp, sizeof(exp), "vaddps128");
    }

    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc5, 0xec, 0x58, 0xcb};
        run_case(&in, exp, sizeof(exp), "vaddps256");
    }

    in.mnemonic = "vpand";
    {
        const uint8_t exp[] = {0xc5, 0xed, 0xdb, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpand");
    }

    in.mnemonic = "vhaddps";
    in.vector_bits = 128;
    {
        const uint8_t exp[] = {0xc5, 0xeb, 0x7c, 0xcb};
        run_case(&in, exp, sizeof(exp), "vhaddps");
    }

    in.mnemonic = "vaddsubpd";
    {
        const uint8_t exp[] = {0xc5, 0xe9, 0xd0, 0xcb};
        run_case(&in, exp, sizeof(exp), "vaddsubpd");
    }

    in.mnemonic = "vxorps";
    {
        const uint8_t exp[] = {0xc5, 0xe8, 0x57, 0xcb};
        run_case(&in, exp, sizeof(exp), "vxorps");
    }

    in.mnemonic = "vaddps";
    in.op1 = reg_op(AS_X86_REG_R8);
    in.op2 = reg_op(AS_X86_REG_R9);
    in.op3 = reg_op(AS_X86_REG_R10);
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0x41, 0x34, 0x58, 0xc2};
        run_case(&in, exp, sizeof(exp), "vaddps highregs");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vbroadcastss";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = mem_op(AS_X86_REG_RAX);
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x7d, 0x18, 0x08};
        run_case(&in, exp, sizeof(exp), "vbroadcastss");
    }

    in.mnemonic = "vbroadcastsd";
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x7d, 0x19, 0x08};
        run_case(&in, exp, sizeof(exp), "vbroadcastsd");
    }

    in.mnemonic = "vbroadcastf128";
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x7d, 0x1a, 0x08};
        run_case(&in, exp, sizeof(exp), "vbroadcastf128");
    }

    in.vector_bits = 128;
    {
        uint8_t out[32];
        size_t out_len = 0;
        char err[128];
        if (as_x86_encode_avx(&in, out, sizeof(out), &out_len, err, sizeof(err)) == 0) {
            fail("vbroadcastf128 accepted 128-bit vector");
        }
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vinsertf128";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.has_imm8 = 1;
    in.imm8 = 0x01;
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x6d, 0x18, 0xcb, 0x01};
        run_case(&in, exp, sizeof(exp), "vinsertf128");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vextractf128";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RBX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = 1;
    in.imm8 = 0x01;
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x7d, 0x19, 0xd3, 0x01};
        run_case(&in, exp, sizeof(exp), "vextractf128");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vmaskmovps";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = mem_op(AS_X86_REG_RAX);
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x6d, 0x2c, 0x08};
        run_case(&in, exp, sizeof(exp), "vmaskmovps load");
    }

    in.op1 = mem_op(AS_X86_REG_RAX);
    in.op3 = reg_op(AS_X86_REG_RCX);
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x6d, 0x2e, 0x08};
        run_case(&in, exp, sizeof(exp), "vmaskmovps store");
    }

    in.mnemonic = "vmaskmovpd";
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op3 = mem_op(AS_X86_REG_RAX);
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x6d, 0x2d, 0x08};
        run_case(&in, exp, sizeof(exp), "vmaskmovpd load");
    }

    in.op1 = mem_op(AS_X86_REG_RAX);
    in.op3 = reg_op(AS_X86_REG_RCX);
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x6d, 0x2f, 0x08};
        run_case(&in, exp, sizeof(exp), "vmaskmovpd store");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vperm2f128";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.has_imm8 = 1;
    in.imm8 = 0x11;
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x6d, 0x06, 0xcb, 0x11};
        run_case(&in, exp, sizeof(exp), "vperm2f128");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vpermilps";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = 1;
    in.imm8 = 0x21;
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x7d, 0x04, 0xca, 0x21};
        run_case(&in, exp, sizeof(exp), "vpermilps");
    }

    in.mnemonic = "vpermilpd";
    in.imm8 = 0x02;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x7d, 0x05, 0xca, 0x02};
        run_case(&in, exp, sizeof(exp), "vpermilpd");
    }

    in.mnemonic = "vtestps";
    in.op_count = 2;
    in.has_imm8 = 0;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x7d, 0x0e, 0xca};
        run_case(&in, exp, sizeof(exp), "vtestps");
    }

    in.mnemonic = "vtestpd";
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x7d, 0x0f, 0xca};
        run_case(&in, exp, sizeof(exp), "vtestpd");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vzeroupper";
    in.op_count = 0;
    {
        const uint8_t exp[] = {0xc5, 0xf8, 0x77};
        run_case(&in, exp, sizeof(exp), "vzeroupper");
    }

    in.mnemonic = "vzeroall";
    {
        const uint8_t exp[] = {0xc5, 0xfc, 0x77};
        run_case(&in, exp, sizeof(exp), "vzeroall");
    }

    puts("ok");
    return 0;
}
