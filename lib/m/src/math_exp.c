/*
 * math_exp.c — exponential, logarithmic, and power functions.
 *
 * Substrate libm targets i386 with x87.  Every primitive here either
 * dispatches into a single x87 instruction (fsqrt, fyl2x, fyl2xp1,
 * f2xm1, fscale) or reduces the argument to a range where one of
 * those primitives gives full double precision.
 *
 * Special-value semantics follow C99 Annex F (with C23 additions for
 * the *m1 / *p1 / pown / powr / rootn / compound / rsqrt family).
 * Where the spec says "raise FE_INVALID", we call feraiseexcept().
 * Where it says "set errno = EDOM / ERANGE", we set errno; the two
 * notification flavours are independent and POSIX allows either or
 * both, so libm sets both for portability.
 */

#include <errno.h>
#include <fenv.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

#ifndef M_LN2
#define M_LN2     0.69314718055994530942
#endif
#ifndef M_LN10
#define M_LN10    2.30258509299404568402
#endif
#ifndef M_LOG2E
#define M_LOG2E   1.44269504088896340736
#endif
#ifndef M_LOG10E
#define M_LOG10E  0.43429448190325182765
#endif
#ifndef M_LOG2_10
#define M_LOG2_10 3.32192809488736234787  /* log2(10) */
#endif
#ifndef M_LOG10_2
#define M_LOG10_2 0.30102999566398119521  /* log10(2) */
#endif

/* x87 exponent threshold.  fscale takes the integer part of st(1) as
 * its exponent.  fyl2x's input range is (0, inf) and f2xm1's is
 * [-1, 1].  These bracket the domain we care about. */

/* -------------------------------------------------------------------- *
 * Helpers
 * -------------------------------------------------------------------- */

/* x87 building block: compute 2^x for any finite x.  Splits x = i + f
 * with i = round-to-nearest(x) and f in [-0.5, 0.5] (well inside the
 * f2xm1 domain of [-1, 1]), then
 *   2^x = 2^i * 2^f = scale(2^f, i)
 * f2xm1 returns 2^f - 1 to preserve precision; we add 1 back, then
 * fscale by i.
 *
 * The operand order on `fsub %st(N), %st(0)` matters: GAS encodes
 * `fsub %st(0), %st(N)` as the REVERSE-subtract opcode (DC E0+N =
 * FSUBR), not what we want.  Use `fsub %st(N), %st(0)` which encodes
 * as D8 E0+N = FSUB ST(0), ST(N) = st0 = st0 - stN, which is the
 * direction we want when st0 holds x and st1 holds i. */
static inline double x87_exp2(double x) {
    double res;
    __asm__ __volatile__(
        "fldl   %1\n\t"             /* st0 = x */
        "fld    %%st(0)\n\t"        /* st0 = x ;  st1 = x */
        "frndint\n\t"               /* st0 = i ;  st1 = x      (round-to-nearest by default) */
        "fxch   %%st(1)\n\t"        /* st0 = x ;  st1 = i */
        "fsub   %%st(1), %%st(0)\n\t" /* st0 = x - i = f */
        "f2xm1\n\t"                 /* st0 = 2^f - 1 ; st1 = i */
        "fld1\n\t"
        "faddp\n\t"                 /* st0 = 2^f ; st1 = i */
        "fscale\n\t"                /* st0 = 2^f * 2^i = 2^x ; st1 = i */
        "fstp   %%st(1)\n\t"        /* drop i */
        "fstpl  %0"
        : "=m"(res)
        : "m"(x));
    return res;
}

/* -------------------------------------------------------------------- *
 * exp / exp2 / exp10 family
 * -------------------------------------------------------------------- */

double exp(double x) {
    /* IEEE-754 / C99 F.9.3.1:
     *   exp(±0)   = 1
     *   exp(-inf) = +0
     *   exp(+inf) = +inf
     *   exp(NaN)  = NaN
     * Overflow when x * log2(e) > finite_dbl_max_exp ≈ 1024; set
     * errno=ERANGE and return +inf.  Underflow when result rounds
     * to 0; set errno=ERANGE and return +0. */
    if (__builtin_isnan(x)) return x;
    if (x == 0.0) return 1.0;
    if (__builtin_isinf(x)) return (x > 0) ? INFINITY : 0.0;
    if (x >  709.7827128933840) { errno = ERANGE; return INFINITY; }
    if (x < -745.1332191019412) { errno = ERANGE; return 0.0; }

    return x87_exp2(x * M_LOG2E);
}

