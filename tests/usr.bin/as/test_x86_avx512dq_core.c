#include "as_x86_avx512dq.h"

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

static void run_case(const as_x86_avx512dq_insn_t *in, const uint8_t *exp, size_t exp_len,
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
    as_x86_avx512dq_insn_t in;

    memset(&in, 0, sizeof(in));
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);

    in.mnemonic = "vcvtps2qq";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x7d, 0x48, 0x7b, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtps2qq");
    }

    in.mnemonic = "vcvtps2uqq";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x7d, 0x48, 0x79, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtps2uqq");
    }

    in.mnemonic = "vcvtpd2qq";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xfd, 0x48, 0x7b, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtpd2qq");
    }

    in.mnemonic = "vcvtpd2uqq";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xfd, 0x48, 0x79, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtpd2uqq");
    }

    in.mnemonic = "vcvttps2qq";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x7d, 0x48, 0x7a, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvttps2qq");
    }

    in.mnemonic = "vcvttps2uqq";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x7d, 0x48, 0x78, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvttps2uqq");
    }

    in.mnemonic = "vcvttpd2qq";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xfd, 0x48, 0x7a, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvttpd2qq");
    }

    in.mnemonic = "vcvttpd2uqq";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xfd, 0x48, 0x78, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvttpd2uqq");
    }

    in.mnemonic = "vcvtqq2ps";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xfc, 0x48, 0x5b, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtqq2ps");
    }

    in.mnemonic = "vcvtqq2pd";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xfe, 0x48, 0xe6, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtqq2pd");
    }

    in.mnemonic = "vcvtuqq2ps";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xff, 0x48, 0x7a, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtuqq2ps");
    }

    in.mnemonic = "vcvtuqq2pd";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xfe, 0x48, 0x7a, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtuqq2pd");
    }

    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);

    in.mnemonic = "vpmullq";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xed, 0x48, 0x40, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpmullq");
    }

    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RCX);
    in.op3.kind = AS_X86_OP_NONE;

    in.mnemonic = "vpmovm2d";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7e, 0x48, 0x38, 0xc9};
        run_case(&in, exp, sizeof(exp), "vpmovm2d");
    }

    in.mnemonic = "vpmovm2q";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xfe, 0x48, 0x38, 0xc9};
        run_case(&in, exp, sizeof(exp), "vpmovm2q");
    }

    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);

    in.mnemonic = "vpmovd2m";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7e, 0x48, 0x39, 0xca};
        run_case(&in, exp, sizeof(exp), "vpmovd2m");
    }

    in.mnemonic = "vpmovq2m";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xfe, 0x48, 0x39, 0xca};
        run_case(&in, exp, sizeof(exp), "vpmovq2m");
    }

    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.has_imm8 = 1;
    in.imm8 = 1;

    in.mnemonic = "vinserti64x2";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0xed, 0x48, 0x38, 0xcb, 0x01};
        run_case(&in, exp, sizeof(exp), "vinserti64x2");
    }

    in.mnemonic = "vinserti32x8";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x6d, 0x48, 0x3a, 0xcb, 0x01};
        run_case(&in, exp, sizeof(exp), "vinserti32x8");
    }

    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3.kind = AS_X86_OP_NONE;

    in.mnemonic = "vextracti64x2";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0xfd, 0x48, 0x39, 0xd1, 0x01};
        run_case(&in, exp, sizeof(exp), "vextracti64x2");
    }

    in.mnemonic = "vextracti32x8";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x7d, 0x48, 0x3b, 0xd1, 0x01};
        run_case(&in, exp, sizeof(exp), "vextracti32x8");
    }

    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.imm8 = 3;

    in.mnemonic = "vrangeps";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x6d, 0x48, 0x50, 0xcb, 0x03};
        run_case(&in, exp, sizeof(exp), "vrangeps");
    }

    in.mnemonic = "vrangepd";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0xed, 0x48, 0x50, 0xcb, 0x03};
        run_case(&in, exp, sizeof(exp), "vrangepd");
    }

    in.mnemonic = "vrangess";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x6d, 0x08, 0x51, 0xcb, 0x03};
        run_case(&in, exp, sizeof(exp), "vrangess");
    }

    in.mnemonic = "vrangesd";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0xed, 0x08, 0x51, 0xcb, 0x03};
        run_case(&in, exp, sizeof(exp), "vrangesd");
    }

    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3.kind = AS_X86_OP_NONE;

    in.mnemonic = "vreduceps";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x7d, 0x48, 0x56, 0xca, 0x03};
        run_case(&in, exp, sizeof(exp), "vreduceps");
    }

    in.mnemonic = "vreducepd";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0xfd, 0x48, 0x56, 0xca, 0x03};
        run_case(&in, exp, sizeof(exp), "vreducepd");
    }

    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);

    in.mnemonic = "vreducess";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x6d, 0x08, 0x57, 0xcb, 0x03};
        run_case(&in, exp, sizeof(exp), "vreducess");
    }

    in.mnemonic = "vreducesd";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0xed, 0x08, 0x57, 0xcb, 0x03};
        run_case(&in, exp, sizeof(exp), "vreducesd");
    }

    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3.kind = AS_X86_OP_NONE;

    in.mnemonic = "vfpclassps";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x7d, 0x48, 0x66, 0xca, 0x03};
        run_case(&in, exp, sizeof(exp), "vfpclassps");
    }

    in.mnemonic = "vfpclasspd";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0xfd, 0x48, 0x66, 0xca, 0x03};
        run_case(&in, exp, sizeof(exp), "vfpclasspd");
    }

    in.mnemonic = "vfpclassss";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x7d, 0x08, 0x67, 0xca, 0x03};
        run_case(&in, exp, sizeof(exp), "vfpclassss");
    }

    in.mnemonic = "vfpclasssd";
    {
        const uint8_t exp[] = {0x62, 0xf3, 0xfd, 0x08, 0x67, 0xca, 0x03};
        run_case(&in, exp, sizeof(exp), "vfpclasssd");
    }

    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.has_imm8 = 0;

    in.mnemonic = "vandps";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x48, 0x54, 0xcb};
        run_case(&in, exp, sizeof(exp), "vandps");
    }

    in.mnemonic = "vandpd";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xed, 0x48, 0x54, 0xcb};
        run_case(&in, exp, sizeof(exp), "vandpd");
    }

    in.mnemonic = "vorps";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x48, 0x56, 0xcb};
        run_case(&in, exp, sizeof(exp), "vorps");
    }

    in.mnemonic = "vorpd";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xed, 0x48, 0x56, 0xcb};
        run_case(&in, exp, sizeof(exp), "vorpd");
    }

    in.mnemonic = "vxorps";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x48, 0x57, 0xcb};
        run_case(&in, exp, sizeof(exp), "vxorps");
    }

    in.mnemonic = "vxorpd";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xed, 0x48, 0x57, 0xcb};
        run_case(&in, exp, sizeof(exp), "vxorpd");
    }

    in.mnemonic = "vandnps";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x48, 0x55, 0xcb};
        run_case(&in, exp, sizeof(exp), "vandnps");
    }

    in.mnemonic = "vandnpd";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xed, 0x48, 0x55, 0xcb};
        run_case(&in, exp, sizeof(exp), "vandnpd");
    }

    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = mem_base(AS_X86_REG_RAX);
    in.op3.kind = AS_X86_OP_NONE;

    in.mnemonic = "vbroadcastf32x2";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x48, 0x19, 0x08};
        run_case(&in, exp, sizeof(exp), "vbroadcastf32x2");
    }

    in.mnemonic = "vbroadcastf32x8";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x48, 0x1b, 0x08};
        run_case(&in, exp, sizeof(exp), "vbroadcastf32x8");
    }

    in.mnemonic = "vbroadcasti32x2";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x48, 0x59, 0x08};
        run_case(&in, exp, sizeof(exp), "vbroadcasti32x2");
    }

    in.mnemonic = "vbroadcasti32x8";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x48, 0x5b, 0x08};
        run_case(&in, exp, sizeof(exp), "vbroadcasti32x8");
    }

    puts("ok");
    return 0;
}
