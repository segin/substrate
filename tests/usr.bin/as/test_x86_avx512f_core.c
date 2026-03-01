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

static void run_case(const as_x86_avx512f_insn_t *in, const uint8_t *exp, size_t exp_len,
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

int main(void) {
    as_x86_avx512f_insn_t in;

    memset(&in, 0, sizeof(in));
    in.mnemonic = "vaddps";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.vector_bits = 512;
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x48, 0x58, 0xcb};
        run_case(&in, exp, sizeof(exp), "vaddps");
    }

    in.mnemonic = "vaddpd";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xed, 0x48, 0x58, 0xcb};
        run_case(&in, exp, sizeof(exp), "vaddpd");
    }

    in.mnemonic = "vsub";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x48, 0x5c, 0xcb};
        run_case(&in, exp, sizeof(exp), "vsub(alias)");
    }

    in.mnemonic = "vmul";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x48, 0x59, 0xcb};
        run_case(&in, exp, sizeof(exp), "vmul(alias)");
    }

    in.mnemonic = "vdiv";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x48, 0x5e, 0xcb};
        run_case(&in, exp, sizeof(exp), "vdiv(alias)");
    }

    in.mnemonic = "vmax";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x48, 0x5f, 0xcb};
        run_case(&in, exp, sizeof(exp), "vmax(alias)");
    }

    in.mnemonic = "vmin";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x6c, 0x48, 0x5d, 0xcb};
        run_case(&in, exp, sizeof(exp), "vmin(alias)");
    }

    in.mnemonic = "vsqrt";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3.kind = AS_X86_OP_NONE;
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x7c, 0x48, 0x51, 0xca};
        run_case(&in, exp, sizeof(exp), "vsqrt(alias)");
    }

    in.mnemonic = "vrsqrt14ps";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x48, 0x4e, 0xca};
        run_case(&in, exp, sizeof(exp), "vrsqrt14ps");
    }

    in.mnemonic = "vrcp14ps";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x48, 0x4c, 0xca};
        run_case(&in, exp, sizeof(exp), "vrcp14ps");
    }

    in.mnemonic = "vfmadd132ps";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x6d, 0x48, 0x98, 0xcb};
        run_case(&in, exp, sizeof(exp), "vfmadd132ps");
    }

    in.mnemonic = "vcvtps2dq";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3.kind = AS_X86_OP_NONE;
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x7d, 0x48, 0x5b, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtps2dq");
    }

    in.mnemonic = "vcvtpd2dq";
    in.vector_bits = 256;
    {
        const uint8_t exp[] = {0x62, 0xf1, 0xff, 0x48, 0xe6, 0xca};
        run_case(&in, exp, sizeof(exp), "vcvtpd2dq");
    }

    in.mnemonic = "vbroadcastss";
    in.vector_bits = 512;
    in.op2 = mem_base(AS_X86_REG_RAX);
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x48, 0x18, 0x08};
        run_case(&in, exp, sizeof(exp), "vbroadcastss");
    }

    in.mnemonic = "vinsertf32x4";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.has_imm8 = 1;
    in.imm8 = 1;
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x6d, 0x48, 0x18, 0xcb, 0x01};
        run_case(&in, exp, sizeof(exp), "vinsertf32x4");
    }

    in.mnemonic = "vextractf32x4";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = 1;
    in.imm8 = 1;
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x7d, 0x48, 0x19, 0xd1, 0x01};
        run_case(&in, exp, sizeof(exp), "vextractf32x4");
    }

    in.mnemonic = "vpermps";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.has_imm8 = 0;
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x6d, 0x48, 0x16, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpermps");
    }

    in.mnemonic = "vpermi2d";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x6d, 0x48, 0x76, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpermi2d");
    }

    in.mnemonic = "vpermt2q";
    {
        const uint8_t exp[] = {0x62, 0xf2, 0xed, 0x48, 0x7e, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpermt2q");
    }

    in.mnemonic = "vshuff32x4";
    in.has_imm8 = 1;
    in.imm8 = 0x1b;
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x6d, 0x48, 0x23, 0xcb, 0x1b};
        run_case(&in, exp, sizeof(exp), "vshuff32x4");
    }

    in.mnemonic = "vcompressps";
    in.op_count = 2;
    in.op1 = mem_base(AS_X86_REG_RAX);
    in.op2 = reg_op(AS_X86_REG_RCX);
    in.has_imm8 = 0;
    in.opmask = 1;
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x49, 0x8a, 0x08};
        run_case(&in, exp, sizeof(exp), "vcompressps");
    }

    in.mnemonic = "vexpandps";
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = mem_base(AS_X86_REG_RAX);
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x49, 0x88, 0x08};
        run_case(&in, exp, sizeof(exp), "vexpandps");
    }

    in.mnemonic = "vgetexpps";
    in.opmask = 0;
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x48, 0x42, 0xca};
        run_case(&in, exp, sizeof(exp), "vgetexpps");
    }

    in.mnemonic = "vgetmantps";
    in.has_imm8 = 1;
    in.imm8 = 1;
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x7d, 0x48, 0x26, 0xca, 0x01};
        run_case(&in, exp, sizeof(exp), "vgetmantps");
    }

    in.mnemonic = "vscalefps";
    in.has_imm8 = 0;
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x6d, 0x48, 0x2c, 0xcb};
        run_case(&in, exp, sizeof(exp), "vscalefps");
    }

    in.mnemonic = "vfixupimmps";
    in.has_imm8 = 1;
    in.imm8 = 1;
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x6d, 0x48, 0x54, 0xcb, 0x01};
        run_case(&in, exp, sizeof(exp), "vfixupimmps");
    }

    in.mnemonic = "vrndscaleps";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = 1;
    in.imm8 = 4;
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x7d, 0x48, 0x08, 0xca, 0x04};
        run_case(&in, exp, sizeof(exp), "vrndscaleps");
    }

    in.mnemonic = "vcmpps";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX); /* k1 */
    in.op2 = reg_op(AS_X86_REG_RCX);
    in.op3 = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = 1;
    in.imm8 = 0x1f;
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x74, 0x48, 0xc2, 0xca, 0x1f};
        run_case(&in, exp, sizeof(exp), "vcmpps");
    }

    in.mnemonic = "vpcmpd";
    in.imm8 = 1;
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x75, 0x48, 0x1f, 0xca, 0x01};
        run_case(&in, exp, sizeof(exp), "vpcmpd");
    }

    in.mnemonic = "vpblendmd";
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.has_imm8 = 0;
    in.opmask = 1;
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x6d, 0x49, 0x64, 0xcb};
        run_case(&in, exp, sizeof(exp), "vpblendmd");
    }

    in.mnemonic = "vpternlogd";
    in.has_imm8 = 1;
    in.imm8 = 0x96;
    in.opmask = 0;
    {
        const uint8_t exp[] = {0x62, 0xf3, 0x6d, 0x48, 0x25, 0xcb, 0x96};
        run_case(&in, exp, sizeof(exp), "vpternlogd");
    }

    in.mnemonic = "vpmovdb";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.has_imm8 = 0;
    in.vector_bits = 512;
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7e, 0x48, 0x31, 0xd1};
        run_case(&in, exp, sizeof(exp), "vpmovdb");
    }

    in.mnemonic = "vmovdqa32";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x7d, 0x48, 0x6f, 0xca};
        run_case(&in, exp, sizeof(exp), "vmovdqa32");
    }

    in.mnemonic = "vmovdqu8";
    {
        const uint8_t exp[] = {0x62, 0xf1, 0x7f, 0x48, 0x6f, 0xca};
        run_case(&in, exp, sizeof(exp), "vmovdqu8");
    }

    in.mnemonic = "vgatherdps";
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = mem_vsib(AS_X86_REG_RAX, AS_X86_REG_RDX, 4);
    in.vector_bits = 512;
    in.opmask = 1;
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x49, 0x92, 0x0c, 0x90};
        run_case(&in, exp, sizeof(exp), "vgatherdps");
    }

    in.mnemonic = "vscatterdps";
    in.op1 = mem_vsib(AS_X86_REG_RAX, AS_X86_REG_RDX, 4);
    in.op2 = reg_op(AS_X86_REG_RCX);
    {
        const uint8_t exp[] = {0x62, 0xf2, 0x7d, 0x49, 0xa2, 0x0c, 0x90};
        run_case(&in, exp, sizeof(exp), "vscatterdps");
    }

    memset(&in, 0, sizeof(in));
    in.op_count = 2;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);

    in.mnemonic = "kmovw";
    {
        const uint8_t exp[] = {0xc5, 0xf8, 0x90, 0xca};
        run_case(&in, exp, sizeof(exp), "kmovw");
    }

    in.mnemonic = "kmovd";
    {
        const uint8_t exp[] = {0xc4, 0xe1, 0xf9, 0x90, 0xca};
        run_case(&in, exp, sizeof(exp), "kmovd");
    }

    in.mnemonic = "knotw";
    {
        const uint8_t exp[] = {0xc5, 0xf8, 0x44, 0xca};
        run_case(&in, exp, sizeof(exp), "knotw");
    }

    in.mnemonic = "kortestw";
    {
        const uint8_t exp[] = {0xc5, 0xf8, 0x98, 0xca};
        run_case(&in, exp, sizeof(exp), "kortestw");
    }

    in.mnemonic = "ktestw";
    {
        const uint8_t exp[] = {0xc5, 0xf8, 0x99, 0xca};
        run_case(&in, exp, sizeof(exp), "ktestw");
    }

    in.mnemonic = "kshiftlw";
    in.has_imm8 = 1;
    in.imm8 = 3;
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0xf9, 0x32, 0xca, 0x03};
        run_case(&in, exp, sizeof(exp), "kshiftlw");
    }

    in.mnemonic = "kshiftrw";
    {
        const uint8_t exp[] = {0xc4, 0xe3, 0xf9, 0x30, 0xca, 0x03};
        run_case(&in, exp, sizeof(exp), "kshiftrw");
    }

    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.has_imm8 = 0;

    in.mnemonic = "kandw";
    {
        const uint8_t exp[] = {0xc5, 0xec, 0x41, 0xcb};
        run_case(&in, exp, sizeof(exp), "kandw");
    }

    in.mnemonic = "korw";
    {
        const uint8_t exp[] = {0xc5, 0xec, 0x45, 0xcb};
        run_case(&in, exp, sizeof(exp), "korw");
    }

    in.mnemonic = "kxorw";
    {
        const uint8_t exp[] = {0xc5, 0xec, 0x47, 0xcb};
        run_case(&in, exp, sizeof(exp), "kxorw");
    }

    in.mnemonic = "kxnorw";
    {
        const uint8_t exp[] = {0xc5, 0xec, 0x46, 0xcb};
        run_case(&in, exp, sizeof(exp), "kxnorw");
    }

    in.mnemonic = "kandnw";
    {
        const uint8_t exp[] = {0xc5, 0xec, 0x42, 0xcb};
        run_case(&in, exp, sizeof(exp), "kandnw");
    }

    in.mnemonic = "kunpckbw";
    {
        const uint8_t exp[] = {0xc5, 0xed, 0x4b, 0xcb};
        run_case(&in, exp, sizeof(exp), "kunpckbw");
    }

    in.mnemonic = "kunpckwd";
    {
        const uint8_t exp[] = {0xc5, 0xec, 0x4b, 0xcb};
        run_case(&in, exp, sizeof(exp), "kunpckwd");
    }

    in.mnemonic = "kunpckdq";
    {
        const uint8_t exp[] = {0xc4, 0xe1, 0xec, 0x4b, 0xcb};
        run_case(&in, exp, sizeof(exp), "kunpckdq");
    }

    in.mnemonic = "kaddw";
    {
        const uint8_t exp[] = {0xc5, 0xec, 0x4a, 0xcb};
        run_case(&in, exp, sizeof(exp), "kaddw");
    }

    puts("ok");
    return 0;
}
