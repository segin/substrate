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
        /* The imaginary part carries the sign of y, tracked via the
         * actual sign BIT (signbit) so that y == -0.0 maps to a
         * negative result: C99 Annex G requires csqrt(-4 - 0i) = 0 - 2i.
         * Using `y == 0.0 ? 1.0 : y` forced +0's sign for both ±0. */
        double v = signbit(y) ? -v_mag : v_mag;
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

/* ============================================================
 * long double variants — computed natively in 80-bit precision
 * via the long-double scalar primitives (expl/logl/sinl/cosl/
 * hypotl/atan2l/sqrtl/log1pl/copysignl).  Same special-value and
 * branch-cut behavior as the double versions above.
 * ============================================================ */

long double complex cexpl(long double complex z) {
    long double x = __real__ z, y = __imag__ z;

    if (isnanl(x) || isnanl(y)) {
        if (isnanl(x) && y == 0.0L) return CMPLXL(NAN, y);
        return CMPLXL(NAN, NAN);
    }
    if (isinfl(x)) {
        if (x > 0.0L) {
            if (y == 0.0L) return CMPLXL(INFINITY, y);
            if (isinfl(y)) return CMPLXL(INFINITY, NAN);
            return CMPLXL(INFINITY * cosl(y), INFINITY * sinl(y));
        } else {
            if (isinfl(y)) return CMPLXL(0.0L, 0.0L);
            return CMPLXL(0.0L * cosl(y), 0.0L * sinl(y));
        }
    }
    long double r = expl(x);
    return CMPLXL(r * cosl(y), r * sinl(y));
}

long double complex clogl(long double complex z) {
    long double x = __real__ z, y = __imag__ z;

    if (x == 0.0L && y == 0.0L) {
        return CMPLXL(-INFINITY, atan2l(y, x));
    }
    long double re;
    long double r2 = x * x + y * y;
    if (r2 > 0.5L && r2 < 3.0L) {
        re = 0.5L * log1pl(r2 - 1.0L);
    } else {
        re = logl(hypotl(x, y));
    }
    return CMPLXL(re, atan2l(y, x));
}

long double complex csqrtl(long double complex z) {
    long double x = __real__ z, y = __imag__ z;

    if (x == 0.0L && y == 0.0L) return CMPLXL(0.0L, y);

    if (isinfl(x)) {
        if (x > 0.0L) {
            if (isnanl(y)) return CMPLXL(INFINITY, NAN);
            return CMPLXL(INFINITY, isinfl(y) ? y : copysignl(0.0L, y));
        }
        if (isnanl(y)) return CMPLXL(NAN, INFINITY);
        return CMPLXL(isinfl(y) ? INFINITY : 0.0L, copysignl(INFINITY, y));
    }
    if (isinfl(y)) return CMPLXL(INFINITY, y);
    if (isnanl(x) || isnanl(y)) return CMPLXL(NAN, NAN);

    long double R = hypotl(x, y);
    if (x >= 0.0L) {
        long double u = sqrtl(0.5L * (R + x));
        long double v = (u == 0.0L) ? 0.0L : 0.5L * y / u;
        return CMPLXL(u, v);
    } else {
        long double v_mag = sqrtl(0.5L * (R - x));
        long double u = (v_mag == 0.0L) ? 0.0L : 0.5L * fabsl(y) / v_mag;
        long double v = signbit(y) ? -v_mag : v_mag;
        return CMPLXL(u, v);
    }
}

long double complex cpowl(long double complex a, long double complex b) {
    if (__real__ a == 0.0L && __imag__ a == 0.0L &&
        __real__ b == 0.0L && __imag__ b == 0.0L) {
        return CMPLXL(1.0L, 0.0L);
    }
    return cexpl(b * clogl(a));
}
