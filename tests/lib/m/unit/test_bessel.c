/*
 * test_bessel.c - Unit tests for Bessel functions
 *
 * Tests j0, j1, jn, y0, y1, yn and their float/long double variants.
 */

#include <stdio.h>
#include <math.h>
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

static void test_j0(void) {
    if (!isclose(j0(0.0), 1.0, 1e-15)) FAIL("j0(0) == 1");
    if (!isclose(j0(1.0), 0.7651976866, 1e-9)) FAIL("j0(1)");
    if (!isclose(j0(2.0), 0.2238907791, 1e-9)) FAIL("j0(2)");
    if (!isclose(j0(5.0), -0.1775967713, 1e-9)) FAIL("j0(5)");
    double z1 = j0(2.4048255577);
    if (fabs(z1) > 1e-6) FAIL("j0(first zero ~2.4048) ~= 0");
    if (!isnan(j0(NAN))) FAIL("j0(NaN)");
    if (!isclose(j0(INFINITY), 0.0, 1e-15)) FAIL("j0(+inf)");
    if (!isclose(j0(-1.0), 0.7651976866, 1e-9)) FAIL("j0(-1) == j0(1)");
}

static void test_j1(void) {
    if (!isclose(j1(0.0), 0.0, 1e-15)) FAIL("j1(0) == 0");
    if (!isclose(j1(1.0), 0.4400505857, 1e-9)) FAIL("j1(1)");
    if (!isclose(j1(2.0), 0.5767248078, 1e-9)) FAIL("j1(2)");
    if (!isclose(j1(5.0), -0.3275791379, 1e-9)) FAIL("j1(5)");
    double z2 = j1(3.8317059702);
    if (fabs(z2) > 1e-6) FAIL("j1(first zero ~3.8317) ~= 0");
    if (!isnan(j1(NAN))) FAIL("j1(NaN)");
    if (!isclose(j1(INFINITY), 0.0, 1e-15)) FAIL("j1(+inf)");
    if (!isclose(j1(-1.0), -0.4400505857, 1e-9)) FAIL("j1(-1) == -j1(1)");
}

static void test_jn(void) {
    if (!isclose(jn(0, 1.0), j0(1.0), 1e-14)) FAIL("jn(0,x) == j0(x)");
    if (!isclose(jn(1, 1.0), j1(1.0), 1e-14)) FAIL("jn(1,x) == j1(x)");
    if (!isclose(jn(0, 0.0), 1.0, 1e-15)) FAIL("jn(0, 0) == 1");
    if (!isclose(jn(1, 0.0), 0.0, 1e-15)) FAIL("jn(1, 0) == 0");
    if (!isclose(jn(5, 1.0), 0.0002497577, 1e-7)) FAIL("jn(5, 1)");
    /* j_10(5) reference value from BSD libm / scipy. */
    if (!isclose(jn(10, 5.0), 0.0014678026473104, 1e-12)) FAIL("jn(10, 5)");
    if (!isnan(jn(1, NAN))) FAIL("jn(NaN)");
    if (!isclose(jn(5, INFINITY), 0.0, 1e-15)) FAIL("jn(n, +inf)");
    if (!isclose(jn(-1, 1.0), -j1(1.0), 1e-14)) FAIL("jn(-1,x) == -j1(x)");
    /* j_{-n}(x) = (-1)^n j_n(x), so j_{-2}(x) = j_2(x), NOT j_0(x). */
    if (!isclose(jn(-2, 1.0), jn(2, 1.0), 1e-14)) FAIL("jn(-2,x) == jn(2,x)");
}

static void test_y0(void) {
    if (!isinf(y0(0.0))) FAIL("y0(0) == -inf");
    if (!isinf(y0(-0.0))) FAIL("y0(-0) == -inf");
    if (isnan(y0(0.1)) || isinf(y0(0.1))) FAIL("y0(0.1) finite");
    if (!isclose(y0(1.0), 0.0882569642, 1e-8)) FAIL("y0(1)");
    if (!isclose(y0(2.0), 0.5103756726, 1e-8)) FAIL("y0(2)");
    if (!isclose(y0(5.0), -0.3085176250, 1e-8)) FAIL("y0(5)");
    /* y0's first positive zero is 0.8935776, NOT 2.4048 (that's j0's). */
    double z3 = y0(0.8935776);
    if (fabs(z3) > 1e-6) FAIL("y0(first zero ~0.8936) ~= 0");
    if (!isnan(y0(-1.0))) FAIL("y0(-1) == NaN");
    if (!isnan(y0(NAN))) FAIL("y0(NaN)");
    if (!isclose(y0(INFINITY), 0.0, 1e-15)) FAIL("y0(+inf)");
}

