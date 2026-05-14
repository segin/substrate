/*
 * test_x87_opt.c — verify that substrate libm's x87-optimised paths
 * behave as advertised.
 *
 * For each x87-backed function we either compare against a known
 * exact result, against a software re-derivation, or assert a
 * spec-firm property of the underlying instruction.
 *
 * REQ-06-0833..0838.
 */

#include <stdio.h>
#include <math.h>
#include <fenv.h>
#include <float.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI   3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif
#ifndef M_PI_4
#define M_PI_4 0.78539816339744830962
#endif
#ifndef M_LN2
#define M_LN2  0.69314718055994530942
#endif

static int g_failures = 0;
#define FAIL(m) do { printf("  FAIL: %s (line %d)\n", (m), __LINE__); g_failures++; } while (0)

static int approx_eq(double a, double b, int ulps) {
    if (a == b) return 1;
    if (isnan(a) && isnan(b)) return 1;
    if (isnan(a) || isnan(b)) return 0;
    if (isinf(a) || isinf(b)) return 0;
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return fabs(a - b) <= (double)ulps * scale * DBL_EPSILON;
}

/* REQ-06-0833: x87-optimised functions produce results that match
 * a generic / portable derivation to within a few ULPs. */
static void test_x87_matches_generic(void) {
    /* sin(x) from x87 must match the Taylor-series result for
     * arguments in the f-prime convergent range. */
    static const double angles[] = { 0.1, 0.5, 1.0, 1.5, 2.0, M_PI_4, M_PI_2 - 0.1 };
    for (size_t i = 0; i < sizeof(angles)/sizeof(angles[0]); i++) {
        double x = angles[i];
        /* Software Taylor reference (16 terms, more than enough for
         * |x| <= pi/2). */
        double t = x, sum = x, x2 = x * x;
        for (int n = 1; n < 16; n++) {
            t *= -x2 / (double)((2*n)*(2*n+1));
            sum += t;
        }
        if (!approx_eq(sin(x), sum, 32))
            FAIL("sin(x) x87 matches Taylor");
    }

    /* log2(x) from fyl2x must match log(x) / ln(2). */
    static const double pos[] = { 0.25, 0.5, 1.0, 2.0, 3.0, 10.0, 1e6 };
    for (size_t i = 0; i < sizeof(pos)/sizeof(pos[0]); i++) {
        double x = pos[i];
        double l = log(x) / M_LN2;
        if (!approx_eq(log2(x), l, 32))
            FAIL("log2(x) matches log(x)/ln(2)");
    }

    /* exp2(int) is exactly an integer power of 2. */
    for (int i = -10; i <= 10; i++) {
        double r = exp2((double)i);
        double ref = ldexp(1.0, i);
        if (r != ref) FAIL("exp2(int) exact");
    }

    /* sqrt(perfect square) is exact. */
    for (double x = 1.0; x <= 1024.0; x *= 2.0) {
        double r = sqrt(x * x);
        if (r != x) FAIL("sqrt(perfect square) exact");
    }
}

/* REQ-06-0834: sin(x) for x > 2^63 still gives a value in [-1, 1].
 * Substrate's fsin includes an fprem1 fallback for arguments out of
 * the native fsin domain (|x| < 2^63 on i486).  We verify:
 *   1. The result is in [-1, 1].
 *   2. It's not the bogus arg-unchanged value fsin returns when its
 *      C2 status bit stays set after a single attempt. */
static void test_range_reduction(void) {
    double huge_vals[] = {
        1e18, -1e18,                  /* well past 2^63 = 9.2e18 */
        1e20, -1e20,
        1.0e+30,
        ldexp(1.0, 64),               /* 2^64 */
        ldexp(1.0, 80),               /* 2^80 */
    };
    for (size_t i = 0; i < sizeof(huge_vals)/sizeof(huge_vals[0]); i++) {
        double x = huge_vals[i];
        double s = sin(x);
        double c = cos(x);
        if (isnan(s) || s < -1.0 || s > 1.0) {
            printf("  sin(%.3e) = %.17g (out of [-1, 1])\n", x, s);
            FAIL("sin(huge) range");
        }
        if (isnan(c) || c < -1.0 || c > 1.0) {
            printf("  cos(%.3e) = %.17g (out of [-1, 1])\n", x, c);
            FAIL("cos(huge) range");
        }
        /* Pythagorean identity must hold to within a few ULPs even
         * after range reduction; if reduction loses too many bits
         * the identity collapses. */
        double pyth = s * s + c * c;
        if (fabs(pyth - 1.0) > 1e-8) {
            printf("  sin(x)^2 + cos(x)^2 at x=%.3e: %.17g (drift %.3e)\n",
                   x, pyth, pyth - 1.0);
            FAIL("Pythagorean after reduction");
        }
    }
}

