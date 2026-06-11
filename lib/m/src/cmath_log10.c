/*
 * cmath_log10.c — Complex base-10 logarithm (glibc extension)
 *
 * Implements clog10(z) = clog(z) / ln(10), computing the complex
 * base-10 logarithm.  The branch cut is along the negative real axis,
 * matching the existing clog family.
 *
 * Implementation:
 *   clog10(z) = clog(z) / M_LN10
 *             = (1/2 * log|z|² + i * atan2(y, x)) / ln(10)
 *
 * Feature-test guard: _GNU_SOURCE
 */

#include <complex.h>
#include <math.h>

#ifndef M_LN10
#define M_LN10  2.30258509299404568402
#endif

double complex clog10(double complex z) {
    double complex cl = clog(z);
    return cl / M_LN10;
}

float complex clog10f(float complex z) {
    return (float complex)clog10((double complex)z);
}

long double complex clog10l(long double complex z) {
    return (long double complex)clog10((double complex)z);
}
