/*
 * prop_explog.c — property tests for exponential, logarithmic, and
 * power functions.  Covers REQ-06-0621..0628.
 */

#include <stdio.h>
#include <math.h>
#include <float.h>
#include <stdint.h>
#include <stddef.h>

#ifndef M_E
#define M_E  2.71828182845904523536
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_failures = 0;

#define PROP(cond, name) do { \
    if (!(cond)) { \
        printf("  FAIL property: %s (line %d)\n", (name), __LINE__); \
        g_failures++; \
    } \
} while (0)

static int approx_eq(double a, double b, int ulps) {
    if (a == b) return 1;
    if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
    if (isinf(a) || isinf(b)) return 0;
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    double tol = ulps * scale * DBL_EPSILON;
    return diff <= tol;
}

/* REQ-06-0621: exp(log(x)) ≈ x for positive finite x. */
static void prop_exp_log_inverse(void) {
    static const double vals[] = {
        1.0, 2.0, 0.5, 10.0, 0.1, 100.0, 1e-3, 1e3, 1e6, 1e-6,
        M_E, M_PI, 42.0, 1234.567
    };
    for (size_t i = 0; i < sizeof(vals)/sizeof(vals[0]); i++) {
        double x = vals[i];
        double r = exp(log(x));
        PROP(approx_eq(r, x, 32), "exp(log(x)) ≈ x");
    }
}

/* REQ-06-0622: log(exp(x)) ≈ x for moderate x. */
static void prop_log_exp_inverse(void) {
    static const double vals[] = {
        0.0, 1.0, -1.0, 5.0, -5.0, 0.1, -0.1,
        20.0, -20.0, 100.0, -100.0,  /* well inside range */
        M_E
    };
    for (size_t i = 0; i < sizeof(vals)/sizeof(vals[0]); i++) {
        double x = vals[i];
        double r = log(exp(x));
        /* log(exp(0)) is the only one that's exactly equal; others
         * pick up rounding so allow a few ULPs absolute. */
        if (x == 0.0) {
            PROP(r == 0.0, "log(exp(0)) == 0");
        } else {
            PROP(fabs(r - x) <= 32 * fabs(x) * DBL_EPSILON ||
                 fabs(r - x) < 1e-13,
                                       "log(exp(x)) ≈ x");
        }
    }
}

/* REQ-06-0623: exp2(log2(x)) ≈ x for positive finite x. */
static void prop_exp2_log2_inverse(void) {
    static const double vals[] = {
        1.0, 2.0, 4.0, 0.5, 0.25, 1024.0, 1e10, 1e-10, 3.0, 7.0
    };
    for (size_t i = 0; i < sizeof(vals)/sizeof(vals[0]); i++) {
        double x = vals[i];
        double r = exp2(log2(x));
        PROP(approx_eq(r, x, 32), "exp2(log2(x)) ≈ x");
    }
}

/* REQ-06-0624: sqrt(x) * sqrt(x) ≈ x for positive x. */
static void prop_sqrt_squared(void) {
    static const double vals[] = {
        1.0, 2.0, 4.0, 0.25, 1e10, 1e-10, 100.0, 0.5, 1e100, 1e-100
    };
    for (size_t i = 0; i < sizeof(vals)/sizeof(vals[0]); i++) {
        double x = vals[i];
        double s = sqrt(x);
        double r = s * s;
        PROP(approx_eq(r, x, 4), "sqrt(x)^2 ≈ x");
    }
}

/* REQ-06-0625: hypot(x,y) >= |x| and hypot(x,y) >= |y|. */
static void prop_hypot_dominates(void) {
    static const double xs[] = { 1.0, 3.0, 1e10, 1e-10, 0.0, -7.0 };
    static const double ys[] = { 2.0, 4.0, 1e10, 1e-10, 5.0,  3.0 };
    for (size_t i = 0; i < sizeof(xs)/sizeof(xs[0]); i++) {
        double x = xs[i], y = ys[i];
        double h = hypot(x, y);
        PROP(h >= fabs(x) || (h == fabs(x) && y == 0.0),
                                       "hypot(x,y) >= |x|");
        PROP(h >= fabs(y) || (h == fabs(y) && x == 0.0),
                                       "hypot(x,y) >= |y|");
    }
}

/* REQ-06-0626: pow(x, 1.0) == x for all x. */
static void prop_pow_identity_exponent(void) {
    static const double vals[] = {
        0.0, 1.0, -1.0, 2.0, -2.0, 0.5, -0.5, 1e10, 1e-10,
        M_E, M_PI
    };
    for (size_t i = 0; i < sizeof(vals)/sizeof(vals[0]); i++) {
        double x = vals[i];
        double r = pow(x, 1.0);
        if (x == 0.0) {
            PROP(r == 0.0, "pow(0, 1) == 0");
        } else if (x < 0.0) {
            /* pow with negative base and integer exponent: we
             * accept the special-case path that returns x. */
            PROP(approx_eq(r, x, 16), "pow(x, 1) ≈ x");
        } else {
            PROP(approx_eq(r, x, 16), "pow(x, 1) ≈ x");
        }
    }
    /* pow(+/-inf, 1) and pow(NaN, 1) — accept whatever C99 says,
     * which is that result equals x. */
    PROP(pow(INFINITY, 1.0) == INFINITY, "pow(+inf, 1) == +inf");
    PROP(pow(-INFINITY, 1.0) == -INFINITY, "pow(-inf, 1) == -inf");
    PROP(isnan(pow(NAN, 1.0)), "pow(NaN, 1) == NaN");
}

/* REQ-06-0627: expm1(x) + 1 ≈ exp(x).  This isn't equality —
 * expm1 is *more* accurate near 0 — so we just check the two
 * agree to within a few ULPs of exp(x). */
static void prop_expm1_vs_exp(void) {
    static const double vals[] = {
        0.0, 0.1, -0.1, 1.0, -1.0, 0.5, -0.5, 10.0, -10.0, 1e-6
    };
    for (size_t i = 0; i < sizeof(vals)/sizeof(vals[0]); i++) {
        double x = vals[i];
        double a = expm1(x) + 1.0;
        double b = exp(x);
        PROP(approx_eq(a, b, 32), "expm1(x) + 1 ≈ exp(x)");
    }
}

/* REQ-06-0628: log1p(expm1(x)) ≈ x for moderate x. */
static void prop_log1p_expm1_inverse(void) {
    static const double vals[] = {
        0.0, 0.1, -0.1, 1.0, -0.5, 0.5, 5.0, -5.0, 1e-8, -1e-8
    };
    for (size_t i = 0; i < sizeof(vals)/sizeof(vals[0]); i++) {
        double x = vals[i];
        double r = log1p(expm1(x));
        if (x == 0.0) {
            PROP(r == 0.0, "log1p(expm1(0)) == 0");
        } else {
            PROP(fabs(r - x) <= 32 * fabs(x) * DBL_EPSILON ||
                 fabs(r - x) < 1e-12,
                                       "log1p(expm1(x)) ≈ x");
        }
    }
}

int main(void) {
    printf("prop_explog: starting\n");
    prop_exp_log_inverse();
    prop_log_exp_inverse();
    prop_exp2_log2_inverse();
    prop_sqrt_squared();
    prop_hypot_dominates();
    prop_pow_identity_exponent();
    prop_expm1_vs_exp();
    prop_log1p_expm1_inverse();
    if (g_failures == 0) {
        printf("prop_explog: PASS\n");
        return 0;
    }
    printf("prop_explog: FAIL (%d failures)\n", g_failures);
    return 1;
}
