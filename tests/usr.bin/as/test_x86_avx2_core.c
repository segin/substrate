#include "as_x86_avx2.h"

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

static as_x86_operand_t mem_vsib(as_x86_reg_t base, as_x86_reg_t index, unsigned scale) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.has_base = 1;
    op.u.mem.base = base;
    op.u.mem.has_index = 1;
    op.u.mem.index = index;
    op.u.mem.scale = scale;
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

static void run_case(const as_x86_avx2_insn_t *in, const uint8_t *exp, size_t exp_len,
                     const char *name) {
    uint8_t out[32];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_avx2(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    expect_bytes(name, out, out_len, exp, exp_len);
}

int main(void) {
    as_x86_avx2_insn_t in;

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vpaddd";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc5, 0xed, 0xfe, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpaddd");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vbroadcasti128";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = mem_base(AS_X86_REG_RAX);
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x7d, 0x5a, 0x08};
        run_case(&in, exp, sizeof(exp), "vbroadcasti128");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vextracti128";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RBX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = 1;
    in.imm8 = 0x01;
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x7d, 0x39, 0xd3, 0x01};
        run_case(&in, exp, sizeof(exp), "vextracti128");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vinserti128";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.has_imm8 = 1;
    in.imm8 = 0x01;
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x6d, 0x38, 0xcb, 0x01};
        run_case(&in, exp, sizeof(exp), "vinserti128");
    }

    in.mnemonic = "vpblendd";
    in.imm8 = 0x11;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x6d, 0x02, 0xcb, 0x11};
        run_case(&in, exp, sizeof(exp), "vpblendd");
    }

    memset(&in, 0, sizeof(in));
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.vector_bits = 256;
    {
        const struct {
            const char *mnemonic;
            uint8_t opcode;
        } cases[] = {
            {"vpbroadcastb", 0x78},
            {"vpbroadcastw", 0x79},
            {"vpbroadcastd", 0x58},
            {"vpbroadcastq", 0x59},
        };
        size_t i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            uint8_t exp[5];
            in.mnemonic = cases[i].mnemonic;
            exp[0] = 0xc4;
            exp[1] = 0xe2;
            exp[2] = 0x7d;
            exp[3] = cases[i].opcode;
            exp[4] = 0xca;
            run_case(&in, exp, sizeof(exp), cases[i].mnemonic);
        }
    }

    memset(&in, 0, sizeof(in));
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.vector_bits = 256;
    {
        const struct {
            const char *mnemonic;
            uint8_t op3;
            uint8_t opcode;
        } cases[] = {
            {"vpermd", 0x6d, 0x36},
            {"vpermps", 0x6d, 0x16},
            {"vpsllvd", 0x6d, 0x47},
            {"vpsllvq", 0xed, 0x47},
            {"vpsrlvd", 0x6d, 0x45},
            {"vpsrlvq", 0xed, 0x45},
            {"vpsravd", 0x6d, 0x46},
        };
        size_t i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            uint8_t exp[5];
            in.mnemonic = cases[i].mnemonic;
            exp[0] = 0xc4;
            exp[1] = 0xe2;
            exp[2] = cases[i].op3;
            exp[3] = cases[i].opcode;
            exp[4] = 0xcb;
            run_case(&in, exp, sizeof(exp), cases[i].mnemonic);
        }
    }

    memset(&in, 0, sizeof(in));
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = 1;
    in.vector_bits = 256;
    in.mnemonic = "vpermpd";
    in.imm8 = 0x02;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0xfd, 0x01, 0xca, 0x02};
        run_case(&in, exp, sizeof(exp), "vpermpd");
    }

    in.mnemonic = "vpermq";
    in.imm8 = 0x03;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0xfd, 0x00, 0xca, 0x03};
        run_case(&in, exp, sizeof(exp), "vpermq");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vperm2i128";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.has_imm8 = 1;
    in.imm8 = 0x31;
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0x6d, 0x46, 0xcb, 0x31};
        run_case(&in, exp, sizeof(exp), "vperm2i128");
    }

    memset(&in, 0, sizeof(in));
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = mem_base(AS_X86_REG_RAX);
    in.vector_bits = 256;
    in.mnemonic = "vpmaskmovd";
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x6d, 0x8c, 0x08};
        run_case(&in, exp, sizeof(exp), "vpmaskmovd load");
    }

    in.op1 = mem_base(AS_X86_REG_RAX);
    in.op3 = reg_op(AS_X86_REG_RCX);
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0x6d, 0x8e, 0x08};
        run_case(&in, exp, sizeof(exp), "vpmaskmovd store");
    }

    in.mnemonic = "vpmaskmovq";
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op3 = mem_base(AS_X86_REG_RAX);
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xed, 0x8c, 0x08};
        run_case(&in, exp, sizeof(exp), "vpmaskmovq load");
    }

    in.op1 = mem_base(AS_X86_REG_RAX);
    in.op3 = reg_op(AS_X86_REG_RCX);
    {
        const uint8_t exp[] = {0xc4, 0xe2, 0xed, 0x8e, 0x08};
        run_case(&in, exp, sizeof(exp), "vpmaskmovq store");
    }

    memset(&in, 0, sizeof(in));
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = mem_vsib(AS_X86_REG_RAX, AS_X86_REG_RDX, 4);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.vector_bits = 256;
    {
        const struct {
            const char *mnemonic;
            uint8_t b2;
            uint8_t opcode;
            unsigned scale;
            uint8_t sib;
            unsigned vector_bits;
        } cases[] = {
            {"vgatherdps", 0x65, 0x92, 4, 0x90, 256},
            {"vgatherdpd", 0xe5, 0x92, 8, 0xd0, 256},
            {"vgatherqps", 0x61, 0x93, 4, 0x90, 128},
            {"vgatherqpd", 0xe5, 0x93, 8, 0xd0, 256},
            {"vpgatherdd", 0x65, 0x90, 4, 0x90, 256},
            {"vpgatherdq", 0xe5, 0x90, 8, 0xd0, 256},
            {"vpgatherqd", 0x61, 0x91, 4, 0x90, 128},
            {"vpgatherqq", 0xe5, 0x91, 8, 0xd0, 256},
        };
        size_t i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            uint8_t exp[6];
            in.mnemonic = cases[i].mnemonic;
            in.op2 = mem_vsib(AS_X86_REG_RAX, AS_X86_REG_RDX, cases[i].scale);
            in.vector_bits = cases[i].vector_bits;
            exp[0] = 0xc4;
            exp[1] = 0xe2;
            exp[2] = cases[i].b2;
            exp[3] = cases[i].opcode;
            exp[4] = 0x0c;
            exp[5] = cases[i].sib;
            run_case(&in, exp, sizeof(exp), cases[i].mnemonic);
        }
    }

    puts("ok");
    return 0;
}
