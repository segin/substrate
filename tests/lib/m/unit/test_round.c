/*
 * test_round.c - Comprehensive unit tests for rounding functions
 *
 * Covers rounding (ceil, floor, trunc, round, roundeven, rint, nearbyint),
 * integer conversions (lrint, llrint, lround, llround), and float/ld variants.
 */

#include <stdio.h>
#include <math.h>
#include <fenv.h>
#include <assert.h>
#include <limits.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", (msg), __LINE__); \
        g_failures++; \
    } \
} while (0)

#define CHECK_EQ(a, b, msg) CHECK((a) == (b), msg)
#define CHECK_NE(a, b, msg) CHECK((a) != (b), msg)

/* REQ-06-0555: ceil returns smallest integer >= x */
static void test_ceil(void) {
    CHECK_EQ(ceil(1.5), 2.0, "ceil(1.5) == 2");
    CHECK_EQ(ceil(-1.5), -1.0, "ceil(-1.5) == -1");
    CHECK_EQ(ceil(0.0), 0.0, "ceil(0.0) == 0");
    CHECK_EQ(ceil(-0.0), -0.0, "ceil(-0.0) == -0");
    CHECK_EQ(ceil(INFINITY), INFINITY, "ceil(INFINITY)");
    CHECK_NE(isnan(ceil(NAN)), 0, "ceil(NAN) is NaN");
    CHECK_EQ(ceil(1.0), 1.0, "ceil(1.0) == 1");
    CHECK_EQ(ceil(-5.0), -5.0, "ceil(-5.0) == -5");
}

/* REQ-06-0555: floor returns largest integer <= x */
static void test_floor(void) {
    CHECK_EQ(floor(1.5), 1.0, "floor(1.5) == 1");
    CHECK_EQ(floor(-1.5), -2.0, "floor(-1.5) == -2");
    CHECK_EQ(floor(0.0), 0.0, "floor(0.0) == 0");
    CHECK_EQ(floor(INFINITY), INFINITY, "floor(INFINITY)");
    CHECK_NE(isnan(floor(NAN)), 0, "floor(NAN) is NaN");
    CHECK_EQ(floor(1.0), 1.0, "floor(1.0) == 1");
    CHECK_EQ(floor(-5.0), -5.0, "floor(-5.0) == -5");
}

/* REQ-06-0555: trunc returns nearest integer toward zero */
static void test_trunc(void) {
    CHECK_EQ(trunc(1.5), 1.0, "trunc(1.5) == 1");
    CHECK_EQ(trunc(-1.5), -1.0, "trunc(-1.5) == -1");
    CHECK_EQ(trunc(0.0), 0.0, "trunc(0.0) == 0");
    CHECK_EQ(trunc(-0.0), -0.0, "trunc(-0.0) == -0");
    CHECK_NE(isnan(trunc(NAN)), 0, "trunc(NAN) is NaN");
    CHECK_EQ(trunc(INFINITY), INFINITY, "trunc(INFINITY)");
}

/* REQ-06-0555: round rounds half-away-from-zero */
static void test_round(void) {
    CHECK_EQ(round(0.5), 1.0, "round(0.5) == 1");
    CHECK_EQ(round(-0.5), -1.0, "round(-0.5) == -1");
    CHECK_EQ(round(1.5), 2.0, "round(1.5) == 2");
    CHECK_EQ(round(-1.5), -2.0, "round(-1.5) == -2");
    CHECK_EQ(round(2.5), 3.0, "round(2.5) == 3");
    CHECK_EQ(round(-2.5), -3.0, "round(-2.5) == -3");
    CHECK_EQ(round(0.0), 0.0, "round(0.0) == 0");
    CHECK_NE(isnan(round(NAN)), 0, "round(NAN) is NaN");
    CHECK_EQ(round(INFINITY), INFINITY, "round(INFINITY)");
}

/* REQ-06-0555: roundeven rounds half-to-even (banker's rounding) */
static void test_roundeven(void) {
    CHECK_EQ(roundeven(0.5), 0.0, "roundeven(0.5) == 0 (to even)");
    CHECK_EQ(roundeven(-0.5), 0.0, "roundeven(-0.5) == 0 (to even)");
    CHECK_EQ(roundeven(1.5), 2.0, "roundeven(1.5) == 2 (to even)");
    CHECK_EQ(roundeven(-1.5), -2.0, "roundeven(-1.5) == -2 (to even)");
    CHECK_EQ(roundeven(2.5), 2.0, "roundeven(2.5) == 2 (to even)");
    CHECK_EQ(roundeven(-2.5), -2.0, "roundeven(-2.5) == -2 (to even)");
    CHECK_EQ(roundeven(3.5), 4.0, "roundeven(3.5) == 4 (to even)");
    CHECK_EQ(roundeven(-3.5), -4.0, "roundeven(-3.5) == -4 (to even)");
    CHECK_EQ(roundeven(0.0), 0.0, "roundeven(0.0) == 0");
    CHECK_EQ(roundeven(1.0), 1.0, "roundeven(1.0) == 1");
    CHECK_NE(isnan(roundeven(NAN)), 0, "roundeven(NAN) is NaN");
    CHECK_EQ(roundeven(INFINITY), INFINITY, "roundeven(INFINITY)");
}

