#ifndef _MATH_H
#define _MATH_H

#include <stdint.h>
#include <fenv.h>

/*
 * C99 Math Library Header
 * IEEE 754 floating-point support
 */

/* Floating-point classification categories */
#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

/* Infinity and NaN constants */
#define HUGE_VAL     __builtin_huge_val()
#define HUGE_VALF    __builtin_huge_valf()
#define HUGE_VALL    __builtin_huge_vall()
#ifndef INFINITY
#define INFINITY     __builtin_inff()
#endif
#ifndef NAN
#define NAN          __builtin_nanf("")
#endif

/* Math error handling */
#define MATH_ERRNO       1
#define MATH_ERREXCEPT   2
#define math_errhandling MATH_ERRNO

/* Internal classification functions */
int __fpclassifyf(float x);
int __fpclassify(double x);
int __fpclassifyl(long double x);
int __signbitf(float x);
int __signbit(double x);
int __signbitl(long double x);
int __isnanf(float x);
int __isnan(double x);
int __isnanl(long double x);
int __isinff(float x);
int __isinf(double x);
int __isinfl(long double x);
/* C23 — signaling-NaN bit-pattern detection (IEEE 754-2008 §6.2.1:
 * the most-significant mantissa bit of a NaN is 0 for sNaN, 1 for qNaN).
 * Any non-NaN returns 0. */
int __issignalingf(float x);
int __issignaling(double x);
int __issignalingl(long double x);
/* C23 — equality that DOES raise FE_INVALID on a NaN operand
 * (unlike == which on x87 with FUCOM only raises for sNaN). */
int __iseqsigf(float x, float y);
int __iseqsig(double x, double y);
int __iseqsigl(long double x, long double y);

/* Type-generic classification macros */
#define fpclassify(x) \
    ((sizeof(x) == sizeof(float)) ? __fpclassifyf(x) : \
     (sizeof(x) == sizeof(double)) ? __fpclassify(x) : \
     __fpclassifyl(x))

#define isfinite(x) (fpclassify(x) >= FP_ZERO)

#define isinf(x) \
    ((sizeof(x) == sizeof(float)) ? __isinff(x) : \
     (sizeof(x) == sizeof(double)) ? __isinf(x) : \
     __isinfl(x))

#define isnan(x) \
    ((sizeof(x) == sizeof(float)) ? __isnanf(x) : \
     (sizeof(x) == sizeof(double)) ? __isnan(x) : \
     __isnanl(x))

#define isnormal(x) (fpclassify(x) == FP_NORMAL)

#define signbit(x) \
    ((sizeof(x) == sizeof(float)) ? __signbitf(x) : \
     (sizeof(x) == sizeof(double)) ? __signbit(x) : \
     __signbitl(x))

/*
 * C99 §7.12.14 — Quiet (non-FE_INVALID-raising) comparison macros.
 *
 * Plain </<=/>/>= raise FE_INVALID when either operand is NaN; these
 * macros must NOT.  GCC/clang provide __builtin_is{less,greater,...}
 * which lower to the FUCOM / UCOMISS family of instructions on x86 —
 * those signal "unordered" without raising the IEEE invalid flag.
 *
 * Fallbacks are constructed so that a NaN operand short-circuits to
 * the desired "unordered" answer before any signalling comparison
 * happens (the bare `<` etc. are only evaluated once we've ruled out
 * NaN with isunordered).
 */
#ifdef __GNUC__
#define isgreater(x, y)      __builtin_isgreater((x), (y))
#define isgreaterequal(x, y) __builtin_isgreaterequal((x), (y))
#define isless(x, y)         __builtin_isless((x), (y))
#define islessequal(x, y)    __builtin_islessequal((x), (y))
#define islessgreater(x, y)  __builtin_islessgreater((x), (y))
#define isunordered(x, y)    __builtin_isunordered((x), (y))
#else
#define isunordered(x, y)    (isnan(x) || isnan(y))
#define isgreater(x, y)      (!isunordered((x), (y)) && (x) >  (y))
#define isgreaterequal(x, y) (!isunordered((x), (y)) && (x) >= (y))
#define isless(x, y)         (!isunordered((x), (y)) && (x) <  (y))
#define islessequal(x, y)    (!isunordered((x), (y)) && (x) <= (y))
#define islessgreater(x, y)  (!isunordered((x), (y)) && \
                              ((x) < (y) || (x) > (y)))
