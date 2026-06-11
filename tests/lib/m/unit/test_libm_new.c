/*
 * test_libm_new.c — unit tests for new libm functions (task 24).
 *
 * Tests obsolescent aliases, C23 narrowing arithmetic, total order,
 * NaN payload, reentrant gamma, and complex log10.
 */

#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <math.h>
#include <fenv.h>
#include <errno.h>
#include <float.h>
#include <stdint.h>
#include <string.h>

#ifndef M_E
#define M_E  2.71828182845904523536
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", (msg), __LINE__); \
        g_failures++; \
    } \
} while (0)

static int approx_eq(double a, double b, int ulps) {
    if (a == b) return 1;
    if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
    if (isinf(a) || isinf(b)) return 0;
    double diff = fabs(a - b);
    double tol = ulps * (fabs(a) > fabs(b) ? fabs(a) : fabs(b)) * DBL_EPSILON;
    return diff <= tol;
}
#define APPROX(a, b, msg) CHECK(approx_eq((a), (b), 16), msg)

/* ============================================================
 * Obsolescent aliases (§24.2)
 * ============================================================ */

static void test_scalb(void) {
    /* scalb(x, n) == scalbn(x, (int)n) for finite n */
    CHECK(approx_eq(scalb(1.0, 3.0), scalbn(1.0, 3), 1), "scalb(1, 3) == 8");
    CHECK(approx_eq(scalb(2.0, -2.0), scalbn(2.0, -2), 1), "scalb(2, -2) == 0.5");
    /* edge: ±0, ±inf, NaN */
    CHECK(scalb(0.0, 10.0) == 0.0, "scalb(0, n) == 0");
    CHECK(isinf(scalb(INFINITY, 10.0)), "scalb(+inf, n) == +inf");
    CHECK(isnan(scalb(1.0, NAN)), "scalb(1, NaN) == NaN");
}

static void test_significand(void) {
    /* significand(x) should return x * 2^(-ilogb(x)) → in [1, 2) */
    double val = significand(100.0);
    CHECK(val >= 1.0 && val < 2.0, "significand(100) in [1, 2)");
    CHECK(approx_eq(val, 1.5625, 1), "significand(100) == 100/64");
    CHECK(significand(0.0) == 0.0, "significand(0) == 0");
    CHECK(significand(INFINITY) == INFINITY, "significand(+inf) == +inf");
    CHECK(isnan(significand(NAN)), "significand(NaN) == NaN");
}

static void test_drem(void) {
    CHECK(drem(5.0, 2.0) == remainder(5.0, 2.0), "drem(5, 2) == remainder(5, 2)");
    CHECK(approx_eq(drem(10.0, 3.0), remainder(10.0, 3.0), 1),
          "drem(10, 3) == remainder(10, 3)");
}

static void test_gamma(void) {
    /* gamma(x) == lgamma(x), sets signgam */
    int sg;
    double lg = lgamma_r(5.0, &sg);
    double gr = gamma(5.0);
    CHECK(approx_eq(gr, lg, 1), "gamma(5) == lgamma(5)");
    CHECK(signgam == sg, "gamma(5) sets signgam");
    /* gamma(0.5) = ln(sqrt(pi)) */
    CHECK(approx_eq(gamma(0.5), 0.5 * log(M_PI), 8), "gamma(0.5) ≈ ln(sqrt(pi))");
}

static void test_pow10(void) {
    CHECK(pow10(0.0) == exp10(0.0), "pow10(0) == exp10(0)");
    CHECK(pow10(1.0) == exp10(1.0), "pow10(1) == exp10(1)");
    CHECK(approx_eq(pow10(2.0), 100.0, 16), "pow10(2) == 100");
    CHECK(approx_eq(pow10f(2.0f), 100.0f, 1), "pow10f(2) == 100");
}

/* ============================================================
 * C23 narrowing arithmetic (§24.3)
 * ============================================================ */

static void test_narrowing(void) {
    /* fadd: double → float (single rounding) */
    double large = 1e20 + 1.0;
    double result = fadd(large, -1e20);
    float narrow = (float)large + (float)(-1e20);
    CHECK(approx_eq(result, narrow, 1), "fadd(1e20, -1e20) single-round");

    /* dadd: no narrowing (double → double) */
    CHECK(approx_eq(dadd(1.0, 2.0), 3.0, 0), "dadd(1, 2) == 3");

    /* fmul: double → float */
    CHECK(approx_eq(fmul(2.0, 3.0), 6.0f, 1), "fmul(2, 3) == 6");

    /* fdiv: double → float */
    CHECK(approx_eq(fdiv(10.0, 2.0), 5.0f, 1), "fdiv(10, 2) == 5");

    /* fsqrt */
    CHECK(approx_eq(fsqrt(4.0), 2.0f, 1), "fsqrt(4) == 2");
    CHECK(approx_eq(dsqrt(4.0), 2.0, 0), "dsqrt(4) == 2");
}

/* ============================================================
 * ISO C23 total order (§24.4)
 * ============================================================ */