double exp2(double x) {
    if (__builtin_isnan(x)) return x;
    if (x == 0.0) return 1.0;
    if (__builtin_isinf(x)) return (x > 0) ? INFINITY : 0.0;
    if (x >  1023.0) { errno = ERANGE; return INFINITY; }
    if (x < -1074.0) { errno = ERANGE; return 0.0; }

    return x87_exp2(x);
}

double exp10(double x) {
    if (__builtin_isnan(x)) return x;
    if (x == 0.0) return 1.0;
    if (__builtin_isinf(x)) return (x > 0) ? INFINITY : 0.0;
    if (x >  308.2547155599167) { errno = ERANGE; return INFINITY; }
    if (x < -323.6068000000000) { errno = ERANGE; return 0.0; }

    /* 10^x = 2^(x * log2(10)) */
    return x87_exp2(x * M_LOG2_10);
}

/* expm1(x) = e^x - 1, accurate near 0 where exp(x)-1 cancels.
 *
 * For |x| > ~0.5*ln(2) ≈ 0.347, exp(x)-1 has enough headroom that
 * direct subtraction is fine.  For smaller x we use a 14-term
 * series of e^x - 1 (Horner form):
 *   e^x - 1 = x + x^2/2! + x^3/3! + ...
 * which converges fast in the reduced range. */
double expm1(double x) {
    if (__builtin_isnan(x)) return x;
    if (__builtin_isinf(x)) return (x > 0) ? INFINITY : -1.0;
    if (x == 0.0) return x;                  /* preserve sign of zero */

    if (__builtin_fabs(x) > 0.347) {
        if (x >  709.7827128933840) { errno = ERANGE; return INFINITY; }
        return exp(x) - 1.0;
    }

    /* Horner-Form 14-term series for e^x - 1, accurate to ~1 ulp
     * over |x| <= 0.347. */
    static const double c[] = {
        1.0 / 1.0,                /* x^1 */
        1.0 / 2.0,                /* x^2 */
        1.0 / 6.0,                /* x^3 */
        1.0 / 24.0,               /* x^4 */
        1.0 / 120.0,              /* x^5 */
        1.0 / 720.0,              /* x^6 */
        1.0 / 5040.0,             /* x^7 */
        1.0 / 40320.0,            /* x^8 */
        1.0 / 362880.0,           /* x^9 */
        1.0 / 3628800.0,          /* x^10 */
        1.0 / 39916800.0,         /* x^11 */
        1.0 / 479001600.0,        /* x^12 */
        1.0 / 6227020800.0,       /* x^13 */
        1.0 / 87178291200.0,      /* x^14 */
    };
    double y = c[13];
    for (int i = 12; i >= 0; i--) y = y * x + c[i];
    return x * y;
}

/* exp2m1(x) = 2^x - 1.  For |x| <= 1 a single f2xm1 gives full
 * double precision; outside that range fall back. */
double exp2m1(double x) {
    if (__builtin_isnan(x)) return x;
    if (__builtin_isinf(x)) return (x > 0) ? INFINITY : -1.0;
    if (x == 0.0) return x;

    if (__builtin_fabs(x) <= 1.0) {
        double res;
        __asm__ __volatile__(
            "fldl  %1\n\t"
            "f2xm1\n\t"
            "fstpl %0"
            : "=m"(res) : "m"(x));
        return res;
    }
    return exp2(x) - 1.0;
}

/* exp10m1(x) = 10^x - 1.  For |x| <= 0.15 (where 10^x <= ~1.41) use
 * the M_LN10 expansion of e^(x*ln10) - 1 to keep precision; else fall
 * back. */
double exp10m1(double x) {
    if (__builtin_isnan(x)) return x;
    if (__builtin_isinf(x)) return (x > 0) ? INFINITY : -1.0;
    if (x == 0.0) return x;

    if (__builtin_fabs(x) > 0.15) {
        return exp10(x) - 1.0;
    }
    /* 10^x - 1 = expm1(x * ln(10)) — and we already have a good
     * expm1 for small arguments. */
    return expm1(x * M_LN10);
}