/* REQ-06-0835: the fprem loop must terminate.  fmod() / remainder()
 * loop until C2 is clear; without that loop, fprem on a too-wide
 * argument leaves C2=1 and returns a partial result.
 *
 * We can't directly observe C2, but if the loop didn't terminate
 * properly the result of fmod(huge_dividend, small_divisor) would
 * be in the wrong range. */
static void test_fprem_termination(void) {
    double dividend = 1e20;
    double divisor = M_PI;
    double r = fmod(dividend, divisor);
    if (fabs(r) > divisor) FAIL("fmod loop didn't fully reduce");
    if (r < -divisor)      FAIL("fmod result range");

    /* IEEE remainder result is in [-divisor/2, +divisor/2]. */
    r = remainder(dividend, divisor);
    if (fabs(r) > divisor / 2.0 + 1e-12)
        FAIL("remainder didn't fully reduce");
}

/* REQ-06-0836: x87 paths are faster than a software-only
 * implementation.  We don't have an in-tree generic path to compare
 * against, so this is a smoke benchmark: a fixed N-iteration loop
 * of sin/cos/sqrt should complete in well under a second.  Anything
 * pathological (e.g. an FPU stack misuse that traps to a slow
 * emulation path) blows the budget by orders of magnitude. */
#include <time.h>
static void bench_smoke(void) {
    const int N = 200000;
    volatile double acc = 0.0;
    clock_t t0 = clock();
    for (int i = 0; i < N; i++) {
        double x = (double)i * 0.0001;
        acc += sin(x) + cos(x) + sqrt(x + 1.0) + log(x + 1.0);
    }
    clock_t t1 = clock();
    double ms = (double)(t1 - t0) * 1000.0 / (double)CLOCKS_PER_SEC;
    if (ms > 5000.0) {
        printf("  bench: %d iters took %.1f ms (>5s — slow path?)\n", N, ms);
        FAIL("bench smoke");
    }
    (void)acc;
}

/* REQ-06-0837: nearbyint() must NOT raise FE_INEXACT even on inputs
 * where rint() would.  This is the only behavioural difference
 * between them on a correctly-implemented x87 path. */
static void test_nearbyint_no_inexact(void) {
    feclearexcept(FE_ALL_EXCEPT);
    double r = nearbyint(1.5);   /* nonintegral input — rint would set INEXACT */
    if (r != 2.0)                          FAIL("nearbyint(1.5) result");
    if (fetestexcept(FE_INEXACT))          FAIL("nearbyint must not raise FE_INEXACT");

    feclearexcept(FE_ALL_EXCEPT);
    r = rint(1.5);
    if (r != 2.0)                          FAIL("rint(1.5) result");
    if (!fetestexcept(FE_INEXACT))         FAIL("rint must raise FE_INEXACT");
    feclearexcept(FE_ALL_EXCEPT);
}

/* REQ-06-0838: lrint / llrint must honour the current rounding mode
 * (fistp does this, since it uses the CW rounding bits directly).
 *
 * We loop through the four rounding modes, ask lrint to convert a
 * value that rounds differently in each, and check the result. */
static void test_lrint_modes(void) {
    int saved = fegetround();

    fesetround(FE_TONEAREST);
    if (lrint(0.5)  != 0)  FAIL("lrint(0.5) banker's");
    if (lrint(1.5)  != 2)  FAIL("lrint(1.5) banker's");
    if (lrint(2.5)  != 2)  FAIL("lrint(2.5) banker's");
    if (lrint(-1.5) != -2) FAIL("lrint(-1.5) banker's");

    fesetround(FE_DOWNWARD);
    if (lrint(0.5)  != 0)  FAIL("lrint(0.5) down");
    if (lrint(-0.5) != -1) FAIL("lrint(-0.5) down");

    fesetround(FE_UPWARD);
    if (lrint(0.5)  != 1)  FAIL("lrint(0.5) up");
    if (lrint(-0.5) != 0)  FAIL("lrint(-0.5) up");

    fesetround(FE_TOWARDZERO);
    if (lrint(0.9)  != 0)  FAIL("lrint(0.9) toward zero");
    if (lrint(-0.9) != 0)  FAIL("lrint(-0.9) toward zero");

    fesetround(saved);
}

int main(void) {
    printf("test_x87_opt: starting\n");
    test_x87_matches_generic();
    test_range_reduction();
    test_fprem_termination();
    bench_smoke();
    test_nearbyint_no_inexact();
    test_lrint_modes();
    if (g_failures == 0) {
        printf("test_x87_opt: PASS\n");
        return 0;
    }
    printf("test_x87_opt: FAIL (%d failures)\n", g_failures);
    return 1;
}
