/*
 * lib/m/src/cmath_trig.c — complex trig + inverse trig.
 *
 * All eighteen entries from <complex.h>'s trigonometric section
 * (ccos / csin / ctan / cacos / casin / catan and their f/l
 * variants) plus the supporting projection helpers (creal, cimag,
 * cabs, carg, conj) that the formulas need.
 *
 * Strategy — real-math closed forms.  We never call clog/csqrt
 * (which would imply having those in libm); every entry decomposes
 * into the real functions we already have (sin, cos, sinh, cosh,
 * log, log1p, hypot, atan2, asin, sqrt).
 *
 * Direct identities (z = x + iy):
 *   ccos(z) = cos(x)*cosh(y) - i*sin(x)*sinh(y)
 *   csin(z) = sin(x)*cosh(y) + i*cos(x)*sinh(y)
 *   ctan(z) = (sin(2x) + i*sinh(2y)) / (cos(2x) + cosh(2y))
 *
 * Inverse via Hull-Fairgrieve-Tang for casin (canonical reference):
 *   alpha = (1/2)*( sqrt((x+1)^2 + y^2) + sqrt((x-1)^2 + y^2) )
 *   beta  = (1/2)*( sqrt((x+1)^2 + y^2) - sqrt((x-1)^2 + y^2) )
 *   Re(casin(z)) = asin(beta)
 *   Im(casin(z)) = log(alpha + sqrt(alpha^2 - 1))     (= acosh(alpha))
 *
 * Then:
 *   cacos(z) = pi/2 - casin(z)        (in particular, when z is real)
 *   catan(z) = (1/2)*atan2(2x, 1 - x^2 - y^2)
 *               + (i/4)*log( (x^2 + (y+1)^2) / (x^2 + (y-1)^2) )
 *
 * Edge cases for ctan(z) when |y| is large: cosh(2y) and sinh(2y)
 * both saturate, but their ratio approaches +/-1.  Short-circuit
 * past the overflow with an explicit ctan(x ± i*Inf) = 0 ± i*1.
 *
 * f/l variants: cast through double for now.  Substrate's libm
 * elsewhere does the same (mathf.c / mathl.c).  Long-double
 * precision is x87 80-bit, so the cast costs precision but the
 * surface is implemented; sharpen later when a real consumer needs
 * the extra bits.
 */

#include <complex.h>
#include <math.h>

#ifndef M_PI_2
#define M_PI_2   1.57079632679489661923
#endif

/* ============================================================
 * Projection / magnitude
 * ============================================================ */

double      creal (double      complex z) { return __real__ z; }
float       crealf(float       complex z) { return __real__ z; }
long double creall(long double complex z) { return __real__ z; }
double      cimag (double      complex z) { return __imag__ z; }
float       cimagf(float       complex z) { return __imag__ z; }
long double cimagl(long double complex z) { return __imag__ z; }

double      cabs (double      complex z) { return hypot(__real__ z, __imag__ z); }
float       cabsf(float       complex z) { return (float)hypot(__real__ z, __imag__ z); }
long double cabsl(long double complex z) { return (long double)hypot((double)(__real__ z), (double)(__imag__ z)); }

double      carg (double      complex z) { return atan2(__imag__ z, __real__ z); }
float       cargf(float       complex z) { return (float)atan2(__imag__ z, __real__ z); }
long double cargl(long double complex z) { return (long double)atan2((double)(__imag__ z), (double)(__real__ z)); }

double      complex conj (double      complex z) { return CMPLX(__real__ z, -__imag__ z); }
float       complex conjf(float       complex z) { return CMPLXF(__real__ z, -__imag__ z); }
long double complex conjl(long double complex z) { return CMPLXL(__real__ z, -__imag__ z); }

/* ============================================================
 * ccos / csin / ctan
 * ============================================================ */

double complex ccos(double complex z) {
    double x = __real__ z, y = __imag__ z;
    return CMPLX(cos(x) * cosh(y), -sin(x) * sinh(y));
}

double complex csin(double complex z) {
    double x = __real__ z, y = __imag__ z;
    return CMPLX(sin(x) * cosh(y), cos(x) * sinh(y));
}

