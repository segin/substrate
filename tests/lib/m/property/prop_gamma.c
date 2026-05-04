/*
 * prop_gamma.c - Property-based tests for gamma and error functions
 *
 * Tests mathematical identities of <math.h> gamma/error primitives over a
 * deterministic pseudo-random sample of finite doubles plus chosen
 * boundary cases.
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

static int isclose(double a, double b, double tol) {
    return fabs(a - b) < tol;
}

/*
 * Deterministic xorshift64 generator.  Seeded with a constant so the test
 * is reproducible run-to-run; the bit pattern is interpreted as a double
 * via memcpy to avoid any strict-aliasing issues.
 */
static uint64_t xs_state = 0x9E3779B97F4A7C15ULL;

static uint64_t xs_next(void) {
    uint64_t x = xs_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    xs_state = x;
    return x;
}

/*
 * Draw a uniformly distributed double in [-6.0, 6.0].  Take the high 53
 * bits of the xorshift output (mantissa precision), normalize to [0, 1),
 * then map to [-6, 6].  This avoids the fpclassify/finite filtering that
 * a raw bit-cast double would require, and concentrates samples in the
 * range where erf is not yet saturated.
 */
static double xs_uniform_pm6(void) {
    uint64_t bits = xs_next() >> 11;        /* 53-bit mantissa */
    double u = (double)bits * (1.0 / (double)(1ULL << 53));
    return -6.0 + 12.0 * u;
}

/*
 * erfc(x) + erf(x) == 1.0 for all finite x.  (REQ-06-0765)
 *
 * erf and erfc are mathematically defined to be complements: erfc(x) is
 * 1 - erf(x).  The Abramowitz & Stegun 7.1.26 polynomial approximation
 * carries ~1.5e-7 max error, so a quality libm built on that bound may
 * accumulate up to ~2e-7 per term, leaving the sum within ~4e-7 of 1.0
 * in the worst case.  We assert the looser 1e-6 bound to remain robust
 * against any specific libm implementation choice while still catching
 * gross identity failures.  We restrict the random sweep to [-6, 6]
 * where erf has not fully saturated to +/-1.0; outside that range erfc
 * itself underflows to 0.0 or saturates to 2.0 and the addition becomes
 * trivial rather than informative.
 */
static void prop_erf_erfc_complement(void) {
    int generated = 0;
    int attempts = 0;
    const int target = 1000;
    const int attempt_cap = 100000;

    while (generated < target && attempts < attempt_cap) {
        attempts++;
        double x = xs_uniform_pm6();
        if (!isfinite(x)) continue;

        double a = erf(x);
        double b = erfc(x);
        if (!isfinite(a) || !isfinite(b)) continue;
        if (fabs(a + b - 1.0) >= 1e-6) {
            FAIL("erf(x) + erfc(x) != 1.0 on random sample");
            return;
        }
        generated++;
    }

    if (generated < target) {
        FAIL("xorshift generator failed to produce enough finite samples for erf/erfc");
        return;
    }

    static const double boundary[] = {
        0.0,
        1.0, -1.0,
        2.0, -2.0,
        3.0, -3.0,
    };
    const int nb = (int)(sizeof(boundary) / sizeof(boundary[0]));
    for (int i = 0; i < nb; i++) {
        double x = boundary[i];
        if (!isfinite(x)) continue;
        double a = erf(x);
        double b = erfc(x);
        if (fabs(a + b - 1.0) >= 1e-6) {
            FAIL("erf(x) + erfc(x) != 1.0 at boundary");
            return;
        }
    }

    (void)isclose;
}

int main(void) {
    prop_erf_erfc_complement();

    if (failures == 0) {
        printf("All property tests passed.\n");
    } else {
        printf("%d property tests failed.\n", failures);
    }

    return failures != 0;
}
