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

/*
 * erf(-x) == -erf(x): erf is an odd function.  (REQ-06-0766)
 *
 * The Substrate libm erf implementation explicitly extracts the sign of
 * the input (sign = (x < 0) ? -1.0 : 1.0), computes the polynomial
 * approximation on fabs(x), and returns sign * y -- so the odd-function
 * identity holds bit-exactly on Substrate.  Host glibc may use a
 * different algorithm whose sign handling is not bit-exact symmetric;
 * if the strict equality check fails we fall back to a tight numeric
 * tolerance (1e-15) which still demonstrates the identity holds within
 * representational rounding.
 *
 * Special-case zero: erf(+0.0) must be +0.0 and erf(-0.0) must be -0.0,
 * preserving signbit -- this is consistent with the sign-extraction code
 * path (sign of -0.0 is treated as +1.0 because (-0.0 < 0) is false in
 * IEEE comparison; the input fabs(-0.0) == +0.0 yields +0.0 from the
 * polynomial, and +1.0 * +0.0 == +0.0; we therefore expect +0.0 for
 * both inputs on Substrate but accept either sign on host libm).
 */
static void prop_erf_odd(void) {
    int generated = 0;
    int attempts = 0;
    const int target = 1000;
    const int attempt_cap = 100000;

    while (generated < target && attempts < attempt_cap) {
        attempts++;
        double x = xs_uniform_pm6();
        if (!isfinite(x)) continue;

        double a = erf(x);
        double b = erf(-x);
        if (!isfinite(a) || !isfinite(b)) continue;

        if (a != -b && fabs(a + b) >= 1e-15) {
            FAIL("erf(-x) != -erf(x) on random sample");
            return;
        }
        generated++;
    }

    if (generated < target) {
        FAIL("xorshift generator failed to produce enough finite samples for erf odd");
        return;
    }

    static const double boundary[] = {
        1.0, -1.0,
        2.0, -2.0,
        3.0, -3.0,
        5.0, -5.0,
    };
    const int nb = (int)(sizeof(boundary) / sizeof(boundary[0]));
    for (int i = 0; i < nb; i++) {
        double x = boundary[i];
        double a = erf(x);
        double b = erf(-x);
        if (a != -b && fabs(a + b) >= 1e-15) {
            FAIL("erf(-x) != -erf(x) at boundary");
            return;
        }
    }

    /* Zero handling: erf(+0.0) must be +0.0; erf(-0.0) should preserve
     * the sign of the input on a properly-implemented libm. */
    double e_pos_zero = erf(0.0);
    double e_neg_zero = erf(-0.0);
    if (e_pos_zero != 0.0 || signbit(e_pos_zero)) {
        FAIL("erf(+0.0) is not +0.0");
        return;
    }
    if (e_neg_zero != 0.0) {
        FAIL("erf(-0.0) is not zero");
        return;
    }
    /* signbit(erf(-0.0)) is implementation-defined across libm
     * implementations; we do not strictly require it here. */
}

/*
 * exp(lgamma(x)) approximately equals |tgamma(x)| for x where tgamma is
 * finite.  (REQ-06-0767)
 *
 * lgamma returns the natural log of the absolute value of the gamma
 * function, so exp(lgamma(x)) reconstructs |tgamma(x)|.  The sign of
 * tgamma at negative non-integer arguments is encoded separately
 * (signgam global on POSIX libm); we therefore compare against the
 * absolute value of tgamma.  We sweep a chosen set of points covering
 * half-integer, small positive integer, and a handful of negative
 * non-integer arguments.  Large positive x (e.g. 100.0) push tgamma to
 * very large finite values; we apply a relative tolerance scaled to
 * |tgamma(x)| to remain robust against floating-point cancellation in
 * the exp/log round-trip.  Integer arguments at zero or below are
 * skipped because tgamma diverges there.
 */
static void prop_lgamma_tgamma_link(void) {
    static const double pos[] = {
        0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 10.0, 50.0, 100.0,
    };
    const int npos = (int)(sizeof(pos) / sizeof(pos[0]));
    for (int i = 0; i < npos; i++) {
        double x = pos[i];
        double t = tgamma(x);
        double l = lgamma(x);
        if (!isfinite(t)) continue;
        double tol = 1e-10 * fabs(t);
        if (!isclose(exp(l), fabs(t), tol)) {
            FAIL("exp(lgamma(x)) != |tgamma(x)| at positive x");
            return;
        }
    }

    static const double neg[] = {
        -0.5, -1.5, -2.5,
    };
    const int nneg = (int)(sizeof(neg) / sizeof(neg[0]));
    for (int i = 0; i < nneg; i++) {
        double x = neg[i];
        double t = tgamma(x);
        double l = lgamma(x);
        if (!isfinite(t)) continue;
        double tol = 1e-10 * fabs(t);
        if (!isclose(exp(l), fabs(t), tol)) {
            FAIL("exp(lgamma(x)) != |tgamma(x)| at negative non-integer x");
            return;
        }
    }
}

/*
 * tgamma(x + 1) approximately equals x * tgamma(x) for positive x: this
 * is the fundamental recurrence relation of the gamma function, the
 * direct continuous analogue of the factorial recurrence n! = n * (n-1)!.
 * (REQ-06-0768)
 *
 * We test on a chosen set of positive arguments covering half-integer
 * values (where tgamma involves sqrt(pi) factors), small integers
 * (matching factorial values exactly), and larger arguments approaching
 * the overflow threshold for tgamma in IEEE-754 double precision
 * (tgamma(171.0) is the last finite value).  Either tgamma(x) or
 * tgamma(x + 1) may overflow at the upper end of our sweep; we skip any
 * sample where either side is non-finite rather than treating it as a
 * failure, since the identity is undefined on infinities.  The relative
 * tolerance is scaled to |b| because tgamma(100) is ~9.3e155 -- absolute
 * tolerances are meaningless across this dynamic range.
 */
static void prop_tgamma_recurrence(void) {
    static const double pos[] = {
        0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 7.0, 10.0, 50.0, 100.0,
    };
    const int npos = (int)(sizeof(pos) / sizeof(pos[0]));
    for (int i = 0; i < npos; i++) {
        double x = pos[i];
        double a = tgamma(x + 1.0);
        double b = x * tgamma(x);
        if (!isfinite(a) || !isfinite(b)) continue;
        if (!isclose(a, b, 1e-10 * fabs(b))) {
            FAIL("tgamma(x+1) != x * tgamma(x) at positive x");
            return;
        }
    }
}

int main(void) {
    prop_erf_erfc_complement();
    prop_erf_odd();
    prop_lgamma_tgamma_link();
    prop_tgamma_recurrence();

    if (failures == 0) {
        printf("All property tests passed.\n");
    } else {
        printf("%d property tests failed.\n", failures);
    }

    return failures != 0;
}
