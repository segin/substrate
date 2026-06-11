/*
 * mathl.c - Long double (l-suffix) math library variants
 *
 * On i386, x87 `long double` is the native 80-bit extended-precision
 * format (64-bit explicit mantissa, 15-bit exponent, sizeof 12 with
 * 2 bytes of tail padding).  The x87 register stack holds exactly this
 * format, so the *l functions here compute natively in 80 bits via x87
 * inline asm — `fldt`/`fstpt` move an 80-bit value to/from memory and
 * every transcendental primitive (fsin, fcos, fptan, fpatan, fyl2x,
 * f2xm1, fscale, fsqrt, frndint, fxtract) operates on st(0) at the full
 * register width.
 *
 * This replaces the previous implementation, which cast every argument
 * to `double`, computed in double, and cast back — losing ~11 mantissa
 * bits and the entire long-double exponent range (inputs above DBL_MAX
 * wrongly became inf).
 *
 * Functions implemented in math_wrap.c (sinl, cosl, tanl, sqrtl, powl,
 * fabsl, fmodl, floorl, ceill, truncl, roundl, the fmin/fmax family,
 * the sinpil family, etc.) are NOT re-implemented here; this file owns
 * the rest of the l-suffix surface.
 *
 * Functions left double-based (no x87 primitive; genuine 80-bit too
 * large for this pass) are clearly marked DOUBLE-BASED below:
 *   erfl, erfcl, tgammal, lgammal, lgammal_r, j0l/j1l/jnl/y0l/y1l/ynl.
 */

#include <errno.h>
#include <fenv.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

/* Long-double constants (extended precision literals). */
#define LDBL_PI     3.141592653589793238462643383279502884L
#define LDBL_PI_2   1.570796326794896619231321691639751442L
#define LDBL_PI_4   0.785398163397448309615660845819875721L

/* ====================================================================
 * 80-bit representation helpers
 *
 * x87 long double in memory: 10 significant bytes (sizeof is 12 with 2
 * padding bytes).  Byte layout (little-endian):
 *   bytes 0..7 : 64-bit mantissa, bit 63 = explicit integer bit
 *   bytes 8..9 : bit 15 = sign, bits 0..14 = biased exponent (bias 16383)
 * ==================================================================== */

union ldshape {
    long double value;
    struct {
        uint64_t mant;
        uint16_t sexp;          /* sign (bit 15) + 15-bit exponent */
        uint16_t pad;
    } parts;
};

#define LD_EXP_BIAS  16383
#define LD_EXP_MASK  0x7fffu
#define LD_SIGN_MASK 0x8000u

/* ====================================================================
 * Internal x87 primitives operating natively at 80-bit width.
 * ==================================================================== */

/* 2^x for finite x, full 80-bit precision.  Splits x = i + f with
 * i = round-to-nearest(x), f in [-0.5, 0.5] (inside the f2xm1 domain),
 * then 2^x = 2^f * 2^i via f2xm1 + fscale.  Mirrors math_exp.c's
 * x87_exp2 but never narrows to double. */
static inline long double x87_exp2l(long double x) {
    long double res;
    __asm__ __volatile__(
        "fldt   %1\n\t"               /* st0 = x */
        "fld    %%st(0)\n\t"          /* st0 = x ;  st1 = x */
        "frndint\n\t"                 /* st0 = i ;  st1 = x */
        "fxch   %%st(1)\n\t"          /* st0 = x ;  st1 = i */
        "fsub   %%st(1), %%st(0)\n\t" /* st0 = x - i = f */
        "f2xm1\n\t"                   /* st0 = 2^f - 1 ; st1 = i */
        "fld1\n\t"
        "faddp\n\t"                   /* st0 = 2^f ; st1 = i */
        "fscale\n\t"                  /* st0 = 2^f * 2^i = 2^x ; st1 = i */
        "fstp   %%st(1)\n\t"          /* drop i */
        "fstpt  %0"
        : "=m"(res)
        : "m"(x));
    return res;
}

/* y * log2(x) at 80-bit width via fyl2x. */
static inline long double x87_yl2x(long double y, long double x) {
    long double res;
    __asm__ __volatile__(
        "fldt   %1\n\t"               /* st0 = y */
        "fldt   %2\n\t"               /* st0 = x ; st1 = y */
        "fyl2x\n\t"                   /* st0 = y * log2(x) */
        "fstpt  %0"
        : "=m"(res) : "m"(y), "m"(x));
    return res;
}

/* sqrt at 80-bit width. */
static inline long double x87_sqrtl(long double x) {
    long double res;
    __asm__ __volatile__("fldt %1; fsqrt; fstpt %0" : "=m"(res) : "m"(x));
    return res;
}

