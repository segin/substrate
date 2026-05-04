/*
 * test_gamma.c - Unit tests for error and gamma functions
 *                (erf, erfc, tgamma, lgamma).
 */

#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <float.h>

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
    failures++; \
} while (0)

static int failures = 0;

static int isclose(double a, double b, double tol) {
    return fabs(a - b) < tol;
}

static int isclosef(float a, float b, float tol) {
    return fabsf(a - b) < tol;
}

/*
 * REQ-06-0757: erf() basic values.
 *   erf(+-0) == +-0 (sign preserved; odd function).
 *   erf(1)   ~= 0.8427007929497149 (A&S 7.1.26 max error ~1.5e-7).
 *   erf(-1)  ~= -0.8427007929497149 (odd: erf(-x) = -erf(x)).
 *   erf(+inf) == 1.0 exact (saturating limit).
 *   erf(-inf) == -1.0 exact (saturating limit).
 */
static void test_erf_basic(void) {
    double p0 = erf(+0.0);
    if (p0 != 0.0 || signbit(p0)) FAIL("erf(+0.0)");

    double n0 = erf(-0.0);
    if (n0 != 0.0 || !signbit(n0)) FAIL("erf(-0.0)");

    if (!isclose(erf(1.0),   0.8427007929497149, 1e-6)) FAIL("erf(1.0)");
    if (!isclose(erf(-1.0), -0.8427007929497149, 1e-6)) FAIL("erf(-1.0)");

    if (erf(INFINITY)  !=  1.0) FAIL("erf(+inf)");
    if (erf(-INFINITY) != -1.0) FAIL("erf(-inf)");

    /* Silence unused-helper warnings on first-test-only build. */
    (void)isclose;
    (void)isclosef;
}

/*
 * REQ-06-0758: erfc() basic values and complementarity.
 *   erfc(0)    == 1.0 exact (since erf(0)=0).
 *   erfc(+inf) == 0.0 exact (saturating limit).
 *   erfc(-inf) == 2.0 exact (since erf(-inf)=-1).
 *   erfc(x) + erf(x) ~= 1.0 for all finite x (definitional identity).
 */
static void test_erfc(void) {
    if (erfc(0.0)       != 1.0) FAIL("erfc(0.0)");
    if (erfc(INFINITY)  != 0.0) FAIL("erfc(+inf)");
    if (erfc(-INFINITY) != 2.0) FAIL("erfc(-inf)");

    static const double xs[] = { -1.0, -0.5, 0.5, 1.0, 2.0 };
    for (size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); i++) {
        double x = xs[i];
        if (!isclose(erf(x) + erfc(x), 1.0, 1e-6)) FAIL("erf(x) + erfc(x) != 1");
    }
}

int main(void) {
    test_erf_basic();
    test_erfc();

    if (failures != 0) {
        printf("FAILURES: %d\n", failures);
    }

    return failures != 0;
}
