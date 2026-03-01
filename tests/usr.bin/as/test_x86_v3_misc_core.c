#include "as_x86_v3_misc.h"

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

static void run_case(const as_x86_v3_misc_insn_t *in, const uint8_t *exp, size_t exp_len,
                     const char *name) {
    uint8_t out[32];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_v3_misc(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    expect_bytes(name, out, out_len, exp, exp_len);
}

int main(void) {
    as_x86_v3_misc_insn_t in;

    memset(&in, 0, sizeof(in));
    in.mnemonic = "lzcnt";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.width_bits = 16;
    {
        const uint8_t exp[] = {0x66, 0xf3, 0x0f, 0xbd, 0xca};
        run_case(&in, exp, sizeof(exp), "lzcnt16");
    }
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0xf3, 0x0f, 0xbd, 0xca};
        run_case(&in, exp, sizeof(exp), "lzcnt32");
    }
    in.width_bits = 64;
    {
        const uint8_t exp[] = {0xf3, 0x48, 0x0f, 0xbd, 0xca};
        run_case(&in, exp, sizeof(exp), "lzcnt64");
    }

    memset(&in, 0, sizeof(in));
    in.mnemonic = "movbe";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = mem_op(AS_X86_REG_RAX);
    in.width_bits = 16;
    {
        const uint8_t exp[] = {0x66, 0x0f, 0x38, 0xf0, 0x08};
        run_case(&in, exp, sizeof(exp), "movbe load16");
    }
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0x0f, 0x38, 0xf0, 0x08};
        run_case(&in, exp, sizeof(exp), "movbe load32");
    }
    in.width_bits = 64;
    {
        const uint8_t exp[] = {0x48, 0x0f, 0x38, 0xf0, 0x08};
        run_case(&in, exp, sizeof(exp), "movbe load64");
    }

    in.op1 = mem_op(AS_X86_REG_RAX);
    in.op2 = reg_op(AS_X86_REG_RCX);
    in.width_bits = 16;
    {
        const uint8_t exp[] = {0x66, 0x0f, 0x38, 0xf1, 0x08};
        run_case(&in, exp, sizeof(exp), "movbe store16");
    }
    in.width_bits = 32;
    {
        const uint8_t exp[] = {0x0f, 0x38, 0xf1, 0x08};
        run_case(&in, exp, sizeof(exp), "movbe store32");
    }
    in.width_bits = 64;
    {
        const uint8_t exp[] = {0x48, 0x0f, 0x38, 0xf1, 0x08};
        run_case(&in, exp, sizeof(exp), "movbe store64");
    }

    {
        static const struct {
            const char *mnemonic;
            uint8_t exp[4];
            size_t exp_len;
            size_t op_count;
        } cases[] = {
            {"xsave", {0x0f, 0xae, 0x20, 0x00}, 3, 1},
            {"xrstor", {0x0f, 0xae, 0x28, 0x00}, 3, 1},
            {"xgetbv", {0x0f, 0x01, 0xd0, 0x00}, 3, 0},
            {"xsetbv", {0x0f, 0x01, 0xd1, 0x00}, 3, 0},
            {"xsaveopt", {0x0f, 0xae, 0x30, 0x00}, 3, 1},
            {"xsavec", {0x0f, 0xc7, 0x20, 0x00}, 3, 1},
            {"xsaves", {0x0f, 0xc7, 0x28, 0x00}, 3, 1},
            {"xsave64", {0x48, 0x0f, 0xae, 0x20}, 4, 1},
            {"xrstor64", {0x48, 0x0f, 0xae, 0x28}, 4, 1},
            {"xsaveopt64", {0x48, 0x0f, 0xae, 0x30}, 4, 1},
            {"xsavec64", {0x48, 0x0f, 0xc7, 0x20}, 4, 1},
            {"xsaves64", {0x48, 0x0f, 0xc7, 0x28}, 4, 1},
        };
        size_t i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            memset(&in, 0, sizeof(in));
            in.mnemonic = cases[i].mnemonic;
            in.op_count = cases[i].op_count;
            if (in.op_count == 1) {
                in.op1 = mem_op(AS_X86_REG_RAX);
            }
            run_case(&in, cases[i].exp, cases[i].exp_len, cases[i].mnemonic);
        }
    }

    puts("ok");
    return 0;
}
