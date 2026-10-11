#ifndef SUBSTRATE_AS_X86_VEC_H
#define SUBSTRATE_AS_X86_VEC_H

/*
 * The instructions with a VEX or an EVEX prefix: one table of them and
 * one emitter.
 *
 * A row of the table is one form of one instruction -- its name, the
 * kinds of its operands and where each goes in the encoding, the prefix
 * and what is in it, the opcode -- and the emitter writes any row.  An
 * instruction is found by its name, the rows of that name are tried in
 * the order they are written, and the first whose operands are the
 * statement's is the encoding.  The rows of a name with a VEX prefix
 * stand before those with EVEX, so that what AVX can say is not said
 * in AVX-512.
 */

#include "as_x86_encode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { AS_VEC_VEX = 1, AS_VEC_EVEX = 2 };
enum { AS_VEC_PP_NONE = 0, AS_VEC_PP_66 = 1, AS_VEC_PP_F3 = 2, AS_VEC_PP_F2 = 3 };
enum { AS_VEC_MAP_0F = 1, AS_VEC_MAP_0F38 = 2, AS_VEC_MAP_0F3A = 3, AS_VEC_MAP_5 = 5, AS_VEC_MAP_6 = 6 };
/* W: 0, 1, or ignored by the processor (and written 0). */
enum { AS_VEC_W0 = 0, AS_VEC_W1 = 1, AS_VEC_WIG = 2 };
/* The vector length: 128, 256 or 512 bits, or ignored (written 0). */
enum { AS_VEC_L128 = 0, AS_VEC_L256 = 1, AS_VEC_L512 = 2, AS_VEC_LIG = 3 };

/*
 * What a row allows besides its operands.  The low three bits are the
 * opcode extension that goes in ModRM.reg where no operand does.
 */
#define AS_VEC_DIGIT(n) ((unsigned)(n) & 7u)
#define AS_VEC_F_MASK (1u << 3)   /* a write mask, {%k1} */
#define AS_VEC_F_ZERO (1u << 4)   /* and {z} with it */
#define AS_VEC_F_ER (1u << 5)     /* embedded rounding, {rn-sae} and the others */
#define AS_VEC_F_SAE (1u << 6)    /* {sae} */
#define AS_VEC_F_SWAP (1u << 7)   /* registers alone: left to the row after, if that is the shorter */
#define AS_VEC_F_BCST (1u << 8)   /* memory only as a broadcast: its size is not said otherwise */
#define AS_VEC_F_MASKREQ (1u << 9) /* a write mask must be given, and not %k0 */
#define AS_VEC_F_64 (1u << 10)    /* in 64-bit code only */
#define AS_VEC_F_VEX3 (1u << 11)  /* never the two-byte prefix */
#define AS_VEC_F_NO64 (1u << 12)  /* W is 0 outside 64-bit code, whatever the row says */

typedef struct {
    const char *name;
    unsigned char enc;    /* AS_VEC_VEX or AS_VEC_EVEX */
    unsigned char pp;
    unsigned char map;
    unsigned char opcode;
    unsigned char w;
    unsigned char len;
    /*
     * The operands as they are written in AT&T order, sources first,
     * one character each:
     *
     *   x y z   a register %xmm, %ymm, %zmm
     *   X Y Z   that register, or memory
     *   k K     a mask register; that, or memory
     *   d q     a general register of 32, of 64 bits
     *   D Q     that register, or memory
     *   R E     a general register of 32 or of 64 bits, either; that, or memory
     *   m       memory
     *   1 2 3   memory whose index is an %xmm, a %ymm, a %zmm register
     *   i       a number of one byte
     */
    const char *ops;
    /*
     * Where each goes: r ModRM.reg, m ModRM.rm, v the prefix's vvvv,
     * i a byte after the instruction, 4 the high four bits of that
     * byte.
     */
    const char *roles;
    unsigned char disp8_n; /* EVEX: what a one-byte displacement is multiplied by */
    unsigned char bcst;    /* EVEX: an element's bytes if memory may be broadcast, else 0 */
    unsigned short flags;
} as_x86_vec_tmpl_t;

extern const as_x86_vec_tmpl_t as_x86_vec_table[];
extern const size_t as_x86_vec_table_count;

typedef struct {
    const char *mnemonic;        /* in lower case */
    const as_x86_operand_t *ops; /* in AT&T order */
    size_t op_count;
    unsigned opmask;
    int have_opmask; /* {%kN} was written, be N 0 or not */
    int zeroing;
    unsigned broadcast; /* N of {1toN}, or 0 */
    int sae;
    int rounding; /* -1, or 0 to 3 for rn, rd, ru, rz */
    unsigned pseudo; /* AS_PSEUDO_* of as_parser.h */
    unsigned mode;   /* 16, 32 or 64 */
    unsigned level;  /* the x86-64 level allowed: under 3 no VEX, under 4 no EVEX */
} as_x86_vec_req_t;

/* 1 if `mnemonic` is the name of a row. */
int as_x86_vec_knows(const char *mnemonic);

/*
 * 1 with the instruction in out[0 .. *out_len), 0 if the mnemonic is
 * no row's, -1 with the reason in errbuf if it is and the statement is
 * not one of its forms.
 */
int as_x86_vec_encode(const as_x86_vec_req_t *rq, uint8_t *out, size_t out_cap, size_t *out_len, char *errbuf,
                      size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
