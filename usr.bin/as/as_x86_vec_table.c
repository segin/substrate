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
#define M4 AS_VEC_MAP_4
#define M5 AS_VEC_MAP_5
#define M6 AS_VEC_MAP_6
#define M7 AS_VEC_MAP_7
#define M8 AS_VEC_MAP_8
#define M9 AS_VEC_MAP_9
#define M10 AS_VEC_MAP_10
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
/* XOP: as VEX is, behind 8F. */
#define XP(n, pp, map, op, w, l, ops, roles, fl) {n, AS_VEC_XOP, pp, map, op, w, l, ops, roles, 0, 0, fl}

/* VEX, %xmm and %ymm: three operands; two; and each with a byte before. */
#define V3(n, pp, map, op, w, fl) \
    VX(n, pp, map, op, w, L128, "Xxx", "mvr", fl), VX(n, pp, map, op, w, L256, "Yyy", "mvr", fl)
#define V2(n, pp, map, op, w, fl) \
    VX(n, pp, map, op, w, L128, "Xx", "mr", fl), VX(n, pp, map, op, w, L256, "Yy", "mr", fl)
#define V3I(n, pp, map, op, w, fl) \
    VX(n, pp, map, op, w, L128, "iXxx", "imvr", fl), VX(n, pp, map, op, w, L256, "iYyy", "imvr", fl)
#define V2I(n, pp, map, op, w, fl) \
    VX(n, pp, map, op, w, L128, "iXx", "imr", fl), VX(n, pp, map, op, w, L256, "iYy", "imr", fl)
/* The same where the byte is never negative. */
#define V3U(n, pp, map, op, w, fl) \
    VX(n, pp, map, op, w, L128, "uXxx", "imvr", fl), VX(n, pp, map, op, w, L256, "uYyy", "imvr", fl)
#define V2U(n, pp, map, op, w, fl) \
    VX(n, pp, map, op, w, L128, "uXx", "imr", fl), VX(n, pp, map, op, w, L256, "uYy", "imr", fl)
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
#define E3U(n, pp, map, op, w, bc, fz) \
    EX(n, pp, map, op, w, L128, "uXxx", "imvr", 16, bc, KZ), EX(n, pp, map, op, w, L256, "uYyy", "imvr", 32, bc, KZ), \
        EX(n, pp, map, op, w, L512, "uZzz", "imvr", 64, bc, KZ | (fz))
