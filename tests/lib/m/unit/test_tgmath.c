/*
 * test_tgmath.c - Unit tests for <tgmath.h> type-generic math dispatch
 *
 * Covers REQ-06-0449..0451.
 */

#include <stdio.h>
#include <tgmath.h>   /* must come before math.h macros to test dispatch */
#include <math.h>
#include <assert.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", (msg), __LINE__); \
        g_failures++; \
    } \
} while (0)

/* REQ-06-0449: type dispatch — sin dispatches based on argument type */
static void test_dispatch_sin(void) {
    volatile float  xf = 0.5f;
    volatile double xd = 0.5;
    volatile long double xl = 0.5L;

    /* float argument → sinf */
    float rf = sin(xf);
    CHECK(rf == sinf(xf), "sin(float) dispatches to sinf");

    /* double argument → sin (the real function) */
    double rd = sin(xd);
    CHECK(rd == sin(xd), "sin(double) dispatches to sin");

    /* long double argument → sinl */
    long double rl = sin(xl);
    CHECK(rl == sinl(xl), "sin(long double) dispatches to sinl");
}

static void test_dispatch_cos(void) {
    volatile float  xf = 0.3f;
    volatile double xd = 0.3;
    volatile long double xl = 0.3L;
    CHECK(cos(xf) == cosf(xf), "cos(float) → cosf");
    CHECK(cos(xl) == cosl(xl), "cos(long double) → cosl");
    (void)cos(xd);
}

static void test_dispatch_sqrt(void) {
    volatile float  xf = 4.0f;
    volatile double xd = 4.0;
    volatile long double xl = 4.0L;
    CHECK(sqrt(xf) == sqrtf(xf), "sqrt(float) → sqrtf");
    CHECK(sqrt(xl) == sqrtl(xl), "sqrt(long double) → sqrtl");
    (void)sqrt(xd);
}

static void test_dispatch_fabs(void) {
    volatile float  xf = -1.0f;
    volatile double xd = -1.0;
    volatile long double xl = -1.0L;
    CHECK(fabs(xf) == fabsf(xf), "fabs(float) → fabsf");
    CHECK(fabs(xl) == fabsl(xl), "fabs(long double) → fabsl");
    (void)fabs(xd);
}

static void test_dispatch_exp(void) {
    volatile float  xf = 1.0f;
    volatile long double xl = 1.0L;
    CHECK(exp(xf) == expf(xf), "exp(float) → expf");
    CHECK(exp(xl) == expl(xl), "exp(long double) → expl");
}

static void test_dispatch_log(void) {
    volatile float  xf = 2.0f;
    volatile long double xl = 2.0L;
    CHECK(log(xf) == logf(xf), "log(float) → logf");
    CHECK(log(xl) == logl(xl), "log(long double) → logl");
}

static void test_dispatch_pow(void) {
    volatile float  xf = 2.0f, yf = 3.0f;
    volatile long double xl = 2.0L, yl = 3.0L;
    CHECK(pow(xf, yf) == powf(xf, yf), "pow(float,float) → powf");
    CHECK(pow(xl, yl) == powl(xl, yl), "pow(long double, long double) → powl");
}

static void test_dispatch_ceil_floor(void) {
    volatile float  xf = 1.7f;
    volatile long double xl = 1.7L;
    CHECK(ceil(xf)  == ceilf(xf),   "ceil(float) → ceilf");
    CHECK(floor(xf) == floorf(xf),  "floor(float) → floorf");
    CHECK(ceil(xl)  == ceill(xl),   "ceil(long double) → ceill");
    CHECK(floor(xl) == floorl(xl),  "floor(long double) → floorl");
}

static void test_dispatch_round_trunc(void) {
    volatile float  xf = 1.6f;
    volatile long double xl = 1.6L;
    CHECK(round(xf)  == roundf(xf),  "round(float) → roundf");
    CHECK(trunc(xf)  == truncf(xf),  "trunc(float) → truncf");
    CHECK(round(xl)  == roundl(xl),  "round(long double) → roundl");
    CHECK(trunc(xl)  == truncl(xl),  "trunc(long double) → truncl");
}

static void test_dispatch_atan2(void) {
    volatile float  yf = 1.0f, xf = 1.0f;
    volatile long double yl = 1.0L, xl = 1.0L;
    CHECK(atan2(yf, xf) == atan2f(yf, xf), "atan2(float,float) → atan2f");
    CHECK(atan2(yl, xl) == atan2l(yl, xl), "atan2(long double, long double) → atan2l");
}

static void test_dispatch_hypot(void) {
    volatile float  xf = 3.0f, yf = 4.0f;
    volatile long double xl = 3.0L, yl = 4.0L;
    CHECK(hypot(xf, yf) == hypotf(xf, yf), "hypot(float,float) → hypotf");
    CHECK(hypot(xl, yl) == hypotl(xl, yl), "hypot(long double,long double) → hypotl");
}

static void test_dispatch_fmin_fmax(void) {
    volatile float  xf = 1.0f, yf = 2.0f;
    volatile long double xl = 1.0L, yl = 2.0L;
    CHECK(fmin(xf, yf) == fminf(xf, yf), "fmin(float,float) → fminf");
    CHECK(fmax(xf, yf) == fmaxf(xf, yf), "fmax(float,float) → fmaxf");
    CHECK(fmin(xl, yl) == fminl(xl, yl), "fmin(long double) → fminl");
    CHECK(fmax(xl, yl) == fmaxl(xl, yl), "fmax(long double) → fmaxl");
}