/* fpatan: atan(y/x) at 80-bit width. */
static inline long double x87_atan2l(long double y, long double x) {
    long double res;
    __asm__ __volatile__("fldt %1; fldt %2; fpatan; fstpt %0"
                         : "=m"(res) : "m"(y), "m"(x));
    return res;
}

/* round to integer using the current rounding mode (frndint). */
static inline long double x87_rndintl(long double x) {
    long double res;
    __asm__ __volatile__("fldt %1; frndint; fstpt %0" : "=m"(res) : "m"(x));
    return res;
}

/* Set the x87 rounding-control field, run frndint, restore.  mode is
 * the 2-bit RC value: 00 nearest, 01 down, 10 up, 11 truncate. */
static long double x87_rndint_mode(long double x, unsigned mode)
{
    uint16_t cw, ncw;
    long double res;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    ncw = (uint16_t)((cw & ~0x0c00u) | ((mode & 3u) << 10));
    __asm__ __volatile__(
        "fldcw  %2\n\t"
        "fldt   %1\n\t"
        "frndint\n\t"
        "fstpt  %0\n\t"
        "fldcw  %3"
        : "=m"(res)
        : "m"(x), "m"(ncw), "m"(cw));
    return res;
}

/* truncate toward zero at 80-bit width (RC = 11). */
static long double x87_rndintl_trunc(long double x)
{
    return x87_rndint_mode(x, 3u);
}

/* ====================================================================
 * Exponential and logarithmic — x87 fyl2x / f2xm1 / fscale.
 * ==================================================================== */

/* exp(x) — Cody-Waite reduction in base e for full 80-bit accuracy.
 *
 * Computing 2^(x*log2e) directly loses bits in the product x*log2e for
 * large |x| (the rounded product's fractional part is coarse).  Instead
 * reduce in base e:
 *   n = round(x / ln2)
 *   r = x - n*ln2,  with ln2 = ln2_hi + ln2_lo (Cody-Waite split) so
 *       n*ln2 is subtracted in two exact-ish pieces; |r| <= ln2/2.
 *   exp(x) = 2^n * e^r,  e^r = 2^(r*log2e) via f2xm1 (r*log2e in
 *       [-0.5, 0.5], a tiny product that keeps all bits).
 */
long double expl(long double x)
{
    if (__isnanl(x)) return x;
    if (x == 0.0L) return 1.0L;
    if (__isinfl(x)) return (x > 0) ? INFINITY : 0.0L;
    if (x >  11356.523406294143949L) { errno = ERANGE; return INFINITY; }
    if (x < -11399.499612568909482L) { errno = ERANGE; return 0.0L; }

    static const long double LOG2E   = 1.442695040888963407359924681001892137L;
    /* Cody-Waite split of ln2: LN2_HI has its low mantissa bits zero so
     * n*LN2_HI is computed exactly for any integer n in range; LN2_LO
     * carries the remaining bits of ln2. */
    static const long double LN2_HI  = 0.6931471805599453972490664455108344554901123046875L;
    static const long double LN2_LO  = -8.7831834324052657887414612170327244745879320e-17L;

    long double n = x87_rndintl(x * LOG2E);   /* nearest integer */
    long double r = (x - n * LN2_HI) - n * LN2_LO;
    /* e^r = 2^(r*log2e); r*log2e in [-0.5, 0.5]. */
    long double f = r * LOG2E;
    long double er;
    __asm__ __volatile__(
        "fldt   %1\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fstpt  %0"
        : "=m"(er) : "m"(f));
    return scalbnl(er, (int)n);
}

long double exp2l(long double x)
{
    if (__isnanl(x)) return x;
    if (x == 0.0L) return 1.0L;
    if (__isinfl(x)) return (x > 0) ? INFINITY : 0.0L;
    if (x >  16384.0L) { errno = ERANGE; return INFINITY; }
    if (x < -16446.0L) { errno = ERANGE; return 0.0L; }
    return x87_exp2l(x);
}

long double exp10l(long double x)
{
    if (__isnanl(x)) return x;
    if (x == 0.0L) return 1.0L;
    if (__isinfl(x)) return (x > 0) ? INFINITY : 0.0L;
    if (x >  4932.0L) { errno = ERANGE; return INFINITY; }
    if (x < -4951.0L) { errno = ERANGE; return 0.0L; }
    static const long double LOG2_10 = 3.321928094887362347870319429489390176L;
    return x87_exp2l(x * LOG2_10);
}

/* expm1l(x) = e^x - 1.  For |x| > ~0.347 direct subtraction is fine;
 * for small x use f2xm1 on x*log2(e): f2xm1 returns 2^t - 1 exactly,
 * so e^x - 1 = 2^(x*log2e) - 1 with no cancellation. */
