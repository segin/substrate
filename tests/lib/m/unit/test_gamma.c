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

int main(void) {
    test_erf_basic();

    if (failures != 0) {
        printf("FAILURES: %d\n", failures);
    }

    return failures != 0;
}
