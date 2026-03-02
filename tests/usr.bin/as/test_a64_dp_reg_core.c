#include "as_a64_dp_reg.h"

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

static void run_case(const as_a64_dp_reg_insn_t *in, uint32_t exp, const char *name) {
    uint8_t out[8];
    size_t out_len = 0;
    char err[128];

    if (as_a64_encode_dp_reg(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
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
    as_a64_dp_reg_insn_t in;
    const struct {
        const char *mnemonic;
        uint32_t word;
    } cases[] = {
        {"add", 0x8b020020u},
        {"add.shift", 0x8b050c83u},
        {"sub.shift", 0xcb8814e6u},
        {"adds", 0xab0b0149u},
        {"subs", 0xeb0e01acu},
        {"add.ext", 0x8b314a0fu},
        {"sub.ext", 0xcb34c272u},
        {"and", 0x8a1702d5u},
        {"orr", 0xaa5a1338u},
        {"eor", 0xca9d079bu},
        {"orn", 0xaa220020u},
        {"eon", 0xca250083u},
        {"bic", 0x8a2800e6u},
        {"bics", 0xea2b0149u},
        {"ands", 0xea0e01acu},
        {"adc", 0x9a11020fu},
        {"adcs", 0xba140272u},
        {"sbc", 0xda1702d5u},
        {"sbcs", 0xfa1a0338u},
        {"madd", 0x9b020c20u},
        {"msub", 0x9b069ca4u},
        {"mul", 0x9b0a7d28u},
        {"mneg", 0x9b0dfd8bu},
        {"smaddl", 0x9b3045eeu},
        {"smsubl", 0x9b34d672u},
        {"umaddl", 0x9bb866f6u},
        {"umsubl", 0x9bbcf77au},
        {"smull", 0x9b227c20u},
        {"umull", 0x9ba57c83u},
        {"smulh", 0x9b487ce6u},
        {"umulh", 0x9bcb7d49u},
        {"sdiv", 0x9ace0dacu},
        {"udiv", 0x9ad10a0fu},
        {"cls", 0xdac01672u},
        {"clz", 0xdac012b4u},
        {"rbit", 0xdac002f6u},
        {"rev", 0xdac00f38u},
        {"rev16", 0xdac0077au},
        {"rev32", 0xdac00bbcu},
        {"csel", 0x9a820020u},
        {"csinc", 0x9a851483u},
        {"csinv", 0xda8820e6u},
        {"csneg", 0xda8b3549u},
        {"cinc", 0x9a8d55acu},
        {"cinv", 0xda8f41eeu},
        {"cneg", 0xda917630u},
        {"cset", 0x9a9f67f2u},
        {"csetm", 0xda9f93f3u},
        {"ccmn", 0xba559285u},
        {"ccmp", 0xfa57a2c9u},
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