#define E2U(n, pp, map, op, w, bc, fz) \
    EX(n, pp, map, op, w, L128, "uXx", "imr", 16, bc, KZ), EX(n, pp, map, op, w, L256, "uYy", "imr", 32, bc, KZ), \
        EX(n, pp, map, op, w, L512, "uZz", "imr", 64, bc, KZ | (fz))
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
        VX(n, P66, M1, iop, WIG, L128, "uxx", "imv", DG(dg)), VX(n, P66, M1, iop, WIG, L256, "uyy", "imv", DG(dg)), \
        EX(n, P66, M1, op, ew, L128, "Xxx", "mvr", 16, 0, KZ), EX(n, P66, M1, op, ew, L256, "Xyy", "mvr", 16, 0, KZ), \
        EX(n, P66, M1, op, ew, L512, "Xzz", "mvr", 16, 0, KZ), \
        EX(n, P66, M1, iop, ew, L128, "uXx", "imv", 16, bc, KZ | DG(dg)), \
        EX(n, P66, M1, iop, ew, L256, "uYy", "imv", 32, bc, KZ | DG(dg)), \
        EX(n, P66, M1, iop, ew, L512, "uZz", "imv", 64, bc, KZ | DG(dg))

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
    V2U("vroundps", P66, M3, 0x08, WIG, 0),
    V2U("vroundpd", P66, M3, 0x09, WIG, 0),
    VX("vroundss", P66, M3, 0x0a, WIG, LIG, "uXxx", "imvr", 0),
    VX("vroundsd", P66, M3, 0x0b, WIG, LIG, "uXxx", "imvr", 0),
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
    V3U("vcmpps", P0, M1, 0xc2, WIG, 0),
    EX("vcmpps", P0, M1, 0xc2, W0, L128, "uXxk", "imvr", 16, 4, K),
    EX("vcmpps", P0, M1, 0xc2, W0, L256, "uYyk", "imvr", 32, 4, K),
    EX("vcmpps", P0, M1, 0xc2, W0, L512, "uZzk", "imvr", 64, 4, K | SAE),
    V3U("vcmppd", P66, M1, 0xc2, WIG, 0),
    EX("vcmppd", P66, M1, 0xc2, W1, L128, "uXxk", "imvr", 16, 8, K),
    EX("vcmppd", P66, M1, 0xc2, W1, L256, "uYyk", "imvr", 32, 8, K),
    EX("vcmppd", P66, M1, 0xc2, W1, L512, "uZzk", "imvr", 64, 8, K | SAE),
    VX("vcmpss", PF3, M1, 0xc2, WIG, LIG, "uXxx", "imvr", 0),
    EX("vcmpss", PF3, M1, 0xc2, W0, LIG, "uXxk", "imvr", 4, 0, K | SAE),
    VX("vcmpsd", PF2, M1, 0xc2, WIG, LIG, "uXxx", "imvr", 0),
    EX("vcmpsd", PF2, M1, 0xc2, W1, LIG, "uXxk", "imvr", 8, 0, K | SAE),
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
    VX("vcvtps2ph", P66, M3, 0x1d, W0, L128, "uxX", "irm", 0),
    VX("vcvtps2ph", P66, M3, 0x1d, W0, L256, "uyX", "irm", 0),
    EX("vcvtps2ph", P66, M3, 0x1d, W0, L128, "uxX", "irm", 8, 0, KZ),
    EX("vcvtps2ph", P66, M3, 0x1d, W0, L256, "uyX", "irm", 16, 0, KZ),
    EX("vcvtps2ph", P66, M3, 0x1d, W0, L512, "uzY", "irm", 32, 0, KZ | SAE),

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
    EX("vpbroadcastb", P66, M2, 0x7a, W0, L128, "dx", "mr", 0, 0, KZ),
    EX("vpbroadcastb", P66, M2, 0x7a, W0, L256, "dy", "mr", 0, 0, KZ),
    EX("vpbroadcastb", P66, M2, 0x7a, W0, L512, "dz", "mr", 0, 0, KZ),
    PBROADCAST("vpbroadcastw", 0x79, W0, 2),
    EX("vpbroadcastw", P66, M2, 0x7b, W0, L128, "dx", "mr", 0, 0, KZ),
    EX("vpbroadcastw", P66, M2, 0x7b, W0, L256, "dy", "mr", 0, 0, KZ),
    EX("vpbroadcastw", P66, M2, 0x7b, W0, L512, "dz", "mr", 0, 0, KZ),
    PBROADCAST("vpbroadcastd", 0x58, W0, 4),
    EX("vpbroadcastd", P66, M2, 0x7c, W0, L128, "dx", "mr", 0, 0, KZ),
    EX("vpbroadcastd", P66, M2, 0x7c, W0, L256, "dy", "mr", 0, 0, KZ),
    EX("vpbroadcastd", P66, M2, 0x7c, W0, L512, "dz", "mr", 0, 0, KZ),
    PBROADCAST("vpbroadcastq", 0x59, W1, 8),
    EX("vpbroadcastq", P66, M2, 0x7c, W1, L128, "qx", "mr", 0, 0, KZ | O64),
    EX("vpbroadcastq", P66, M2, 0x7c, W1, L256, "qy", "mr", 0, 0, KZ | O64),
    EX("vpbroadcastq", P66, M2, 0x7c, W1, L512, "qz", "mr", 0, 0, KZ | O64),
    VX("vinsertf128", P66, M3, 0x18, W0, L256, "uXyy", "imvr", 0),
    VX("vinserti128", P66, M3, 0x38, W0, L256, "uXyy", "imvr", 0),
    VX("vextractf128", P66, M3, 0x19, W0, L256, "uyX", "irm", 0),
    VX("vextracti128", P66, M3, 0x39, W0, L256, "uyX", "irm", 0),
    VX("vinsertps", P66, M3, 0x21, WIG, L128, "uXxx", "imvr", 0),
    EX("vinsertps", P66, M3, 0x21, W0, L128, "uXxx", "imvr", 4, 0, 0),
    VX("vextractps", P66, M3, 0x17, WIG, L128, "uxR", "irm", 0),
    VX("vextractps", P66, M3, 0x17, WIG, L128, "uxm", "irm", 0),
    EX("vextractps", P66, M3, 0x17, WIG, L128, "uxR", "irm", 4, 0, 0),
    EX("vextractps", P66, M3, 0x17, WIG, L128, "uxm", "irm", 4, 0, 0),
    VX("vpextrb", P66, M3, 0x14, W0, L128, "uxR", "irm", 0),
    VX("vpextrb", P66, M3, 0x14, W0, L128, "uxm", "irm", 0),
    EX("vpextrb", P66, M3, 0x14, WIG, L128, "uxR", "irm", 1, 0, 0),
    EX("vpextrb", P66, M3, 0x14, WIG, L128, "uxm", "irm", 1, 0, 0),
    VX("vpextrw", P66, M1, 0xc5, W0, L128, "uxR", "imr", 0),
    VX("vpextrw", P66, M3, 0x15, W0, L128, "uxm", "irm", 0),
    EX("vpextrw", P66, M1, 0xc5, WIG, L128, "uxR", "imr", 0, 0, 0),
    EX("vpextrw", P66, M3, 0x15, WIG, L128, "uxm", "irm", 2, 0, 0),
    VX("vpextrd", P66, M3, 0x16, W0, L128, "uxD", "irm", 0),
    EX("vpextrd", P66, M3, 0x16, W0, L128, "uxD", "irm", 4, 0, 0),
    VX("vpextrq", P66, M3, 0x16, W1, L128, "uxQ", "irm", O64),
    EX("vpextrq", P66, M3, 0x16, W1, L128, "uxQ", "irm", 8, 0, O64),
    VX("vpinsrb", P66, M3, 0x20, W0, L128, "uExx", "imvr", 0),
    EX("vpinsrb", P66, M3, 0x20, WIG, L128, "uExx", "imvr", 1, 0, 0),
    VX("vpinsrw", P66, M1, 0xc4, W0, L128, "uExx", "imvr", 0),
    EX("vpinsrw", P66, M1, 0xc4, WIG, L128, "uExx", "imvr", 2, 0, 0),
    VX("vpinsrd", P66, M3, 0x22, W0, L128, "uDxx", "imvr", 0),
    EX("vpinsrd", P66, M3, 0x22, W0, L128, "uDxx", "imvr", 4, 0, 0),
    VX("vpinsrq", P66, M3, 0x22, W1, L128, "uQxx", "imvr", O64),
    EX("vpinsrq", P66, M3, 0x22, W1, L128, "uQxx", "imvr", 8, 0, O64),

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
    V3U("vpalignr", P66, M3, 0x0f, WIG, 0),
    E3U("vpalignr", P66, M3, 0x0f, WIG, 0, 0),
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
    VX("vpcmpestri", P66, M3, 0x61, WIG, L128, "uXx", "imr", 0),
    VX("vpcmpestrm", P66, M3, 0x60, WIG, L128, "uXx", "imr", 0),
    VX("vpcmpistri", P66, M3, 0x63, WIG, L128, "uXx", "imr", 0),
    VX("vpcmpistrm", P66, M3, 0x62, WIG, L128, "uXx", "imr", 0),

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
    VX(n, P66, M1, 0x73, WIG, L128, "uxx", "imv", DG(dg)), VX(n, P66, M1, 0x73, WIG, L256, "uyy", "imv", DG(dg)), \
        EX(n, P66, M1, 0x73, WIG, L128, "uXx", "imv", 16, 0, DG(dg)), \
        EX(n, P66, M1, 0x73, WIG, L256, "uYy", "imv", 32, 0, DG(dg)), \
        EX(n, P66, M1, 0x73, WIG, L512, "uZz", "imv", 64, 0, DG(dg))
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
    VX("vaeskeygenassist", P66, M3, 0xdf, WIG, L128, "uXx", "imr", 0),
    V3I("vpclmulqdq", P66, M3, 0x44, WIG, 0),
    EX("vpclmulqdq", P66, M3, 0x44, WIG, L128, "iXxx", "imvr", 16, 0, 0),
    EX("vpclmulqdq", P66, M3, 0x44, WIG, L256, "iYyy", "imvr", 32, 0, 0),
    EX("vpclmulqdq", P66, M3, 0x44, WIG, L512, "iZzz", "imvr", 64, 0, 0),
    V3U("vgf2p8affineqb", P66, M3, 0xce, W1, 0),
    E3U("vgf2p8affineqb", P66, M3, 0xce, W1, 8, 0),
    V3U("vgf2p8affineinvqb", P66, M3, 0xcf, W1, 0),
    E3U("vgf2p8affineinvqb", P66, M3, 0xcf, W1, 8, 0),
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
    E3U("valignd", P66, M3, 0x03, W0, 4, 0),
    E3U("valignq", P66, M3, 0x03, W1, 8, 0),
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
#define E3SI(n, pp, map, op, w, N, fl) EX(n, pp, map, op, w, LIG, "uXxx", "imvr", N, 0, KZ | (fl))
    E2U("vgetmantps", P66, M3, 0x26, W0, 4, SAE),
    E2U("vgetmantpd", P66, M3, 0x26, W1, 8, SAE),
    E3SI("vgetmantss", P66, M3, 0x27, W0, 4, SAE),
    E3SI("vgetmantsd", P66, M3, 0x27, W1, 8, SAE),
    E2U("vrndscaleps", P66, M3, 0x08, W0, 4, SAE),
    E2U("vrndscalepd", P66, M3, 0x09, W1, 8, SAE),
    E3SI("vrndscaless", P66, M3, 0x0a, W0, 4, SAE),
    E3SI("vrndscalesd", P66, M3, 0x0b, W1, 8, SAE),
    E2U("vreduceps", P66, M3, 0x56, W0, 4, SAE),
    E2U("vreducepd", P66, M3, 0x56, W1, 8, SAE),
    E3SI("vreducess", P66, M3, 0x57, W0, 4, SAE),
    E3SI("vreducesd", P66, M3, 0x57, W1, 8, SAE),
    E3U("vrangeps", P66, M3, 0x50, W0, 4, SAE),
    E3U("vrangepd", P66, M3, 0x50, W1, 8, SAE),
    E3SI("vrangess", P66, M3, 0x51, W0, 4, SAE),
    E3SI("vrangesd", P66, M3, 0x51, W1, 8, SAE),
    E3I("vfixupimmps", P66, M3, 0x54, W0, 4, SAE),
    E3I("vfixupimmpd", P66, M3, 0x54, W1, 8, SAE),
    EX("vfixupimmss", P66, M3, 0x55, W0, LIG, "iXxx", "imvr", 4, 0, KZ | SAE),
    EX("vfixupimmsd", P66, M3, 0x55, W1, LIG, "iXxx", "imvr", 8, 0, KZ | SAE),
    E3U("vpshldw", P66, M3, 0x70, W1, 0, 0),
    E3U("vpshldd", P66, M3, 0x71, W0, 4, 0),
    E3U("vpshldq", P66, M3, 0x71, W1, 8, 0),
    E3U("vpshrdw", P66, M3, 0x72, W1, 0, 0),
    E3U("vpshrdd", P66, M3, 0x73, W0, 4, 0),
    E3U("vpshrdq", P66, M3, 0x73, W1, 8, 0),
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
    EX("vpsraq", P66, M1, 0x72, W1, L128, "uXx", "imv", 16, 8, KZ | DG(4)),
    EX("vpsraq", P66, M1, 0x72, W1, L256, "uYy", "imv", 32, 8, KZ | DG(4)),
    EX("vpsraq", P66, M1, 0x72, W1, L512, "uZz", "imv", 64, 8, KZ | DG(4)),
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
    EX(n, pp, map, op, w, L128, "uXxk", "imvr", 16, bc, K), EX(n, pp, map, op, w, L256, "uYyk", "imvr", 32, bc, K), \
        EX(n, pp, map, op, w, L512, "uZzk", "imvr", 64, bc, K)
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
    EX(n, P66, M3, op, w, L256, "uXyy", "imvr", 16, 0, KZ), EX(n, P66, M3, op, w, L512, "uXzz", "imvr", 16, 0, KZ)
