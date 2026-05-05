/*
 * test_classify.c — Unit tests for IEEE 754 classification & comparison.
 *
 * Covers REQ-06-0470..0481:
 *   fpclassify, isfinite, isinf, isnan, isnormal, signbit
 *   isgreater/isgreaterequal/isless/islessequal/islessgreater/isunordered
 *   iseqsig (must raise FE_INVALID on NaN)
 *   issignaling (sNaN bit-pattern detection)
 * Float, double, and long double variants exercised throughout.
 */

#include <stdio.h>
#include <math.h>
#include <fenv.h>
#include <float.h>
#include <stdint.h>
#include <string.h>

static int failures;

#define FAIL(msg) do { printf("FAIL: %s\n", msg); failures++; } while (0)
#define EXPECT(cond, msg) do { if (!(cond)) FAIL(msg); } while (0)

/*
 * Construct a signaling NaN by setting the exponent to all-ones and a
 * non-MSB mantissa bit.  Must be done bit-wise — passing a normal
 * literal sNaN through a wider-than-source FP register tends to quiet
 * it, since x87/SSE quiet sNaNs on load.
 */
static float make_snan_f(void) {
    union { uint32_t u; float f; } u;
    u.u = 0x7FA00000u;  /* exp=0xFF, mant top bit clear, lower bits set */
    return u.f;
}

static double make_snan_d(void) {
    union { uint64_t u; double d; } u;
    u.u = 0x7FF4000000000000ULL;
    return u.d;
}

/* ---- REQ-06-0471: fpclassify returns correct category ---- */
static void test_fpclassify_categories(void) {
    EXPECT(fpclassify(+0.0)         == FP_ZERO,      "fpclassify(+0.0)");
    EXPECT(fpclassify(-0.0)         == FP_ZERO,      "fpclassify(-0.0)");
    EXPECT(fpclassify(1.0)          == FP_NORMAL,    "fpclassify(1.0)");
    EXPECT(fpclassify(-1.0)         == FP_NORMAL,    "fpclassify(-1.0)");
    EXPECT(fpclassify(INFINITY)     == FP_INFINITE,  "fpclassify(INFINITY)");
    EXPECT(fpclassify(-INFINITY)    == FP_INFINITE,  "fpclassify(-INFINITY)");
    EXPECT(fpclassify((double)NAN)  == FP_NAN,       "fpclassify(NAN)");
    EXPECT(fpclassify(DBL_MIN / 2)  == FP_SUBNORMAL, "fpclassify(DBL_MIN/2)");
    EXPECT(fpclassify(DBL_MAX)      == FP_NORMAL,    "fpclassify(DBL_MAX)");

    /* float variant */
    EXPECT(fpclassify(0.0f)         == FP_ZERO,      "fpclassify(0.0f)");
    EXPECT(fpclassify(FLT_MIN / 2)  == FP_SUBNORMAL, "fpclassify(FLT_MIN/2)");
    EXPECT(fpclassify(FLT_MAX)      == FP_NORMAL,    "fpclassify(FLT_MAX)");

    /* long double variant */
    EXPECT(fpclassify((long double)0.0L)        == FP_ZERO,   "fpclassify(0.0L)");
    EXPECT(fpclassify((long double)1.0L)        == FP_NORMAL, "fpclassify(1.0L)");
}

/* ---- REQ-06-0472: isfinite ---- */
static void test_isfinite(void) {
    EXPECT(isfinite(0.0),            "isfinite(0.0)");
    EXPECT(isfinite(1.0),            "isfinite(1.0)");
    EXPECT(isfinite(-1.0),           "isfinite(-1.0)");
    EXPECT(isfinite(DBL_MAX),        "isfinite(DBL_MAX)");
    EXPECT(isfinite(DBL_MIN / 2),    "isfinite(denorm)");
    EXPECT(!isfinite(INFINITY),      "isfinite(+INF) must be false");
    EXPECT(!isfinite(-INFINITY),     "isfinite(-INF) must be false");
    EXPECT(!isfinite((double)NAN),   "isfinite(NAN) must be false");
}

/* ---- REQ-06-0473: isinf only true for ±INFINITY ---- */
static void test_isinf(void) {
    EXPECT(isinf(INFINITY),          "isinf(+INF)");
    EXPECT(isinf(-INFINITY),         "isinf(-INF)");
    EXPECT(!isinf(0.0),              "isinf(0.0)");
    EXPECT(!isinf(DBL_MAX),          "isinf(DBL_MAX)");
    EXPECT(!isinf((double)NAN),      "isinf(NaN) must be false");
}