#endif

/*
 * C23 §7.12.4 additions.
 *
 * iseqsig : equality that DOES raise FE_INVALID on a NaN operand.
 *           Implemented as a real call so the FE_INVALID raise is
 *           explicit and not dependent on which flavour of x87 compare
 *           the compiler picks.
 * issignaling : true iff x is a signaling NaN.
 * iscanonical : per IEEE 754 binary formats every value is canonical,
 *               so this is the constant 1 for any non-NaN finite/inf.
 *               We still evaluate x to provoke any side effects.
 * issubnormal : convenience for fpclassify(x) == FP_SUBNORMAL.
 * iszero      : convenience for fpclassify(x) == FP_ZERO.
 */
#define issignaling(x) \
    ((sizeof(x) == sizeof(float)) ? __issignalingf(x) : \
     (sizeof(x) == sizeof(double)) ? __issignaling(x) : \
     __issignalingl(x))

#define iseqsig(x, y) \
    ((sizeof((x) + (y)) == sizeof(float))  ? __iseqsigf((x), (y))  : \
     (sizeof((x) + (y)) == sizeof(double)) ? __iseqsig((x), (y))   : \
     __iseqsigl((x), (y)))

#define iscanonical(x) ((void)(x), 1)
#define issubnormal(x) (fpclassify(x) == FP_SUBNORMAL)
#define iszero(x)      (fpclassify(x) == FP_ZERO)

/* Basic trigonometric functions */
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
void sincos(double x, double *s, double *c);

/* C23 inverse trigonometric pi-argument variants */
double acospi(double x);
double atan2pi(double y, double x);

/* Hyperbolic functions */
double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);

/* ilogb special-value return codes (C99 7.12.6.5) */
#define FP_ILOGB0   (-2147483647 - 1)   /* INT_MIN */
#define FP_ILOGBNAN (-2147483647 - 1)   /* INT_MIN */

/* Floating-point manipulation */
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double modf(double x, double *iptr);
double scalbn(double x, int n);
double scalbln(double x, long n);
int    ilogb(double x);
double logb(double x);
double nextafter(double x, double y);
double nexttoward(double x, long double y);
double nextup(double x);
double nextdown(double x);
double copysign(double x, double y);
double nan(const char *tagp);

/* Exponential and logarithmic functions */
double exp(double x);
double exp2(double x);
double expm1(double x);
double log(double x);
double log10(double x);
double log2(double x);
double log1p(double x);
double pow(double x, double y);
double sqrt(double x);
double cbrt(double x);
double hypot(double x, double y);

/* C23 exponential and logarithmic extensions */
double exp10(double x);
double exp10m1(double x);
double exp2m1(double x);
double log10p1(double x);
double log2p1(double x);
double logp1(double x);

/* C23 power and root functions */
double pown(double x, intmax_t n);
double powr(double x, double y);
double rootn(double x, int n);
double compound(double x, intmax_t n);
double rsqrt(double x);

/* Absolute value and remainder */
double fabs(double x);
double fmod(double x, double y);
double remainder(double x, double y);
double remquo(double x, double y, int *quo);

/* Fused multiply-add */
double fma(double x, double y, double z);
float fmaf(float x, float y, float z);
long double fmal(long double x, long double y, long double z);

/* Min/Max (C99 — NaN-ignoring) */
double fmax(double x, double y);
double fmin(double x, double y);
double fdim(double x, double y);