long double expm1l(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return (x > 0) ? INFINITY : -1.0L;
    if (x == 0.0L) return x;             /* preserve sign of zero */

    static const long double LOG2E = 1.442695040888963407359924681001892137L;
    long double t = x * LOG2E;
    if (t >= -1.0L && t <= 1.0L) {
        long double res;
        __asm__ __volatile__("fldt %1; f2xm1; fstpt %0" : "=m"(res) : "m"(t));
        return res;
    }
    if (x >  11356.523406294143949L) { errno = ERANGE; return INFINITY; }
    if (x < -40.0L) return -1.0L;
    return expl(x) - 1.0L;
}

long double exp2m1l(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return (x > 0) ? INFINITY : -1.0L;
    if (x == 0.0L) return x;
    if (x >= -1.0L && x <= 1.0L) {
        long double res;
        __asm__ __volatile__("fldt %1; f2xm1; fstpt %0" : "=m"(res) : "m"(x));
        return res;
    }
    return exp2l(x) - 1.0L;
}

long double exp10m1l(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return (x > 0) ? INFINITY : -1.0L;
    if (x == 0.0L) return x;
    static const long double LN10 = 2.302585092994045684017991454684364208L;
    if (x > -0.15L && x < 0.15L) return expm1l(x * LN10);
    return exp10l(x) - 1.0L;
}

/* log(x) = ln(2) * log2(x) via fyl2x with y = ln2. */
long double logl(long double x)
{
    if (__isnanl(x)) return x;
    if (x < 0.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0L) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__isinfl(x)) return x;
    long double res;
    __asm__ __volatile__("fldln2; fldt %1; fyl2x; fstpt %0" : "=m"(res) : "m"(x));
    return res;
}

long double log2l(long double x)
{
    if (__isnanl(x)) return x;
    if (x < 0.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0L) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__isinfl(x)) return x;
    long double res;
    __asm__ __volatile__("fld1; fldt %1; fyl2x; fstpt %0" : "=m"(res) : "m"(x));
    return res;
}

long double log10l(long double x)
{
    if (__isnanl(x)) return x;
    if (x < 0.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0L) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__isinfl(x)) return x;
    long double res;
    __asm__ __volatile__("fldlg2; fldt %1; fyl2x; fstpt %0" : "=m"(res) : "m"(x));
    return res;
}

/* log1pl(x) = ln(1+x).  fyl2xp1 computes y*log2(1+st0) for
 * |st0| < 1 - sqrt(2)/2 ~ 0.293; push ln(2). */
long double log1pl(long double x)
{
    if (__isnanl(x)) return x;
    if (x < -1.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == -1.0L) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__isinfl(x)) return x;
    if (x == 0.0L) return x;
    if (x > -0.2928L && x < 0.2928L) {
        long double res;
        __asm__ __volatile__("fldln2; fldt %1; fyl2xp1; fstpt %0"
                             : "=m"(res) : "m"(x));
        return res;
    }
    return logl(1.0L + x);
}

/* C23 aliases */
long double logp1l(long double x) { return log1pl(x); }

long double log2p1l(long double x)
{
    if (__isnanl(x)) return x;
    if (x < -1.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == -1.0L) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__isinfl(x)) return x;
    if (x == 0.0L) return x;
    if (x > -0.2928L && x < 0.2928L) {
        long double res;
        __asm__ __volatile__("fld1; fldt %1; fyl2xp1; fstpt %0"
                             : "=m"(res) : "m"(x));
        return res;
    }
    return log2l(1.0L + x);
}

long double log10p1l(long double x)
{
    if (__isnanl(x)) return x;
    if (x < -1.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == -1.0L) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    if (__isinfl(x)) return x;
    if (x == 0.0L) return x;
    if (x > -0.2928L && x < 0.2928L) {
        long double res;
        __asm__ __volatile__("fldlg2; fldt %1; fyl2xp1; fstpt %0"
                             : "=m"(res) : "m"(x));
        return res;
    }
    return log10l(1.0L + x);
}

/* ====================================================================
 * Inverse trigonometric — fpatan / fsqrt, full 80-bit.
 * ==================================================================== */

long double atanl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return (x < 0) ? -LDBL_PI_2 : LDBL_PI_2;
    if (x == 0.0L) return x;             /* preserve sign of zero */
    /* fpatan(st1/st0) with st1 = x, st0 = 1 gives atan(x). */
    return x87_atan2l(x, 1.0L);
}

