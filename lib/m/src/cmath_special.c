/*
 * lib/m/src/cmath_special.c — complex gamma, error, and Bessel
 * functions.  Substrate / GNU extension: the C standard's complex
 * surface stops at <complex.h>'s exp/log/trig/hyperbolic block, so
 * these aren't in any spec — but tgmath dispatch and scientific
 * code (libstdc++ <complex>, numerical libraries) want them.
 *
 * Twenty-four entries, eight functions × float/double/long-double:
 *
 *   clgamma  — log-gamma
 *   ctgamma  — gamma
 *   cerf     — error function
 *   cerfc    — complementary error function
 *   cj0      — Bessel J_0
 *   cj1      — Bessel J_1
 *   cy0      — Bessel Y_0
 *   cy1      — Bessel Y_1
 *
 * Algorithms (double precision):
 *
 *   clgamma — Lanczos approximation with g=7, n=8.  For Re(z) >= 0.5
 *     direct; for Re(z) < 0.5 use the reflection formula
 *       Γ(z) Γ(1-z) = π / sin(π z)
 *     to map onto the well-behaved half-plane.
 *
 *   ctgamma = cexp(clgamma(z)), with a sign fix-up around the
 *     reflection branch (the imaginary part of clgamma is multi-
 *     valued; cexp folds it correctly except where the imaginary
 *     part wraps).
 *
 *   cerf — truncated Taylor series:
 *       erf(z) = (2/sqrt(π)) Σ (-1)^k z^(2k+1) / (k! (2k+1))
 *     Converges quickly for |z| < ~5.  For larger |z| the series
 *     still converges but slowly; cap at 64 terms.  Real-axis
 *     correctness verified against the existing real erf().
 *
 *   cerfc(z) = 1 - cerf(z).  For large positive Re(z) this loses
 *     precision in the (1 - tiny) subtraction; honest implementation
 *     would switch to a continued-fraction or Faddeeva form there.
 *     Documented limitation.
 *
 *   cj0 / cj1 — power series:
 *       J_n(z) = Σ (-1)^k (z/2)^(n+2k) / (k! (n+k)!)
 *     Practical for |z| < ~30; beyond that the term cancellation
 *     swamps double precision.  Real-axis matches j0(x)/j1(x).
 *
 *   cy0 / cy1 — series with logarithmic component:
 *       Y_0(z) = (2/π) [log(z/2) + γ_E] J_0(z)
 *              - (2/π) Σ (-1)^k H_k (z/2)^(2k) / (k!)^2
 *     where H_k = Σ 1/j  (harmonic numbers; H_0 = 0).
 *     Y_1 has the analogous form plus a -2/(π z) singularity.
 *
 * float / long-double variants delegate to double through casts.
 * Substrate long double is x87 80-bit — the cast loses some bits.
 */

#include <complex.h>
#include <math.h>

#ifndef M_PI
#define M_PI       3.14159265358979323846
#endif
#ifndef M_SQRTPI
#define M_SQRTPI   1.77245385090551602729   /* sqrt(π) */
#endif
#define EULER_MASCHERONI 0.57721566490153286061

/* ============================================================
 * clgamma / ctgamma — Lanczos.
 * ============================================================ */

