/*
 * lib/m/src/cmath_hyper.c — complex hyperbolic, inverse hyperbolic,
 * and Riemann-sphere projection.
 *
 * Twenty-one entries:
 *   ccosh / csinh / ctanh × float/double/long-double
 *   cacosh / casinh / catanh × float/double/long-double
 *   cproj × float/double/long-double
 *
 * Implementation strategy:
 *
 *   csinh / ccosh — direct from real identities (analogues of csin / ccos):
 *     csinh(x+iy) = sinh(x)*cos(y) + i*cosh(x)*sin(y)
 *     ccosh(x+iy) = cosh(x)*cos(y) + i*sinh(x)*sin(y)
 *
 *   ctanh — closed form with overflow guard:
 *     ctanh(x+iy) = (sinh(2x) + i*sin(2y)) / (cosh(2x) + cos(2y))
 *     For |x| > 22, cosh(2x) saturates double range; return
 *     copysign(1, x) + 0i (lim_{x->±inf} tanh(x+iy) = ±1).
 *
 *   casinh / catanh — i-rotation identities:
 *     casinh(z) = -i * casin(i*z)
 *     catanh(z) = -i * catan(i*z)
 *
 *   cacosh — relate to cacos with branch fix-up.  Principal branch
 *   demands Re(cacosh(z)) >= 0, so we compute w = cacos(z) and rotate:
 *     cacosh(z) = +i*w if Im(w) <= 0
 *     cacosh(z) = -i*w if Im(w)  > 0
 *   This places the result in the correct half-plane while satisfying
 *   cosh(cacosh(z)) = z on the principal sheet.
 *
 *   cproj — C99 7.3.9.4: maps every complex infinity to a single
 *   point on the Riemann sphere.
 *     if any component is infinite: return CMPLX(+inf, copysign(0, y))
 *     otherwise:                   return z unchanged
 */

#include <complex.h>
#include <math.h>

/* ============================================================
 * ccosh / csinh / ctanh
 * ============================================================ */

double complex csinh(double complex z) {
    double x = __real__ z, y = __imag__ z;
    return CMPLX(sinh(x) * cos(y), cosh(x) * sin(y));
}

double complex ccosh(double complex z) {
    double x = __real__ z, y = __imag__ z;
    return CMPLX(cosh(x) * cos(y), sinh(x) * sin(y));
}

double complex ctanh(double complex z) {
    double x = __real__ z, y = __imag__ z;

    /* For |x| > 22, cosh(2x) saturates; the limit is sign(x). */
    if (x >  22.0) return CMPLX( 1.0, 0.0);
    if (x < -22.0) return CMPLX(-1.0, 0.0);

    double two_x = 2.0 * x, two_y = 2.0 * y;
    double denom = cosh(two_x) + cos(two_y);
    if (denom == 0.0) {
        return CMPLX(INFINITY, INFINITY);
    }
    return CMPLX(sinh(two_x) / denom, sin(two_y) / denom);
}

float complex csinhf(float complex z) { return (float complex)csinh((double complex)z); }
float complex ccoshf(float complex z) { return (float complex)ccosh((double complex)z); }
float complex ctanhf(float complex z) { return (float complex)ctanh((double complex)z); }

long double complex csinhl(long double complex z) {
    long double x = __real__ z, y = __imag__ z;
    return CMPLXL(sinhl(x) * cosl(y), coshl(x) * sinl(y));
}

long double complex ccoshl(long double complex z) {
    long double x = __real__ z, y = __imag__ z;
    return CMPLXL(coshl(x) * cosl(y), sinhl(x) * sinl(y));
}

long double complex ctanhl(long double complex z) {
    long double x = __real__ z, y = __imag__ z;
    if (x >  22.0L) return CMPLXL( 1.0L, 0.0L);
    if (x < -22.0L) return CMPLXL(-1.0L, 0.0L);
    long double two_x = 2.0L * x, two_y = 2.0L * y;
    long double denom = coshl(two_x) + cosl(two_y);
    if (denom == 0.0L) return CMPLXL(INFINITY, INFINITY);
    return CMPLXL(sinhl(two_x) / denom, sinl(two_y) / denom);
}

