/*
 * test_trig.c - Unit tests for C23 pi-argument trigonometric functions
 *
 * Tests sinpi, cospi, tanpi, asinpi, atanpi and their float/ldouble variants.
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

static void test_sin(void) {
    double sp0 = sin(+0.0);
    if (sp0 != 0.0 || signbit(sp0)) FAIL("sin(+0.0)");

    double sn0 = sin(-0.0);
    if (sn0 != 0.0 || !signbit(sn0)) FAIL("sin(-0.0)");

    if (!isclose(sin(M_PI / 2.0), 1.0, 1e-15)) FAIL("sin(pi/2)");

    if (!isclose(sin(M_PI), 0.0, 1e-14)) FAIL("sin(pi)");

    if (!isnan(sin(NAN))) FAIL("sin(NaN)");
    if (!isnan(sin(INFINITY))) FAIL("sin(+inf)");
    if (!isnan(sin(-INFINITY))) FAIL("sin(-inf)");
}

static void test_cos(void) {
    if (cos(+0.0) != 1.0) FAIL("cos(+0.0)");

    if (cos(-0.0) != 1.0) FAIL("cos(-0.0)");

    if (!isclose(cos(M_PI), -1.0, 1e-14)) FAIL("cos(pi)");

    if (!isclose(cos(M_PI / 2.0), 0.0, 1e-15)) FAIL("cos(pi/2)");

    if (!isnan(cos(NAN))) FAIL("cos(NaN)");
    if (!isnan(cos(INFINITY))) FAIL("cos(+inf)");
    if (!isnan(cos(-INFINITY))) FAIL("cos(-inf)");
}

static void test_tan(void) {
    double tp0 = tan(+0.0);
    if (tp0 != 0.0 || signbit(tp0)) FAIL("tan(+0.0)");

    double tn0 = tan(-0.0);
    if (tn0 != 0.0 || !signbit(tn0)) FAIL("tan(-0.0)");

    if (!isclose(tan(M_PI / 4.0), 1.0, 1e-14)) FAIL("tan(pi/4)");

    if (!isnan(tan(NAN))) FAIL("tan(NaN)");
    if (!isnan(tan(INFINITY))) FAIL("tan(+inf)");
    if (!isnan(tan(-INFINITY))) FAIL("tan(-inf)");
}

static void test_asin(void) {
    double ap0 = asin(+0.0);
    if (ap0 != 0.0 || signbit(ap0)) FAIL("asin(+0.0)");

    double an0 = asin(-0.0);
    if (an0 != 0.0 || !signbit(an0)) FAIL("asin(-0.0)");

    if (!isclose(asin(1.0), M_PI / 2.0, 1e-15)) FAIL("asin(1)");
    if (!isclose(asin(-1.0), -M_PI / 2.0, 1e-15)) FAIL("asin(-1)");

    if (!isnan(asin(2.0))) FAIL("asin(2) domain");
    if (!isnan(asin(-2.0))) FAIL("asin(-2) domain");
    if (!isnan(asin(NAN))) FAIL("asin(NaN)");
    if (!isnan(asin(INFINITY))) FAIL("asin(+inf)");
    if (!isnan(asin(-INFINITY))) FAIL("asin(-inf)");
}

static void test_acos(void) {
    if (acos(1.0) != 0.0) FAIL("acos(1)");

    if (!isclose(acos(0.0), M_PI / 2.0, 1e-15)) FAIL("acos(0)");
    if (!isclose(acos(-1.0), M_PI, 1e-14)) FAIL("acos(-1)");

    if (!isnan(acos(2.0))) FAIL("acos(2) domain");
    if (!isnan(acos(-2.0))) FAIL("acos(-2) domain");
    if (!isnan(acos(NAN))) FAIL("acos(NaN)");
    if (!isnan(acos(INFINITY))) FAIL("acos(+inf)");
    if (!isnan(acos(-INFINITY))) FAIL("acos(-inf)");
}

static void test_atan(void) {
    double ap0 = atan(+0.0);
    if (ap0 != 0.0 || signbit(ap0)) FAIL("atan(+0.0)");

    double an0 = atan(-0.0);
    if (an0 != 0.0 || !signbit(an0)) FAIL("atan(-0.0)");

    if (!isclose(atan(1.0), M_PI / 4.0, 1e-15)) FAIL("atan(1)");
    if (!isclose(atan(-1.0), -M_PI / 4.0, 1e-15)) FAIL("atan(-1)");

    if (!isclose(atan(INFINITY), M_PI / 2.0, 1e-15)) FAIL("atan(+inf)");
    if (!isclose(atan(-INFINITY), -M_PI / 2.0, 1e-15)) FAIL("atan(-inf)");

    if (!isnan(atan(NAN))) FAIL("atan(NaN)");
}

static void test_atan2(void) {
    /* Spec: atan2(0,1)==0, atan2(1,0)~pi/2, atan2(0,-1)~pi, atan2(-1,0)~-pi/2 */
    if (atan2(0.0, 1.0) != 0.0) FAIL("atan2(0,1)");
    if (!isclose(atan2(1.0, 0.0), M_PI / 2.0, 1e-15)) FAIL("atan2(1,0)");
    if (!isclose(atan2(0.0, -1.0), M_PI, 1e-14)) FAIL("atan2(0,-1)");
    if (!isclose(atan2(-1.0, 0.0), -M_PI / 2.0, 1e-15)) FAIL("atan2(-1,0)");

    /* Diagonal */
    if (!isclose(atan2(1.0, 1.0), M_PI / 4.0, 1e-15)) FAIL("atan2(1,1)");

    /* NaN propagation */
    if (!isnan(atan2(NAN, 1.0))) FAIL("atan2(NaN,1)");
    if (!isnan(atan2(1.0, NAN))) FAIL("atan2(1,NaN)");

    /* C99 Annex F infinity corners */
    if (!isclose(atan2(INFINITY, INFINITY), M_PI / 4.0, 1e-15)) FAIL("atan2(+inf,+inf)");
    if (!isclose(atan2(-INFINITY, -INFINITY), -3.0 * M_PI / 4.0, 1e-15)) FAIL("atan2(-inf,-inf)");
    if (!isclose(atan2(INFINITY, 0.0), M_PI / 2.0, 1e-15)) FAIL("atan2(+inf,0)");
    if (atan2(1.0, INFINITY) != 0.0) FAIL("atan2(1,+inf)");
    if (!isclose(atan2(1.0, -INFINITY), M_PI, 1e-14)) FAIL("atan2(1,-inf)");
}

