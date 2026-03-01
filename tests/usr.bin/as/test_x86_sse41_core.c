#include "as_x86_sse41.h"

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
} rr_case_t;

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

static void expect_bytes(const char *mnemonic, const uint8_t *got, size_t got_len,
                         const uint8_t *exp, size_t exp_len) {
    if (got_len != exp_len || memcmp(got, exp, exp_len) != 0) {
        size_t i;
        fprintf(stderr, "mismatch for %s\n  got:", mnemonic);
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

static void run_rr_case(const rr_case_t *tc) {
    as_x86_sse41_insn_t in;
    uint8_t out[32];
    size_t out_len = 0;
    char err[128];
    uint8_t exp[8];
    size_t exp_len = 0;

    memset(&in, 0, sizeof(in));
    in.mnemonic = tc->mnemonic;
    in.op_count = 2;
    in.dst = reg_op(AS_X86_REG_RCX);
    in.src = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = tc->has_imm;
    in.imm8 = tc->imm;

    if (as_x86_encode_sse41(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "encode error %s: %s\n", tc->mnemonic, err);
        fail("encode failed");
    }

    exp[exp_len++] = 0x66;
    exp[exp_len++] = 0x0f;
    exp[exp_len++] = tc->map;
    exp[exp_len++] = tc->opcode;
    exp[exp_len++] = 0xca;
    if (tc->has_imm) {
        exp[exp_len++] = tc->imm;
    }

    expect_bytes(tc->mnemonic, out, out_len, exp, exp_len);
}

static void run_rm_dst_case(const char *mnemonic, uint8_t map, uint8_t opcode, int force_rex_w,
                            uint8_t imm) {
    as_x86_sse41_insn_t in;
    uint8_t out[32];
    size_t out_len = 0;
    char err[128];
    uint8_t exp[8];
    size_t exp_len = 0;

    memset(&in, 0, sizeof(in));
    in.mnemonic = mnemonic;
    in.op_count = 2;
    in.dst = reg_op(AS_X86_REG_RDX);
    in.src = reg_op(AS_X86_REG_RCX);
    in.has_imm8 = 1;
    in.imm8 = imm;

    if (as_x86_encode_sse41(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "encode error %s: %s\n", mnemonic, err);
        fail("encode failed");
    }

    exp[exp_len++] = 0x66;
    if (force_rex_w) {
        exp[exp_len++] = 0x48;
    }
    exp[exp_len++] = 0x0f;
    exp[exp_len++] = map;
    exp[exp_len++] = opcode;
    exp[exp_len++] = 0xca;
    exp[exp_len++] = imm;

    expect_bytes(mnemonic, out, out_len, exp, exp_len);
}

int main(void) {
    const rr_case_t rr_cases[] = {
        {"blendps", 0x3a, 0x0c, 1, 0x11},     {"blendpd", 0x3a, 0x0d, 1, 0x22},
        {"blendvps", 0x38, 0x14, 0, 0x00},    {"blendvpd", 0x38, 0x15, 0, 0x00},
        {"dpps", 0x3a, 0x40, 1, 0xf3},        {"dppd", 0x3a, 0x41, 1, 0xa5},
        {"insertps", 0x3a, 0x21, 1, 0x07},    {"mpsadbw", 0x3a, 0x42, 1, 0x02},
        {"packusdw", 0x38, 0x2b, 0, 0x00},    {"pblendvb", 0x38, 0x10, 0, 0x00},
        {"pblendw", 0x3a, 0x0e, 1, 0x0f},     {"pcmpeqq", 0x38, 0x29, 0, 0x00},
        {"pinsrb", 0x3a, 0x20, 1, 0x03},      {"pinsrd", 0x3a, 0x22, 1, 0x01},
        {"pmaxsb", 0x38, 0x3c, 0, 0x00},      {"pmaxsd", 0x38, 0x3d, 0, 0x00},
        {"pmaxud", 0x38, 0x3f, 0, 0x00},      {"pmaxuw", 0x38, 0x3e, 0, 0x00},
        {"pminsb", 0x38, 0x38, 0, 0x00},      {"pminsd", 0x38, 0x39, 0, 0x00},
        {"pminud", 0x38, 0x3b, 0, 0x00},      {"pminuw", 0x38, 0x3a, 0, 0x00},
        {"pmovsxbw", 0x38, 0x20, 0, 0x00},    {"pmovsxbd", 0x38, 0x21, 0, 0x00},
        {"pmovsxbq", 0x38, 0x22, 0, 0x00},    {"pmovsxwd", 0x38, 0x23, 0, 0x00},
        {"pmovsxwq", 0x38, 0x24, 0, 0x00},    {"pmovsxdq", 0x38, 0x25, 0, 0x00},
        {"pmovzxbw", 0x38, 0x30, 0, 0x00},    {"pmovzxbd", 0x38, 0x31, 0, 0x00},
        {"pmovzxbq", 0x38, 0x32, 0, 0x00},    {"pmovzxwd", 0x38, 0x33, 0, 0x00},
        {"pmovzxwq", 0x38, 0x34, 0, 0x00},    {"pmovzxdq", 0x38, 0x35, 0, 0x00},
        {"pmuldq", 0x38, 0x28, 0, 0x00},      {"pmulld", 0x38, 0x40, 0, 0x00},
        {"ptest", 0x38, 0x17, 0, 0x00},       {"roundpd", 0x3a, 0x09, 1, 0x04},
        {"roundps", 0x3a, 0x08, 1, 0x04},     {"roundsd", 0x3a, 0x0b, 1, 0x04},
        {"roundss", 0x3a, 0x0a, 1, 0x04},     {"phminposuw", 0x38, 0x41, 0, 0x00},
    };
    size_t i;

    for (i = 0; i < sizeof(rr_cases) / sizeof(rr_cases[0]); ++i) {
        run_rr_case(&rr_cases[i]);
    }

    run_rm_dst_case("extractps", 0x3a, 0x17, 0, 0x03);
    run_rm_dst_case("pextrb", 0x3a, 0x14, 0, 0x02);
    run_rm_dst_case("pextrd", 0x3a, 0x16, 0, 0x01);
    run_rm_dst_case("pextrq", 0x3a, 0x16, 1, 0x01);
    run_rm_dst_case("pextrw", 0x3a, 0x15, 0, 0x01);

    {
        as_x86_sse41_insn_t in;
        uint8_t out[32];
        size_t out_len = 0;
        char err[128];
        const uint8_t exp[] = {0x66, 0x48, 0x0f, 0x3a, 0x22, 0xca, 0x03};

        memset(&in, 0, sizeof(in));
        in.mnemonic = "pinsrq";
        in.op_count = 2;
        in.dst = reg_op(AS_X86_REG_RCX);
        in.src = reg_op(AS_X86_REG_RDX);
        in.has_imm8 = 1;
        in.imm8 = 0x03;

        if (as_x86_encode_sse41(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
            fprintf(stderr, "pinsrq encode error: %s\n", err);
            fail("pinsrq encode failed");
        }
        expect_bytes("pinsrq", out, out_len, exp, sizeof(exp));
    }

    {
        as_x86_sse41_insn_t in;
        uint8_t out[32];
        size_t out_len = 0;
        char err[128];
        const uint8_t exp[] = {0x66, 0x0f, 0x38, 0x2a, 0x08};

        memset(&in, 0, sizeof(in));
        in.mnemonic = "movntdqa";
        in.op_count = 2;
        in.dst = reg_op(AS_X86_REG_RCX);
        in.src = mem_op(AS_X86_REG_RAX, 0, 0);

        if (as_x86_encode_sse41(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
            fprintf(stderr, "movntdqa encode error: %s\n", err);
            fail("movntdqa encode failed");
        }
        expect_bytes("movntdqa", out, out_len, exp, sizeof(exp));

        in.src = reg_op(AS_X86_REG_RDX);
        if (as_x86_encode_sse41(&in, out, sizeof(out), &out_len, err, sizeof(err)) == 0) {
            fail("movntdqa accepted register source");
        }
    }

    {
        as_x86_sse41_insn_t in;
        uint8_t out[32];
        size_t out_len = 0;
        char err[128];
        const uint8_t exp[] = {0x66, 0x0f, 0x3a, 0x17, 0x48, 0x10, 0x06};

        memset(&in, 0, sizeof(in));
        in.mnemonic = "extractps";
        in.op_count = 2;
        in.dst = mem_op(AS_X86_REG_RAX, 0x10, 1);
        in.src = reg_op(AS_X86_REG_RCX);
        in.has_imm8 = 1;
        in.imm8 = 0x06;

        if (as_x86_encode_sse41(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
            fprintf(stderr, "extractps(mem) encode error: %s\n", err);
            fail("extractps(mem) encode failed");
        }
        expect_bytes("extractps(mem)", out, out_len, exp, sizeof(exp));
    }

    {
        as_x86_sse41_insn_t in;
        uint8_t out[32];
        size_t out_len = 0;
        char err[128];
        const uint8_t exp[] = {0x66, 0x45, 0x0f, 0x38, 0x17, 0xc1};

        memset(&in, 0, sizeof(in));
        in.mnemonic = "ptest";
        in.op_count = 2;
        in.dst = reg_op(AS_X86_REG_R8);
        in.src = reg_op(AS_X86_REG_R9);

        if (as_x86_encode_sse41(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
            fprintf(stderr, "ptest rex encode error: %s\n", err);
            fail("ptest rex encode failed");
        }
        expect_bytes("ptest rex", out, out_len, exp, sizeof(exp));
    }

    puts("ok");
    return 0;
}
