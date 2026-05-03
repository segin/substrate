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

int main(void) {
    prop_cosh_sinh_pythagorean();

    if (failures == 0) {
        printf("All property tests passed.\n");
    } else {
        printf("%d property tests failed.\n", failures);
    }

    return failures != 0;
}
