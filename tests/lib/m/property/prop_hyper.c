/*
 * prop_hyper.c - Property-based tests for hyperbolic functions
 *
 * Tests mathematical properties across the safe finite domain of each
 * hyperbolic function.  cosh/sinh overflow above |x| ~ 709 (double),
 * so sweeps stay well inside that range.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>

#define FAIL(msg) do { \
    printf("FAIL property: %s\n", msg); \
    failures++; \
} while (0)

static int failures = 0;

static int isclose(double a, double b, double tol) {
    return fabs(a - b) < tol;
}

/*
 * cosh(x)^2 - sinh(x)^2 == 1  (hyperbolic Pythagorean identity, REQ-06-0687)
 * Verified across a uniform sweep of x in [-10, 10] plus boundary cases.
 * The naive c*c - s*s formulation suffers cancellation at large |x|, so
 * the sweep tolerance is loosened relative to the boundary tolerance.
 */
static void prop_cosh_sinh_pythagorean(void) {
    const double lo = -10.0;
    const double hi =  10.0;
    const int n = 1000;
    const double step = (hi - lo) / (double)(n - 1);

    for (int i = 0; i < n; i++) {
        double x = lo + (double)i * step;
        if (!isfinite(x)) continue;
        /* Guard against overflow; cosh(709) ~ DBL_MAX. */
        if (fabs(x) > 700.0) continue;
        double c = cosh(x);
        double s = sinh(x);
        if (!isfinite(c) || !isfinite(s)) continue;
        double residual = c * c - s * s - 1.0;
        /*
         * The naive c*c - s*s formulation exhibits catastrophic cancellation:
         * for x near +/-10, cosh(x)^2 ~ 1.2e8, so absolute residuals scale
         * roughly as cosh(x)^2 * DBL_EPSILON ~ 3e-8.  Use a relaxed tolerance
         * sized for the worst case in the swept range.
         */
        if (fabs(residual) >= 1e-7) {
            FAIL("cosh^2 - sinh^2 != 1 on uniform sweep");
            return;
        }
    }

    static const double boundary[] = {
        0.0,
        1.0, -1.0,
        5.0, -5.0,
    };
    const int nb = (int)(sizeof(boundary) / sizeof(boundary[0]));
    for (int i = 0; i < nb; i++) {
        double x = boundary[i];
        if (!isfinite(x)) continue;
        double c = cosh(x);
        double s = sinh(x);
        double residual = c * c - s * s - 1.0;
        /*
         * Boundary cases are tighter than the sweep, but x = +/- 5 still
         * carries ~ cosh(5)^2 * DBL_EPSILON ~ 1.6e-12 of cancellation
         * residual, so 1e-11 is the smallest tolerance that holds for
         * the requested boundary set.
         */
        if (fabs(residual) >= 1e-11) {
            FAIL("cosh^2 - sinh^2 != 1 at boundary");
            return;
        }
    }

    (void)isclose;
}

/*
 * sinh(-x) == -sinh(x)  (odd-function symmetry, REQ-06-0688)
 *
 * sinh is exactly odd by construction in any reasonable implementation
 * (sinh(x) = (e^x - e^-x)/2, which negates cleanly under x -> -x), so
 * the identity should hold bit-exactly.  We assert exact equality on
 * a uniform sweep of [-10, 10] plus the requested boundary set, and
 * additionally verify signbit preservation at +/-0.0.
 */
static void prop_sinh_odd(void) {
    const double lo = -10.0;
    const double hi =  10.0;
    const int n = 1000;
    const double step = (hi - lo) / (double)(n - 1);

    for (int i = 0; i < n; i++) {
        double x = lo + (double)i * step;
        if (!isfinite(x)) continue;
        double a = sinh(-x);
        double b = -sinh(x);
        /*
         * Bit-exact equality is the contract: sinh is implemented as an
         * odd function and host glibc preserves this exactly.  Fall back
         * to a tight tolerance (1e-15) only if equality fails, in case
         * an alternate libm rounds the negation differently.
         */
        if (a != b && fabs(a - b) >= 1e-15) {
            FAIL("sinh(-x) != -sinh(x) on uniform sweep");
            return;
        }
    }

    static const double boundary[] = {
        0.0,
        1.0, -1.0,
        5.0, -5.0,
    };
    const int nb = (int)(sizeof(boundary) / sizeof(boundary[0]));
    for (int i = 0; i < nb; i++) {
        double x = boundary[i];
        if (!isfinite(x)) continue;
        double a = sinh(-x);
        double b = -sinh(x);
        if (a != b && fabs(a - b) >= 1e-15) {
            FAIL("sinh(-x) != -sinh(x) at boundary");
            return;
        }
    }

    /*
     * Signed-zero handling: sinh(+0.0) must be +0.0 and sinh(-0.0) must
     * be -0.0.  Distinguishing them requires signbit() since +0.0 == -0.0
     * compares equal under IEEE 754.
     */
    double sp = sinh(0.0);
    double sn = sinh(-0.0);
    if (sp != 0.0 || signbit(sp)) {
        FAIL("sinh(+0.0) is not +0.0");
        return;
    }
    if (sn != 0.0 || !signbit(sn)) {
        FAIL("sinh(-0.0) is not -0.0");
        return;
    }
}