/* -------------------------------------------------------------------- *
 * log / log2 / log10 family
 * -------------------------------------------------------------------- */

double log(double x) {
    if (__builtin_isnan(x)) return x;
    if (x < 0.0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__builtin_isinf(x)) return x;  /* +inf -> +inf */

    /* fyl2x computes st1 * log2(st0); push ln(2) so st1 * log2(x)
     * = ln(2) * log2(x) = ln(x). */
    double res;
    __asm__ __volatile__(
        "fldln2\n\t"
        "fldl   %1\n\t"
        "fyl2x\n\t"
        "fstpl  %0"
        : "=m"(res) : "m"(x));
    return res;
}

double log2(double x) {
    if (__builtin_isnan(x)) return x;
    if (x < 0.0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__builtin_isinf(x)) return x;

    double res;
    __asm__ __volatile__(
        "fld1\n\t"
        "fldl   %1\n\t"
        "fyl2x\n\t"
        "fstpl  %0"
        : "=m"(res) : "m"(x));
    return res;
}

double log10(double x) {
    if (__builtin_isnan(x)) return x;
    if (x < 0.0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__builtin_isinf(x)) return x;

    double res;
    __asm__ __volatile__(
        "fldlg2\n\t"                /* log10(2) */
        "fldl   %1\n\t"
        "fyl2x\n\t"                 /* log10(2) * log2(x) = log10(x) */
        "fstpl  %0"
        : "=m"(res) : "m"(x));
    return res;
}

/* log1p(x) = ln(1+x), accurate near 0.
 *
 * x87 has fyl2xp1 which computes st1 * log2(1 + st0) for |st0| <
 * 1 - sqrt(2)/2 ≈ 0.293.  Push ln(2) and we get ln(1+x) directly.
 * Outside that range fall back to log(1+x). */
double log1p(double x) {
    if (__builtin_isnan(x)) return x;
    if (x < -1.0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == -1.0) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__builtin_isinf(x)) return x;
    if (x == 0.0) return x;

    if (__builtin_fabs(x) < 0.2928) {
        double res;
        __asm__ __volatile__(
            "fldln2\n\t"
            "fldl    %1\n\t"
            "fyl2xp1\n\t"
            "fstpl   %0"
            : "=m"(res) : "m"(x));
        return res;
    }
    return log(1.0 + x);
}

/* logp1 is the C23 alias for log1p. */
double logp1(double x) { return log1p(x); }

double log2p1(double x) {
    if (__builtin_isnan(x)) return x;
    if (x < -1.0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == -1.0) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__builtin_isinf(x)) return x;
    if (x == 0.0) return x;

    if (__builtin_fabs(x) < 0.2928) {
        double res;
        __asm__ __volatile__(
            "fld1\n\t"
            "fldl    %1\n\t"
            "fyl2xp1\n\t"
            "fstpl   %0"
            : "=m"(res) : "m"(x));
        return res;
    }
    return log2(1.0 + x);
}

double log10p1(double x) {
    if (__builtin_isnan(x)) return x;
    if (x < -1.0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == -1.0) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__builtin_isinf(x)) return x;
    if (x == 0.0) return x;

    if (__builtin_fabs(x) < 0.2928) {
        double res;
        __asm__ __volatile__(
            "fldlg2\n\t"
            "fldl    %1\n\t"
            "fyl2xp1\n\t"
            "fstpl   %0"
            : "=m"(res) : "m"(x));
        return res;
    }
    return log10(1.0 + x);
}

/* -------------------------------------------------------------------- *
 * sqrt / rsqrt / cbrt / hypot
 * -------------------------------------------------------------------- */

