#include "as_x86_sse3.h"

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

static as_x86_operand_t mem_base(as_x86_reg_t base) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.has_base = 1;
    op.u.mem.base = base;
    op.u.mem.scale = 1;
    return op;
}

static as_x86_operand_t mem_base_disp(as_x86_reg_t base, int32_t disp) {
    as_x86_operand_t op = mem_base(base);
    op.u.mem.has_disp = 1;
    op.u.mem.disp = disp;
    return op;
}

static as_x86_operand_t mem_rip(int32_t disp) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.rip_relative = 1;
    op.u.mem.has_disp = 1;
    op.u.mem.disp = disp;
    op.u.mem.scale = 1;
    return op;
}

static void check(const as_x86_sse3_insn_t *in, const uint8_t *exp, size_t exp_len) {
    uint8_t out[64];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_sse3(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "encode error for %s: %s\n", in->mnemonic, err);
        fail("encode failed");
    }
    if (out_len != exp_len || memcmp(out, exp, exp_len) != 0) {
        size_t i;
        fprintf(stderr, "byte mismatch for %s\nexpected:", in->mnemonic);
        for (i = 0; i < exp_len; ++i) {
            fprintf(stderr, " %02x", exp[i]);
        }
        fprintf(stderr, "\nactual:  ");
        for (i = 0; i < out_len; ++i) {
            fprintf(stderr, " %02x", out[i]);
        }
        fprintf(stderr, "\n");
        fail("mismatch");
    }
}

int main(void) {
    as_x86_sse3_insn_t in;

    memset(&in, 0, sizeof(in));
    in.mnemonic = "addsubpd";
    in.op_count = 2;
    in.dst = reg_op(AS_X86_REG_RCX);
    in.src = reg_op(AS_X86_REG_RDX);
    {
        const uint8_t exp[] = {0x66, 0x0f, 0xd0, 0xca};
        check(&in, exp, sizeof(exp));
    }

    in.mnemonic = "addsubps";
    {
        const uint8_t exp[] = {0xf2, 0x0f, 0xd0, 0xca};
        check(&in, exp, sizeof(exp));
    }

    in.mnemonic = "haddpd";
    in.src = mem_base_disp(AS_X86_REG_R12, 0x20);
    {
        const uint8_t exp[] = {0x66, 0x41, 0x0f, 0x7c, 0x4c, 0x24, 0x20};
        check(&in, exp, sizeof(exp));
    }

    in.mnemonic = "hsubps";
    in.dst = reg_op(AS_X86_REG_R8);
    in.src = reg_op(AS_X86_REG_R9);
    {
        const uint8_t exp[] = {0xf2, 0x45, 0x0f, 0x7d, 0xc1};
        check(&in, exp, sizeof(exp));
    }

    in.mnemonic = "lddqu";
    in.dst = reg_op(AS_X86_REG_RBX);
    in.src = mem_rip(0x10);
    {
        const uint8_t exp[] = {0xf2, 0x0f, 0xf0, 0x1d, 0x10, 0x00, 0x00, 0x00};
        check(&in, exp, sizeof(exp));
    }

    in.mnemonic = "movddup";
    in.dst = reg_op(AS_X86_REG_RDX);
    in.src = reg_op(AS_X86_REG_RCX);
    {
        const uint8_t exp[] = {0xf2, 0x0f, 0x12, 0xd1};
        check(&in, exp, sizeof(exp));
    }

    in.mnemonic = "movshdup";
    {
        const uint8_t exp[] = {0xf3, 0x0f, 0x16, 0xd1};
        check(&in, exp, sizeof(exp));
    }

    in.mnemonic = "movsldup";
    {
        const uint8_t exp[] = {0xf3, 0x0f, 0x12, 0xd1};
        check(&in, exp, sizeof(exp));
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "fisttp";
    in.op_count = 1;
    in.width_bits = 16;
    in.dst = mem_base(AS_X86_REG_RAX);
    {
        const uint8_t exp[] = {0xdf, 0x08};
        check(&in, exp, sizeof(exp));
    }

    in.width_bits = 32;
    in.dst = mem_base_disp(AS_X86_REG_RBX, 4);
    {
        const uint8_t exp[] = {0xdb, 0x4b, 0x04};
        check(&in, exp, sizeof(exp));
    }

    in.width_bits = 64;
    in.dst = mem_base(AS_X86_REG_R12);
    {
        const uint8_t exp[] = {0x41, 0xdd, 0x0c, 0x24};
        check(&in, exp, sizeof(exp));
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "monitor";
    {
        const uint8_t exp[] = {0x0f, 0x01, 0xc8};
        check(&in, exp, sizeof(exp));
    }

    in.mnemonic = "mwait";
    {
        const uint8_t exp[] = {0x0f, 0x01, 0xc9};
        check(&in, exp, sizeof(exp));
    }

    puts("ok");
    return 0;
}
