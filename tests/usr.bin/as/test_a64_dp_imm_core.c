#include "as_a64_dp_imm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static uint32_t read32le(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void run_case(const as_a64_dp_imm_insn_t *in, uint32_t exp, const char *name) {
    uint8_t out[8];
    size_t out_len = 0;
    char err[128];

    if (as_a64_encode_dp_imm(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s encode error: %s\n", name, err);
        fail("encode failed");
    }

    if (out_len != 4 || read32le(out) != exp) {
        fprintf(stderr, "%s mismatch: len=%zu word=0x%08x exp=0x%08x\n",
                name, out_len, read32le(out), exp);
        fail("word mismatch");
    }
}

int main(void) {
    as_a64_dp_imm_insn_t in;
    const struct {
        const char *mnemonic;
        uint32_t word;
    } cases[] = {
        {"add", 0x91048c20u},
        {"add.lsl12", 0x916af062u},
        {"adds", 0xb1001ca4u},
        {"sub", 0xd10c84e6u},
        {"subs", 0xf1400528u},
        {"and", 0x92089d6au},
        {"orr", 0xb2009dacu},
        {"eor", 0xd24015eeu},
        {"ands", 0xf2402e30u},
        {"movn", 0x92824688u},
        {"movz", 0xd2b579a9u},
        {"movk", 0xf2caaab4u},
        {"adr", 0x10000800u},
        {"adrp", 0xb0091a21u},
        {"bfm", 0xb3455062u},
        {"sbfm", 0x934c50a4u},
        {"ubfm", 0xd34450e6u},
        {"bfi", 0xb37c2c20u},
        {"bfxil", 0xb3483c62u},
        {"sbfx", 0x934330a4u},
        {"ubfx", 0xd3453ce6u},
        {"sxtb", 0x93401d28u},
        {"sxth", 0x93403d6au},
        {"sxtw", 0x93407dacu},
        {"uxtb", 0x53001deeu},
        {"uxth", 0x53003e30u},
        {"extr", 0x93d41e72u},
    };
    size_t i;

    memset(&in, 0, sizeof(in));
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        in.mnemonic = cases[i].mnemonic;
        run_case(&in, cases[i].word, cases[i].mnemonic);
    }

    puts("ok");
    return 0;
}