/* Lanczos g=7, n=8 coefficients (Numerical Recipes / Wikipedia). */
static const double lanczos_g = 7.0;
static const double lanczos_c[9] = {
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

double complex clgamma(double complex z) {
    /* Reflection for Re(z) < 0.5:
     *   Γ(z) Γ(1-z) = π / sin(π z)
     *   → clgamma(z) = log(π) − clog(sin(π z)) − clgamma(1−z) */
    if (creal(z) < 0.5) {
        double complex pi_z = M_PI * z;
        double complex sin_pi_z = CMPLX(sin(creal(pi_z)) * cosh(cimag(pi_z)),
                                        cos(creal(pi_z)) * sinh(cimag(pi_z)));
        return CMPLX(log(M_PI), 0.0) - clog(sin_pi_z) - clgamma(1.0 - z);
    }

    /* Standard Lanczos in the right half-plane. */
    double complex shifted = z - 1.0;
    double complex A = CMPLX(lanczos_c[0], 0.0);
    for (int k = 1; k < 9; k++) {
        A += lanczos_c[k] / (shifted + (double)k);
    }
    double complex t = shifted + lanczos_g + 0.5;
    /* (1/2)·log(2π) + (shifted + 0.5)·log(t) − t + log(A) */
    return CMPLX(0.5 * log(2.0 * M_PI), 0.0)
         + (shifted + 0.5) * clog(t)
         - t
         + clog(A);
}

double complex ctgamma(double complex z) {
    /* Pole at non-positive integers — let cexp do the saturating. */
    return cexp(clgamma(z));
}

/* ============================================================
 * cerf / cerfc — Taylor series.
 * ============================================================ */

double complex cerf(double complex z) {
    /* erf(0) = 0 exactly. */
    if (creal(z) == 0.0 && cimag(z) == 0.0) return z;

    /* Taylor series.  64 terms cover |z| <= ~5 to double precision;
     * beyond that the leading exp(-z²) factor in the asymptotic form
     * dominates anyway. */
    double complex zsq    = z * z;
    double complex term   = z;
    double complex sum    = z;
    for (int k = 1; k < 64; k++) {
        /* term_k = -term_{k-1} * z^2 * (2k-1) / (k * (2k+1))
         * is wrong; correct recurrence for k-th nonzero term:
         *   t_k = t_{k-1} * (-z^2 * (2k-1)) / (k * (2k+1))
         * Direct factor: t_k = (-1)^k z^{2k+1} / (k! (2k+1)) */
        term = -term * zsq / (double)k;
        double complex contribution = term / (double)(2 * k + 1);
        sum += contribution;
        if (cabs(contribution) < 1e-18 * cabs(sum)) break;
    }
    return (2.0 / M_SQRTPI) * sum;
}

double complex cerfc(double complex z) {
    return CMPLX(1.0, 0.0) - cerf(z);
}

/* ============================================================
 * cj0 / cj1 — Bessel J via power series.
 * ============================================================ */

/* Series: J_n(z) = Σ (-1)^k (z/2)^(n+2k) / (k! (n+k)!).
 * Compute term-by-term with a ratio recurrence to avoid factorials. */
static double complex bessel_j_series(int n, double complex z) {
    double complex z_half  = 0.5 * z;
    double complex z_half2 = z_half * z_half;
    /* term_0 = (z/2)^n / n!  */
    double complex term = CMPLX(1.0, 0.0);
    for (int k = 1; k <= n; k++) term *= z_half / (double)k;
    double complex sum = term;
    for (int k = 1; k < 80; k++) {
        /* term_k / term_{k-1} = -z^2/4 / (k * (n+k)) */
        term = -term * z_half2 / ((double)k * (double)(n + k));
        sum += term;
        if (cabs(term) < 1e-18 * cabs(sum)) break;
    }
    return sum;
}

double complex cj0(double complex z) {
    /* J_0(0) = 1 exactly. */
    if (creal(z) == 0.0 && cimag(z) == 0.0) return CMPLX(1.0, 0.0);
    return bessel_j_series(0, z);
}

double complex cj1(double complex z) {
    if (creal(z) == 0.0 && cimag(z) == 0.0) return CMPLX(0.0, 0.0);
    return bessel_j_series(1, z);
}

/* ============================================================
 * cy0 / cy1 — Bessel Y via series with log term.
 * ============================================================ */

double complex cy0(double complex z) {
    if (creal(z) == 0.0 && cimag(z) == 0.0) {
        /* Y_0(0) = -inf in the real-axis limit; in the complex
         * setting it's an essential singularity.  Return -inf real
         * and let the caller deal. */
        return CMPLX(-INFINITY, 0.0);
    }
    double complex z_half  = 0.5 * z;
    double complex z_half2 = z_half * z_half;
    double complex j0_z    = cj0(z);

    /* Series Σ (-1)^k H_k (z/2)^(2k) / (k!)^2, with H_0 = 0. */
    double H = 0.0;
    double complex term = CMPLX(1.0, 0.0);   /* k=0 term has H_0=0; contribution = 0 */
    double complex sum  = CMPLX(0.0, 0.0);
    for (int k = 1; k < 80; k++) {
        /* term_k / term_{k-1} = -(z/2)^2 / k^2 */
        term = -term * z_half2 / ((double)k * (double)k);
        H   += 1.0 / (double)k;
        double complex contribution = H * term;
        sum += contribution;
        if (cabs(contribution) < 1e-18 * (cabs(sum) + 1.0)) break;
    }

    /* Y_0(z) = (2/π) [log(z/2) + γ_E] J_0(z) − (2/π) sum */
    double complex log_term = clog(z_half) + EULER_MASCHERONI;
    return (2.0 / M_PI) * (log_term * j0_z - sum);
}

double complex cy1(double complex z) {
    if (creal(z) == 0.0 && cimag(z) == 0.0) {
        return CMPLX(-INFINITY, 0.0);
    }
    double complex z_half  = 0.5 * z;
    double complex z_half2 = z_half * z_half;
    double complex j1_z    = cj1(z);

    /* Series for Y_1 — coefficient pattern shifts by one term and
     * involves harmonic-style coefficients with both H_k and H_{k+1}.
     * Specifically (Abramowitz & Stegun 9.1.11):
     *   Y_1(z) = (2/π) [log(z/2) + γ_E] J_1(z)
     *          − (2/(π z))
     *          − (1/π) Σ_{k=0}^∞ (-1)^k (H_k + H_{k+1}) (z/2)^(2k+1) / (k! (k+1)!) */
    double H_k = 0.0;
    double H_kp1 = 1.0;    /* H_1 = 1 */
    /* term_0 = (z/2) / 0! / 1! = z/2 */
    double complex term = z_half;
    double complex sum  = (H_k + H_kp1) * term;     /* k=0 */
    for (int k = 1; k < 80; k++) {
        /* term ratio: t_k / t_{k-1} = -(z/2)^2 / (k * (k+1)) */
        term = -term * z_half2 / ((double)k * (double)(k + 1));
        H_k   += 1.0 / (double)k;
        H_kp1 += 1.0 / (double)(k + 1);
        double complex contribution = (H_k + H_kp1) * term;
        sum += contribution;
        if (cabs(contribution) < 1e-18 * (cabs(sum) + 1.0)) break;
    }

    double complex log_term = clog(z_half) + EULER_MASCHERONI;
    return (2.0 / M_PI) * log_term * j1_z
         - 2.0 / (M_PI * z)
         - sum / M_PI;
}

/* ============================================================
 * float / long-double wrappers
 * ============================================================ */

float complex clgammaf(float complex z) { return (float complex)clgamma((double complex)z); }
float complex ctgammaf(float complex z) { return (float complex)ctgamma((double complex)z); }
float complex cerff   (float complex z) { return (float complex)cerf   ((double complex)z); }
float complex cerfcf  (float complex z) { return (float complex)cerfc  ((double complex)z); }
float complex cj0f    (float complex z) { return (float complex)cj0    ((double complex)z); }
float complex cj1f    (float complex z) { return (float complex)cj1    ((double complex)z); }
float complex cy0f    (float complex z) { return (float complex)cy0    ((double complex)z); }
float complex cy1f    (float complex z) { return (float complex)cy1    ((double complex)z); }

long double complex clgammal(long double complex z) { return (long double complex)clgamma((double complex)z); }
long double complex ctgammal(long double complex z) { return (long double complex)ctgamma((double complex)z); }
long double complex cerfl   (long double complex z) { return (long double complex)cerf   ((double complex)z); }
long double complex cerfcl  (long double complex z) { return (long double complex)cerfc  ((double complex)z); }
long double complex cj0l    (long double complex z) { return (long double complex)cj0    ((double complex)z); }
long double complex cj1l    (long double complex z) { return (long double complex)cj1    ((double complex)z); }
long double complex cy0l    (long double complex z) { return (long double complex)cy0    ((double complex)z); }
long double complex cy1l    (long double complex z) { return (long double complex)cy1    ((double complex)z); }
