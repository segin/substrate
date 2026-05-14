/*
 * <complex.h> — C99/C11 complex math interface for substrate.
 *
 * The complex types are the GCC `_Complex` types; this header defines
 * the standard spellings (`complex`, `imaginary`, `I`) plus the
 * CMPLX construction macros, then declares the function surface.
 *
 * Substrate libm implements:
 *   creal / cimag / cabs / carg / conj  — projection / magnitude
 *   ccos / csin / ctan / cacos / casin / catan  — trigonometric
 * The hyperbolic and exponential/logarithmic families are TBD.
 *
 * Each function has float/double/long-double variants (`f`/none/`l`
 * suffix), matching C99 7.3.
 */

#ifndef _COMPLEX_H
#define _COMPLEX_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
#define complex   _Complex
#define imaginary _Imaginary
#endif

/* Pure imaginary unit.  __builtin_complex emits the precise bit
 * pattern (0.0 + i*1.0) without going through any user-overridable
 * arithmetic — required so I*0.0 doesn't silently dot back into
 * real arithmetic. */
#if defined(__GNUC__) && (__GNUC__ >= 5 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
#  define _Complex_I  (__extension__ __builtin_complex(0.0f, 1.0f))
#else
#  define _Complex_I  (1.0fi)
#endif

#ifndef _Imaginary_I
#  define _Imaginary_I  _Complex_I
#endif

#define I  _Complex_I

/* CMPLX-family macros (C11 7.3.9.3): build a complex value from
 * its real and imaginary parts, preserving signed zero and Inf/NaN
 * exactly.  `re + I*im` would lose the sign of zero on the
 * imaginary part when `re` happens to be +/-0. */
#if defined(__GNUC__) && (__GNUC__ >= 5 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
#  define CMPLX(x, y)   __builtin_complex((double)(x), (double)(y))
#  define CMPLXF(x, y)  __builtin_complex((float)(x), (float)(y))
#  define CMPLXL(x, y)  __builtin_complex((long double)(x), (long double)(y))
#else
#  define CMPLX(x, y)   ((double complex)((double)(x) + I * (double)(y)))
#  define CMPLXF(x, y)  ((float complex)((float)(x) + I * (float)(y)))
#  define CMPLXL(x, y)  ((long double complex)((long double)(x) + I * (long double)(y)))
#endif

/* ---- projection / magnitude --------------------------------------------- */
double      creal (double      complex z);
float       crealf(float       complex z);
long double creall(long double complex z);
double      cimag (double      complex z);
float       cimagf(float       complex z);
long double cimagl(long double complex z);
double      cabs (double       complex z);
float       cabsf(float        complex z);
long double cabsl(long double  complex z);
double      carg (double       complex z);
float       cargf(float        complex z);
long double cargl(long double  complex z);
double      complex conj (double      complex z);
float       complex conjf(float       complex z);
long double complex conjl(long double complex z);

/* ---- trigonometric ------------------------------------------------------ */
double      complex ccos (double      complex z);
float       complex ccosf(float       complex z);
long double complex ccosl(long double complex z);

double      complex csin (double      complex z);
float       complex csinf(float       complex z);
long double complex csinl(long double complex z);

double      complex ctan (double      complex z);
float       complex ctanf(float       complex z);
long double complex ctanl(long double complex z);

double      complex cacos (double      complex z);
float       complex cacosf(float       complex z);
long double complex cacosl(long double complex z);

double      complex casin (double      complex z);
float       complex casinf(float       complex z);
long double complex casinl(long double complex z);

double      complex catan (double      complex z);
float       complex catanf(float       complex z);
long double complex catanl(long double complex z);

/* ---- exponential / logarithmic / power / sqrt -------------------------- */
double      complex cexp (double      complex z);
float       complex cexpf(float       complex z);
long double complex cexpl(long double complex z);

double      complex clog (double      complex z);
float       complex clogf(float       complex z);
long double complex clogl(long double complex z);

double      complex csqrt (double      complex z);
float       complex csqrtf(float       complex z);
long double complex csqrtl(long double complex z);

double      complex cpow (double      complex x, double      complex y);
float       complex cpowf(float       complex x, float       complex y);
long double complex cpowl(long double complex x, long double complex y);

/* ---- hyperbolic / inverse hyperbolic ----------------------------------- */
double      complex ccosh (double      complex z);
float       complex ccoshf(float       complex z);
long double complex ccoshl(long double complex z);

double      complex csinh (double      complex z);
float       complex csinhf(float       complex z);
long double complex csinhl(long double complex z);

double      complex ctanh (double      complex z);
float       complex ctanhf(float       complex z);
long double complex ctanhl(long double complex z);

double      complex cacosh (double      complex z);
float       complex cacoshf(float       complex z);
long double complex cacoshl(long double complex z);

double      complex casinh (double      complex z);
float       complex casinhf(float       complex z);
long double complex casinhl(long double complex z);

double      complex catanh (double      complex z);
float       complex catanhf(float       complex z);
long double complex catanhl(long double complex z);

/* ---- Riemann-sphere projection ----------------------------------------- */
double      complex cproj (double      complex z);
float       complex cprojf(float       complex z);
long double complex cprojl(long double complex z);

/* ---- gamma / error / Bessel (substrate extension) ---------------------- *
 * These mirror the real tgamma/lgamma/erf/erfc/j0/j1/y0/y1 set; not part
 * of any C standard but useful when tgmath dispatches on a _Complex
 * argument.  Implementations live in lib/m/src/cmath_special.c. */
double      complex clgamma (double      complex z);
float       complex clgammaf(float       complex z);
long double complex clgammal(long double complex z);
double      complex ctgamma (double      complex z);
float       complex ctgammaf(float       complex z);
long double complex ctgammal(long double complex z);

double      complex cerf  (double      complex z);
float       complex cerff (float       complex z);
long double complex cerfl (long double complex z);
double      complex cerfc (double      complex z);
float       complex cerfcf(float       complex z);
long double complex cerfcl(long double complex z);

double      complex cj0 (double      complex z);
float       complex cj0f(float       complex z);
long double complex cj0l(long double complex z);
double      complex cj1 (double      complex z);
float       complex cj1f(float       complex z);
long double complex cj1l(long double complex z);

double      complex cy0 (double      complex z);
float       complex cy0f(float       complex z);
long double complex cy0l(long double complex z);
double      complex cy1 (double      complex z);
float       complex cy1f(float       complex z);
long double complex cy1l(long double complex z);

#ifdef __cplusplus
}
#endif

#endif /* _COMPLEX_H */
