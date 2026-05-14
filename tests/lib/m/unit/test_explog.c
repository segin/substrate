/*
 * test_explog.c — unit tests for exponential, logarithmic, and power
 * families.  Covers REQ-06-0607..0619 of docs/tasks/06-6-c-library.md.
 */

#include <stdio.h>
#include <math.h>
#include <fenv.h>
#include <errno.h>
#include <float.h>
#include <stdint.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", (msg), __LINE__); \
        g_failures++; \
    } \
} while (0)

/* ULP-based tolerance for ≈ comparisons.  16 ULPs is generous; with
 * x87 long-double internal precision substrate libm typically lands
 * within 1-2 ULPs but a few specific reductions (compound, expm1
 * cross-over) climb closer to 4-8. */
static int approx_eq(double a, double b, int ulps) {
    if (a == b) return 1;
    if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
    if (isinf(a) || isinf(b)) return 0;
    double diff = fabs(a - b);
    double tol = ulps * (fabs(a) > fabs(b) ? fabs(a) : fabs(b)) * DBL_EPSILON;
    return diff <= tol;
}
#define APPROX(a, b, msg) CHECK(approx_eq((a), (b), 16), msg)

#ifndef M_E
#define M_E 2.71828182845904523536
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif

/* REQ-06-0607 */
static void test_exp(void) {
    CHECK(exp(0.0) == 1.0,                 "exp(0) == 1");
    APPROX(exp(1.0), M_E,                  "exp(1) ≈ e");
    CHECK(exp(-INFINITY) == 0.0,           "exp(-inf) == +0");
    CHECK(exp(INFINITY) == INFINITY,       "exp(+inf) == +inf");
    CHECK(isnan(exp(NAN)),                 "exp(NaN) == NaN");
    /* overflow → +inf with errno = ERANGE */
    errno = 0;
    CHECK(exp(1000.0) == INFINITY,         "exp(1000) overflows to +inf");
    CHECK(errno == ERANGE,                 "exp overflow sets errno=ERANGE");
    /* underflow → 0 */
    errno = 0;
    CHECK(exp(-1000.0) == 0.0,             "exp(-1000) underflows to 0");
    CHECK(errno == ERANGE,                 "exp underflow sets errno=ERANGE");
}

/* REQ-06-0608 */
static void test_exp2(void) {
    CHECK(exp2(0.0) == 1.0,                "exp2(0) == 1");
    CHECK(exp2(10.0) == 1024.0,            "exp2(10) == 1024");
    CHECK(exp2(-1.0) == 0.5,               "exp2(-1) == 0.5");
    CHECK(exp2(INFINITY) == INFINITY,      "exp2(+inf) == +inf");
    CHECK(exp2(-INFINITY) == 0.0,          "exp2(-inf) == +0");
    CHECK(isnan(exp2(NAN)),                "exp2(NaN) == NaN");
    APPROX(exp2(0.5), M_SQRT2,             "exp2(0.5) ≈ sqrt(2)");
}

/* REQ-06-0609 */
static void test_expm1(void) {
    CHECK(expm1(0.0) == 0.0,               "expm1(0) == 0");
    CHECK(signbit(expm1(-0.0)),            "expm1(-0) preserves sign");
    APPROX(expm1(1.0), M_E - 1.0,          "expm1(1) ≈ e-1");
    /* Accuracy check at tiny x: expm1(tiny) ≈ tiny + tiny^2/2.
     * For x = 1e-10, true value differs from x by ~5e-21, smaller
     * than DBL_EPSILON*x, so result should round-trip to x exactly. */
    double tiny = 1e-10;
    CHECK(fabs(expm1(tiny) - tiny) < 1e-19,
                                           "expm1(tiny) ≈ tiny");
    /* Compared to naive exp(x)-1, expm1 should be MORE accurate near
     * 0; here we just sanity-check we don't catastrophically lose. */
    CHECK(expm1(INFINITY) == INFINITY,     "expm1(+inf) == +inf");
    CHECK(expm1(-INFINITY) == -1.0,        "expm1(-inf) == -1");
    CHECK(isnan(expm1(NAN)),               "expm1(NaN) == NaN");
}

