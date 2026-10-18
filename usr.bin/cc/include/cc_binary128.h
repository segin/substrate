#ifndef CC_BINARY128_H
#define CC_BINARY128_H

/*
 * IEEE 754 binary128 constants, the format of _Float128 and __float128 on
 * x86 (gcc's): a sign bit, a 15-bit exponent biased by 16383 and a 112-bit
 * fraction, held as two 64-bit halves, low half first.  Constants are
 * encoded here, exactly or rounded once to nearest, ties to even;
 * arithmetic on them is libgcc's (__addtf3 and kin) at run time.
 */

#include <stdint.h>
#include <stddef.h>

/**
 * Encode an integer.  Every 64-bit magnitude is exact.
 *
 * @param magnitude  its absolute value
 * @param negative   nonzero for a negative integer
 * @param out        receives the encoding, low half first
 */
void cc_b128_from_int(unsigned long long magnitude, int negative, uint64_t out[2]);

/**
 * Encode a double, exactly (infinities and NaNs included).
 *
 * @param v    the value
 * @param out  receives the encoding
 */
void cc_b128_from_double(double v, uint64_t out[2]);

/**
 * Encode a long double, exactly where it is the x87's 64-bit-significand
 * format (as on every host cc runs on), else through double.
 *
 * @param v    the value
 * @param out  receives the encoding
 */
void cc_b128_from_ld(long double v, uint64_t out[2]);

/**
 * Encode a floating constant's text -- decimal (`1.25e-3`) or hexadecimal
 * (`0x1.8p4`), any suffix ignored -- rounded once to nearest, ties to even.
 *
 * @param s    the constant's spelling
 * @param len  its length
 * @param out  receives the encoding
 * @return     0, or -1 if the text is not a floating constant
 */
int cc_b128_from_string(const char *s, size_t len, uint64_t out[2]);

/**
 * One arithmetic operation, as IEEE 754 makes it in binary128 (rounded to
 * nearest, ties to even): what gcc folds a constant expression to.
 *
 * @param op   '+', '-', '*' or '/'
 * @param a    the left operand's encoding
 * @param b    the right one's
 * @param out  receives the result's
 * @return     0, or -1 for another operator or out of memory
 */
int cc_b128_arith(int op, const uint64_t a[2], const uint64_t b[2], uint64_t out[2]);

#endif