/* Min/Max (C23 — NaN-propagating and variants) */
double fmaximum(double x, double y);
double fminimum(double x, double y);
double fmaximum_num(double x, double y);
double fminimum_num(double x, double y);
double fmaximum_mag(double x, double y);
double fminimum_mag(double x, double y);
float fmaximumf(float x, float y);
float fminimumf(float x, float y);
float fmaximum_numf(float x, float y);
float fminimum_numf(float x, float y);
float fmaximum_magf(float x, float y);
float fminimum_magf(float x, float y);
long double fmaximuml(long double x, long double y);
long double fminimuml(long double x, long double y);
long double fmaximum_numl(long double x, long double y);
long double fminimum_numl(long double x, long double y);
long double fmaximum_magl(long double x, long double y);
long double fminimum_magl(long double x, long double y);

/* Rounding */
double ceil(double x);
double floor(double x);
double trunc(double x);
double round(double x);
double roundeven(double x);
double rint(double x);
double nearbyint(double x);
long lrint(double x);
long long llrint(double x);
long lround(double x);
long long llround(double x);

/* Error and gamma functions */
double erf(double x);
double erfc(double x);
double tgamma(double x);
double lgamma(double x);
double lgamma_r(double x, int *signp);

/* Sign of Gamma(x) set by lgamma() (XSI/POSIX) */
extern int signgam;

/* Bessel functions of the first kind (XSI/POSIX) */
double j0(double x);
double j1(double x);
double jn(int n, double x);

/* Bessel functions of the second kind (XSI/POSIX) */
double y0(double x);
double y1(double x);
double yn(int n, double x);

/* Float versions (f suffix) */
float sinf(float x);
float cosf(float x);
float tanf(float x);
float asinf(float x);
float acosf(float x);
float atanf(float x);
float atan2f(float y, float x);
void  sincosf(float x, float *s, float *c);
float sinhf(float x);
float coshf(float x);
float tanhf(float x);
float asinhf(float x);
float acoshf(float x);
float atanhf(float x);
float expf(float x);
float exp2f(float x);
float expm1f(float x);
float logf(float x);
float log2f(float x);
float log10f(float x);
float log1pf(float x);
float powf(float x, float y);
float sqrtf(float x);
float cbrtf(float x);
float hypotf(float x, float y);

/* C23 float exponential and logarithmic extensions */
float exp10f(float x);
float exp10m1f(float x);
float exp2m1f(float x);
float log10p1f(float x);
float log2p1f(float x);
float logp1f(float x);

/* C23 float power and root functions */
float pownf(float x, intmax_t n);
float powrf(float x, float y);
float rootnf(float x, int n);
float compoundf(float x, intmax_t n);
float rsqrtf(float x);

float fabsf(float x);
float fmodf(float x, float y);
float remainderf(float x, float y);
float remquof(float x, float y, int *quo);
float fmaxf(float x, float y);
float fminf(float x, float y);
float fdimf(float x, float y);
float ceilf(float x);
float floorf(float x);
float truncf(float x);
float roundf(float x);
float roundevenf(float x);
float rintf(float x);
float nearbyintf(float x);
long      lroundf(float x);
long long llroundf(float x);
long      lrintf(float x);
long long llrintf(float x);
float frexpf(float x, int *exp);
float ldexpf(float x, int exp);
float modff(float x, float *iptr);
float scalbnf(float x, int n);
float scalblnf(float x, long n);
int   ilogbf(float x);
float logbf(float x);
float nextafterf(float x, float y);
float nexttowardf(float x, long double y);
float copysignf(float x, float y);
float nanf(const char *tagp);
float erff(float x);
float erfcf(float x);
float tgammaf(float x);
float lgammaf(float x);
float j0f(float x);
float j1f(float x);
float jnf(int n, float x);
float y0f(float x);
float y1f(float x);
float ynf(int n, float x);