/* ---- REQ-06-0474: isnan covers qNaN, sNaN, NAN ---- */
static void test_isnan(void) {
    EXPECT(isnan((double)NAN),       "isnan(NAN)");
    EXPECT(isnan(make_snan_d()),     "isnan(sNaN)");
    EXPECT(isnan(make_snan_f()),     "isnan(sNaN float)");
    EXPECT(!isnan(0.0),              "isnan(0.0)");
    EXPECT(!isnan(INFINITY),         "isnan(INFINITY)");
    EXPECT(!isnan(1.0),              "isnan(1.0)");
}

/* ---- REQ-06-0475: isnormal ---- */
static void test_isnormal(void) {
    EXPECT(!isnormal(0.0),           "isnormal(0)");
    EXPECT(!isnormal(-0.0),          "isnormal(-0)");
    EXPECT(!isnormal(DBL_MIN / 2),   "isnormal(denorm)");
    EXPECT(!isnormal(INFINITY),      "isnormal(INF)");
    EXPECT(!isnormal((double)NAN),   "isnormal(NaN)");
    EXPECT(isnormal(1.0),            "isnormal(1.0)");
    EXPECT(isnormal(DBL_MIN),        "isnormal(DBL_MIN)");
    EXPECT(isnormal(DBL_MAX),        "isnormal(DBL_MAX)");
}

/* ---- REQ-06-0476: signbit ---- */
static void test_signbit(void) {
    EXPECT(!signbit(+0.0),           "signbit(+0)");
    EXPECT( signbit(-0.0),           "signbit(-0)");
    EXPECT(!signbit(+1.0),           "signbit(+1)");
    EXPECT( signbit(-1.0),           "signbit(-1)");
    EXPECT(!signbit(+INFINITY),      "signbit(+INF)");
    EXPECT( signbit(-INFINITY),      "signbit(-INF)");
    /* signbit on NaN is well-defined but implementation-dependent for
     * the *bit* — what we require is consistency between (NaN) and -(NaN). */
    double n = (double)NAN;
    EXPECT(signbit(n) != signbit(-n), "signbit(NaN) != signbit(-NaN)");
}

/* ---- REQ-06-0477: isgreater & friends DO NOT raise FE_INVALID on NaN ---- */
static void test_quiet_compare_no_raise(void) {
    double n = (double)NAN;
    feclearexcept(FE_INVALID);
    (void)isgreater(n, 1.0);
    (void)isgreaterequal(n, 1.0);
    (void)isless(n, 1.0);
    (void)islessequal(n, 1.0);
    (void)islessgreater(n, 1.0);
    (void)isunordered(n, 1.0);
    EXPECT(!fetestexcept(FE_INVALID),
           "isgreater/etc. must not raise FE_INVALID on NaN");

    /* Sanity: ordered comparisons return correct truth values. */
    EXPECT( isgreater(2.0, 1.0),       "isgreater(2,1)");
    EXPECT(!isgreater(1.0, 2.0),       "isgreater(1,2)");
    EXPECT( isgreaterequal(1.0, 1.0),  "isgreaterequal(1,1)");
    EXPECT( isless(1.0, 2.0),          "isless(1,2)");
    EXPECT( islessequal(1.0, 1.0),     "islessequal(1,1)");
    EXPECT( islessgreater(1.0, 2.0),   "islessgreater(1,2)");
    EXPECT(!islessgreater(1.0, 1.0),   "islessgreater(1,1) must be false");
    /* All comparisons against NaN return false. */
    EXPECT(!isgreater(n, n),           "isgreater(NaN,NaN)");
    EXPECT(!isless(n, 1.0),            "isless(NaN,1)");
}

/* ---- REQ-06-0478: isunordered iff either operand is NaN ---- */
static void test_isunordered(void) {
    double n = (double)NAN;
    EXPECT( isunordered(n, 1.0),     "isunordered(NaN,1)");
    EXPECT( isunordered(1.0, n),     "isunordered(1,NaN)");
    EXPECT( isunordered(n, n),       "isunordered(NaN,NaN)");
    EXPECT(!isunordered(1.0, 2.0),   "isunordered(1,2)");
    EXPECT(!isunordered(0.0, 0.0),   "isunordered(0,0)");
    EXPECT(!isunordered(INFINITY, 0.0), "isunordered(INF,0)");
}