/* REQ-06-0610 */
static void test_log(void) {
    CHECK(log(1.0) == 0.0,                 "log(1) == 0");
    APPROX(log(M_E), 1.0,                  "log(e) ≈ 1");
    /* log(0) → -inf, errno = ERANGE */
    errno = 0;
    feclearexcept(FE_ALL_EXCEPT);
    CHECK(log(0.0) == -INFINITY,           "log(0) == -inf");
    CHECK(errno == ERANGE,                 "log(0) sets errno=ERANGE");
    /* log(-1) → NaN with FE_INVALID */
    errno = 0;
    feclearexcept(FE_ALL_EXCEPT);
    CHECK(isnan(log(-1.0)),                "log(-1) == NaN");
    CHECK(fetestexcept(FE_INVALID),        "log(-1) raises FE_INVALID");
    CHECK(errno == EDOM,                   "log(-1) sets errno=EDOM");
    CHECK(log(INFINITY) == INFINITY,       "log(+inf) == +inf");
    CHECK(isnan(log(NAN)),                 "log(NaN) == NaN");
    feclearexcept(FE_ALL_EXCEPT);
}

/* REQ-06-0611 */
static void test_log2(void) {
    CHECK(log2(1.0) == 0.0,                "log2(1) == 0");
    APPROX(log2(1024.0), 10.0,             "log2(1024) ≈ 10");
    APPROX(log2(2.0), 1.0,                 "log2(2) ≈ 1");
    CHECK(log2(INFINITY) == INFINITY,      "log2(+inf) == +inf");
    feclearexcept(FE_ALL_EXCEPT);
}

/* REQ-06-0612 */
static void test_log10(void) {
    CHECK(log10(1.0) == 0.0,               "log10(1) == 0");
    APPROX(log10(1000.0), 3.0,             "log10(1000) ≈ 3");
    APPROX(log10(10.0), 1.0,               "log10(10) ≈ 1");
    APPROX(log10(0.01), -2.0,              "log10(0.01) ≈ -2");
    feclearexcept(FE_ALL_EXCEPT);
}

/* REQ-06-0613 */
static void test_log1p(void) {
    CHECK(log1p(0.0) == 0.0,               "log1p(0) == 0");
    CHECK(signbit(log1p(-0.0)),            "log1p(-0) preserves sign");
    APPROX(log1p(M_E - 1.0), 1.0,          "log1p(e-1) ≈ 1");
    /* tiny x: log1p(x) ≈ x to within ulp */
    double tiny = 1e-12;
    CHECK(fabs(log1p(tiny) - tiny) < 1e-23,
                                           "log1p(tiny) ≈ tiny");
    errno = 0;
    CHECK(log1p(-1.0) == -INFINITY,        "log1p(-1) == -inf");
    CHECK(errno == ERANGE,                 "log1p(-1) sets errno=ERANGE");
    feclearexcept(FE_ALL_EXCEPT);
}

/* REQ-06-0614, REQ-06-0615 */
static void test_pow(void) {
    CHECK(pow(2.0, 10.0) == 1024.0,        "pow(2,10) == 1024");
    CHECK(pow(-1.0, 2.0) == 1.0,           "pow(-1,2) == 1");
    CHECK(pow(-1.0, 3.0) == -1.0,          "pow(-1,3) == -1");
    /* C99 F.9.4.4: pow(0, 0) = 1 */
    CHECK(pow(0.0, 0.0) == 1.0,            "pow(0,0) == 1 (C99)");
    /* pow(x, 0) == 1 for ANY x — including NaN and ±inf */
    CHECK(pow(NAN, 0.0) == 1.0,            "pow(NaN, 0) == 1");
    CHECK(pow(INFINITY, 0.0) == 1.0,       "pow(+inf, 0) == 1");
    CHECK(pow(-INFINITY, 0.0) == 1.0,      "pow(-inf, 0) == 1");
    CHECK(pow(0.0, 0.0) == 1.0,            "pow(0, 0) == 1");
    /* pow(1, y) == 1 for ANY y — including NaN */
    CHECK(pow(1.0, NAN) == 1.0,            "pow(1, NaN) == 1");
    CHECK(pow(1.0, INFINITY) == 1.0,       "pow(1, +inf) == 1");
    /* pow(-1, ±inf) == 1 (C99 F.9.4.4) */
    CHECK(pow(-1.0, INFINITY) == 1.0,      "pow(-1, +inf) == 1");
    CHECK(pow(-1.0, -INFINITY) == 1.0,     "pow(-1, -inf) == 1");
    /* negative base, non-integer exponent → NaN + EDOM */
    errno = 0;
    feclearexcept(FE_ALL_EXCEPT);
    CHECK(isnan(pow(-2.0, 0.5)),           "pow(-2, 0.5) == NaN");
    CHECK(errno == EDOM,                   "pow(neg,frac) sets errno=EDOM");
    /* pow(x>1, +inf) = +inf, pow(x<1, +inf) = 0 */
    CHECK(pow(2.0, INFINITY) == INFINITY,  "pow(2, +inf) == +inf");
    CHECK(pow(0.5, INFINITY) == 0.0,       "pow(0.5, +inf) == 0");
    CHECK(pow(2.0, -INFINITY) == 0.0,      "pow(2, -inf) == 0");
    feclearexcept(FE_ALL_EXCEPT);
}

