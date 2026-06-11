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

/* ====================================================================
 * Long double versions — native 80-bit x87, never narrowed to double.
 *
 * fsin/fcos/fptan accept |x| < 2^63; for larger |x| we range-reduce
 * mod 2pi with fprem1 (the same loop the double sin/cos use) so the
 * argument is back in fsin's accurate domain.
 * ==================================================================== */

long double sinl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return NAN;
    long double res;
    __asm__ __volatile__(
        "fldt %1\n\t"
        "1: fsin\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstpt %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

long double cosl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return NAN;
    long double res;
    __asm__ __volatile__(
        "fldt %1\n\t"
        "1: fcos\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstpt %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

void sincosl(long double x, long double *s, long double *c)
{
    if (__isnanl(x) || __isinfl(x)) { *s = NAN; *c = NAN; return; }
    __asm__ __volatile__(
        "fldt %2\n\t"
        "1: fsincos\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstpt %1\n\t"
        "fstpt %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(*s), "=m"(*c) : "m"(x) : "ax", "cc");
}

long double tanl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return NAN;
    long double res;
    __asm__ __volatile__(
        "fldt %1\n\t"
        "1: fptan\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 2f\n\t"
        "fstp %%st(0)\n\t"          /* pop the 1.0 fptan pushed */
        "fstpt %0\n\t"
        "jmp 3f\n\t"
        "2: fldpi\n\t"
        "fadd %%st(0)\n\t"
        "fxch %%st(1)\n\t"
        "fprem1\n\t"
        "fstp %%st(1)\n\t"
        "jmp 1b\n\t"
        "3:"
        : "=m"(res) : "m"(x) : "ax", "cc");
    return res;
}

long double sqrtl(long double x)
{
    if (__isnanl(x)) return x;
    if (x < 0.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0L) return x;            /* preserve sign of zero */
    if (__isinfl(x)) return x;
    long double res;
    __asm__ __volatile__("fldt %1; fsqrt; fstpt %0" : "=m"(res) : "m"(x));
    return res;
}

/* x^y = 2^(y*log2(x)) at full 80-bit width; C99 Annex F.9.4.4 table. */
static int powl_int_kind(long double y)
{
    if (__isinfl(y) || __isnanl(y)) return 0;
    long double ay = (y < 0) ? -y : y;
    if (ay >= 18446744073709551616.0L) return 1;   /* 2^64: all even */
    long double yi = truncl(y);
    if (yi != y) return 0;
    long long ll = (long long)yi;
    return (ll & 1) ? 2 : 1;
}

long double powl(long double x, long double y)
{
    if (y == 0.0L) return 1.0L;
    if (x == 1.0L) return 1.0L;
    if (__isnanl(x) || __isnanl(y)) return NAN;

    int ik = powl_int_kind(y);
    int y_odd_int = (ik == 2);

    if (__isinfl(y)) {
        long double ax = (x < 0) ? -x : x;
        if (ax == 1.0L) return 1.0L;
        if (ax < 1.0L)  return (y > 0) ? 0.0L : INFINITY;
        return (y > 0) ? INFINITY : 0.0L;
    }
    if (x == 0.0L) {
        if (y < 0.0L) {
            errno = ERANGE; feraiseexcept(FE_DIVBYZERO);
            if (y_odd_int && __signbitl(x)) return -INFINITY;
            return INFINITY;
        }
        if (y_odd_int && __signbitl(x)) return -0.0L;
        return 0.0L;
    }
    if (__isinfl(x)) {
        if (x > 0.0L) return (y > 0.0L) ? INFINITY : 0.0L;
        if (y > 0.0L) return y_odd_int ? -INFINITY : INFINITY;
        return y_odd_int ? -0.0L : 0.0L;
    }
    if (x < 0.0L) {
        if (ik == 0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
        long double r = powl(-x, y);
        return y_odd_int ? -r : r;
    }

    /* x > 0 finite, y finite non-zero: fyl2x then exp2 at 80-bit. */
    long double yl2x;
    __asm__ __volatile__(
        "fldt   %2\n\t"            /* y */
        "fldt   %1\n\t"            /* x */
        "fyl2x\n\t"               /* y * log2(x) */
        "fstpt  %0"
        : "=m"(yl2x) : "m"(x), "m"(y));
    if (yl2x >  16384.0L) { errno = ERANGE; return INFINITY; }
    if (yl2x < -16446.0L) { errno = ERANGE; return 0.0L; }
    return exp2l(yl2x);
}

long double fabsl(long double x) { return (x < 0) ? -x : x; }

/* fmodl / remainderl — x87 fprem (truncated) / fprem1 (round-nearest). */
long double fmodl(long double x, long double y)
{
    if (__isnanl(x) || __isnanl(y) || __isinfl(x) || y == 0.0L) {
        feraiseexcept(FE_INVALID); errno = EDOM; return NAN;
    }
    if (__isinfl(y)) return x;
    long double res;
    __asm__ __volatile__(
        "fldt %2\n\t"             /* st0 = y */
        "fldt %1\n\t"             /* st0 = x ; st1 = y */
        "1: fprem\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 1b\n\t"               /* C2 set => reduction incomplete */
        "fstpt %0\n\t"
        "fstp %%st(0)"
        : "=m"(res) : "m"(x), "m"(y) : "ax", "cc");
    return res;
}

long double remainderl(long double x, long double y)
{
    if (__isnanl(x) || __isnanl(y) || __isinfl(x) || y == 0.0L) {
        feraiseexcept(FE_INVALID); errno = EDOM; return NAN;
    }
    if (__isinfl(y)) return x;
    long double res;
    __asm__ __volatile__(
        "fldt %2\n\t"
        "fldt %1\n\t"
        "1: fprem1\n\t"
        "fnstsw %%ax\n\t"
        "sahf\n\t"
        "jp 1b\n\t"
        "fstpt %0\n\t"
        "fstp %%st(0)"
        : "=m"(res) : "m"(x), "m"(y) : "ax", "cc");
    return res;
}

long double remquol(long double x, long double y, int *quo)
{
    /* compute the IEEE remainder at 80-bit; derive a low-bits quotient. */
    long double r = remainderl(x, y);
    if (quo) {
        if (__isnanl(x) || __isnanl(y) || y == 0.0L || __isinfl(x)) { *quo = 0; }
        else {
            long double q = roundl((x - r) / y);
            long long qi = (long long)fmodl(q, 8.0L);
            *quo = (int)qi;
        }
    }
    return r;
}

/* fma: x87 has no fused MAC; compute x*y+z at 80-bit width (not truly
 * fused, but full extended precision — far better than via double). */
long double fmal(long double x, long double y, long double z)
{
    return x * y + z;
}

long double fmaxl(long double x, long double y)
{
    if (__isnanl(x)) return y;
    if (__isnanl(y)) return x;
    return (x > y) ? x : y;
}
long double fminl(long double x, long double y)
{
    if (__isnanl(x)) return y;
    if (__isnanl(y)) return x;
    return (x < y) ? x : y;
}
long double fdiml(long double x, long double y)
{
    if (__isnanl(x) || __isnanl(y)) return NAN;
    return (x > y) ? (x - y) : 0.0L;
}

/* Rounding — frndint at 80-bit width with explicit modes. */
static long double wrap_rndint_mode(long double x, unsigned mode)
{
    uint16_t cw, ncw; long double res;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    ncw = (uint16_t)((cw & ~0x0c00u) | ((mode & 3u) << 10));
    __asm__ __volatile__("fldcw %2; fldt %1; frndint; fstpt %0; fldcw %3"
                         : "=m"(res) : "m"(x), "m"(ncw), "m"(cw));
    return res;
}

long double ceill(long double x)
{
    if (__isnanl(x) || __isinfl(x) || x == 0.0L) return x;
    return wrap_rndint_mode(x, 2u);    /* round up */
}
long double floorl(long double x)
{
    if (__isnanl(x) || __isinfl(x) || x == 0.0L) return x;
    return wrap_rndint_mode(x, 1u);    /* round down */
}
long double truncl(long double x)
{
    if (__isnanl(x) || __isinfl(x) || x == 0.0L) return x;
    return wrap_rndint_mode(x, 3u);    /* toward zero */
}
long double roundl(long double x)      /* half away from zero */
{
    if (__isnanl(x) || __isinfl(x) || x == 0.0L) return x;
    long double t = wrap_rndint_mode(x, 3u);   /* trunc */
    long double frac = x - t;
    if (frac >= 0.5L)  return t + 1.0L;
    if (frac <= -0.5L) return t - 1.0L;
    return t;
}
long double roundevenl(long double x)  /* half to even = round-nearest */
{
    if (__isnanl(x) || __isinfl(x) || x == 0.0L) return x;
    return wrap_rndint_mode(x, 0u);    /* nearest, ties to even */
}

long double fmaximuml(long double x, long double y)
{
    if (__isnanl(x) || __isnanl(y)) return NAN;
    if (x == 0.0L && y == 0.0L) return __signbitl(x) ? y : x;  /* +0 > -0 */
    return (x > y) ? x : y;
}
long double fminimuml(long double x, long double y)
{
    if (__isnanl(x) || __isnanl(y)) return NAN;
    if (x == 0.0L && y == 0.0L) return __signbitl(x) ? x : y;  /* -0 < +0 */
    return (x < y) ? x : y;
}
long double fmaximum_numl(long double x, long double y)
{
    if (__isnanl(x)) return y;
    if (__isnanl(y)) return x;
    if (x == 0.0L && y == 0.0L) return __signbitl(x) ? y : x;
    return (x > y) ? x : y;
}
long double fminimum_numl(long double x, long double y)
{
    if (__isnanl(x)) return y;
    if (__isnanl(y)) return x;
    if (x == 0.0L && y == 0.0L) return __signbitl(x) ? x : y;
    return (x < y) ? x : y;
}
long double fmaximum_magl(long double x, long double y)
{
    if (__isnanl(x) || __isnanl(y)) return NAN;
    long double ax = (x < 0) ? -x : x, ay = (y < 0) ? -y : y;
    if (ax > ay) return x;
    if (ay > ax) return y;
    return fmaximuml(x, y);
}
long double fminimum_magl(long double x, long double y)
{
    if (__isnanl(x) || __isnanl(y)) return NAN;
    long double ax = (x < 0) ? -x : x, ay = (y < 0) ? -y : y;
    if (ax < ay) return x;
    if (ay < ax) return y;
    return fminimuml(x, y);
}

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
