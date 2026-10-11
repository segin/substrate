#include "as_x86_vec.h"

/*
 * The table of the instructions with a VEX or an EVEX prefix.  What a
 * row is, and the characters of its operands, are in as_x86_vec.h.
 *
 * The rows of one name stand together, those with VEX before those
 * with EVEX; where an instruction is AVX-512 unless asked otherwise
 * (the VNNI and IFMA ones) its EVEX rows are first.
 */

#define P0 AS_VEC_PP_NONE
#define P66 AS_VEC_PP_66
#define PF3 AS_VEC_PP_F3
#define PF2 AS_VEC_PP_F2
#define M1 AS_VEC_MAP_0F
#define M2 AS_VEC_MAP_0F38
#define M3 AS_VEC_MAP_0F3A
#define W0 AS_VEC_W0
#define W1 AS_VEC_W1
#define WIG AS_VEC_WIG
#define L128 AS_VEC_L128
#define L256 AS_VEC_L256
#define L512 AS_VEC_L512
#define LIG AS_VEC_LIG

#define K AS_VEC_F_MASK
#define KZ (AS_VEC_F_MASK | AS_VEC_F_ZERO)
#define ER AS_VEC_F_ER
#define SAE AS_VEC_F_SAE
#define SWAP AS_VEC_F_SWAP
#define KREQ AS_VEC_F_MASKREQ
#define O64 AS_VEC_F_64
#define NO64 AS_VEC_F_NO64
#define DG(n) AS_VEC_DIGIT(n)

#define VX(n, pp, map, op, w, l, ops, roles, fl) {n, AS_VEC_VEX, pp, map, op, w, l, ops, roles, 0, 0, fl}
#define EX(n, pp, map, op, w, l, ops, roles, N, bc, fl) {n, AS_VEC_EVEX, pp, map, op, w, l, ops, roles, N, bc, fl}

/* VEX, %xmm and %ymm: three operands; two; and each with a byte before. */
#define V3(n, pp, map, op, w, fl) \
    VX(n, pp, map, op, w, L128, "Xxx", "mvr", fl), VX(n, pp, map, op, w, L256, "Yyy", "mvr", fl)
#define V2(n, pp, map, op, w, fl) \
    VX(n, pp, map, op, w, L128, "Xx", "mr", fl), VX(n, pp, map, op, w, L256, "Yy", "mr", fl)
#define V3I(n, pp, map, op, w, fl) \
    VX(n, pp, map, op, w, L128, "iXxx", "imvr", fl), VX(n, pp, map, op, w, L256, "iYyy", "imvr", fl)
#define V2I(n, pp, map, op, w, fl) \
    VX(n, pp, map, op, w, L128, "iXx", "imr", fl), VX(n, pp, map, op, w, L256, "iYy", "imr", fl)
/* VEX, %xmm alone. */
#define V3X(n, pp, map, op, w, fl) VX(n, pp, map, op, w, L128, "Xxx", "mvr", fl)
#define V3S(n, pp, map, op, w, fl) VX(n, pp, map, op, w, LIG, "Xxx", "mvr", fl)
#define V2S(n, pp, map, op, w, fl) VX(n, pp, map, op, w, LIG, "Xx", "mr", fl)

/* EVEX, the three lengths; `bc` an element's bytes where memory may be
 * broadcast; `fz` what the 512-bit row allows besides (rounding). */
#define E3(n, pp, map, op, w, bc, fz) \
    EX(n, pp, map, op, w, L128, "Xxx", "mvr", 16, bc, KZ), EX(n, pp, map, op, w, L256, "Yyy", "mvr", 32, bc, KZ), \
        EX(n, pp, map, op, w, L512, "Zzz", "mvr", 64, bc, KZ | (fz))
#define E2(n, pp, map, op, w, bc, fz) \
    EX(n, pp, map, op, w, L128, "Xx", "mr", 16, bc, KZ), EX(n, pp, map, op, w, L256, "Yy", "mr", 32, bc, KZ), \
        EX(n, pp, map, op, w, L512, "Zz", "mr", 64, bc, KZ | (fz))
#define E3I(n, pp, map, op, w, bc, fz) \
    EX(n, pp, map, op, w, L128, "iXxx", "imvr", 16, bc, KZ), EX(n, pp, map, op, w, L256, "iYyy", "imvr", 32, bc, KZ), \
        EX(n, pp, map, op, w, L512, "iZzz", "imvr", 64, bc, KZ | (fz))
#define E2I(n, pp, map, op, w, bc, fz) \
    EX(n, pp, map, op, w, L128, "iXx", "imr", 16, bc, KZ), EX(n, pp, map, op, w, L256, "iYy", "imr", 32, bc, KZ), \
        EX(n, pp, map, op, w, L512, "iZz", "imr", 64, bc, KZ | (fz))
/* EVEX, a scalar: `N` its bytes. */
#define E3S(n, pp, map, op, w, N, fl) EX(n, pp, map, op, w, LIG, "Xxx", "mvr", N, 0, KZ | (fl))
#define E2S(n, pp, map, op, w, N, fl) EX(n, pp, map, op, w, LIG, "Xx", "mr", N, 0, fl)
/* EVEX, the 256- and 512-bit lengths only. */
#define E3YZ(n, pp, map, op, w, bc) \
    EX(n, pp, map, op, w, L256, "Yyy", "mvr", 32, bc, KZ), EX(n, pp, map, op, w, L512, "Zzz", "mvr", 64, bc, KZ)

/* The packed and the scalar arithmetic of AVX, and the same in AVX-512:
 * ps, pd, ss, sd of one opcode. */
#define ARITH(base, op, vfl, pfl, sfl) \
    V3(base "ps", P0, M1, op, WIG, vfl), E3(base "ps", P0, M1, op, W0, 4, pfl), \
        V3(base "pd", P66, M1, op, WIG, vfl), E3(base "pd", P66, M1, op, W1, 8, pfl), \
        V3S(base "ss", PF3, M1, op, WIG, 0), E3S(base "ss", PF3, M1, op, W0, 4, sfl), \
        V3S(base "sd", PF2, M1, op, WIG, 0), E3S(base "sd", PF2, M1, op, W1, 8, sfl)

/* The logic of AVX, whose AVX-512 forms are of AVX-512DQ. */
#define LOGIC(base, op, vfl) \
    V3(base "ps", P0, M1, op, WIG, vfl), E3(base "ps", P0, M1, op, W0, 4, 0), \
        V3(base "pd", P66, M1, op, WIG, vfl), E3(base "pd", P66, M1, op, W1, 8, 0)

/* An integer operation of AVX and AVX2 on %xmm and %ymm, and in
 * AVX-512: of bytes or words (no broadcast), of doublewords, of
 * quadwords. */
#define INT_BW(n, pp, map, op, vfl) V3(n, pp, map, op, WIG, vfl), E3(n, pp, map, op, WIG, 0, 0)
#define INT_D(n, pp, map, op, vfl) V3(n, pp, map, op, WIG, vfl), E3(n, pp, map, op, W0, 4, 0)
#define INT_Q(n, pp, map, op, vfl) V3(n, pp, map, op, WIG, vfl), E3(n, pp, map, op, W1, 8, 0)
/* And one that AVX-512 has under other names, or not at all. */
#define INT_V(n, pp, map, op, vfl) V3(n, pp, map, op, WIG, vfl)

/* A shift by a count in an %xmm register or in memory, and by a
 * number: `op` and `iop` /`dg`. */
#define SHIFT(n, op, iop, dg, ew, bc) \
    VX(n, P66, M1, op, WIG, L128, "Xxx", "mvr", 0), VX(n, P66, M1, op, WIG, L256, "Xyy", "mvr", 0), \
        VX(n, P66, M1, iop, WIG, L128, "ixx", "imv", DG(dg)), VX(n, P66, M1, iop, WIG, L256, "iyy", "imv", DG(dg)), \
        EX(n, P66, M1, op, ew, L128, "Xxx", "mvr", 16, 0, KZ), EX(n, P66, M1, op, ew, L256, "Xyy", "mvr", 16, 0, KZ), \
        EX(n, P66, M1, op, ew, L512, "Xzz", "mvr", 16, 0, KZ), \
        EX(n, P66, M1, iop, ew, L128, "iXx", "imv", 16, bc, KZ | DG(dg)), \
        EX(n, P66, M1, iop, ew, L256, "iYy", "imv", 32, bc, KZ | DG(dg)), \
        EX(n, P66, M1, iop, ew, L512, "iZz", "imv", 64, bc, KZ | DG(dg))

/* Sign and zero extension: the source is `sx`, `sy`, `sz` bytes of
 * memory, or an %xmm register (a %ymm one for the 512-bit quarter...). */
#define PMOVX(n, op, sx, sy, sz, zsrc) \
    VX(n, P66, M2, op, WIG, L128, "Xx", "mr", 0), VX(n, P66, M2, op, WIG, L256, "Xy", "mr", 0), \
        EX(n, P66, M2, op, WIG, L128, "Xx", "mr", sx, 0, KZ), EX(n, P66, M2, op, WIG, L256, "Xy", "mr", sy, 0, KZ), \
        EX(n, P66, M2, op, WIG, L512, zsrc "z", "mr", sz, 0, KZ)

/* The fused multiply-adds: `op` the packed opcode, the scalar the one
 * after. */
#define FMA_P(base, op) \
    V3(base "ps", P66, M2, op, W0, 0), E3(base "ps", P66, M2, op, W0, 4, ER), \
        V3(base "pd", P66, M2, op, W1, 0), E3(base "pd", P66, M2, op, W1, 8, ER)
#define FMA_S(base, op) \
    V3S(base "ss", P66, M2, op, W0, 0), E3S(base "ss", P66, M2, op, W0, 4, ER), \
        V3S(base "sd", P66, M2, op, W1, 0), E3S(base "sd", P66, M2, op, W1, 8, ER)
#define FMA(base, o132, o213, o231) \
    FMA_P(base "132", o132), FMA_P(base "213", o213), FMA_P(base "231", o231), FMA_S(base "132", (o132) + 1), \
        FMA_S(base "213", (o213) + 1), FMA_S(base "231", (o231) + 1)
#define FMA_PONLY(base, o132, o213, o231) FMA_P(base "132", o132), FMA_P(base "213", o213), FMA_P(base "231", o231)

/* FMA4: the third source in the last byte.  With registers alone, and
 * with memory first, W is 1; with memory second it is 0. */
#define FMA4_P(n, op) \
    VX(n, P66, M3, op, W1, L128, "Xxxx", "m4vr", 0), VX(n, P66, M3, op, W0, L128, "xmxx", "4mvr", 0), \
        VX(n, P66, M3, op, W1, L256, "Yyyy", "m4vr", 0), VX(n, P66, M3, op, W0, L256, "ymyy", "4mvr", 0)
#define FMA4_S(n, op) \
    VX(n, P66, M3, op, W1, LIG, "Xxxx", "m4vr", 0), VX(n, P66, M3, op, W0, LIG, "xmxx", "4mvr", 0)
#define FMA4(base, op) \
    FMA4_P(base "ps", op), FMA4_P(base "pd", (op) + 1), FMA4_S(base "ss", (op) + 2), FMA4_S(base "sd", (op) + 3)

/* A move of a whole vector: to a register (`ld`) and from one (`st`).
 * Registers alone are the first, or the second where that is shorter. */