double sqrt(double x) {
    if (__builtin_isnan(x)) return x;
    if (x < 0.0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    /* sqrt(+0) = +0, sqrt(-0) = -0 — preserve sign of zero. */
    if (x == 0.0) return x;
    if (__builtin_isinf(x)) return x;  /* sqrt(+inf) = +inf */

    double res;
    __asm__ __volatile__("fldl %1; fsqrt; fstpl %0" : "=m"(res) : "m"(x));
    return res;
}

double rsqrt(double x) {
    if (__builtin_isnan(x)) return x;
    if (x < 0.0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return INFINITY; }
    if (__builtin_isinf(x)) return 0.0;

    double res;
    /* GAS swap quirk on fdiv/fdivrp + two-register operands — avoid by
     * using two-operand `fdiv` with explicit destination ST(0). */
    __asm__ __volatile__(
        "fldl   %1\n\t"             /* st0 = x */
        "fsqrt\n\t"                 /* st0 = sqrt(x) */
        "fld1\n\t"                  /* st0 = 1 ; st1 = sqrt(x) */
        "fdiv   %%st(1), %%st(0)\n\t" /* st0 = st0 / st1 = 1 / sqrt(x) */
        "fstp   %%st(1)\n\t"        /* drop sqrt(x) */
        "fstpl  %0"
        : "=m"(res) : "m"(x));
    return res;
}

/* cbrt(x) - cube root.
 *
 * Use frexp() to reduce x = m * 2^e with m in [0.5, 1).  Write
 * e = 3q + r with r in {0, 1, 2}; then
 *   cbrt(x) = cbrt(m * 2^r) * 2^q
 * The argument m * 2^r lies in [0.5, 4), a tight enough range to
 * seed a 6-iteration Newton-Raphson refinement.
 *
 * Re-applying 2^q uses scalbn() so the result handles the entire
 * double range, not just |q| < 63 like a naive 1ULL<<q shift would. */
double cbrt(double x) {
    if (__builtin_isnan(x)) return x;
    if (__builtin_isinf(x)) return x;
    if (x == 0.0) return x;

    int neg = (x < 0.0);
    if (neg) x = -x;

    int e;
    double m = frexp(x, &e);   /* m in [0.5, 1) */

    /* Decompose e = 3q + r, r in {0, 1, 2}.  C's signed % is
     * truncating so do it by hand. */
    int q, r;
    if (e >= 0) {
        q = e / 3;
        r = e - 3 * q;
    } else {
        q = -((-e + 2) / 3);
        r = e - 3 * q;
    }
    static const double pow2_r[3] = { 1.0, 2.0, 4.0 };
    double mr = m * pow2_r[r];          /* mr in [0.5, 4) */

    /* Halley seed: cbrt(x) ≈ x * (2 + cbrt(mr))/(1 + 2 * cbrt(mr)).
     * Use a coarse quadratic fit of cbrt over [0.5, 4) to within
     * ~5% — Newton iteration then locks on quickly. */
    double g = 0.5874 + mr * (0.4196 - 0.04134 * mr);

    /* Newton-Raphson: g_{n+1} = (2*g + mr/g^2) / 3.  Converges
     * cubically from a half-percent seed — 5 iterations suffice. */
    for (int i = 0; i < 6; i++) {
        double g2 = g * g;
        g = (2.0 * g + mr / g2) / 3.0;
    }

    double result = scalbn(g, q);
    return neg ? -result : result;
}

/* hypot(x, y) = sqrt(x^2 + y^2) without spurious overflow when either
 * argument is large or one is much larger than the other. */
double hypot(double x, double y) {
    if (__builtin_isinf(x) || __builtin_isinf(y)) return INFINITY;
    if (__builtin_isnan(x) || __builtin_isnan(y)) return NAN;

    x = __builtin_fabs(x);
    y = __builtin_fabs(y);
    if (x < y) { double t = x; x = y; y = t; }
    if (x == 0.0) return 0.0;
    double r = y / x;
    return x * sqrt(1.0 + r * r);
}

/* -------------------------------------------------------------------- *
 * pow / pown / powr / rootn / compound
 * -------------------------------------------------------------------- */

/* Is y an integer value (and if so, is it odd)?  Returns:
 *   0  — y is not an integer
 *   1  — y is an even integer
 *   2  — y is an odd integer
 * For |y| above ~2^53 every double is integer-valued; treat such
 * monsters as even (their representable values are all even). */
static int int_kind(double y) {
    if (__builtin_isinf(y) || __builtin_isnan(y)) return 0;
    double ay = __builtin_fabs(y);
    if (ay >= 9.007199254740992e15) return 1;  /* 2^53 — definitely even */
    double yi;
    if (modf(y, &yi) != 0.0) return 0;
    /* yi fits in long long because ay < 2^53.  Test low bit. */
    long long ll = (long long)yi;
    return (ll & 1) ? 2 : 1;
}

/* pow(x, y) — full C99 Annex F.9.4.4 special-case table. */
double pow(double x, double y) {
    /* Per F.9.4.4: pow(x, +/-0) is 1 for ANY x, including NaN. */
    if (y == 0.0) return 1.0;
    /* pow(1, y) = 1 for ANY y, including NaN. */
    if (x == 1.0) return 1.0;

    if (__builtin_isnan(x) || __builtin_isnan(y)) return NAN;

    int ik = int_kind(y);
    int y_odd_int = (ik == 2);

    if (__builtin_isinf(y)) {
        double ax = __builtin_fabs(x);
        if (ax == 1.0) return 1.0;     /* pow(±1, ±inf) = 1 */
        if (ax < 1.0)  return (y > 0) ? 0.0      : INFINITY;
        /* ax > 1.0 */ return (y > 0) ? INFINITY : 0.0;
    }

    if (x == 0.0) {
        if (y < 0.0) {
            /* Pole error. */
            errno = ERANGE;
            feraiseexcept(FE_DIVBYZERO);
            /* signbit handling: pow(-0, odd negative) = -inf */
            if (y_odd_int && __builtin_signbit(x)) return -INFINITY;
            return INFINITY;
        }
        /* y > 0 */
        if (y_odd_int && __builtin_signbit(x)) return -0.0;
        return 0.0;
    }

    if (__builtin_isinf(x)) {
        if (x > 0.0) return (y > 0.0) ? INFINITY : 0.0;
        /* x = -inf */
        if (y > 0.0) return y_odd_int ? -INFINITY : INFINITY;
        return y_odd_int ? -0.0 : 0.0;
    }

    if (x < 0.0) {
        if (ik == 0) {
            /* Non-integer y with negative base: domain error. */
            errno = EDOM;
            feraiseexcept(FE_INVALID);
            return NAN;
        }
        /* |x|^y, then flip sign if y is odd integer. */
        double r = pow(-x, y);
        return y_odd_int ? -r : r;
    }

    /* General case: x > 0 finite, y finite non-zero.
     * x^y = 2^(y * log2(x)) — single fyl2x + the x87_exp2 helper. */
    double yl2x;
    __asm__ __volatile__(
        "fldl   %2\n\t"             /* y */
        "fldl   %1\n\t"             /* x */
        "fyl2x\n\t"                 /* st0 = y * log2(x) */
        "fstpl  %0"
        : "=m"(yl2x) : "m"(x), "m"(y));

    if (yl2x >  1023.0) { errno = ERANGE; return  INFINITY; }
    if (yl2x < -1074.0) { errno = ERANGE; return  0.0; }
    return x87_exp2(yl2x);
}

/* pown(x, n) - integer n.  Differs from pow() in that the sign of
 * the result for negative bases is well-defined.  Binary exponentiation. */
double pown(double x, intmax_t n) {
    if (n == 0) return 1.0;             /* pown(x, 0) = 1 for any x */
    if (__builtin_isnan(x)) return x;
    if (x == 0.0) {
        if (n < 0) {
            errno = ERANGE;
            feraiseexcept(FE_DIVBYZERO);
            if ((n & 1) && __builtin_signbit(x)) return -INFINITY;
            return INFINITY;
        }
        if ((n & 1) && __builtin_signbit(x)) return -0.0;
        return 0.0;
    }
    if (__builtin_isinf(x)) {
        if (x > 0) return (n > 0) ? INFINITY : 0.0;
        if (n > 0) return (n & 1) ? -INFINITY : INFINITY;
        return (n & 1) ? -0.0 : 0.0;
    }

    int negn = (n < 0);
    /* INTMAX_MIN can't be negated in two's complement; treat
     * separately by stepping once before flipping. */
    uintmax_t un;
    if (negn) {
        if (n == INTMAX_MIN) {
            un = (uintmax_t)INTMAX_MAX + 1u;
        } else {
            un = (uintmax_t)(-n);
        }
    } else {
        un = (uintmax_t)n;
    }

    double result = 1.0;
    double base = x;
    while (un > 0) {
        if (un & 1) result *= base;
        un >>= 1;
        if (un) base *= base;
    }
    if (negn) result = 1.0 / result;
    return result;
}

/* powr(x, y) = e^(y * ln(x)), domain x >= 0.  C23 7.12.7.7. */
double powr(double x, double y) {
    /* powr corner cases (C23 F.10.4.7) — stricter than pow():
     *   powr(0, 0)        = NaN (FE_INVALID)
     *   powr(inf, 0)      = NaN (FE_INVALID)
     *   powr(1, inf)      = NaN (FE_INVALID)
     *   powr(x, NaN)      = NaN
     *   powr(NaN, y)      = NaN
     *   powr(x, 0) for x in (0, inf) = 1
     *   powr(0, y) for y > 0 = 0
     *   powr(0, y) for y < 0 = +inf (FE_DIVBYZERO)
     *   powr(neg, y)      = NaN (FE_INVALID)  for any y */
    if (__builtin_isnan(x) || __builtin_isnan(y)) return NAN;
    if (x < 0.0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }

    if (x == 0.0 && y == 0.0) {
        errno = EDOM; feraiseexcept(FE_INVALID); return NAN;
    }
    if (__builtin_isinf(x) && y == 0.0) {
        errno = EDOM; feraiseexcept(FE_INVALID); return NAN;
    }
    if (x == 1.0 && __builtin_isinf(y)) {
        errno = EDOM; feraiseexcept(FE_INVALID); return NAN;
    }

    if (y == 0.0) return 1.0;
    if (x == 0.0) {
        if (y > 0.0) return 0.0;
        errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return INFINITY;
    }
    if (__builtin_isinf(x)) return (y > 0) ? INFINITY : 0.0;
    if (__builtin_isinf(y)) {
        if (x == 1.0) return 1.0;
        if (x < 1.0)  return (y < 0) ? INFINITY : 0.0;
        return (y < 0) ? 0.0 : INFINITY;
    }
    return exp(y * log(x));
}

/* rootn(x, n) - n-th root.  rootn(x, 2) = sqrt(x), rootn(x, 3) =
 * cbrt(x).  Negative n means 1/rootn(x, -n). */
double rootn(double x, int n) {
    if (n == 0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (__builtin_isnan(x)) return x;
    if (n == 1) return x;
    if (n == 2) return sqrt(x);
    if (n == 3) return cbrt(x);
    if (n == -1) return 1.0 / x;

    if (x == 0.0) {
        if (n > 0) return (n & 1) ? x : 0.0;
        errno = ERANGE; feraiseexcept(FE_DIVBYZERO);
        return ((n & 1) && __builtin_signbit(x)) ? -INFINITY : INFINITY;
    }
    if (__builtin_isinf(x)) {
        if (x > 0) return (n > 0) ? INFINITY : 0.0;
        if (!(n & 1)) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
        return (n > 0) ? -INFINITY : -0.0;
    }

    if (n < 0) {
        double r = rootn(x, -n);
        return 1.0 / r;
    }

    if (x < 0.0) {
        if (!(n & 1)) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
        return -pow(-x, 1.0 / (double)n);
    }
    return pow(x, 1.0 / (double)n);
}

/* compound(x, n) = (1+x)^n, computed via exp(n * log1p(x)) so small
 * x stays accurate. */
double compound(double x, intmax_t n) {
    if (__builtin_isnan(x)) return x;
    if (x < -1.0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (n == 0) return 1.0;
    if (x == 0.0) return 1.0;
    if (x == -1.0) {
        if (n > 0) return 0.0;
        errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return INFINITY;
    }
    if (__builtin_isinf(x)) {
        if (x > 0) return (n > 0) ? INFINITY : 0.0;
        /* x = -inf: (1 + -inf)^n — argument < -1, already filtered. */
        return NAN;
    }

    /* Use exp(n * log1p(x)).  This is stable for small x (where
     * log1p captures the small-argument precision) and accurate for
     * moderate x.  For n outside intmax of exp's headroom we'll just
     * overflow to inf which is fine. */
    return exp((double)n * log1p(x));
}