#define EXTRACT128(n, w, op) \
    EX(n, P66, M3, op, w, L256, "uyx", "irm", 16, 0, KZ), EX(n, P66, M3, op, w, L256, "uym", "irm", 16, 0, K), \
        EX(n, P66, M3, op, w, L512, "uzx", "irm", 16, 0, KZ), EX(n, P66, M3, op, w, L512, "uzm", "irm", 16, 0, K)
#define INSERT256(n, w, op) EX(n, P66, M3, op, w, L512, "uYzz", "imvr", 32, 0, KZ)
#define EXTRACT256(n, w, op) \
    EX(n, P66, M3, op, w, L512, "uzy", "irm", 32, 0, KZ), EX(n, P66, M3, op, w, L512, "uzm", "irm", 32, 0, K)
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

    /*
     * BMI1 and BMI2: VEX, and general registers.
     */
#define OP32 AS_VEC_F_OP32
#define OP16 AS_VEC_F_OP16
#define WEAK AS_VEC_F_WEAK
/* One of 32 bits and one of 64, under the name and under the name
 * with an l or a q after it. */
#define GPR3(n, pp, map, op, o32, o64, roles, fl) \
    VX(n, pp, map, op, W0, L128, o32, roles, AS_VEC_F_VEX3 | (fl)), \
        VX(n, pp, map, op, W1, L128, o64, roles, AS_VEC_F_VEX3 | O64 | (fl)), \
        VX(n "l", pp, map, op, W0, L128, o32, roles, AS_VEC_F_VEX3 | (fl)), \
        VX(n "q", pp, map, op, W1, L128, o64, roles, AS_VEC_F_VEX3 | O64 | (fl))
    GPR3("andn", P0, M2, 0xf2, "Ddd", "Qqq", "mvr", 0),
    /* (TBM's `bextr`, whose field is a number of four bytes: before
     * BMI's, so that the rows of the name stand together.) */
    XP("bextr", P0, M10, 0x10, W0, L128, "iDd", "imr", AS_VEC_F_IMM32),
    XP("bextr", P0, M10, 0x10, W1, L128, "iQq", "imr", AS_VEC_F_IMM32 | O64),
    GPR3("bextr", P0, M2, 0xf7, "dDd", "qQq", "vmr", 0),
    GPR3("bzhi", P0, M2, 0xf5, "dDd", "qQq", "vmr", 0),
    GPR3("mulx", PF2, M2, 0xf6, "Ddd", "Qqq", "mvr", 0),
    GPR3("pdep", PF2, M2, 0xf5, "Ddd", "Qqq", "mvr", 0),
    GPR3("pext", PF3, M2, 0xf5, "Ddd", "Qqq", "mvr", 0),
    GPR3("sarx", PF3, M2, 0xf7, "dDd", "qQq", "vmr", 0),
    GPR3("shlx", P66, M2, 0xf7, "dDd", "qQq", "vmr", 0),
    GPR3("shrx", PF2, M2, 0xf7, "dDd", "qQq", "vmr", 0),
    GPR3("rorx", PF2, M3, 0xf0, "iDd", "iQq", "imr", 0),
    GPR3("blsr", P0, M2, 0xf3, "Dd", "Qq", "mv", DG(1)),
    GPR3("blsmsk", P0, M2, 0xf3, "Dd", "Qq", "mv", DG(2)),
    GPR3("blsi", P0, M2, 0xf3, "Dd", "Qq", "mv", DG(3)),

    /*
     * No VEX from here: MMX, SSE to SSE4.2, AES, SHA, GFNI, and the
     * three counts that are written like them.
     */