static void test_y1(void) {
    if (!isinf(y1(0.0))) FAIL("y1(0) == -inf");
    if (!isinf(y1(-0.0))) FAIL("y1(-0) == -inf");
    if (isnan(y1(0.1)) || isinf(y1(0.1))) FAIL("y1(0.1) finite");
    if (!isclose(y1(1.0), -0.7812128213, 1e-8)) FAIL("y1(1)");
    /* y_1 reference values from BSD libm / scipy. */
    if (!isclose(y1(2.0), -0.1070324315, 1e-8)) FAIL("y1(2)");
    if (!isclose(y1(5.0),  0.1478631434, 1e-8)) FAIL("y1(5)");
    /* y1's first positive zero is 2.1971413, NOT 3.8317 (that's j1's). */
    double z4 = y1(2.1971413);
    if (fabs(z4) > 1e-6) FAIL("y1(first zero ~2.1971) ~= 0");
    if (!isnan(y1(-1.0))) FAIL("y1(-1) == NaN");
    if (!isnan(y1(NAN))) FAIL("y1(NaN)");
    if (!isclose(y1(INFINITY), 0.0, 1e-15)) FAIL("y1(+inf)");
}

static void test_yn(void) {
    if (!isclose(yn(0, 1.0), y0(1.0), 1e-14)) FAIL("yn(0,x) == y0(x)");
    if (!isclose(yn(1, 1.0), y1(1.0), 1e-14)) FAIL("yn(1,x) == y1(x)");
    if (!isinf(yn(2, 0.0))) FAIL("yn(2, 0) == -inf");
    /* y_5(1) is large and negative — y_n blows up as x → 0 for n>0. */
    if (!isclose(yn(5, 1.0), -2.604058666e+02, 1.0)) FAIL("yn(5, 1)");
    if (!isclose(yn(5, 5.0), -0.4536948225, 1e-4)) FAIL("yn(5, 5)");
    /* Domain: y_n(x<0) is NaN for every n (incl. 0). */
    if (!isnan(yn(0, -1.0))) FAIL("yn(0, -1) == NaN");
    if (!isnan(yn(2, -1.0))) FAIL("yn(n, -1) == NaN");
    if (!isnan(yn(1, NAN))) FAIL("yn(NaN)");
    if (!isclose(yn(3, INFINITY), 0.0, 1e-15)) FAIL("yn(n, +inf)");
}

static void test_float_variants(void) {
    if (!isclosef(j0f(0.0f), 1.0f, 1e-6f)) FAIL("j0f(0)");
    if (!isclosef(j1f(0.0f), 0.0f, 1e-6f)) FAIL("j1f(0)");
    if (!isclosef(jnf(0, 1.0f), j0f(1.0f), 1e-5f)) FAIL("jnf(0,x)==j0f(x)");
    if (!isclosef(jnf(1, 1.0f), j1f(1.0f), 1e-5f)) FAIL("jnf(1,x)==j1f(x)");
    if (!(isinf(y0f(0.0f)) && signbit(y0f(0.0f)))) FAIL("y0f(0) == -inf");
    if (!(isinf(y1f(0.0f)) && signbit(y1f(0.0f)))) FAIL("y1f(0) == -inf");
    if (!isnan(y0f(-1.0f))) FAIL("y0f(-1) == NaN");
    if (!isnan(y1f(-1.0f))) FAIL("y1f(-1) == NaN");
    if (!isclosef(ynf(0, 1.0f), y0f(1.0f), 1e-4f)) FAIL("ynf(0,x)==y0f(x)");
}

static void test_nan_inf(void) {
    if (!isnan(j0(NAN))) FAIL("j0(NaN)");
    if (!isnan(j1(NAN))) FAIL("j1(NaN)");
    if (!isnan(jn(3, NAN))) FAIL("jn(NaN)");
    if (!isnan(y0(NAN))) FAIL("y0(NaN)");
    if (!isnan(y1(NAN))) FAIL("y1(NaN)");
    if (!isnan(yn(3, NAN))) FAIL("yn(NaN)");
    if (!isclose(j0(INFINITY), 0.0, 1e-15)) FAIL("j0(+inf)");
    if (!isclose(j1(INFINITY), 0.0, 1e-15)) FAIL("j1(+inf)");
    if (!isclose(jn(5, INFINITY), 0.0, 1e-15)) FAIL("jn(+inf)");
    if (!isclose(y0(INFINITY), 0.0, 1e-15)) FAIL("y0(+inf)");
}

int main(void) {
    printf("=== Bessel function unit tests ===\n\n");

    test_j0();
    printf("j0: %s\n", failures ? "FAIL" : "PASS");

    test_j1();
    printf("j1: %s\n", failures ? "FAIL" : "PASS");

    test_jn();
    printf("jn: %s\n", failures ? "FAIL" : "PASS");

    test_y0();
    printf("y0: %s\n", failures ? "FAIL" : "PASS");

    test_y1();
    printf("y1: %s\n", failures ? "FAIL" : "PASS");

    test_yn();
    printf("yn: %s\n", failures ? "FAIL" : "PASS");

    test_float_variants();
    printf("float variants: %s\n", failures ? "FAIL" : "PASS");

    test_nan_inf();
    printf("NaN/Inf handling: %s\n", failures ? "FAIL" : "PASS");

    printf("\n%s: %d failures\n", failures ? "FAILED" : "PASSED", failures);
    return failures > 0;
}