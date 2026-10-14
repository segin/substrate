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

/* The prefix of a row: VEX, EVEX, or neither -- the instructions of
 * MMX and SSE, which the same emitter writes. */
enum { AS_VEC_VEX = 1, AS_VEC_EVEX = 2, AS_VEC_LEGACY = 3, AS_VEC_XOP = 4 };
enum { AS_VEC_PP_NONE = 0, AS_VEC_PP_66 = 1, AS_VEC_PP_F3 = 2, AS_VEC_PP_F2 = 3 };
/* (AS_VEC_MAP_ONE: an opcode of one byte, which only a row without a
 * VEX or an EVEX prefix can have.) */
enum {
    AS_VEC_MAP_ONE = 0,
    AS_VEC_MAP_0F = 1,
    AS_VEC_MAP_0F38 = 2,
    AS_VEC_MAP_0F3A = 3,
    AS_VEC_MAP_4 = 4, /* EVEX of APX: the instructions that had no prefix of this kind */
    AS_VEC_MAP_5 = 5,
    AS_VEC_MAP_6 = 6,
    AS_VEC_MAP_7 = 7,
    /* XOP's own: the prefix is 8F, and is always of three bytes. */
    AS_VEC_MAP_8 = 8,
    AS_VEC_MAP_9 = 9,
    AS_VEC_MAP_10 = 10
};
/* W: 0, 1, or ignored by the processor (and written 0). */
enum { AS_VEC_W0 = 0, AS_VEC_W1 = 1, AS_VEC_WIG = 2 };
/* The vector length: 128, 256 or 512 bits, or ignored (written 0). */
enum { AS_VEC_L128 = 0, AS_VEC_L256 = 1, AS_VEC_L512 = 2, AS_VEC_LIG = 3 };
/* (A row with no VEX has no vector length, and its `len` says one
 * thing instead: that the number after the opcode is of eight bytes.) */
#define AS_VEC_LEGACY_IMM64 1
/* (Nor has it a broadcast, and its `bcst` says another: that the rows
 * of AS_VEC_F_BASE of its name are all the forms the name has, so that
 * what none of them is of is no instruction, and is not looked for in
 * the encoders the rows came from.) */
#define AS_VEC_LEGACY_WHOLE 1

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
#define AS_VEC_F_OP16 (1u << 13)  /* no VEX: an operand of 16 bits, with the prefix that says so */
#define AS_VEC_F_OP32 (1u << 14)  /* no VEX: one of 32 bits, which in 16-bit code has the prefix */
#define AS_VEC_F_WEAK (1u << 15)  /* the name is also another instruction's: without a vector register, not this */
#define AS_VEC_F_X32 (1u << 16)   /* in 16- and 32-bit code only */
#define AS_VEC_F_MODRM (1u << 17) /* VEX: a ModRM though no operand is in r/m, which is register 0 */
#define AS_VEC_F_IMM32 (1u << 18) /* the number after it is of four bytes */
#define AS_VEC_F_SIB (1u << 19)   /* memory always with a SIB byte: AMX, whose index is the stride */
#define AS_VEC_F_SUFFIX (1u << 20) /* no VEX: the row's byte goes after the operands (3DNow!) */
#define AS_VEC_F_LOCK (1u << 21)   /* no VEX: may have a lock prefix (said by the name's first row) */
#define AS_VEC_F_NORIP (1u << 22)  /* memory that is not an address from %rip (MPX) */
#define AS_VEC_F_ND (1u << 23)     /* EVEX of APX: a new destination, in vvvv, and the bit that says so */
#define AS_VEC_F_IMM16 (1u << 24)  /* the number after it is of two bytes */
#define AS_VEC_F_APX (1u << 25)    /* the name is an instruction assembled elsewhere, and these its forms in APX */
#define AS_VEC_F_NF (1u << 26)     /* EVEX of APX: no flags are written, and the bit that says so */
#define AS_VEC_F_SCC (1u << 27)    /* EVEX of APX: a condition in the prefix, which is the row's `bcst` */
#define AS_VEC_F_REGRM (1u << 28)  /* the register in r/m is in ModRM.reg too: a product into what is multiplied */
#define AS_VEC_F_PROMO (1u << 29)  /* APX's EVEX form of an instruction with another: only where {evex} or {nf} asks */
#define AS_VEC_F_PROMOHI (1u << 30) /* and one that %r16 to %r31 need too, the instruction having no REX2 form */
/*
 * A form an instruction has always had, of a name that is still
 * assembled elsewhere for the forms that have no row yet: the table is
 * asked first, in every mode, and what none of its rows is of is left
 * to where it always went.  (The instructions of general registers are
 * coming here from the encoders that compare names, a family at a time.)
 */