#define LX(n, pp, map, op, w, ops, roles, fl) {n, AS_VEC_LEGACY, pp, map, op, w, 0, ops, roles, 0, 0, fl}
/* An instruction with no operand in ModRM: `b` the byte after the
 * opcode, 0 for none. */
#define LFIX(n, op, b, ops, roles) {n, AS_VEC_LEGACY, P0, M1, op, W0, 0, ops, roles, b, 0, 0}
/* %xmm, or memory, to %xmm; and with a number before. */
#define S2(n, pp, map, op) LX(n, pp, map, op, W0, "Xx", "mr", 0)
#define S2I(n, pp, map, op) LX(n, pp, map, op, W0, "iXx", "imr", 0)
#define S2U(n, pp, map, op) LX(n, pp, map, op, W0, "uXx", "imr", 0)
/* The same for MMX registers, and for %xmm with the 66 that makes it so. */
#define MS2(n, map, op) LX(n, P0, map, op, W0, "Nn", "mr", 0), LX(n, P66, map, op, W0, "Xx", "mr", 0)
/* ps, pd, ss, sd of one opcode. */
#define SARITH(base, op) \
    S2(base "ps", P0, M1, op), S2(base "pd", P66, M1, op), S2(base "ss", PF3, M1, op), S2(base "sd", PF2, M1, op)
#define SLOGIC(base, op) S2(base "ps", P0, M1, op), S2(base "pd", P66, M1, op)
/* A whole vector to a register, and from one. */
#define SMOVE(n, pp, ld, st) LX(n, pp, M1, ld, W0, "Xx", "mr", SWAP), LX(n, pp, M1, st, W0, "xX", "rm", 0)
/* A shift: by a count in a register or in memory, and by a number. */
#define MSHIFT(n, op, iop, dg) \
    LX(n, P0, M1, op, W0, "Nn", "mr", 0), LX(n, P0, M1, iop, W0, "un", "im", DG(dg)), \
        LX(n, P66, M1, op, W0, "Xx", "mr", 0), LX(n, P66, M1, iop, W0, "ux", "im", DG(dg))
/* To a general register of 32 or 64 bits from a scalar, under the name
 * and with an l or a q after it. */
#define SCVT2SI(n, pp, op) \
    LX(n, pp, M1, op, W0, "Xd", "mr", 0), LX(n, pp, M1, op, W1, "Xq", "mr", O64), \
        LX(n "l", pp, M1, op, W0, "Xd", "mr", 0), LX(n "q", pp, M1, op, W1, "Xq", "mr", O64)
#define SSI2F(n, pp) \
    LX(n, pp, M1, 0x2a, W0, "Dx", "mr", 0), LX(n, pp, M1, 0x2a, W1, "qx", "mr", O64), \
        LX(n "l", pp, M1, 0x2a, W0, "Dx", "mr", 0), LX(n "q", pp, M1, 0x2a, W1, "Qx", "mr", O64)
/* A count of bits: of 16, 32 or 64, by the register or by the letter. */
#define BITCOUNT(n, op) \
    LX(n, PF3, M1, op, W0, "Ww", "mr", OP16), LX(n, PF3, M1, op, W0, "Dd", "mr", OP32), \
        LX(n, PF3, M1, op, W1, "Qq", "mr", O64), LX(n "w", PF3, M1, op, W0, "Ww", "mr", OP16), \
        LX(n "l", PF3, M1, op, W0, "Dd", "mr", OP32), LX(n "q", PF3, M1, op, W1, "Qq", "mr", O64)

    SARITH("add", 0x58),
    SARITH("mul", 0x59),
    SARITH("sub", 0x5c),
    SARITH("div", 0x5e),
    SARITH("max", 0x5f),
    SARITH("min", 0x5d),
    SARITH("sqrt", 0x51),
    SLOGIC("and", 0x54),
    SLOGIC("andn", 0x55),
    SLOGIC("or", 0x56),
    SLOGIC("xor", 0x57),
    S2("rcpps", P0, M1, 0x53),
    S2("rcpss", PF3, M1, 0x53),
    S2("rsqrtps", P0, M1, 0x52),
    S2("rsqrtss", PF3, M1, 0x52),
    S2U("cmpps", P0, M1, 0xc2),
    S2U("cmppd", P66, M1, 0xc2),
    S2U("cmpss", PF3, M1, 0xc2),
    LX("cmpsd", PF2, M1, 0xc2, W0, "uXx", "imr", WEAK),
    S2("comiss", P0, M1, 0x2f),
    S2("comisd", P66, M1, 0x2f),
    S2("ucomiss", P0, M1, 0x2e),
    S2("ucomisd", P66, M1, 0x2e),
    S2I("shufps", P0, M1, 0xc6),
    S2I("shufpd", P66, M1, 0xc6),
    S2("unpcklps", P0, M1, 0x14),
    S2("unpckhps", P0, M1, 0x15),
    S2("unpcklpd", P66, M1, 0x14),
    S2("unpckhpd", P66, M1, 0x15),
    S2("addsubpd", P66, M1, 0xd0),
    S2("addsubps", PF2, M1, 0xd0),
    S2("haddpd", P66, M1, 0x7c),
    S2("haddps", PF2, M1, 0x7c),
    S2("hsubpd", P66, M1, 0x7d),
    S2("hsubps", PF2, M1, 0x7d),
    S2U("roundps", P66, M3, 0x08),
    S2U("roundpd", P66, M3, 0x09),
    S2U("roundss", P66, M3, 0x0a),
    S2U("roundsd", P66, M3, 0x0b),
    S2I("dpps", P66, M3, 0x40),
    S2I("dppd", P66, M3, 0x41),
    S2I("blendps", P66, M3, 0x0c),
    S2I("blendpd", P66, M3, 0x0d),
    S2I("pblendw", P66, M3, 0x0e),
    S2I("mpsadbw", P66, M3, 0x42),
    S2U("insertps", P66, M3, 0x21),
    /* The mask of these is %xmm0, written or not. */
