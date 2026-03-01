#include "as_x86_avx512bw.h"
#include "as_x86_avx512cd.h"
#include "as_x86_avx512dq.h"
#include "as_x86_avx512f.h"

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

static void run_f_case(const as_x86_avx512f_insn_t *in, const uint8_t *exp, size_t exp_len,
                       const char *name) {
    uint8_t out[64];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_avx512f(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    expect_bytes(name, out, out_len, exp, exp_len);
}

static void run_bw_case(const as_x86_avx512bw_insn_t *in, const uint8_t *exp, size_t exp_len,
                        const char *name) {
    uint8_t out[64];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_avx512bw(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    expect_bytes(name, out, out_len, exp, exp_len);
}

static void run_cd_case(const as_x86_avx512cd_insn_t *in, const uint8_t *exp, size_t exp_len,
                        const char *name) {
    uint8_t out[64];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_avx512cd(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    expect_bytes(name, out, out_len, exp, exp_len);
}

static void run_dq_case(const as_x86_avx512dq_insn_t *in, const uint8_t *exp, size_t exp_len,
                        const char *name) {
    uint8_t out[64];
    size_t out_len = 0;
    char err[128];

    if (as_x86_encode_avx512dq(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    expect_bytes(name, out, out_len, exp, exp_len);
}

int main(void) {
    as_x86_avx512f_insn_t f;
    as_x86_avx512bw_insn_t bw;
    as_x86_avx512cd_insn_t cd;
    as_x86_avx512dq_insn_t dq;

    memset(&cd, 0, sizeof(cd));
    cd.mnemonic = "vpconflictd";
    cd.op_count = 2;
    cd.op1 = reg_op(AS_X86_REG_RCX);
    cd.op2 = reg_op(AS_X86_REG_RDX);
    cd.vector_bits = 256;
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x28, 0xc4, 0xca};
        run_cd_case(&cd, exp, sizeof(exp), "vpconflictd ymm");
    }

    memset(&dq, 0, sizeof(dq));
    dq.mnemonic = "vpmullq";
    dq.op_count = 3;
    dq.op1 = reg_op(AS_X86_REG_RCX);
    dq.op2 = reg_op(AS_X86_REG_RDX);
    dq.op3 = reg_op(AS_X86_REG_RBX);
    dq.vector_bits = 256;
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xed, 0x28, 0x40, 0xcb};
        run_dq_case(&dq, exp, sizeof(exp), "vpmullq ymm");
    }

    memset(&f, 0, sizeof(f));
    f.mnemonic = "vaddps";
    f.op_count = 3;
    f.op1 = reg_op(AS_X86_REG_RCX);
    f.op2 = reg_op(AS_X86_REG_RDX);
    f.op3 = reg_op(AS_X86_REG_RBX);
    f.vector_bits = 128;
    f.opmask = 1;
    f.zeroing = 1;
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x89, 0x58, 0xcb};
        run_f_case(&f, exp, sizeof(exp), "vaddps xmm{k}{z}");
    }

    memset(&bw, 0, sizeof(bw));
    bw.mnemonic = "vpaddb";
    bw.op_count = 3;
    bw.op1 = reg_op(AS_X86_REG_RCX);
    bw.op2 = reg_op(AS_X86_REG_RDX);
    bw.op3 = reg_op(AS_X86_REG_RBX);
    bw.vector_bits = 256;
    bw.opmask = 1;
    bw.zeroing = 1;
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6d, 0xa9, 0xfc, 0xcb};
        run_bw_case(&bw, exp, sizeof(exp), "vpaddb ymm{k}{z}");
    }

    memset(&f, 0, sizeof(f));
    f.mnemonic = "vaddps";
    f.op_count = 3;
    f.op1 = reg_op(AS_X86_REG_RCX);
    f.op2 = reg_op(AS_X86_REG_RDX);
    f.op3 = mem_base(AS_X86_REG_RAX);
    f.vector_bits = 256;
    f.broadcast = 1;
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x38, 0x58, 0x08};
        run_f_case(&f, exp, sizeof(exp), "vaddps ymm broadcast");
    }

    f.vector_bits = 128;
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x18, 0x58, 0x08};
        run_f_case(&f, exp, sizeof(exp), "vaddps xmm broadcast");
    }

    puts("ok");
    return 0;
}
