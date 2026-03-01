#include "as_x86_fma.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    uint8_t base;
    int packed_only;
} fam_t;

typedef struct {
    const char *name;
    uint8_t off;
} form_t;

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

static void run_one(const char *mnemonic, unsigned vector_bits, uint8_t opcode, int vex_w,
                    int vex_l) {
    as_x86_fma_insn_t in;
    uint8_t out[32];
    size_t out_len = 0;
    char err[128];
    uint8_t exp[5];

    memset(&in, 0, sizeof(in));
    in.mnemonic = mnemonic;
    in.op_count = 3;
    in.op1 = reg_op(AS_X86_REG_RCX);
    in.op2 = reg_op(AS_X86_REG_RDX);
    in.op3 = reg_op(AS_X86_REG_RBX);
    in.vector_bits = vector_bits;

    if (as_x86_encode_fma(&in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "encode error for %s: %s\n", mnemonic, err);
        fail("encode failed");
    }

    exp[0] = 0xc4;
    exp[1] = 0xe2;
    exp[2] = (uint8_t)((vex_w ? 0x80 : 0x00) | (0x0d << 3) | (vex_l ? 0x04 : 0x00) | 0x01);
    exp[3] = opcode;
    exp[4] = 0xcb;

    expect_bytes(mnemonic, out, out_len, exp, sizeof(exp));
}

int main(void) {
    const fam_t fams[] = {
        {"fmadd", 0x98, 0},
        {"fmsub", 0x9a, 0},
        {"fnmadd", 0x9c, 0},
        {"fnmsub", 0x9e, 0},
        {"fmaddsub", 0x96, 1},
        {"fmsubadd", 0x97, 1},
    };
    const form_t forms[] = {
        {"132", 0x00},
        {"213", 0x10},
        {"231", 0x20},
    };
    const char *suffixes_all[] = {"ps", "pd", "ss", "sd"};
    const char *suffixes_packed[] = {"ps", "pd"};
    size_t i;
    size_t j;
    size_t k;

    for (i = 0; i < sizeof(fams) / sizeof(fams[0]); ++i) {
        const char **suffixes = fams[i].packed_only ? suffixes_packed : suffixes_all;
        size_t suffix_count = fams[i].packed_only ? 2 : 4;

        for (j = 0; j < sizeof(forms) / sizeof(forms[0]); ++j) {
            for (k = 0; k < suffix_count; ++k) {
                char mnemonic[64];
                const char *sfx = suffixes[k];
                unsigned vector_bits;
                uint8_t opcode;
                int vex_w;
                int vex_l;

                snprintf(mnemonic, sizeof(mnemonic), "v%s%s%s", fams[i].name, forms[j].name,
                         sfx);

                if (strcmp(sfx, "ps") == 0 || strcmp(sfx, "pd") == 0) {
                    vector_bits = 128;
                    opcode = (uint8_t)(fams[i].base + forms[j].off);
                    vex_w = (strcmp(sfx, "pd") == 0);
                    vex_l = 0;
                    run_one(mnemonic, vector_bits, opcode, vex_w, vex_l);

                    vector_bits = 256;
                    vex_l = 1;
                    run_one(mnemonic, vector_bits, opcode, vex_w, vex_l);
                } else {
                    vector_bits = 128;
                    opcode = (uint8_t)(fams[i].base + forms[j].off + 1);
                    vex_w = (strcmp(sfx, "sd") == 0);
                    vex_l = 0;
                    run_one(mnemonic, vector_bits, opcode, vex_w, vex_l);
                }
            }
        }
    }

    {
        as_x86_fma_insn_t in;
        uint8_t out[32];
        size_t out_len = 0;
        char err[128];

        memset(&in, 0, sizeof(in));
        in.mnemonic = "vfmadd132ss";
        in.op_count = 3;
        in.op1 = reg_op(AS_X86_REG_RCX);
        in.op2 = reg_op(AS_X86_REG_RDX);
        in.op3 = reg_op(AS_X86_REG_RBX);
        in.vector_bits = 256;

        if (as_x86_encode_fma(&in, out, sizeof(out), &out_len, err, sizeof(err)) == 0) {
            fail("scalar FMA accepted 256-bit vector");
        }
    }

    puts("ok");
    return 0;
}
