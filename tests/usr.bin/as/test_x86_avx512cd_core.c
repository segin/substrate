#include "as_x86_avx512cd.h"

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

int main(void) {
    const struct {
        const char *mnemonic;
        uint8_t exp[6];
    } cases[] = {
        {"vpconflictd", {0x62, 0xf2, 0x7d, 0x48, 0xc4, 0xca}},
        {"vpconflictq", {0x62, 0xf2, 0xfd, 0x48, 0xc4, 0xca}},
        {"vplzcntd", {0x62, 0xf2, 0x7d, 0x48, 0x44, 0xca}},
        {"vplzcntq", {0x62, 0xf2, 0xfd, 0x48, 0x44, 0xca}},
        {"vpbroadcastmb2q", {0x62, 0xf2, 0xfe, 0x48, 0x2a, 0xca}},
        {"vpbroadcastmw2d", {0x62, 0xf2, 0x7e, 0x48, 0x3a, 0xca}},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        as_x86_avx512cd_insn_t in;
        uint8_t out[32];
        size_t out_len = 0;
        char err[128];

        memset(&in, 0, sizeof(in));
        in.mnemonic = cases[i].mnemonic;
        in.op_count = 2;
        in.op1 = reg_op(AS_X86_REG_RCX);
        in.op2 = reg_op(AS_X86_REG_RDX);

        if (as_x86_encode_avx512cd(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
            fprintf(stderr, "%s encode error: %s\n", cases[i].mnemonic, err);
            fail("encode failed");
        }

        expect_bytes(cases[i].mnemonic, out, out_len, cases[i].exp, sizeof(cases[i].exp));
    }

    puts("ok");
    return 0;
}