/* REQ-06-0555: rint rounds to nearest, ties to even */
static void test_rint(void) {
    CHECK_EQ(rint(0.5), 0.0, "rint(0.5) == 0 (to even)");
    CHECK_EQ(rint(1.5), 2.0, "rint(1.5) == 2 (to even)");
    CHECK_EQ(rint(2.5), 2.0, "rint(2.5) == 2 (to even)");
    CHECK_EQ(rint(3.5), 4.0, "rint(3.5) == 4 (to even)");
    CHECK_EQ(rint(0.0), 0.0, "rint(0.0) == 0");
    CHECK_NE(isnan(rint(NAN)), 0, "rint(NAN) is NaN");
    CHECK_EQ(rint(INFINITY), INFINITY, "rint(INFINITY)");
}

/* REQ-06-0555: nearbyint behaves like rint */
static void test_nearbyint(void) {
    CHECK_EQ(nearbyint(0.5), 0.0, "nearbyint(0.5) == 0");
    CHECK_EQ(nearbyint(1.5), 2.0, "nearbyint(1.5) == 2");
    CHECK_EQ(nearbyint(0.0), 0.0, "nearbyint(0.0) == 0");
    CHECK_NE(isnan(nearbyint(NAN)), 0, "nearbyint(NAN) is NaN");
}

/* REQ-06-0555: float variants */
static void test_float_rounding(void) {
    CHECK_EQ(ceilf(1.5f), 2.0f, "ceilf(1.5) == 2");
    CHECK_EQ(floorf(1.5f), 1.0f, "floorf(1.5) == 1");
    CHECK_EQ(truncf(1.5f), 1.0f, "truncf(1.5) == 1");
    CHECK_EQ(roundf(1.5f), 2.0f, "roundf(1.5) == 2");
    CHECK_EQ(roundevenf(0.5f), 0.0f, "roundevenf(0.5) == 0");
    CHECK_EQ(roundevenf(1.5f), 2.0f, "roundevenf(1.5) == 2");
    CHECK_EQ(rintf(0.5f), 0.0f, "rintf(0.5) == 0");
    CHECK_EQ(nearbyintf(0.5f), 0.0f, "nearbyintf(0.5) == 0");
}

/* REQ-06-0555: long double variants */
static void test_ld_rounding(void) {
    CHECK_EQ(ceill(1.5), 2.0, "ceill(1.5) == 2");
    CHECK_EQ(floorl(1.5), 1.0, "floorl(1.5) == 1");
    CHECK_EQ(truncl(1.5), 1.0, "truncl(1.5) == 1");
    CHECK_EQ(roundl(1.5), 2.0, "roundl(1.5) == 2");
    CHECK_EQ(roundevenl(0.5), 0.0, "roundevenl(0.5) == 0");
    CHECK_EQ(roundevenl(1.5), 2.0, "roundevenl(1.5) == 2");
}

/* REQ-06-0555: integer conversion rounding functions */
static void test_lround_llround(void) {
    CHECK_EQ(lround(0.3), 0, "lround(0.3) == 0");
    CHECK_EQ(lround(0.5), 1, "lround(0.5) == 1");
    CHECK_EQ(lround(1.5), 2, "lround(1.5) == 2");
    CHECK_EQ(lround(-1.5), -2, "lround(-1.5) == -2");
    CHECK_EQ(llround(0.3), 0, "llround(0.3) == 0");
    CHECK_EQ(llround(0.5), 1, "llround(0.5) == 1");
    CHECK_EQ(llround(1.5), 2, "llround(1.5) == 2");
    CHECK_EQ(llround(-1.5), -2, "llround(-1.5) == -2");
}

/* REQ-06-0555: float variants of lround/llround */
static void test_float_integer_rounding(void) {
    CHECK_EQ(lroundf(0.5f), 1, "lroundf(0.5) == 1");
    CHECK_EQ(llroundf(0.5f), 1, "llroundf(0.5) == 1");
}

