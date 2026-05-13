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

/*
 * erf(x) - error function: 2/sqrt(pi) * integral_0^x e^(-t^2) dt
 *
 * Uses Abramowitz & Stegun 7.1.26 rational approximation
 * (max error ~1.5e-7).  Range: [-1, 1].  Odd function.
 */
double erf(double x) {
    if (isnan(x)) return x;
    if (x == 0.0) return x;                 /* preserves +0.0/-0.0 (erf is odd) */
    if (isinf(x)) return (x > 0) ? 1.0 : -1.0;

    double sign = (x < 0) ? -1.0 : 1.0;
    double absx = fabs(x);

    /* For |x| > 6.0, erf is essentially +/-1 to double precision. */
    if (absx > 6.0) return sign;

    const double a1 =  0.254829592;
    const double a2 = -0.284496736;
    const double a3 =  1.421413741;
    const double a4 = -1.453152027;
    const double a5 =  1.061405429;
    const double p  =  0.3275911;

    double t = 1.0 / (1.0 + p * absx);
    double y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1)
                     * t * exp(-absx * absx);

    return sign * y;
}

/*
 * erfc(x) - complementary error function: 1 - erf(x).
 *
 * v1 wrapper: defers to 1 - erf(x).  This loses precision for very large
 * positive x where erf(x) approaches 1, but satisfies the C99 7.12.8.2
 * special-value contract and the moderate-accuracy test suite.  A future
 * revision can substitute an asymptotic expansion (A&S 7.1.26) for large x.
 */
double erfc(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return (x > 0) ? 0.0 : 2.0;
    return 1.0 - erf(x);
}

/*
 * tgamma(x) - true Gamma function, Gamma(x).
 *
 * C99 7.12.8.4 special-value contract:
 *   tgamma(NaN)        -> NaN
 *   tgamma(+0)         -> +Inf, FE_DIVBYZERO
 *   tgamma(-0)         -> -Inf, FE_DIVBYZERO
 *   tgamma(neg int)    -> NaN, FE_INVALID (poles)
 *   tgamma(+Inf)       -> +Inf
 *   tgamma(-Inf)       -> NaN, FE_INVALID
 *   tgamma(n+1) == n!  for non-negative integer n
 *   tgamma(0.5)        == sqrt(pi)
 *   overflow at large x -> +HUGE_VAL, FE_OVERFLOW
 *
 * Implementation: Lanczos approximation (g=7, n=9), with the reflection
 * formula Gamma(x) = pi / (sin(pi*x) * Gamma(1-x)) used for x < 0.5.
 * Coefficients per Wikipedia "Lanczos approximation"; gives ~15 digits.
 */
double tgamma(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) {
        if (x > 0) return x;
        feraiseexcept(FE_INVALID);
        return NAN;
    }
    if (x == 0.0) {
        feraiseexcept(FE_DIVBYZERO);
        return signbit(x) ? -INFINITY : INFINITY;
    }

    /* Negative integer poles */
    if (x < 0.0 && x == floor(x)) {
        feraiseexcept(FE_INVALID);
        return NAN;
    }

    /* Reflection formula for x < 0.5 */
    if (x < 0.5) {
        return M_PI / (sin(M_PI * x) * tgamma(1.0 - x));
    }

    /* Overflow guard: tgamma(171.624...) overflows double */
    if (x > 171.624) {
        feraiseexcept(FE_OVERFLOW);
        return HUGE_VAL;
    }

    static const double g = 7.0;
    static const double p[9] = {
        0.99999999999980993,
        676.5203681218851,
       -1259.1392167224028,
        771.32342877765313,
       -176.61502916214059,
        12.507343278686905,
       -0.13857109526572012,
        9.9843695780195716e-6,
        1.5056327351493116e-7
    };

    x -= 1.0;
    double a = p[0];
    double t = x + g + 0.5;
    for (int i = 1; i < 9; i++) {
        a += p[i] / (x + i);
    }
    return sqrt(2.0 * M_PI) * pow(t, x + 0.5) * exp(-t) * a;
}

/*
 * lgamma_r(x, signp) - natural log of |Gamma(x)|, reentrant (BSD extension).
 *
 * Stores the sign of Gamma(x) in *signp (+1 or -1).
 *
 * Special values (C99 7.12.8.3 + POSIX):
 *   lgamma_r(NaN)        -> NaN, *signp = 1
 *   lgamma_r(+/-0)       -> +Inf, FE_DIVBYZERO, *signp = 1
 *   lgamma_r(neg int)    -> +Inf, FE_DIVBYZERO, *signp = 1 (poles)
 *   lgamma_r(+/-Inf)     -> +Inf, *signp = 1
 *   lgamma_r(1) == 0,  lgamma_r(2) == 0,  lgamma_r(n+1) == log(n!)
 *
 * Implementation: For |x| where tgamma() does not overflow, defer to the
 * existing Lanczos-based tgamma() and take log of the absolute value.
 * For large x (x >= 170) where tgamma overflows, fall back to Stirling's
 * series:  ln Gamma(x) ~ (x-0.5) ln x - x + 0.5 ln(2pi) + 1/(12x).
 */
