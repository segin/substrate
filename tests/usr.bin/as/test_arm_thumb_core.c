#include "as_arm_encode.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void expect_u32(const char *name, uint32_t got, uint32_t exp) {
    if (got != exp) {
        fprintf(stderr, "%s mismatch: got=0x%08x exp=0x%08x\n", name, got, exp);
        fail("value mismatch");
    }
}

static void expect_str(const char *name, const char *got, const char *exp) {
    if (strcmp(got, exp) != 0) {
        fprintf(stderr, "%s mismatch: got=%s exp=%s\n", name, got, exp);
        fail("string mismatch");
    }
}

static void verify_unified_parse(void) {
    char base[32];
    as_arm_thumb_width_t width;

    if (as_arm_thumb_parse_unified("add", base, sizeof(base), &width) != 0 ||
        width != AS_ARM_THUMB_WIDTH_AUTO) {
        fail("parse auto width failed");
    }
    expect_str("base-auto", base, "add");

    if (as_arm_thumb_parse_unified("ADD.W", base, sizeof(base), &width) != 0 ||
        width != AS_ARM_THUMB_WIDTH_WIDE) {
        fail("parse .w failed");
    }
    expect_str("base-wide", base, "add");

    if (as_arm_thumb_parse_unified("add.n", base, sizeof(base), &width) != 0 ||
        width != AS_ARM_THUMB_WIDTH_NARROW) {
        fail("parse .n failed");
    }
    expect_str("base-narrow", base, "add");
}

static void verify_thumb_add_encoding(void) {
    uint32_t word;
    int is_wide;

    if (as_arm_thumb_encode_add_imm(1, 1, 1, AS_ARM_THUMB_WIDTH_AUTO, &word, &is_wide) != 0 ||
        is_wide != 0) {
        fail("auto narrow selection failed");
    }
    expect_u32("adds-r1-imm1", word, 0x00003101u);

    if (as_arm_thumb_encode_add_imm(8, 9, 0x123, AS_ARM_THUMB_WIDTH_AUTO, &word, &is_wide) != 0 ||
        is_wide != 1) {
        fail("auto wide selection failed");
    }
    expect_u32("addw-r8-r9-0x123", word, 0xf2091823u);

    if (as_arm_thumb_encode_add_imm(1, 1, 1, AS_ARM_THUMB_WIDTH_WIDE, &word, &is_wide) != 0 ||
        is_wide != 1) {
        fail("explicit wide failed");
    }
    expect_u32("addw-r1-r1-1", word, 0xf2010101u);

    if (as_arm_thumb_encode_add_imm(8, 9, 1, AS_ARM_THUMB_WIDTH_NARROW, &word, &is_wide) == 0) {
        fail("narrow should reject high-register form");
    }

    if (as_arm_thumb_encode_add_imm(0, 0, 0x1fff, AS_ARM_THUMB_WIDTH_AUTO, &word, &is_wide) == 0) {
        fail("auto should reject out-of-range immediate");
    }
}

static void verify_syntax_state(void) {
    as_arm_state_ctx_t st;

    as_arm_state_init(&st);
    if (st.unified_syntax != 1) {
        fail("unified syntax default failed");
    }

    if (as_arm_apply_directive(&st, ".syntax divided") != 0 || st.unified_syntax != 0) {
        fail(".syntax divided failed");
    }

    if (as_arm_apply_directive(&st, ".syntax unified") != 0 || st.unified_syntax != 1) {
        fail(".syntax unified failed");
    }
}

int main(void) {
    verify_unified_parse();
    verify_thumb_add_encoding();
    verify_syntax_state();
    puts("ok");
    return 0;
}