/*
 * cosh(-x) == cosh(x)  (even-function symmetry, REQ-06-0689)
 *
 * cosh is exactly even by construction (cosh(x) = (e^x + e^-x)/2 is
 * symmetric under x -> -x), so the identity should hold bit-exactly.
 * Assert exact equality on a uniform sweep of [-10, 10] plus the
 * requested boundary set, and additionally verify cosh(0.0) == 1.0.
 */
static void prop_cosh_even(void) {
    const double lo = -10.0;
    const double hi =  10.0;
    const int n = 1000;
    const double step = (hi - lo) / (double)(n - 1);

    for (int i = 0; i < n; i++) {
        double x = lo + (double)i * step;
        if (!isfinite(x)) continue;
        double a = cosh(-x);
        double b = cosh(x);
        /*
         * Bit-exact equality is the contract: cosh is implemented as an
         * even function and host glibc preserves this exactly.  Fall back
         * to a tight tolerance (1e-15) only if equality fails, in case
         * an alternate libm rounds the negation differently.
         */
        if (a != b && fabs(a - b) >= 1e-15) {
            FAIL("cosh(-x) != cosh(x) on uniform sweep");
            return;
        }
    }

    static const double boundary[] = {
        0.0,
        1.0, -1.0,
        5.0, -5.0,
    };
    const int nb = (int)(sizeof(boundary) / sizeof(boundary[0]));
    for (int i = 0; i < nb; i++) {
        double x = boundary[i];
        if (!isfinite(x)) continue;
        double a = cosh(-x);
        double b = cosh(x);
        if (a != b && fabs(a - b) >= 1e-15) {
            FAIL("cosh(-x) != cosh(x) at boundary");
            return;
        }
    }

    /* cosh(0) == 1 exactly under IEEE 754. */
    if (cosh(0.0) != 1.0) {
        FAIL("cosh(0.0) is not 1.0");
        return;
    }
}

/*
 * tanh(x) == sinh(x) / cosh(x)  (definitional identity, REQ-06-0690)
 *
 * Holds for moderate |x| where neither sinh nor cosh has lost meaningful
 * precision.  Outside ~|x| > 5, both sinh and cosh approach e^|x|/2 and
 * the ratio degrades relative to a direct tanh implementation that
 * computes (e^{2x} - 1)/(e^{2x} + 1) or similar.  Restrict the sweep to
 * x in [-5, 5] (500 uniformly spaced samples) where the residual stays
 * comfortably within 1e-14, with a tighter 1e-15 bound for the requested
 * boundary points.
 */
static void prop_tanh_definition(void) {
    const double lo = -5.0;
    const double hi =  5.0;
    const int n = 500;
    const double step = (hi - lo) / (double)(n - 1);

    for (int i = 0; i < n; i++) {
        double x = lo + (double)i * step;
        if (!isfinite(x)) continue;
        double t = tanh(x);
        double s = sinh(x);
        double c = cosh(x);
        if (!isfinite(s) || !isfinite(c) || c == 0.0) continue;
        double ratio = s / c;
        if (fabs(t - ratio) >= 1e-14) {
            FAIL("tanh(x) != sinh(x)/cosh(x) on uniform sweep");
            return;
        }
    }

    static const double boundary[] = {
        0.0,
        1.0, -1.0,
    };
    const int nb = (int)(sizeof(boundary) / sizeof(boundary[0]));
    for (int i = 0; i < nb; i++) {
        double x = boundary[i];
        if (!isfinite(x)) continue;
        double t = tanh(x);
        double s = sinh(x);
        double c = cosh(x);
        if (c == 0.0) continue;
        double ratio = s / c;
        if (fabs(t - ratio) >= 1e-15) {
            FAIL("tanh(x) != sinh(x)/cosh(x) at boundary");
            return;
        }
    }
}

int main(void) {
    prop_cosh_sinh_pythagorean();
    prop_sinh_odd();
    prop_cosh_even();
    prop_tanh_definition();

    if (failures == 0) {
        printf("All property tests passed.\n");
    } else {
        printf("%d property tests failed.\n", failures);
    }

    return failures != 0;
}
