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

/* Long double classification on the raw 80-bit x87 representation.
 * Casting to double first (the old implementation) overflowed any value
 * above DBL_MAX (~1.8e308) to infinity and underflowed tiny values to
 * zero, so the entire long-double-only range was mis-classified -- which
 * made isinfl()/isnanl()/finitel() wrong for large finite long doubles
 * (e.g. hypotl/expl results) and corrupted every libm long-double
 * function that early-returns on those tests. */
int __fpclassifyl(long double x) {
    /* 64-bit significand (explicit integer bit at bit 63) + a 16-bit
     * field holding the 15-bit biased exponent (bits 0-14) and sign. */
    union { long double ld; struct { uint64_t mant; uint16_t se; } p; } u = { .ld = x };
    uint16_t exp = u.p.se & 0x7FFFu;
    uint64_t mant = u.p.mant;
    if (exp == 0x7FFFu)
        return ((mant & 0x7FFFFFFFFFFFFFFFULL) == 0 && (mant >> 63)) ? FP_INFINITE : FP_NAN;
    if (exp == 0)
        return (mant == 0) ? FP_ZERO : FP_SUBNORMAL;
    return FP_NORMAL;
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
    /* Sign is bit 15 of the high 16-bit field; reading it off the raw
     * representation also gives the correct sign of -0.0L and of values
     * outside the double range (a (double) cast would lose both). */
    union { long double ld; struct { uint64_t mant; uint16_t se; } p; } u = { .ld = x };
    return (u.p.se >> 15) & 1;
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
