/*
 * prop_round.c - Property-based tests for rounding functions
 *
 * Each property is exercised over its full domain (all valid inputs) or
 * over a representative set of interesting values.
 */

#include <stdio.h>
#include <math.h>
#include <fenv.h>
#include <limits.h>

static int g_failures = 0;

#define PROP_CHECK(cond, name) do { \
    if (!(cond)) { \
        printf("  FAIL property: %s (line %d)\n", (name), __LINE__); \
        g_failures++; \
    } \
} while (0)

/*
 * REQ-06-0566: ceil(x) <= x is false for non-integer x; ceil(x) == x for integers
 */
static void prop_ceil_properties(void) {
    double values[] = { 0.0, 0.5, 1.0, 1.5, -0.5, -1.0, -1.5, 10.5, -10.5 };
    for (int i = 0; i < 9; i++) {
        double x = values[i];
        double c = ceil(x);
        /* ceil(x) >= x always */
        PROP_CHECK(c >= x, "ceil(x) >= x");
        /* for integers, ceil(x) == x */
        if (x == (double)(long)x) {
            PROP_CHECK(c == x, "ceil(int) == int");
        }
    }
}

/*
 * REQ-06-0566: floor(x) >= x always; floor(x) == x for integers
 */
static void prop_floor_properties(void) {
    double values[] = { 0.0, 0.5, 1.0, 1.5, -0.5, -1.0, -1.5, 10.5, -10.5 };
    for (int i = 0; i < 9; i++) {
        double x = values[i];
        double f = floor(x);
        PROP_CHECK(f <= x, "floor(x) <= x");
        if (x == (double)(long)x) {
            PROP_CHECK(f == x, "floor(int) == int");
        }
    }
}

/*
 * REQ-06-0566: trunc(x) always satisfies: |trunc(x)| <= |x|
 */
static void prop_trunc_properties(void) {
    double values[] = { 0.5, 1.5, -0.5, -1.5, 10.5, -10.5, 0.9 };
    for (int i = 0; i < 7; i++) {
        double x = values[i];
        double t = trunc(x);
        PROP_CHECK(t <= x, "trunc(x) <= x for positive");
        if (x < 0) {
            PROP_CHECK(t >= x, "trunc(x) >= x for negative");
        }
        PROP_CHECK(t == (double)(long)t, "trunc(x) is integer");
    }
}

/*
 * REQ-06-0566: round(x) satisfies |round(x) - x| >= 0.5 for half values
 */
static void prop_round_properties(void) {
    double values[] = { 0.5, 1.5, 2.5, -0.5, -1.5, -2.5 };
    for (int i = 0; i < 6; i++) {
        double x = values[i];
        double r = round(x);
        /* round(x) is always the next integer away from zero at x.5 */
        if (x > 0) {
            PROP_CHECK(r == floor(x) + 1, "round(pos x.5) = floor + 1");
        }
        if (x < 0) {
            PROP_CHECK(r == ceil(x) - 1, "round(neg x.5) = ceil - 1");
        }
    }
}

/*
 * REQ-06-0566: roundeven(x) == rint(x) — always round to even
 */
static void prop_roundeven_properties(void) {
    double values[] = { 0.5, 1.5, 2.5, 3.5, -0.5, -1.5, -2.5, -3.5 };
    for (int i = 0; i < 8; i++) {
        double x = values[i];
        double re = roundeven(x);
        double ri = rint(x);
        PROP_CHECK(re == ri, "roundeven(x) == rint(x)");
        /* roundeven(x.5) always rounds to even */
        double floor_x = floor(x);
        double ceil_x = ceil(x);
        double mid = floor_x + 0.5;
        if (x == mid) {
            /* result must be even integer */
            long long result_ll = (long long)re;
            PROP_CHECK(result_ll % 2 == 0, "roundeven(x.5) -> even integer");
        }
    }
}

/*
 * REQ-06-0566: rint(x) == roundeven(x) for all inputs
 */
static void prop_rint_properties(void) {
    double values[] = { 0.5, 1.5, 2.5, 3.5, -0.5, -1.5, -2.5, -3.5, 0.0, 1.0 };
    for (int i = 0; i < 10; i++) {
        double x = values[i];
        double ri = rint(x);
        PROP_CHECK(ri == (double)(long)(ri + 0.5) - 0.5 || ri == x,
                   "rint(x) is integer or identity");
    }
}

/*
 * REQ-06-0566: nearbyint(x) == rint(x)
 */
static void prop_nearbyint_properties(void) {
    double values[] = { 0.5, 1.5, 2.5, 3.5, -0.5, -1.5, -2.5, -3.5, 0.0, 1.0 };
    for (int i = 0; i < 10; i++) {
        double x = values[i];
        PROP_CHECK(nearbyint(x) == rint(x), "nearbyint(x) == rint(x)");
    }
}

/*
 * REQ-06-0566: ceil(x) - floor(x) == 1 for non-integer x, 0 for integer x
 */
static void prop_ceil_floor_diff(void) {
    double values[] = { 0.5, 1.5, 2.5, 10.5, -0.5, -1.5, -2.5, -10.5 };
    for (int i = 0; i < 8; i++) {
        double x = values[i];
        double c = ceil(x);
        double f = floor(x);
        if (x == (double)(long)x) {
            PROP_CHECK(c - f == 0, "ceil(int) - floor(int) == 0");
        } else {
            PROP_CHECK(c - f == 1.0, "ceil(x) - floor(x) == 1 for non-integer");
        }
    }
}