/* Long double versions (l suffix) */
long double sinl(long double x);
long double cosl(long double x);
long double tanl(long double x);
long double asinl(long double x);
long double acosl(long double x);
long double atanl(long double x);
long double atan2l(long double y, long double x);
void        sincosl(long double x, long double *s, long double *c);
long double sinhl(long double x);
long double coshl(long double x);
long double tanhl(long double x);
long double asinhl(long double x);
long double acoshl(long double x);
long double atanhl(long double x);
long double expl(long double x);
long double exp2l(long double x);
long double expm1l(long double x);
long double logl(long double x);
long double log2l(long double x);
long double log10l(long double x);
long double log1pl(long double x);
long double powl(long double x, long double y);
long double sqrtl(long double x);
long double cbrtl(long double x);
long double hypotl(long double x, long double y);

/* C23 long double exponential and logarithmic extensions */
long double exp10l(long double x);
long double exp10m1l(long double x);
long double exp2m1l(long double x);
long double log10p1l(long double x);
long double log2p1l(long double x);
long double logp1l(long double x);

/* C23 long double power and root functions */
long double pownl(long double x, intmax_t n);
long double powrl(long double x, long double y);
long double rootnl(long double x, int n);
long double compoundl(long double x, intmax_t n);
long double rsqrtl(long double x);

long double fabsl(long double x);
long double fmodl(long double x, long double y);
long double remainderl(long double x, long double y);
long double remquol(long double x, long double y, int *quo);
long double fmaxl(long double x, long double y);
long double fminl(long double x, long double y);
long double fdiml(long double x, long double y);
long double ceill(long double x);
long double floorl(long double x);
long double truncl(long double x);
long double roundl(long double x);
long double roundevenl(long double x);
long double rintl(long double x);
long double nearbyintl(long double x);
long      lroundl(long double x);
long long llroundl(long double x);
long      lrintl(long double x);
long long llrintl(long double x);
long double frexpl(long double x, int *exp);
long double ldexpl(long double x, int exp);
long double modfl(long double x, long double *iptr);
long double scalbnl(long double x, int n);
long double scalblnl(long double x, long n);
int         ilogbl(long double x);
long double logbl(long double x);
long double nextafterl(long double x, long double y);
long double nexttowardl(long double x, long double y);
long double copysignl(long double x, long double y);
long double nanl(const char *tagp);
long double erfl(long double x);
long double erfcl(long double x);
long double tgammal(long double x);
long double lgammal(long double x);
long double j0l(long double x);
long double j1l(long double x);
long double jnl(int n, long double x);
long double y0l(long double x);
long double y1l(long double x);
long double ynl(int n, long double x);

/* C23: pi-argument trigonometric functions */
double sinpi(double x);
double cospi(double x);
double tanpi(double x);
double asinpi(double x);
double acospi(double x);
double atanpi(double x);

/* Float variants */
float sinpif(float x);
float cospif(float x);
float tanpif(float x);
float asinpif(float x);
float atanpif(float x);

/* Long double variants */
long double sinpil(long double x);
long double cospil(long double x);
long double tanpil(long double x);
long double asinpil(long double x);
long double atanpil(long double x);

/* C23: fromfp family — convert floating-point values with explicit rounding */
int fromfp(double *y, double x, fenv_t *envp, int rounding_mode);
int fromfpx(double *y, double x, fenv_t *envp, int rounding_mode);
int ufromfp(unsigned int *y, double x, fenv_t *envp, int rounding_mode);
int ufromfpx(unsigned int *y, double x, fenv_t *envp, int rounding_mode);

/* Float fromfp variants */
int fromfpf(float *y, float x, fenv_t *envp, int rounding_mode);
int fromfpxf(float *y, float x, fenv_t *envp, int rounding_mode);
int ufromfpf(unsigned int *y, float x, fenv_t *envp, int rounding_mode);
int ufromfpxf(unsigned int *y, float x, fenv_t *envp, int rounding_mode);

/* Long double fromfp variants */
int fromfpxl(long double *y, long double x, fenv_t *envp, int rounding_mode);
int fromfpl(long double *y, long double x, fenv_t *envp, int rounding_mode);
int ufromfpul(unsigned int *y, long double x, fenv_t *envp, int rounding_mode);
int ufromfpxl(unsigned int *y, long double x, fenv_t *envp, int rounding_mode);

#endif /* _MATH_H */
