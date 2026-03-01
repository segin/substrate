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

static as_x86_operand_t rel_op(int32_t v) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_REL;
    op.u.rel = v;
    return op;
}

static as_x86_operand_t mem_base(as_x86_reg_t b) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.has_base = 1;
    op.u.mem.base = b;
    op.u.mem.scale = 1;
    return op;
}

static as_x86_operand_t mem_base_disp(as_x86_reg_t b, int32_t d) {
    as_x86_operand_t op = mem_base(b);
    op.u.mem.has_disp = 1;
    op.u.mem.disp = d;
    return op;
}

static as_x86_operand_t mem_full(as_x86_reg_t b, as_x86_reg_t i, unsigned scale, int32_t d) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.has_base = 1;
    op.u.mem.base = b;
    op.u.mem.has_index = 1;
    op.u.mem.index = i;
    op.u.mem.scale = scale;
    op.u.mem.has_disp = 1;
    op.u.mem.disp = d;
    return op;
}

static as_x86_operand_t mem_disp32(int32_t d) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.disp_only = 1;
    op.u.mem.has_disp = 1;
    op.u.mem.disp = d;
    op.u.mem.scale = 1;
    return op;
}

static void check_encode(const as_x86_insn_t *insn, const uint8_t *exp, size_t exp_len) {
    uint8_t out[64];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_i386(insn, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "encoder error for %s: %s\n", insn->mnemonic, err);
        fail("encode failed");
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
        fail("encoding mismatch");
    }
}

int main(void) {
    as_x86_insn_t insn;

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "mov";
    insn.op_count = 2;
    insn.ops[0] = reg_op(AS_X86_REG_EAX);
    insn.ops[1] = mem_base(AS_X86_REG_EBX);
    {
        const uint8_t exp[] = {0x8b, 0x03};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.ops[1] = mem_base_disp(AS_X86_REG_EBX, 0x10);
    {
        const uint8_t exp[] = {0x8b, 0x43, 0x10};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.ops[1] = mem_full(AS_X86_REG_EBX, AS_X86_REG_ECX, 4, 0x12345678);
    {
        const uint8_t exp[] = {0x8b, 0x84, 0x8b, 0x78, 0x56, 0x34, 0x12};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.ops[1] = mem_disp32(0x12345678);
    {
        const uint8_t exp[] = {0x8b, 0x05, 0x78, 0x56, 0x34, 0x12};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.ops[1] = mem_base(AS_X86_REG_ESP);
    {
        const uint8_t exp[] = {0x8b, 0x04, 0x24};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "mov";
    insn.seg_override = AS_X86_SEG_FS;
    insn.operand_size_override = 1;
    insn.address_size_override = 1;
    insn.op_count = 2;
    insn.ops[0] = reg_op(AS_X86_REG_EAX);
    insn.ops[1] = mem_disp32(0x12345678);
    {
        const uint8_t exp[] = {0x66, 0x67, 0x64, 0x8b, 0x05, 0x78, 0x56, 0x34, 0x12};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "lea";
    insn.op_count = 2;
    insn.ops[0] = reg_op(AS_X86_REG_EAX);
    insn.ops[1] = mem_full(AS_X86_REG_EBX, AS_X86_REG_ECX, 2, 8);
    {
        const uint8_t exp[] = {0x8d, 0x44, 0x4b, 0x08};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "xchg";
    insn.op_count = 2;
    insn.ops[0] = mem_base(AS_X86_REG_EAX);
    insn.ops[1] = reg_op(AS_X86_REG_EBX);
    {
        const uint8_t exp[] = {0x87, 0x18};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "add";
    insn.op_count = 2;
    insn.ops[0] = mem_base(AS_X86_REG_EBX);
    insn.ops[1] = imm_op(0x11223344);
    {
        const uint8_t exp[] = {0x81, 0x03, 0x44, 0x33, 0x22, 0x11};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "xor";
    insn.ops[0] = reg_op(AS_X86_REG_EAX);
    insn.ops[1] = mem_base(AS_X86_REG_EBX);
    {
        const uint8_t exp[] = {0x33, 0x03};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "cmp";
    insn.ops[0] = reg_op(AS_X86_REG_EAX);
    insn.ops[1] = reg_op(AS_X86_REG_ECX);
    {
        const uint8_t exp[] = {0x39, 0xc8};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "sar";
    insn.op_count = 2;
    insn.ops[0] = reg_op(AS_X86_REG_EAX);
    insn.ops[1] = imm_op(1);
    {
        const uint8_t exp[] = {0xc1, 0xf8, 0x01};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "movsb";
    {
        const uint8_t exp[] = {0xa4};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "scasd";
    {
        const uint8_t exp[] = {0xaf};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "jmp";
    insn.op_count = 1;
    insn.ops[0] = rel_op(0x10);
    {
        const uint8_t exp[] = {0xe9, 0x10, 0x00, 0x00, 0x00};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "jz";
    insn.ops[0] = rel_op(-4);
    {
        const uint8_t exp[] = {0x0f, 0x84, 0xfc, 0xff, 0xff, 0xff};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "call";
    insn.ops[0] = rel_op(0x20);
    {
        const uint8_t exp[] = {0xe8, 0x20, 0x00, 0x00, 0x00};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "ret";
    {
        const uint8_t exp[] = {0xc3};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "push";
    insn.op_count = 1;
    insn.ops[0] = reg_op(AS_X86_REG_EAX);
    {
        const uint8_t exp[] = {0x50};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.ops[0] = imm_op(0x01020304);
    {
        const uint8_t exp[] = {0x68, 0x04, 0x03, 0x02, 0x01};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "pop";
    insn.ops[0] = reg_op(AS_X86_REG_EBX);
    {
        const uint8_t exp[] = {0x5b};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "clc";
    {
        const uint8_t exp[] = {0xf8};
        check_encode(&insn, exp, sizeof(exp));
    }
    insn.mnemonic = "pushf";
    {
        const uint8_t exp[] = {0x9c};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "in";
    insn.op_count = 2;
    insn.ops[0] = reg_op(AS_X86_REG_EAX);
    insn.ops[1] = imm_op(0x80);
    {
        const uint8_t exp[] = {0xe5, 0x80};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "out";
    insn.ops[0] = imm_op(0x81);
    insn.ops[1] = reg_op(AS_X86_REG_EAX);
    {
        const uint8_t exp[] = {0xe7, 0x81};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "nop";
    {
        const uint8_t exp[] = {0x90};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "hlt";
    {
        const uint8_t exp[] = {0xf4};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "int";
    insn.op_count = 1;
    insn.ops[0] = imm_op(0x80);
    {
        const uint8_t exp[] = {0xcd, 0x80};
        check_encode(&insn, exp, sizeof(exp));
    }

    puts("ok");
    return 0;
}
