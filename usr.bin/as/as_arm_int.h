#ifndef SUBSTRATE_AS_ARM_INT_H
#define SUBSTRATE_AS_ARM_INT_H

/* What the encoders of ARM and AArch64 share in reading an operand's
 * text.  They are defined in as_arm.c. */

#include "as_arm.h"

#include <stddef.h>
#include <stdint.h>

/*
 * The parts of the encoders that are files of their own.  Each is
 * handed the instruction with its mnemonic taken apart -- for ARM its
 * own name, whether it sets the flags and its condition; for AArch64
 * the name in lower case -- and answers as as_arm_encode() does.
 */
/* VFP and NEON (as_arm_simd.c). */
int as_arm_vfp_neon(const as_arm_req_t *rq, const char *base, unsigned cond, as_arm_out_t *out, char *errbuf,
                    size_t errbuf_sz);
/* Thumb (as_arm_thumb.c). */
int as_arm_thumb(const as_arm_req_t *rq, const char *base, int sets_flags, unsigned cond, as_arm_out_t *out,
                 char *errbuf, size_t errbuf_sz);
/* The floating-point and vector registers of AArch64 (as_a64_simd.c). */
int as_a64_simd(const as_arm_req_t *rq, const char *m, as_arm_out_t *out, char *errbuf, size_t errbuf_sz);

/* An operand of AArch64 that is a register. */
typedef struct {
    unsigned n;
    unsigned bits;  /* 32 or 64 for a general one; 8 to 128 for a scalar of the vector registers */
    int is_sp;
    int is_zr;
    int fp;         /* a scalar: b, h, s, d or q */
    int vec;        /* a vector with its arrangement, v0.16b, or an element of one */
    unsigned q;     /* a vector: 1 if 128 bits of it */
    unsigned esize; /* a vector: 0 bytes, 1 halves, 2 words, 3 doubles */
    int index;      /* a vector's element, v0.s[1]: its number, or -1 */
} as_a64_reg_t;

/* 0 with the register, or -1 for text that is none. */
int as_a64_parse_reg(const char *s, as_a64_reg_t *r);
/* A condition of AArch64 by its name: 0 with its four bits. */
int as_a64_cond(const char *s, unsigned *c);

/* A number as ARM's data-processing instructions hold one, eight bits
 * turned right by an even count: 0 with the twelve bits. */
int as_arm_modified_imm(uint32_t value, uint32_t *enc);
/* A core register of ARM by its name: its number, or -1. */
int as_arm_core_reg(const char *s);

/* The text without the blanks around it, in lower case: into buf,
 * which is returned. */
char *as_arm_text(const char *s, char *buf, size_t buf_sz);

/* A number, with a `#` before it or bare: 0, or -1 if it is none that
 * can be worked out here. */
int as_arm_number(const char *s, long long *out);

/* `name`, `name+n` or `name-n`: 0 with the name and the number. */
int as_arm_symbol(const char *s, char *name, size_t name_sz, long long *addend);

#endif