#define SBLENDV(n, op) LX(n, P66, M2, op, W0, "Xx", "mr", 0), LX(n, P66, M2, op, W0, "0Xx", "-mr", 0)
    SBLENDV("blendvps", 0x14),
    SBLENDV("blendvpd", 0x15),
    SBLENDV("pblendvb", 0x10),

    /* Conversions. */
    S2("cvtdq2pd", PF3, M1, 0xe6),
    S2("cvtdq2ps", P0, M1, 0x5b),
    S2("cvtpd2dq", PF2, M1, 0xe6),
    S2("cvtpd2ps", P66, M1, 0x5a),
    S2("cvtps2dq", P66, M1, 0x5b),
    S2("cvtps2pd", P0, M1, 0x5a),
    S2("cvttpd2dq", P66, M1, 0xe6),
    S2("cvttps2dq", PF3, M1, 0x5b),
    S2("cvtsd2ss", PF2, M1, 0x5a),
    S2("cvtss2sd", PF3, M1, 0x5a),
    SCVT2SI("cvtsd2si", PF2, 0x2d),
    SCVT2SI("cvttsd2si", PF2, 0x2c),
    SCVT2SI("cvtss2si", PF3, 0x2d),
    SCVT2SI("cvttss2si", PF3, 0x2c),
    SSI2F("cvtsi2sd", PF2),
    SSI2F("cvtsi2ss", PF3),
    LX("cvtpi2ps", P0, M1, 0x2a, W0, "Nx", "mr", 0),
    LX("cvtpi2pd", P66, M1, 0x2a, W0, "Nx", "mr", 0),
    LX("cvtps2pi", P0, M1, 0x2d, W0, "Xn", "mr", 0),
    LX("cvttps2pi", P0, M1, 0x2c, W0, "Xn", "mr", 0),
    LX("cvtpd2pi", P66, M1, 0x2d, W0, "Xn", "mr", 0),
    LX("cvttpd2pi", P66, M1, 0x2c, W0, "Xn", "mr", 0),

    /* Moves. */
    SMOVE("movaps", P0, 0x28, 0x29),
    SMOVE("movapd", P66, 0x28, 0x29),
    SMOVE("movups", P0, 0x10, 0x11),
    SMOVE("movupd", P66, 0x10, 0x11),
    SMOVE("movdqa", P66, 0x6f, 0x7f),
    SMOVE("movdqu", PF3, 0x6f, 0x7f),
    SMOVE("movss", PF3, 0x10, 0x11),
    LX("movsd", PF2, M1, 0x10, W0, "Xx", "mr", SWAP | WEAK),
    LX("movsd", PF2, M1, 0x11, W0, "xX", "rm", WEAK),
    LX("movd", P0, M1, 0x6e, W0, "Dn", "mr", 0),
    LX("movd", P0, M1, 0x7e, W0, "nD", "rm", 0),
    LX("movd", P66, M1, 0x6e, W0, "Dx", "mr", 0),
    LX("movd", P66, M1, 0x7e, W0, "xD", "rm", 0),
    LX("movd", P0, M1, 0x6e, W1, "qn", "mr", O64),
    LX("movd", P0, M1, 0x7e, W1, "nq", "rm", O64),
    LX("movd", P66, M1, 0x6e, W1, "qx", "mr", O64),
    LX("movd", P66, M1, 0x7e, W1, "xq", "rm", O64),
    LX("movq", P0, M1, 0x6f, W0, "Nn", "mr", WEAK),
    LX("movq", P0, M1, 0x7f, W0, "nN", "rm", WEAK),
    LX("movq", PF3, M1, 0x7e, W0, "Xx", "mr", WEAK),
    LX("movq", P66, M1, 0xd6, W0, "xX", "rm", WEAK),
    LX("movq", P0, M1, 0x6e, W1, "qn", "mr", O64 | WEAK),
    LX("movq", P0, M1, 0x7e, W1, "nq", "rm", O64 | WEAK),
    LX("movq", P66, M1, 0x6e, W1, "qx", "mr", O64 | WEAK),
    LX("movq", P66, M1, 0x7e, W1, "xq", "rm", O64 | WEAK),
    LX("movdq2q", PF2, M1, 0xd6, W0, "xn", "mr", 0),
    LX("movq2dq", PF3, M1, 0xd6, W0, "nx", "mr", 0),
    LX("movhlps", P0, M1, 0x12, W0, "xx", "mr", 0),
    LX("movlhps", P0, M1, 0x16, W0, "xx", "mr", 0),
