/*
 * libbc - Bignum Library for bc/dc
 *
 * num.h: Core data structures and prototypes.
 */

#ifndef _LIBBC_NUM_H
#define _LIBBC_NUM_H

// Digits are base 100 (0-99).
// Storage: Least significant digit at index 0?
// Actually, standard schoolbook fits better with LSD at 0 for growing.
// But for division, MSB first is often nicer.
// Let's stick to:
// digits[0] = LSD (10^0 / 10^-scale)
// For integer 1234: [34, 12] (len=2, scale=0) -> 12 * 100^1 + 34 * 100^0
// For 12.3456: [56, 34, 12] (len=3, scale=4??)
// Base 100 simplifies printing (2 decimal digits per byte).

typedef struct number {
    unsigned char *digits; // Base 100, LSD at [0]
    int len;               // Number of base-100 digits
    int scale;             // Number of decimal digits after decimal point (affects interpretation)
    int sign;              // 1 (positive), -1 (negative), 0 (zero)
    int cap;               // Capacity of digits array
} bc_num;

// Management
bc_num *bc_new(void);
void bc_free(bc_num *n);
bc_num *bc_dup(bc_num *src);
bc_num *bc_from_long(long long v);
bc_num *bc_from_string(const char *s);
void bc_print(bc_num *n); // Prints to stdout

// Utils
void bc_canonicalize(bc_num *n); 
void bc_expsize(bc_num *n, int needed);

// Arithmetic
bc_num *bc_add(bc_num *a, bc_num *b);
bc_num *bc_sub(bc_num *a, bc_num *b);
bc_num *bc_mul(bc_num *a, bc_num *b);
bc_num *bc_div(bc_num *a, bc_num *b);
bc_num *bc_mod(bc_num *a, bc_num *b);
bc_num *bc_pow(bc_num *a, bc_num *b);

// Comparison
// Comparison
// Returns -1 (a<b), 0 (a=b), 1 (a>b)
int bc_compare(bc_num *a, bc_num *b);
int bc_abs_cmp(bc_num *a, bc_num *b);

static inline int bc_is_neg(bc_num *n) {
    return n->sign < 0;
}

static inline int bc_is_zero(bc_num *n) {
    return n->len == 0 || n->sign == 0;
}

#endif