/* REQ-06-0616 */
static void test_sqrt(void) {
    CHECK(sqrt(4.0) == 2.0,                "sqrt(4) == 2");
    CHECK(sqrt(0.0) == 0.0,                "sqrt(0) == 0");
    CHECK(signbit(sqrt(-0.0)),             "sqrt(-0) preserves sign");
    errno = 0;
    feclearexcept(FE_ALL_EXCEPT);
    CHECK(isnan(sqrt(-1.0)),               "sqrt(-1) == NaN");
    CHECK(fetestexcept(FE_INVALID),        "sqrt(-1) raises FE_INVALID");
    CHECK(errno == EDOM,                   "sqrt(-1) sets errno=EDOM");
    CHECK(sqrt(INFINITY) == INFINITY,      "sqrt(+inf) == +inf");
    CHECK(isnan(sqrt(NAN)),                "sqrt(NaN) == NaN");
    APPROX(sqrt(2.0), M_SQRT2,             "sqrt(2) ≈ sqrt(2)");
    feclearexcept(FE_ALL_EXCEPT);
}

/* REQ-06-0617 */
static void test_cbrt(void) {
    CHECK(cbrt(-8.0) == -2.0,              "cbrt(-8) == -2");
    CHECK(cbrt(8.0) == 2.0,                "cbrt(8) == 2");
    CHECK(cbrt(0.0) == 0.0,                "cbrt(0) == 0");
    CHECK(signbit(cbrt(-0.0)),             "cbrt(-0) preserves sign");
    CHECK(cbrt(27.0) == 3.0,               "cbrt(27) == 3");
    APPROX(cbrt(1e9), 1e3,                 "cbrt(1e9) ≈ 1000");
    APPROX(cbrt(1e-9), 1e-3,               "cbrt(1e-9) ≈ 1e-3");
    CHECK(cbrt(INFINITY) == INFINITY,      "cbrt(+inf) == +inf");
    CHECK(cbrt(-INFINITY) == -INFINITY,    "cbrt(-inf) == -inf");
    CHECK(isnan(cbrt(NAN)),                "cbrt(NaN) == NaN");
}

/* REQ-06-0618 */
static void test_hypot(void) {
    CHECK(hypot(3.0, 4.0) == 5.0,          "hypot(3,4) == 5");
    CHECK(hypot(5.0, 0.0) == 5.0,          "hypot(x,0) == fabs(x)");
    CHECK(hypot(-5.0, 0.0) == 5.0,         "hypot(-x,0) == fabs(x)");
    CHECK(hypot(0.0, 0.0) == 0.0,          "hypot(0,0) == 0");
    CHECK(hypot(INFINITY, NAN) == INFINITY,
                                           "hypot(+inf, NaN) == +inf");
    CHECK(hypot(NAN, INFINITY) == INFINITY,
                                           "hypot(NaN, +inf) == +inf");
    CHECK(isnan(hypot(NAN, 1.0)),          "hypot(NaN, x) == NaN");
    /* No spurious overflow */
    APPROX(hypot(3e300, 4e300), 5e300,     "hypot survives near DBL_MAX");
}

