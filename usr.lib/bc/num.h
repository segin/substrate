/*
 * libbc - Bignum Library for bc/dc
 *
 * num.h: Core data structures and prototypes.
 */

#ifndef _LIBBC_NUM_H
#define _LIBBC_NUM_H

// Digits are base 100 (0-99).
// Storage: Least significant digit at index 0.
// For integer 1234: [34, 12] (len=2, scale=0) -> 12 * 100^1 + 34 * 100^0
// For 12.3456: [56, 34, 12] (len=3, scale=4)
// Scale is the number of decimal digits after the decimal point.
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
long long bc_num_to_long(bc_num *n);
bc_num *bc_from_string(const char *s, int ibase);
void bc_print(bc_num *n); // Print base 10
void bc_print_base(bc_num *n, int obase);

// Utils
void bc_expsize(bc_num *n, int needed);
void bc_trim(bc_num *n);
bc_num *bc_align_scale(bc_num *a, bc_num *b, bc_num **a_aligned, bc_num **b_aligned); // Returns max_scale
bc_num *bc_truncate(bc_num *n);

// Configuration Variables (used by bc/dc)
extern int bc_scale;
extern int bc_ibase;
extern int bc_obase;

// Arithmetic (honor POSIX scale rules)
bc_num *bc_add(bc_num *a, bc_num *b);
bc_num *bc_sub(bc_num *a, bc_num *b);
bc_num *bc_mul(bc_num *a, bc_num *b);
bc_num *bc_div(bc_num *a, bc_num *b);
bc_num *bc_mod(bc_num *a, bc_num *b);
bc_num *bc_pow(bc_num *a, bc_num *b);
bc_num *bc_sqrt(bc_num *a);

// Math Library (-l)
bc_num *bc_math_s(bc_num *a); // sine
bc_num *bc_math_c(bc_num *a); // cosine
bc_num *bc_math_a(bc_num *a); // arctangent
bc_num *bc_math_l(bc_num *a); // natural log
bc_num *bc_math_e(bc_num *a); // exponential
bc_num *bc_math_j(bc_num *n, bc_num *x); // bessel

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

// Low-level ops
void bc_raw_add(bc_num *r, bc_num *a, bc_num *b);
void bc_raw_sub(bc_num *r, bc_num *a, bc_num *b);

// Error handling
void bc_error(const char *fmt, ...);
void bc_warn(const char *fmt, ...);

#endif
