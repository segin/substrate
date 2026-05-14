/*
 * lib/m/src/cmath_explog.c — complex exp / log / sqrt / pow.
 *
 * Twelve entries (cexp / clog / csqrt / cpow × float/double/long-double)
 * implemented in closed form on top of substrate's real-math primitives
 * (exp, log, log1p, sin, cos, hypot, atan2, sqrt, copysign, fabs).
 *
 * Identities:
 *
 *   cexp(z) = exp(x) * (cos(y) + i*sin(y))
 *
 *   clog(z) = log|z| + i*arg(z)
 *           = (1/2)*log(x^2 + y^2) + i*atan2(y, x)
 *     For |z| near 1 the direct log loses bits to cancellation; use
 *     log1p(|z|^2 - 1) / 2 in that band.
 *
 *   csqrt(z) — principal branch (real part >= 0, imag-sign tracks y):
 *     let R = hypot(x, y)
 *     if x >= 0:
 *         u = sqrt((R + x) / 2)
 *         v = (u == 0) ? 0 : y / (2u)
 *     else:
 *         v_mag = sqrt((R - x) / 2)
 *         u = (v_mag == 0) ? 0 : fabs(y) / (2 * v_mag)
 *         v = copysign(v_mag, y)
 *     return u + i*v
 *
 *   cpow(x, y) = cexp(y * clog(x))    [principal value]
 *     Standard convention: cpow(0+0i, 0+0i) = 1+0i to match real pow().
 *
 * Special-value handling per C99 G.6.3 covers the obvious cases
 * (zeros, infinities, NaN propagation).  Exhaustive Annex G coverage
 * is pages of spec text; we handle the cases real callers hit and
 * leave the more exotic ones (±0 sign tracking through cexp's
 * Inf * 0 inner products, etc.) to a future polish pass.
 */

#include <complex.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================
 * cexp
 * ============================================================ */

double complex cexp(double complex z) {
    double x = __real__ z, y = __imag__ z;

    /* NaN propagation: NaN in real or imag part (and the other not
     * an inf) -> NaN+iNaN. */
    if (isnan(x) || isnan(y)) {
        /* cexp(NaN+0i) preserves the zero imaginary; everything
         * else produces NaN+iNaN. */
        if (isnan(x) && y == 0.0) return CMPLX(NAN, y);
        return CMPLX(NAN, NAN);
    }

    /* Real part infinite: handle separately so we don't compute
     * exp(+inf)*0 = NaN when y == 0. */
    if (isinf(x)) {
        if (x > 0.0) {
            if (y == 0.0) return CMPLX(INFINITY, y);    /* +inf + 0i */
            if (isinf(y)) return CMPLX(INFINITY, NAN);  /* dir undefined */
            return CMPLX(INFINITY * cos(y), INFINITY * sin(y));
        } else {
            /* x = -inf: result magnitude is 0 */
            if (isinf(y)) return CMPLX(0.0, 0.0);
            return CMPLX(0.0 * cos(y), 0.0 * sin(y));
        }
    }

    /* Finite x.  exp(x) over the double range is well-behaved. */
    double r = exp(x);
    return CMPLX(r * cos(y), r * sin(y));
}

/* ============================================================
 * clog
 * ============================================================ */

double complex clog(double complex z) {
    double x = __real__ z, y = __imag__ z;

    /* clog(0+0i) is a pole. */
    if (x == 0.0 && y == 0.0) {
        return CMPLX(-INFINITY, atan2(y, x));   /* atan2 picks the sign */
    }

    double re;
    /* For |z| close to 1 the direct log(hypot) loses bits.  Switch
     * to log1p(|z|^2 - 1) / 2 inside a narrow band. */
    double r2 = x * x + y * y;     /* may overflow for huge z */
    if (r2 > 0.5 && r2 < 3.0) {
        re = 0.5 * log1p(r2 - 1.0);
    } else {
        re = log(hypot(x, y));
    }
    double im = atan2(y, x);
    return CMPLX(re, im);
}

/* ============================================================
 * csqrt — principal branch.
 * ============================================================ */

double complex csqrt(double complex z) {
    double x = __real__ z, y = __imag__ z;

    /* csqrt(0+0i) = 0+0i (sign of zero on imag preserved). */
    if (x == 0.0 && y == 0.0) return CMPLX(0.0, y);

    /* Inf handling: csqrt(+inf + iy) = +inf + iy for any y,
     *               csqrt(-inf + iy) = 0 + i*inf  for finite y,
     *               csqrt(NaN+iy) = NaN+NaN unless y is +/-inf. */
    if (isinf(x)) {
        if (x > 0.0) {
            if (isnan(y)) return CMPLX(INFINITY, NAN);
            return CMPLX(INFINITY, isinf(y) ? y : copysign(0.0, y));
        }
        /* x = -inf */
        if (isnan(y)) return CMPLX(NAN, INFINITY);
        return CMPLX(isinf(y) ? INFINITY : 0.0, copysign(INFINITY, y));
    }
    if (isinf(y)) return CMPLX(INFINITY, y);
    if (isnan(x) || isnan(y)) return CMPLX(NAN, NAN);

    double R = hypot(x, y);

    if (x >= 0.0) {
        double u = sqrt(0.5 * (R + x));
        double v = (u == 0.0) ? 0.0 : 0.5 * y / u;
        return CMPLX(u, v);
    } else {
        double v_mag = sqrt(0.5 * (R - x));
        double u = (v_mag == 0.0) ? 0.0 : 0.5 * fabs(y) / v_mag;
        double v = copysign(v_mag, y == 0.0 ? 1.0 : y);
        return CMPLX(u, v);
    }
}

/* ============================================================
 * cpow
 * ============================================================ */

double complex cpow(double complex x, double complex y) {
    /* C convention (matches real pow): 0^0 = 1. */
    if (__real__ x == 0.0 && __imag__ x == 0.0 &&
        __real__ y == 0.0 && __imag__ y == 0.0) {
        return CMPLX(1.0, 0.0);
    }
    /* cpow(0, y) with y having positive real part -> 0; otherwise
     * pole / NaN.  Let cexp(y * clog(x)) handle the algebra; the
     * clog produces -inf for the real part which routes correctly. */
    return cexp(y * clog(x));
}

/* ============================================================
 * float / long double wrappers
 * ============================================================ */

float complex cexpf(float complex z)  { return (float complex) cexp((double complex)z); }
float complex clogf(float complex z)  { return (float complex) clog((double complex)z); }
float complex csqrtf(float complex z) { return (float complex)csqrt((double complex)z); }
float complex cpowf(float complex a, float complex b) {
    return (float complex)cpow((double complex)a, (double complex)b);
}

long double complex cexpl(long double complex z)  { return (long double complex) cexp((double complex)z); }
long double complex clogl(long double complex z)  { return (long double complex) clog((double complex)z); }
long double complex csqrtl(long double complex z) { return (long double complex)csqrt((double complex)z); }
long double complex cpowl(long double complex a, long double complex b) {
    return (long double complex)cpow((double complex)a, (double complex)b);
}
