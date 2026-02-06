#ifndef _MATH_H
#define _MATH_H

#include <stdint.h>

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
#define INFINITY     __builtin_inff()
#define NAN          __builtin_nanf("")

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

/* Basic trigonometric functions */
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);

/* Hyperbolic functions */
double sinh(double x);
double cosh(double x);
double tanh(double x);

/* Exponential and logarithmic functions */
double exp(double x);
double log(double x);
double log10(double x);
double log2(double x);
double pow(double x, double y);
double sqrt(double x);

/* Absolute value and remainder */
double fabs(double x);
double fmod(double x, double y);
double remainder(double x, double y);

/* Min/Max */
double fmax(double x, double y);
double fmin(double x, double y);
double fdim(double x, double y);

/* Rounding */
double ceil(double x);
double floor(double x);
double trunc(double x);
double round(double x);
double rint(double x);

/* Float versions (f suffix) */
float sinf(float x);
float cosf(float x);
float tanf(float x);
float sqrtf(float x);
float powf(float x, float y);
float fabsf(float x);
float fmodf(float x, float y);
float fmaxf(float x, float y);
float fminf(float x, float y);
float ceilf(float x);
float floorf(float x);
float truncf(float x);
float roundf(float x);

/* Long double versions (l suffix) */
long double sinl(long double x);
long double cosl(long double x);
long double tanl(long double x);
long double sqrtl(long double x);
long double powl(long double x, long double y);
long double fabsl(long double x);
long double fmodl(long double x, long double y);
long double fmaxl(long double x, long double y);
long double fminl(long double x, long double y);
long double ceill(long double x);
long double floorl(long double x);
long double truncl(long double x);
long double roundl(long double x);

#endif /* _MATH_H */
