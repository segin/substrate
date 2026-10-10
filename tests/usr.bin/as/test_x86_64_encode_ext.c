#include "as_x86_encode.h"

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

static as_x86_operand_t imm_op(int32_t v) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_IMM;
    op.u.imm = v;
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

static as_x86_operand_t mem_full(as_x86_reg_t base, as_x86_reg_t index, unsigned scale, int32_t disp) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.has_base = 1;
    op.u.mem.base = base;
    op.u.mem.has_index = 1;
    op.u.mem.index = index;
    op.u.mem.scale = scale;
    op.u.mem.has_disp = 1;
    op.u.mem.disp = disp;
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

static void check_encode64(const as_x86_insn_t *insn, const uint8_t *exp, size_t exp_len) {
    uint8_t out[64];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_x86_64(insn, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "encode64 error for %s: %s\n", insn->mnemonic, err);
        fail("encode64 failed");
    }
    if (out_len != exp_len || memcmp(out, exp, exp_len) != 0) {
        size_t i;
        fprintf(stderr, "byte mismatch for %s\nexpected:", insn->mnemonic);
        for (i = 0; i < exp_len; ++i) {
            fprintf(stderr, " %02x", exp[i]);
        }
        fprintf(stderr, "\nactual:  ");
        for (i = 0; i < out_len; ++i) {
            fprintf(stderr, " %02x", out[i]);
        }
        fprintf(stderr, "\n");
        fail("encode64 mismatch");
    }
}

int main(void) {
    as_x86_insn_t insn;

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "mov";
    insn.op_count = 2;
    insn.rex_w = 1;
    insn.ops[0] = reg_op(AS_X86_REG_R8);
    insn.ops[1] = mem_full(AS_X86_REG_R9, AS_X86_REG_R10, 8, 0x20);
    {
        const uint8_t exp[] = {0x4f, 0x8b, 0x44, 0xd1, 0x20};
        check_encode64(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "mov";
    insn.op_count = 2;
    insn.rex_w = 1;
    insn.ops[0] = reg_op(AS_X86_REG_RAX);
    insn.ops[1] = mem_rip(0x1234);
    {
        const uint8_t exp[] = {0x48, 0x8b, 0x05, 0x34, 0x12, 0x00, 0x00};
        check_encode64(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "mov";
    insn.op_count = 2;
    insn.rex_w = 1;
    insn.ops[0] = reg_op(AS_X86_REG_R9);
    insn.ops[1] = reg_op(AS_X86_REG_RAX);
    {
        const uint8_t exp[] = {0x4c, 0x8b, 0xc8};
        check_encode64(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "cmpxchg16b";
    insn.op_count = 1;
    insn.ops[0] = mem_base(AS_X86_REG_R12);
    {
        const uint8_t exp[] = {0x49, 0x0f, 0xc7, 0x0c, 0x24};
        check_encode64(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "call";
    insn.op_count = 1;
    insn.ops[0] = reg_op(AS_X86_REG_R11);
    {
        const uint8_t exp[] = {0x41, 0xff, 0xd3};
        check_encode64(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "jmp";
    {
        const uint8_t exp[] = {0x41, 0xff, 0xe3};
        check_encode64(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "call";
    insn.ops[0] = mem_base(AS_X86_REG_R12);
    {
        const uint8_t exp[] = {0x41, 0xff, 0x14, 0x24};
        check_encode64(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "syscall";
    {
        const uint8_t exp[] = {0x0f, 0x05};
        check_encode64(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "sysret";
    {
        const uint8_t exp[] = {0x0f, 0x07};
        check_encode64(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "swapgs";
    {
        const uint8_t exp[] = {0x0f, 0x01, 0xf8};
        check_encode64(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "mov";
    insn.op_count = 2;
    insn.rex_w = 1;
    insn.ops[0] = reg_op(AS_X86_REG_R15);
    insn.ops[1] = imm_op(0x11223344);
    {
        const uint8_t exp[] = {0x49, 0xc7, 0xc7, 0x44, 0x33, 0x22, 0x11};
        check_encode64(&insn, exp, sizeof(exp));
    }

    puts("ok");
    return 0;
}