long double atan2l(long double y, long double x)
{
    if (__isnanl(y) || __isnanl(x)) return NAN;

    int y_neg = __signbitl(y);
    int x_neg = __signbitl(x);
    int y_inf = __isinfl(y);
    int x_inf = __isinfl(x);

    if (y == 0.0L) {
        if (!x_neg) return y;
        return y_neg ? -LDBL_PI : LDBL_PI;
    }
    if (x == 0.0L) return y_neg ? -LDBL_PI_2 : LDBL_PI_2;

    if (y_inf) {
        if (x_inf) {
            long double base = x_neg ? (3.0L * LDBL_PI / 4.0L) : LDBL_PI_4;
            return y_neg ? -base : base;
        }
        return y_neg ? -LDBL_PI_2 : LDBL_PI_2;
    }
    if (x_inf) {
        if (!x_neg) return y_neg ? -0.0L : 0.0L;
        return y_neg ? -LDBL_PI : LDBL_PI;
    }
    return x87_atan2l(y, x);
}

/* asin(x) = atan(x / sqrt(1 - x^2)). */
long double asinl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x) || x < -1.0L || x > 1.0L) {
        feraiseexcept(FE_INVALID); errno = EDOM; return NAN;
    }
    if (x == 0.0L) return x;
    if (x == 1.0L) return LDBL_PI_2;
    if (x == -1.0L) return -LDBL_PI_2;
    long double s = x87_sqrtl((1.0L - x) * (1.0L + x));   /* sqrt(1-x^2) */
    return x87_atan2l(x, s);
}

/* acos(x) = atan2(sqrt(1-x^2), x), giving the full [0, pi] range
 * directly (better than pi/2 - asin near x = -1). */
long double acosl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x) || x < -1.0L || x > 1.0L) {
        feraiseexcept(FE_INVALID); errno = EDOM; return NAN;
    }
    if (x == 1.0L) return 0.0L;
    if (x == -1.0L) return LDBL_PI;
    if (x == 0.0L) return LDBL_PI_2;
    long double s = x87_sqrtl((1.0L - x) * (1.0L + x));
    return x87_atan2l(s, x);
}

/* ====================================================================
 * sqrt / cbrt / hypot.
 * ==================================================================== */

long double cbrtl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return x;
    if (x == 0.0L) return x;

    int neg = (x < 0.0L);
    long double ax = neg ? -x : x;

    /* Decompose ax = m * 2^e via fxtract, m in [1, 2). */
    long double m;
    long double e_ld;
    __asm__ __volatile__("fldt %2; fxtract; fstpt %0; fstpt %1"
                         : "=m"(m), "=m"(e_ld) : "m"(ax));
    int e = (int)e_ld;

    /* e = 3q + r, r in {0,1,2}. */
    int q, r;
    if (e >= 0) { q = e / 3; r = e - 3 * q; }
    else { q = -((-e + 2) / 3); r = e - 3 * q; }
    static const long double pow2_r[3] = { 1.0L, 2.0L, 4.0L };
    long double mr = m * pow2_r[r];      /* mr in [1, 8) */

    /* Newton on the 80-bit value seeded by the double cube root. */
    long double g = (long double)cbrt((double)mr);
    for (int i = 0; i < 3; i++) {
        long double g2 = g * g;
        g = (2.0L * g + mr / g2) / 3.0L;
    }

    long double result = scalbnl(g, q);
    return neg ? -result : result;
}

/* hypot(x, y) without spurious overflow: factor out the larger
 * magnitude. */
long double hypotl(long double x, long double y)
{
    if (__isinfl(x) || __isinfl(y)) return INFINITY;
    if (__isnanl(x) || __isnanl(y)) return NAN;
    x = (x < 0) ? -x : x;
    y = (y < 0) ? -y : y;
    if (x < y) { long double t = x; x = y; y = t; }
    if (x == 0.0L) return 0.0L;
    long double r = y / x;
    return x * x87_sqrtl(1.0L + r * r);
}

long double rsqrtl(long double x)
{
    if (__isnanl(x)) return x;
    if (x < 0.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0L) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return INFINITY; }
    if (__isinfl(x)) return 0.0L;
    return 1.0L / x87_sqrtl(x);
}

/* ====================================================================
 * C23 power / root variants — x^y = 2^(y*log2(x)) at 80-bit width.
 * (powl itself lives in math_wrap.c.)
 * ==================================================================== */