#define VMOVE(n, pp, ld, st) \
    VX(n, pp, M1, ld, WIG, L128, "Xx", "mr", SWAP), VX(n, pp, M1, st, WIG, L128, "xX", "rm", 0), \
        VX(n, pp, M1, ld, WIG, L256, "Yy", "mr", SWAP), VX(n, pp, M1, st, WIG, L256, "yY", "rm", 0)
#define EMOVE(n, pp, w, ld, st) \
    EX(n, pp, M1, ld, w, L128, "Xx", "mr", 16, 0, KZ), EX(n, pp, M1, st, w, L128, "xx", "rm", 16, 0, KZ), \
        EX(n, pp, M1, st, w, L128, "xm", "rm", 16, 0, K), EX(n, pp, M1, ld, w, L256, "Yy", "mr", 32, 0, KZ), \
        EX(n, pp, M1, st, w, L256, "yy", "rm", 32, 0, KZ), EX(n, pp, M1, st, w, L256, "ym", "rm", 32, 0, K), \
        EX(n, pp, M1, ld, w, L512, "Zz", "mr", 64, 0, KZ), EX(n, pp, M1, st, w, L512, "zz", "rm", 64, 0, KZ), \
        EX(n, pp, M1, st, w, L512, "zm", "rm", 64, 0, K)

/* A gather of AVX2: the mask is a vector register, in vvvv. */
#define GATHER2(n, op, w, ix, xr, iy, yr) \
    VX(n, P66, M2, op, w, L128, "x" ix "x", "vmr", 0), VX(n, P66, M2, op, w, L256, yr iy yr, "vmr", 0)

