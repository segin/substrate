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

/*
 * REQ-06-0680: hyperbolic functions at x = 1.0 (and -1.0 for parity).
 *   sinh(1) ≈ 1.1752011936438014  (odd: sinh(-x) = -sinh(x))
 *   cosh(1) ≈ 1.5430806348152437  (even: cosh(-x) =  cosh(x))
 *   tanh(1) ≈ 0.7615941559557649  (odd: tanh(-x) = -tanh(x))
 */
static void test_hyper_one(void) {
    if (!isclose(sinh(1.0), 1.1752011936438014, 1e-14)) FAIL("sinh(1.0)");
    if (!isclose(cosh(1.0), 1.5430806348152437, 1e-14)) FAIL("cosh(1.0)");
    if (!isclose(tanh(1.0), 0.7615941559557649, 1e-14)) FAIL("tanh(1.0)");

    if (!isclose(sinh(-1.0), -1.1752011936438014, 1e-14)) FAIL("sinh(-1.0)");
    if (!isclose(cosh(-1.0),  1.5430806348152437, 1e-14)) FAIL("cosh(-1.0)");
    if (!isclose(tanh(-1.0), -0.7615941559557649, 1e-14)) FAIL("tanh(-1.0)");
}

/*
 * REQ-06-0681: hyperbolic functions at +-infinity.
 *   sinh(+inf) = +inf, sinh(-inf) = -inf  (odd)
 *   cosh(+-inf) = +inf                    (even)
 *   tanh(+inf) =  1.0, tanh(-inf) = -1.0  (saturating, exact)
 */
static void test_hyper_inf(void) {
    double sp = sinh(INFINITY);
    if (!isinf(sp) || !(sp > 0.0)) FAIL("sinh(+inf)");

    double sn = sinh(-INFINITY);
    if (!isinf(sn) || !(sn < 0.0)) FAIL("sinh(-inf)");

    double cp = cosh(INFINITY);
    if (!isinf(cp) || !(cp > 0.0)) FAIL("cosh(+inf)");

    double cn = cosh(-INFINITY);
    if (!isinf(cn) || !(cn > 0.0)) FAIL("cosh(-inf)");

    if (tanh(INFINITY)  !=  1.0) FAIL("tanh(+inf)");
    if (tanh(-INFINITY) != -1.0) FAIL("tanh(-inf)");
}

/*
 * REQ-06-0682: inverse hyperbolic functions at the identity points.
 *   asinh(+-0) == +-0 (sign preserved; odd function)
 *   acosh(1)   == 0.0 exact (left endpoint of domain)
 *   atanh(+-0) == +-0 (sign preserved; odd function)
 */
static void test_inv_hyper_zero(void) {
    double ap0 = asinh(+0.0);
    if (ap0 != 0.0 || signbit(ap0)) FAIL("asinh(+0.0)");

    double an0 = asinh(-0.0);
    if (an0 != 0.0 || !signbit(an0)) FAIL("asinh(-0.0)");

    if (acosh(1.0) != 0.0) FAIL("acosh(1.0)");

    double tp0 = atanh(+0.0);
    if (tp0 != 0.0 || signbit(tp0)) FAIL("atanh(+0.0)");

    double tn0 = atanh(-0.0);
    if (tn0 != 0.0 || !signbit(tn0)) FAIL("atanh(-0.0)");
}

int main(void) {
    test_hyper_zero();
    test_hyper_one();
    test_hyper_inf();
    test_inv_hyper_zero();

    if (failures != 0) {
        printf("FAILURES: %d\n", failures);
    }

    return failures != 0;
}
