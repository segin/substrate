/*
 * math.c - Math library functions
 *
 * Implements exponential, logarithmic, and trigonometric functions
 * using Taylor series approximations and mathematical identities.
 */

#include <errno.h>
#include <fenv.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

/* Constants (with guards to avoid redefinition) */
#ifndef M_PI
#define M_PI      3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2    1.57079632679489661923
#endif
#ifndef M_PI_4
#define M_PI_4    0.78539816339744830962
#endif
#ifndef M_E
#define M_E       2.71828182845904523536
#endif
#ifndef M_LN2
#define M_LN2     0.69314718055994530942
#endif
#ifndef M_LN10
#define M_LN10    2.30258509299404568402
#endif
#ifndef M_LOG2E
#define M_LOG2E   1.44269504088896340736
#endif

/*
 * exp(x) - e^x using Taylor series
 * e^x = 1 + x + x^2/2! + x^3/3! + ...
 */
double exp(double x) {
    if (isnan(x)) return x;
    if (x == 0.0) return 1.0;
    if (isinf(x)) return (x > 0) ? INFINITY : 0.0;

    double res;
    __asm__ __volatile__(
        "fldl2e\n\t"        /* Load log2(e) */
        "fmulp\n\t"         /* st(0) = x * log2(e) */
        "fld %%st(0)\n\t"
        "frndint\n\t"       /* st(0) = i = round(x * log2(e)) */
        "fsub %%st(0), %%st(1)\n\t" /* st(1) = f = (x * log2(e)) - i */
        "fxch\n\t"
        "f2xm1\n\t"         /* st(0) = 2^f - 1 */
        "fld1\n\t"
        "faddp\n\t"         /* st(0) = 2^f */
        "fscale\n\t"        /* st(0) = 2^f * 2^i = 2^(x * log2(e)) = e^x */
        "fstp %%st(1)\n\t"
        "fstpl %0"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

/* exp2(x) = 2^x = e^(x * ln(2)) */
double exp2(double x) {
    if (isnan(x)) return x;
    if (x == 0.0) return 1.0;
    if (isinf(x)) return (x > 0) ? INFINITY : 0.0;

    double res;
    __asm__ __volatile__(
        "fldl %1\n\t"
        "fld %%st(0)\n\t"
        "frndint\n\t"       /* i */
        "fsub %%st(0), %%st(1)\n\t" /* f */
        "fxch\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp %%st(1)\n\t"
        "fstpl %0"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

/* expm1(x) = e^x - 1, accurate for small x.
 *
 * For |x| above ~ln(2)/2 the catastrophic cancellation in exp(x)-1 is
 * negligible, so we just call exp.  Otherwise we use the identity
 *   e^x - 1 = 2 * sinh(x/2) * exp(x/2)
 * and a polynomial for sinh(z) = z + z^3/6 + z^5/120 + ... that
 * converges fast for the reduced range. */
double expm1(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return (x > 0) ? INFINITY : -1.0;
    if (fabs(x) > 0.35) return exp(x) - 1.0;

    /* sinh(z) Taylor, |z| <= 0.175 */
    double z = x * 0.5;
    double z2 = z * z;
    double sinh_z = z * (1.0 + z2 *
                         (1.0/6.0 + z2 *
                          (1.0/120.0 + z2 *
                           (1.0/5040.0 + z2 / 362880.0))));
    return 2.0 * sinh_z * exp(z);
}

/*
 * Portable natural log via argument reduction + Taylor series.
 *
 * Reduce x = m * 2^e with m in [1, 2), then
 *   log(x) = e*ln(2) + log(m) = e*ln(2) + 2*atanh((m-1)/(m+1))
 * The atanh series converges quickly because z = (m-1)/(m+1) lies in
 * [0, 1/3].  Twenty terms of (z + z^3/3 + z^5/5 + ...) deliver near
 * double precision over the reduced range.
 */
__attribute__((unused))
static double log_portable(double x) {
    union { double d; uint64_t u; } v;
    v.d = x;
    int e = (int)((v.u >> 52) & 0x7FF) - 1023;
    /* Force exponent to 0 → m in [1, 2). */
    v.u = (v.u & 0x000FFFFFFFFFFFFFULL) | 0x3FF0000000000000ULL;
    double m = v.d;
    double z = (m - 1.0) / (m + 1.0);
    double z2 = z * z;
    double sum = z;
    double term = z;
    for (int k = 1; k < 25; k++) {
        term *= z2;
        sum += term / (double)(2 * k + 1);
    }
    /* M_LN2 = ln(2) */
    return 2.0 * sum + (double)e * 0.69314718055994530942;
}

/*
 * log family — POSIX requires:
 *   log(0)   → -INF, errno = ERANGE
 *   log(<0)  → NaN,  errno = EDOM
 *   log(NaN) → NaN
 * fyl2x on x <= 0 is undefined per the i387 spec, so guard explicitly.
 */
double log(double x) {
    if (isnan(x)) return x;
    if (x < 0.0)   { errno = EDOM;   return NAN; }
    if (x == 0.0)  { errno = ERANGE; return -INFINITY; }
    if (isinf(x))  return x;
#if defined(__i386__) || defined(__x86_64__)
    double res;
    __asm__ __volatile__("fldln2; fldl %1; fyl2x; fstpl %0"
                         : "=m"(res) : "m"(x) : "st", "st(1)");
    return res;
#else
    return log_portable(x);
#endif
}

double log2(double x) {
    if (isnan(x)) return x;
    if (x < 0.0)   { errno = EDOM;   return NAN; }
    if (x == 0.0)  { errno = ERANGE; return -INFINITY; }
    if (isinf(x))  return x;
#if defined(__i386__) || defined(__x86_64__)
    double res;
    __asm__ __volatile__("fld1; fldl %1; fyl2x; fstpl %0"
                         : "=m"(res) : "m"(x) : "st", "st(1)");
    return res;
#else
    /* log2(x) = log(x) / ln(2) */
    return log_portable(x) * 1.4426950408889634074;
#endif
}

double log10(double x) {
    if (isnan(x)) return x;
    if (x < 0.0)   { errno = EDOM;   return NAN; }
    if (x == 0.0)  { errno = ERANGE; return -INFINITY; }
    if (isinf(x))  return x;
#if defined(__i386__) || defined(__x86_64__)
    double res;
    __asm__ __volatile__("fldlg2; fldl %1; fyl2x; fstpl %0"
                         : "=m"(res) : "m"(x) : "st", "st(1)");
    return res;
#else
    /* log10(x) = log(x) / ln(10) */
    return log_portable(x) * 0.43429448190325182765;
#endif
}

/* log1p(x) = log(1+x), accurate for small x.
 * Domain: x > -1.  log1p(-1) = -INF, log1p(<-1) = NaN. */
double log1p(double x) {
    if (isnan(x)) return x;
    if (x < -1.0)  { errno = EDOM;   return NAN; }
    if (x == -1.0) { errno = ERANGE; return -INFINITY; }
    if (fabs(x) < 1e-9) return x - 0.5 * x * x;
    return log(1.0 + x);
}

/* pow(x, y) = x^y = e^(y * log(x)) */
double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 1.0) return 1.0;
    if (x == 0.0) return (y > 0) ? 0.0 : INFINITY;
    if (isnan(x) || isnan(y)) return NAN;

    if (x < 0.0) {
        double yi;
        if (modf(y, &yi) != 0.0) return NAN;
        double res = pow(-x, y);
        double parity = fmod(yi, 2.0);
        if (parity == 1.0 || parity == -1.0) return -res;
        return res;
    }

    double res;
    __asm__ __volatile__(
        "fldl %2\n\t"       /* y */
        "fldl %1\n\t"       /* x */
        "fyl2x\n\t"         /* st(0) = y * log2(x) */
        "fld %%st(0)\n\t"
        "frndint\n\t"       /* i */
        "fsub %%st(0), %%st(1)\n\t" /* f */
        "fxch\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp %%st(1)\n\t"
        "fstpl %0"
        : "=m"(res) : "m"(x), "m"(y) : "ax", "cc");
    return res;
}

/* logp1(x) — alias for log1p(x) per C23. */
double logp1(double x) {
    return log1p(x);
}

/* log2p1(x) — compute log2(1+x) accurately for small x.
 * On x87, use fyl2xp1 for |x| < 1 - sqrt(2)/2 (~0.2929).
 * log2(1+x) = x/ln(2) for tiny x (Taylor). */
double log2p1(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return x;
    if (x < -1.0)  { errno = EDOM;   return NAN; }
    if (x == -1.0) { errno = ERANGE; return -INFINITY; }
    if (fabs(x) < 1e-9) return x * 1.4426950408889634074; /* /ln(2) */
#if defined(__i386__) || defined(__x86_64__)
    if (fabs(x) < 0.2929) {
        double res;
        __asm__ __volatile__(
            "fldl %1\n\t"
            "fldl %%st(0)\n\t"
            "fld1\n\t"
            "fsubp\n\t"
            "fyl2xp1\n\t"
            "fstpl %0"
            : "=m"(res) : "m"(x) : "ax", "cc");
        return res;
    }
#endif
    return log2(1.0 + x);
}

/* log10p1(x) — compute log10(1+x) accurately for small x. */
double log10p1(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return x;
    if (x < -1.0)  { errno = EDOM;   return NAN; }
    if (x == -1.0) { errno = ERANGE; return -INFINITY; }
    if (fabs(x) < 1e-9) return x * 0.43429448190325182765; /* /ln(10) */
    return log10(1.0 + x);
}

/* exp2m1(x) — compute 2^x - 1 accurately for |x| < 1.
 * On x87: direct f2xm1 for |x| < 1. */
double exp2m1(double x) {
    if (isnan(x)) return x;
    if (x == 0.0) return 0.0;
    if (isinf(x)) return (x > 0) ? INFINITY : -1.0;
    if (fabs(x) >= 1.0) {
        return exp2(x) - 1.0;
    }
#if defined(__i386__) || defined(__x86_64__)
    double res;
    __asm__ __volatile__(
        "fldl %1\n\t"
        "f2xm1\n\t"
        "fstpl %0"
        : "=m"(res) : "m"(x));
    return res;
#else
    static const double ln2 = 0.69314718055994530942;
    double t = x * ln2;
    double t2 = t * t;
    return t2 * (0.5 + t2 * (1.0/6.0 + t2 * (1.0/120.0 + t2 * (1.0/5040.0 + t2/362880.0))));
#endif
}

/* exp10(x) — compute 10^x.
 * On x87: x * log2(10) → fyl2x/fscale.  10^x = 2^(x*log2(10)). */
double exp10(double x) {
    if (isnan(x)) return x;
    if (x == 0.0) return 1.0;
    if (x >  308.0) return INFINITY;
    if (x < -323.0) return 0.0;
#if defined(__i386__) || defined(__x86_64__)
    static const double log2_10 = 3.32192809488736234787;
    double res;
    __asm__ __volatile__(
        "fldl %2\n\t"
        "fldln2\n\t"
        "fmulp %%st, %%st(1)\n\t"
        "fld %%st(0)\n\t"
        "frndint\n\t"
        "fxch\n\t"
        "fsub %%st(0), %%st(1)\n\t"
        "fxch\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp %%st(1)\n\t"
        "fstpl %0"
        : "=m"(res) : "m"(log2_10), "m"(x) : "ax", "cc");
    return res;
#else
    return exp(x * 2.30258509299404568402); /* x * ln(10) */
#endif
}

/* exp10m1(x) — compute 10^x - 1 accurately for small x. */
double exp10m1(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return (x > 0) ? INFINITY : -1.0;
    if (fabs(x) > 0.35) {
        return exp10(x) - 1.0;
    }
    static const double ln10 = 2.30258509299404568402;
    static const double ln10_2 = ln10 * ln10;
    double z = x * ln10;
    return z * (1.0 + 0.5 * z * (1.0 + z * (ln10_2 / 6.0)));
}

/* rsqrt(x) — reciprocal square root: 1/sqrt(x).
 * On x87: fsqrt then fdivr with 1.0. */
double rsqrt(double x) {
    if (isnan(x) || x < 0.0) return NAN;
    if (x == 0.0) return INFINITY;
    if (isinf(x)) return 0.0;
#if defined(__i386__) || defined(__x86_64__)
    double res;
    __asm__ __volatile__(
        "fldl %1\n\t"
        "fsqrt\n\t"
        "fld1\n\t"
        "fdivrp %%st, %%st(1)\n\t"
        "fstpl %0"
        : "=m"(res) : "m"(x) : "ax");
    return res;
#else
    return 1.0 / sqrt(x);
#endif
}

/* pown(x, n) — x raised to integer power n (intmax_t).
 * Binary exponentiation. Handles n < 0 via reciprocal. */
double pown(double x, intmax_t n) {
    if (x == 0.0) {
        if (n == 0) return 1.0;
        if (n > 0) return 0.0;
        return INFINITY;
    }
    if (isnan(x)) return NAN;
    if (isinf(x)) return (n > 0) ? INFINITY : (n < 0) ? 0.0 : 1.0;
    if (n == 0) return 1.0;
    if (n == 1) return x;
    if (x < 0.0 && n == (intmax_t)(long long)n) {
        double r = pown(-x, n);
        int q = (int)(n % 2);
        if (q == 0) return r;
        return -r;
    }
    if (x < 0.0) return NAN;
    int neg = (n < 0);
    if (neg) n = -n;
    double result = 1.0;
    double base = x;
    while (n > 0) {
        if (n & 1) result *= base;
        base *= base;
        n >>= 1;
    }
    if (neg) result = 1.0 / result;
    return result;
}

/* powr(x, y) — e^(y * ln(x)), domain x >= 0.
 * Different NaN/±0 semantics from pow() (C23 F.9.9.4.3). */
double powr(double x, double y) {
    if (isnan(x) || isnan(y)) return NAN;
    if (x == 0.0) {
        if (y > 0.0) return 0.0;
        if (y < 0.0) return INFINITY;
        return 1.0;
    }
    if (x < 0.0) { errno = EDOM; return NAN; }
    if (x == 1.0) return 1.0;
    if (isinf(y)) {
        if (x > 1.0) return (y < 0) ? 0.0 : INFINITY;
        if (x < 1.0) return (y < 0) ? INFINITY : 0.0;
        return 1.0;
    }
    if (isinf(x)) return (y > 0) ? INFINITY : 0.0;
    if (y == 0.0) return 1.0;
    return exp(y * log(x));
}

/* rootn(x, n) — n-th root of x.
 * rootn(x, 2) == sqrt(x), rootn(x, 3) == cbrt(x). */
double rootn(double x, int n) {
    if (n == 0) return NAN;
    if (isnan(x)) return NAN;
    if (x == 0.0) {
        if (n > 0) return 0.0;
        if (n < 0) return INFINITY;
    }
    if (n < 0) {
        double r = rootn(x, -n);
        return (r == 0.0) ? INFINITY : (1.0 / r);
    }
    if (x < 0.0 && (n % 2) == 0) return NAN;
    if (x < 0.0) {
        return -pow(-x, 1.0 / (double)n);
    }
    return pow(x, 1.0 / (double)n);
}

/* compound(x, n) — (1+x)^n, computed stably for small x. */
double compound(double x, intmax_t n) {
    if (isnan(x)) return NAN;
    if (x == 0.0) return 1.0;
    if (n == 0) return 1.0;
    if (n == 1) return 1.0 + x;
    if (n < 0) {
        double r = compound(x, -n);
        return (r == 0.0) ? INFINITY : (1.0 / r);
    }
    int neg = (x < 0.0 && (n % 2) != 0);
    if (neg) x = -x;
    if (fabs(x) < 1e-9) {
        double s = n * (x - 0.5 * x * x);
        return neg ? (-exp(s)) : exp(s);
    }
    double result = 1.0;
    double base = 1.0 + x;
    intmax_t nn = n;
    while (nn > 0) {
        if (nn & 1) result *= base;
        base *= base;
        nn >>= 1;
    }
    return neg ? -result : result;
}

/*
 * sqrt(x) - Square root using Newton-Raphson
 * x_{n+1} = 0.5 * (x_n + S/x_n)
 */
double sqrt(double x) {
    if (x < 0) return NAN;
    double res;
    __asm__ __volatile__("fldl %1; fsqrt; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

/*
 * cbrt(x) - Cube root.
 *
 * Argument reduction: x = m * 2^e with m in [0.5, 1).  Then
 *   cbrt(x) = cbrt(m) * 2^(e/3)
 * Split e = 3q + r with r in {0, 1, 2}:
 *   cbrt(x) = cbrt(m * 2^r) * 2^q
 * The reduced argument m*2^r lies in [0.5, 4), so a fixed-precision
 * Newton iteration converges in a handful of steps from a good seed.
 * The previous implementation used `guess = x/2` which diverges for
 * very small or very large x.
 */
double cbrt(double x) {
    if (isnan(x) || isinf(x)) return x;
    if (x == 0.0) return x;

    int neg = (x < 0);
    if (neg) x = -x;

    /* Decompose x = m * 2^e, m in [0.5, 1). */
    union { double d; uint64_t u; } v;
    v.d = x;
    int e = (int)((v.u >> 52) & 0x7FF) - 1022; /* unbiased exp s.t. m in [0.5,1) */
    v.u = (v.u & 0x000FFFFFFFFFFFFFULL) | 0x3FE0000000000000ULL;
    double m = v.d;

    /* e = 3q + r, r in {0,1,2} */
    int q;
    int r;
    if (e >= 0) {
        q = e / 3;
        r = e % 3;
    } else {
        q = -((-e + 2) / 3);
        r = e - 3 * q;
    }
    /* Pull r into the mantissa: m_r = m * 2^r ∈ [0.5, 4) */
    static const double pow2_r[3] = { 1.0, 2.0, 4.0 };
    double mr = m * pow2_r[r];

    /* Linear-interpolated seed for cbrt(mr) on [0.5, 4) — within ~5%. */
    double guess = 0.5 + 0.5 * mr; /* good enough for Newton to hit double precision in <10 iters */

    /* Newton: g_{n+1} = (2g + mr/g^2) / 3 */
    for (int i = 0; i < 20; i++) {
        double g2 = guess * guess;
        if (g2 == 0.0) { guess = 1e-300; continue; }
        double next = (2.0 * guess + mr / g2) / 3.0;
        if (fabs(next - guess) < 1e-16 * fabs(next)) {
            guess = next;
            break;
        }
        guess = next;
    }

    /* Re-apply 2^q */
    double result;
    if (q >= 0) {
        result = guess * (double)(1ULL << (q < 63 ? q : 0));
        if (q >= 63) result *= (double)(1ULL << 62) * (double)(1ULL << (q - 62));
    } else {
        result = guess / (double)(1ULL << (-q < 63 ? -q : 0));
        if (-q >= 63) result /= (double)(1ULL << 62) * (double)(1ULL << (-q - 62));
    }
    return neg ? -result : result;
}

/* hypot(x, y) = sqrt(x^2 + y^2), avoiding overflow */
double hypot(double x, double y) {
    x = fabs(x);
    y = fabs(y);
    if (x < y) { double t = x; x = y; y = t; }
    if (x == 0) return 0.0;
    double r = y / x;
    return x * sqrt(1.0 + r * r);
}

/*
 * sin(x) - Sine using Taylor series
 * sin(x) = x - x^3/3! + x^5/5! - ...
 */