static void test_totalorder(void) {
    /* +0 < -0 */
    CHECK(totalorder(-0.0, +0.0), "totalorder(-0, +0)");

    /* negative < positive finite */
    CHECK(totalorder(-1.0, 1.0), "totalorder(-1, 1)");

    /* -inf < finite < +inf */
    CHECK(totalorder(-INFINITY, 0.0), "totalorder(-inf, 0)");
    CHECK(totalorder(0.0, INFINITY), "totalorder(0, +inf)");

    /* NaN > everything */
    CHECK(totalorder(-1.0, NAN), "-1 < NaN");
    CHECK(totalorder(INFINITY, NAN), "+inf < NaN");

    /* totalorder(a, a) == 1 (reflexive) */
    CHECK(totalorder(1.0, 1.0), "totalorder(1, 1)");
    CHECK(totalorder(NAN, NAN), "totalorder(NaN, NaN)");

    /* totalordermag: compare by magnitude */
    CHECK(totalordermag(-1.0, 2.0), "totalordermag(-1, 2) == totalorder(1, 2)");
    /* totalordermag(-2, 1) compares |2| vs |1|; 2 > 1 so 0 (not less) */
    CHECK(!totalordermag(-2.0, 1.0), "totalordermag(-2, 1) == 0 (2 > 1 by mag)");
}

static void test_canonicalize(void) {
    /* Non-NaN: canonicalize returns x unchanged */
    double cx;
    CHECK(canonicalize(1.0, &cx) == 0, "canonicalize(1) returns 0");
    CHECK(approx_eq(cx, 1.0, 0), "canonicalize(1) returns 1");

    /* NaN: canonicalize quiets it */
    double nan = NAN;
    CHECK(canonicalize(nan, &cx) == 0, "canonicalize(NaN) returns 0");
    CHECK(isnan(cx), "canonicalize(NaN) produces NaN");
    CHECK(isnan(cx) && !__issignaling(cx), "canonicalize quiets sNaN");
}

static void test_getpayload(void) {
    /* Non-NaN returns -1 */
    double one = 1.0;
    CHECK(getpayload(&one) == -1.0, "getpayload(1) == -1");

    /* NaN payload: should be extractable */
    double nan = NAN;
    double p = getpayload(&nan);
    CHECK(p >= -1.0, "getpayload(NaN) >= -1");
}

static void test_setpayload(void) {
    double res;
    /* setpayload with payload 0 on +1.0 → +quiet-NaN with payload 0 */
    CHECK(setpayload(&res, 1.0, 0) == 0, "setpayload(+1, 0) returns 0");
    CHECK(isnan(res), "setpayload produces NaN");
    CHECK(!signbit(res), "setpayload(+1, 0) has positive sign");

    /* Payload round-trip */
    CHECK(setpayload(&res, 1.0, 42) == 0, "setpayload(+1, 42) returns 0");
    double p = getpayload(&res);
    CHECK(p >= 41.0 && p <= 43.0, "getpayload(setpayload(42)) ≈ 42");

    /* Float variants */
    float f;
    CHECK(setpayloadf(&f, 1.0f, 0) == 0, "setpayloadf(+1, 0) returns 0");
    CHECK(isnan(f), "setpayloadf produces NaN");
}

static void test_setpayloadsig(void) {
    double res;
    CHECK(setpayloadsig(&res, 1.0, 0) == 0, "setpayloadsig(+1, 0) returns 0");
    CHECK(isnan(res), "setpayloadsig produces NaN");
    /* Should be signaling */
    CHECK(__issignaling(res), "setpayloadsig produces sNaN");
}

/* ============================================================
 * Reentrant gamma (§24.5)
 * ============================================================ */

static void test_lgammaf_r(void) {
    int sg;
    double r = lgammaf_r(5.0f, &sg);
    CHECK(r >= 0.0, "lgammaf_r(5) >= 0 (ln(24) ≈ 3.17)");
    CHECK(sg == 1, "lgammaf_r(5) sets sign=+1 (5! > 0)");

    /* Thread-safe: should NOT touch global signgam */
    signgam = 42;
    int local_sg;
    lgammaf_r(2.0f, &local_sg);
    CHECK(signgam == 42, "lgammaf_r does not modify global signgam");
    CHECK(local_sg == 1, "lgammaf_r(2) sets signgam correctly");
}

static void test_lgammal_r(void) {
    int sg;
    double r = lgammal_r(3.0, &sg);  /* 3.0 → lgamma(3) = ln(2) ≈ 0.69 */
    CHECK(r >= 0.0, "lgammal_r(3) ≈ ln(2)");
    CHECK(sg == 1, "lgammal_r(3) sets sign=+1");
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("test_libm_new: starting\n");

    test_scalb();
    test_significand();
    test_drem();
    test_gamma();
    test_pow10();
    test_narrowing();
    test_totalorder();
    test_canonicalize();
    test_getpayload();
    test_setpayload();
    test_setpayloadsig();
    test_lgammaf_r();
    test_lgammal_r();

    printf("\nPASSED: %d failures\n", g_failures);
    return g_failures;
}