static void test_sinpi(void) {
    /* sinpi(n/2) should give +-1 or 0 for integer n */
    double expected[] = { 0.0, 1.0, 0.0, -1.0, 0.0, 1.0, 0.0, -1.0 };
    
    for (int i = 0; i < 8; i++) {
        double x = i / 2.0;
        double r = sinpi(x);
        if (!isclose(r, expected[i], 1e-13)) {
            printf("DEBUG sinpi(%g) = %f (expected %f)\n", x, r, expected[i]);
            printf("DEBUG sin(%g*pi) = %f\n", x, sin(x * 3.14159265358979323846));
            FAIL("sinpi(i/2) mismatch");
            break;
        }
    }

    /* sinpi(0) == 0 */
    if (sinpi(0) != 0) {
        printf("DEBUG sinpi(0) = %f\n", sinpi(0));
        FAIL("sinpi(0)");
    }
    
    /* sinpi(1) == 0 */
    if (!isclose(sinpi(1), 0.0, 1e-13)) {
        printf("DEBUG sinpi(1) = %f, sin(pi) = %f\n", sinpi(1), sin(3.14159265358979323846));
        FAIL("sinpi(1)");
    }

    /*
     * REQ-06-0657: exact-value tests.
     * sinpi() must return bit-exact values at integers and half-integers
     * via integer/half-integer special-case dispatch (mod-2 reduction).
     * These compare with == (not isclose).
     */
    if (sinpi(0.0) != 0.0) FAIL("sinpi(0.0) != 0.0 (exact)");
    if (sinpi(1.0) != 0.0) FAIL("sinpi(1.0) != 0.0 (exact)");
    if (sinpi(2.0) != 0.0) FAIL("sinpi(2.0) != 0.0 (exact)");
    if (sinpi(-1.0) != 0.0) FAIL("sinpi(-1.0) != 0.0 (exact)");
    if (sinpi(0.5) != 1.0) FAIL("sinpi(0.5) != 1.0 (exact)");
    if (sinpi(-0.5) != -1.0) FAIL("sinpi(-0.5) != -1.0 (exact)");
    if (sinpi(1.5) != -1.0) FAIL("sinpi(1.5) != -1.0 (exact)");
    if (!isnan(sinpi(NAN))) FAIL("sinpi(NaN) is not NaN");
    if (!isnan(sinpi(INFINITY))) FAIL("sinpi(+inf) is not NaN");
    if (!isnan(sinpi(-INFINITY))) FAIL("sinpi(-inf) is not NaN");
}

