#include "as_x86_ssse3.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *mnemonic;
    uint8_t map;
    uint8_t opcode;
    int has_imm;
    uint8_t imm;
} ss_case_t;

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

static void run_case(const ss_case_t *tc) {
    as_x86_ssse3_insn_t in;
    uint8_t out[32];
    size_t out_len = 0;
    char err[128];
    uint8_t exp[8];
    size_t exp_len;

    memset(&in, 0, sizeof(in));
    in.mnemonic = tc->mnemonic;
    in.op_count = 2;
    in.dst = reg_op(AS_X86_REG_RCX);
    in.src = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = tc->has_imm;
    in.imm8 = tc->imm;

    if (as_x86_encode_ssse3(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "encode error %s: %s\n", tc->mnemonic, err);
        fail("encode failed");
    }

    exp[0] = 0x66;
    exp[1] = 0x0f;
    exp[2] = tc->map;
    exp[3] = tc->opcode;
    exp[4] = 0xca;
    exp_len = 5;
    if (tc->has_imm) {
        exp[5] = tc->imm;
        exp_len = 6;
    }

    if (out_len != exp_len || memcmp(out, exp, exp_len) != 0) {
        fprintf(stderr, "mismatch for %s\n", tc->mnemonic);
        fail("byte mismatch");
    }
}

int main(void) {
    const ss_case_t cases[] = {
        {"pabsb", 0x38, 0x1c, 0, 0},      {"pabsw", 0x38, 0x1d, 0, 0},
        {"pabsd", 0x38, 0x1e, 0, 0},      {"palignr", 0x3a, 0x0f, 1, 0x08},
        {"phaddw", 0x38, 0x01, 0, 0},     {"phaddd", 0x38, 0x02, 0, 0},
        {"phaddsw", 0x38, 0x03, 0, 0},    {"phsubw", 0x38, 0x05, 0, 0},
        {"phsubd", 0x38, 0x06, 0, 0},     {"phsubsw", 0x38, 0x07, 0, 0},
        {"pmaddubsw", 0x38, 0x04, 0, 0},  {"pmulhrsw", 0x38, 0x0b, 0, 0},
        {"pshufb", 0x38, 0x00, 0, 0},     {"psignb", 0x38, 0x08, 0, 0},
        {"psignw", 0x38, 0x09, 0, 0},     {"psignd", 0x38, 0x0a, 0, 0},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        run_case(&cases[i]);
    }

    /* High-register REX coverage. */
    {
        as_x86_ssse3_insn_t in;
        uint8_t out[32];
        size_t out_len = 0;
        char err[128];
        const uint8_t exp[] = {0x66, 0x45, 0x0f, 0x38, 0x00, 0xc1};

        memset(&in, 0, sizeof(in));
        in.mnemonic = "pshufb";
        in.op_count = 2;
        in.dst = reg_op(AS_X86_REG_R8);
        in.src = reg_op(AS_X86_REG_R9);

        if (as_x86_encode_ssse3(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
            fprintf(stderr, "rex encode error: %s\n", err);
            fail("rex encode failed");
        }
        if (out_len != sizeof(exp) || memcmp(out, exp, sizeof(exp)) != 0) {
            fail("rex bytes mismatch");
        }
    }

    puts("ok");
    return 0;
}