long double powrl(long double x, long double y)
{
    if (__isnanl(x) || __isnanl(y)) return NAN;
    if (x < 0.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0L && y == 0.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (__isinfl(x) && y == 0.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (x == 1.0L && __isinfl(y)) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (y == 0.0L) return 1.0L;
    if (x == 0.0L) {
        if (y > 0.0L) return 0.0L;
        errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return INFINITY;
    }
    if (__isinfl(x)) return (y > 0) ? INFINITY : 0.0L;
    if (__isinfl(y)) {
        if (x == 1.0L) return 1.0L;
        if (x < 1.0L) return (y < 0) ? INFINITY : 0.0L;
        return (y < 0) ? 0.0L : INFINITY;
    }
    /* x^y = 2^(y*log2(x)); fyl2x forms y*log2(x) at full 80-bit width
     * in one instruction (more accurate than expl(y*logl(x))). */
    long double yl2x = x87_yl2x(y, x);
    if (yl2x >  16384.0L) { errno = ERANGE; return INFINITY; }
    if (yl2x < -16446.0L) { errno = ERANGE; return 0.0L; }
    return x87_exp2l(yl2x);
}

long double pownl(long double x, intmax_t n)
{
    if (n == 0) return 1.0L;
    if (__isnanl(x)) return x;
    if (x == 0.0L) {
        if (n < 0) {
            errno = ERANGE; feraiseexcept(FE_DIVBYZERO);
            if ((n & 1) && __signbitl(x)) return -INFINITY;
            return INFINITY;
        }
        if ((n & 1) && __signbitl(x)) return -0.0L;
        return 0.0L;
    }
    if (__isinfl(x)) {
        if (x > 0) return (n > 0) ? INFINITY : 0.0L;
        if (n > 0) return (n & 1) ? -INFINITY : INFINITY;
        return (n & 1) ? -0.0L : 0.0L;
    }

    int negn = (n < 0);
    uintmax_t un;
    if (negn) {
        if (n == INTMAX_MIN) un = (uintmax_t)INTMAX_MAX + 1u;
        else un = (uintmax_t)(-n);
    } else un = (uintmax_t)n;

    long double result = 1.0L;
    long double base = x;
    while (un > 0) {
        if (un & 1) result *= base;
        un >>= 1;
        if (un) base *= base;
    }
    if (negn) result = 1.0L / result;
    return result;
}

long double rootnl(long double x, int n)
{
    if (n == 0) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (__isnanl(x)) return x;
    if (n == 1) return x;
    if (n == 2) return sqrtl(x);
    if (n == 3) return cbrtl(x);
    if (n == -1) return 1.0L / x;

    if (x == 0.0L) {
        if (n > 0) return (n & 1) ? x : 0.0L;
        errno = ERANGE; feraiseexcept(FE_DIVBYZERO);
        return ((n & 1) && __signbitl(x)) ? -INFINITY : INFINITY;
    }
    if (__isinfl(x)) {
        if (x > 0) return (n > 0) ? INFINITY : 0.0L;
        if (!(n & 1)) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
        return (n > 0) ? -INFINITY : -0.0L;
    }
    if (n < 0) return 1.0L / rootnl(x, -n);
    if (x < 0.0L) {
        if (!(n & 1)) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
        return -powl(-x, 1.0L / (long double)n);
    }
    return powl(x, 1.0L / (long double)n);
}

long double compoundl(long double x, intmax_t n)
{
    if (__isnanl(x)) return x;
    if (x < -1.0L) { errno = EDOM; feraiseexcept(FE_INVALID); return NAN; }
    if (n == 0) return 1.0L;
    if (x == 0.0L) return 1.0L;
    if (x == -1.0L) {
        if (n > 0) return 0.0L;
        errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return INFINITY;
    }
    if (__isinfl(x)) {
        if (x > 0) return (n > 0) ? INFINITY : 0.0L;
        return NAN;
    }
    return expl((long double)n * log1pl(x));
}

/* ====================================================================
 * Hyperbolic — long-double formulas built on the 80-bit primitives.
 * ==================================================================== */

long double sinhl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return x;
    if (x == 0.0L) return x;
    long double ax = (x < 0) ? -x : x;
    if (ax < 1e-10L) return x;          /* sinh(x) ~ x */
    /* For large |x|, e^-x underflows; sinh ~ sign(x) * e^|x| / 2. */
    if (ax > 11356.0L) {
        long double h = expl(ax) * 0.5L;
        return (x < 0) ? -h : h;
    }
    long double ex = expl(x);
    return (ex - 1.0L / ex) * 0.5L;
}

long double coshl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return INFINITY;
    if (x == 0.0L) return 1.0L;
    long double ax = (x < 0) ? -x : x;
    if (ax > 11356.0L) return expl(ax) * 0.5L;
    long double ex = expl(ax);
    return (ex + 1.0L / ex) * 0.5L;
}

long double tanhl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return (x > 0) ? 1.0L : -1.0L;
    if (x == 0.0L) return x;
    long double ax = (x < 0) ? -x : x;
    if (ax > 22.0L) return (x > 0) ? 1.0L : -1.0L;
    long double e2x = expl(2.0L * x);
    return (e2x - 1.0L) / (e2x + 1.0L);
}

