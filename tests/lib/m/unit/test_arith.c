/*
 * test_arith.c - Unit tests for basic arithmetic math functions.
 */

#include <stdio.h>
#if defined(__has_include)
#  if __has_include(<features.h>)
#    include <features.h>
#  endif
#endif
#include <float.h>
#include <math.h>

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
    failures++; \
} while (0)

static int failures = 0;

static int isclose(double a, double b, double tol)
{
    return fabs(a - b) <= tol;
}

static void test_fabs_basic(void)
{
    if (fabs(3.5) != 3.5)
        FAIL("fabs(3.5) != 3.5");
    if (fabs(-3.5) != 3.5)
        FAIL("fabs(-3.5) != 3.5");
    if (fabs(0.0) != 0.0 || signbit(fabs(0.0)))
        FAIL("fabs(0.0) did not return +0.0");
    if (fabs(-0.0) != 0.0 || signbit(fabs(-0.0)))
        FAIL("fabs(-0.0) did not return +0.0");
    if (!isinf(fabs(INFINITY)) || signbit(fabs(INFINITY)))
        FAIL("fabs(INFINITY) did not return +INFINITY");
    if (!isinf(fabs(-INFINITY)) || signbit(fabs(-INFINITY)))
        FAIL("fabs(-INFINITY) did not return +INFINITY");
    if (!isnan(fabs(NAN)))
        FAIL("fabs(NAN) did not return NaN");
}

static void test_fmod_basic(void)
{
    double r = fmod(5.5, 2.0);
    if (!isclose(r, 1.5, 1e-15))
        FAIL("fmod(5.5, 2.0) != 1.5");

    r = fmod(-5.5, 2.0);
    if (!isclose(r, -1.5, 1e-15) || !signbit(r))
        FAIL("fmod(-5.5, 2.0) did not preserve dividend sign");

    r = fmod(0.0, 3.0);
    if (r != 0.0 || signbit(r))
        FAIL("fmod(0.0, 3.0) did not return +0.0");

    if (!isnan(fmod(NAN, 2.0)))
        FAIL("fmod(NAN, 2.0) did not propagate NaN");
    if (!isnan(fmod(1.0, NAN)))
        FAIL("fmod(1.0, NAN) did not propagate NaN");
}

static void test_remainder_basic(void)
{
    double r = remainder(5.0, 2.0);
    if (!isclose(r, 1.0, 1e-15))
        FAIL("remainder(5.0, 2.0) != 1.0");

    r = remainder(7.0, 2.0);
    if (!isclose(r, -1.0, 1e-15))
        FAIL("remainder(7.0, 2.0) did not use ties-to-even quotient");
}

static void test_remquo_basic(void)
{
    int quo = 0;
    double r = remquo(29.0, 3.0, &quo);
    if (!isclose(r, remainder(29.0, 3.0), 1e-15))
        FAIL("remquo(29.0, 3.0) remainder mismatch");
    if ((quo & 0x7) != 2)
        FAIL("remquo(29.0, 3.0) low quotient bits incorrect");

    quo = 0;
    r = remquo(-29.0, 3.0, &quo);
    if (!isclose(r, remainder(-29.0, 3.0), 1e-15))
        FAIL("remquo(-29.0, 3.0) remainder mismatch");
    if (quo >= 0 || ((-quo) & 0x7) != 2)
        FAIL("remquo(-29.0, 3.0) quotient sign or bits incorrect");
}

static void test_fma_rounding_difference(void)
{
    volatile double eps = 0x1p-52;
    volatile double x = 1.0 + eps;
    volatile double y = 1.0 - eps;
    volatile double z = -1.0;
    double naive = (x * y) + z;
    double fused = fma(x, y, z);

    if (naive != 0.0)
        FAIL("naive (x*y)+z did not round to zero in FMA test case");
    if (!(fused < 0.0))
        FAIL("fma(x, y, z) did not preserve the exact negative residual");
    if (fused == naive)
        FAIL("fma(x, y, z) did not differ from naive multiply-add");
}

static void test_fma_special_values(void)
{
    if (!isnan(fma(0.0, INFINITY, NAN)))
        FAIL("fma(0.0, INFINITY, NAN) did not return NaN");
    if (!isnan(fma(INFINITY, 2.0, -INFINITY)))
        FAIL("fma(INFINITY, 2.0, -INFINITY) did not return NaN");
    if (!isinf(fma(INFINITY, 2.0, INFINITY)) || signbit(fma(INFINITY, 2.0, INFINITY)))
        FAIL("fma(INFINITY, 2.0, INFINITY) did not return +INFINITY");
}

static void test_fmax_fmin(void)
{
    double poszero = 0.0;
    double negzero = -0.0;

    if (fmax(NAN, 4.0) != 4.0)
        FAIL("fmax(NAN, 4.0) did not return non-NaN operand");
    if (fmin(4.0, NAN) != 4.0)
        FAIL("fmin(4.0, NAN) did not return non-NaN operand");
    if (signbit(fmax(poszero, negzero)))
        FAIL("fmax(+0.0, -0.0) did not return +0.0");
    if (!signbit(fmin(poszero, negzero)))
        FAIL("fmin(+0.0, -0.0) did not return -0.0");
}

static void test_fdim_basic(void)
{
    if (!isclose(fdim(5.0, 3.0), 2.0, 1e-15))
        FAIL("fdim(5.0, 3.0) != 2.0");
    if (fdim(3.0, 5.0) != 0.0 || signbit(fdim(3.0, 5.0)))
        FAIL("fdim(3.0, 5.0) did not return +0.0");
    if (!isnan(fdim(NAN, 3.0)))
        FAIL("fdim(NAN, 3.0) did not propagate NaN");
}

static void test_c23_maximum_minimum(void)
{
    double poszero = 0.0;
    double negzero = -0.0;

    if (!isnan(fmaximum(NAN, 1.0)))
        FAIL("fmaximum(NAN, 1.0) did not propagate NaN");
    if (!isnan(fminimum(1.0, NAN)))
        FAIL("fminimum(1.0, NAN) did not propagate NaN");
    if (fmax(NAN, 1.0) != 1.0)
        FAIL("fmax(NAN, 1.0) did not ignore NaN");
    if (signbit(fmaximum(poszero, negzero)))
        FAIL("fmaximum(+0.0, -0.0) did not return +0.0");
    if (!signbit(fminimum(poszero, negzero)))
        FAIL("fminimum(+0.0, -0.0) did not return -0.0");
}

int main(void)
{
    test_fabs_basic();
    test_fmod_basic();
    test_remainder_basic();
    test_remquo_basic();
    test_fma_rounding_difference();
    test_fma_special_values();
    test_fmax_fmin();
    test_fdim_basic();
    test_c23_maximum_minimum();

    if (failures != 0) {
        printf("%d arithmetic test(s) failed\n", failures);
        return 1;
    }

    printf("basic arithmetic math tests passed\n");
    return 0;
}