#include "as_a64_system.h"

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

static void run_case(const as_a64_system_insn_t *in, uint32_t exp, const char *name) {
    uint8_t out[8];
    size_t out_len = 0;
    char err[128];

    if (as_a64_encode_system(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
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
    as_a64_system_insn_t in;
    const struct {
        const char *mnemonic;
        uint32_t word;
    } cases[] = {
        {"mrs", 0xd53b4200u},
        {"msr", 0xd51b4201u},
        {"nop", 0xd503201fu},
        {"yield", 0xd503203fu},
        {"wfe", 0xd503205fu},
        {"wfi", 0xd503207fu},
        {"sev", 0xd503209fu},
        {"sevl", 0xd50320bfu},
        {"dmb", 0xd5033fbfu},
        {"dsb", 0xd5033a9fu},
        {"isb", 0xd5033fdfu},
        {"clrex", 0xd5033f5fu},
        {"sys", 0xd5087802u},
        {"sysl", 0xd5287803u},
        {"dc", 0xd50b7424u},
        {"ic", 0xd50b7525u},
        {"at", 0xd5087806u},
        {"tlbi", 0xd508871fu},
        {"hint", 0xd50320ffu},
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