/* asinh(x) = log(x + sqrt(x^2 + 1)). */
long double asinhl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return x;
    if (x == 0.0L) return x;
    long double ax = (x < 0) ? -x : x;
    if (ax < 1e-10L) return x;
    long double r;
    if (ax > 1e10L) {
        /* avoid x^2 overflow: asinh(x) ~ sign(x)*(log(2|x|)). */
        r = logl(2.0L * ax);
    } else if (ax < 0.5L) {
        /* log1p form for small x to keep precision:
         * asinh(x) = log1p(x + x^2/(1+sqrt(1+x^2))). */
        long double s = x87_sqrtl(1.0L + ax * ax);
        r = log1pl(ax + ax * ax / (1.0L + s));
    } else {
        r = logl(ax + x87_sqrtl(ax * ax + 1.0L));
    }
    return (x < 0) ? -r : r;
}

/* acosh(x) = log(x + sqrt(x^2 - 1)), x >= 1. */
long double acoshl(long double x)
{
    if (__isnanl(x)) return x;
    if (x == 1.0L) return 0.0L;
    if (x < 1.0L) { feraiseexcept(FE_INVALID); errno = EDOM; return NAN; }
    if (__isinfl(x)) return x;
    if (x > 1e10L) return logl(2.0L * x);
    if (x < 1.5L) {
        long double t = x - 1.0L;
        return log1pl(t + x87_sqrtl(t * (x + 1.0L)));
    }
    return logl(x + x87_sqrtl((x - 1.0L) * (x + 1.0L)));
}

/* atanh(x) = 0.5 * log((1+x)/(1-x)), |x| < 1. */
long double atanhl(long double x)
{
    if (__isnanl(x)) return x;
    if (x == 0.0L) return x;
    if (x == 1.0L) { feraiseexcept(FE_DIVBYZERO); errno = ERANGE; return INFINITY; }
    if (x == -1.0L) { feraiseexcept(FE_DIVBYZERO); errno = ERANGE; return -INFINITY; }
    if (x < -1.0L || x > 1.0L || __isinfl(x)) {
        feraiseexcept(FE_INVALID); errno = EDOM; return NAN;
    }
    /* atanh(x) = 0.5 * log1p(2x/(1-x)) — stable for small x. */
    return 0.5L * log1pl(2.0L * x / (1.0L - x));
}

/* ====================================================================
 * Rounding — frndint with explicit rounding-mode control + truncation.
 * (floorl/ceill/truncl/roundl/roundevenl are in math_wrap.c; here are
 *  rintl/nearbyintl + the long integer variants.)
 * ==================================================================== */

/* rint: round to integer using the current mode; may raise FE_INEXACT. */
long double rintl(long double x)
{
    if (__isnanl(x) || __isinfl(x)) return x;
    return x87_rndintl(x);
}

/* nearbyint: like rint but never raises FE_INEXACT.  Save/restore the
 * status word's inexact flag around the frndint. */
long double nearbyintl(long double x)
{
    if (__isnanl(x) || __isinfl(x)) return x;
    fenv_t env;
    feholdexcept(&env);
    long double r = x87_rndintl(x);
    fesetenv(&env);
    return r;
}

long lrintl(long double x)
{
    long r;
    __asm__ __volatile__("fldt %1; fistpl %0" : "=m"(r) : "m"(x));
    return r;
}

long long llrintl(long double x)
{
    long long r;
    __asm__ __volatile__("fldt %1; fistpll %0" : "=m"(r) : "m"(x));
    return r;
}

/* round(): round half away from zero, regardless of current mode. */
static long double roundl_internal(long double x)
{
    long double t = x87_rndintl_trunc(x);          /* trunc toward 0 */
    long double frac = x - t;
    if (frac > 0.5L || frac == 0.5L) return t + 1.0L;
    if (frac < -0.5L || frac == -0.5L) return t - 1.0L;
    return t;
}

long lroundl(long double x)
{
    if (__isnanl(x) || __isinfl(x)) return (x < 0) ? LONG_MIN : LONG_MAX;
    return (long)roundl_internal(x);
}

long long llroundl(long double x)
{
    if (__isnanl(x) || __isinfl(x)) return (x < 0) ? LLONG_MIN : LLONG_MAX;
    return (long long)roundl_internal(x);
}

/* ====================================================================
 * Floating-point manipulation — fxtract / fscale / direct 80-bit byte
 * manipulation, never via double.
 * ==================================================================== */

