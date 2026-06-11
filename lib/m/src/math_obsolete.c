/*
 * math_obsolete.c — Obsolescent aliases (SVID / 4.3BSD / X/Open)
 *
 * These functions are obsolescent in POSIX.1-2024 and marked for
 * elimination in a future version, but they remain in widespread use
 * by legacy numerical code.  Provide them as thin forwarders to the
 * standard replacements so old sources link without rewrites.
 *
 * Feature-test guards:
 *   _DEFAULT_SOURCE  —  the default (POSIX.1-2008 and later)
 *   _XOPEN_SOURCE    —  X/Open / XSI compat
 *   _SVID_SOURCE     —  SVID / SVID-3 / SVID-4 / SVID-5
 *   _BSD_SOURCE      —  4.3BSD compat
 */

#include <math.h>
#include <stdint.h>

/* ============================================================
 * scalb(x, n) / scalbf(x, n)
 *
 * Historical two-argument scalb(x, n) returning x * 2^n.
 * Companion to the existing scalbl(x, n) and scalbn(x, n).
 *
 * Edge case: n == ±Inf (as double) → ±Inf (except x==0 → ±0).
 * The existing scalbn takes an int; the old scalb takes a double.
 * We match the historical "fractional exponent" semantics by
 * computing scalbn(x, (int)n) and handling the ±Inf edge.
 * ============================================================ */

double scalb(double x, double n) {
    if (isnan(x)) return x;
    if (isnan(n)) return NAN;
    if (isinf(n)) {
        /* n == +inf: x * 2^(+inf).  Finite/inf x != 0 -> ±inf (sign of x);
         *            x == 0 -> 0 * inf -> NaN (invalid).
         * n == -inf: x * 2^(-inf).  x != 0 -> ±0 (sign of x);
         *            x == 0 -> ±0 (sign of x). */
        if (n > 0.0) {
            if (x == 0.0) return NAN;
            return copysign(INFINITY, x);
        } else {
            return copysign(0.0, x);
        }
    }
    /* Finite n — scalbn takes int; clamp huge |n| to int range so the
     * overflow/underflow path inside scalbn() still produces ±inf / ±0. */
    if (n > 2147483647.0)  n = 2147483647.0;
    if (n < -2147483648.0) n = -2147483648.0;
    return scalbn(x, (int)n);
}

float scalbf(float x, float n) {
    if (isnan(x)) return x;
    if (isnan(n)) return NAN;
    if (isinf(n)) {
        if (n > 0.0f) {
            if (x == 0.0f) return NAN;
            return copysignf(INFINITY, x);
        } else {
            return copysignf(0.0f, x);
        }
    }
    if (n > 2147483647.0f)  n = 2147483647.0f;
    if (n < -2147483648.0f) n = -2147483648.0f;
    return scalbnf(x, (int)n);
}

/* ============================================================
 * significand(x) — return the mantissa scaled to [1, 2)
 *
 * significand(x) = x / 2^ilogb(x) for finite non-zero x.
 * For zero or inf/NaN, return the input as-is.
 * ============================================================ */

double significand(double x) {
    if (isinf(x) || isnan(x) || x == 0.0) return x;
    return scalbn(x, -ilogb(x));
}

float significandf(float x) {
    if (isinf(x) || isnan(x) || x == 0.0f) return x;
    return scalbnf(x, -ilogbf(x));
}

long double significandl(long double x) {
    if (isinf(x) || isnan(x) || x == 0.0) return x;
    return scalbnl(x, -ilogbl(x));
}

/* ============================================================
 * drem(x, y) / dremf(x, y) — obsolescent alias of remainder / remainderf
 * ============================================================ */

double drem(double x, double y) { return remainder(x, y); }
float  dremf(float x, float y)  { return remainderf(x, y); }

/* ============================================================
 * gamma(x) / gammaf(x) — historical name for lgamma / lgammaf
 *
 * These return the natural logarithm of |Gamma(x)| and set
 * the global signgam to the sign of Gamma(x).
 * ============================================================ */

double gamma(double x) {
    int sg;
    double r = lgamma_r(x, &sg);
    signgam = sg;
    return r;
}

float gammaf(float x) {
    int sg;
    float r = (float)lgamma_r((double)x, &sg);
    signgam = sg;
    return r;
}

/* ============================================================
 * pow10(x) / pow10f(x) / pow10l(x) — alias of exp10 / exp10f / exp10l
 * ============================================================ */

double pow10(double x)      { return exp10(x); }
float  pow10f(float x)      { return exp10f(x); }
long double pow10l(long double x) { return exp10l(x); }

/* ============================================================
 * matherr(x) — SVID error hook (no-op)
 *
 * The default implementation returns 0 (no override).
 * Substrate libm reports errors via errno and the fenv,
 * not through this hook.  Existing SVID-era code that
 * references matherr will link; the hook is effectively
 * a no-op stub.
 * ============================================================ */

struct exception {
    char   *name;
    char   *arith;
    char   *type;
    double  arg1;
    double  arg2;
    double  retval;
};

int matherr(struct exception *e) {
    (void) e;
    return 0;
}