/* ============================================================
 * casinh / cacosh / catanh
 *
 * Note: declarations for casin / cacos / catan come from <complex.h>.
 * ============================================================ */

double complex casinh(double complex z) {
    /* casinh(z) = -i * casin(i*z).  i*z = -y + ix. */
    double complex iz = CMPLX(-(__imag__ z), __real__ z);
    double complex w  = casin(iz);
    /* -i * (u + iv) = v - iu. */
    return CMPLX(__imag__ w, -(__real__ w));
}

double complex cacosh(double complex z) {
    /* cacosh(z) = ±i * cacos(z), sign chosen so Re(result) >= 0.
     * cacos(z) lives in the strip Re in [0, pi]; multiplying by ±i
     * exchanges real and imaginary parts (with sign).
     *
     *   i * (u + iv) = -v + iu     -> Re = -v
     *  -i * (u + iv) =  v - iu     -> Re =  v
     *
     * We want Re >= 0, so pick +i when v <= 0 and -i when v > 0. */
    double complex w = cacos(z);
    if (__imag__ w > 0.0) {
        return CMPLX(__imag__ w, -(__real__ w));   /* -i*w */
    }
    return CMPLX(-(__imag__ w), __real__ w);       /* +i*w */
}

double complex catanh(double complex z) {
    /* catanh(z) = -i * catan(i*z). */
    double complex iz = CMPLX(-(__imag__ z), __real__ z);
    double complex w  = catan(iz);
    return CMPLX(__imag__ w, -(__real__ w));
}

float complex casinhf(float complex z) { return (float complex)casinh((double complex)z); }
float complex cacoshf(float complex z) { return (float complex)cacosh((double complex)z); }
float complex catanhf(float complex z) { return (float complex)catanh((double complex)z); }

long double complex casinhl(long double complex z) {
    /* casinh(z) = -i * casin(i*z).  i*z = -y + ix. */
    long double complex iz = CMPLXL(-(__imag__ z), __real__ z);
    long double complex w  = casinl(iz);
    return CMPLXL(__imag__ w, -(__real__ w));
}

long double complex cacoshl(long double complex z) {
    /* cacosh(z) = ±i * cacos(z), sign so Re(result) >= 0. */
    long double complex w = cacosl(z);
    if (__imag__ w > 0.0L) {
        return CMPLXL(__imag__ w, -(__real__ w));   /* -i*w */
    }
    return CMPLXL(-(__imag__ w), __real__ w);       /* +i*w */
}

long double complex catanhl(long double complex z) {
    /* catanh(z) = -i * catan(i*z). */
    long double complex iz = CMPLXL(-(__imag__ z), __real__ z);
    long double complex w  = catanl(iz);
    return CMPLXL(__imag__ w, -(__real__ w));
}

/* ============================================================
 * cproj — projection onto the Riemann sphere.
 *
 * C99 7.3.9.4: cproj(z) returns the value of z projected onto the
 * Riemann sphere.  Every infinity in the complex plane maps to a
 * single point at the "north pole" — represented in IEEE-754 land
 * as +inf + i*0, with the sign of the imaginary zero following the
 * sign of the original imaginary part.
 * ============================================================ */

double complex cproj(double complex z) {
    if (isinf(__real__ z) || isinf(__imag__ z)) {
        return CMPLX(INFINITY, copysign(0.0, __imag__ z));
    }
    return z;
}

float complex cprojf(float complex z) {
    if (isinf(__real__ z) || isinf(__imag__ z)) {
        return CMPLXF(INFINITY, copysignf(0.0f, __imag__ z));
    }
    return z;
}

long double complex cprojl(long double complex z) {
    if (isinf(__real__ z) || isinf(__imag__ z)) {
        return CMPLXL(INFINITY, copysignl(0.0L, __imag__ z));
    }
    return z;
}