static void test_cospi(void) {
    /* cospi(n) should give +-1 for integer n */
    for (int i = 0; i < 5; i++) {
        double r = cospi(i);
        double expected = (i % 2) == 0 ? 1.0 : -1.0;
        if (!isclose(r, expected, 1e-13)) {
            FAIL("cospi(n) mismatch");
            break;
        }
    }
    
    /* cospi(0) == 1 */
    if (cospi(0) != 1.0) FAIL("cospi(0)");
    
    /* cospi(1) == -1 */
    if (!isclose(cospi(1), -1.0, 1e-13)) FAIL("cospi(1)");

    /*
     * REQ-06-0658: exact-value tests.
     * cospi() must return bit-exact values at integers and half-integers
     * via integer/half-integer special-case dispatch.
     * These compare with == (not isclose).
     */
    if (cospi(0.0) != 1.0) FAIL("cospi(0.0) != 1.0 (exact)");
    if (cospi(1.0) != -1.0) FAIL("cospi(1.0) != -1.0 (exact)");
    if (cospi(2.0) != 1.0) FAIL("cospi(2.0) != 1.0 (exact)");
    if (cospi(-1.0) != -1.0) FAIL("cospi(-1.0) != -1.0 (exact)");
    if (cospi(0.5) != 0.0) FAIL("cospi(0.5) != 0.0 (exact)");
    if (cospi(-0.5) != 0.0) FAIL("cospi(-0.5) != 0.0 (exact)");
    if (cospi(1.5) != 0.0) FAIL("cospi(1.5) != 0.0 (exact)");
    if (!isnan(cospi(NAN))) FAIL("cospi(NaN) is not NaN");
    if (!isnan(cospi(INFINITY))) FAIL("cospi(+inf) is not NaN");
    if (!isnan(cospi(-INFINITY))) FAIL("cospi(-inf) is not NaN");
}

static void test_tanpi(void) {
    /* tanpi(n) should be 0 for integer n */
    for (int i = 0; i < 5; i++) {
        double r = tanpi(i);
        if (!isclose(r, 0.0, 1e-13)) {
            FAIL("tanpi(n) == 0");
            break;
        }
    }
    
    /* tanpi(0) == 0 */
    if (fabs(tanpi(0)) > 1e-15) FAIL("tanpi(0)");
}

static void test_asinpi(void) {
    /* asinpi(0) == 0 */
    if (fabs(asinpi(0)) > 1e-15) FAIL("asinpi(0)");
    
    /* asinpi(1) == pi/2 / pi = 0.5 */
    if (!isclose(asinpi(1.0), 0.5, 1e-13)) FAIL("asinpi(1)");
    
    /* asinpi(-1) == -0.5 */
    if (!isclose(asinpi(-1.0), -0.5, 1e-13)) FAIL("asinpi(-1)");
    
    /* asinpi(0.5) should give asin(0.5)/pi = (pi/6)/pi = 1/6 */
    if (!isclose(asinpi(0.5), 1.0/6.0, 1e-13)) FAIL("asinpi(0.5)");
}

