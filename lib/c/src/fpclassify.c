/*
 * fpclassify.c - IEEE 754 floating-point classification functions
 *
 * Provides __fpclassify*, __signbit*, __isnan*, __isinf*,
 * __issignaling*, __iseqsig* for math.h macros.
 */

#include <math.h>
#include <fenv.h>
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

/*
 * Signaling-NaN detection (C23, IEEE 754-2008 §6.2.1).
 *
 * In the recommended ("MSB-quiet") encoding used by every modern x86,
 * ARM, RISC-V, etc.:
 *   - bit pattern is NaN iff exponent = all-ones AND mantissa != 0
 *   - top bit of mantissa = 1 → quiet NaN
 *   - top bit of mantissa = 0 → signaling NaN
 * (At least one OTHER mantissa bit must be set in the sNaN case so the
 * value isn't reinterpreted as Infinity.)
 */
int __issignalingf(float x) {
    union { float f; uint32_t u; } u = { .f = x };
    uint32_t exp  = (u.u >> 23) & 0xFFu;
    uint32_t mant = u.u & 0x7FFFFFu;
    if (exp != 0xFFu || mant == 0) return 0;        /* not NaN */
    return (mant & 0x400000u) == 0;                  /* MSB-quiet → 0 = signaling */
}

int __issignaling(double x) {
    union { double d; uint64_t u; } u = { .d = x };
    uint64_t exp  = (u.u >> 52) & 0x7FFu;
    uint64_t mant = u.u & 0xFFFFFFFFFFFFFULL;
    if (exp != 0x7FFu || mant == 0) return 0;
    return (mant & 0x8000000000000ULL) == 0;
}

int __issignalingl(long double x) {
    return __issignaling((double)x);
}

/*
 * Equality with explicit FE_INVALID raise on a NaN operand (C23).
 *
 * The bare `==` on x87 is compiled to FUCOMI by GCC, which only raises
 * the invalid flag for SIGNALING NaNs — quiet NaNs compare unordered
 * silently.  iseqsig must raise for ANY NaN operand, so we explicitly
 * detect and raise.  Returns the integer-valued ordered equality result
 * (0 if either is NaN, otherwise x == y).
 */
int __iseqsigf(float x, float y) {
    if (__isnanf(x) || __isnanf(y)) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    return x == y;
}

int __iseqsig(double x, double y) {
    if (__isnan(x) || __isnan(y)) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    return x == y;
}

int __iseqsigl(long double x, long double y) {
    if (__isnanl(x) || __isnanl(y)) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    return x == y;
}
