/*
 * test_hyper.c - Unit tests for hyperbolic functions (sinh, cosh, tanh).
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
 * REQ-06-0679: bit-exact zero values.
 *   sinh(+-0) == +-0 (sign preserved)
 *   cosh(+-0) == 1.0
 *   tanh(+-0) == +-0 (sign preserved)
 */
static void test_hyper_zero(void) {
    double sp0 = sinh(+0.0);
    if (sp0 != 0.0 || signbit(sp0)) FAIL("sinh(+0.0)");

    double sn0 = sinh(-0.0);
    if (sn0 != 0.0 || !signbit(sn0)) FAIL("sinh(-0.0)");

    if (cosh(+0.0) != 1.0) FAIL("cosh(+0.0)");
    if (cosh(-0.0) != 1.0) FAIL("cosh(-0.0)");

    double tp0 = tanh(+0.0);
    if (tp0 != 0.0 || signbit(tp0)) FAIL("tanh(+0.0)");

    double tn0 = tanh(-0.0);
    if (tn0 != 0.0 || !signbit(tn0)) FAIL("tanh(-0.0)");

    /* Silence unused-helper warnings on first-test-only build. */
    (void)isclose;
    (void)isclosef;
}

int main(void) {
    test_hyper_zero();

    if (failures != 0) {
        printf("FAILURES: %d\n", failures);
    }

    return failures != 0;
}
