/*
 * math.c - Math library functions
 *
 * Implements exponential, logarithmic, and trigonometric functions
 * using Taylor series approximations and mathematical identities.
 */

#include <errno.h>
#include <fenv.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

/* Constants (with guards to avoid redefinition) */
#ifndef M_PI
#define M_PI      3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2    1.57079632679489661923
#endif
#ifndef M_PI_4
#define M_PI_4    0.78539816339744830962
#endif
#ifndef M_E
#define M_E       2.71828182845904523536
#endif
#ifndef M_LN2
#define M_LN2     0.69314718055994530942
#endif
#ifndef M_LN10
#define M_LN10    2.30258509299404568402
#endif
#ifndef M_LOG2E
#define M_LOG2E   1.44269504088896340736
#endif

/* Float versions */
float sinf(float x) { return(float)sin(x); }
float cosf(float x) { return(float)cos(x); }
float tanf(float x) { return(float)tan(x); }
void sincosf(float x, float *s, float *c) { double ds, dc; sincos(x, &ds, &dc); *s = (float)ds; *c = (float)dc; }
float sqrtf(float x) { return(float)sqrt(x); }
float powf(float x, float y) { return(float)pow(x, y); }
float fabsf(float x) { return (x < 0) ? -x : x; }
float fmodf(float x, float y) { return(float)fmod(x, y); }
float remainderf(float x, float y) { return(float)remainder(x, y); }
float remquof(float x, float y, int *quo) { return(float)remquo(x, y, quo); }
float fmaf(float x, float y, float z) { return(float)fma(x, y, z); }
float fmaxf(float x, float y) { return(float)fmax(x, y); }
float fminf(float x, float y) { return(float)fmin(x, y); }
float fdimf(float x, float y) { return(float)fdim(x, y); }
float ceilf(float x) { return(float)ceil(x); }
float floorf(float x) { return(float)floor(x); }
float truncf(float x) { return(float)trunc(x); }
float roundf(float x) { return(float)round(x); }
float roundevenf(float x) { return(roundeven(x)); }
float fmaximumf(float x, float y) { return(float)fmaximum(x, y); }
float fminimumf(float x, float y) { return(float)fminimum(x, y); }
float fmaximum_numf(float x, float y) { return(float)fmaximum_num(x, y); }
float fminimum_numf(float x, float y) { return(float)fminimum_num(x, y); }
float fmaximum_magf(float x, float y) { return(float)fmaximum_mag(x, y); }
float fminimum_magf(float x, float y) { return(float)fminimum_mag(x, y); }

/* Long double versions (same as double on i386) */
long double sinl(long double x) { return sin(x); }
long double cosl(long double x) { return cos(x); }
long double tanl(long double x) { return tan(x); }
void sincosl(long double x, long double *s, long double *c) { double ds, dc; sincos(x, &ds, &dc); *s = ds; *c = dc; }
long double sqrtl(long double x) { return sqrt(x); }
long double powl(long double x, long double y) { return pow(x, y); }
long double fabsl(long double x) { return (x < 0) ? -x : x; }
long double fmodl(long double x, long double y) { return fmod(x, y); }
long double remainderl(long double x, long double y) { return remainder(x, y); }
long double remquol(long double x, long double y, int *quo) { return remquo(x, y, quo); }
long double fmal(long double x, long double y, long double z) { return fma(x, y, z); }
long double fmaxl(long double x, long double y) { return fmax(x, y); }
long double fminl(long double x, long double y) { return fmin(x, y); }
long double fdiml(long double x, long double y) { return fdim(x, y); }
long double ceill(long double x) { return ceil(x); }
long double floorl(long double x) { return floor(x); }
long double truncl(long double x) { return trunc(x); }
long double roundl(long double x) { return round(x); }
long double roundevenl(long double x) { return roundeven(x); }
long double fmaximuml(long double x, long double y) { return fmaximum(x, y); }
long double fminimuml(long double x, long double y) { return fminimum(x, y); }
long double fmaximum_numl(long double x, long double y) { return fmaximum_num(x, y); }
long double fminimum_numl(long double x, long double y) { return fminimum_num(x, y); }
long double fmaximum_magl(long double x, long double y) { return fmaximum_mag(x, y); }
long double fminimum_magl(long double x, long double y) { return fminimum_mag(x, y); }

/* C23 pi-argument trig float wrappers */
float sinpif(float x) { return (float)sinpi(x); }
float cospif(float x) { return (float)cospi(x); }
float tanpif(float x) { return (float)tanpi(x); }
float asinpif(float x) { return (float)asinpi(x); }
float atanpif(float x) { return (float)atanpi(x); }

/* C23 pi-argument trig long double wrappers */
long double sinpil(long double x) { return sinpi(x); }
long double cospil(long double x) { return cospi(x); }
long double tanpil(long double x) { return tanpi(x); }
long double asinpil(long double x) { return asinpi(x); }
long double atanpil(long double x) { return atanpi(x); }