#define AS_VEC_F_BASE (1u << 31)

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
     *   i u     a number of one byte: -128 to 255; 0 to 255 only
     *   l       %cl and no other: a count
     *   o       the number 1 and no other: a count that is in the opcode
     *   s       a number of one byte that is extended by its sign:
     *           -128 to 127, and known here
     *   S T     one that is such a byte once cut to 16, to 32 bits
     *   f g j   a general register of 16, 32, 64 bits that is not
     *           %ax, %eax, %rax
     *   H v     a general register of 8 bits, any of them; one that
     *           is not %al
     *   V F G J %al, %ax, %eax, %rax and no other
     *   U I     a register of the x87's stack; its top, %st, alone
     *   C       %dx as a port, written `%dx` or `(%dx)`
     *   5 6 7   a control register, a debug register, a segment register
     *   O P     memory at an address that is a number alone; and one
     *           that is not a symbol's place in the global offset table
     *   n N     an MMX register; that, or memory
     *   t       a tile register, %tmm0 to %tmm7
     *   B       a bound register, %bnd0 to %bnd3
     *   h       a general register of 8 bits: %al to %bl, %r8b to %r15b
     *   w W     a general register of 16 bits; that, or memory
     *   0       %xmm0 and no other
     *   a c e b %eax, %ecx, %edx, %ebx and no other, or in 64-bit code
     *           %rax, %rcx, %rdx, %rbx
     *   A       %rax, %eax or %ax, holding an address: the one that is
     *           not the mode's own has the address-size prefix
     */
    const char *ops;
    /*
     * Where each goes: r ModRM.reg, m ModRM.rm, v the prefix's vvvv,
     * i a byte after the instruction, 4 the high four bits of that
     * byte, - nowhere (the instruction implies it), + the opcode
     * itself, whose low three bits are the register's (`push %rbx`),
     * o after the opcode, an address with no ModRM (`mov sym, %eax`
     * in 32-bit code: class O).
     */
    const char *roles;
    /* EVEX: what a one-byte displacement is multiplied by.  (A row
     * with no VEX and no operand in ModRM: the byte after its opcode,
     * if it has one.) */
    unsigned char disp8_n;
    unsigned char bcst;    /* EVEX: an element's bytes if memory may be broadcast, else 0 */
    unsigned flags;
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
    /* `{dfv=}` was written, and its flags (cf 1, zf 2, sf 4, of 8). */
    unsigned char have_dfv;
    unsigned char dfv;
    unsigned mode;   /* 16, 32 or 64 */
    unsigned level;  /* the x86-64 level allowed: under 3 no VEX, under 4 no EVEX */
    /* A REX prefix was written, `rex.W` or `{rex}`: its bits, which
     * may be none.  Only an instruction with no VEX has one. */
    int explicit_rex;
    unsigned rex_bits;
    /* Set by the emitter for itself: an operand is %ah, %ch, %dh or
     * %bh, which no instruction with a REX prefix can name. */
    int no_rex;
    /* The form with ModRM, and not the accumulator's with an address
     * after the opcode: `xrelease mov %eax, x` is F3 89 05. */
    int no_moffs;
    /* data16 was written: the operand is of 16 bits where the code's
     * is not, which a return's count is cut by. */
    int data16;
    /*
     * A branch to a place.  The operand's number is the distance where
     * `rel_is_disp` says so; where the instruction's own offset is
     * known it is the place, and the distance is from the end of the
     * instruction; otherwise it is what a relocation is added to.
     * `force_near` asks for the form with the full displacement
     * though a byte would do.  `resolved` says the place is a label
     * whose distance is final: no relocation follows, and what the
     * bytes cannot hold is refused as GNU as refuses it.
     */
    int rel_is_disp;
    int has_offset;
    uint64_t offset;
    int force_near;
    int resolved;
    /*
     * The prefixes the statement has besides the instruction's own:
     * written, or asked for by its operands.  The emitter writes them
     * with its own in the order GNU as has: the segment, 67, 66, the
     * F2 or F3 of xacquire or xrelease, F0, the F2 or F3 of a repeat,
     * and then the instruction's.  0 is none of each.
     */
    unsigned char pfx_seg;  /* 26, 2E, 36, 3E, 64 or 65 */
    unsigned char pfx_67;   /* the address size, where nothing else says it */
    unsigned char pfx_66;   /* data16 */
    unsigned char pfx_hle;  /* F2 or F3 */
    unsigned char pfx_lock; /* F0 */
    unsigned char pfx_rep;  /* F2 or F3 */
    /* Where the emitter says which of the bytes it wrote hold an
     * operand's expression, or NULL. */
    as_x86_fields_t *fields;
} as_x86_vec_req_t;

