/*
 * mathl.c - Long double (l-suffix) math library variants
 *
 * On i386, x87 long double is 80-bit extended precision (the native FPU
 * format).  All variants here delegate to the corresponding double function
 * by casting the argument to double and the result back to long double.
 * This is standards-compliant and avoids duplicating complex algorithms.
 *
 * Functions already implemented in math.c and declared in math.h are
 * NOT re-implemented here:
 *   sinl, cosl, tanl, sincosl, sqrtl, powl, fabsl, fmodl, remainderl,
 *   remquol, fmal, fmaxl, fminl, fdiml, ceill, floorl, truncl, roundl,
 *   fmaximuml, fminimuml, fmaximum_numl, fminimum_numl,
 *   fmaximum_magl, fminimum_magl,
 *   sinpil, cospil, tanpil, asinpil, atanpil
 */

#include <math.h>

/* Inverse trigonometric */
long double asinl(long double x)  { return (long double)asin((double)x); }
long double acosl(long double x)  { return (long double)acos((double)x); }
long double atanl(long double x)  { return (long double)atan((double)x); }
long double atan2l(long double y, long double x)
{
    return (long double)atan2((double)y, (double)x);
}

/* Hyperbolic */
long double sinhl(long double x)  { return (long double)sinh((double)x); }
long double coshl(long double x)  { return (long double)cosh((double)x); }
long double tanhl(long double x)  { return (long double)tanh((double)x); }
long double asinhl(long double x) { return (long double)asinh((double)x); }
long double acoshl(long double x) { return (long double)acosh((double)x); }
long double atanhl(long double x) { return (long double)atanh((double)x); }

/* Exponential and logarithmic */
long double expl(long double x)   { return (long double)exp((double)x); }
long double exp2l(long double x)  { return (long double)exp2((double)x); }
long double expm1l(long double x) { return (long double)expm1((double)x); }
long double logl(long double x)   { return (long double)log((double)x); }
long double log2l(long double x)  { return (long double)log2((double)x); }
long double log10l(long double x) { return (long double)log10((double)x); }
long double log1pl(long double x) { return (long double)log1p((double)x); }

/* Power and root */
long double cbrtl(long double x)
{
    return (long double)cbrt((double)x);
}

long double hypotl(long double x, long double y)
{
    return (long double)hypot((double)x, (double)y);
}

/* Rounding — additional variants not in math.c */
long double rintl(long double x)      { return (long double)rint((double)x); }
long double nearbyintl(long double x) { return (long double)nearbyint((double)x); }

/* Long integer rounding */
long      lroundl(long double x)  { return lround((double)x); }
long long llroundl(long double x) { return llround((double)x); }
long      lrintl(long double x)   { return lrint((double)x); }
long long llrintl(long double x)  { return llrint((double)x); }

/* Floating-point manipulation */
long double frexpl(long double x, int *exp)
{
    return (long double)frexp((double)x, exp);
}

long double ldexpl(long double x, int exp)
{
    return (long double)ldexp((double)x, exp);
}

long double modfl(long double x, long double *iptr)
{
    double tmp;
    long double frac = (long double)modf((double)x, &tmp);
    *iptr = (long double)tmp;
    return frac;
}

long double scalbnl(long double x, int n)
{
    return (long double)scalbn((double)x, n);
}

long double scalblnl(long double x, long n)
{
    return (long double)scalbln((double)x, n);
}

/* Exponent extraction */
int         ilogbl(long double x)  { return ilogb((double)x); }
long double logbl(long double x)   { return (long double)logb((double)x); }

/* Next representable value */
long double nextafterl(long double x, long double y)
{
    return (long double)nextafter((double)x, (double)y);
}

long double nexttowardl(long double x, long double y)
{
    return (long double)nexttoward((double)x, y);
}

/* Sign manipulation */
long double copysignl(long double x, long double y)
{
    return (long double)copysign((double)x, (double)y);
}

/* NaN */
long double nanl(const char *tagp)
{
    return (long double)nan(tagp);
}

/* Error and gamma functions */
long double erfl(long double x)    { return (long double)erf((double)x); }
long double erfcl(long double x)   { return (long double)erfc((double)x); }
long double tgammal(long double x) { return (long double)tgamma((double)x); }
long double lgammal(long double x) { return (long double)lgamma((double)x); }

/* C23: fromfp family (long double variants) */
int fromfpxl(long double *y, long double x, fenv_t *envp, int rounding_mode) {
    if (envp) {
        fenv_t zero_env = { 0 };
        *envp = zero_env;
    }
    switch (rounding_mode) {
    case FE_TONEAREST:
        *y = x;
        break;
    case FE_DOWNWARD:
        *y = floorl(x);
        break;
    case FE_UPWARD:
        *y = ceill(x);
        break;
    case FE_TOWARDZERO:
        *y = truncl(x);
        break;
    default:
        return -1;
    }
    return 0;
}

int fromfpl(long double *y, long double x, fenv_t *envp, int rounding_mode) {
    return fromfpxl(y, x, envp, rounding_mode);
}

int ufromfpul(unsigned int *y, long double x, fenv_t *envp, int rounding_mode) {
    long double tmp = 0;
    int rc = fromfpxl(&tmp, x, envp, rounding_mode);
    *y = (unsigned int)tmp;
    return rc;
}

int ufromfpxl(unsigned int *y, long double x, fenv_t *envp, int rounding_mode) {
    return ufromfpul(y, x, envp, rounding_mode);
}