const as_x86_vec_tmpl_t as_x86_vec_table[] = {
    /* AVX: arithmetic. */
    ARITH("vadd", 0x58, 0, ER, ER),
    ARITH("vmul", 0x59, 0, ER, ER),
    ARITH("vsub", 0x5c, 0, ER, ER),
    ARITH("vdiv", 0x5e, 0, ER, ER),
    ARITH("vmax", 0x5f, 0, SAE, SAE),
    ARITH("vmin", 0x5d, 0, SAE, SAE),
    V2("vsqrtps", P0, M1, 0x51, WIG, 0),
    E2("vsqrtps", P0, M1, 0x51, W0, 4, ER),
    V2("vsqrtpd", P66, M1, 0x51, WIG, 0),
    E2("vsqrtpd", P66, M1, 0x51, W1, 8, ER),
    V3S("vsqrtss", PF3, M1, 0x51, WIG, 0),
    E3S("vsqrtss", PF3, M1, 0x51, W0, 4, ER),
    V3S("vsqrtsd", PF2, M1, 0x51, WIG, 0),
    E3S("vsqrtsd", PF2, M1, 0x51, W1, 8, ER),
    LOGIC("vand", 0x54, 0),
    LOGIC("vandn", 0x55, 0),
    LOGIC("vor", 0x56, 0),
    LOGIC("vxor", 0x57, 0),
    V3("vaddsubpd", P66, M1, 0xd0, WIG, 0),
    V3("vaddsubps", PF2, M1, 0xd0, WIG, 0),
    V3("vhaddpd", P66, M1, 0x7c, WIG, 0),
    V3("vhaddps", PF2, M1, 0x7c, WIG, 0),
    V3("vhsubpd", P66, M1, 0x7d, WIG, 0),
    V3("vhsubps", PF2, M1, 0x7d, WIG, 0),
    V2("vrcpps", P0, M1, 0x53, WIG, 0),
    V3S("vrcpss", PF3, M1, 0x53, WIG, 0),
    V2("vrsqrtps", P0, M1, 0x52, WIG, 0),
    V3S("vrsqrtss", PF3, M1, 0x52, WIG, 0),
    V2I("vroundps", P66, M3, 0x08, WIG, 0),
    V2I("vroundpd", P66, M3, 0x09, WIG, 0),
    VX("vroundss", P66, M3, 0x0a, WIG, LIG, "iXxx", "imvr", 0),
    VX("vroundsd", P66, M3, 0x0b, WIG, LIG, "iXxx", "imvr", 0),
    VX("vdppd", P66, M3, 0x41, WIG, L128, "iXxx", "imvr", 0),
    V3I("vdpps", P66, M3, 0x40, WIG, 0),

    /* Blends, shuffles, unpacks. */
    V3I("vblendps", P66, M3, 0x0c, WIG, 0),
    V3I("vblendpd", P66, M3, 0x0d, WIG, 0),
    VX("vblendvps", P66, M3, 0x4a, W0, L128, "xXxx", "4mvr", 0),
    VX("vblendvps", P66, M3, 0x4a, W0, L256, "yYyy", "4mvr", 0),
    VX("vblendvpd", P66, M3, 0x4b, W0, L128, "xXxx", "4mvr", 0),
    VX("vblendvpd", P66, M3, 0x4b, W0, L256, "yYyy", "4mvr", 0),
    VX("vpblendvb", P66, M3, 0x4c, W0, L128, "xXxx", "4mvr", 0),
    VX("vpblendvb", P66, M3, 0x4c, W0, L256, "yYyy", "4mvr", 0),
    V3I("vpblendw", P66, M3, 0x0e, WIG, 0),
    V3I("vpblendd", P66, M3, 0x02, W0, 0),
    V3I("vshufps", P0, M1, 0xc6, WIG, 0),
    E3I("vshufps", P0, M1, 0xc6, W0, 4, 0),
    V3I("vshufpd", P66, M1, 0xc6, WIG, 0),
    E3I("vshufpd", P66, M1, 0xc6, W1, 8, 0),
    V3("vunpcklps", P0, M1, 0x14, WIG, 0),
    E3("vunpcklps", P0, M1, 0x14, W0, 4, 0),
    V3("vunpckhps", P0, M1, 0x15, WIG, 0),
    E3("vunpckhps", P0, M1, 0x15, W0, 4, 0),
    V3("vunpcklpd", P66, M1, 0x14, WIG, 0),
    E3("vunpcklpd", P66, M1, 0x14, W1, 8, 0),
    V3("vunpckhpd", P66, M1, 0x15, WIG, 0),
    E3("vunpckhpd", P66, M1, 0x15, W1, 8, 0),
    V3("vpermilps", P66, M2, 0x0c, W0, 0),
    V2I("vpermilps", P66, M3, 0x04, W0, 0),
    E3("vpermilps", P66, M2, 0x0c, W0, 4, 0),
    E2I("vpermilps", P66, M3, 0x04, W0, 4, 0),
    V3("vpermilpd", P66, M2, 0x0d, W0, 0),
    V2I("vpermilpd", P66, M3, 0x05, W0, 0),
    E3("vpermilpd", P66, M2, 0x0d, W1, 8, 0),
    E2I("vpermilpd", P66, M3, 0x05, W1, 8, 0),
    VX("vperm2f128", P66, M3, 0x06, W0, L256, "iYyy", "imvr", 0),
    VX("vperm2i128", P66, M3, 0x46, W0, L256, "iYyy", "imvr", 0),
    VX("vpermd", P66, M2, 0x36, W0, L256, "Yyy", "mvr", 0),
    E3YZ("vpermd", P66, M2, 0x36, W0, 4),
    VX("vpermps", P66, M2, 0x16, W0, L256, "Yyy", "mvr", 0),
    E3YZ("vpermps", P66, M2, 0x16, W0, 4),
    VX("vpermq", P66, M3, 0x00, W1, L256, "iYy", "imr", 0),
    EX("vpermq", P66, M3, 0x00, W1, L256, "iYy", "imr", 32, 8, KZ),
    EX("vpermq", P66, M3, 0x00, W1, L512, "iZz", "imr", 64, 8, KZ),
    E3YZ("vpermq", P66, M2, 0x36, W1, 8),
    VX("vpermpd", P66, M3, 0x01, W1, L256, "iYy", "imr", 0),
    EX("vpermpd", P66, M3, 0x01, W1, L256, "iYy", "imr", 32, 8, KZ),
    EX("vpermpd", P66, M3, 0x01, W1, L512, "iZz", "imr", 64, 8, KZ),
    E3YZ("vpermpd", P66, M2, 0x16, W1, 8),

    /* Compares. */
    V3I("vcmpps", P0, M1, 0xc2, WIG, 0),
    EX("vcmpps", P0, M1, 0xc2, W0, L128, "iXxk", "imvr", 16, 4, K),
    EX("vcmpps", P0, M1, 0xc2, W0, L256, "iYyk", "imvr", 32, 4, K),
    EX("vcmpps", P0, M1, 0xc2, W0, L512, "iZzk", "imvr", 64, 4, K | SAE),
    V3I("vcmppd", P66, M1, 0xc2, WIG, 0),
    EX("vcmppd", P66, M1, 0xc2, W1, L128, "iXxk", "imvr", 16, 8, K),
    EX("vcmppd", P66, M1, 0xc2, W1, L256, "iYyk", "imvr", 32, 8, K),
    EX("vcmppd", P66, M1, 0xc2, W1, L512, "iZzk", "imvr", 64, 8, K | SAE),
    VX("vcmpss", PF3, M1, 0xc2, WIG, LIG, "iXxx", "imvr", 0),
    EX("vcmpss", PF3, M1, 0xc2, W0, LIG, "iXxk", "imvr", 4, 0, K | SAE),
    VX("vcmpsd", PF2, M1, 0xc2, WIG, LIG, "iXxx", "imvr", 0),
    EX("vcmpsd", PF2, M1, 0xc2, W1, LIG, "iXxk", "imvr", 8, 0, K | SAE),
    V2S("vcomiss", P0, M1, 0x2f, WIG, 0),
    E2S("vcomiss", P0, M1, 0x2f, W0, 4, SAE),
    V2S("vcomisd", P66, M1, 0x2f, WIG, 0),
    E2S("vcomisd", P66, M1, 0x2f, W1, 8, SAE),
    V2S("vucomiss", P0, M1, 0x2e, WIG, 0),
    E2S("vucomiss", P0, M1, 0x2e, W0, 4, SAE),
    V2S("vucomisd", P66, M1, 0x2e, WIG, 0),
    E2S("vucomisd", P66, M1, 0x2e, W1, 8, SAE),
    V2("vptest", P66, M2, 0x17, WIG, 0),
    V2("vtestps", P66, M2, 0x0e, W0, 0),
    V2("vtestpd", P66, M2, 0x0f, W0, 0),

    /* Conversions. */
    VX("vcvtdq2pd", PF3, M1, 0xe6, WIG, L128, "Xx", "mr", 0),
    VX("vcvtdq2pd", PF3, M1, 0xe6, WIG, L256, "Xy", "mr", 0),
    EX("vcvtdq2pd", PF3, M1, 0xe6, W0, L128, "Xx", "mr", 8, 4, KZ),
    EX("vcvtdq2pd", PF3, M1, 0xe6, W0, L256, "Xy", "mr", 16, 4, KZ),
    EX("vcvtdq2pd", PF3, M1, 0xe6, W0, L512, "Yz", "mr", 32, 4, KZ),
    V2("vcvtdq2ps", P0, M1, 0x5b, WIG, 0),
    E2("vcvtdq2ps", P0, M1, 0x5b, W0, 4, ER),
    V2("vcvtps2dq", P66, M1, 0x5b, WIG, 0),
    E2("vcvtps2dq", P66, M1, 0x5b, W0, 4, ER),
    V2("vcvttps2dq", PF3, M1, 0x5b, WIG, 0),
    E2("vcvttps2dq", PF3, M1, 0x5b, W0, 4, SAE),
    VX("vcvtps2pd", P0, M1, 0x5a, WIG, L128, "Xx", "mr", 0),
    VX("vcvtps2pd", P0, M1, 0x5a, WIG, L256, "Xy", "mr", 0),
    EX("vcvtps2pd", P0, M1, 0x5a, W0, L128, "Xx", "mr", 8, 4, KZ),
    EX("vcvtps2pd", P0, M1, 0x5a, W0, L256, "Xy", "mr", 16, 4, KZ),
    EX("vcvtps2pd", P0, M1, 0x5a, W0, L512, "Yz", "mr", 32, 4, KZ | SAE),
#define NARROW(n, pp, op, efl) \
    VX(n, pp, M1, op, WIG, L128, "xx", "mr", 0), VX(n, pp, M1, op, WIG, L256, "yx", "mr", 0), \
        EX(n, pp, M1, op, W1, L128, "xx", "mr", 16, 8, KZ), EX(n, pp, M1, op, W1, L256, "yx", "mr", 32, 8, KZ), \
        EX(n, pp, M1, op, W1, L128, "mx", "mr", 16, 8, KZ | AS_VEC_F_BCST), \
        EX(n, pp, M1, op, W1, L256, "mx", "mr", 32, 8, KZ | AS_VEC_F_BCST), \
        EX(n, pp, M1, op, W1, L512, "Zy", "mr", 64, 8, KZ | (efl)), \
        VX(n "x", pp, M1, op, WIG, L128, "Xx", "mr", 0), EX(n "x", pp, M1, op, W1, L128, "Xx", "mr", 16, 8, KZ), \
        VX(n "y", pp, M1, op, WIG, L256, "Yx", "mr", 0), EX(n "y", pp, M1, op, W1, L256, "Yx", "mr", 32, 8, KZ)
    NARROW("vcvtpd2dq", PF2, 0xe6, ER),
    NARROW("vcvttpd2dq", P66, 0xe6, SAE),
    NARROW("vcvtpd2ps", P66, 0x5a, ER),
#define CVT2SI(n, pp, op, N, efl) \
    VX(n, pp, M1, op, W0, LIG, "Xd", "mr", NO64), VX(n, pp, M1, op, W1, LIG, "Xq", "mr", O64), \
        EX(n, pp, M1, op, W0, LIG, "Xd", "mr", N, 0, efl), EX(n, pp, M1, op, W1, LIG, "Xq", "mr", N, 0, O64 | (efl)), \
        VX(n "l", pp, M1, op, W0, LIG, "Xd", "mr", 0), EX(n "l", pp, M1, op, W0, LIG, "Xd", "mr", N, 0, efl), \
        VX(n "q", pp, M1, op, W1, LIG, "Xq", "mr", O64), EX(n "q", pp, M1, op, W1, LIG, "Xq", "mr", N, 0, O64 | (efl))
    CVT2SI("vcvtsd2si", PF2, 0x2d, 8, ER),
    CVT2SI("vcvttsd2si", PF2, 0x2c, 8, SAE),
    CVT2SI("vcvtss2si", PF3, 0x2d, 4, ER),
    CVT2SI("vcvttss2si", PF3, 0x2c, 4, SAE),
    V3S("vcvtsd2ss", PF2, M1, 0x5a, WIG, 0),
    E3S("vcvtsd2ss", PF2, M1, 0x5a, W1, 8, ER),
    V3S("vcvtss2sd", PF3, M1, 0x5a, WIG, 0),
    E3S("vcvtss2sd", PF3, M1, 0x5a, W0, 4, SAE),
#define SI2F(n, pp, efl32, efl64) \
    VX(n, pp, M1, 0x2a, W0, LIG, "Dxx", "mvr", 0), VX(n, pp, M1, 0x2a, W1, LIG, "qxx", "mvr", O64), \
        EX(n, pp, M1, 0x2a, W0, LIG, "Dxx", "mvr", 4, 0, efl32), EX(n, pp, M1, 0x2a, W1, LIG, "qxx", "mvr", 8, 0, O64 | (efl64)), \
        VX(n "l", pp, M1, 0x2a, W0, LIG, "Dxx", "mvr", 0), EX(n "l", pp, M1, 0x2a, W0, LIG, "Dxx", "mvr", 4, 0, efl32), \
        VX(n "q", pp, M1, 0x2a, W1, LIG, "Qxx", "mvr", O64), \
        EX(n "q", pp, M1, 0x2a, W1, LIG, "Qxx", "mvr", 8, 0, O64 | (efl64))
    SI2F("vcvtsi2sd", PF2, 0, ER),
    SI2F("vcvtsi2ss", PF3, ER, ER),
    VX("vcvtph2ps", P66, M2, 0x13, W0, L128, "Xx", "mr", 0),
    VX("vcvtph2ps", P66, M2, 0x13, W0, L256, "Xy", "mr", 0),
    EX("vcvtph2ps", P66, M2, 0x13, W0, L128, "Xx", "mr", 8, 0, KZ),
    EX("vcvtph2ps", P66, M2, 0x13, W0, L256, "Xy", "mr", 16, 0, KZ),
    EX("vcvtph2ps", P66, M2, 0x13, W0, L512, "Yz", "mr", 32, 0, KZ | SAE),
    VX("vcvtps2ph", P66, M3, 0x1d, W0, L128, "ixX", "irm", 0),
    VX("vcvtps2ph", P66, M3, 0x1d, W0, L256, "iyX", "irm", 0),
    EX("vcvtps2ph", P66, M3, 0x1d, W0, L128, "ixX", "irm", 8, 0, KZ),
    EX("vcvtps2ph", P66, M3, 0x1d, W0, L256, "iyX", "irm", 16, 0, KZ),
    EX("vcvtps2ph", P66, M3, 0x1d, W0, L512, "izY", "irm", 32, 0, KZ | SAE),

    /* Moves. */
    VMOVE("vmovaps", P0, 0x28, 0x29),
    EMOVE("vmovaps", P0, W0, 0x28, 0x29),
    VMOVE("vmovapd", P66, 0x28, 0x29),
    EMOVE("vmovapd", P66, W1, 0x28, 0x29),
    VMOVE("vmovups", P0, 0x10, 0x11),
    EMOVE("vmovups", P0, W0, 0x10, 0x11),
    VMOVE("vmovupd", P66, 0x10, 0x11),
    EMOVE("vmovupd", P66, W1, 0x10, 0x11),
    VMOVE("vmovdqa", P66, 0x6f, 0x7f),
    VMOVE("vmovdqu", PF3, 0x6f, 0x7f),
    EMOVE("vmovdqa32", P66, W0, 0x6f, 0x7f),
    EMOVE("vmovdqa64", P66, W1, 0x6f, 0x7f),
    EMOVE("vmovdqu8", PF2, W0, 0x6f, 0x7f),
    EMOVE("vmovdqu16", PF2, W1, 0x6f, 0x7f),
    EMOVE("vmovdqu32", PF3, W0, 0x6f, 0x7f),
    EMOVE("vmovdqu64", PF3, W1, 0x6f, 0x7f),
    VX("vmovd", P66, M1, 0x6e, W0, L128, "Dx", "mr", 0),
    VX("vmovd", P66, M1, 0x7e, W0, L128, "xD", "rm", 0),
    VX("vmovd", P66, M1, 0x6e, W1, L128, "qx", "mr", O64),
    VX("vmovd", P66, M1, 0x7e, W1, L128, "xq", "rm", O64),
    EX("vmovd", P66, M1, 0x6e, W0, L128, "Dx", "mr", 4, 0, 0),
    EX("vmovd", P66, M1, 0x7e, W0, L128, "xD", "rm", 4, 0, 0),
    EX("vmovd", P66, M1, 0x6e, W1, L128, "qx", "mr", 8, 0, O64),
    EX("vmovd", P66, M1, 0x7e, W1, L128, "xq", "rm", 8, 0, O64),
    EX("vmovd", PF3, M1, 0x7e, W0, L128, "xx", "mr", 4, 0, 0),
    VX("vmovq", P66, M1, 0x6e, W1, L128, "qx", "mr", O64),
    VX("vmovq", P66, M1, 0x7e, W1, L128, "xq", "rm", O64),
    VX("vmovq", PF3, M1, 0x7e, WIG, L128, "Xx", "mr", SWAP),
    VX("vmovq", P66, M1, 0xd6, WIG, L128, "xX", "rm", 0),
    /* (With EVEX, memory is moved by the opcodes a general register
     * is, and not by those two.) */
    EX("vmovq", P66, M1, 0x6e, W1, L128, "qx", "mr", 8, 0, O64),
    EX("vmovq", P66, M1, 0x6e, W1, L128, "mx", "mr", 8, 0, 0),
    EX("vmovq", P66, M1, 0x7e, W1, L128, "xq", "rm", 8, 0, O64),
    EX("vmovq", P66, M1, 0x7e, W1, L128, "xm", "rm", 8, 0, 0),
    EX("vmovq", PF3, M1, 0x7e, W1, L128, "xx", "mr", 8, 0, 0),
    VX("vmovss", PF3, M1, 0x10, WIG, LIG, "mx", "mr", 0),
    VX("vmovss", PF3, M1, 0x11, WIG, LIG, "xm", "rm", 0),
    VX("vmovss", PF3, M1, 0x10, WIG, LIG, "xxx", "mvr", SWAP),
    VX("vmovss", PF3, M1, 0x11, WIG, LIG, "xxx", "rvm", 0),
    EX("vmovss", PF3, M1, 0x10, W0, LIG, "mx", "mr", 4, 0, KZ),
    EX("vmovss", PF3, M1, 0x11, W0, LIG, "xm", "rm", 4, 0, K),
    EX("vmovss", PF3, M1, 0x10, W0, LIG, "xxx", "mvr", 4, 0, KZ),
    VX("vmovsd", PF2, M1, 0x10, WIG, LIG, "mx", "mr", 0),
    VX("vmovsd", PF2, M1, 0x11, WIG, LIG, "xm", "rm", 0),
    VX("vmovsd", PF2, M1, 0x10, WIG, LIG, "xxx", "mvr", SWAP),
    VX("vmovsd", PF2, M1, 0x11, WIG, LIG, "xxx", "rvm", 0),
    EX("vmovsd", PF2, M1, 0x10, W1, LIG, "mx", "mr", 8, 0, KZ),
    EX("vmovsd", PF2, M1, 0x11, W1, LIG, "xm", "rm", 8, 0, K),
    EX("vmovsd", PF2, M1, 0x10, W1, LIG, "xxx", "mvr", 8, 0, KZ),
    VX("vmovddup", PF2, M1, 0x12, WIG, L128, "Xx", "mr", 0),
    VX("vmovddup", PF2, M1, 0x12, WIG, L256, "Yy", "mr", 0),
    EX("vmovddup", PF2, M1, 0x12, W1, L128, "Xx", "mr", 8, 0, KZ),
    EX("vmovddup", PF2, M1, 0x12, W1, L256, "Yy", "mr", 32, 0, KZ),
    EX("vmovddup", PF2, M1, 0x12, W1, L512, "Zz", "mr", 64, 0, KZ),
    V2("vmovshdup", PF3, M1, 0x16, WIG, 0),
    E2("vmovshdup", PF3, M1, 0x16, W0, 0, 0),
    V2("vmovsldup", PF3, M1, 0x12, WIG, 0),
    E2("vmovsldup", PF3, M1, 0x12, W0, 0, 0),
    VX("vmovhlps", P0, M1, 0x12, WIG, L128, "xxx", "mvr", 0),
    EX("vmovhlps", P0, M1, 0x12, W0, L128, "xxx", "mvr", 0, 0, 0),
    VX("vmovlhps", P0, M1, 0x16, WIG, L128, "xxx", "mvr", 0),
    EX("vmovlhps", P0, M1, 0x16, W0, L128, "xxx", "mvr", 0, 0, 0),
#define HALFMOVE(n, pp, ew, ld, st) \
    VX(n, pp, M1, ld, WIG, L128, "mxx", "mvr", 0), VX(n, pp, M1, st, WIG, L128, "xm", "rm", 0), \
        EX(n, pp, M1, ld, ew, L128, "mxx", "mvr", 8, 0, 0), EX(n, pp, M1, st, ew, L128, "xm", "rm", 8, 0, 0)
    HALFMOVE("vmovhps", P0, W0, 0x16, 0x17),
    HALFMOVE("vmovhpd", P66, W1, 0x16, 0x17),
    HALFMOVE("vmovlps", P0, W0, 0x12, 0x13),
    HALFMOVE("vmovlpd", P66, W1, 0x12, 0x13),
    VX("vmovmskps", P0, M1, 0x50, WIG, L128, "xR", "mr", 0),
    VX("vmovmskps", P0, M1, 0x50, WIG, L256, "yR", "mr", 0),
    VX("vmovmskpd", P66, M1, 0x50, WIG, L128, "xR", "mr", 0),
    VX("vmovmskpd", P66, M1, 0x50, WIG, L256, "yR", "mr", 0),
    VX("vpmovmskb", P66, M1, 0xd7, WIG, L128, "xR", "mr", 0),
    VX("vpmovmskb", P66, M1, 0xd7, WIG, L256, "yR", "mr", 0),
#define NTSTORE(n, pp, ew, op) \
    VX(n, pp, M1, op, WIG, L128, "xm", "rm", 0), VX(n, pp, M1, op, WIG, L256, "ym", "rm", 0), \
        EX(n, pp, M1, op, ew, L128, "xm", "rm", 16, 0, 0), EX(n, pp, M1, op, ew, L256, "ym", "rm", 32, 0, 0), \
        EX(n, pp, M1, op, ew, L512, "zm", "rm", 64, 0, 0)
    NTSTORE("vmovntps", P0, W0, 0x2b),
    NTSTORE("vmovntpd", P66, W1, 0x2b),
    NTSTORE("vmovntdq", P66, W0, 0xe7),
    VX("vmovntdqa", P66, M2, 0x2a, WIG, L128, "mx", "mr", 0),
    VX("vmovntdqa", P66, M2, 0x2a, WIG, L256, "my", "mr", 0),
    EX("vmovntdqa", P66, M2, 0x2a, W0, L128, "mx", "mr", 16, 0, 0),
    EX("vmovntdqa", P66, M2, 0x2a, W0, L256, "my", "mr", 32, 0, 0),
    EX("vmovntdqa", P66, M2, 0x2a, W0, L512, "mz", "mr", 64, 0, 0),
    VX("vlddqu", PF2, M1, 0xf0, WIG, L128, "mx", "mr", 0),
    VX("vlddqu", PF2, M1, 0xf0, WIG, L256, "my", "mr", 0),
    VX("vmaskmovdqu", P66, M1, 0xf7, WIG, L128, "xx", "mr", 0),
#define MASKMOV(n, w, ld, st) \
    VX(n, P66, M2, ld, w, L128, "mxx", "mvr", 0), VX(n, P66, M2, ld, w, L256, "myy", "mvr", 0), \
        VX(n, P66, M2, st, w, L128, "xxm", "rvm", 0), VX(n, P66, M2, st, w, L256, "yym", "rvm", 0)
    MASKMOV("vmaskmovps", W0, 0x2c, 0x2e),
    MASKMOV("vmaskmovpd", W0, 0x2d, 0x2f),
    MASKMOV("vpmaskmovd", W0, 0x8c, 0x8e),
    MASKMOV("vpmaskmovq", W1, 0x8c, 0x8e),
    VX("vldmxcsr", P0, M1, 0xae, WIG, L128, "m", "m", DG(2)),
    VX("vstmxcsr", P0, M1, 0xae, WIG, L128, "m", "m", DG(3)),
    VX("vzeroupper", P0, M1, 0x77, WIG, L128, "", "", 0),
    VX("vzeroall", P0, M1, 0x77, WIG, L256, "", "", 0),

    /* Broadcasts, inserts, extracts. */
    VX("vbroadcastss", P66, M2, 0x18, W0, L128, "Xx", "mr", 0),
    VX("vbroadcastss", P66, M2, 0x18, W0, L256, "Xy", "mr", 0),
    EX("vbroadcastss", P66, M2, 0x18, W0, L128, "Xx", "mr", 4, 0, KZ),
    EX("vbroadcastss", P66, M2, 0x18, W0, L256, "Xy", "mr", 4, 0, KZ),
    EX("vbroadcastss", P66, M2, 0x18, W0, L512, "Xz", "mr", 4, 0, KZ),
    VX("vbroadcastsd", P66, M2, 0x19, W0, L256, "Xy", "mr", 0),
    EX("vbroadcastsd", P66, M2, 0x19, W1, L256, "Xy", "mr", 8, 0, KZ),
    EX("vbroadcastsd", P66, M2, 0x19, W1, L512, "Xz", "mr", 8, 0, KZ),
    VX("vbroadcastf128", P66, M2, 0x1a, W0, L256, "my", "mr", 0),
    VX("vbroadcasti128", P66, M2, 0x5a, W0, L256, "my", "mr", 0),
#define PBROADCAST(n, op, ew, N) \
    VX(n, P66, M2, op, W0, L128, "Xx", "mr", 0), VX(n, P66, M2, op, W0, L256, "Xy", "mr", 0), \
        EX(n, P66, M2, op, ew, L128, "Xx", "mr", N, 0, KZ), EX(n, P66, M2, op, ew, L256, "Xy", "mr", N, 0, KZ), \
        EX(n, P66, M2, op, ew, L512, "Xz", "mr", N, 0, KZ)
    PBROADCAST("vpbroadcastb", 0x78, W0, 1),
    EX("vpbroadcastb", P66, M2, 0x7a, W0, L128, "Rx", "mr", 0, 0, KZ),
    EX("vpbroadcastb", P66, M2, 0x7a, W0, L256, "Ry", "mr", 0, 0, KZ),
    EX("vpbroadcastb", P66, M2, 0x7a, W0, L512, "Rz", "mr", 0, 0, KZ),
    PBROADCAST("vpbroadcastw", 0x79, W0, 2),
    EX("vpbroadcastw", P66, M2, 0x7b, W0, L128, "Rx", "mr", 0, 0, KZ),
    EX("vpbroadcastw", P66, M2, 0x7b, W0, L256, "Ry", "mr", 0, 0, KZ),
    EX("vpbroadcastw", P66, M2, 0x7b, W0, L512, "Rz", "mr", 0, 0, KZ),
    PBROADCAST("vpbroadcastd", 0x58, W0, 4),
    EX("vpbroadcastd", P66, M2, 0x7c, W0, L128, "dx", "mr", 0, 0, KZ),
    EX("vpbroadcastd", P66, M2, 0x7c, W0, L256, "dy", "mr", 0, 0, KZ),
    EX("vpbroadcastd", P66, M2, 0x7c, W0, L512, "dz", "mr", 0, 0, KZ),
    PBROADCAST("vpbroadcastq", 0x59, W1, 8),
    EX("vpbroadcastq", P66, M2, 0x7c, W1, L128, "qx", "mr", 0, 0, KZ | O64),
    EX("vpbroadcastq", P66, M2, 0x7c, W1, L256, "qy", "mr", 0, 0, KZ | O64),
    EX("vpbroadcastq", P66, M2, 0x7c, W1, L512, "qz", "mr", 0, 0, KZ | O64),
    VX("vinsertf128", P66, M3, 0x18, W0, L256, "iXyy", "imvr", 0),
    VX("vinserti128", P66, M3, 0x38, W0, L256, "iXyy", "imvr", 0),
    VX("vextractf128", P66, M3, 0x19, W0, L256, "iyX", "irm", 0),
    VX("vextracti128", P66, M3, 0x39, W0, L256, "iyX", "irm", 0),
    VX("vinsertps", P66, M3, 0x21, WIG, L128, "iXxx", "imvr", 0),
    EX("vinsertps", P66, M3, 0x21, W0, L128, "iXxx", "imvr", 4, 0, 0),
    VX("vextractps", P66, M3, 0x17, WIG, L128, "ixR", "irm", 0),
    VX("vextractps", P66, M3, 0x17, WIG, L128, "ixm", "irm", 0),
    EX("vextractps", P66, M3, 0x17, WIG, L128, "ixR", "irm", 4, 0, 0),
    EX("vextractps", P66, M3, 0x17, WIG, L128, "ixm", "irm", 4, 0, 0),
    VX("vpextrb", P66, M3, 0x14, W0, L128, "ixR", "irm", 0),
    VX("vpextrb", P66, M3, 0x14, W0, L128, "ixm", "irm", 0),
    EX("vpextrb", P66, M3, 0x14, WIG, L128, "ixR", "irm", 1, 0, 0),
    EX("vpextrb", P66, M3, 0x14, WIG, L128, "ixm", "irm", 1, 0, 0),
    VX("vpextrw", P66, M1, 0xc5, W0, L128, "ixR", "imr", 0),
    VX("vpextrw", P66, M3, 0x15, W0, L128, "ixm", "irm", 0),
    EX("vpextrw", P66, M1, 0xc5, WIG, L128, "ixR", "imr", 0, 0, 0),
    EX("vpextrw", P66, M3, 0x15, WIG, L128, "ixm", "irm", 2, 0, 0),
    VX("vpextrd", P66, M3, 0x16, W0, L128, "ixD", "irm", 0),
    EX("vpextrd", P66, M3, 0x16, W0, L128, "ixD", "irm", 4, 0, 0),
    VX("vpextrq", P66, M3, 0x16, W1, L128, "ixQ", "irm", O64),
    EX("vpextrq", P66, M3, 0x16, W1, L128, "ixQ", "irm", 8, 0, O64),
    VX("vpinsrb", P66, M3, 0x20, W0, L128, "iExx", "imvr", 0),
    EX("vpinsrb", P66, M3, 0x20, WIG, L128, "iExx", "imvr", 1, 0, 0),
    VX("vpinsrw", P66, M1, 0xc4, W0, L128, "iExx", "imvr", 0),
    EX("vpinsrw", P66, M1, 0xc4, WIG, L128, "iExx", "imvr", 2, 0, 0),
    VX("vpinsrd", P66, M3, 0x22, W0, L128, "iDxx", "imvr", 0),
    EX("vpinsrd", P66, M3, 0x22, W0, L128, "iDxx", "imvr", 4, 0, 0),
    VX("vpinsrq", P66, M3, 0x22, W1, L128, "iQxx", "imvr", O64),
    EX("vpinsrq", P66, M3, 0x22, W1, L128, "iQxx", "imvr", 8, 0, O64),

    /* Integer arithmetic. */
    INT_BW("vpaddb", P66, M1, 0xfc, 0),
    INT_BW("vpaddw", P66, M1, 0xfd, 0),
    INT_D("vpaddd", P66, M1, 0xfe, 0),
    INT_Q("vpaddq", P66, M1, 0xd4, 0),
    INT_BW("vpaddsb", P66, M1, 0xec, 0),
    INT_BW("vpaddsw", P66, M1, 0xed, 0),
    INT_BW("vpaddusb", P66, M1, 0xdc, 0),
    INT_BW("vpaddusw", P66, M1, 0xdd, 0),
    INT_BW("vpsubb", P66, M1, 0xf8, 0),
    INT_BW("vpsubw", P66, M1, 0xf9, 0),
    INT_D("vpsubd", P66, M1, 0xfa, 0),
    INT_Q("vpsubq", P66, M1, 0xfb, 0),
    INT_BW("vpsubsb", P66, M1, 0xe8, 0),
    INT_BW("vpsubsw", P66, M1, 0xe9, 0),
    INT_BW("vpsubusb", P66, M1, 0xd8, 0),
    INT_BW("vpsubusw", P66, M1, 0xd9, 0),
    INT_BW("vpavgb", P66, M1, 0xe0, 0),
    INT_BW("vpavgw", P66, M1, 0xe3, 0),
    INT_BW("vpmaddwd", P66, M1, 0xf5, 0),
    INT_BW("vpmaddubsw", P66, M2, 0x04, 0),
    INT_BW("vpmulhuw", P66, M1, 0xe4, 0),
    INT_BW("vpmulhw", P66, M1, 0xe5, 0),
    INT_BW("vpmulhrsw", P66, M2, 0x0b, 0),
    INT_BW("vpmullw", P66, M1, 0xd5, 0),
    INT_D("vpmulld", P66, M2, 0x40, 0),
    INT_Q("vpmuludq", P66, M1, 0xf4, 0),
    INT_Q("vpmuldq", P66, M2, 0x28, 0),
    INT_BW("vpsadbw", P66, M1, 0xf6, 0),
    INT_BW("vpmaxsb", P66, M2, 0x3c, 0),
    INT_BW("vpmaxsw", P66, M1, 0xee, 0),
    INT_D("vpmaxsd", P66, M2, 0x3d, 0),
    INT_BW("vpmaxub", P66, M1, 0xde, 0),
    INT_BW("vpmaxuw", P66, M2, 0x3e, 0),
    INT_D("vpmaxud", P66, M2, 0x3f, 0),
    INT_BW("vpminsb", P66, M2, 0x38, 0),
    INT_BW("vpminsw", P66, M1, 0xea, 0),
    INT_D("vpminsd", P66, M2, 0x39, 0),
    INT_BW("vpminub", P66, M1, 0xda, 0),
    INT_BW("vpminuw", P66, M2, 0x3a, 0),
    INT_D("vpminud", P66, M2, 0x3b, 0),
    INT_V("vpand", P66, M1, 0xdb, 0),
    INT_V("vpandn", P66, M1, 0xdf, 0),
    INT_V("vpor", P66, M1, 0xeb, 0),
    INT_V("vpxor", P66, M1, 0xef, 0),
    INT_V("vphaddw", P66, M2, 0x01, 0),
    INT_V("vphaddd", P66, M2, 0x02, 0),
    INT_V("vphaddsw", P66, M2, 0x03, 0),
    INT_V("vphsubw", P66, M2, 0x05, 0),
    INT_V("vphsubd", P66, M2, 0x06, 0),
    INT_V("vphsubsw", P66, M2, 0x07, 0),
    INT_V("vpsignb", P66, M2, 0x08, 0),
    INT_V("vpsignw", P66, M2, 0x09, 0),
    INT_V("vpsignd", P66, M2, 0x0a, 0),
    INT_BW("vpshufb", P66, M2, 0x00, 0),
    INT_BW("vpacksswb", P66, M1, 0x63, 0),
    INT_D("vpackssdw", P66, M1, 0x6b, 0),
    INT_BW("vpackuswb", P66, M1, 0x67, 0),
    INT_D("vpackusdw", P66, M2, 0x2b, 0),
    INT_BW("vpunpckhbw", P66, M1, 0x68, 0),
    INT_BW("vpunpckhwd", P66, M1, 0x69, 0),
    INT_D("vpunpckhdq", P66, M1, 0x6a, 0),
    INT_Q("vpunpckhqdq", P66, M1, 0x6d, 0),
    INT_BW("vpunpcklbw", P66, M1, 0x60, 0),
    INT_BW("vpunpcklwd", P66, M1, 0x61, 0),
    INT_D("vpunpckldq", P66, M1, 0x62, 0),
    INT_Q("vpunpcklqdq", P66, M1, 0x6c, 0),
    V2("vpabsb", P66, M2, 0x1c, WIG, 0),
    E2("vpabsb", P66, M2, 0x1c, WIG, 0, 0),
    V2("vpabsw", P66, M2, 0x1d, WIG, 0),
    E2("vpabsw", P66, M2, 0x1d, WIG, 0, 0),
    V2("vpabsd", P66, M2, 0x1e, WIG, 0),
    E2("vpabsd", P66, M2, 0x1e, W0, 4, 0),
    VX("vphminposuw", P66, M2, 0x41, WIG, L128, "Xx", "mr", 0),
    V3I("vmpsadbw", P66, M3, 0x42, WIG, 0),
    E3I("vmpsadbw", PF3, M3, 0x42, W0, 0, 0),
    V3I("vpalignr", P66, M3, 0x0f, WIG, 0),
    E3I("vpalignr", P66, M3, 0x0f, WIG, 0, 0),
    V2I("vpshufd", P66, M1, 0x70, WIG, 0),
    E2I("vpshufd", P66, M1, 0x70, W0, 4, 0),
    V2I("vpshufhw", PF3, M1, 0x70, WIG, 0),
    E2I("vpshufhw", PF3, M1, 0x70, WIG, 0, 0),
    V2I("vpshuflw", PF2, M1, 0x70, WIG, 0),
    E2I("vpshuflw", PF2, M1, 0x70, WIG, 0, 0),

    /* Integer compares: into a vector with VEX, into a mask with EVEX. */
#define PCMP(n, map, op, vfl, ew, bc) \
    V3(n, P66, map, op, WIG, vfl), EX(n, P66, map, op, ew, L128, "Xxk", "mvr", 16, bc, K), \
        EX(n, P66, map, op, ew, L256, "Yyk", "mvr", 32, bc, K), EX(n, P66, map, op, ew, L512, "Zzk", "mvr", 64, bc, K)
    PCMP("vpcmpeqb", M1, 0x74, 0, WIG, 0),
    PCMP("vpcmpeqw", M1, 0x75, 0, WIG, 0),
    PCMP("vpcmpeqd", M1, 0x76, 0, W0, 4),
    PCMP("vpcmpeqq", M2, 0x29, 0, W1, 8),
    PCMP("vpcmpgtb", M1, 0x64, 0, WIG, 0),
    PCMP("vpcmpgtw", M1, 0x65, 0, WIG, 0),
    PCMP("vpcmpgtd", M1, 0x66, 0, W0, 4),
    PCMP("vpcmpgtq", M2, 0x37, 0, W1, 8),
    VX("vpcmpestri", P66, M3, 0x61, WIG, L128, "iXx", "imr", 0),
    VX("vpcmpestrm", P66, M3, 0x60, WIG, L128, "iXx", "imr", 0),
    VX("vpcmpistri", P66, M3, 0x63, WIG, L128, "iXx", "imr", 0),
    VX("vpcmpistrm", P66, M3, 0x62, WIG, L128, "iXx", "imr", 0),

    /* Shifts. */
    SHIFT("vpsllw", 0xf1, 0x71, 6, WIG, 0),
    SHIFT("vpslld", 0xf2, 0x72, 6, W0, 4),
    SHIFT("vpsllq", 0xf3, 0x73, 6, W1, 8),
    SHIFT("vpsraw", 0xe1, 0x71, 4, WIG, 0),
    SHIFT("vpsrad", 0xe2, 0x72, 4, W0, 4),
    SHIFT("vpsrlw", 0xd1, 0x71, 2, WIG, 0),
    SHIFT("vpsrld", 0xd2, 0x72, 2, W0, 4),
    SHIFT("vpsrlq", 0xd3, 0x73, 2, W1, 8),
#define BYTESHIFT(n, dg) \
    VX(n, P66, M1, 0x73, WIG, L128, "ixx", "imv", DG(dg)), VX(n, P66, M1, 0x73, WIG, L256, "iyy", "imv", DG(dg)), \
        EX(n, P66, M1, 0x73, WIG, L128, "iXx", "imv", 16, 0, DG(dg)), \
        EX(n, P66, M1, 0x73, WIG, L256, "iYy", "imv", 32, 0, DG(dg)), \
        EX(n, P66, M1, 0x73, WIG, L512, "iZz", "imv", 64, 0, DG(dg))
    BYTESHIFT("vpslldq", 7),
    BYTESHIFT("vpsrldq", 3),
    V3("vpsllvd", P66, M2, 0x47, W0, 0),
    E3("vpsllvd", P66, M2, 0x47, W0, 4, 0),
    V3("vpsllvq", P66, M2, 0x47, W1, 0),
    E3("vpsllvq", P66, M2, 0x47, W1, 8, 0),
    V3("vpsravd", P66, M2, 0x46, W0, 0),
    E3("vpsravd", P66, M2, 0x46, W0, 4, 0),
    V3("vpsrlvd", P66, M2, 0x45, W0, 0),
    E3("vpsrlvd", P66, M2, 0x45, W0, 4, 0),
    V3("vpsrlvq", P66, M2, 0x45, W1, 0),
    E3("vpsrlvq", P66, M2, 0x45, W1, 8, 0),

    /* Extensions. */
    PMOVX("vpmovsxbw", 0x20, 8, 16, 32, "Y"),
    PMOVX("vpmovsxbd", 0x21, 4, 8, 16, "X"),
    PMOVX("vpmovsxbq", 0x22, 2, 4, 8, "X"),
    PMOVX("vpmovsxwd", 0x23, 8, 16, 32, "Y"),
    PMOVX("vpmovsxwq", 0x24, 4, 8, 16, "X"),
    PMOVX("vpmovsxdq", 0x25, 8, 16, 32, "Y"),
    PMOVX("vpmovzxbw", 0x30, 8, 16, 32, "Y"),
    PMOVX("vpmovzxbd", 0x31, 4, 8, 16, "X"),
    PMOVX("vpmovzxbq", 0x32, 2, 4, 8, "X"),
    PMOVX("vpmovzxwd", 0x33, 8, 16, 32, "Y"),
    PMOVX("vpmovzxwq", 0x34, 4, 8, 16, "X"),
    PMOVX("vpmovzxdq", 0x35, 8, 16, 32, "Y"),

    /* AES, carry-less multiplication, Galois field. */
#define AESROW(n, op) \
    VX(n, P66, M2, op, WIG, L128, "Xxx", "mvr", 0), VX(n, P66, M2, op, WIG, L256, "Yyy", "mvr", 0), \
        EX(n, P66, M2, op, WIG, L128, "Xxx", "mvr", 16, 0, 0), EX(n, P66, M2, op, WIG, L256, "Yyy", "mvr", 32, 0, 0), \
        EX(n, P66, M2, op, WIG, L512, "Zzz", "mvr", 64, 0, 0)
    AESROW("vaesenc", 0xdc),
    AESROW("vaesenclast", 0xdd),
    AESROW("vaesdec", 0xde),
    AESROW("vaesdeclast", 0xdf),
    VX("vaesimc", P66, M2, 0xdb, WIG, L128, "Xx", "mr", 0),
    VX("vaeskeygenassist", P66, M3, 0xdf, WIG, L128, "iXx", "imr", 0),
    V3I("vpclmulqdq", P66, M3, 0x44, WIG, 0),
    EX("vpclmulqdq", P66, M3, 0x44, WIG, L128, "iXxx", "imvr", 16, 0, 0),
    EX("vpclmulqdq", P66, M3, 0x44, WIG, L256, "iYyy", "imvr", 32, 0, 0),
    EX("vpclmulqdq", P66, M3, 0x44, WIG, L512, "iZzz", "imvr", 64, 0, 0),
    V3I("vgf2p8affineqb", P66, M3, 0xce, W1, 0),
    E3I("vgf2p8affineqb", P66, M3, 0xce, W1, 8, 0),
    V3I("vgf2p8affineinvqb", P66, M3, 0xcf, W1, 0),
    E3I("vgf2p8affineinvqb", P66, M3, 0xcf, W1, 8, 0),
    V3("vgf2p8mulb", P66, M2, 0xcf, W0, 0),
    E3("vgf2p8mulb", P66, M2, 0xcf, W0, 0, 0),

    /* VNNI and IFMA: AVX-512 unless {vex} is written. */
    E3("vpdpbusd", P66, M2, 0x50, W0, 4, 0),
    V3("vpdpbusd", P66, M2, 0x50, W0, 0),
    E3("vpdpbusds", P66, M2, 0x51, W0, 4, 0),
    V3("vpdpbusds", P66, M2, 0x51, W0, 0),
    E3("vpdpwssd", P66, M2, 0x52, W0, 4, 0),
    V3("vpdpwssd", P66, M2, 0x52, W0, 0),
    E3("vpdpwssds", P66, M2, 0x53, W0, 4, 0),
    V3("vpdpwssds", P66, M2, 0x53, W0, 0),
    E3("vpmadd52luq", P66, M2, 0xb4, W1, 8, 0),
    V3("vpmadd52luq", P66, M2, 0xb4, W1, 0),
    E3("vpmadd52huq", P66, M2, 0xb5, W1, 8, 0),
    V3("vpmadd52huq", P66, M2, 0xb5, W1, 0),
    V3("vpdpbssd", PF2, M2, 0x50, W0, 0),
    E3("vpdpbssd", PF2, M2, 0x50, W0, 4, 0),
    V3("vpdpbssds", PF2, M2, 0x51, W0, 0),
    E3("vpdpbssds", PF2, M2, 0x51, W0, 4, 0),
    V3("vpdpbsud", PF3, M2, 0x50, W0, 0),
    E3("vpdpbsud", PF3, M2, 0x50, W0, 4, 0),
    V3("vpdpbsuds", PF3, M2, 0x51, W0, 0),
    E3("vpdpbsuds", PF3, M2, 0x51, W0, 4, 0),
    V3("vpdpbuud", P0, M2, 0x50, W0, 0),
    E3("vpdpbuud", P0, M2, 0x50, W0, 4, 0),
    V3("vpdpbuuds", P0, M2, 0x51, W0, 0),
    E3("vpdpbuuds", P0, M2, 0x51, W0, 4, 0),

    /* FMA. */
    FMA_PONLY("vfmaddsub", 0x96, 0xa6, 0xb6),
    FMA_PONLY("vfmsubadd", 0x97, 0xa7, 0xb7),
    FMA("vfmadd", 0x98, 0xa8, 0xb8),
    FMA("vfmsub", 0x9a, 0xaa, 0xba),
    FMA("vfnmadd", 0x9c, 0xac, 0xbc),
    FMA("vfnmsub", 0x9e, 0xae, 0xbe),

    /* FMA4. */
    FMA4_P("vfmaddsubps", 0x5c),
    FMA4_P("vfmaddsubpd", 0x5d),
    FMA4_P("vfmsubaddps", 0x5e),
    FMA4_P("vfmsubaddpd", 0x5f),
    FMA4("vfmadd", 0x68),
    FMA4("vfmsub", 0x6c),
    FMA4("vfnmadd", 0x78),
    FMA4("vfnmsub", 0x7c),

    /* Gathers.  Those of AVX2 have a vector register for a mask, in
     * vvvv; those of AVX-512 a mask register, which must be given.
     * And scatters.  `a`, `b`, `c` are the index of each length, `x`,
     * `y`, `z` the vector. */
#define GATHER5(n, op, w, N, a, x, b, y, c, z) \
    EX(n, P66, M2, op, w, L128, a x, "mr", N, 0, KREQ), EX(n, P66, M2, op, w, L256, b y, "mr", N, 0, KREQ), \
        EX(n, P66, M2, op, w, L512, c z, "mr", N, 0, KREQ)
#define SCATTER(n, op, w, N, a, x, b, y, c, z) \
    EX(n, P66, M2, op, w, L128, x a, "rm", N, 0, KREQ), EX(n, P66, M2, op, w, L256, y b, "rm", N, 0, KREQ), \
        EX(n, P66, M2, op, w, L512, z c, "rm", N, 0, KREQ)
    GATHER2("vgatherdps", 0x92, W0, "1", "x", "2", "y"),
    GATHER5("vgatherdps", 0x92, W0, 4, "1", "x", "2", "y", "3", "z"),
    GATHER2("vgatherdpd", 0x92, W1, "1", "x", "1", "y"),
    GATHER5("vgatherdpd", 0x92, W1, 8, "1", "x", "1", "y", "2", "z"),
    VX("vgatherqps", P66, M2, 0x93, W0, L128, "x1x", "vmr", 0),
    VX("vgatherqps", P66, M2, 0x93, W0, L256, "x2x", "vmr", 0),
    GATHER5("vgatherqps", 0x93, W0, 4, "1", "x", "2", "x", "3", "y"),
    GATHER2("vgatherqpd", 0x93, W1, "1", "x", "2", "y"),
    GATHER5("vgatherqpd", 0x93, W1, 8, "1", "x", "2", "y", "3", "z"),
    GATHER2("vpgatherdd", 0x90, W0, "1", "x", "2", "y"),
    GATHER5("vpgatherdd", 0x90, W0, 4, "1", "x", "2", "y", "3", "z"),
    GATHER2("vpgatherdq", 0x90, W1, "1", "x", "1", "y"),
    GATHER5("vpgatherdq", 0x90, W1, 8, "1", "x", "1", "y", "2", "z"),
    VX("vpgatherqd", P66, M2, 0x91, W0, L128, "x1x", "vmr", 0),
    VX("vpgatherqd", P66, M2, 0x91, W0, L256, "x2x", "vmr", 0),
    GATHER5("vpgatherqd", 0x91, W0, 4, "1", "x", "2", "x", "3", "y"),
    GATHER2("vpgatherqq", 0x91, W1, "1", "x", "2", "y"),
    GATHER5("vpgatherqq", 0x91, W1, 8, "1", "x", "2", "y", "3", "z"),

    /*
     * AVX-512 alone from here.
     */
    SCATTER("vscatterdps", 0xa2, W0, 4, "1", "x", "2", "y", "3", "z"),
    SCATTER("vscatterdpd", 0xa2, W1, 8, "1", "x", "1", "y", "2", "z"),
    SCATTER("vscatterqps", 0xa3, W0, 4, "1", "x", "2", "x", "3", "y"),
    SCATTER("vscatterqpd", 0xa3, W1, 8, "1", "x", "2", "y", "3", "z"),
    SCATTER("vpscatterdd", 0xa0, W0, 4, "1", "x", "2", "y", "3", "z"),
    SCATTER("vpscatterdq", 0xa0, W1, 8, "1", "x", "1", "y", "2", "z"),
    SCATTER("vpscatterqd", 0xa1, W0, 4, "1", "x", "2", "x", "3", "y"),
    SCATTER("vpscatterqq", 0xa1, W1, 8, "1", "x", "2", "y", "3", "z"),

    /* Logic and arithmetic that AVX has under one name and AVX-512
     * under two. */
    E3("vpandd", P66, M1, 0xdb, W0, 4, 0),
    E3("vpandq", P66, M1, 0xdb, W1, 8, 0),
    E3("vpandnd", P66, M1, 0xdf, W0, 4, 0),
    E3("vpandnq", P66, M1, 0xdf, W1, 8, 0),
    E3("vpord", P66, M1, 0xeb, W0, 4, 0),
    E3("vporq", P66, M1, 0xeb, W1, 8, 0),
    E3("vpxord", P66, M1, 0xef, W0, 4, 0),
    E3("vpxorq", P66, M1, 0xef, W1, 8, 0),
    E3("vpmaxsq", P66, M2, 0x3d, W1, 8, 0),
    E3("vpmaxuq", P66, M2, 0x3f, W1, 8, 0),
    E3("vpminsq", P66, M2, 0x39, W1, 8, 0),
    E3("vpminuq", P66, M2, 0x3b, W1, 8, 0),
    E3("vpmullq", P66, M2, 0x40, W1, 8, 0),
    E2("vpabsq", P66, M2, 0x1f, W1, 8, 0),
    E3("vpmultishiftqb", P66, M2, 0x83, W1, 8, 0),
    E3I("vdbpsadbw", P66, M3, 0x42, W0, 0, 0),
    E3I("vpternlogd", P66, M3, 0x25, W0, 4, 0),
    E3I("vpternlogq", P66, M3, 0x25, W1, 8, 0),
    E3I("valignd", P66, M3, 0x03, W0, 4, 0),
    E3I("valignq", P66, M3, 0x03, W1, 8, 0),
    E3("vblendmps", P66, M2, 0x65, W0, 4, 0),
    E3("vblendmpd", P66, M2, 0x65, W1, 8, 0),
    E3("vpblendmd", P66, M2, 0x64, W0, 4, 0),
    E3("vpblendmq", P66, M2, 0x64, W1, 8, 0),
    E3("vpblendmb", P66, M2, 0x66, W0, 0, 0),
    E3("vpblendmw", P66, M2, 0x66, W1, 0, 0),
    E3("vpermi2d", P66, M2, 0x76, W0, 4, 0),
    E3("vpermi2q", P66, M2, 0x76, W1, 8, 0),
    E3("vpermi2ps", P66, M2, 0x77, W0, 4, 0),
    E3("vpermi2pd", P66, M2, 0x77, W1, 8, 0),
    E3("vpermi2b", P66, M2, 0x75, W0, 0, 0),
    E3("vpermi2w", P66, M2, 0x75, W1, 0, 0),
    E3("vpermt2d", P66, M2, 0x7e, W0, 4, 0),
    E3("vpermt2q", P66, M2, 0x7e, W1, 8, 0),
    E3("vpermt2ps", P66, M2, 0x7f, W0, 4, 0),
    E3("vpermt2pd", P66, M2, 0x7f, W1, 8, 0),
    E3("vpermt2b", P66, M2, 0x7d, W0, 0, 0),
    E3("vpermt2w", P66, M2, 0x7d, W1, 0, 0),
    E3("vpermb", P66, M2, 0x8d, W0, 0, 0),
    E3("vpermw", P66, M2, 0x8d, W1, 0, 0),
    E3("vscalefps", P66, M2, 0x2c, W0, 4, ER),
    E3("vscalefpd", P66, M2, 0x2c, W1, 8, ER),
    E3S("vscalefss", P66, M2, 0x2d, W0, 4, ER),
    E3S("vscalefsd", P66, M2, 0x2d, W1, 8, ER),
    E2("vrcp14ps", P66, M2, 0x4c, W0, 4, 0),
    E2("vrcp14pd", P66, M2, 0x4c, W1, 8, 0),
    E3S("vrcp14ss", P66, M2, 0x4d, W0, 4, 0),
    E3S("vrcp14sd", P66, M2, 0x4d, W1, 8, 0),
    E2("vrsqrt14ps", P66, M2, 0x4e, W0, 4, 0),
    E2("vrsqrt14pd", P66, M2, 0x4e, W1, 8, 0),
    E3S("vrsqrt14ss", P66, M2, 0x4f, W0, 4, 0),
    E3S("vrsqrt14sd", P66, M2, 0x4f, W1, 8, 0),
    E3S("vrcp28ss", P66, M2, 0xcb, W0, 4, SAE),
    E3S("vrcp28sd", P66, M2, 0xcb, W1, 8, SAE),
    E3S("vrsqrt28ss", P66, M2, 0xcd, W0, 4, SAE),
    E3S("vrsqrt28sd", P66, M2, 0xcd, W1, 8, SAE),
    E2("vgetexpps", P66, M2, 0x42, W0, 4, SAE),
    E2("vgetexppd", P66, M2, 0x42, W1, 8, SAE),
    E3S("vgetexpss", P66, M2, 0x43, W0, 4, SAE),
    E3S("vgetexpsd", P66, M2, 0x43, W1, 8, SAE),
    E2("vpconflictd", P66, M2, 0xc4, W0, 4, 0),
    E2("vpconflictq", P66, M2, 0xc4, W1, 8, 0),
    E2("vplzcntd", P66, M2, 0x44, W0, 4, 0),
    E2("vplzcntq", P66, M2, 0x44, W1, 8, 0),

    /* With a number: of two operands, and scalars. */
#define E3SI(n, pp, map, op, w, N, fl) EX(n, pp, map, op, w, LIG, "iXxx", "imvr", N, 0, KZ | (fl))
    E2I("vgetmantps", P66, M3, 0x26, W0, 4, SAE),
    E2I("vgetmantpd", P66, M3, 0x26, W1, 8, SAE),
    E3SI("vgetmantss", P66, M3, 0x27, W0, 4, SAE),
    E3SI("vgetmantsd", P66, M3, 0x27, W1, 8, SAE),
    E2I("vrndscaleps", P66, M3, 0x08, W0, 4, SAE),
    E2I("vrndscalepd", P66, M3, 0x09, W1, 8, SAE),
    E3SI("vrndscaless", P66, M3, 0x0a, W0, 4, SAE),
    E3SI("vrndscalesd", P66, M3, 0x0b, W1, 8, SAE),
    E2I("vreduceps", P66, M3, 0x56, W0, 4, SAE),
    E2I("vreducepd", P66, M3, 0x56, W1, 8, SAE),
    E3SI("vreducess", P66, M3, 0x57, W0, 4, SAE),
    E3SI("vreducesd", P66, M3, 0x57, W1, 8, SAE),
    E3I("vrangeps", P66, M3, 0x50, W0, 4, SAE),
    E3I("vrangepd", P66, M3, 0x50, W1, 8, SAE),
    E3SI("vrangess", P66, M3, 0x51, W0, 4, SAE),
    E3SI("vrangesd", P66, M3, 0x51, W1, 8, SAE),
    E3I("vfixupimmps", P66, M3, 0x54, W0, 4, SAE),
    E3I("vfixupimmpd", P66, M3, 0x54, W1, 8, SAE),
    E3SI("vfixupimmss", P66, M3, 0x55, W0, 4, SAE),
    E3SI("vfixupimmsd", P66, M3, 0x55, W1, 8, SAE),
    E3I("vpshldw", P66, M3, 0x70, W1, 0, 0),
    E3I("vpshldd", P66, M3, 0x71, W0, 4, 0),
    E3I("vpshldq", P66, M3, 0x71, W1, 8, 0),
    E3I("vpshrdw", P66, M3, 0x72, W1, 0, 0),
    E3I("vpshrdd", P66, M3, 0x73, W0, 4, 0),
    E3I("vpshrdq", P66, M3, 0x73, W1, 8, 0),
    E3("vpshldvw", P66, M2, 0x70, W1, 0, 0),
    E3("vpshldvd", P66, M2, 0x71, W0, 4, 0),
    E3("vpshldvq", P66, M2, 0x71, W1, 8, 0),
    E3("vpshrdvw", P66, M2, 0x72, W1, 0, 0),
    E3("vpshrdvd", P66, M2, 0x73, W0, 4, 0),
    E3("vpshrdvq", P66, M2, 0x73, W1, 8, 0),
#define E4SHUF(n, w, op, bc) \
    EX(n, P66, M3, op, w, L256, "iYyy", "imvr", 32, bc, KZ), EX(n, P66, M3, op, w, L512, "iZzz", "imvr", 64, bc, KZ)
    E4SHUF("vshuff32x4", W0, 0x23, 4),
    E4SHUF("vshuff64x2", W1, 0x23, 8),
    E4SHUF("vshufi32x4", W0, 0x43, 4),
    E4SHUF("vshufi64x2", W1, 0x43, 8),

    /* Shifts and rotates. */
    EX("vpsraq", P66, M1, 0xe2, W1, L128, "Xxx", "mvr", 16, 0, KZ),
    EX("vpsraq", P66, M1, 0xe2, W1, L256, "Xyy", "mvr", 16, 0, KZ),
    EX("vpsraq", P66, M1, 0xe2, W1, L512, "Xzz", "mvr", 16, 0, KZ),
    EX("vpsraq", P66, M1, 0x72, W1, L128, "iXx", "imv", 16, 8, KZ | DG(4)),
    EX("vpsraq", P66, M1, 0x72, W1, L256, "iYy", "imv", 32, 8, KZ | DG(4)),
    EX("vpsraq", P66, M1, 0x72, W1, L512, "iZz", "imv", 64, 8, KZ | DG(4)),
    E3("vpsravq", P66, M2, 0x46, W1, 8, 0),
    E3("vpsllvw", P66, M2, 0x12, W1, 0, 0),
    E3("vpsravw", P66, M2, 0x11, W1, 0, 0),
    E3("vpsrlvw", P66, M2, 0x10, W1, 0, 0),
#define ROTI(n, w, bc, dg) \
    EX(n, P66, M1, 0x72, w, L128, "iXx", "imv", 16, bc, KZ | DG(dg)), \
        EX(n, P66, M1, 0x72, w, L256, "iYy", "imv", 32, bc, KZ | DG(dg)), \
        EX(n, P66, M1, 0x72, w, L512, "iZz", "imv", 64, bc, KZ | DG(dg))
    ROTI("vprold", W0, 4, 1),
    ROTI("vprolq", W1, 8, 1),
    ROTI("vprord", W0, 4, 0),
    ROTI("vprorq", W1, 8, 0),
    E3("vprolvd", P66, M2, 0x15, W0, 4, 0),
    E3("vprolvq", P66, M2, 0x15, W1, 8, 0),
    E3("vprorvd", P66, M2, 0x14, W0, 4, 0),
    E3("vprorvq", P66, M2, 0x14, W1, 8, 0),

    /* Compares and tests into a mask. */
#define E3K(n, pp, map, op, w, bc) \
    EX(n, pp, map, op, w, L128, "Xxk", "mvr", 16, bc, K), EX(n, pp, map, op, w, L256, "Yyk", "mvr", 32, bc, K), \
        EX(n, pp, map, op, w, L512, "Zzk", "mvr", 64, bc, K)
#define E3KI(n, pp, map, op, w, bc) \
    EX(n, pp, map, op, w, L128, "iXxk", "imvr", 16, bc, K), EX(n, pp, map, op, w, L256, "iYyk", "imvr", 32, bc, K), \
        EX(n, pp, map, op, w, L512, "iZzk", "imvr", 64, bc, K)
    E3KI("vpcmpd", P66, M3, 0x1f, W0, 4),
    E3KI("vpcmpq", P66, M3, 0x1f, W1, 8),
    E3KI("vpcmpud", P66, M3, 0x1e, W0, 4),
    E3KI("vpcmpuq", P66, M3, 0x1e, W1, 8),
    E3KI("vpcmpb", P66, M3, 0x3f, W0, 0),
    E3KI("vpcmpw", P66, M3, 0x3f, W1, 0),
    E3KI("vpcmpub", P66, M3, 0x3e, W0, 0),
    E3KI("vpcmpuw", P66, M3, 0x3e, W1, 0),
    E3K("vptestmd", P66, M2, 0x27, W0, 4),
    E3K("vptestmq", P66, M2, 0x27, W1, 8),
    E3K("vptestmb", P66, M2, 0x26, W0, 0),
    E3K("vptestmw", P66, M2, 0x26, W1, 0),
    E3K("vptestnmd", PF3, M2, 0x27, W0, 4),
    E3K("vptestnmq", PF3, M2, 0x27, W1, 8),
    E3K("vptestnmb", PF3, M2, 0x26, W0, 0),
    E3K("vptestnmw", PF3, M2, 0x26, W1, 0),
    E3K("vpshufbitqmb", P66, M2, 0x8f, W0, 0),
    /* The class of a number: the length is the register's, or the
     * broadcast's, or the letter's after the name. */
#define FPCLASS(n, w, bc) \
    EX(n, P66, M3, 0x66, w, L128, "ixk", "imr", 16, bc, K), EX(n, P66, M3, 0x66, w, L256, "iyk", "imr", 32, bc, K), \
        EX(n, P66, M3, 0x66, w, L512, "izk", "imr", 64, bc, K), \
        EX(n, P66, M3, 0x66, w, L128, "imk", "imr", 16, bc, K | AS_VEC_F_BCST), \
        EX(n, P66, M3, 0x66, w, L256, "imk", "imr", 32, bc, K | AS_VEC_F_BCST), \
        EX(n, P66, M3, 0x66, w, L512, "imk", "imr", 64, bc, K | AS_VEC_F_BCST), \
        EX(n "x", P66, M3, 0x66, w, L128, "iXk", "imr", 16, bc, K), \
        EX(n "y", P66, M3, 0x66, w, L256, "iYk", "imr", 32, bc, K), \
        EX(n "z", P66, M3, 0x66, w, L512, "iZk", "imr", 64, bc, K)
    FPCLASS("vfpclassps", W0, 4),
    FPCLASS("vfpclasspd", W1, 8),
    EX("vfpclassss", P66, M3, 0x67, W0, LIG, "iXk", "imr", 4, 0, K),
    EX("vfpclasssd", P66, M3, 0x67, W1, LIG, "iXk", "imr", 8, 0, K),

    /* Masks to vectors and back. */
#define V2M(n, w, op) \
    EX(n, PF3, M2, op, w, L128, "xk", "mr", 0, 0, 0), EX(n, PF3, M2, op, w, L256, "yk", "mr", 0, 0, 0), \
        EX(n, PF3, M2, op, w, L512, "zk", "mr", 0, 0, 0)
#define M2V(n, w, op) \
    EX(n, PF3, M2, op, w, L128, "kx", "mr", 0, 0, 0), EX(n, PF3, M2, op, w, L256, "ky", "mr", 0, 0, 0), \
        EX(n, PF3, M2, op, w, L512, "kz", "mr", 0, 0, 0)
    V2M("vpmovb2m", W0, 0x29),
    V2M("vpmovw2m", W1, 0x29),
    V2M("vpmovd2m", W0, 0x39),
    V2M("vpmovq2m", W1, 0x39),
    M2V("vpmovm2b", W0, 0x28),
    M2V("vpmovm2w", W1, 0x28),
    M2V("vpmovm2d", W0, 0x38),
    M2V("vpmovm2q", W1, 0x38),
    M2V("vpbroadcastmb2q", W1, 0x2a),
    M2V("vpbroadcastmw2d", W0, 0x3a),

    /* Truncations: the whole of a vector into part of one, or memory.
     * `dz` is what the 512-bit one fills, `nx`, `ny`, `nz` the bytes. */
#define PMOVDOWN(n, op, dz, nx, ny, nz) \
    EX(n, PF3, M2, op, W0, L128, "xx", "rm", nx, 0, KZ), EX(n, PF3, M2, op, W0, L128, "xm", "rm", nx, 0, K), \
        EX(n, PF3, M2, op, W0, L256, "yx", "rm", ny, 0, KZ), EX(n, PF3, M2, op, W0, L256, "ym", "rm", ny, 0, K), \
        EX(n, PF3, M2, op, W0, L512, "z" dz, "rm", nz, 0, KZ), EX(n, PF3, M2, op, W0, L512, "zm", "rm", nz, 0, K)
#define PMOVDOWN3(base, op, dz, nx, ny, nz) \
    PMOVDOWN("vpmov" base, (op) + 0x10, dz, nx, ny, nz), PMOVDOWN("vpmovs" base, op, dz, nx, ny, nz), \
        PMOVDOWN("vpmovus" base, (op) - 0x10, dz, nx, ny, nz)
    PMOVDOWN3("wb", 0x20, "y", 8, 16, 32),
    PMOVDOWN3("db", 0x21, "x", 4, 8, 16),
    PMOVDOWN3("qb", 0x22, "x", 2, 4, 8),
    PMOVDOWN3("dw", 0x23, "y", 8, 16, 32),
    PMOVDOWN3("qw", 0x24, "x", 4, 8, 16),
    PMOVDOWN3("qd", 0x25, "y", 8, 16, 32),

    /* Compress and expand. */
#define COMPRESS(n, w, op, N) \
    EX(n, P66, M2, op, w, L128, "xx", "rm", N, 0, KZ), EX(n, P66, M2, op, w, L128, "xm", "rm", N, 0, K), \
        EX(n, P66, M2, op, w, L256, "yy", "rm", N, 0, KZ), EX(n, P66, M2, op, w, L256, "ym", "rm", N, 0, K), \
        EX(n, P66, M2, op, w, L512, "zz", "rm", N, 0, KZ), EX(n, P66, M2, op, w, L512, "zm", "rm", N, 0, K)
#define EXPAND(n, w, op, N) \
    EX(n, P66, M2, op, w, L128, "Xx", "mr", N, 0, KZ), EX(n, P66, M2, op, w, L256, "Yy", "mr", N, 0, KZ), \
        EX(n, P66, M2, op, w, L512, "Zz", "mr", N, 0, KZ)
    COMPRESS("vcompressps", W0, 0x8a, 4),
    COMPRESS("vcompresspd", W1, 0x8a, 8),
    COMPRESS("vpcompressd", W0, 0x8b, 4),
    COMPRESS("vpcompressq", W1, 0x8b, 8),
    COMPRESS("vpcompressb", W0, 0x63, 1),
    COMPRESS("vpcompressw", W1, 0x63, 2),
    EXPAND("vexpandps", W0, 0x88, 4),
    EXPAND("vexpandpd", W1, 0x88, 8),
    EXPAND("vpexpandd", W0, 0x89, 4),
    EXPAND("vpexpandq", W1, 0x89, 8),
    EXPAND("vpexpandb", W0, 0x62, 1),
    EXPAND("vpexpandw", W1, 0x62, 2),

    /* Broadcasts, inserts and extracts of parts of a vector. */
    EX("vbroadcastf32x2", P66, M2, 0x19, W0, L256, "Xy", "mr", 8, 0, KZ),
    EX("vbroadcastf32x2", P66, M2, 0x19, W0, L512, "Xz", "mr", 8, 0, KZ),
    EX("vbroadcasti32x2", P66, M2, 0x59, W0, L128, "Xx", "mr", 8, 0, KZ),
    EX("vbroadcasti32x2", P66, M2, 0x59, W0, L256, "Xy", "mr", 8, 0, KZ),
    EX("vbroadcasti32x2", P66, M2, 0x59, W0, L512, "Xz", "mr", 8, 0, KZ),
#define BCAST128(n, w, op) \
    EX(n, P66, M2, op, w, L256, "my", "mr", 16, 0, KZ), EX(n, P66, M2, op, w, L512, "mz", "mr", 16, 0, KZ)
    BCAST128("vbroadcastf32x4", W0, 0x1a),
    BCAST128("vbroadcastf64x2", W1, 0x1a),
    BCAST128("vbroadcasti32x4", W0, 0x5a),
    BCAST128("vbroadcasti64x2", W1, 0x5a),
    EX("vbroadcastf32x8", P66, M2, 0x1b, W0, L512, "mz", "mr", 32, 0, KZ),
    EX("vbroadcastf64x4", P66, M2, 0x1b, W1, L512, "mz", "mr", 32, 0, KZ),
    EX("vbroadcasti32x8", P66, M2, 0x5b, W0, L512, "mz", "mr", 32, 0, KZ),
    EX("vbroadcasti64x4", P66, M2, 0x5b, W1, L512, "mz", "mr", 32, 0, KZ),
#define INSERT128(n, w, op) \
    EX(n, P66, M3, op, w, L256, "iXyy", "imvr", 16, 0, KZ), EX(n, P66, M3, op, w, L512, "iXzz", "imvr", 16, 0, KZ)
#define EXTRACT128(n, w, op) \
    EX(n, P66, M3, op, w, L256, "iyx", "irm", 16, 0, KZ), EX(n, P66, M3, op, w, L256, "iym", "irm", 16, 0, K), \
        EX(n, P66, M3, op, w, L512, "izx", "irm", 16, 0, KZ), EX(n, P66, M3, op, w, L512, "izm", "irm", 16, 0, K)
#define INSERT256(n, w, op) EX(n, P66, M3, op, w, L512, "iYzz", "imvr", 32, 0, KZ)
#define EXTRACT256(n, w, op) \
    EX(n, P66, M3, op, w, L512, "izy", "irm", 32, 0, KZ), EX(n, P66, M3, op, w, L512, "izm", "irm", 32, 0, K)
    INSERT128("vinsertf32x4", W0, 0x18),
    INSERT128("vinsertf64x2", W1, 0x18),
    INSERT128("vinserti32x4", W0, 0x38),
    INSERT128("vinserti64x2", W1, 0x38),
    INSERT256("vinsertf32x8", W0, 0x1a),
    INSERT256("vinsertf64x4", W1, 0x1a),
    INSERT256("vinserti32x8", W0, 0x3a),
    INSERT256("vinserti64x4", W1, 0x3a),
    EXTRACT128("vextractf32x4", W0, 0x19),
    EXTRACT128("vextractf64x2", W1, 0x19),
    EXTRACT128("vextracti32x4", W0, 0x39),
    EXTRACT128("vextracti64x2", W1, 0x39),
    EXTRACT256("vextractf32x8", W0, 0x1b),
    EXTRACT256("vextractf64x4", W1, 0x1b),
    EXTRACT256("vextracti32x8", W0, 0x3b),
    EXTRACT256("vextracti64x4", W1, 0x3b),

    /* Conversions.  ENARROW: to a vector of half the length, from a
     * register, or from memory whose size a broadcast or the name's
     * last letter says.  EWIDEN: from one of half the length. */
#define ENARROW(n, pp, op, w, bc, efl) \
    EX(n, pp, M1, op, w, L128, "xx", "mr", 16, bc, KZ), EX(n, pp, M1, op, w, L256, "yx", "mr", 32, bc, KZ), \
        EX(n, pp, M1, op, w, L128, "mx", "mr", 16, bc, KZ | AS_VEC_F_BCST), \
        EX(n, pp, M1, op, w, L256, "mx", "mr", 32, bc, KZ | AS_VEC_F_BCST), \
        EX(n, pp, M1, op, w, L512, "Zy", "mr", 64, bc, KZ | (efl)), \
        EX(n "x", pp, M1, op, w, L128, "Xx", "mr", 16, bc, KZ), EX(n "y", pp, M1, op, w, L256, "Yx", "mr", 32, bc, KZ)
#define EWIDEN(n, pp, op, w, efl) \
    EX(n, pp, M1, op, w, L128, "Xx", "mr", 8, 4, KZ), EX(n, pp, M1, op, w, L256, "Xy", "mr", 16, 4, KZ), \
        EX(n, pp, M1, op, w, L512, "Yz", "mr", 32, 4, KZ | (efl))
    ENARROW("vcvtpd2udq", P0, 0x79, W1, 8, ER),
    ENARROW("vcvttpd2udq", P0, 0x78, W1, 8, SAE),
    ENARROW("vcvtqq2ps", P0, 0x5b, W1, 8, ER),
    ENARROW("vcvtuqq2ps", PF2, 0x7a, W1, 8, ER),
    E2("vcvtps2udq", P0, M1, 0x79, W0, 4, ER),
    E2("vcvttps2udq", P0, M1, 0x78, W0, 4, SAE),
    E2("vcvtudq2ps", PF2, M1, 0x7a, W0, 4, ER),
    EWIDEN("vcvtudq2pd", PF3, 0x7a, W0, 0),
    E2("vcvtpd2qq", P66, M1, 0x7b, W1, 8, ER),
    E2("vcvtpd2uqq", P66, M1, 0x79, W1, 8, ER),
    E2("vcvttpd2qq", P66, M1, 0x7a, W1, 8, SAE),
    E2("vcvttpd2uqq", P66, M1, 0x78, W1, 8, SAE),
    E2("vcvtqq2pd", PF3, M1, 0xe6, W1, 8, ER),
    E2("vcvtuqq2pd", PF3, M1, 0x7a, W1, 8, ER),
    EWIDEN("vcvtps2qq", P66, 0x7b, W0, ER),
    EWIDEN("vcvtps2uqq", P66, 0x79, W0, ER),
    EWIDEN("vcvttps2qq", P66, 0x7a, W0, SAE),
    EWIDEN("vcvttps2uqq", P66, 0x78, W0, SAE),
#define ECVT2USI(n, pp, op, N, efl) \
    EX(n, pp, M1, op, W0, LIG, "Xd", "mr", N, 0, efl), EX(n, pp, M1, op, W1, LIG, "Xq", "mr", N, 0, O64 | (efl)), \
        EX(n "l", pp, M1, op, W0, LIG, "Xd", "mr", N, 0, efl), EX(n "q", pp, M1, op, W1, LIG, "Xq", "mr", N, 0, O64 | (efl))
    ECVT2USI("vcvtsd2usi", PF2, 0x79, 8, ER),
    ECVT2USI("vcvttsd2usi", PF2, 0x78, 8, SAE),
    ECVT2USI("vcvtss2usi", PF3, 0x79, 4, ER),
    ECVT2USI("vcvttss2usi", PF3, 0x78, 4, SAE),
#define EUSI2F(n, pp, efl32, efl64) \
    EX(n, pp, M1, 0x7b, W0, LIG, "Dxx", "mvr", 4, 0, efl32), EX(n, pp, M1, 0x7b, W1, LIG, "qxx", "mvr", 8, 0, O64 | (efl64)), \
        EX(n "l", pp, M1, 0x7b, W0, LIG, "Dxx", "mvr", 4, 0, efl32), \
        EX(n "q", pp, M1, 0x7b, W1, LIG, "Qxx", "mvr", 8, 0, O64 | (efl64))
    EUSI2F("vcvtusi2sd", PF2, 0, ER),
    EUSI2F("vcvtusi2ss", PF3, ER, ER),
};

const size_t as_x86_vec_table_count = sizeof(as_x86_vec_table) / sizeof(as_x86_vec_table[0]);