/* 1 if `mnemonic` is the name of a row. */
int as_x86_vec_knows(const char *mnemonic);

/* Whether the table has APX's forms of an instruction that is
 * otherwise assembled elsewhere (`add` with three operands).  Such a
 * name is not one as_x86_vec_knows() says yes to; as_x86_vec_encode()
 * gives 0 for it where no row is of the operands.  `op_count` is how
 * many operands the statement has: 0 if no such form has as many.
 * `asked` is whether the statement has `{evex}` or `{nf}` before it,
 * and `high` whether it has one of %r16 to %r31: the forms that are
 * only for those are not counted otherwise. */
int as_x86_vec_has_apx(const char *mnemonic, size_t op_count, int asked, int high);

/* Has the name a row of AS_VEC_F_BASE with this many operands? */
int as_x86_vec_has_base(const char *mnemonic, size_t op_count);

/* Are the name's rows of AS_VEC_F_BASE all the forms it has
 * (AS_VEC_LEGACY_WHOLE)? */
int as_x86_vec_base_is_whole(const char *mnemonic);

/* Whether the table has any row of the name (in lower case), APX's or
 * not: it is the name of something. */
int as_x86_vec_has_rows(const char *mnemonic);

/*
 * 1 with the instruction in out[0 .. *out_len), 0 if the mnemonic is
 * no row's, -1 with the reason in errbuf if it is and the statement is
 * not one of its forms.
 */
int as_x86_vec_encode(const as_x86_vec_req_t *rq, uint8_t *out, size_t out_cap, size_t *out_len, char *errbuf,
                      size_t errbuf_sz);

/*
 * Whether `data16` may be written before the instruction: it may
 * before one with neither VEX nor EVEX and no MMX or vector register --
 * `popcnt`, `movbe`, `xgetbv`, `emms` -- where 66 is the operand size
 * and is put before everything else.  Before the rest 66 is part of the
 * opcode or is nothing, and GNU as refuses it.
 */
int as_x86_vec_takes_data16(const char *mnemonic);

/* 1 if the instruction may be locked (AS_VEC_F_LOCK on its first row):
 * `lock cmpxchg16b`, and `xacquire` and `xrelease` with it. */
int as_x86_vec_takes_lock(const char *mnemonic);
int as_x86_vec_base_takes_lock(const char *mnemonic);

#ifdef __cplusplus
}
#endif

#endif