static void test_atanpi(void) {
    /* atanpi(0) == 0 */
    if (fabs(atanpi(0)) > 1e-15) FAIL("atanpi(0)");
    
    /* atanpi(1) should give atan(1)/pi = (pi/4)/pi = 0.25 */
    double atanpi1 = atanpi(1.0);
    if (!isclose(atanpi1, 0.25, 1e-12)) {
        printf("DEBUG atanpi(1) = %f (expected 0.25), atan(1) = %f\n", atanpi1, atan(1.0));
        FAIL("atanpi(1)");
    }
    
    /* atanpi is odd: atanpi(-x) == -atanpi(x) */
    double ap = atanpi(2.0);
    double an = atanpi(-2.0);
    if (!isclose(ap, -an, 1e-13)) FAIL("atanpi(-x) == -atanpi(x)");
    
    /* atanpi(x) -> 0.5 as x -> inf */
    if (!isclose(atanpi(1e15), 0.5, 1e-13)) FAIL("atanpi(inf)");
}

static void test_float_variants(void) {
    /* sinpif(n/2) */
    float s = sinpif(0.5f);
    if (fabsf(s) > 1e-4) {
        printf("DEBUG sinpif(0.5) = %f (expected 1.0)\n", s);
        FAIL("sinpif(0.5)");
    }
    
    /* cospif(0) == 1 */
    if (fabsf(cospif(0.0f) - 1.0f) > 1e-5) FAIL("cospif(0)");
    
    /* tanpif(0) == 0 */
    if (fabsf(tanpif(0.0f)) > 1e-5) FAIL("tanpif(0)");
    
    /* asinpif(0) == 0 */
    if (fabsf(asinpif(0.0f)) > 1e-5) FAIL("asinpif(0)");
    
    /* asinpif(1) == 0.5 */
    if (fabsf(asinpif(1.0f) - 0.5f) > 1e-4) FAIL("asinpif(1)");
    
    /* atanpif(0) == 0 */
    if (fabsf(atanpif(0.0f)) > 1e-5) FAIL("atanpif(0)");
    
    /* atanpif(1) == 0.25 */
    float apif = atanpif(1.0f);
    if (fabsf(apif - 0.25f) > 1e-3) {
        printf("DEBUG atanpif(1) = %f (expected 0.25)\n", apif);
        FAIL("atanpif(1)");
    }
}

static void test_identity(void) {
    /* sinpi(x) = sin(x*pi) */
    for (double x = -3.0; x <= 3.0; x += 0.5) {
        if (!isclose(sinpi(x), sin(M_PI * x), 1e-13)) {
            FAIL("sinpi identity");
            break;
        }
    }
    
    /* cospi(x) = cos(x*pi) */
    for (double x = -3.0; x <= 3.0; x += 0.5) {
        if (!isclose(cospi(x), cos(M_PI * x), 1e-13)) {
            FAIL("cospi identity");
            break;
        }
    }
    
    /* tanpi(x) = tan(x*pi) */
    for (double x = -1.5; x <= 1.5; x += 0.5) {
        if (!isclose(tanpi(x), tan(M_PI * x), 1e-13)) {
            FAIL("tanpi identity");
            break;
        }
    }
}

int main(void) {
    test_sin();
    test_cos();
    test_tan();
    test_asin();
    test_acos();
    test_atan();
    test_atan2();
    test_sinpi();
    test_cospi();
    test_tanpi();
    test_asinpi();
    test_atanpi();
    test_float_variants();
    test_identity();
    
    if (failures == 0) {
        printf("All tests passed.\n");
    } else {
        printf("%d tests failed.\n", failures);
    }
    
    return failures;
}