/* REQ-06-0555: fromfp family */
static void test_fromfp(void) {
    double y;
    fenv_t env;
    int rc;

    rc = fromfp(&y, 1.5, NULL, FE_TONEAREST);
    CHECK(rc == 0, "fromfp returns 0");
    CHECK_EQ(y, 1.5, "fromfp stores 1.5");

    rc = fromfp(&y, 1.5, NULL, FE_DOWNWARD);
    CHECK(rc == 0, "fromfp FE_DOWNWARD returns 0");
    CHECK_EQ(y, 1.0, "fromfp FE_DOWNWARD(1.5) == 1.0");

    rc = fromfp(&y, 1.5, NULL, FE_UPWARD);
    CHECK(rc == 0, "fromfp FE_UPWARD returns 0");
    CHECK_EQ(y, 2.0, "fromfp FE_UPWARD(1.5) == 2.0");

    rc = fromfp(&y, 1.5, NULL, FE_TOWARDZERO);
    CHECK(rc == 0, "fromfp FE_TOWARDZERO returns 0");
    CHECK_EQ(y, 1.0, "fromfp FE_TOWARDZERO(1.5) == 1.0");

    rc = fromfp(&y, 1.5, NULL, 999);
    CHECK(rc != 0, "fromfp invalid mode returns non-zero");
}

/* REQ-06-0555: fromfpx (same as fromfp) */
static void test_fromfpx(void) {
    double y;
    int rc;
    rc = fromfpx(&y, 2.5, NULL, FE_DOWNWARD);
    CHECK(rc == 0, "fromfpx returns 0");
    CHECK_EQ(y, 2.0, "fromfpx FE_DOWNWARD(2.5) == 2.0");
}

/* REQ-06-0555: ufromfp */
static void test_ufromfp(void) {
    unsigned int y;
    int rc;
    rc = ufromfp(&y, 3.5, NULL, FE_DOWNWARD);
    CHECK(rc == 0, "ufromfp returns 0");
    CHECK_EQ(y, 3u, "ufromfp FE_DOWNWARD(3.5) == 3");
}

/* REQ-06-0555: ufromfpx */
static void test_ufromfpx(void) {
    unsigned int y;
    int rc;
    rc = ufromfpx(&y, 3.5, NULL, FE_TONEAREST);
    CHECK(rc == 0, "ufromfpx returns 0");
    CHECK_EQ(y, 3u, "ufromfpx FE_TONEAREST(3.5) == 3");
}

/* REQ-06-0555: fromfp with envp */
static void test_fromfp_env(void) {
    double y;
    fenv_t env;
    int rc = fromfp(&y, 1.5, &env, FE_TONEAREST);
    CHECK(rc == 0, "fromfp with envp returns 0");
    (void)env;
    (void)y;
}

static void test_fromfp_float(void) {
    float y;
    int rc;
    rc = fromfpf(&y, 1.5f, NULL, FE_DOWNWARD);
    CHECK(rc == 0, "fromfpf returns 0");
    CHECK_EQ(y, 1.0f, "fromfpf FE_DOWNWARD(1.5) == 1.0");
}

static void test_fromfp_float_towardzero(void) {
    float y;
    int rc;
    rc = fromfpxf(&y, -1.5f, NULL, FE_TOWARDZERO);
    CHECK(rc == 0, "fromfpxf returns 0");
    CHECK_EQ(y, -1.0f, "fromfpxf FE_TOWARDZERO(-1.5) == -1.0");
}

static void test_ufromfp_float(void) {
    unsigned int y;
    int rc;
    rc = ufromfpf(&y, 3.5f, NULL, FE_TONEAREST);
    CHECK(rc == 0, "ufromfpf returns 0");
    CHECK_EQ(y, 3u, "ufromfpf FE_TONEAREST(3.5) == 3");
}

static void test_fromfp_ld(void) {
    long double y;
    int rc = fromfpxl(&y, 1.5, NULL, FE_UPWARD);
    CHECK(rc == 0, "fromfpxl returns 0");
    CHECK_EQ(y, 2.0, "fromfpxl FE_UPWARD(1.5) == 2.0");
}

static void test_fromfp_ld_towardzero(void) {
    long double y;
    int rc = fromfpl(&y, -2.5, NULL, FE_TOWARDZERO);
    CHECK(rc == 0, "fromfpl returns 0");
    CHECK_EQ(y, -2.0, "fromfpl FE_TOWARDZERO(-2.5) == -2.0");
}

static void test_ufromfp_ld(void) {
    unsigned int y;
    int rc = ufromfpul(&y, 4.5, NULL, FE_TONEAREST);
    CHECK(rc == 0, "ufromfpul returns 0");
    CHECK_EQ(y, 4u, "ufromfpul FE_TONEAREST(4.5) == 4");
}

int main(void) {
    printf("Testing rounding functions...\n\n");

    test_ceil();
    test_floor();
    test_trunc();
    test_round();
    test_roundeven();
    test_rint();
    test_nearbyint();
    test_float_rounding();
    test_ld_rounding();
    test_lround_llround();
    test_float_integer_rounding();
    test_fromfp();
    test_fromfpx();
    test_ufromfp();
    test_ufromfpx();
    test_fromfp_env();
    test_fromfp_float();
    test_fromfp_float_towardzero();
    test_ufromfp_float();
    test_fromfp_ld();
    test_fromfp_ld_towardzero();
    test_ufromfp_ld();

    printf("Tests complete: %d failures\n", g_failures);
    return g_failures;
}
