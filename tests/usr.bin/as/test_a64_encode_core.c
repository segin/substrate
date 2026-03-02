#include "as_a64_encode.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static uint64_t mask_for_bits(unsigned bits) {
    if (bits == 64u) {
        return ~0ull;
    }
    return (1ull << bits) - 1ull;
}

static uint64_t ror_elem(uint64_t v, unsigned r, unsigned bits) {
    uint64_t m = mask_for_bits(bits);

    v &= m;
    r %= bits;
    if (r == 0u) {
        return v;
    }
    return ((v >> r) | (v << (bits - r))) & m;
}

static uint64_t replicate_elem(uint64_t elem, unsigned elem_bits, unsigned reg_bits) {
    uint64_t out = 0;
    unsigned pos;

    for (pos = 0; pos < reg_bits; pos += elem_bits) {
        out |= elem << pos;
    }

    return out & mask_for_bits(reg_bits);
}

static uint64_t decode_logical_imm(unsigned reg_bits, uint8_t n, uint8_t immr, uint8_t imms) {
    unsigned len;
    unsigned levels;
    unsigned s;
    unsigned r;
    unsigned esize;
    uint64_t base;

    (void)n;

    if (n) {
        len = 6;
    } else {
        uint8_t tmp = (uint8_t)(~imms & 0x3fu);
        len = 0;
        while (tmp > 1u) {
            tmp >>= 1;
            ++len;
        }
    }

    esize = 1u << len;
    levels = esize - 1u;
    s = imms & levels;
    r = immr & levels;

    base = (s == 63u) ? ~0ull : ((1ull << (s + 1u)) - 1ull);
    return replicate_elem(ror_elem(base, r, esize), esize, reg_bits);
}

int main(void) {
    as_a64_reg_t reg;
    as_a64_cond_t cond;
    uint8_t n;
    uint8_t immr;
    uint8_t imms;
    uint8_t hw;
    uint8_t immlo;
    uint32_t immhi;
    uint8_t b[4];

    if (as_a64_parse_reg("x0", &reg) != 0 || reg.regclass != AS_A64_REGCLASS_X || reg.index != 0) {
        fail("parse x0");
    }
    if (as_a64_parse_reg("W30", &reg) != 0 || reg.regclass != AS_A64_REGCLASS_W || reg.index != 30) {
        fail("parse w30");
    }
    if (as_a64_parse_reg("sp", &reg) != 0 || reg.regclass != AS_A64_REGCLASS_SP || reg.index != 31) {
        fail("parse sp");
    }
    if (as_a64_parse_reg("wsp", &reg) != 0 || reg.regclass != AS_A64_REGCLASS_WSP || reg.index != 31) {
        fail("parse wsp");
    }
    if (as_a64_parse_reg("xzr", &reg) != 0 || reg.regclass != AS_A64_REGCLASS_XZR || reg.index != 31) {
        fail("parse xzr");
    }
    if (as_a64_parse_reg("wzr", &reg) != 0 || reg.regclass != AS_A64_REGCLASS_WZR || reg.index != 31) {
        fail("parse wzr");
    }
    if (as_a64_parse_reg("x31", &reg) == 0) {
        fail("reject x31");
    }

    if (as_a64_cond_from_string("eq", &cond) != 0 || cond != AS_A64_COND_EQ) {
        fail("cond eq");
    }
    if (as_a64_cond_from_string("hs", &cond) != 0 || cond != AS_A64_COND_CS) {
        fail("cond hs alias");
    }
    if (as_a64_cond_from_string("lo", &cond) != 0 || cond != AS_A64_COND_CC) {
        fail("cond lo alias");
    }
    if (as_a64_cond_from_string("nv", &cond) != 0 || cond != AS_A64_COND_NV) {
        fail("cond nv");
    }

    if (as_a64_encode_logical_imm(0x00ff00ff00ff00ffull, 64, &n, &immr, &imms) != 0) {
        fail("logical imm encode 64");
    }
    if (decode_logical_imm(64, n, immr, imms) != 0x00ff00ff00ff00ffull) {
        fail("logical imm decode 64");
    }

    if (as_a64_encode_logical_imm(0x00ff00ffu, 32, &n, &immr, &imms) != 0) {
        fail("logical imm encode 32");
    }
    if (decode_logical_imm(32, n, immr, imms) != 0x00ff00ffu) {
        fail("logical imm decode 32");
    }

    if (as_a64_encode_logical_imm(0, 64, &n, &immr, &imms) == 0) {
        fail("logical imm reject zero");
    }
    if (as_a64_encode_logical_imm(~0ull, 64, &n, &immr, &imms) == 0) {
        fail("logical imm reject all ones");
    }

    if (as_a64_encode_movwide_shift(0, 0, &hw) != 0 || hw != 0) {
        fail("movwide shift 32 #0");
    }
    if (as_a64_encode_movwide_shift(16, 0, &hw) != 0 || hw != 1) {
        fail("movwide shift 32 #16");
    }
    if (as_a64_encode_movwide_shift(32, 0, &hw) == 0) {
        fail("movwide shift 32 reject #32");
    }
    if (as_a64_encode_movwide_shift(48, 1, &hw) != 0 || hw != 3) {
        fail("movwide shift 64 #48");
    }

    if (as_a64_encode_adr_imm(0x12345, &immlo, &immhi) != 0) {
        fail("adr imm encode");
    }
    if ((((uint32_t)immhi << 2) | immlo) != 0x12345u) {
        fail("adr imm split");
    }
    if (as_a64_encode_adr_imm(1ll << 20, &immlo, &immhi) == 0) {
        fail("adr imm out of range");
    }

    if (as_a64_encode_adrp_imm(0x12345000ll, &immlo, &immhi) != 0) {
        fail("adrp imm encode");
    }
    if ((((uint32_t)immhi << 2) | immlo) != 0x12345u) {
        fail("adrp imm split");
    }
    if (as_a64_encode_adrp_imm(0x12345ll, &immlo, &immhi) == 0) {
        fail("adrp alignment reject");
    }

    as_a64_put32le(b, 0x11223344u);
    if (b[0] != 0x44u || b[1] != 0x33u || b[2] != 0x22u || b[3] != 0x11u) {
        fail("put32le");
    }

    puts("ok");
    return 0;
}
