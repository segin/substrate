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
double sin(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return NAN;

    double res;
    __asm__ __volatile__(
        "fldl %1\n\t"
        "1: fsin\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstpl %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

/*
 * cos(x) - Cosine using Taylor series
 * cos(x) = 1 - x^2/2! + x^4/4! - ...
 */
double cos(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return NAN;

    double res;
    __asm__ __volatile__(
        "fldl %1\n\t"
        "1: fcos\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstpl %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

void sincos(double x, double *s, double *c) {
    __asm__ __volatile__(
        "fldl %2\n\t"
        "1: fsincos\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstpl %1\n\t"
        "fstpl %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(*s), "=m"(*c) : "m"(x) : "ax", "cc");
}

/* tan(x) = sin(x) / cos(x) */
double tan(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return NAN;

    double res;
    __asm__ __volatile__(
        "fldl %1\n\t"
        "1: fptan\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstp %%st(0)\n\t" // Pop the 1.0 pushed by fptan
        "fstpl %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

/*
 * atan(x) - Arctangent using Taylor series
 * atan(x) = x - x^3/3 + x^5/5 - ... for |x| <= 1
 */
double atan(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return (x < 0) ? -M_PI_2 : M_PI_2;
    if (x == 0.0) return x;

    int neg = (x < 0);
    if (neg) x = -x;

    if (x == 1.0) return (neg) ? -M_PI_4 : M_PI_4;
    
    int inv = (x > 1.0);
    if (inv) x = 1.0 / x;
    
    /* Taylor series for |x| <= 1 */
    double x2 = x * x;
    double term = x, sum = x;
    for (int i = 1; i < 100 && fabs(term) > 1e-15; i++) {
        term *= -x2;
        sum += term / (2 * i + 1);
    }
    
    if (inv) sum = M_PI_2 - sum;
    return neg ? -sum : sum;
}

/* atan2(y, x) - Two-argument arctangent (C99 Annex F.10.1.4) */
double atan2(double y, double x) {
    if (isnan(y) || isnan(x)) return NAN;

    int y_neg = signbit(y);
    int x_neg = signbit(x);
    int y_inf = isinf(y);
    int x_inf = isinf(x);

    if (y == 0.0) {
        if (!x_neg) return y;       /* preserves sign of zero in y */
        return y_neg ? -M_PI : M_PI;
    }
    if (x == 0.0) {
        return y_neg ? -M_PI_2 : M_PI_2;
    }

    if (y_inf) {
        if (x_inf) {
            double base = x_neg ? (3.0 * M_PI / 4.0) : M_PI_4;
            return y_neg ? -base : base;
        }
        return y_neg ? -M_PI_2 : M_PI_2;
    }
    if (x_inf) {
        if (!x_neg) return y_neg ? -0.0 : 0.0;
        return y_neg ? -M_PI : M_PI;
    }

    double res;
    __asm__ __volatile__("fldl %1; fldl %2; fpatan; fstpl %0" : "=m"(res) : "m"(y), "m"(x));
    return res;
}

/*
 * asin(x) - Arcsine using identity
 * asin(x) = atan(x / sqrt(1 - x^2))
 */
double asin(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return NAN;
    if (x < -1.0 || x > 1.0) return NAN;
    if (x == 0.0) return x;
    if (x == 1.0) return M_PI_2;
    if (x == -1.0) return -M_PI_2;
    return atan(x / sqrt(1.0 - x * x));
}

/* acos(x) = pi/2 - asin(x) */
double acos(double x) {
    if (x < -1.0 || x > 1.0) return NAN;
    return M_PI_2 - asin(x);
}

/*
 * Hyperbolic functions
 * sinh(x) = (e^x - e^-x) / 2
 * cosh(x) = (e^x + e^-x) / 2
 * tanh(x) = sinh(x) / cosh(x)
 */
double sinh(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return x;  /* sinh(+inf)=+inf, sinh(-inf)=-inf */
    if (x == 0.0) return x;  /* preserves +0.0/-0.0 (sinh is odd) */
    if (fabs(x) < 1e-9) return x;  /* Taylor for small x */
    double ex = exp(x);
    return (ex - 1.0 / ex) * 0.5;
}

double cosh(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return INFINITY;  /* cosh(+/-inf)=+inf (even function) */
    if (x == 0.0) return 1.0;        /* cosh(+/-0)=1 */
    double ex = exp(fabs(x));        /* even function; use |x| for symmetry */
    return (ex + 1.0 / ex) * 0.5;
}

double tanh(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return x > 0 ? 1.0 : -1.0;  /* tanh(+/-inf)=+/-1 */
    if (x == 0.0) return x;                    /* preserves +0.0/-0.0 (tanh is odd) */
    if (x > 20.0) return 1.0;                  /* asymptote; avoids exp() overflow */
    if (x < -20.0) return -1.0;
    double e2x = exp(2.0 * x);
    return (e2x - 1.0) / (e2x + 1.0);
}

/* asinh(x) = log(x + sqrt(x^2 + 1)) */
double asinh(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return x;  /* asinh(+/-inf)=+/-inf (asinh is odd, full real line) */
    if (x == 0.0) return x;  /* preserves +0.0/-0.0 (asinh is odd) */
    if (fabs(x) < 1e-9) return x;  /* Taylor for small x */
    return log(x + sqrt(x * x + 1.0));
}

/* acosh(x) = log(x + sqrt(x^2 - 1)), x >= 1 */
double acosh(double x) {
    if (isnan(x)) return x;                 /* acosh(NaN)=NaN */
    if (x == 1.0) return 0.0;               /* acosh(1)=+0 exactly */
    if (x < 1.0) {                          /* domain error (incl. -inf) */
        feraiseexcept(FE_INVALID);
        return NAN;
    }
    if (isinf(x)) return x;                 /* acosh(+inf)=+inf */
    return log(x + sqrt(x * x - 1.0));
}

/* atanh(x) = 0.5 * log((1+x)/(1-x)), |x| < 1 */
double atanh(double x) {
    if (isnan(x)) return x;                 /* atanh(NaN)=NaN */
    if (x == 0.0) return x;                 /* preserves +0.0/-0.0 (atanh is odd) */
    if (x == 1.0) {                         /* +pole */
        feraiseexcept(FE_DIVBYZERO);
        return INFINITY;
    }
    if (x == -1.0) {                        /* -pole */
        feraiseexcept(FE_DIVBYZERO);
        return -INFINITY;
    }
    if (x < -1.0 || x > 1.0 || isinf(x)) {  /* domain error (incl. +/-inf) */
        feraiseexcept(FE_INVALID);
        return NAN;
    }
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

/*
 * erf(x) - error function: 2/sqrt(pi) * integral_0^x e^(-t^2) dt
 *
 * Uses Abramowitz & Stegun 7.1.26 rational approximation
 * (max error ~1.5e-7).  Range: [-1, 1].  Odd function.
 */
double erf(double x) {
    if (isnan(x)) return x;
    if (x == 0.0) return x;                 /* preserves +0.0/-0.0 (erf is odd) */
    if (isinf(x)) return (x > 0) ? 1.0 : -1.0;

    double sign = (x < 0) ? -1.0 : 1.0;
    double absx = fabs(x);

    /* For |x| > 6.0, erf is essentially +/-1 to double precision. */
    if (absx > 6.0) return sign;

    const double a1 =  0.254829592;
    const double a2 = -0.284496736;
    const double a3 =  1.421413741;
    const double a4 = -1.453152027;
    const double a5 =  1.061405429;
    const double p  =  0.3275911;

    double t = 1.0 / (1.0 + p * absx);
    double y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1)
                     * t * exp(-absx * absx);

    return sign * y;
}

/*
 * erfc(x) - complementary error function: 1 - erf(x).
 *
 * v1 wrapper: defers to 1 - erf(x).  This loses precision for very large
 * positive x where erf(x) approaches 1, but satisfies the C99 7.12.8.2
 * special-value contract and the moderate-accuracy test suite.  A future
 * revision can substitute an asymptotic expansion (A&S 7.1.26) for large x.
 */
double erfc(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return (x > 0) ? 0.0 : 2.0;
    return 1.0 - erf(x);
}

/*
 * tgamma(x) - true Gamma function, Gamma(x).
 *
 * C99 7.12.8.4 special-value contract:
 *   tgamma(NaN)        -> NaN
 *   tgamma(+0)         -> +Inf, FE_DIVBYZERO
 *   tgamma(-0)         -> -Inf, FE_DIVBYZERO
 *   tgamma(neg int)    -> NaN, FE_INVALID (poles)
 *   tgamma(+Inf)       -> +Inf
 *   tgamma(-Inf)       -> NaN, FE_INVALID
 *   tgamma(n+1) == n!  for non-negative integer n
 *   tgamma(0.5)        == sqrt(pi)
 *   overflow at large x -> +HUGE_VAL, FE_OVERFLOW
 *
 * Implementation: Lanczos approximation (g=7, n=9), with the reflection
 * formula Gamma(x) = pi / (sin(pi*x) * Gamma(1-x)) used for x < 0.5.
 * Coefficients per Wikipedia "Lanczos approximation"; gives ~15 digits.
 */
double tgamma(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) {
        if (x > 0) return x;
        feraiseexcept(FE_INVALID);
        return NAN;
    }
    if (x == 0.0) {
        feraiseexcept(FE_DIVBYZERO);
        return signbit(x) ? -INFINITY : INFINITY;
    }

    /* Negative integer poles */
    if (x < 0.0 && x == floor(x)) {
        feraiseexcept(FE_INVALID);
        return NAN;
    }

    /* Reflection formula for x < 0.5 */
    if (x < 0.5) {
        return M_PI / (sin(M_PI * x) * tgamma(1.0 - x));
    }

    /* Overflow guard: tgamma(171.624...) overflows double */
    if (x > 171.624) {
        feraiseexcept(FE_OVERFLOW);
        return HUGE_VAL;
    }

    static const double g = 7.0;
    static const double p[9] = {
        0.99999999999980993,
        676.5203681218851,
       -1259.1392167224028,
        771.32342877765313,
       -176.61502916214059,
        12.507343278686905,
       -0.13857109526572012,
        9.9843695780195716e-6,
        1.5056327351493116e-7
    };

    x -= 1.0;
    double a = p[0];
    double t = x + g + 0.5;
    for (int i = 1; i < 9; i++) {
        a += p[i] / (x + i);
    }
    return sqrt(2.0 * M_PI) * pow(t, x + 0.5) * exp(-t) * a;
}

/*
 * lgamma_r(x, signp) - natural log of |Gamma(x)|, reentrant (BSD extension).
 *
 * Stores the sign of Gamma(x) in *signp (+1 or -1).
 *
 * Special values (C99 7.12.8.3 + POSIX):
 *   lgamma_r(NaN)        -> NaN, *signp = 1
 *   lgamma_r(+/-0)       -> +Inf, FE_DIVBYZERO, *signp = 1
 *   lgamma_r(neg int)    -> +Inf, FE_DIVBYZERO, *signp = 1 (poles)
 *   lgamma_r(+/-Inf)     -> +Inf, *signp = 1
 *   lgamma_r(1) == 0,  lgamma_r(2) == 0,  lgamma_r(n+1) == log(n!)
 *
 * Implementation: For |x| where tgamma() does not overflow, defer to the
 * existing Lanczos-based tgamma() and take log of the absolute value.
 * For large x (x >= 170) where tgamma overflows, fall back to Stirling's
 * series:  ln Gamma(x) ~ (x-0.5) ln x - x + 0.5 ln(2pi) + 1/(12x).
 */
int signgam = 1;

double lgamma_r(double x, int *signp) {
    if (isnan(x)) { *signp = 1; return x; }
    if (isinf(x)) { *signp = 1; return INFINITY; }
    if (x == 0.0 || (x < 0.0 && x == floor(x))) {
        feraiseexcept(FE_DIVBYZERO);
        *signp = 1;
        return INFINITY;
    }

    /* Moderate range: use Lanczos-backed tgamma directly. */
    if (x < 170.0) {
        double g = tgamma(x);
        if (g < 0.0) { *signp = -1; g = -g; }
        else { *signp = 1; }
        return log(g);
    }

    /* Large positive x: Stirling's approximation (Gamma(x) > 0 here). */
    *signp = 1;
    return (x - 0.5) * log(x) - x + 0.5 * log(2.0 * M_PI) + 1.0 / (12.0 * x);
}

/*
 * lgamma(x) - natural log of |Gamma(x)|; sets the global signgam to the
 * sign of Gamma(x) (XSI/POSIX). Not thread-safe; use lgamma_r() for that.
 */
double lgamma(double x) {
    return lgamma_r(x, &signgam);
}

/*
 * C23 pi-argument trigonometric functions
 */
double sinpi(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return NAN;

    double r = x - 2.0 * floor(x * 0.5 + 0.5);

    if (r == 0.0) return 0.0;
    if (r == 1.0 || r == -1.0) return 0.0;
    if (r == 0.5) return 1.0;
    if (r == -0.5) return -1.0;

    if (r > 0.5) r = 1.0 - r;
    else if (r < -0.5) r = -1.0 - r;

    return sin(M_PI * r);
}

double cospi(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return NAN;

    double r = x - 2.0 * floor(x * 0.5 + 0.5);

    if (r == 0.0) return 1.0;
    if (r == 1.0 || r == -1.0) return -1.0;
    if (r == 0.5 || r == -0.5) return 0.0;

    if (r > 0.5) return -cos(M_PI * (1.0 - r));
    if (r < -0.5) return -cos(M_PI * (1.0 + r));

    return cos(M_PI * r);
}

double tanpi(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return NAN;

    double r = x - 2.0 * floor(x * 0.5 + 0.5);

    if (r == 0.0) return 0.0;
    if (r == 1.0 || r == -1.0) return 0.0;
    if (r == 0.5) return INFINITY;
    if (r == -0.5) return -INFINITY;

    return sinpi(x) / cospi(x);
}

double asinpi(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return NAN;
    if (x < -1.0 || x > 1.0) return NAN;
    if (x == 0.0) return x;
    if (x == 1.0) return 0.5;
    if (x == -1.0) return -0.5;
    return asin(x) / M_PI;
}

double acospi(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return NAN;
    if (x < -1.0 || x > 1.0) return NAN;
    if (x == 1.0) return 0.0;
    if (x == 0.0) return 0.5;
    if (x == -1.0) return 1.0;
    return acos(x) / M_PI;
}

double atanpi(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return x > 0.0 ? 0.5 : -0.5;
    if (x == 0.0) return x;
    return atan(x) / M_PI;
}

double atan2pi(double y, double x) {
    if (isnan(y) || isnan(x)) return NAN;
    if (isinf(y) && isinf(x)) {
        if (!signbit(x)) return signbit(y) ? -0.25 : 0.25;
        return signbit(y) ? -0.75 : 0.75;
    }
    if (isinf(y)) return signbit(y) ? -0.5 : 0.5;
    if (isinf(x)) {
        if (!signbit(x)) return signbit(y) ? -0.0 : 0.0;
        return signbit(y) ? -1.0 : 1.0;
    }
    if (y == 0.0) {
        if (!signbit(x)) return y;
        return signbit(y) ? -1.0 : 1.0;
    }
    if (x == 0.0) return signbit(y) ? -0.5 : 0.5;
    return atan2(y, x) / M_PI;
}

/*
 * Floating-point manipulation functions
 */

/* frexp: x = mantissa * 2^exp, where 0.5 <= |mantissa| < 1.
 * Conformance: C99 7.12.6.4.
 *  - x == 0.0 (including -0.0): *exp = 0, return x (preserves sign of zero).
 *  - x == +/-inf or NaN: *exp = 0, return x (do not crash).
 *  - Subnormal x: still normalized via the doubling loop.
 */
double frexp(double x, int *exp) {
    if (x == 0.0) { *exp = 0; return x; }
    if (isinf(x) || isnan(x)) { *exp = 0; return x; }

    int neg = (x < 0);
    if (neg) x = -x;

    *exp = 0;
    while (x >= 1.0) { x *= 0.5; (*exp)++; }
    while (x < 0.5) { x *= 2.0; (*exp)--; }

    return neg ? -x : x;
}

/* ldexp: x * 2^exp.
 * Conformance: C99 7.12.6.6.
 *  - x == 0.0 (including -0.0): return x (sign preserved).
 *  - x == NaN: return NaN.
 *  - x == +/-inf: return x.
 *  - Overflow: return +/-HUGE_VAL with errno = ERANGE.
 *  - Underflow: return +/-0 (or subnormal) with errno = ERANGE.
 * Uses the x87 fscale instruction (matches scalbn's strategy).
 */
double ldexp(double x, int exp) {
    if (isnan(x)) return x;
    if (x == 0.0 || isinf(x)) return x;

    double res;
    __asm__ __volatile__("fildl %2; fldl %1; fscale; fstp %%st(1); fstpl %0"
                         : "=m"(res) : "m"(x), "m"(exp));

    if (isinf(res) && !isinf(x)) {
        errno = ERANGE;
        return (x < 0.0) ? -HUGE_VAL : HUGE_VAL;
    }
    if (res == 0.0 && x != 0.0) {
        errno = ERANGE;
    }
    return res;
}

/* modf: split into integer and fractional parts (C99 7.12.6.12) */
double modf(double x, double *iptr) {
    /* NaN: *iptr = NaN, return NaN */
    if (isnan(x)) {
        *iptr = x;
        return x;
    }
    /* +/-Inf: *iptr = +/-Inf, return +/-0.0 (sign of x) */
    if (isinf(x)) {
        *iptr = x;
        return copysign(0.0, x);
    }
    /* +/-0.0: *iptr = +/-0.0, return +/-0.0 (sign preserved on both) */
    if (x == 0.0) {
        *iptr = x;
        return x;
    }
    /* Normal case: truncate via bit manipulation to preserve sign of zero
     * (the local trunc() converts via int and loses -0.0 / overflows on huge x). */
    union { double d; uint64_t u; } u = { .d = x };
    int exp = (int)((u.u >> 52) & 0x7FF) - 1023;

    if (exp < 0) {
        /* |x| < 1: integer part is +/-0.0 with sign of x, fraction is x */
        *iptr = copysign(0.0, x);
        return x;
    }
    if (exp >= 52) {
        /* |x| is so large it has no fractional bits */
        *iptr = x;
        return copysign(0.0, x);
    }
    /* Mask off the fractional mantissa bits */
    uint64_t mask = ((uint64_t)1 << (52 - exp)) - 1;
    if ((u.u & mask) == 0) {
        /* Already an integer */
        *iptr = x;
        return copysign(0.0, x);
    }
    union { double d; uint64_t u; } iu = { .u = u.u & ~mask };
    *iptr = iu.d;
    return x - iu.d;
}

/* scalbn: x * 2^n (FLT_RADIX = 2 for IEEE-754).
 * Conformance: C99 7.12.6.13.
 *  - x == 0.0 (including -0.0): return x (sign preserved).
 *  - x == NaN: return NaN.
 *  - x == +/-inf: return x.
 *  - Overflow: return +/-HUGE_VAL with errno = ERANGE.
 *  - Underflow: return +/-0 with errno = ERANGE.
 * Equivalent to ldexp() on IEEE-754 platforms.
 */
double scalbn(double x, int n) {
    if (isnan(x)) return x;
    if (x == 0.0 || isinf(x)) return x;

    double res;
    __asm__ __volatile__("fildl %2; fldl %1; fscale; fstp %%st(1); fstpl %0"
                         : "=m"(res) : "m"(x), "m"(n));

    if (isinf(res) && !isinf(x)) {
        errno = ERANGE;
        return (x < 0.0) ? -HUGE_VAL : HUGE_VAL;
    }
    if (res == 0.0 && x != 0.0) {
        errno = ERANGE;
    }
    return res;
}

/* scalbln: x * 2^n with long exponent.
 * Conformance: C99 7.12.6.13. Behaviour matches scalbn() once the
 * exponent has been clamped into int range.
 */
double scalbln(double x, long n) {
    if (n > INT_MAX) n = INT_MAX;
    else if (n < INT_MIN) n = INT_MIN;
    return scalbn(x, (int)n);
}

/* ilogb: extract the unbiased exponent of x as an int.
 * Conformance: C99 7.12.6.5. For finite non-zero x the result is
 * floor(log2(|x|)). The special inputs 0, +/-Inf and NaN raise
 * FE_INVALID and return FP_ILOGB0, INT_MAX, FP_ILOGBNAN respectively.
 * Implementation strategy: frexp() returns frac in [0.5, 1) such that
 * x == frac * 2^e, hence log2(|x|) == e - 1. This naturally handles
 * subnormals because frexp() normalises them.
 */
int ilogb(double x) {
    if (isnan(x)) {
        feraiseexcept(FE_INVALID);
        return FP_ILOGBNAN;
    }
    if (x == 0.0) {
        feraiseexcept(FE_INVALID);
        return FP_ILOGB0;
    }
    if (isinf(x)) {
        feraiseexcept(FE_INVALID);
        return INT_MAX;
    }
    int e;
    (void)frexp(x, &e);
    return e - 1;
}

/* logb: extract the unbiased exponent of x as a double.
 * Conformance: C99 7.12.6.11. For finite non-zero x the result is
 * floor(log2(|x|)) returned as a double (same value as ilogb()).
 * logb(0) is a pole error: returns -INFINITY and raises FE_DIVBYZERO.
 * logb(+/-Inf) returns +INFINITY (no exception). logb(NaN) returns NaN.
 */
double logb(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return INFINITY;
    if (x == 0.0) {
        feraiseexcept(FE_DIVBYZERO);
        return -INFINITY;
    }
    int e;
    (void)frexp(x, &e);
    return (double)(e - 1);
}

/* nextafter: next representable value after x towards y.
 * Conformance: C99 7.12.11.3, Annex F.10.8.3.
 *  - x or y NaN: return NaN.
 *  - x == y: return y (preserves sign of zero per F.10.8.3).
 *  - x == +/-0 moving outward: smallest +/-subnormal; range error
 *    (errno = ERANGE, FE_UNDERFLOW raised).
 *  - x finite, magnitude becomes infinite: return +/-HUGE_VAL; range error
 *    (errno = ERANGE, FE_OVERFLOW raised).
 *  - x finite, result is subnormal (loss of precision): range error
 *    (errno = ERANGE, FE_UNDERFLOW raised).
 *  - x == +/-INF moving toward finite y: return +/-DBL_MAX (no range error).
 * Operates by incrementing/decrementing the IEEE-754 bit pattern.
 */
double nextafter(double x, double y) {
    if (isnan(x) || isnan(y)) return NAN;
    if (x == y) return y;

    union { double d; uint64_t u; } u = { .d = x };

    if (x == 0.0) {
        /* Smallest subnormal in direction of y; sign comes from y. */
        u.u = 1;
        if (signbit(y)) u.u |= ((uint64_t)1 << 63);
        errno = ERANGE;
        feraiseexcept(FE_UNDERFLOW);
        return u.d;
    }

    /* x > 0 and y > x  -> increase magnitude (u.u++)
     * x > 0 and y < x  -> decrease magnitude (u.u--)
     * x < 0 and y > x  -> decrease magnitude (u.u--)
     * x < 0 and y < x  -> increase magnitude (u.u++)
     * Equivalent: (x > 0) == (y > x) selects increment. */
    if ((x > 0.0) == (y > x)) {
        u.u++;
    } else {
        u.u--;
    }

    /* Range checks per Annex F.10.8.3. */
    if (isinf(u.d)) {
        errno = ERANGE;
        feraiseexcept(FE_OVERFLOW | FE_INEXACT);
        return signbit(x) ? -HUGE_VAL : HUGE_VAL;
    }
    /* Subnormal result from a previously normal operation: underflow. */
    if ((u.u & 0x7FF0000000000000ULL) == 0) {
        errno = ERANGE;
        feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
    }
    return u.d;
}

/* nexttoward: like nextafter() but with long double direction argument.
 * Conformance: C99 7.12.11.4.
 *  - x or y NaN: return NaN.
 *  - (long double)x == y: return (double)y (preserves sign of zero).
 *  - Otherwise: step x one ULP toward y using nextafter() with the
 *    appropriate +/-INFINITY direction sentinel. The extra precision of
 *    long double y matters precisely when (double)y == x but y differs
 *    from x as a long double, in which case we must still step away.
 */
double nexttoward(double x, long double y) {
    if (isnan(x) || isnan((double)y)) return NAN;
    long double xl = (long double)x;
    if (xl == y) return (double)y;
    return nextafter(x, (xl < y) ? INFINITY : -INFINITY);
}

/* nextup: next representable value toward +INFINITY (C23 7.12.11.5).
 *  - NaN: return NaN.
 *  - +INFINITY: return +INFINITY (already at maximum; no further "up" step).
 *  - -INFINITY: return -DBL_MAX.
 *  - -0.0: returns smallest positive subnormal (per IEEE 754-2019 nextUp(-0)).
 *  - Otherwise: next representable double > x.
 * Trivially implemented via nextafter(x, +INFINITY); the x == y case of
 * nextafter() handles +INFINITY by returning +INFINITY without raising
 * any exceptions.
 */
double nextup(double x) {
    if (isnan(x)) return x;
    return nextafter(x, INFINITY);
}

/* nextdown: next representable value toward -INFINITY (C23 7.12.11.6).
 *  - NaN: return NaN.
 *  - -INFINITY: return -INFINITY (already at minimum; no further "down" step).
 *  - +INFINITY: return DBL_MAX.
 *  - +0.0: returns largest negative subnormal (per IEEE 754-2019 nextDown(+0)).
 *  - Otherwise: next representable double < x.
 * Trivially implemented via nextafter(x, -INFINITY); the x == y case of
 * nextafter() handles -INFINITY by returning -INFINITY without raising
 * any exceptions.
 */
double nextdown(double x) {
    if (isnan(x)) return x;
    return nextafter(x, -INFINITY);
}

/* copysign: magnitude of x with sign of y */
double copysign(double x, double y) {
    union { double d; uint64_t u; } ux = { .d = x }, uy = { .d = y };
    ux.u = (ux.u & 0x7FFFFFFFFFFFFFFFULL) | (uy.u & 0x8000000000000000ULL);
    return ux.d;
}

/*
 * nan: return a quiet NaN. Per C99 7.12.11.2, the tagp string selects an
 * implementation-defined NaN payload; we ignore tagp and return NAN, which
 * is standards-compliant since most code cannot observe the payload.
 */
double nan(const char *tagp) {
    (void)tagp;
    return NAN;
}

/* Absolute value — bit-twiddle on the IEEE-754 sign bit.  The
 * previous x87 inline-asm version (fldl; fabs; fstpl) was correct
 * in isolation but broke at -O2 when GCC inlined it into a caller
 * already using a deep x87 stack: the inner `fldl` could push past
 * st(7) and silently produce a NaN.  Bit-twiddle has no such
 * coupling to the x87 register allocator. */
double fabs(double x) {
    union { double d; uint64_t u; } v = { .d = x };
    v.u &= 0x7FFFFFFFFFFFFFFFULL;
    return v.d;
}

/* Remainder functions */
double fmod(double x, double y) {
    if (y == 0.0) return NAN;
    double res;
    __asm__ __volatile__(
        "fldl %2\n\t"
        "fldl %1\n\t"
        "1: fprem\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 1b\n\t"
        "fstpl %0\n\t"
        "fstp %%st(0)"
        : "=m"(res) : "m"(x), "m"(y) : "ax", "cc");
    return res;
}

double remainder(double x, double y) {
    if (y == 0.0) return NAN;
    double res;
    __asm__ __volatile__(
        "fldl %2\n\t"
        "fldl %1\n\t"
        "1: fprem1\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 1b\n\t"
        "fstpl %0\n\t"
        "fstp %%st(0)"
        : "=m"(res) : "m"(x), "m"(y) : "ax", "cc");
    return res;
}

/* Min/Max - actual implementations */
double fmax(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    if (x == 0.0 && y == 0.0)
        return signbit(x) ? y : x;
    return (x > y) ? x : y;
}

double fmin(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    if (x == 0.0 && y == 0.0)
        return signbit(x) ? x : y;
    return (x < y) ? x : y;
}

/* Positive difference */
double fdim(double x, double y) {
	if(isnan(x) || isnan(y)) return NAN;
	return (x > y) ? (x - y) : 0.0;
}

/*
 * remquo: IEEE remainder with low-order quotient bits.
 * Returns remainder(x, y) and stores quotient sign + low 3 bits in *quo.
 */
double remquo(double x, double y, int *quo) {
	if(isnan(x) || isnan(y) || isinf(x) || y == 0.0) {
		*quo = 0;
		return NAN;
	}

	int sign = 1;
	double ax = x, ay = y;
	if(ax < 0) { ax = -ax; sign = -sign; }
	if(ay < 0) { ay = -ay; sign = -sign; }

	/* Use repeated division to get full quotient mod 8 */
	double q_approx = ax / ay;
	long long q_int = (long long)rint(q_approx);
	*quo = (int)((q_int & 0x7) * sign);

	return remainder(x, y);
}

/*
 * fma(x, y, z) - Fused Multiply-Add: (x * y) + z with single rounding.
 *
 * On x87 there is no hardware FMA instruction. We use Dekker's algorithm
 * to split the product x*y into an exact hi+lo pair via double-double
 * arithmetic, then add z and round once.
 *
 * Dekker split factor for 53-bit mantissa: 2^27 + 1 = 134217729.
 */
double fma(double x, double y, double z) {
	/* Handle special values */
	if(isnan(x) || isnan(y) || isnan(z)) return NAN;
	if((isinf(x) && y == 0.0) || (x == 0.0 && isinf(y))) return NAN;
	if(isinf(x) || isinf(y)) {
		double p = x * y;
		if(isinf(z) && ((p > 0) != (z > 0))) return NAN;
		return p + z;
	}
	if(isinf(z)) return z;
	if(x == 0.0 || y == 0.0) return x * y + z;

	/*
	 * Dekker's product: split x and y into hi/lo parts so that
	 * x*y = p_hi + p_lo exactly (no rounding error in the sum).
	 */
	static const double SPLIT = 134217729.0; /* 2^27 + 1 */

	double cx = x * SPLIT;
	double x_hi = cx - (cx - x);
	double x_lo = x - x_hi;

	double cy = y * SPLIT;
	double y_hi = cy - (cy - y);
	double y_lo = y - y_hi;

	double p_hi = x * y;          /* rounded product */
	double p_lo = ((x_hi * y_hi - p_hi) + x_hi * y_lo
	              + x_lo * y_hi) + x_lo * y_lo;

	/* Now compute (p_hi + p_lo) + z with single rounding */
	double s_hi = p_hi + z;
	double s_lo;
	if(fabs(p_hi) >= fabs(z))
		s_lo = (p_hi - s_hi) + z + p_lo;
	else
		s_lo = (z - s_hi) + p_hi + p_lo;

	return s_hi + s_lo;
}

/*
 * C23 fmaximum / fminimum family.
 *
 * fmaximum/fminimum:         NaN-propagating, distinguish +0/-0.
 * fmaximum_num/fminimum_num: NaN-ignoring (like C99 fmax/fmin), distinguish +0/-0.
 * fmaximum_mag/fminimum_mag: compare magnitudes, NaN-propagating.
 */
double fmaximum(double x, double y) {
	if(isnan(x) || isnan(y)) return NAN;
	if(x == 0.0 && y == 0.0) {
		/* +0 > -0 */
		return signbit(x) ? y : x;
	}
	return (x > y) ? x : y;
}

double fminimum(double x, double y) {
	if(isnan(x) || isnan(y)) return NAN;
	if(x == 0.0 && y == 0.0) {
		/* -0 < +0 */
		return signbit(x) ? x : y;
	}
	return (x < y) ? x : y;
}

double fmaximum_num(double x, double y) {
	if(isnan(x) && isnan(y)) return NAN;
	if(isnan(x)) return y;
	if(isnan(y)) return x;
	if(x == 0.0 && y == 0.0)
		return signbit(x) ? y : x;
	return (x > y) ? x : y;
}

double fminimum_num(double x, double y) {
	if(isnan(x) && isnan(y)) return NAN;
	if(isnan(x)) return y;
	if(isnan(y)) return x;
	if(x == 0.0 && y == 0.0)
		return signbit(x) ? x : y;
	return (x < y) ? x : y;
}

double fmaximum_mag(double x, double y) {
	if(isnan(x) || isnan(y)) return NAN;
	double ax = fabs(x), ay = fabs(y);
	if(ax > ay) return x;
	if(ay > ax) return y;
	/* Equal magnitudes: fall back to fmaximum for sign distinction */
	return fmaximum(x, y);
}

double fminimum_mag(double x, double y) {
	if(isnan(x) || isnan(y)) return NAN;
	double ax = fabs(x), ay = fabs(y);
	if(ax < ay) return x;
	if(ay < ax) return y;
	return fminimum(x, y);
}

/* Rounding functions.
 *
 * The naive (int)x cast was undefined behaviour for |x| > 2^31.  The
 * portable implementation works on the IEEE-754 representation: extract
 * the unbiased exponent, mask off the fractional bits, then nudge by
 * ±1 ULP (in the integer sense) for floor/ceil when the input had a
 * non-zero fractional part.  This does not depend on x87 and works on
 * every architecture with a 64-bit IEEE double.
 *
 * On i386/x86_64 we also provide an x87 fast path using frndint with
 * an explicitly-set rounding mode.  The original control word is
 * restored before returning.
 */

/* mode: 0 = truncate-toward-zero, 1 = floor (down), 2 = ceil (up) */
__attribute__((unused))
static double round_to_int_portable(double x, int mode) {
    if (isnan(x) || isinf(x) || x == 0.0) return x;

    union { double d; uint64_t u; } v;
    v.d = x;
    int sign = (int)(v.u >> 63);
    int e = (int)((v.u >> 52) & 0x7FF) - 1023;

    if (e >= 52) {
        /* No fractional bits — already an integer. */
        return x;
    }
    if (e < 0) {
        /* |x| < 1 */
        if (mode == 0) return sign ? -0.0 : 0.0;
        if (mode == 1) return sign ? -1.0 : 0.0;          /* floor */
        return sign ? -0.0 : 1.0;                          /* ceil */
    }

    uint64_t frac_mask = (1ULL << (52 - e)) - 1ULL;
    if ((v.u & frac_mask) == 0) return x;

    union { double d; uint64_t u; } t;
    t.u = v.u & ~frac_mask; /* truncated toward zero */
    if (mode == 0) return t.d;
    if (mode == 1) return sign ? t.d - 1.0 : t.d;        /* floor */
    return sign ? t.d : t.d + 1.0;                        /* ceil */
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * Rounding-control bits in the i387 control word:
 *   00 = round to nearest, 01 = round down (-inf),
 *   10 = round up (+inf),  11 = truncate toward zero.
 */
static inline double round_to_int_x87(double x, unsigned rc_bits) {
    double res;
    unsigned short cw_orig, cw_new;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw_orig));
    cw_new = (unsigned short)((cw_orig & 0xF3FFu) | (rc_bits & 0x0C00u));
    __asm__ __volatile__(
        "fldcw %1\n\t"
        "fldl %2\n\t"
        "frndint\n\t"
        "fstpl %0\n\t"
        "fldcw %3\n\t"
        : "=m"(res)
        : "m"(cw_new), "m"(x), "m"(cw_orig));
    return res;
}
#endif

double ceil(double x) {
    if (isnan(x) || isinf(x)) return x;
#if defined(__i386__) || defined(__x86_64__)
    return round_to_int_x87(x, 0x0800);
#else
    return round_to_int_portable(x, 2);
#endif
}

double floor(double x) {
    if (isnan(x) || isinf(x)) return x;
#if defined(__i386__) || defined(__x86_64__)
    return round_to_int_x87(x, 0x0400);
#else
    return round_to_int_portable(x, 1);
#endif
}

double trunc(double x) {
    if (isnan(x) || isinf(x)) return x;
#if defined(__i386__) || defined(__x86_64__)
    return round_to_int_x87(x, 0x0C00);
#else
    return round_to_int_portable(x, 0);
#endif
}

double round(double x) {
    /* C99: round-half-away-from-zero, regardless of current mode. */
    if (isnan(x) || isinf(x)) return x;
    return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5);
}

/*
 * roundeven: round-half-to-even (banker's rounding), C23.
 * Always uses round-to-nearest-even regardless of rounding mode.
 */
double roundeven(double x) {
    return rint(x);
}

double rint(double x) {
    if (isnan(x) || isinf(x)) return x;
#if defined(__i386__) || defined(__x86_64__)
    double res;
    __asm__ __volatile__("fldl %1; frndint; fstpl %0" : "=m"(res) : "m"(x));
    return res;
#else
    /* Portable round-to-nearest-even.  Loses fenv rounding mode on
     * non-x86 — the right fix is a per-arch fenv backend, which is
     * out of scope here. */
    double t = round_to_int_portable(x, 0); /* truncate */
    double frac = x - t;
    if (frac > 0.5)  return t + 1.0;
    if (frac < -0.5) return t - 1.0;
    if (frac == 0.5 || frac == -0.5) {
        /* Round to even. */
        long long ti = (long long)t;
        if (ti & 1LL) {
            return frac > 0 ? t + 1.0 : t - 1.0;
        }
        return t;
    }
    return t;
#endif
}

double nearbyint(double x) {
    return rint(x); /* x87 frndint honours CW; portable path approximates. */
}

/*
 * lrint/llrint: round per current FE rounding mode and convert to integer.
 *
 * x87 path: fistp stores the indefinite-integer encoding on overflow and
 * sets the FPU's invalid-operation flag.  We surface that as errno=ERANGE.
 *
 * Portable path: rint() honours the current rounding mode (no x87 needed
 * on non-x86 — left as nearbyint-equivalent), then we range-check before
 * the cast.  The `rint` here is the C-fallback version below.
 */
long lrint(double x) {
#if defined(__i386__) || defined(__x86_64__)
    long res;
    unsigned short sw;
    __asm__ __volatile__(
        "fclex\n\t"
        "fldl %2\n\t"
        "fistpl %0\n\t"
        "fnstsw %1\n\t"
        : "=m"(res), "=m"(sw) : "m"(x));
    if (sw & 0x01) errno = ERANGE; /* IE: invalid operation */
    return res;
#else
    if (isnan(x)) { errno = EDOM; return 0; }
    double r = rint(x);
    if (r > (double)LONG_MAX || r < (double)LONG_MIN) {
        errno = ERANGE;
        return r > 0 ? LONG_MAX : LONG_MIN;
    }
    return (long)r;
#endif
}

long long llrint(double x) {
#if defined(__i386__) || defined(__x86_64__)
    long long res;
    unsigned short sw;
    __asm__ __volatile__(
        "fclex\n\t"
        "fldl %2\n\t"
        "fistpq %0\n\t"
        "fnstsw %1\n\t"
        : "=m"(res), "=m"(sw) : "m"(x));
    if (sw & 0x01) errno = ERANGE;
    return res;
#else
    if (isnan(x)) { errno = EDOM; return 0; }
    double r = rint(x);
    if (r > (double)LLONG_MAX || r < (double)LLONG_MIN) {
        errno = ERANGE;
        return r > 0 ? LLONG_MAX : LLONG_MIN;
    }
    return (long long)r;
#endif
}

/*
 * lround/llround: round-half-away-from-zero, then convert.  The cast back to
 * a signed integer type is UB on overflow so we bound-check first and clamp.
 */
long lround(double x) {
    if (isnan(x)) { errno = EDOM; return 0; }
    double r = round(x);
    if (r > (double)LONG_MAX) { errno = ERANGE; return LONG_MAX; }
    if (r < (double)LONG_MIN) { errno = ERANGE; return LONG_MIN; }
    return (long)r;
}

long long llround(double x) {
    if (isnan(x)) { errno = EDOM; return 0; }
    double r = round(x);
    if (r > (double)LLONG_MAX) { errno = ERANGE; return LLONG_MAX; }
    if (r < (double)LLONG_MIN) { errno = ERANGE; return LLONG_MIN; }
    return (long long)r;
}

/* Float versions */
float sinf(float x) { return(float)sin(x); }
float cosf(float x) { return(float)cos(x); }
float tanf(float x) { return(float)tan(x); }
void sincosf(float x, float *s, float *c) { double ds, dc; sincos(x, &ds, &dc); *s = (float)ds; *c = (float)dc; }
float sqrtf(float x) { return(float)sqrt(x); }
float powf(float x, float y) { return(float)pow(x, y); }
float fabsf(float x) { return (x < 0) ? -x : x; }
float fmodf(float x, float y) { return(float)fmod(x, y); }
float remainderf(float x, float y) { return(float)remainder(x, y); }
float remquof(float x, float y, int *quo) { return(float)remquo(x, y, quo); }
float fmaf(float x, float y, float z) { return(float)fma(x, y, z); }
float fmaxf(float x, float y) { return(float)fmax(x, y); }
float fminf(float x, float y) { return(float)fmin(x, y); }
float fdimf(float x, float y) { return(float)fdim(x, y); }
float ceilf(float x) { return(float)ceil(x); }
float floorf(float x) { return(float)floor(x); }
float truncf(float x) { return(float)trunc(x); }
float roundf(float x) { return(float)round(x); }
float roundevenf(float x) { return(roundeven(x)); }
float fmaximumf(float x, float y) { return(float)fmaximum(x, y); }
float fminimumf(float x, float y) { return(float)fminimum(x, y); }
float fmaximum_numf(float x, float y) { return(float)fmaximum_num(x, y); }
float fminimum_numf(float x, float y) { return(float)fminimum_num(x, y); }
float fmaximum_magf(float x, float y) { return(float)fmaximum_mag(x, y); }
float fminimum_magf(float x, float y) { return(float)fminimum_mag(x, y); }

/* Long double versions (same as double on i386) */
long double sinl(long double x) { return sin(x); }
long double cosl(long double x) { return cos(x); }
long double tanl(long double x) { return tan(x); }
void sincosl(long double x, long double *s, long double *c) { double ds, dc; sincos(x, &ds, &dc); *s = ds; *c = dc; }
long double sqrtl(long double x) { return sqrt(x); }
long double powl(long double x, long double y) { return pow(x, y); }
long double fabsl(long double x) { return (x < 0) ? -x : x; }
long double fmodl(long double x, long double y) { return fmod(x, y); }
long double remainderl(long double x, long double y) { return remainder(x, y); }
long double remquol(long double x, long double y, int *quo) { return remquo(x, y, quo); }
long double fmal(long double x, long double y, long double z) { return fma(x, y, z); }
long double fmaxl(long double x, long double y) { return fmax(x, y); }
long double fminl(long double x, long double y) { return fmin(x, y); }
long double fdiml(long double x, long double y) { return fdim(x, y); }
long double ceill(long double x) { return ceil(x); }
long double floorl(long double x) { return floor(x); }
long double truncl(long double x) { return trunc(x); }
long double roundl(long double x) { return round(x); }
long double roundevenl(long double x) { return roundeven(x); }
long double fmaximuml(long double x, long double y) { return fmaximum(x, y); }
long double fminimuml(long double x, long double y) { return fminimum(x, y); }
long double fmaximum_numl(long double x, long double y) { return fmaximum_num(x, y); }
long double fminimum_numl(long double x, long double y) { return fminimum_num(x, y); }
long double fmaximum_magl(long double x, long double y) { return fmaximum_mag(x, y); }
long double fminimum_magl(long double x, long double y) { return fminimum_mag(x, y); }

/* C23 pi-argument trig float wrappers */
float sinpif(float x) { return (float)sinpi(x); }
float cospif(float x) { return (float)cospi(x); }
float tanpif(float x) { return (float)tanpi(x); }
float asinpif(float x) { return (float)asinpi(x); }
float atanpif(float x) { return (float)atanpi(x); }

/* C23 pi-argument trig long double wrappers */
long double sinpil(long double x) { return sinpi(x); }
long double cospil(long double x) { return cospi(x); }
long double tanpil(long double x) { return tanpi(x); }
long double asinpil(long double x) { return asinpi(x); }
long double atanpil(long double x) { return atanpi(x); }

/* C23: fromfp family — convert floating-point values with explicit rounding */

/*
 * fromfp: store x into *y using the given rounding_mode.
 * rounding_mode values match fesetround() modes; FE_TONEAREST is default.
 * Returns 0 on success, non-zero for unsupported rounding modes.
 * envp (if non-NULL) is set to the current fenv state.
 */
int fromfp(double *y, double x, fenv_t *envp, int rounding_mode) {
    if (envp) {
        fenv_t zero_env = { 0 };
        *envp = zero_env;
    }
    switch (rounding_mode) {
    case FE_TONEAREST:
        *y = x;
        break;
    case FE_DOWNWARD:
        *y = floor(x);
        break;
    case FE_UPWARD:
        *y = ceil(x);
        break;
    case FE_TOWARDZERO:
        *y = trunc(x);
        break;
    default:
        return -1;
    }
    return 0;
}

int fromfpx(double *y, double x, fenv_t *envp, int rounding_mode) {
    return fromfp(y, x, envp, rounding_mode);
}

int ufromfp(unsigned int *y, double x, fenv_t *envp, int rounding_mode) {
    double tmp = 0;
    int rc = fromfp(&tmp, x, envp, rounding_mode);
    *y = (unsigned int)tmp;
    return rc;
}

/*
 * Bessel functions J_n(x) and Y_n(x) — XSI/POSIX extensions.
 *
 *   |x| <= 8: Taylor series in z = (x/2)^2.  Converges absolutely
 *             for any x, but the term count grows with |x|; we cap
 *             at 60 iterations and trust the early-out below.  At
 *             |x|=8 the series reaches 1e-18 in ~35 terms.
 *
 *   |x| >  8: Hankel asymptotic expansion via the standard P_n / Q_n
 *             modulus and phase functions in z = 1/(8x)^2.  This is
 *             a *divergent* series — we sum until the term magnitude
 *             stops shrinking and then stop, which gives the best
 *             obtainable accuracy for the given |x|.
 *
 * J_n with n >= 2:
 *   n <= x : forward recurrence from j0/j1 (numerically stable).
 *   n >  x : Miller's backward recurrence with the standard
 *            normalisation  J_0 + 2(J_2 + J_4 + ...) = 1.
 *
 * Y_n with n >= 2: forward recurrence always (stable for Y).
 *
 * Symmetry:
 *   J_n(-x) = (-1)^n J_n(x)
 *   J_{-n}(x) = (-1)^n J_n(x)
 *
 * Special values:
 *   j0(0) = 1, j1(0) = 0, jn(n != 0, 0) = 0
 *   y0(0) = y1(0) = yn(n, 0) = -inf
 *   y*(x < 0) = NaN + FE_INVALID
 *   j*(NaN)   = NaN          y*(NaN)   = NaN
 *   j*(+inf)  = 0            y*(+inf)  = 0
 */

#define BESSEL_EPS    1.0e-18
#define BESSEL_MAXIT  60
/* As #define to avoid any chance of -O2 misoptimising a file-scope
 * static const reference across the x87-asm log() boundary. */
#define BESSEL_GAMMA    0.5772156649015328606
#define BESSEL_TWOOPI   0.6366197723675813431
#define BESSEL_ONEOPI   0.3183098861837906715
#define BESSEL_RSQRTPI  0.5641895835477562869

/*
 * Taylor series for J_n(x), x >= 0.  Only called for n = 0 or 1
 * from the public API; works for any n >= 0 in principle.
 */
static double bessel_j_series(int n, double x)
{
    double half = 0.5 * x;
    double z = half * half;
    double term;
    int k;

    /* term_0 = (x/2)^n / n!  */
    term = 1.0;
    for (k = 1; k <= n; k++) term *= half / k;

    double sum = term;
    for (k = 1; k <= BESSEL_MAXIT; k++) {
        term *= -z / ((double)k * (double)(n + k));
        sum += term;
        if (fabs(term) < BESSEL_EPS * fabs(sum)) break;
    }
    return sum;
}

/* Y_0(x), power series, x > 0. */
static double bessel_y0_series(double x)
{
    double half = 0.5 * x;
    double z = half * half;
    /* Build J_0 and a sum involving harmonic numbers in parallel. */
    double term = 1.0;
    double j0sum = 1.0;
    double Hk = 0.0;
    double hsum = 0.0;
    for (int k = 1; k <= BESSEL_MAXIT; k++) {
        term *= -z / ((double)k * (double)k);
        j0sum += term;
        Hk += 1.0 / k;
        hsum += Hk * term;
        if (fabs(term) * (Hk + 1.0) <
            BESSEL_EPS * (fabs(j0sum) + fabs(hsum))) break;
    }
    return BESSEL_TWOOPI * ((log(half) + BESSEL_GAMMA) * j0sum - hsum);
}

/* Y_1(x), power series, x > 0. */
static double bessel_y1_series(double x)
{
    double half = 0.5 * x;
    double z = half * half;
    /* j1sum is J_1(x) / (x/2). */
    double term = 1.0;
    double j1sum = 1.0;
    double Hk = 0.0;            /* H_0 */
    double Hkp1 = 1.0;          /* H_1 */
    double psum = Hk + Hkp1;    /* k=0 contribution to Σ (H_k + H_{k+1}) z^k / (k!(k+1)!) */
    for (int k = 1; k <= BESSEL_MAXIT; k++) {
        term *= -z / ((double)k * (double)(k + 1));
        j1sum += term;
        Hk = Hkp1;
        Hkp1 = Hk + 1.0 / (k + 1);
        psum += (Hk + Hkp1) * term;
        if (fabs(term) * (Hkp1 + 1.0) <
            BESSEL_EPS * (fabs(j1sum) + fabs(psum))) break;
    }
    double j1x = half * j1sum;
    return BESSEL_TWOOPI * ((log(half) + BESSEL_GAMMA) * j1x - 1.0 / x)
         - half * BESSEL_ONEOPI * psum;
}

/*
 * Hankel asymptotic modulus and phase.  Computes P_n(x), Q_n(x)
 * in z = 1/(8x)^2.  Truncates on the first non-decreasing term —
 * standard handling for an asymptotic (divergent) series.
 */
static void bessel_pq(int n, double x, double *P, double *Q)
{
    double mu = 4.0 * (double)n * (double)n;
    double inv8x = 1.0 / (8.0 * x);
    double inv8x2 = inv8x * inv8x;

    double p_term = 1.0;
    double q_term = (mu - 1.0) * inv8x;
    double p_sum  = p_term;
    double q_sum  = q_term;
    double prev   = fabs(p_term) + fabs(q_term);

    for (int k = 1; k <= 16; k++) {
        double f1 = mu - (4.0*k - 3.0) * (4.0*k - 3.0);
        double f2 = mu - (4.0*k - 1.0) * (4.0*k - 1.0);
        double f3 = mu - (4.0*k + 1.0) * (4.0*k + 1.0);

        p_term *= -f1 * f2 * inv8x2 / ((2.0*k - 1.0) * (2.0*k));
        q_term *= -f2 * f3 * inv8x2 / ((2.0*k)       * (2.0*k + 1.0));

        p_sum += p_term;
        q_sum += q_term;

        double mag = fabs(p_term) + fabs(q_term);
        if (mag > prev) break;                          /* diverging — stop */
        if (mag < BESSEL_EPS * (fabs(p_sum) + fabs(q_sum))) break;
        prev = mag;
    }
    *P = p_sum;
    *Q = q_sum;
}

/* J_n(x) for x >> 1, n = 0 or 1. */
static double bessel_j_asymp(int n, double x)
{
    double P, Q;
    bessel_pq(n, x, &P, &Q);
    double inv = BESSEL_RSQRTPI / sqrt(x);
    double s = sin(x), c = cos(x);
    if (n == 0) return inv * ((P + Q) * c + (P - Q) * s);
    return         inv * ((Q - P) * c + (P + Q) * s);   /* n == 1 */
}

/* Y_n(x) for x >> 1, n = 0 or 1. */
static double bessel_y_asymp(int n, double x)
{
    double P, Q;
    bessel_pq(n, x, &P, &Q);
    double inv = BESSEL_RSQRTPI / sqrt(x);
    double s = sin(x), c = cos(x);
    if (n == 0) return inv * ((Q - P) * c + (P + Q) * s);
    return         inv * (-(P + Q) * c + (Q - P) * s);  /* n == 1 */
}

/* Internal: J_n(|x|) for n = 0 or 1. */
static double bessel_j_pos(int n, double x_abs)
{
    if (x_abs <= 8.0) return bessel_j_series(n, x_abs);
    return bessel_j_asymp(n, x_abs);
}

/* --- Public API --- */

double j0(double x)
{
    if (isnan(x)) return x;
    if (isinf(x)) return 0.0;
    double ax = fabs(x);
    if (ax == 0.0) return 1.0;
    return bessel_j_pos(0, ax);
}

double j1(double x)
{
    if (isnan(x)) return x;
    if (isinf(x)) return 0.0;
    double ax = fabs(x);
    if (ax == 0.0) return 0.0;
    double r = bessel_j_pos(1, ax);
    return (x < 0.0) ? -r : r;
}

double y0(double x)
{
    if (isnan(x)) return x;
    if (x < 0.0) { feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0) return -INFINITY;
    if (isinf(x)) return 0.0;
    if (x <= 8.0) return bessel_y0_series(x);
    return bessel_y_asymp(0, x);
}

double y1(double x)
{
    if (isnan(x)) return x;
    if (x < 0.0) { feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0) return -INFINITY;
    if (isinf(x)) return 0.0;
    if (x <= 8.0) return bessel_y1_series(x);
    return bessel_y_asymp(1, x);
}

double jn(int n, double x)
{
    if (isnan(x)) return x;

    /* Reduce to non-negative order via J_{-n}(x) = (-1)^n J_n(x). */
    int sign_n = 1;
    if (n < 0) {
        n = -n;
        if (n & 1) sign_n = -sign_n;
    }

    /* Reduce to non-negative argument via J_n(-x) = (-1)^n J_n(x). */
    int sign_x = 1;
    if (x < 0.0) {
        x = -x;
        if (n & 1) sign_x = -sign_x;
    }

    if (isinf(x)) return 0.0;
    if (n == 0) return sign_n * sign_x * j0(x);
    if (n == 1) return sign_n * sign_x * bessel_j_pos(1, x);
    if (x == 0.0) return 0.0;

    double result;
    if ((double)n <= x) {
        /* Forward recurrence — stable when n <= x. */
        double fkm1 = bessel_j_pos(0, x);
        double fk   = bessel_j_pos(1, x);
        for (int k = 1; k < n; k++) {
            double fkp1 = (2.0 * (double)k / x) * fk - fkm1;
            fkm1 = fk;
            fk   = fkp1;
        }
        result = fk;
    } else {
        /* Miller's backward recurrence — stable when n > x.
           Pick m well above n; round to even so the normalisation
           sum (J_0 + 2(J_2+J_4+...) = 1) captures only even-indexed
           terms cleanly. */
        int m = n + (int)sqrt(40.0 * (double)n);
        if (m < n + 20) m = n + 20;
        m = (m + 1) & ~1;

        double fkp1 = 0.0;
        double fk   = 1.0;
        double saved = 0.0;
        double sum_even = 0.0;
        for (int k = m; k >= 1; k--) {
            double fkm1 = (2.0 * (double)k / x) * fk - fkp1;
            fkp1 = fk;
            fk   = fkm1;
            int idx = k - 1;
            if (idx == n) saved = fk;
            if (idx > 0 && (idx & 1) == 0) sum_even += fk;
            if (fabs(fk) > 1.0e100) {
                fk        *= 1.0e-100;
                fkp1      *= 1.0e-100;
                saved     *= 1.0e-100;
                sum_even  *= 1.0e-100;
            }
        }
        /* fk is now f_0; renormalise so that J_0 + 2(J_2+...) = 1. */
        double norm = fk + 2.0 * sum_even;
        result = saved / norm;
    }
    return sign_n * sign_x * result;
}

double yn(int n, double x)
{
    if (isnan(x)) return x;
    if (x < 0.0) { feraiseexcept(FE_INVALID); return NAN; }

    int sign_n = 1;
    if (n < 0) {
        n = -n;
        if (n & 1) sign_n = -sign_n;
    }

    if (x == 0.0) return -INFINITY;
    if (isinf(x)) return 0.0;
    if (n == 0) return sign_n * y0(x);
    if (n == 1) return sign_n * y1(x);

    /* Y_n forward recurrence is always stable. */
    double ykm1 = y0(x);
    double yk   = y1(x);
    for (int k = 1; k < n; k++) {
        double ykp1 = (2.0 * (double)k / x) * yk - ykm1;
        ykm1 = yk;
        yk   = ykp1;
        if (!isfinite(yk)) break;            /* Y grows fast for n >> x */
    }
    return sign_n * yk;
}


int ufromfpx(unsigned int *y, double x, fenv_t *envp, int rounding_mode) {
    return ufromfp(y, x, envp, rounding_mode);
}
