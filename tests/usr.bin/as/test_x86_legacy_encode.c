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

    /*
     * What this encoder makes of an address, by an instruction it still
     * has: arpl, 63 and a ModRM, whose first operand here is the memory
     * and whose second the register.  (These were of mov, and before
     * that of lea too; both are rows of the table now,
     * as_x86_vec_table_base.h, and are tested there.)
     */
    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "arpl";
    insn.op_count = 2;
    insn.ops[0] = mem_base(AS_X86_REG_EBX);
    insn.ops[1] = reg_op(AS_X86_REG_EAX);
    {
        const uint8_t exp[] = {0x63, 0x03};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.ops[0] = mem_base_disp(AS_X86_REG_EBX, 0x10);
    {
        const uint8_t exp[] = {0x63, 0x43, 0x10};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.ops[0] = mem_full(AS_X86_REG_EBX, AS_X86_REG_ECX, 4, 0x12345678);
    {
        const uint8_t exp[] = {0x63, 0x84, 0x8b, 0x78, 0x56, 0x34, 0x12};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.ops[0] = mem_disp32(0x12345678);
    {
        const uint8_t exp[] = {0x63, 0x05, 0x78, 0x56, 0x34, 0x12};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.ops[0] = mem_base(AS_X86_REG_ESP);
    {
        const uint8_t exp[] = {0x63, 0x04, 0x24};
        check_encode(&insn, exp, sizeof(exp));
    }

    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "arpl";
    insn.seg_override = AS_X86_SEG_FS;
    insn.operand_size_override = 1;
    insn.address_size_override = 1;
    insn.op_count = 2;
    insn.ops[0] = mem_disp32(0x12345678);
    insn.ops[1] = reg_op(AS_X86_REG_EAX);
    {
        /* GNU as's order: segment, address size, operand size. */
        const uint8_t exp[] = {0x64, 0x67, 0x66, 0x63, 0x05, 0x78, 0x56, 0x34, 0x12};
        check_encode(&insn, exp, sizeof(exp));
    }

    /* (A base, an index with a scale, and a displacement of one byte.) */
    memset(&insn, 0, sizeof(insn));
    insn.mnemonic = "arpl";
    insn.op_count = 2;
    insn.ops[0] = mem_full(AS_X86_REG_EBX, AS_X86_REG_ECX, 2, 8);
    insn.ops[1] = reg_op(AS_X86_REG_EAX);
    {
        const uint8_t exp[] = {0x63, 0x44, 0x4b, 0x08};
        check_encode(&insn, exp, sizeof(exp));
    }

    /* (xchg, add, xor and cmp were asked of this encoder here.  It has
     * them no longer: they are rows of the table, as_x86_vec_table_base.h,
     * and test_base_rows.sh and the forms files hold them.) */

    /* (Nor has it sar: the shifts are rows too.) */

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

    /* (jz was asked of this encoder here; the conditional jumps are
     * rows of the table now.) */

    insn.mnemonic = "call";
    insn.ops[0] = rel_op(0x20);
    {
        const uint8_t exp[] = {0xe8, 0x20, 0x00, 0x00, 0x00};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "jmp";
    insn.ops[0] = reg_op(AS_X86_REG_EAX);
    {
        const uint8_t exp[] = {0xff, 0xe0};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "call";
    insn.ops[0] = reg_op(AS_X86_REG_EAX);
    {
        const uint8_t exp[] = {0xff, 0xd0};
        check_encode(&insn, exp, sizeof(exp));
    }

    insn.mnemonic = "call";
    insn.ops[0] = mem_disp32(0x12345678);
    {
        const uint8_t exp[] = {0xff, 0x15, 0x78, 0x56, 0x34, 0x12};
        check_encode(&insn, exp, sizeof(exp));
    }

    /* (Nor push and pop, in and out, ret, or the instructions of no
     * operand -- clc, pushf, nop, hlt.) */

    /* (Nor int.) */

    puts("ok");
    return 0;
}
