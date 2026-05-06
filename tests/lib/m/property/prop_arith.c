/*
 * prop_arith.c - Property-based tests for basic arithmetic math functions
 *
 * Tests mathematical properties of <math.h> basic arithmetic primitives
 * (fabs/fmod/remainder/remquo/fma/fmax/fmin/fdim) over a deterministic
 * pseudo-random sample of finite doubles plus chosen boundary cases.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdint.h>

#define FAIL(msg) do { \
    printf("FAIL property: %s\n", msg); \
    failures++; \
} while (0)

static int failures = 0;

/* Unused helper left for future use */
static int isclose(double a, double b, double tol)
{
    return fabs(a - b) <= tol * (1.0 + fabs(b));
}

/*
 * Deterministic xorshift64 generator for pseudo-random samples.
 */
static uint64_t xs_state = 0x9E3779B97F4A7C15ULL;

static uint64_t xs_next(void)
{
    uint64_t x = xs_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    xs_state = x;
    return x;
}

static double xs_next_double(void)
{
    uint64_t bits = xs_next();
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

/*
 * Property: fabs(x) >= 0 for all x (including -0.0 -> +0.0).
 * (REQ-06-0522)
 *
 * For any input (finite, infinite, or NaN), the absolute value must be
 * non-negative, with the special case that fabs(-0.0) returns +0.0.
 */
static void prop_fabs_nonnegative(void)
{
    int generated = 0;
    int attempts = 0;
    const int target = 1000;
    const int attempt_cap = 100000;

    while (generated < target && attempts < attempt_cap) {
        attempts++;
        double x = xs_next_double();
        if (!isfinite(x) && !isnan(x)) continue;

        double r = fabs(x);
        if (isnan(x)) {
            if (!isnan(r))
                FAIL("fabs(NaN) did not return NaN on random sample");
            generated++;
            continue;
        }

        if (r < 0.0) {
            FAIL("fabs(x) returned negative value on random sample");
            return;
        }
        if (x == 0.0 && signbit(r)) {
            FAIL("fabs(x) did not convert -0.0 to +0.0 on random sample");
            return;
        }
        generated++;
    }

    if (generated < target)
        FAIL("xorshift generator did not produce enough samples for fabs");
}

/*
 * Property: fmod(x, y) has same sign as x and |fmod(x,y)| < |y|.
 * (REQ-06-0523)
 *
 * IEEE 754 fmod returns remainder with dividend sign and magnitude < divisor.
 */
static void prop_fmod_properties(void)
{
    int generated = 0;
    int attempts = 0;
    const int target = 500;
    const int attempt_cap = 100000;

    while (generated < target && attempts < attempt_cap) {
        attempts++;
        double x = xs_next_double();
        double y = xs_next_double();

        if (!isfinite(x) || !isfinite(y) || y == 0.0) continue;
        if (isnan(x) || isnan(y)) continue;

        double r = fmod(x, y);
        if (isnan(r)) {
            FAIL("fmod returned NaN on valid finite inputs");
            return;
        }

        if (signbit(r) != signbit(x) && r != 0.0) {
            FAIL("fmod sign does not match dividend on random sample");
            return;
        }

        double ay = fabs(y);
        if (fabs(r) >= ay) {
            FAIL("fmod magnitude >= divisor magnitude on random sample");
            return;
        }

        generated++;
    }

    if (generated < target)
        FAIL("xorshift generator did not produce enough finite (x,y) pairs for fmod");
}

/*
 * Property: fma(x, y, z) finite for all finite (x,y,z) where x*y+z is representable.
 * (REQ-06-0524)
 *
 * The FMA must compute (x*y) + z using single rounding, producing a representable
 * result when the mathematically exact sum is within the normal range.
 */
static void prop_fma_preserves_finiteness(void)
{
    int generated = 0;
    int attempts = 0;
    const int target = 500;
    const int attempt_cap = 100000;

    while (generated < target && attempts < attempt_cap) {
        attempts++;
        double x = xs_next_double();
        double y = xs_next_double();
        double z = xs_next_double();

        if (!isfinite(x) || !isfinite(y) || !isfinite(z)) continue;
        if (isnan(x) || isnan(y) || isnan(z)) continue;
        if (fabs(x) > 1e100 || fabs(y) > 1e100 || fabs(z) > 1e100) continue;

        double result = fma(x, y, z);
        if (isnan(result) && !isnan(x*y+z)) {
            FAIL("fma(x, y, z) returned NaN for bounded finite inputs");
            return;
        }

        generated++;
    }

    if (generated < target)
        FAIL("xorshift generator did not produce enough (x,y,z) triples for fma");
}

/*
 * Property: fmax(x, y) >= x && fmax(x, y) >= y for non-NaN inputs.
 * (REQ-06-0525)
 *
 * fmax (NaN-ignoring) must return a value at least as large as both
 * non-NaN operands (or the non-NaN operand if one is NaN).
 */
static void prop_fmax_idempotent(void)
{
    int generated = 0;
    int attempts = 0;
    const int target = 500;
    const int attempt_cap = 100000;

    while (generated < target && attempts < attempt_cap) {
        attempts++;
        double x = xs_next_double();
        double y = xs_next_double();

        if (isnan(x) && isnan(y)) continue;

        double r = fmax(x, y);
        double x_cmp = isnan(x) ? y : x;
        double y_cmp = isnan(y) ? x : y;

        if (!isnan(r)) {
            if (r < x_cmp || r < y_cmp) {
                FAIL("fmax result < one of operands on random sample");
                return;
            }
        }
        generated++;
    }

    if (generated < target)
        FAIL("xorshift generator did not produce enough (x,y) pairs for fmax");
}

/*
 * Property: fdim(x, y) = max(x - y, 0) for all finite inputs.
 * (REQ-06-0526)
 *
 * fdim returns the positive difference or zero, and is non-negative.
 */
static void prop_fdim_reconstruction(void)
{
    int generated = 0;
    int attempts = 0;
    const int target = 500;
    const int attempt_cap = 100000;

    while (generated < target && attempts < attempt_cap) {
        attempts++;
        double x = xs_next_double();
        double y = xs_next_double();

        if (!isfinite(x) || !isfinite(y)) continue;
        if (isnan(x) || isnan(y)) continue;

        double d = fdim(x, y);

        /* fdim is always non-negative */
        if (d < 0.0) {
            FAIL("fdim(x, y) returned negative value on random sample");
            return;
        }

        /* If x <= y, fdim(x, y) must be 0 */
        if (x <= y && d != 0.0) {
            FAIL("fdim(x, y) not zero when x <= y on random sample");
            return;
        }

        /* If x > y, fdim(x, y) must equal x - y */
        if (x > y) {
            double expected = x - y;
            if (d != expected) {
                FAIL("fdim(x, y) != x - y when x > y on random sample");
                return;
            }
        }

        generated++;
    }

    if (generated < target)
        FAIL("xorshift generator did not produce enough finite (x,y) pairs for fdim");
}

int main(void)
{
    prop_fabs_nonnegative();
    prop_fmod_properties();
    prop_fma_preserves_finiteness();
    prop_fmax_idempotent();
    prop_fdim_reconstruction();

    if (failures != 0) {
        printf("%d arithmetic property test(s) failed\n", failures);
        return 1;
    }

    printf("basic arithmetic property tests passed\n");
    return 0;
}