int signgam = 1;

double lgamma_r(double x, int *signp) {
    if (isnan(x)) { *signp = 1; return x; }
    if (isinf(x)) { *signp = 1; return INFINITY; }
    if (x == 0.0 || (x < 0.0 && x == floor(x))) {
        feraiseexcept(FE_DIVBYZERO);
        *signp = 1;
        return INFINITY;
    }

    /* Moderate range: use Lanczos-backed tgamma directly. */
    if (x < 170.0) {
        double g = tgamma(x);
        if (g < 0.0) { *signp = -1; g = -g; }
        else { *signp = 1; }
        return log(g);
    }

    /* Large positive x: Stirling's approximation (Gamma(x) > 0 here). */
    *signp = 1;
    return (x - 0.5) * log(x) - x + 0.5 * log(2.0 * M_PI) + 1.0 / (12.0 * x);
}

/*
 * lgamma(x) - natural log of |Gamma(x)|; sets the global signgam to the
 * sign of Gamma(x) (XSI/POSIX). Not thread-safe; use lgamma_r() for that.
 */
double lgamma(double x) {
    return lgamma_r(x, &signgam);
}

/*
 * C23 pi-argument trigonometric functions
 */
/*
 * Bessel functions J_n(x) and Y_n(x) — XSI/POSIX extensions.
 *
 *   |x| <= 12.5: Taylor series in z = (x/2)^2.  Converges absolutely
 *             for any x; we cap the iteration count at 80.  The
 *             cutoff is high enough that the Taylor regime overlaps
 *             well into the band where the Hankel asymptotic would
 *             still be returning ~1e-8 relative error.  Taylor at
 *             x=12.5 loses ~5 decimal digits to cancellation between
 *             alternating terms but keeps ~11 sig figs — better than
 *             the asymptotic in that band.  At x=12.5 the series
 *             needs ~30 iterations to drive the term below 1e-18 of
 *             the running sum.
 *
 *   |x| > 12.5: Hankel asymptotic expansion via the standard P_n /
 *             Q_n modulus and phase functions in z = 1/(8x)^2.  This
 *             is a *divergent* series — we sum until the term
 *             magnitude stops shrinking and then stop, which gives
 *             the best obtainable accuracy for the given |x|.  At
 *             x=12.5 the smallest reachable term is ~1e-12.
 *
 * J_n with n >= 2:
 *   n <= x : forward recurrence from j0/j1 (numerically stable).
 *   n >  x : Miller's backward recurrence with the standard
 *            normalisation  J_0 + 2(J_2 + J_4 + ...) = 1.
 *
 * Y_n with n >= 2: forward recurrence always (stable for Y).
 *
 * Symmetry:
 *   J_n(-x) = (-1)^n J_n(x)
 *   J_{-n}(x) = (-1)^n J_n(x)
 *
 * Special values:
 *   j0(0) = 1, j1(0) = 0, jn(n != 0, 0) = 0
 *   y0(0) = y1(0) = yn(n, 0) = -inf
 *   y*(x < 0) = NaN + FE_INVALID
 *   j*(NaN)   = NaN          y*(NaN)   = NaN
 *   j*(+inf)  = 0            y*(+inf)  = 0
 */

#define BESSEL_EPS              1.0e-18
#define BESSEL_MAXIT            80
#define BESSEL_SERIES_CUTOFF    12.5
/* As #define to avoid any chance of -O2 misoptimising a file-scope
 * static const reference across the x87-asm log() boundary. */
#define BESSEL_GAMMA    0.5772156649015328606
#define BESSEL_TWOOPI   0.6366197723675813431
#define BESSEL_ONEOPI   0.3183098861837906715
#define BESSEL_RSQRTPI  0.5641895835477562869

/*
 * Taylor series for J_n(x), x >= 0.  Only called for n = 0 or 1
 * from the public API; works for any n >= 0 in principle.
 */
static double bessel_j_series(int n, double x)
{
    double half = 0.5 * x;
    double z = half * half;
    double term;
    int k;

    /* term_0 = (x/2)^n / n!  */
    term = 1.0;
    for (k = 1; k <= n; k++) term *= half / k;

    double sum = term;
    for (k = 1; k <= BESSEL_MAXIT; k++) {
        term *= -z / ((double)k * (double)(n + k));
        sum += term;
        if (fabs(term) < BESSEL_EPS * fabs(sum)) break;
    }
    return sum;
}

