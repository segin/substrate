/*
 * mathf.c - Float (f-suffix) math library variants
 *
 * All float variants delegate to the corresponding double function via
 * cast-to-double / cast-back-to-float.  This is standards-compliant
 * (IEEE 754 round-to-nearest) and avoids duplicating complex algorithms.
 *
 * Functions already implemented in math.c and declared in math.h are
 * NOT re-implemented here:
 *   sinf, cosf, tanf, sincosf, sqrtf, powf, fabsf, fmodf, remainderf,
 *   remquof, fmaf, fmaxf, fminf, fdimf, ceilf, floorf, truncf, roundf,
 *   fmaximumf, fminimumf, fmaximum_numf, fminimum_numf,
 *   fmaximum_magf, fminimum_magf,
 *   sinpif, cospif, tanpif, asinpif, atanpif
 */

#include <math.h>
#include <string.h>

/* Inverse trigonometric */
float asinf(float x)  { return (float)asin((double)x); }
float acosf(float x)  { return (float)acos((double)x); }
float atanf(float x)  { return (float)atan((double)x); }
float atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }

/* Hyperbolic */
float sinhf(float x)  { return (float)sinh((double)x); }
float coshf(float x)  { return (float)cosh((double)x); }
float tanhf(float x)  { return (float)tanh((double)x); }
float asinhf(float x) { return (float)asinh((double)x); }
float acoshf(float x) { return (float)acosh((double)x); }
float atanhf(float x) { return (float)atanh((double)x); }

/* Exponential and logarithmic */
float expf(float x)   { return (float)exp((double)x); }
float exp2f(float x)  { return (float)exp2((double)x); }
float expm1f(float x) { return (float)expm1((double)x); }
float logf(float x)   { return (float)log((double)x); }
float log2f(float x)  { return (float)log2((double)x); }
float log10f(float x) { return (float)log10((double)x); }
float log1pf(float x) { return (float)log1p((double)x); }

/* Power and root */
float cbrtf(float x)           { return (float)cbrt((double)x); }
float hypotf(float x, float y) { return (float)hypot((double)x, (double)y); }

/* Rounding — additional variants not in math.c */
float rintf(float x)      { return (float)rint((double)x); }
float nearbyintf(float x) { return (float)nearbyint((double)x); }

/* Long integer rounding */
long  lroundf(float x)  { return lround((double)x); }
long long llroundf(float x) { return llround((double)x); }
long  lrintf(float x)   { return lrint((double)x); }
long long llrintf(float x)  { return llrint((double)x); }

/* Floating-point manipulation */
float frexpf(float x, int *exp)
{
    return (float)frexp((double)x, exp);
}

float ldexpf(float x, int exp)
{
    return (float)ldexp((double)x, exp);
}

float modff(float x, float *iptr)
{
    double tmp;
    float frac = (float)modf((double)x, &tmp);
    *iptr = (float)tmp;
    return frac;
}

float scalbnf(float x, int n)   { return (float)scalbn((double)x, n); }
float scalblnf(float x, long n) { return (float)scalbln((double)x, n); }

/* Exponent extraction */
int   ilogbf(float x)  { return ilogb((double)x); }
float logbf(float x)   { return (float)logb((double)x); }

/* Next representable value */
float nextafterf(float x, float y)
{
    return (float)nextafter((double)x, (double)y);
}

float nexttowardf(float x, long double y)
{
    return (float)nexttoward((double)x, y);
}

/* Sign manipulation */
float copysignf(float x, float y)
{
    return (float)copysign((double)x, (double)y);
}

/* NaN */
float nanf(const char *tagp)
{
    return (float)nan(tagp);
}

/* Error and gamma functions */
float erff(float x)    { return (float)erf((double)x); }
float erfcf(float x)   { return (float)erfc((double)x); }
float tgammaf(float x) { return (float)tgamma((double)x); }
float lgammaf(float x) { return (float)lgamma((double)x); }

/* C23: exp/log family */
float exp10f(float x)         { return (float)exp10((double)x); }
float exp10m1f(float x)       { return (float)exp10m1((double)x); }
float exp2m1f(float x)        { return (float)exp2m1((double)x); }
float logp1f(float x)         { return (float)logp1((double)x); }
float log2p1f(float x)        { return (float)log2p1((double)x); }
float log10p1f(float x)       { return (float)log10p1((double)x); }
float rsqrtf(float x)         { return (float)rsqrt((double)x); }
float pownf(float x, intmax_t n) { return (float)pown((double)x, n); }
float powrf(float x, float y) { return (float)powr((double)x, (double)y); }
float rootnf(float x, int n)  { return (float)rootn((double)x, n); }
float compoundf(float x, intmax_t n) { return (float)compound((double)x, n); }
/* Bessel functions (float) */
float j0f(float x) { return (float)j0((double)x); }
float j1f(float x) { return (float)j1((double)x); }
float jnf(int n, float x) { return (float)jn(n, (double)x); }
float y0f(float x) { return (float)y0((double)x); }
float y1f(float x) { return (float)y1((double)x); }
float ynf(int n, float x) { return (float)yn(n, (double)x); }

int fromfpxf(float *y, float x, fenv_t *envp, int rounding_mode) {
    if (envp) {
        fenv_t zero_env;
        memset(&zero_env, 0, sizeof(zero_env));
        *envp = zero_env;
    }
    switch (rounding_mode) {
    case FE_TONEAREST:
        *y = x;
        break;
    case FE_DOWNWARD:
        *y = floorf(x);
        break;
    case FE_UPWARD:
        *y = ceilf(x);
        break;
    case FE_TOWARDZERO:
        *y = truncf(x);
        break;
    default:
        return -1;
    }
    return 0;
}

int fromfpf(float *y, float x, fenv_t *envp, int rounding_mode) {
    return fromfpxf(y, x, envp, rounding_mode);
}

int ufromfpf(unsigned int *y, float x, fenv_t *envp, int rounding_mode) {
    float tmp = 0;
    int rc = fromfpxf(&tmp, x, envp, rounding_mode);
    *y = (unsigned int)tmp;
    return rc;
}

int ufromfpxf(unsigned int *y, float x, fenv_t *envp, int rounding_mode) {
    return ufromfpf(y, x, envp, rounding_mode);
}