#define SHALFMOVE(n, pp, ld, st) LX(n, pp, M1, ld, W0, "mx", "mr", 0), LX(n, pp, M1, st, W0, "xm", "rm", 0)
    SHALFMOVE("movhps", P0, 0x16, 0x17),
    SHALFMOVE("movhpd", P66, 0x16, 0x17),
    SHALFMOVE("movlps", P0, 0x12, 0x13),
    SHALFMOVE("movlpd", P66, 0x12, 0x13),
    LX("movmskps", P0, M1, 0x50, W0, "xR", "mr", 0),
    LX("movmskpd", P66, M1, 0x50, W0, "xR", "mr", 0),
    LX("pmovmskb", P0, M1, 0xd7, W0, "nR", "mr", 0),
    LX("pmovmskb", P66, M1, 0xd7, W0, "xR", "mr", 0),
    LX("movntps", P0, M1, 0x2b, W0, "xm", "rm", 0),
    LX("movntpd", P66, M1, 0x2b, W0, "xm", "rm", 0),
    LX("movntdq", P66, M1, 0xe7, W0, "xm", "rm", 0),
    LX("movntq", P0, M1, 0xe7, W0, "nm", "rm", 0),
    LX("movntdqa", P66, M2, 0x2a, W0, "mx", "mr", 0),
    LX("maskmovq", P0, M1, 0xf7, W0, "nn", "mr", 0),
    LX("maskmovdqu", P66, M1, 0xf7, W0, "xx", "mr", 0),
    LX("lddqu", PF2, M1, 0xf0, W0, "mx", "mr", 0),
    S2("movddup", PF2, M1, 0x12),
    S2("movshdup", PF3, M1, 0x16),
    S2("movsldup", PF3, M1, 0x12),
    LFIX("emms", 0x77, 0, "", ""),
    LFIX("monitor", 0x01, 0xc8, "", ""),
    LFIX("monitor", 0x01, 0xc8, "Ace", "---"),
    LFIX("mwait", 0x01, 0xc9, "", ""),
    LFIX("mwait", 0x01, 0xc9, "ac", "--"),

    /* Integer arithmetic of MMX and SSE2. */
    MS2("paddb", M1, 0xfc),
    MS2("paddw", M1, 0xfd),
    MS2("paddd", M1, 0xfe),
    MS2("paddq", M1, 0xd4),
    MS2("paddsb", M1, 0xec),
    MS2("paddsw", M1, 0xed),
    MS2("paddusb", M1, 0xdc),
    MS2("paddusw", M1, 0xdd),
    MS2("psubb", M1, 0xf8),
    MS2("psubw", M1, 0xf9),
    MS2("psubd", M1, 0xfa),
    MS2("psubq", M1, 0xfb),
    MS2("psubsb", M1, 0xe8),
    MS2("psubsw", M1, 0xe9),
    MS2("psubusb", M1, 0xd8),
    MS2("psubusw", M1, 0xd9),
    MS2("pand", M1, 0xdb),
    MS2("pandn", M1, 0xdf),
    MS2("por", M1, 0xeb),
    MS2("pxor", M1, 0xef),
    MS2("pavgb", M1, 0xe0),
    MS2("pavgw", M1, 0xe3),
    MS2("pcmpeqb", M1, 0x74),
    MS2("pcmpeqw", M1, 0x75),
    MS2("pcmpeqd", M1, 0x76),
    MS2("pcmpgtb", M1, 0x64),
    MS2("pcmpgtw", M1, 0x65),
    MS2("pcmpgtd", M1, 0x66),
    MS2("pmaddwd", M1, 0xf5),
    MS2("pmaxsw", M1, 0xee),
    MS2("pmaxub", M1, 0xde),
    MS2("pminsw", M1, 0xea),
    MS2("pminub", M1, 0xda),
    MS2("pmulhuw", M1, 0xe4),
    MS2("pmulhw", M1, 0xe5),
    MS2("pmullw", M1, 0xd5),
    MS2("pmuludq", M1, 0xf4),
    MS2("psadbw", M1, 0xf6),
    MS2("packsswb", M1, 0x63),
    MS2("packssdw", M1, 0x6b),
    MS2("packuswb", M1, 0x67),
    MS2("punpckhbw", M1, 0x68),
    MS2("punpckhwd", M1, 0x69),
    MS2("punpckhdq", M1, 0x6a),
    MS2("punpcklbw", M1, 0x60),
    MS2("punpcklwd", M1, 0x61),
    MS2("punpckldq", M1, 0x62),
    S2("punpckhqdq", P66, M1, 0x6d),
    S2("punpcklqdq", P66, M1, 0x6c),
    MSHIFT("psllw", 0xf1, 0x71, 6),
    MSHIFT("pslld", 0xf2, 0x72, 6),
    MSHIFT("psllq", 0xf3, 0x73, 6),
    MSHIFT("psraw", 0xe1, 0x71, 4),
    MSHIFT("psrad", 0xe2, 0x72, 4),
    MSHIFT("psrlw", 0xd1, 0x71, 2),
    MSHIFT("psrld", 0xd2, 0x72, 2),
    MSHIFT("psrlq", 0xd3, 0x73, 2),
    LX("pslldq", P66, M1, 0x73, W0, "ux", "im", DG(7)),
    LX("psrldq", P66, M1, 0x73, W0, "ux", "im", DG(3)),
    LX("pshufw", P0, M1, 0x70, W0, "iNn", "imr", 0),
    S2I("pshufd", P66, M1, 0x70),
    S2I("pshufhw", PF3, M1, 0x70),
    S2I("pshuflw", PF2, M1, 0x70),
    LX("pextrw", P0, M1, 0xc5, W0, "unR", "imr", 0),
    LX("pextrw", P66, M1, 0xc5, W0, "uxR", "imr", 0),
    LX("pextrw", P66, M3, 0x15, W0, "uxm", "irm", 0),
    LX("pinsrw", P0, M1, 0xc4, W0, "uEn", "imr", 0),
    LX("pinsrw", P66, M1, 0xc4, W0, "uEx", "imr", 0),

    /* SSSE3, on MMX registers and on %xmm. */
    MS2("pabsb", M2, 0x1c),
    MS2("pabsw", M2, 0x1d),
    MS2("pabsd", M2, 0x1e),
    MS2("phaddw", M2, 0x01),
    MS2("phaddd", M2, 0x02),
    MS2("phaddsw", M2, 0x03),
    MS2("phsubw", M2, 0x05),
    MS2("phsubd", M2, 0x06),
    MS2("phsubsw", M2, 0x07),
    MS2("pmaddubsw", M2, 0x04),
    MS2("pmulhrsw", M2, 0x0b),
    MS2("pshufb", M2, 0x00),
    MS2("psignb", M2, 0x08),
    MS2("psignw", M2, 0x09),
    MS2("psignd", M2, 0x0a),
    LX("palignr", P0, M3, 0x0f, W0, "uNn", "imr", 0),
    LX("palignr", P66, M3, 0x0f, W0, "uXx", "imr", 0),

    /* SSE4.1 and SSE4.2. */
    S2("packusdw", P66, M2, 0x2b),
    S2("pcmpeqq", P66, M2, 0x29),
    S2("pcmpgtq", P66, M2, 0x37),
    S2("phminposuw", P66, M2, 0x41),
    S2("pmaxsb", P66, M2, 0x3c),
    S2("pmaxsd", P66, M2, 0x3d),
    S2("pmaxuw", P66, M2, 0x3e),
    S2("pmaxud", P66, M2, 0x3f),
    S2("pminsb", P66, M2, 0x38),
    S2("pminsd", P66, M2, 0x39),
    S2("pminuw", P66, M2, 0x3a),
    S2("pminud", P66, M2, 0x3b),
    S2("pmuldq", P66, M2, 0x28),
    S2("pmulld", P66, M2, 0x40),
    S2("ptest", P66, M2, 0x17),
    S2("pmovsxbw", P66, M2, 0x20),
    S2("pmovsxbd", P66, M2, 0x21),
    S2("pmovsxbq", P66, M2, 0x22),
    S2("pmovsxwd", P66, M2, 0x23),
    S2("pmovsxwq", P66, M2, 0x24),
    S2("pmovsxdq", P66, M2, 0x25),
    S2("pmovzxbw", P66, M2, 0x30),
    S2("pmovzxbd", P66, M2, 0x31),
    S2("pmovzxbq", P66, M2, 0x32),
    S2("pmovzxwd", P66, M2, 0x33),
    S2("pmovzxwq", P66, M2, 0x34),
    S2("pmovzxdq", P66, M2, 0x35),
    LX("extractps", P66, M3, 0x17, W0, "uxE", "irm", 0),
    LX("pextrb", P66, M3, 0x14, W0, "uxE", "irm", 0),
    LX("pextrd", P66, M3, 0x16, W0, "uxD", "irm", 0),
    LX("pextrq", P66, M3, 0x16, W1, "uxQ", "irm", O64),
    LX("pinsrb", P66, M3, 0x20, W0, "uEx", "imr", 0),
    LX("pinsrd", P66, M3, 0x22, W0, "uDx", "imr", 0),
    LX("pinsrq", P66, M3, 0x22, W1, "uQx", "imr", O64),
