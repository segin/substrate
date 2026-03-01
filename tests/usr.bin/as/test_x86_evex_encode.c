#include "as_x86_evex.h"

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

static as_x86_operand_t mem_base_disp(as_x86_reg_t base, int32_t disp) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.has_base = 1;
    op.u.mem.base = base;
    op.u.mem.has_disp = 1;
    op.u.mem.disp = disp;
    op.u.mem.scale = 1;
    return op;
}

static void check_evex(const as_x86_evex_insn_t *insn, const uint8_t *exp, size_t exp_len) {
    uint8_t out[64];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_evex_3op(insn, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "evex encode error for %s: %s\n", insn->mnemonic, err);
        fail("evex encode failed");
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
        fail("evex mismatch");
    }
}

int main(void) {
    as_x86_evex_insn_t in;

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vaddps";
    in.opcode = 0x58;
    in.map = AS_EVEX_MAP_0F;
    in.pp = AS_EVEX_PP_NONE;
    in.evex_w = 0;
    in.dst = AS_X86_REG_R8;
    in.src1 = AS_X86_REG_R9;
    in.src2 = mem_base_disp(AS_X86_REG_R12, 0x20);
    in.opmask = 3;
    in.zeroing = 1;
    in.broadcast = 1;
    in.evex_l2 = 2;
    in.rounding_mode = -1;
    {
        const uint8_t exp[] = {0x62, 0xc1, 0x34, 0xdb, 0x58, 0x44, 0x24, 0x20};
        check_evex(&in, exp, sizeof(exp));
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vaddsd";
    in.opcode = 0x58;
    in.map = AS_EVEX_MAP_0F;
    in.pp = AS_EVEX_PP_66;
    in.evex_w = 1;
    in.dst = AS_X86_REG_RCX;
    in.src1 = AS_X86_REG_RDX;
    in.src2 = reg_op(AS_X86_REG_RBX);
    in.opmask = 1;
    in.sae = 1;
    in.rounding_mode = 3;
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xed, 0x79, 0x58, 0xcb};
        check_evex(&in, exp, sizeof(exp));
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vpmulld";
    in.opcode = 0x40;
    in.map = AS_EVEX_MAP_0F38;
    in.pp = AS_EVEX_PP_F3;
    in.evex_w = 0;
    in.dst = AS_X86_REG_R8;
    in.src1 = AS_X86_REG_RCX;
    in.src2 = reg_op(AS_X86_REG_RDX);
    in.opmask = 7;
    in.zeroing = 1;
    in.evex_l2 = 1;
    in.rounding_mode = -1;
    {
        const uint8_t exp[] = {0x62, 0xe2, 0x76, 0xaf, 0x40, 0xc2};
        check_evex(&in, exp, sizeof(exp));
    }

    puts("ok");
    return 0;
}
