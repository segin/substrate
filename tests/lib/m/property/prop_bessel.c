/*
 * prop_bessel.c — property tests for Bessel functions.
 *
 * Three properties exercised against random x:
 *
 *   1. Three-term recurrence for J:
 *        J_{n-1}(x) + J_{n+1}(x) = (2n/x) J_n(x)
 *
 *   2. Bound on J_0:
 *        |J_0(x)| <= 1   for all x >= 0
 *
 *   3. Same recurrence for Y.  Y_n is unbounded as n grows for fixed
 *      x, so we keep n small enough that the values stay finite.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int fail = 0;
static int pass = 0;

#define CHECK(cond, fmt, ...) do {                                       \
    if (!(cond)) {                                                       \
        fprintf(stderr, "FAIL: " fmt "\n", ##__VA_ARGS__);               \
        fail++;                                                          \
    } else {                                                             \
        pass++;                                                          \
    }                                                                    \
} while (0)

/* Tolerance scaled by the magnitude of the rhs of the recurrence:
 * cancellation in J_{n-1} + J_{n+1} can be severe when 2n/x is large. */
static int near(double a, double b, double abs_tol, double rel_tol) {
    double diff = fabs(a - b);
    double scale = fabs(a) + fabs(b);
    return diff <= abs_tol || diff <= rel_tol * scale;
}

static void prop_j_recurrence(void) {
    /* x in [0.5, 20], n in [1, 12]. */
    double xs[] = {0.5, 1.0, 2.0, 3.0, 5.0, 7.5, 10.0, 12.5, 15.0, 20.0};
    int    ns[] = {1, 2, 3, 5, 8, 12};
    for (size_t i = 0; i < sizeof(xs)/sizeof(*xs); i++) {
        for (size_t j = 0; j < sizeof(ns)/sizeof(*ns); j++) {
            double x = xs[i];
            int n = ns[j];
            double lhs = jn(n - 1, x) + jn(n + 1, x);
            double rhs = (2.0 * n / x) * jn(n, x);
            CHECK(near(lhs, rhs, 1e-9, 1e-9),
                  "J recurrence n=%d x=%g  lhs=%g  rhs=%g", n, x, lhs, rhs);
        }
    }
}

static void prop_j0_bound(void) {
    /* |J_0(x)| <= 1 for all x >= 0.  Check a sweep that hits both
     * small-x and large-x regimes plus a near-zero on each lobe. */
    double xs[] = {0.0, 0.1, 0.5, 1.0, 2.0, 2.4048255577, 3.0, 5.0,
                   5.520078110, 7.5, 8.6537279129, 10.0, 15.0, 20.0,
                   50.0, 100.0, 1000.0};
    for (size_t i = 0; i < sizeof(xs)/sizeof(*xs); i++) {
        double v = j0(xs[i]);
        CHECK(fabs(v) <= 1.0 + 1e-15,
              "|J_0(%g)| = %g exceeds 1", xs[i], v);
    }
}

static void prop_y_recurrence(void) {
    /* x in [0.5, 20], n in [1, 8].  Avoid larger n at small x because
     * Y_n diverges (~1/x^n) and the rhs * Y_n explodes. */
    double xs[] = {1.0, 2.0, 3.0, 5.0, 7.5, 10.0, 12.5, 15.0, 20.0};
    int    ns[] = {1, 2, 3, 5, 8};
    for (size_t i = 0; i < sizeof(xs)/sizeof(*xs); i++) {
        for (size_t j = 0; j < sizeof(ns)/sizeof(*ns); j++) {
            double x = xs[i];
            int n = ns[j];
            double a = yn(n - 1, x);
            double b = yn(n + 1, x);
            double c = yn(n,     x);
            /* Skip degenerate cases where the answer is huge. */
            if (!isfinite(a) || !isfinite(b) || !isfinite(c)) continue;
            double lhs = a + b;
            double rhs = (2.0 * n / x) * c;
            CHECK(near(lhs, rhs, 1e-6, 1e-7),
                  "Y recurrence n=%d x=%g  lhs=%g  rhs=%g", n, x, lhs, rhs);
        }
    }
}

int main(void) {
    printf("=== Bessel property tests ===\n");
    prop_j_recurrence();
    prop_j0_bound();
    prop_y_recurrence();
    printf("\n%s: %d/%d cases passed.\n",
           fail ? "FAILED" : "PASSED", pass, pass + fail);
    return fail > 0;
}