#define PCMPSTR(n, op) S2U(n, P66, M3, op), LX(n "q", P66, M3, op, W1, "uXx", "imr", O64)
    PCMPSTR("pcmpestrm", 0x60),
    PCMPSTR("pcmpestri", 0x61),
    S2U("pcmpistrm", P66, M3, 0x62),
    S2U("pcmpistri", P66, M3, 0x63),

    /* AES, carry-less multiplication, SHA, the Galois field. */
    S2("aesenc", P66, M2, 0xdc),
    S2("aesenclast", P66, M2, 0xdd),
    S2("aesdec", P66, M2, 0xde),
    S2("aesdeclast", P66, M2, 0xdf),
    S2("aesimc", P66, M2, 0xdb),
    S2U("aeskeygenassist", P66, M3, 0xdf),
    S2I("pclmulqdq", P66, M3, 0x44),
    S2I("sha1rnds4", P0, M3, 0xcc),
    S2("sha1nexte", P0, M2, 0xc8),
    S2("sha1msg1", P0, M2, 0xc9),
    S2("sha1msg2", P0, M2, 0xca),
    S2("sha256rnds2", P0, M2, 0xcb),
    LX("sha256rnds2", P0, M2, 0xcb, W0, "0Xx", "-mr", 0),
    S2("sha256msg1", P0, M2, 0xcc),
    S2("sha256msg2", P0, M2, 0xcd),
    S2("gf2p8mulb", P66, M2, 0xcf),
    S2U("gf2p8affineqb", P66, M3, 0xce),
    S2U("gf2p8affineinvqb", P66, M3, 0xcf),

    /* A move with the bytes turned about: from memory or to it, and
     * never between registers. */
#define MOVBE(n, o16, o32, o64, s16, s32, s64) \
    LX(n, P0, M2, 0xf0, W0, o16, "mr", OP16), LX(n, P0, M2, 0xf0, W0, o32, "mr", OP32), \
        LX(n, P0, M2, 0xf0, W1, o64, "mr", O64), LX(n, P0, M2, 0xf1, W0, s16, "rm", OP16), \
        LX(n, P0, M2, 0xf1, W0, s32, "rm", OP32), LX(n, P0, M2, 0xf1, W1, s64, "rm", O64)
    MOVBE("movbe", "mw", "md", "mq", "wm", "dm", "qm"),
    LX("movbew", P0, M2, 0xf0, W0, "mw", "mr", OP16),
    LX("movbew", P0, M2, 0xf1, W0, "wm", "rm", OP16),
    LX("movbel", P0, M2, 0xf0, W0, "md", "mr", OP32),
    LX("movbel", P0, M2, 0xf1, W0, "dm", "rm", OP32),
    LX("movbeq", P0, M2, 0xf0, W1, "mq", "mr", O64),
    LX("movbeq", P0, M2, 0xf1, W1, "qm", "rm", O64),
    LX("movnti", P0, M1, 0xc3, W0, "dm", "rm", 0),
    LX("movnti", P0, M1, 0xc3, W1, "qm", "rm", O64),
    LX("movntil", P0, M1, 0xc3, W0, "dm", "rm", 0),
    LX("movntiq", P0, M1, 0xc3, W1, "qm", "rm", O64),
    LFIX("xgetbv", 0x01, 0xd0, "", ""),
    LFIX("xsetbv", 0x01, 0xd1, "", ""),
    LX("xrstors", P0, M1, 0xc7, W0, "m", "m", DG(3)),
    LX("xrstors64", P0, M1, 0xc7, W1, "m", "m", O64 | DG(3)),

    BITCOUNT("popcnt", 0xb8),
    BITCOUNT("lzcnt", 0xbd),
    BITCOUNT("tzcnt", 0xbc),

    /*
     * The instructions of the system, of virtual machines, of the
     * shadow stack and their like: an opcode and no more, or with one
     * register or one address.  The 32-bit emitter had most of them and
     * the 64-bit one almost none -- `rdrand %eax`, `clflushopt (%rax)`,
     * `xtest` and forty more were "unsupported mnemonic" in 64-bit
     * code -- and neither had those of AMD's virtual machines or of
     * the shadow stack.
     */
