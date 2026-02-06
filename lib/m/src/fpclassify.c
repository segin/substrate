/*
 * fpclassify.c - IEEE 754 floating-point classification functions
 *
 * Provides __fpclassify*, __signbit*, __isnan*, __isinf* for math.h macros.
 */

#include <math.h>
#include <stdint.h>

/*
 * IEEE 754 bit layouts:
 *   float:  1 sign, 8 exponent, 23 mantissa
 *   double: 1 sign, 11 exponent, 52 mantissa
 */

/* Float classification */
int __fpclassifyf(float x) {
    union { float f; uint32_t u; } u = { .f = x };
    uint32_t exp = (u.u >> 23) & 0xFF;
    uint32_t mant = u.u & 0x7FFFFF;
    
    if (exp == 0) {
        return (mant == 0) ? FP_ZERO : FP_SUBNORMAL;
    } else if (exp == 0xFF) {
        return (mant == 0) ? FP_INFINITE : FP_NAN;
    }
    return FP_NORMAL;
}

/* Double classification */
int __fpclassify(double x) {
    union { double d; uint64_t u; } u = { .d = x };
    uint64_t exp = (u.u >> 52) & 0x7FF;
    uint64_t mant = u.u & 0xFFFFFFFFFFFFFULL;
    
    if (exp == 0) {
        return (mant == 0) ? FP_ZERO : FP_SUBNORMAL;
    } else if (exp == 0x7FF) {
        return (mant == 0) ? FP_INFINITE : FP_NAN;
    }
    return FP_NORMAL;
}

/* Long double classification (same as double on i386) */
int __fpclassifyl(long double x) {
    return __fpclassify((double)x);
}

/* Sign bit extraction */
int __signbitf(float x) {
    union { float f; uint32_t u; } u = { .f = x };
    return (u.u >> 31) & 1;
}

int __signbit(double x) {
    union { double d; uint64_t u; } u = { .d = x };
    return (u.u >> 63) & 1;
}

int __signbitl(long double x) {
    return __signbit((double)x);
}

/* NaN detection */
int __isnanf(float x) {
    return __fpclassifyf(x) == FP_NAN;
}

int __isnan(double x) {
    return __fpclassify(x) == FP_NAN;
}

int __isnanl(long double x) {
    return __fpclassifyl(x) == FP_NAN;
}

/* Infinity detection */
int __isinff(float x) {
    return __fpclassifyf(x) == FP_INFINITE;
}

int __isinf(double x) {
    return __fpclassify(x) == FP_INFINITE;
}

int __isinfl(long double x) {
    return __fpclassifyl(x) == FP_INFINITE;
}