/* frexp: x = m * 2^e, 0.5 <= |m| < 1. */
long double frexpl(long double x, int *exp)
{
    union ldshape u = { .value = x };
    int e = u.parts.sexp & LD_EXP_MASK;
    int sign = u.parts.sexp & LD_SIGN_MASK;

    if (x == 0.0L || __isnanl(x) || __isinfl(x)) { *exp = 0; return x; }

    if (e == 0) {
        /* subnormal/pseudo-denormal: normalize by scaling up. */
        x *= 18446744073709551616.0L;        /* 2^64 */
        u.value = x;
        e = u.parts.sexp & LD_EXP_MASK;
        sign = u.parts.sexp & LD_SIGN_MASK;
        *exp = e - LD_EXP_BIAS - 63;
    } else {
        *exp = e - LD_EXP_BIAS + 1;
    }
    /* Force the unbiased exponent to -1 so m is in [0.5, 1). */
    u.parts.sexp = (uint16_t)(sign | (LD_EXP_BIAS - 1));
    return u.value;
}

long double ldexpl(long double x, int exp)
{
    return scalbnl(x, exp);
}

/* scalbn: x * 2^n via fscale. */
long double scalbnl(long double x, int n)
{
    if (__isnanl(x) || __isinfl(x) || x == 0.0L) return x;
    /* fscale takes the integer part of st(1).  Build n as a long
     * double exponent and scale.  Clamp extreme n so the loadable
     * exponent doesn't overflow the conversion. */
    if (n > 40000) n = 40000;
    if (n < -40000) n = -40000;
    long double nl = (long double)n;
    long double res;
    __asm__ __volatile__(
        "fldt   %2\n\t"               /* st0 = n */
        "fldt   %1\n\t"               /* st0 = x ; st1 = n */
        "fscale\n\t"                  /* st0 = x * 2^n */
        "fstp   %%st(1)\n\t"
        "fstpt  %0"
        : "=m"(res) : "m"(x), "m"(nl));
    return res;
}

long double scalblnl(long double x, long n)
{
    /* On i386 `long` is 32-bit, so n already fits an int. */
    return scalbnl(x, (int)n);
}

long double modfl(long double x, long double *iptr)
{
    if (__isinfl(x)) { *iptr = x; return (x < 0) ? -0.0L : 0.0L; }
    if (__isnanl(x)) { *iptr = x; return x; }
    long double ip = x87_rndintl_trunc(x);
    *iptr = ip;
    long double frac = x - ip;
    /* preserve sign of zero fractional part */
    if (frac == 0.0L) return __signbitl(x) ? -0.0L : 0.0L;
    return frac;
}

/* ilogb / logb — extract the unbiased exponent. */
int ilogbl(long double x)
{
    if (__isnanl(x)) return FP_ILOGBNAN;
    if (__isinfl(x)) return INT_MAX;
    if (x == 0.0L) return FP_ILOGB0;
    union ldshape u = { .value = x };
    int e = u.parts.sexp & LD_EXP_MASK;
    if (e == 0) {
        /* subnormal: count the leading zeros of the mantissa. */
        uint64_t m = u.parts.mant;
        if (m == 0) return FP_ILOGB0;
        int sub = 0;
        while (!(m & 0x8000000000000000ULL)) { m <<= 1; sub++; }
        return -LD_EXP_BIAS - sub + 1;
    }
    return e - LD_EXP_BIAS;
}

long double logbl(long double x)
{
    if (__isnanl(x)) return x;
    if (__isinfl(x)) return INFINITY;
    if (x == 0.0L) { errno = ERANGE; feraiseexcept(FE_DIVBYZERO); return -INFINITY; }
    return (long double)ilogbl(x);
}

/* significandl lives in math_obsolete.c (built on scalbnl/ilogbl). */

/* ====================================================================
 * nextafter / nexttoward / copysign — direct 80-bit byte manipulation.
 * nexttowardl's whole point is the long-double step; never via double.
 * ==================================================================== */

long double copysignl(long double x, long double y)
{
    union ldshape ux = { .value = x };
    union ldshape uy = { .value = y };
    ux.parts.sexp = (uint16_t)((ux.parts.sexp & LD_EXP_MASK) |
                               (uy.parts.sexp & LD_SIGN_MASK));
    return ux.value;
}

