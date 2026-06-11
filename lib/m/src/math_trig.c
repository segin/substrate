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
 * atan(x) - Arctangent via the x87 fpatan instruction.
 *
 * atan(x) == atan2(x, 1), so `fld1; fldl x; fpatan` computes
 * atan(st(0)/st(1)) = atan(x/1) to full hardware precision across the
 * whole domain.  This replaces a 100-term Taylor series that lost
 * accuracy near |x| = 1 (slow convergence) — and in turn improves
 * asin/acos and the *pi variants that are built on atan.
 */
double atan(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return (x < 0) ? -M_PI_2 : M_PI_2;
    if (x == 0.0) return x;   /* preserve sign of zero (atan is odd) */

    /* fpatan computes atan(st(1)/st(0)).  Load x then 1.0 so that
     * st(1)=x, st(0)=1 and the result is atan(x/1) = atan(x). */
    double res;
    __asm__ __volatile__("fldl %1; fld1; fpatan; fstpl %0"
                         : "=m"(res) : "m"(x));
    return res;
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
    if (isinf(x) || x < -1.0 || x > 1.0) {   /* domain error */
        feraiseexcept(FE_INVALID);
        errno = EDOM;
        return NAN;
    }
    if (x == 0.0) return x;
    if (x == 1.0) return M_PI_2;
    if (x == -1.0) return -M_PI_2;
    return atan(x / sqrt(1.0 - x * x));
}

/* acos(x) = pi/2 - asin(x) */
double acos(double x) {
    if (isnan(x)) return x;
    if (isinf(x) || x < -1.0 || x > 1.0) {   /* domain error */
        feraiseexcept(FE_INVALID);
        errno = EDOM;
        return NAN;
    }
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
    if (isinf(x) || x < -1.0 || x > 1.0) {   /* domain error */
        feraiseexcept(FE_INVALID);
        errno = EDOM;
        return NAN;
    }
    if (x == 0.0) return x;
    if (x == 1.0) return 0.5;
    if (x == -1.0) return -0.5;
    return asin(x) / M_PI;
}

double acospi(double x) {
    if (isnan(x)) return x;
    if (isinf(x) || x < -1.0 || x > 1.0) {   /* domain error */
        feraiseexcept(FE_INVALID);
        errno = EDOM;
        return NAN;
    }
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