/* REQ-06-0619 */
static void test_rsqrt(void) {
    CHECK(rsqrt(4.0) == 0.5,               "rsqrt(4) == 0.5");
    CHECK(rsqrt(1.0) == 1.0,               "rsqrt(1) == 1");
    errno = 0;
    CHECK(rsqrt(0.0) == INFINITY,          "rsqrt(0) == +inf");
    CHECK(errno == ERANGE,                 "rsqrt(0) sets errno=ERANGE");
    errno = 0;
    feclearexcept(FE_ALL_EXCEPT);
    CHECK(isnan(rsqrt(-1.0)),              "rsqrt(-1) == NaN");
    CHECK(fetestexcept(FE_INVALID),        "rsqrt(-1) raises FE_INVALID");
    CHECK(rsqrt(INFINITY) == 0.0,          "rsqrt(+inf) == 0");
    feclearexcept(FE_ALL_EXCEPT);
}

/* Coverage for C23 extensions: exp10/exp2m1/exp10m1, log2p1/log10p1/logp1,
 * pown/powr/rootn/compound. */
static void test_c23_extensions(void) {
    APPROX(exp10(0.0), 1.0,                "exp10(0) == 1");
    APPROX(exp10(3.0), 1000.0,             "exp10(3) ≈ 1000");
    APPROX(exp10(-2.0), 0.01,              "exp10(-2) ≈ 0.01");

    APPROX(exp2m1(0.0), 0.0,               "exp2m1(0) == 0");
    APPROX(exp2m1(1.0), 1.0,               "exp2m1(1) == 1");
    APPROX(exp2m1(10.0), 1023.0,           "exp2m1(10) == 1023");

    APPROX(exp10m1(0.0), 0.0,              "exp10m1(0) == 0");
    APPROX(exp10m1(2.0), 99.0,             "exp10m1(2) == 99");

    APPROX(logp1(0.0), 0.0,                "logp1 == log1p alias");
    APPROX(log2p1(0.0), 0.0,               "log2p1(0) == 0");
    APPROX(log2p1(1.0), 1.0,               "log2p1(1) == 1");
    APPROX(log10p1(9.0), 1.0,              "log10p1(9) == 1");

    CHECK(pown(2.0, 10) == 1024.0,         "pown(2,10) == 1024");
    CHECK(pown(2.0, -1) == 0.5,            "pown(2,-1) == 0.5");
    CHECK(pown(0.0, 0) == 1.0,             "pown(0,0) == 1");
    CHECK(pown(-2.0, 3) == -8.0,           "pown(-2,3) == -8");
    CHECK(pown(-2.0, 4) == 16.0,           "pown(-2,4) == 16");

    APPROX(powr(2.0, 10.0), 1024.0,        "powr(2,10) == 1024");
    feclearexcept(FE_ALL_EXCEPT);
    errno = 0;
    CHECK(isnan(powr(-1.0, 2.0)),          "powr(-1, y) == NaN");
    CHECK(errno == EDOM,                   "powr(neg) sets errno=EDOM");

    CHECK(rootn(8.0, 3) == 2.0,            "rootn(8,3) == 2");
    CHECK(rootn(-8.0, 3) == -2.0,          "rootn(-8,3) == -2");
    APPROX(rootn(16.0, 4), 2.0,            "rootn(16,4) == 2");

    CHECK(compound(0.0, 5) == 1.0,         "compound(0,n) == 1");
    CHECK(compound(1.0, 0) == 1.0,         "compound(x,0) == 1");
    APPROX(compound(1.0, 10), 1024.0,      "compound(1,10) == 2^10");
    feclearexcept(FE_ALL_EXCEPT);
}

int main(void) {
    printf("test_explog: starting\n");
    test_exp();
    test_exp2();
    test_expm1();
    test_log();
    test_log2();
    test_log10();
    test_log1p();
    test_pow();
    test_sqrt();
    test_cbrt();
    test_hypot();
    test_rsqrt();
    test_c23_extensions();
    if (g_failures == 0) {
        printf("test_explog: PASS\n");
        return 0;
    }
    printf("test_explog: FAIL (%d failures)\n", g_failures);
    return 1;
}