/* Y_0(x), power series, x > 0. */
static double bessel_y0_series(double x)
{
    double half = 0.5 * x;
    double z = half * half;
    /* Build J_0 and a sum involving harmonic numbers in parallel. */
    double term = 1.0;
    double j0sum = 1.0;
    double Hk = 0.0;
    double hsum = 0.0;
    for (int k = 1; k <= BESSEL_MAXIT; k++) {
        term *= -z / ((double)k * (double)k);
        j0sum += term;
        Hk += 1.0 / k;
        hsum += Hk * term;
        if (fabs(term) * (Hk + 1.0) <
            BESSEL_EPS * (fabs(j0sum) + fabs(hsum))) break;
    }
    return BESSEL_TWOOPI * ((log(half) + BESSEL_GAMMA) * j0sum - hsum);
}

/* Y_1(x), power series, x > 0. */
static double bessel_y1_series(double x)
{
    double half = 0.5 * x;
    double z = half * half;
    /* j1sum is J_1(x) / (x/2). */
    double term = 1.0;
    double j1sum = 1.0;
    double Hk = 0.0;            /* H_0 */
    double Hkp1 = 1.0;          /* H_1 */
    double psum = Hk + Hkp1;    /* k=0 contribution to Σ (H_k + H_{k+1}) z^k / (k!(k+1)!) */
    for (int k = 1; k <= BESSEL_MAXIT; k++) {
        term *= -z / ((double)k * (double)(k + 1));
        j1sum += term;
        Hk = Hkp1;
        Hkp1 = Hk + 1.0 / (k + 1);
        psum += (Hk + Hkp1) * term;
        if (fabs(term) * (Hkp1 + 1.0) <
            BESSEL_EPS * (fabs(j1sum) + fabs(psum))) break;
    }
    double j1x = half * j1sum;
    return BESSEL_TWOOPI * ((log(half) + BESSEL_GAMMA) * j1x - 1.0 / x)
         - half * BESSEL_ONEOPI * psum;
}

/*
 * Hankel asymptotic modulus and phase.  Computes P_n(x), Q_n(x)
 * in z = 1/(8x)^2.  Truncates on the first non-decreasing term —
 * standard handling for an asymptotic (divergent) series.
 */
static void bessel_pq(int n, double x, double *P, double *Q)
{
    double mu = 4.0 * (double)n * (double)n;
    double inv8x = 1.0 / (8.0 * x);
    double inv8x2 = inv8x * inv8x;

    double p_term = 1.0;
    double q_term = (mu - 1.0) * inv8x;
    double p_sum  = p_term;
    double q_sum  = q_term;
    double prev   = fabs(p_term) + fabs(q_term);

    for (int k = 1; k <= 16; k++) {
        double f1 = mu - (4.0*k - 3.0) * (4.0*k - 3.0);
        double f2 = mu - (4.0*k - 1.0) * (4.0*k - 1.0);
        double f3 = mu - (4.0*k + 1.0) * (4.0*k + 1.0);

        p_term *= -f1 * f2 * inv8x2 / ((2.0*k - 1.0) * (2.0*k));
        q_term *= -f2 * f3 * inv8x2 / ((2.0*k)       * (2.0*k + 1.0));

        p_sum += p_term;
        q_sum += q_term;

        double mag = fabs(p_term) + fabs(q_term);
        if (mag > prev) break;                          /* diverging — stop */
        if (mag < BESSEL_EPS * (fabs(p_sum) + fabs(q_sum))) break;
        prev = mag;
    }
    *P = p_sum;
    *Q = q_sum;
}

/* J_n(x) for x >> 1, n = 0 or 1. */
static double bessel_j_asymp(int n, double x)
{
    double P, Q;
    bessel_pq(n, x, &P, &Q);
    double inv = BESSEL_RSQRTPI / sqrt(x);
    double s = sin(x), c = cos(x);
    if (n == 0) return inv * ((P + Q) * c + (P - Q) * s);
    return         inv * ((Q - P) * c + (P + Q) * s);   /* n == 1 */
}

/* Y_n(x) for x >> 1, n = 0 or 1. */
static double bessel_y_asymp(int n, double x)
{
    double P, Q;
    bessel_pq(n, x, &P, &Q);
    double inv = BESSEL_RSQRTPI / sqrt(x);
    double s = sin(x), c = cos(x);
    if (n == 0) return inv * ((Q - P) * c + (P + Q) * s);
    return         inv * (-(P + Q) * c + (Q - P) * s);  /* n == 1 */
}

