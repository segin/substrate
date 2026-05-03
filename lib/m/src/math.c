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

/* expm1(x) = e^x - 1, accurate for small x */
double expm1(double x) {
    if (fabs(x) < 1e-9) return x + 0.5 * x * x;  /* Taylor for small x */
    return exp(x) - 1.0;
}

/*
 * log(x) - natural logarithm using Newton-Raphson on exp
 * Uses identity: log(x) = 2 * atanh((x-1)/(x+1)) for x > 0
 */
double log(double x) {
    double res;
    __asm__ __volatile__("fldln2; fldl %1; fyl2x; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

/* log2(x) = log(x) / ln(2) */
double log2(double x) {
    double res;
    __asm__ __volatile__("fld1; fldl %1; fyl2x; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

/* log10(x) = log(x) / ln(10) */
double log10(double x) {
    double res;
    __asm__ __volatile__("fldlg2; fldl %1; fyl2x; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

/* log1p(x) = log(1+x), accurate for small x */
double log1p(double x) {
    if (fabs(x) < 1e-9) return x - 0.5 * x * x;  /* Taylor for small x */
    return log(1.0 + x);
}

/* pow(x, y) = x^y = e^(y * log(x)) */
double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 1.0) return 1.0;
    if (x == 0.0) return (y > 0) ? 0.0 : INFINITY;
    if (isnan(x) || isnan(y)) return NAN;

    if (x < 0.0) {
        /* Negative base: only valid if y is an integer */
        double yi;
        if (modf(y, &yi) != 0.0) return NAN;
        double res = pow(-x, y);
        if (((long long)yi) % 2) return -res;
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

/* cbrt(x) - Cube root using Newton-Raphson */
double cbrt(double x) {
    if (x == 0) return 0.0;
    
    int neg = (x < 0);
    if (neg) x = -x;
    
    /* Initial guess */
    double guess = x * 0.5;
    if (guess == 0) guess = 1.0;
    
    /* Newton-Raphson: x_{n+1} = (2*x_n + S/x_n^2) / 3 */
    for (int i = 0; i < 20; i++) {
        double next = (2.0 * guess + x / (guess * guess)) / 3.0;
        if (fabs(next - guess) < 1e-15 * fabs(guess)) break;
        guess = next;
    }
    
    return neg ? -guess : guess;
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

/* nextafter: next representable value after x towards y */
double nextafter(double x, double y) {
    if (isnan(x) || isnan(y)) return NAN;
    if (x == y) return y;
    
    union { double d; uint64_t u; } u = { .d = x };
    
    if (x == 0.0) {
        /* Smallest subnormal */
        u.u = 1;
        return (y > 0) ? u.d : -u.d;
    }
    
    if ((x > 0) == (y > x)) {
        u.u++;
    } else {
        u.u--;
    }
    return u.d;
}

/* copysign: magnitude of x with sign of y */
double copysign(double x, double y) {
    union { double d; uint64_t u; } ux = { .d = x }, uy = { .d = y };
    ux.u = (ux.u & 0x7FFFFFFFFFFFFFFFULL) | (uy.u & 0x8000000000000000ULL);
    return ux.d;
}

/* Absolute value - actual implementation */
double fabs(double x) {
    double res;
    __asm__ __volatile__("fldl %1; fabs; fstpl %0" : "=m"(res) : "m"(x));
    return res;
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
    return (x > y) ? x : y;
}

double fmin(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
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

/* Rounding functions */
double ceil(double x) {
    int i = (int)x;
    return (x > i) ? (double)(i + 1) : (double)i;
}

double floor(double x) {
    int i = (int)x;
    return (x < i) ? (double)(i - 1) : (double)i;
}

double trunc(double x) {
    return (double)(int)x;
}

double round(double x) {
    return (x >= 0) ? floor(x + 0.5) : ceil(x - 0.5);
}

double rint(double x) {
    double res;
    __asm__ __volatile__("fldl %1; frndint; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

double nearbyint(double x) {
    return rint(x); // x87 frndint honors CW but doesn't necessarily raise inexact if masked
}

long lrint(double x) {
    long res;
    __asm__ __volatile__("fldl %1; fistpl %0" : "=m"(res) : "m"(x));
    return res;
}

long long llrint(double x) {
    long long res;
    __asm__ __volatile__("fldl %1; fistpq %0" : "=m"(res) : "m"(x));
    return res;
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