/* ---- REQ-06-0479: iseqsig raises FE_INVALID on NaN ---- */
static void test_iseqsig_raises(void) {
    double n = (double)NAN;

    feclearexcept(FE_ALL_EXCEPT);
    int eq = iseqsig(n, 1.0);
    EXPECT(eq == 0, "iseqsig(NaN,1) returns 0");
    EXPECT(fetestexcept(FE_INVALID),
           "iseqsig must raise FE_INVALID on NaN");

    feclearexcept(FE_ALL_EXCEPT);
    EXPECT(iseqsig(1.0, 2.0) == 0, "iseqsig(1,2) returns 0");
    EXPECT(!fetestexcept(FE_INVALID),
           "iseqsig on ordered operands must NOT raise FE_INVALID");

    feclearexcept(FE_ALL_EXCEPT);
    EXPECT(iseqsig(1.5, 1.5) == 1, "iseqsig(1.5,1.5) returns 1");
    EXPECT(!fetestexcept(FE_INVALID),
           "iseqsig on equal operands must NOT raise FE_INVALID");
}

/* ---- REQ-06-0480: issignaling detects sNaN bit pattern ---- */
static void test_issignaling(void) {
    double sn = make_snan_d();
    float  snf = make_snan_f();

    EXPECT(issignaling(sn),          "issignaling(sNaN double)");
    EXPECT(issignaling(snf),         "issignaling(sNaN float)");

    /* Quiet NaN (NAN macro) must NOT be signaling. */
    EXPECT(!issignaling((double)NAN),"issignaling(qNaN) must be false");

    /* Non-NaN values are never signaling. */
    EXPECT(!issignaling(0.0),        "issignaling(0)");
    EXPECT(!issignaling(1.0),        "issignaling(1)");
    EXPECT(!issignaling(INFINITY),   "issignaling(INF)");
    EXPECT(!issignaling(-INFINITY),  "issignaling(-INF)");
}

/* ---- C23 issubnormal / iszero ---- */
static void test_c23_predicates(void) {
    EXPECT(issubnormal(DBL_MIN / 2), "issubnormal(denorm)");
    EXPECT(!issubnormal(0.0),        "issubnormal(0)");
    EXPECT(!issubnormal(1.0),        "issubnormal(1)");
    EXPECT(!issubnormal(INFINITY),   "issubnormal(INF)");

    EXPECT(iszero(0.0),              "iszero(+0)");
    EXPECT(iszero(-0.0),             "iszero(-0)");
    EXPECT(!iszero(DBL_MIN / 2),     "iszero(denorm)");
    EXPECT(!iszero(1.0),             "iszero(1)");

    EXPECT(iscanonical(0.0),         "iscanonical(0)");
    EXPECT(iscanonical(1.0),         "iscanonical(1)");
    EXPECT(iscanonical(INFINITY),    "iscanonical(INF)");
}

/* ---- REQ-06-0481: float and long double variants of the basics ---- */
static void test_float_variants(void) {
    EXPECT(fpclassify(0.0f) == FP_ZERO,            "fpclassify float zero");
    EXPECT(fpclassify(INFINITY) == FP_INFINITE,    "fpclassify float INF");
    EXPECT(isnan((float)NAN),                      "isnan float NaN");
    EXPECT(signbit(-1.0f),                         "signbit float neg");
    EXPECT(!isfinite((float)INFINITY),             "isfinite float INF");
    EXPECT(isnormal(1.0f),                         "isnormal float 1");
}

static void test_long_double_variants(void) {
    long double inf = (long double)INFINITY;
    long double nan = (long double)NAN;
    EXPECT(fpclassify(0.0L) == FP_ZERO,            "fpclassify long double zero");
    EXPECT(fpclassify(inf)  == FP_INFINITE,        "fpclassify long double INF");
    EXPECT(isnan(nan),                             "isnan long double NaN");
    EXPECT(signbit(-1.0L),                         "signbit long double neg");
    EXPECT(isnormal(1.0L),                         "isnormal long double 1");
}

int main(void) {
    test_fpclassify_categories();
    test_isfinite();
    test_isinf();
    test_isnan();
    test_isnormal();
    test_signbit();
    test_quiet_compare_no_raise();
    test_isunordered();
    test_iseqsig_raises();
    test_issignaling();
    test_c23_predicates();
    test_float_variants();
    test_long_double_variants();

    if (failures == 0) {
        puts("PASS: test_classify");
        return 0;
    }
    printf("FAIL: %d failure(s)\n", failures);
    return 1;
}
