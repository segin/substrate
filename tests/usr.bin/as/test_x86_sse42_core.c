#include "as_x86_sse42.h"

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

static as_x86_operand_t mem_op(as_x86_reg_t base, int32_t disp, int has_disp) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.has_base = 1;
    op.u.mem.base = base;
    op.u.mem.scale = 1;
    op.u.mem.has_disp = has_disp;
    op.u.mem.disp = disp;
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

static void run_crc32_case(unsigned width_bits, as_x86_operand_t dst, as_x86_operand_t src,
                           const uint8_t *exp, size_t exp_len) {
    as_x86_sse42_insn_t in;
    uint8_t out[32];
    size_t out_len = 0;
    char err[128];

    memset(&in, 0, sizeof(in));
    in.mnemonic = "crc32";
    in.op_count = 2;
    in.dst = dst;
    in.src = src;
    in.width_bits = width_bits;

    if (as_x86_encode_sse42(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "crc32 encode error: %s\n", err);
        fail("crc32 encode failed");
    }

    expect_bytes("crc32", out, out_len, exp, exp_len);
}

int main(void) {
    {
        const uint8_t exp[] = {0xf2, 0x0f, 0x38, 0xf0, 0xca};
        run_crc32_case(8, reg_op(AS_X86_REG_RCX), reg_op(AS_X86_REG_RDX), exp, sizeof(exp));
    }

    {
        const uint8_t exp[] = {0x66, 0xf2, 0x0f, 0x38, 0xf1, 0xca};
        run_crc32_case(16, reg_op(AS_X86_REG_RCX), reg_op(AS_X86_REG_RDX), exp, sizeof(exp));
    }

    {
        const uint8_t exp[] = {0xf2, 0x0f, 0x38, 0xf1, 0xca};
        run_crc32_case(32, reg_op(AS_X86_REG_RCX), reg_op(AS_X86_REG_RDX), exp, sizeof(exp));
    }

    {
        const uint8_t exp[] = {0xf2, 0x48, 0x0f, 0x38, 0xf1, 0xca};
        run_crc32_case(64, reg_op(AS_X86_REG_RCX), reg_op(AS_X86_REG_RDX), exp, sizeof(exp));
    }

    {
        const uint8_t exp[] = {0xf2, 0x45, 0x0f, 0x38, 0xf0, 0xc1};
        run_crc32_case(8, reg_op(AS_X86_REG_R8), reg_op(AS_X86_REG_R9), exp, sizeof(exp));
    }

    {
        as_x86_sse42_insn_t in;
        uint8_t out[32];
        size_t out_len = 0;
        char err[128];
        const uint8_t exp_m[] = {0xf2, 0x0f, 0x38, 0xf1, 0x48, 0x10};

        memset(&in, 0, sizeof(in));
        in.mnemonic = "crc32";
        in.op_count = 2;
        in.dst = reg_op(AS_X86_REG_RCX);
        in.src = mem_op(AS_X86_REG_RAX, 0x10, 1);
        in.width_bits = 32;

        if (as_x86_encode_sse42(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
            fprintf(stderr, "crc32 mem encode error: %s\n", err);
            fail("crc32 mem encode failed");
        }
        expect_bytes("crc32(mem)", out, out_len, exp_m, sizeof(exp_m));

        in.width_bits = 7;
        if (as_x86_encode_sse42(&in, out, sizeof(out), &out_len, err, sizeof(err)) == 0) {
            fail("crc32 accepted invalid width");
        }
    }

    {
        static const struct {
            const char *mnemonic;
            uint8_t opcode;
            uint8_t imm;
        } cases[] = {
            {"pcmpestrm", 0x60, 0x55},
            {"pcmpestri", 0x61, 0xaa},
            {"pcmpistrm", 0x62, 0x12},
            {"pcmpistri", 0x63, 0x34},
        };
        size_t i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            as_x86_sse42_insn_t in;
            uint8_t out[32];
            size_t out_len = 0;
            char err[128];
            uint8_t exp[8];

            memset(&in, 0, sizeof(in));
            in.mnemonic = cases[i].mnemonic;
            in.op_count = 2;
            in.dst = reg_op(AS_X86_REG_RCX);
            in.src = reg_op(AS_X86_REG_RDX);
            in.has_imm8 = 1;
            in.imm8 = cases[i].imm;

            if (as_x86_encode_sse42(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
                fprintf(stderr, "%s encode error: %s\n", cases[i].mnemonic, err);
                fail("pcmp encode failed");
            }

            exp[0] = 0x66;
            exp[1] = 0x0f;
            exp[2] = 0x3a;
            exp[3] = cases[i].opcode;
            exp[4] = 0xca;
            exp[5] = cases[i].imm;
            expect_bytes(cases[i].mnemonic, out, out_len, exp, 6);
        }
    }

    {
        as_x86_sse42_insn_t in;
        uint8_t out[32];
        size_t out_len = 0;
        char err[128];
        const uint8_t exp[] = {0x66, 0x0f, 0x38, 0x37, 0xca};
        const uint8_t exp_rex[] = {0x66, 0x45, 0x0f, 0x38, 0x37, 0xc1};

        memset(&in, 0, sizeof(in));
        in.mnemonic = "pcmpgtq";
        in.op_count = 2;
        in.dst = reg_op(AS_X86_REG_RCX);
        in.src = reg_op(AS_X86_REG_RDX);

        if (as_x86_encode_sse42(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
            fprintf(stderr, "pcmpgtq encode error: %s\n", err);
            fail("pcmpgtq encode failed");
        }
        expect_bytes("pcmpgtq", out, out_len, exp, sizeof(exp));

        in.dst = reg_op(AS_X86_REG_R8);
        in.src = reg_op(AS_X86_REG_R9);
        if (as_x86_encode_sse42(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
            fprintf(stderr, "pcmpgtq rex encode error: %s\n", err);
            fail("pcmpgtq rex encode failed");
        }
        expect_bytes("pcmpgtq rex", out, out_len, exp_rex, sizeof(exp_rex));
    }

    puts("ok");
    return 0;
}
