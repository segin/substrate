#include "as_a64_loadstore.h"

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

static void run_case(const as_a64_loadstore_insn_t *in, uint32_t exp, const char *name) {
    uint8_t out[8];
    size_t out_len = 0;
    char err[128];

    if (as_a64_encode_loadstore(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
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
    as_a64_loadstore_insn_t in;
    const struct {
        const char *mnemonic;
        uint32_t word;
    } cases[] = {
        {"ldr", 0xf9400820u},
        {"str", 0xf9000c62u},
        {"ldr.pre", 0xf8408ca4u},
        {"str.post", 0xf80084e6u},
        {"ldr.reg", 0xf86a6928u},
        {"ldrb", 0x3940058bu},
        {"strb", 0x390009cdu},
        {"ldrh", 0x79400a0fu},
        {"strh", 0x79000e51u},
        {"ldrsb", 0x39800e93u},
        {"ldrsh", 0x798012d5u},
        {"ldrsw", 0xb9800f17u},
        {"ldp", 0xa9410440u},
        {"stp", 0xa9bf10a3u},
        {"ldpsw", 0x69431d06u},
        {"ldnp", 0xa8422969u},
        {"stnp", 0xa83e35ccu},
        {"ldr.literal", 0x5800010fu},
        {"ldxr", 0xc85f7e30u},
        {"stxr", 0xc8127e93u},
        {"ldxrb", 0x085f7ed5u},
        {"stxrb", 0x08177f38u},
        {"ldxrh", 0x485f7f7au},
        {"stxrh", 0x481c7fddu},
        {"ldxp", 0xc87f0440u},
        {"stxp", 0xc82314c4u},
        {"ldar", 0xc8dffd07u},
        {"stlr", 0xc89ffd49u},
        {"ldarb", 0x08dffd8bu},
        {"stlrb", 0x089ffdcdu},
        {"ldarh", 0x48dffe0fu},
        {"stlrh", 0x489ffe51u},
        {"ldaxr", 0xc85ffe93u},
        {"stlxr", 0xc815fef6u},
        {"ldaxrb", 0x085fff38u},
        {"stlxrb", 0x081aff9bu},
        {"ldaxrh", 0x485fffddu},
        {"stlxrh", 0x4800fc41u},
        {"ldaxp", 0xc87f90a3u},
        {"stlxp", 0xc826a127u},
        {"prfm", 0xf9802140u},
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
