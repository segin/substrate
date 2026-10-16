#ifndef CC_DECIMAL_H
#define CC_DECIMAL_H

/*
 * Decimal floating constants (C23 _Decimal32/64/128) in the encoding gcc
 * uses on x86, BID (binary integer decimal, IEEE 754-2008): a decimal
 * number -- a sign, a coefficient's digits and an exponent -- read from a
 * constant's text, from an integer, or from a binary floating constant the
 * way gcc converts one, and encoded at a width with its rounding.
 */

#include <stdint.h>
#include <stddef.h>

/** The most coefficient digits a cc_dec_t holds before rounding (an exact binary double needs ~770). */
#define CC_DEC_MAX_DIGITS 1200

/** A decimal number: (-1)^sign x coefficient x 10^exponent, or an infinity. */
typedef struct {
    int sign;                         /**< 1 for a negative number (or -0) */
    int is_inf;                       /**< an infinity */
    int ndigits;                      /**< the coefficient's digits in digits[] (no leading zeros; 0 for zero) */
    long exponent;                    /**< the power of ten the coefficient is scaled by */
    char digits[CC_DEC_MAX_DIGITS];   /**< the coefficient's decimal digits, most significant first, 0-9 */
} cc_dec_t;

int cc_dec_from_string(const char *s, size_t len, cc_dec_t *out);
int cc_dec_from_int(unsigned long long magnitude, int negative, cc_dec_t *out);
int cc_dec_from_binary(double v, cc_dec_t *out);
int cc_dec_from_bid(int bits, const uint64_t bid[2], cc_dec_t *out);
void cc_dec_encode_bid(const cc_dec_t *d, int bits, uint64_t out[2]);

#endif