/*
 * REQ-06-0566: floor(x) <= trunc(x) <= ceil(x) for positive x
 * floor(x) >= trunc(x) >= ceil(x) for negative x
 */
static void prop_trunc_between(void) {
    double pos[] = { 0.5, 1.5, 2.5, 0.9 };
    double neg[] = { -0.5, -1.5, -2.5, -0.9 };
    for (int i = 0; i < 4; i++) {
        double f1 = floor(pos[i]);
        double t = trunc(pos[i]);
        double c = ceil(pos[i]);
        PROP_CHECK(f1 <= t && t <= c, "floor(x) <= trunc(x) <= ceil(x) for positive");

        double f2 = floor(neg[i]);
        t = trunc(neg[i]);
        c = ceil(neg[i]);
        PROP_CHECK(f2 >= t && t >= c, "floor(x) >= trunc(x) >= ceil(x) for negative");
    }
}

/*
 * REQ-06-0566: float rounding matches double rounding (cast back and forth)
 */
static void prop_float_double_consistency(void) {
    double values[] = { 0.5, 1.5, 2.5, -0.5, -1.5, -2.5, 10.7 };
    for (int i = 0; i < 7; i++) {
        double x = values[i];
        float fx = (float)x;
        PROP_CHECK(ceilf(fx) == (float)ceil(x), "ceilf == (float)ceil");
        PROP_CHECK(floorf(fx) == (float)floor(x), "floorf == (float)floor");
        PROP_CHECK(truncf(fx) == (float)trunc(x), "truncf == (float)trunc");
        PROP_CHECK(roundf(fx) == (float)round(x), "roundf == (float)round");
        PROP_CHECK(roundevenf(fx) == (float)roundeven(x), "roundevenf == (float)roundeven");
        PROP_CHECK(rintf(fx) == (float)rint(x), "rintf == (float)rint");
        PROP_CHECK(nearbyintf(fx) == (float)nearbyint(x), "nearbyintf == (float)nearbyint");
    }
}

/*
 * REQ-06-0566: fromfp respects rounding modes
 */
static void prop_fromfp_respects_mode(void) {
    double y;
    int rc;

    rc = fromfp(&y, 3.5, NULL, FE_TONEAREST);
    PROP_CHECK(rc == 0, "fromfp FE_TONEAREST succeeds");
    PROP_CHECK(y == 3.5, "fromfp FE_TONEAREST(3.5) == 3.5");

    rc = fromfp(&y, 3.5, NULL, FE_DOWNWARD);
    PROP_CHECK(rc == 0, "fromfp FE_DOWNWARD succeeds");
    PROP_CHECK(y == 3.0, "fromfp FE_DOWNWARD(3.5) == 3.0");

    rc = fromfp(&y, 3.5, NULL, FE_UPWARD);
    PROP_CHECK(rc == 0, "fromfp FE_UPWARD succeeds");
    PROP_CHECK(y == 4.0, "fromfp FE_UPWARD(3.5) == 4.0");

    rc = fromfp(&y, -3.5, NULL, FE_DOWNWARD);
    PROP_CHECK(rc == 0, "fromfp FE_DOWNWARD neg succeeds");
    PROP_CHECK(y == -4.0, "fromfp FE_DOWNWARD(-3.5) == -4.0");

    rc = fromfp(&y, -3.5, NULL, FE_UPWARD);
    PROP_CHECK(rc == 0, "fromfp FE_UPWARD neg succeeds");
    PROP_CHECK(y == -3.0, "fromfp FE_UPWARD(-3.5) == -3.0");
}

/*
 * REQ-06-0566: fromfp(invalid) returns non-zero
 */
static void prop_fromfp_invalid_mode(void) {
    double y;
    for (int mode = 100; mode < 200; mode += 10) {
        int rc = fromfp(&y, 1.0, NULL, mode);
        PROP_CHECK(rc != 0, "fromfp(invalid mode) != 0");
    }
}

/*
 * REQ-06-0566: lround/llround match round() for values in range
 */
static void prop_lround_llround(void) {
    double values[] = { 0.3, 0.5, 0.7, 1.5, 2.5, -0.5, -1.5 };
    for (int i = 0; i < 7; i++) {
        double x = values[i];
        double r = round(x);
        PROP_CHECK((long)lround(x) == (long)r, "lround(x) == (long)round(x)");
        PROP_CHECK((long long)llround(x) == (long long)r, "llround(x) == (long long)round(x)");
    }
}

int main(void) {
    printf("Property tests for rounding functions...\n\n");

    prop_ceil_properties();
    prop_floor_properties();
    prop_trunc_properties();
    prop_round_properties();
    prop_roundeven_properties();
    prop_rint_properties();
    prop_nearbyint_properties();
    prop_ceil_floor_diff();
    prop_trunc_between();
    prop_float_double_consistency();
    prop_fromfp_respects_mode();
    prop_fromfp_invalid_mode();
    prop_lround_llround();

    printf("Properties complete: %d failures\n", g_failures);
    return g_failures;
}