static void test_dispatch_copysign(void) {
    volatile float  xf = 1.0f, yf = -1.0f;
    volatile long double xl = 1.0L, yl = -1.0L;
    CHECK(copysign(xf, yf) == copysignf(xf, yf), "copysign(float) → copysignf");
    CHECK(copysign(xl, yl) == copysignl(xl, yl), "copysign(long double) → copysignl");
}

/* REQ-06-0451: no double-evaluation */
static void test_no_double_eval(void) {
    int cnt = 0;
    volatile float val = 1.0f;
    /* The argument expression (cnt++, val) should be evaluated exactly once */
    float r = sin((cnt++, val));
    (void)r;
    CHECK(cnt == 1, "sin() evaluates argument exactly once");

    cnt = 0;
    float r2 = sqrt((cnt++, val));
    (void)r2;
    CHECK(cnt == 1, "sqrt() evaluates argument exactly once");

    cnt = 0;
    volatile float v2 = 2.0f;
    float r3 = pow((cnt++, val), (cnt++, v2));
    (void)r3;
    CHECK(cnt == 2, "pow() evaluates both arguments exactly once");
}

/* REQ-06-0450: test all major function families dispatch */
static void test_dispatch_families(void) {
    volatile float xf = 0.5f;
    volatile long double xl = 0.5L;

    /* Trig */
    CHECK(tan(xf)  == tanf(xf),   "tan(float) → tanf");
    CHECK(asin(xf) == asinf(xf),  "asin(float) → asinf");
    CHECK(acos(xf) == acosf(xf),  "acos(float) → acosf");
    CHECK(atan(xf) == atanf(xf),  "atan(float) → atanf");

    /* Hyperbolic */
    CHECK(sinh(xf)  == sinhf(xf),  "sinh(float) → sinhf");
    CHECK(cosh(xf)  == coshf(xf),  "cosh(float) → coshf");
    CHECK(tanh(xf)  == tanhf(xf),  "tanh(float) → tanhf");
    CHECK(asinh(xf) == asinhf(xf), "asinh(float) → asinhf");
    CHECK(acosh((float)1.5f) == acoshf(1.5f), "acosh(float) → acoshf");
    CHECK(atanh(xf) == atanhf(xf), "atanh(float) → atanhf");

    /* Exp/log */
    CHECK(exp2(xf)  == exp2f(xf),  "exp2(float) → exp2f");
    CHECK(expm1(xf) == expm1f(xf), "expm1(float) → expm1f");
    CHECK(log2(xf)  == log2f(xf),  "log2(float) → log2f");
    CHECK(log10(xf) == log10f(xf), "log10(float) → log10f");
    CHECK(log1p(xf) == log1pf(xf), "log1p(float) → log1pf");

    /* Roots */
    CHECK(cbrt(xf) == cbrtf(xf),  "cbrt(float) → cbrtf");

    /* Rounding */
    CHECK(rint(xf)      == rintf(xf),      "rint(float) → rintf");
    CHECK(nearbyint(xf) == nearbyintf(xf), "nearbyint(float) → nearbyintf");

    /* Remainder */
    volatile float v2 = 0.3f;
    CHECK(fmod(xf, v2)      == fmodf(xf, v2),      "fmod(float) → fmodf");
    CHECK(remainder(xf, v2) == remainderf(xf, v2), "remainder(float) → remainderf");

    /* Error/gamma */
    CHECK(erf(xf)    == erff(xf),    "erf(float) → erff");
    CHECK(erfc(xf)   == erfcf(xf),   "erfc(float) → erfcf");
    CHECK(tgamma(xf) == tgammaf(xf), "tgamma(float) → tgammaf");
}

int main(void) {
    printf("=== tgmath.h type-generic dispatch tests ===\n\n");

    test_dispatch_sin();
    printf("sin() type dispatch\n");

    test_dispatch_cos();
    printf("cos() type dispatch\n");

    test_dispatch_sqrt();
    printf("sqrt() type dispatch\n");

    test_dispatch_fabs();
    printf("fabs() type dispatch\n");

    test_dispatch_exp();
    printf("exp() type dispatch\n");

    test_dispatch_log();
    printf("log() type dispatch\n");

    test_dispatch_pow();
    printf("pow() type dispatch\n");

    test_dispatch_ceil_floor();
    printf("ceil()/floor() type dispatch\n");

    test_dispatch_round_trunc();
    printf("round()/trunc() type dispatch\n");

    test_dispatch_atan2();
    printf("atan2() type dispatch\n");

    test_dispatch_hypot();
    printf("hypot() type dispatch\n");

    test_dispatch_fmin_fmax();
    printf("fmin()/fmax() type dispatch\n");

    test_dispatch_copysign();
    printf("copysign() type dispatch\n");

    test_dispatch_families();
    printf("all function families dispatch\n");

    test_no_double_eval();
    printf("no double-evaluation of arguments\n");

    printf("\n");
    if (g_failures == 0) {
        printf("=== ALL TGMATH TESTS PASSED ===\n");
        return 0;
    } else {
        printf("=== %d TEST(S) FAILED ===\n", g_failures);
        return 1;
    }
}