double complex ctan(double complex z) {
    double x = __real__ z, y = __imag__ z;

    /* For |y| beyond ~22, cosh(2y) overflows double range
     * (2^1024 ≈ exp(2*354)); in that limit tan(x + iy) → +/-i. */
    if (y > 22.0)  return CMPLX(0.0,  1.0);
    if (y < -22.0) return CMPLX(0.0, -1.0);

    double two_x = 2.0 * x, two_y = 2.0 * y;
    double denom = cos(two_x) + cosh(two_y);
    if (denom == 0.0) {
        /* Pole at z = (pi/2 + k*pi) + 0i — return signed infinity
         * in the imaginary part to flag the issue while keeping the
         * type a double complex. */
        return CMPLX(INFINITY, INFINITY);
    }
    return CMPLX(sin(two_x) / denom, sinh(two_y) / denom);
}

float complex ccosf(float complex z) {
    return (float complex)ccos((double complex)z);
}
float complex csinf(float complex z) {
    return (float complex)csin((double complex)z);
}
float complex ctanf(float complex z) {
    return (float complex)ctan((double complex)z);
}

long double complex ccosl(long double complex z) {
    return (long double complex)ccos((double complex)z);
}
long double complex csinl(long double complex z) {
    return (long double complex)csin((double complex)z);
}
long double complex ctanl(long double complex z) {
    return (long double complex)ctan((double complex)z);
}

/* ============================================================
 * cacos / casin / catan
 * ============================================================ */

/* asin's range is [-pi/2, pi/2]; passing a tiny argument outside
 * that range happens when alpha/beta come back slightly out due to
 * rounding.  Clamp. */
static double clamp_unit(double v) {
    if (v >  1.0) return  1.0;
    if (v < -1.0) return -1.0;
    return v;
}

/* log(alpha + sqrt(alpha^2 - 1)) = acosh(alpha), evaluated stably:
 *   for alpha near 1: log1p((alpha - 1) + sqrt((alpha-1)*(alpha+1)))
 *   for large alpha:  log(2*alpha) - 1/(4*alpha^2) + ...
 * Substrate doesn't have acosh in libm yet, but the closed-form
 * works for the moderate range casin/cacos encounter. */
static double acosh_helper(double alpha) {
    if (alpha < 1.0) alpha = 1.0;     /* sqrt clamping */
    if (alpha < 1.5) {
        double am1 = alpha - 1.0;
        return log1p(am1 + sqrt(am1 * (alpha + 1.0)));
    }
    return log(alpha + sqrt(alpha * alpha - 1.0));
}

double complex casin(double complex z) {
    double x = __real__ z, y = __imag__ z;
    double ax = fabs(x), ay = fabs(y);

    /* Hull-Fairgrieve-Tang.  Compute alpha and beta. */
    double r = hypot(ax + 1.0, ay);
    double s = hypot(ax - 1.0, ay);
    double alpha = 0.5 * (r + s);
    double beta  = 0.5 * (r - s);

    double re = asin(clamp_unit(beta));
    double im = acosh_helper(alpha);

    if (x < 0) re = -re;
    if (y < 0) im = -im;
    return CMPLX(re, im);
}

double complex cacos(double complex z) {
    /* cacos(z) = pi/2 - casin(z) — exact identity from C99 G.6.1.2 */
    double complex s = casin(z);
    return CMPLX(M_PI_2 - __real__ s, -__imag__ s);
}

double complex catan(double complex z) {
    double x = __real__ z, y = __imag__ z;

    /* Pure imaginary axis: catan(0 + iy) for |y| == 1 is the
     * domain pole.  Return signed infinity for the imaginary part. */
    if (x == 0.0 && (y == 1.0 || y == -1.0)) {
        return CMPLX(0.0, y * INFINITY);
    }

    double re = 0.5 * atan2(2.0 * x, 1.0 - x * x - y * y);

    /* Imaginary part = (1/4) * log((x^2 + (y+1)^2) / (x^2 + (y-1)^2))
     * Use log1p form to preserve precision when y is small. */
    double num = x * x + (y + 1.0) * (y + 1.0);
    double den = x * x + (y - 1.0) * (y - 1.0);
    double im;
    if (den == 0.0) {
        im = INFINITY;
    } else {
        /* log(num/den) = log1p((num - den) / den) — stable for
         * num close to den (which is the y ≈ 0 case). */
        im = 0.25 * log1p((num - den) / den);
    }
    return CMPLX(re, im);
}

float complex casinf(float complex z) {
    return (float complex)casin((double complex)z);
}
float complex cacosf(float complex z) {
    return (float complex)cacos((double complex)z);
}
float complex catanf(float complex z) {
    return (float complex)catan((double complex)z);
}

long double complex casinl(long double complex z) {
    return (long double complex)casin((double complex)z);
}
long double complex cacosl(long double complex z) {
    return (long double complex)cacos((double complex)z);
}
long double complex catanl(long double complex z) {
    return (long double complex)catan((double complex)z);
}