#define LFIXP(n, pp, op, b, ops, roles) {n, AS_VEC_LEGACY, pp, M1, op, W0, 0, ops, roles, b, 0, 0}
#define X32 AS_VEC_F_X32
    /* With a byte that is a number after the byte that is the opcode's. */
    {"xabort", AS_VEC_LEGACY, P0, AS_VEC_MAP_ONE, 0xc6, W0, 0, "u", "i", 0xf8, 0, 0},
    {"hreset", AS_VEC_LEGACY, PF3, M3, 0xf0, W0, 0, "u", "i", 0xc0, 0, 0},
    LFIX("femms", 0x0e, 0, "", ""),
    LFIX("getsec", 0x37, 0, "", ""),
    LFIXP("wbnoinvd", PF3, 0x09, 0, "", ""),
    LFIX("serialize", 0x01, 0xe8, "", ""),
    LFIX("xtest", 0x01, 0xd6, "", ""),
    LFIX("xend", 0x01, 0xd5, "", ""),
    LFIX("rdpkru", 0x01, 0xee, "", ""),
    LFIX("wrpkru", 0x01, 0xef, "", ""),
    LFIX("pconfig", 0x01, 0xc5, "", ""),
    LFIX("encls", 0x01, 0xcf, "", ""),
    LFIX("enclu", 0x01, 0xd7, "", ""),
    LFIX("enclv", 0x01, 0xc0, "", ""),
    LFIX("vmmcall", 0x01, 0xd9, "", ""),
    LFIX("stgi", 0x01, 0xdc, "", ""),
    LFIX("clgi", 0x01, 0xdd, "", ""),
    LFIXP("saveprevssp", PF3, 0x01, 0xea, "", ""),
    LFIXP("setssbsy", PF3, 0x01, 0xe8, "", ""),
    LFIXP("xresldtrk", PF2, 0x01, 0xe9, "", ""),
    LFIXP("xsusldtrk", PF2, 0x01, 0xe8, "", ""),
    {"uiret", AS_VEC_LEGACY, PF3, M1, 0x01, W0, 0, "", "", 0xec, 0, O64},
    {"testui", AS_VEC_LEGACY, PF3, M1, 0x01, W0, 0, "", "", 0xed, 0, O64},
    {"clui", AS_VEC_LEGACY, PF3, M1, 0x01, W0, 0, "", "", 0xee, 0, O64},
    {"stui", AS_VEC_LEGACY, PF3, M1, 0x01, W0, 0, "", "", 0xef, 0, O64},
    /* With the registers they are told things in written, which are
     * no part of the instruction: the first of each holds an address. */
    LFIX("clzero", 0x01, 0xfc, "", ""),
    LFIX("clzero", 0x01, 0xfc, "A", "-"),
    LFIX("monitorx", 0x01, 0xfa, "", ""),
    LFIX("monitorx", 0x01, 0xfa, "Ace", "---"),
    LFIX("mwaitx", 0x01, 0xfb, "", ""),
    LFIX("mwaitx", 0x01, 0xfb, "acb", "---"),
    LFIX("skinit", 0x01, 0xde, "", ""),
    LFIX("skinit", 0x01, 0xde, "a", "-"),
    LFIX("invlpga", 0x01, 0xdf, "", ""),
    LFIX("invlpga", 0x01, 0xdf, "Ac", "--"),
    LFIX("vmrun", 0x01, 0xd8, "", ""),
    LFIX("vmrun", 0x01, 0xd8, "A", "-"),
    LFIX("vmload", 0x01, 0xda, "", ""),
    LFIX("vmload", 0x01, 0xda, "A", "-"),
    LFIX("vmsave", 0x01, 0xdb, "", ""),
    LFIX("vmsave", 0x01, 0xdb, "A", "-"),
    /* A random number, to a register of any width. */
    LX("rdrand", P0, M1, 0xc7, W0, "w", "m", OP16 | DG(6)),
    LX("rdrand", P0, M1, 0xc7, W0, "d", "m", OP32 | DG(6)),
    LX("rdrand", P0, M1, 0xc7, W1, "q", "m", O64 | DG(6)),
    LX("rdseed", P0, M1, 0xc7, W0, "w", "m", OP16 | DG(7)),
    LX("rdseed", P0, M1, 0xc7, W0, "d", "m", OP32 | DG(7)),
    LX("rdseed", P0, M1, 0xc7, W1, "q", "m", O64 | DG(7)),
    /* The processor's number: to a register of the mode's own width. */
    LX("rdpid", PF3, M1, 0xc7, W0, "q", "m", O64 | DG(7)),
    LX("rdpid", PF3, M1, 0xc7, W0, "d", "m", X32 | DG(7)),
    LX("senduipi", PF3, M1, 0xc7, W0, "q", "m", O64 | DG(6)),
    /* One address. */
    LX("vmclear", P66, M1, 0xc7, W0, "m", "m", DG(6)),
    LX("vmptrld", P0, M1, 0xc7, W0, "m", "m", DG(6)),
    LX("vmxon", PF3, M1, 0xc7, W0, "m", "m", DG(6)),
    LX("vmptrst", P0, M1, 0xc7, W0, "m", "m", DG(7)),
    LX("clflushopt", P66, M1, 0xae, W0, "m", "m", DG(7)),
    LX("clwb", P66, M1, 0xae, W0, "m", "m", DG(6)),
    LX("clrssbsy", PF3, M1, 0xae, W0, "m", "m", DG(6)),
    LX("rstorssp", PF3, M1, 0x01, W0, "m", "m", DG(5)),
    LX("cldemote", P0, M1, 0x1c, W0, "m", "m", DG(0)),
    /* A value for the trace: 32 bits or 64, by the register or by the
     * letter, and from memory 32 unless the letter says. */
    LX("ptwrite", PF3, M1, 0xae, W0, "D", "m", DG(4)),
    LX("ptwrite", PF3, M1, 0xae, W1, "q", "m", O64 | DG(4)),
    LX("ptwritel", PF3, M1, 0xae, W0, "D", "m", DG(4)),
    LX("ptwriteq", PF3, M1, 0xae, W1, "Q", "m", O64 | DG(4)),
    /* The shadow stack. */
    LX("incsspd", PF3, M1, 0xae, W0, "d", "m", DG(5)),
    LX("incsspq", PF3, M1, 0xae, W1, "q", "m", O64 | DG(5)),
    LX("rdsspd", PF3, M1, 0x1e, W0, "d", "m", DG(1)),
    LX("rdsspq", PF3, M1, 0x1e, W1, "q", "m", O64 | DG(1)),
    LX("wrssd", P0, M2, 0xf6, W0, "dm", "rm", 0),
    LX("wrssq", P0, M2, 0xf6, W1, "qm", "rm", O64),
    LX("wrussd", P66, M2, 0xf5, W0, "dm", "rm", 0),
    LX("wrussq", P66, M2, 0xf5, W1, "qm", "rm", O64),
    /* From 16 bytes of memory, with a register of the mode's width. */
    LX("invept", P66, M2, 0x80, W0, "mq", "mr", O64),
    LX("invept", P66, M2, 0x80, W0, "md", "mr", X32),
    LX("invvpid", P66, M2, 0x81, W0, "mq", "mr", O64),
    LX("invvpid", P66, M2, 0x81, W0, "md", "mr", X32),
    LX("invpcid", P66, M2, 0x82, W0, "mq", "mr", O64),
    LX("invpcid", P66, M2, 0x82, W0, "md", "mr", X32),
    /* A store that goes round the cache: a register, or 64 bytes whose
     * destination is an address in a register of the address's width. */
    LX("movdiri", P0, M2, 0xf9, W0, "dm", "rm", 0),
    LX("movdiri", P0, M2, 0xf9, W1, "qm", "rm", O64),
    LX("movdir64b", P66, M2, 0xf8, W0, "mq", "mr", O64),
    LX("movdir64b", P66, M2, 0xf8, W0, "md", "mr", X32),
    LX("movdir64b", P66, M2, 0xf8, W0, "mw", "mr", X32),

    /* And the rows of the instruction sets that came after, which are
     * many and are in a file of their own. */
#include "as_x86_vec_table_ext.h"
#include "as_x86_vec_table_apx.h"
    /* And the forms the instructions of general registers always had,
     * as they come here: after APX's, which are of other operands. */
#include "as_x86_vec_table_base.h"
};

const size_t as_x86_vec_table_count = sizeof(as_x86_vec_table) / sizeof(as_x86_vec_table[0]);
