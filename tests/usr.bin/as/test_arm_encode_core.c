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

static void verify_conditions(void) {
    const struct {
        const char *name;
        as_arm_cond_t cond;
    } cases[] = {
        {"eq", AS_ARM_COND_EQ},
        {"ne", AS_ARM_COND_NE},
        {"cs", AS_ARM_COND_CS},
        {"hs", AS_ARM_COND_CS},
        {"cc", AS_ARM_COND_CC},
        {"lo", AS_ARM_COND_CC},
        {"mi", AS_ARM_COND_MI},
        {"pl", AS_ARM_COND_PL},
        {"vs", AS_ARM_COND_VS},
        {"vc", AS_ARM_COND_VC},
        {"hi", AS_ARM_COND_HI},
        {"ls", AS_ARM_COND_LS},
        {"ge", AS_ARM_COND_GE},
        {"lt", AS_ARM_COND_LT},
        {"gt", AS_ARM_COND_GT},
        {"le", AS_ARM_COND_LE},
        {"al", AS_ARM_COND_AL},
        {"nv", AS_ARM_COND_NV},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        as_arm_cond_t cond;
        if (as_arm_cond_from_string(cases[i].name, &cond) != 0 || cond != cases[i].cond) {
            fail("condition decode failed");
        }
    }
}

static void verify_pack_and_shifts(void) {
    uint32_t w;
    as_arm_shift_spec_t sh;

    if (as_arm_pack_cond_class(AS_ARM_COND_EQ, 5, 0x00123456u, &w) != 0) {
        fail("pack cond/class failed");
    }
    expect_u32("pack-cond-class", w, 0x0a123456u);

    memset(&sh, 0, sizeof(sh));
    sh.kind = AS_ARM_SHIFT_LSL;
    sh.by_reg = 0;
    sh.amount = 3;
    if (as_arm_encode_operand2_reg(2, &sh, &w) != 0) {
        fail("lsl imm shift failed");
    }
    expect_u32("lsl-imm", w, 0x00000182u);

    sh.kind = AS_ARM_SHIFT_LSR;
    sh.by_reg = 1;
    sh.amount = 4;
    if (as_arm_encode_operand2_reg(2, &sh, &w) != 0) {
        fail("lsr reg shift failed");
    }
    expect_u32("lsr-reg", w, 0x00000432u);

    sh.kind = AS_ARM_SHIFT_RRX;
    sh.by_reg = 0;
    sh.amount = 0;
    if (as_arm_encode_operand2_reg(2, &sh, &w) != 0) {
        fail("rrx shift failed");
    }
    expect_u32("rrx", w, 0x00000062u);

    if (as_arm_encode_operand2_imm(0xff000000u, &w) != 0) {
        fail("imm rotate encode failed");
    }
    expect_u32("imm-rotate", w, 0x000004ffu);
}

static void verify_addr_modes(void) {
    as_arm_addr_mode2_t m;
    uint32_t bits;
    uint8_t p;
    uint8_t u;

    memset(&m, 0, sizeof(m));
    m.rn = 1;
    m.pre_indexed = 1;
    m.add = 1;
    m.writeback = 1;
    m.imm12 = 0x20;
    if (as_arm_encode_addr_mode2(&m, &bits) != 0) {
        fail("mode2 imm encode failed");
    }
    expect_u32("mode2-imm", bits, 0x01a10020u);

    memset(&m, 0, sizeof(m));
    m.rn = 3;
    m.pre_indexed = 0;
    m.add = 0;
    m.writeback = 1;
    m.is_reg_offset = 1;
    m.rm = 4;
    m.shift.kind = AS_ARM_SHIFT_LSL;
    m.shift.by_reg = 0;
    m.shift.amount = 2;
    if (as_arm_encode_addr_mode2(&m, &bits) != 0) {
        fail("mode2 reg encode failed");
    }
    expect_u32("mode2-reg", bits, 0x02230104u);

    if (as_arm_ldm_mode_to_pu(AS_ARM_LDM_IA, &p, &u) != 0 || p != 0 || u != 1) {
        fail("ldm ia mapping failed");
    }
    if (as_arm_ldm_mode_to_pu(AS_ARM_LDM_FD, &p, &u) != 0 || p != 1 || u != 0) {
        fail("ldm fd mapping failed");
    }
}

static void verify_mnemonic_split(void) {
    char base[32];
    as_arm_cond_t cond;
    int setflags;

    if (as_arm_split_mnemonic("addeqs", base, sizeof(base), &cond, &setflags) != 0 ||
        strcmp(base, "add") != 0 || cond != AS_ARM_COND_EQ || setflags != 1) {
        fail("split addeqs failed");
    }

    if (as_arm_split_mnemonic("movne", base, sizeof(base), &cond, &setflags) != 0 ||
        strcmp(base, "mov") != 0 || cond != AS_ARM_COND_NE || setflags != 0) {
        fail("split movne failed");
    }

    if (as_arm_split_mnemonic("adds", base, sizeof(base), &cond, &setflags) != 0 ||
        strcmp(base, "add") != 0 || cond != AS_ARM_COND_AL || setflags != 1) {
        fail("split adds failed");
    }
}

static void verify_state_and_interwork(void) {
    as_arm_state_ctx_t st;
    uint32_t w;

    as_arm_state_init(&st);
    if (st.mode != AS_ARM_MODE_ARM) {
        fail("default arm mode failed");
    }
    if (as_arm_apply_directive(&st, ".thumb") != 0 || st.mode != AS_ARM_MODE_THUMB) {
        fail(".thumb directive failed");
    }
    if (as_arm_apply_directive(&st, ".thumb_func") != 0 || st.thumb_func != 1) {
        fail(".thumb_func directive failed");
    }
    if (as_arm_apply_directive(&st, ".arm") != 0 || st.mode != AS_ARM_MODE_ARM || st.thumb_func != 0) {
        fail(".arm directive failed");
    }

    if (as_arm_encode_bx(AS_ARM_COND_NE, 3, &w) != 0) {
        fail("bx encode failed");
    }
    expect_u32("bx", w, 0x112fff13u);

    if (as_arm_encode_blx(AS_ARM_COND_AL, 4, &w) != 0) {
        fail("blx encode failed");
    }
    expect_u32("blx", w, 0xe12fff34u);
}

static void verify_it_blocks(void) {
    as_arm_state_ctx_t st;
    as_arm_cond_t cond;

    as_arm_state_init(&st);
    if (as_arm_it_start(&st, AS_ARM_COND_EQ, "te") == 0) {
        fail("it should fail outside thumb mode");
    }

    if (as_arm_apply_directive(&st, ".thumb") != 0) {
        fail("thumb mode set failed");
    }

    if (as_arm_it_start(&st, AS_ARM_COND_EQ, "te") != 0) {
        fail("it start failed");
    }
    if (as_arm_it_next_cond(&st, &cond) != 0 || cond != AS_ARM_COND_EQ) {
        fail("it slot0 failed");
    }
    if (as_arm_it_next_cond(&st, &cond) != 0 || cond != AS_ARM_COND_EQ) {
        fail("it slot1 failed");
    }
    if (as_arm_it_next_cond(&st, &cond) != 0 || cond != AS_ARM_COND_NE) {
        fail("it slot2 failed");
    }
    if (as_arm_it_next_cond(&st, &cond) == 0) {
        fail("it should be exhausted");
    }
}

int main(void) {
    verify_conditions();
    verify_pack_and_shifts();
    verify_addr_modes();
    verify_mnemonic_split();
    verify_state_and_interwork();
    verify_it_blocks();
    puts("ok");
    return 0;
}
