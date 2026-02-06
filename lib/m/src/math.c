/*
 * math.c - Basic math library functions
 *
 * Stub implementations for commonly used math functions.
 * TODO: Replace with proper implementations using FPU or software emulation.
 */

#include <math.h>

/* Trigonometric functions - stubs */
double sin(double x) { (void)x; return 0.0; }
double cos(double x) { (void)x; return 1.0; }
double tan(double x) { (void)x; return 0.0; }
double asin(double x) { (void)x; return 0.0; }
double acos(double x) { (void)x; return 0.0; }
double atan(double x) { (void)x; return 0.0; }
double atan2(double y, double x) { (void)y; (void)x; return 0.0; }

/* Hyperbolic functions - stubs */
double sinh(double x) { (void)x; return 0.0; }
double cosh(double x) { (void)x; return 1.0; }
double tanh(double x) { (void)x; return 0.0; }

/* Exponential and logarithmic functions - stubs */
double exp(double x) { (void)x; return 1.0; }
double log(double x) { (void)x; return 0.0; }
double log10(double x) { (void)x; return 0.0; }
double log2(double x) { (void)x; return 0.0; }
double pow(double x, double y) { (void)x; (void)y; return 1.0; }
double sqrt(double x) { (void)x; return 0.0; }

/* Absolute value - actual implementation */
double fabs(double x) {
    return (x < 0) ? -x : x;
}

/* Remainder functions */
double fmod(double x, double y) {
    if (y == 0.0) return NAN;
    return x - (int)(x / y) * y;
}

double remainder(double x, double y) {
    if (y == 0.0) return NAN;
    double n = x / y;
    double q = (n >= 0) ? (int)(n + 0.5) : (int)(n - 0.5);
    return x - q * y;
}

/* Min/Max - actual implementations */
double fmax(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    return (x > y) ? x : y;
}

double fmin(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    return (x < y) ? x : y;
}

/* Positive difference */
double fdim(double x, double y) {
    return (x > y) ? (x - y) : 0.0;
}

/* Rounding functions */
double ceil(double x) {
    int i = (int)x;
    return (x > i) ? (double)(i + 1) : (double)i;
}

double floor(double x) {
    int i = (int)x;
    return (x < i) ? (double)(i - 1) : (double)i;
}

double trunc(double x) {
    return (double)(int)x;
}

double round(double x) {
    return (x >= 0) ? floor(x + 0.5) : ceil(x - 0.5);
}

double rint(double x) {
    /* Round to nearest integer, ties to even (banker's rounding) */
    double n = floor(x + 0.5);
    /* If exactly halfway, round to even */
    if (x + 0.5 == n && (int)n % 2 != 0) {
        n -= 1.0;
    }
    return n;
}

/* Float versions */
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
float tanf(float x) { return (float)tan(x); }
float sqrtf(float x) { return (float)sqrt(x); }
float powf(float x, float y) { return (float)pow(x, y); }
float fabsf(float x) { return (x < 0) ? -x : x; }
float fmodf(float x, float y) { return (float)fmod(x, y); }
float fmaxf(float x, float y) { return (float)fmax(x, y); }
float fminf(float x, float y) { return (float)fmin(x, y); }
float ceilf(float x) { return (float)ceil(x); }
float floorf(float x) { return (float)floor(x); }
float truncf(float x) { return (float)trunc(x); }
float roundf(float x) { return (float)round(x); }

/* Long double versions (same as double on i386) */
long double sinl(long double x) { return sin(x); }
long double cosl(long double x) { return cos(x); }
long double tanl(long double x) { return tan(x); }
long double sqrtl(long double x) { return sqrt(x); }
long double powl(long double x, long double y) { return pow(x, y); }
long double fabsl(long double x) { return (x < 0) ? -x : x; }
long double fmodl(long double x, long double y) { return fmod(x, y); }
long double fmaxl(long double x, long double y) { return fmax(x, y); }
long double fminl(long double x, long double y) { return fmin(x, y); }
long double ceill(long double x) { return ceil(x); }
long double floorl(long double x) { return floor(x); }
long double truncl(long double x) { return trunc(x); }
long double roundl(long double x) { return round(x); }
