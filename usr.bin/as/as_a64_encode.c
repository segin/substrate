#include "as_a64_encode.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const char *name;
    as_a64_cond_t cond;
} cond_name_t;

static int streq_ci(const char *a, const char *b) {
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }

    for (i = 0; a[i] != '\0' && b[i] != '\0'; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int parse_u8(const char *s, unsigned *out) {
    unsigned v = 0;
    size_t i;

    if (s == NULL || s[0] == '\0' || out == NULL) {
        return -1;
    }

    for (i = 0; s[i] != '\0'; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        v = v * 10u + (unsigned)(s[i] - '0');
        if (v > 255u) {
            return -1;
        }
    }

    *out = v;
    return 0;
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

    if (reg_bits == 64u) {
        return out;
    }
    return out & ((1ull << reg_bits) - 1ull);
}

int as_a64_cond_from_string(const char *name, as_a64_cond_t *out_cond) {
    static const cond_name_t table[] = {
        {"eq", AS_A64_COND_EQ}, {"ne", AS_A64_COND_NE}, {"cs", AS_A64_COND_CS},
        {"hs", AS_A64_COND_CS}, {"cc", AS_A64_COND_CC}, {"lo", AS_A64_COND_CC},
        {"mi", AS_A64_COND_MI}, {"pl", AS_A64_COND_PL}, {"vs", AS_A64_COND_VS},
        {"vc", AS_A64_COND_VC}, {"hi", AS_A64_COND_HI}, {"ls", AS_A64_COND_LS},
        {"ge", AS_A64_COND_GE}, {"lt", AS_A64_COND_LT}, {"gt", AS_A64_COND_GT},
        {"le", AS_A64_COND_LE}, {"al", AS_A64_COND_AL}, {"nv", AS_A64_COND_NV},
    };
    size_t i;

    if (name == NULL || out_cond == NULL) {
        return -1;
    }

    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (streq_ci(name, table[i].name)) {
            *out_cond = table[i].cond;
            return 0;
        }
    }

    return -1;
}

int as_a64_parse_reg(const char *name, as_a64_reg_t *out_reg) {
    unsigned idx;

    if (name == NULL || out_reg == NULL) {
        return -1;
    }

    if (streq_ci(name, "sp")) {
        out_reg->regclass = AS_A64_REGCLASS_SP;
        out_reg->index = 31;
        return 0;
    }
    if (streq_ci(name, "wsp")) {
        out_reg->regclass = AS_A64_REGCLASS_WSP;
        out_reg->index = 31;
        return 0;
    }
    if (streq_ci(name, "xzr")) {
        out_reg->regclass = AS_A64_REGCLASS_XZR;
        out_reg->index = 31;
        return 0;
    }
    if (streq_ci(name, "wzr")) {
        out_reg->regclass = AS_A64_REGCLASS_WZR;
        out_reg->index = 31;
        return 0;
    }

    if ((name[0] == 'x' || name[0] == 'X') && parse_u8(name + 1, &idx) == 0 && idx <= 30u) {
        out_reg->regclass = AS_A64_REGCLASS_X;
        out_reg->index = (uint8_t)idx;
        return 0;
    }
    if ((name[0] == 'w' || name[0] == 'W') && parse_u8(name + 1, &idx) == 0 && idx <= 30u) {
        out_reg->regclass = AS_A64_REGCLASS_W;
        out_reg->index = (uint8_t)idx;
        return 0;
    }

    return -1;
}

int as_a64_encode_logical_imm(uint64_t imm, unsigned reg_bits, uint8_t *out_n,
                              uint8_t *out_immr, uint8_t *out_imms) {
    unsigned esize;

    if (out_n == NULL || out_immr == NULL || out_imms == NULL) {
        return -1;
    }
    if (reg_bits != 32u && reg_bits != 64u) {
        return -1;
    }

    if ((imm & mask_for_bits(reg_bits)) == 0u) {
        return -1;
    }
    if ((imm & mask_for_bits(reg_bits)) == mask_for_bits(reg_bits)) {
        return -1;
    }

    for (esize = 2; esize <= reg_bits; esize <<= 1) {
        unsigned ones;

        for (ones = 1; ones < esize; ++ones) {
            uint64_t base;
            unsigned r;

            if (ones == 64u) {
                base = ~0ull;
            } else {
                base = (1ull << ones) - 1ull;
            }

            for (r = 0; r < esize; ++r) {
                uint64_t elem = ror_elem(base, r, esize);
                uint64_t candidate = replicate_elem(elem, esize, reg_bits);
                uint8_t len = 0;
                uint8_t levels;
                uint8_t s;
                uint8_t immr;
                uint8_t imms;

                if ((candidate & mask_for_bits(reg_bits)) != (imm & mask_for_bits(reg_bits))) {
                    continue;
                }

                while ((1u << len) < esize) {
                    ++len;
                }
                levels = (uint8_t)((1u << len) - 1u);
                s = (uint8_t)(ones - 1u);
                immr = (uint8_t)(r & levels);
                imms = (uint8_t)((((~(esize - 1u)) << 1) & 0x3fu) | s);

                *out_n = (esize == 64u) ? 1u : 0u;
                *out_immr = immr;
                *out_imms = imms;
                return 0;
            }
        }
    }

    return -1;
}

int as_a64_encode_movwide_shift(unsigned shift, int is64, uint8_t *out_hw) {
    if (out_hw == NULL) {
        return -1;
    }
    if (shift % 16u != 0u) {
        return -1;
    }

    if (is64) {
        if (shift > 48u) {
            return -1;
        }
    } else {
        if (shift > 16u) {
            return -1;
        }
    }

    *out_hw = (uint8_t)(shift / 16u);
    return 0;
}

int as_a64_encode_adr_imm(int64_t byte_delta, uint8_t *out_immlo, uint32_t *out_immhi) {
    uint32_t imm21;

    if (out_immlo == NULL || out_immhi == NULL) {
        return -1;
    }
    if (byte_delta < -(1ll << 20) || byte_delta > ((1ll << 20) - 1ll)) {
        return -1;
    }

    imm21 = (uint32_t)((uint64_t)byte_delta & 0x1fffffull);
    *out_immlo = (uint8_t)(imm21 & 0x3u);
    *out_immhi = (imm21 >> 2) & 0x7ffffu;
    return 0;
}

int as_a64_encode_adrp_imm(int64_t byte_delta, uint8_t *out_immlo, uint32_t *out_immhi) {
    int64_t page_delta;

    if (out_immlo == NULL || out_immhi == NULL) {
        return -1;
    }
    if ((byte_delta & 0xfffll) != 0) {
        return -1;
    }

    page_delta = byte_delta >> 12;
    if (page_delta < -(1ll << 20) || page_delta > ((1ll << 20) - 1ll)) {
        return -1;
    }

    return as_a64_encode_adr_imm(page_delta, out_immlo, out_immhi);
}

void as_a64_put32le(uint8_t *out, uint32_t v) {
    if (out == NULL) {
        return;
    }
    out[0] = (uint8_t)(v & 0xffu);
    out[1] = (uint8_t)((v >> 8) & 0xffu);
    out[2] = (uint8_t)((v >> 16) & 0xffu);
    out[3] = (uint8_t)((v >> 24) & 0xffu);
}