/* Step x toward y by one representable 80-bit value. */
long double nextafterl(long double x, long double y)
{
    if (__isnanl(x) || __isnanl(y)) return x + y;     /* NaN propagation */
    if (x == y) return y;                             /* incl. ±0 -> sign of y */

    union ldshape ux = { .value = x };

    if (x == 0.0L) {
        /* smallest subnormal toward y's sign. */
        ux.parts.mant = 1;
        ux.parts.sexp = (uint16_t)(__signbitl(y) ? LD_SIGN_MASK : 0);
        return ux.value;
    }

    /* Determine direction: increase magnitude if moving away from 0
     * (|y| > |x| in the direction of y), else decrease. */
    int up;
    if (x > 0.0L) up = (y > x);
    else up = (y < x);          /* x < 0: "up" means more negative magnitude */

    if (up) {
        /* increase magnitude */
        if (ux.parts.mant == 0xffffffffffffffffULL) {
            ux.parts.mant = 0x8000000000000000ULL;
            ux.parts.sexp++;
        } else {
            ux.parts.mant++;
            /* normalized values keep the explicit integer bit set; if
             * it was cleared (we crossed from a power of two going up)
             * the increment already restored bit 63 for normals. */
            if (!(ux.parts.mant & 0x8000000000000000ULL))
                ux.parts.mant |= 0x8000000000000000ULL;
        }
    } else {
        /* decrease magnitude */
        if (ux.parts.mant == 0x8000000000000000ULL &&
            (ux.parts.sexp & LD_EXP_MASK) > 1) {
            ux.parts.sexp--;
            ux.parts.mant = 0xffffffffffffffffULL;
        } else {
            ux.parts.mant--;
        }
    }
    if (__isinfl(ux.value)) { errno = ERANGE; feraiseexcept(FE_OVERFLOW); }
    return ux.value;
}

long double nexttowardl(long double x, long double y)
{
    return nextafterl(x, y);     /* both operands are long double */
}

/* ====================================================================
 * NaN
 * ==================================================================== */

long double nanl(const char *tagp)
{
    (void)tagp;
    union ldshape u;
    u.parts.mant = 0xC000000000000000ULL;   /* quiet NaN, integer bit set */
    u.parts.sexp = LD_EXP_MASK;              /* max exponent */
    u.parts.pad = 0;
    return u.value;
}

/* ====================================================================
 * C23 fromfp family (operate at 80-bit width).
 * ==================================================================== */

int fromfpxl(long double *y, long double x, fenv_t *envp, int rounding_mode)
{
    if (envp) {
        fenv_t zero_env = { 0 };
        *envp = zero_env;
    }
    switch (rounding_mode) {
    case FE_TONEAREST:  *y = x87_rndint_mode(x, 0u); break;
    case FE_DOWNWARD:   *y = floorl(x); break;
    case FE_UPWARD:     *y = ceill(x); break;
    case FE_TOWARDZERO: *y = truncl(x); break;
    default: return -1;
    }
    return 0;
}

int fromfpl(long double *y, long double x, fenv_t *envp, int rounding_mode)
{
    return fromfpxl(y, x, envp, rounding_mode);
}

int ufromfpul(unsigned int *y, long double x, fenv_t *envp, int rounding_mode)
{
    long double tmp = 0;
    int rc = fromfpxl(&tmp, x, envp, rounding_mode);
    *y = (unsigned int)tmp;
    return rc;
}

int ufromfpxl(unsigned int *y, long double x, fenv_t *envp, int rounding_mode)
{
    return ufromfpul(y, x, envp, rounding_mode);
}

/* ====================================================================
 * DOUBLE-BASED FALLBACKS
 *
 * The following have no x87 primitive and a genuine 80-bit
 * implementation (Stirling / continued fractions / Chebyshev series)
 * would be too large for this pass; they remain double-precision:
 *   erfl, erfcl, tgammal, lgammal, lgammal_r,
 *   j0l, j1l, jnl, y0l, y1l, ynl.
 * These lose ~11 mantissa bits but keep correct values within the
 * double exponent range, which is adequate for the special functions.
 * ==================================================================== */

long double erfl(long double x)    { return (long double)erf((double)x); }
long double erfcl(long double x)   { return (long double)erfc((double)x); }
long double tgammal(long double x) { return (long double)tgamma((double)x); }
long double lgammal(long double x) { return (long double)lgamma((double)x); }

long double j0l(long double x) { return (long double)j0((double)x); }
long double j1l(long double x) { return (long double)j1((double)x); }
long double jnl(int n, long double x) { return (long double)jn(n, (double)x); }
long double y0l(long double x) { return (long double)y0((double)x); }
long double y1l(long double x) { return (long double)y1((double)x); }
long double ynl(int n, long double x) { return (long double)yn(n, (double)x); }

/* ====================================================================
 * BSD classification + scaling entry points (real symbols, referenced
 * by ksh93's libast float table).
 * ==================================================================== */

int isinfl(long double x)  { return __isinfl(x); }
int isnanl(long double x)  { return __isnanl(x); }
int finitel(long double x) { return !__isinfl(x) && !__isnanl(x); }
long double scalbl(long double x, long double n) { return scalbnl(x, (int)n); }
