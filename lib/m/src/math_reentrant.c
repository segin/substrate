/*
 * math_reentrant.c — Reentrant gamma functions (glibc / 4.3BSD-Reno)
 *
 * Provides lgammaf_r() and lgammal_r() — thread-safe variants of
 * lgammaf() / lgammal() that return the sign of Γ(x) via an
 * integer pointer instead of the global signgam.
 *
 * These variants do NOT read or write the global signgam variable.
 *
 * Feature-test guard: _GNU_SOURCE / _DEFAULT_SOURCE
 */

#include <math.h>

float lgammaf_r(float x, int *signp) {
    int sg;
    float r = (float)lgamma_r((double)x, &sg);
    if (signp) *signp = sg;
    return r;
}

/*
 * lgammal_r() now lives in mathl.c, computed in genuine 80-bit precision
 * (Stirling series + recurrence + reflection) rather than casting through
 * double's lgamma_r().  Keeping a second definition here would collide at
 * link time, so it is intentionally absent.
 */