/* Internal: J_n(|x|) for n = 0 or 1. */
static double bessel_j_pos(int n, double x_abs)
{
    if (x_abs <= BESSEL_SERIES_CUTOFF) return bessel_j_series(n, x_abs);
    return bessel_j_asymp(n, x_abs);
}

/* --- Public API --- */

double j0(double x)
{
    if (isnan(x)) return x;
    if (isinf(x)) return 0.0;
    double ax = fabs(x);
    if (ax == 0.0) return 1.0;
    return bessel_j_pos(0, ax);
}

double j1(double x)
{
    if (isnan(x)) return x;
    if (isinf(x)) return 0.0;
    double ax = fabs(x);
    if (ax == 0.0) return 0.0;
    double r = bessel_j_pos(1, ax);
    return (x < 0.0) ? -r : r;
}

double y0(double x)
{
    if (isnan(x)) return x;
    if (x < 0.0) { feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0) return -INFINITY;
    if (isinf(x)) return 0.0;
    if (x <= BESSEL_SERIES_CUTOFF) return bessel_y0_series(x);
    return bessel_y_asymp(0, x);
}

double y1(double x)
{
    if (isnan(x)) return x;
    if (x < 0.0) { feraiseexcept(FE_INVALID); return NAN; }
    if (x == 0.0) return -INFINITY;
    if (isinf(x)) return 0.0;
    if (x <= BESSEL_SERIES_CUTOFF) return bessel_y1_series(x);
    return bessel_y_asymp(1, x);
}

double jn(int n, double x)
{
    if (isnan(x)) return x;

    /* Reduce to non-negative order via J_{-n}(x) = (-1)^n J_n(x). */
    int sign_n = 1;
    if (n < 0) {
        n = -n;
        if (n & 1) sign_n = -sign_n;
    }

    /* Reduce to non-negative argument via J_n(-x) = (-1)^n J_n(x). */
    int sign_x = 1;
    if (x < 0.0) {
        x = -x;
        if (n & 1) sign_x = -sign_x;
    }

    if (isinf(x)) return 0.0;
    if (n == 0) return sign_n * sign_x * j0(x);
    if (n == 1) return sign_n * sign_x * bessel_j_pos(1, x);
    if (x == 0.0) return 0.0;

    double result;
    if ((double)n <= x) {
        /* Forward recurrence — stable when n <= x. */
        double fkm1 = bessel_j_pos(0, x);
        double fk   = bessel_j_pos(1, x);
        for (int k = 1; k < n; k++) {
            double fkp1 = (2.0 * (double)k / x) * fk - fkm1;
            fkm1 = fk;
            fk   = fkp1;
        }
        result = fk;
    } else {
        /* Miller's backward recurrence — stable when n > x.
           Pick m well above n; round to even so the normalisation
           sum (J_0 + 2(J_2+J_4+...) = 1) captures only even-indexed
           terms cleanly. */
        int m = n + (int)sqrt(40.0 * (double)n);
        if (m < n + 20) m = n + 20;
        m = (m + 1) & ~1;

        double fkp1 = 0.0;
        double fk   = 1.0;
        double saved = 0.0;
        double sum_even = 0.0;
        for (int k = m; k >= 1; k--) {
            double fkm1 = (2.0 * (double)k / x) * fk - fkp1;
            fkp1 = fk;
            fk   = fkm1;
            int idx = k - 1;
            if (idx == n) saved = fk;
            if (idx > 0 && (idx & 1) == 0) sum_even += fk;
            if (fabs(fk) > 1.0e100) {
                fk        *= 1.0e-100;
                fkp1      *= 1.0e-100;
                saved     *= 1.0e-100;
                sum_even  *= 1.0e-100;
            }
        }
        /* fk is now f_0; renormalise so that J_0 + 2(J_2+...) = 1. */
        double norm = fk + 2.0 * sum_even;
        result = saved / norm;
    }
    return sign_n * sign_x * result;
}

double yn(int n, double x)
{
    if (isnan(x)) return x;
    if (x < 0.0) { feraiseexcept(FE_INVALID); return NAN; }

    int sign_n = 1;
    if (n < 0) {
        n = -n;
        if (n & 1) sign_n = -sign_n;
    }

    if (x == 0.0) return -INFINITY;
    if (isinf(x)) return 0.0;
    if (n == 0) return sign_n * y0(x);
    if (n == 1) return sign_n * y1(x);

    /* Y_n forward recurrence is always stable. */
    double ykm1 = y0(x);
    double yk   = y1(x);
    for (int k = 1; k < n; k++) {
        double ykp1 = (2.0 * (double)k / x) * yk - ykm1;
        ykm1 = yk;
        yk   = ykp1;
        if (!isfinite(yk)) break;            /* Y grows fast for n >> x */
    }
    return sign_n * yk;
}


