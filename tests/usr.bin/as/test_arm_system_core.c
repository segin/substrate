#include "as_arm_system.h"

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

static void run_case(const as_arm_system_insn_t *in, uint32_t exp, const char *name) {
    uint8_t out[8];
    size_t out_len = 0;
    char err[128];

    if (as_arm_encode_system(in, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
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
    as_arm_system_insn_t in;
    const struct {
        const char *mnemonic;
        uint32_t word;
    } descs[] = {
        {"mrs", 0xe10f1000u},
        {"msr", 0xe121f001u},
        {"cpsie", 0xf1080080u},
        {"cpsid", 0xf10c0080u},
        {"setend", 0xf1010200u},
        {"dmb", 0xf57ff05fu},
        {"dsb", 0xf57ff04fu},
        {"isb", 0xf57ff06fu},
        {"wfi", 0xe320f003u},
        {"wfe", 0xe320f002u},
        {"sev", 0xe320f004u},
        {"yield", 0xe320f001u},
        {"nop", 0xe320f000u},
        {"cdp", 0xee021f03u},
        {"cdp2", 0xfe021f03u},
        {"mcr", 0xee021f13u},
        {"mcr2", 0xfe021f13u},
        {"mrc", 0xee121f13u},
        {"mrc2", 0xfe121f13u},
        {"mcrr", 0xec421f03u},
        {"mcrr2", 0xfc421f03u},
        {"mrrc", 0xec521f03u},
        {"mrrc2", 0xfc521f03u},
        {"ldc", 0xecb21f01u},
        {"ldc2", 0xfcb21f01u},
        {"stc", 0xeca21f01u},
        {"stc2", 0xfca21f01u},
        {"clrex", 0xf57ff01fu},
    };
    size_t i;

    memset(&in, 0, sizeof(in));
    in.cond = AS_ARM_COND_AL;

    in.mnemonic = "svc";
    in.has_imm = 1;
    in.imm = 1;
    run_case(&in, 0xef000001u, "svc #1");

    in.mnemonic = "swi";
    in.imm = 2;
    run_case(&in, 0xef000002u, "swi #2");

    in.mnemonic = "bkpt";
    in.imm = 3;
    run_case(&in, 0xe1200073u, "bkpt #3");

    in.mnemonic = "hlt";
    in.imm = 1;
    run_case(&in, 0xe1000071u, "hlt #1");

    in.mnemonic = "dbg";
    in.imm = 1;
    run_case(&in, 0xe320f0f1u, "dbg #1");

    in.has_imm = 0;
    in.cond = AS_ARM_COND_NE;
    in.mnemonic = "mcr";
    run_case(&in, 0x1e021f13u, "mcrne");

    in.cond = AS_ARM_COND_AL;
    for (i = 0; i < sizeof(descs) / sizeof(descs[0]); ++i) {
        in.mnemonic = descs[i].mnemonic;
        run_case(&in, descs[i].word, descs[i].mnemonic);
    }

    puts("ok");
    return 0;
}
