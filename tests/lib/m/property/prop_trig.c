/*
 * prop_trig.c - Property-based tests for C23 pi-argument trigonometric functions
 *
 * Tests mathematical properties across the full domain of each function.
 */

#include <stdio.h>
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

static int isclosef(float a, float b, float tol) {
    return fabsf(a - b) < tol;
}

/*
 * sinpi(x)^2 + cospi(x)^2 == 1
 */
static void prop_sinpi_cospi_identity(void) {
    for (double x = -10.0; x <= 10.0; x += 0.1) {
        double s = sinpi(x);
        double c = cospi(x);
        double sq = s * s + c * c;
        if (!isclose(sq, 1.0, 1e-12)) {
            FAIL("sinpi^2 + cospi^2 == 1");
            break;
        }
    }
}

/*
 * sinpi(-x) == -sinpi(x)  (odd)
 */
static void prop_sinpi_odd(void) {
    for (double x = -5.0; x <= 5.0; x += 0.1) {
        double s = sinpi(x);
        double sn = sinpi(-x);
        if (!isclose(sn, -s, 1e-13)) {
            FAIL("sinpi is odd");
            break;
        }
    }
}

/*
 * cospi(-x) == cospi(x)  (even)
 */
static void prop_cospi_even(void) {
    for (double x = -5.0; x <= 5.0; x += 0.1) {
        double c = cospi(x);
        double cn = cospi(-x);
        if (!isclose(cn, c, 1e-13)) {
            FAIL("cospi is even");
            break;
        }
    }
}

/*
 * tanpi(-x) == -tanpi(x)  (odd)
 */
static void prop_tanpi_odd(void) {
    for (double x = -2.0; x <= 2.0; x += 0.1) {
        double t = tanpi(x);
        double tn = tanpi(-x);
        if (!isclose(tn, -t, 1e-13)) {
            FAIL("tanpi is odd");
            break;
        }
    }
}

/*
 * asinpi(sinpi(x)) == x  for x in [-0.5, 0.5]
 */
static void prop_asinpi_sinpi_inverse(void) {
    for (double x = -0.5; x <= 0.5; x += 0.01) {
        double s = sinpi(x);
        double a = asinpi(s);
        if (!isclose(a, x, 1e-13)) {
            FAIL("asinpi(sinpi(x)) == x");
            break;
        }
    }
}

/*
 * atanpi(tanpi(x)) == x  for x in [-1, 1]
 */
static void prop_atanpi_tanpi_inverse(void) {
    for (double x = -1.0; x <= 1.0; x += 0.05) {
        double t = tanpi(x);
        double a = atanpi(t);
        if (!isclose(a, x, 1e-13)) {
            FAIL("atanpi(tanpi(x)) == x");
            break;
        }
    }
}

/*
 * asinpi is odd
 */
static void prop_asinpi_odd(void) {
    for (double x = -1.0; x <= 1.0; x += 0.1) {
        double a = asinpi(x);
        double an = asinpi(-x);
        if (!isclose(an, -a, 1e-13)) {
            FAIL("asinpi is odd");
            break;
        }
    }
}

/*
 * atanpi is odd
 */
static void prop_atanpi_odd(void) {
    for (double x = -10.0; x <= 10.0; x += 0.1) {
        double a = atanpi(x);
        double an = atanpi(-x);
        if (!isclose(an, -a, 1e-13)) {
            FAIL("atanpi is odd");
            break;
        }
    }
}

/*
 * atanpi(1) == 0.25  exactly
 */
static void prop_atanpi_one(void) {
    if (!isclose(atanpi(1.0), 0.25, 1e-13)) {
        FAIL("atanpi(1) == 0.25");
    }
}

/*
 * sinpi(float) == sinpi(double) within float precision
 */
static void prop_sinpi_float_double(void) {
    for (double x = -5.0; x <= 5.0; x += 0.5) {
        double d = sinpi(x);
        float f = sinpif((float)x);
        if (!isclosef(f, (float)d, 1e-5f)) {
            FAIL("sinpi float-double consistency");
            break;
        }
    }
}

/*
 * sin(x)^2 + cos(x)^2 == 1  (Pythagorean identity, REQ-06-0662)
 * Verified across a representative sweep of finite x.
 */
static void prop_sin_cos_pythagorean(void) {
    const double pi = 3.14159265358979323846;
    const double lo = -10.0 * pi;
    const double hi =  10.0 * pi;
    const int n = 1000;
    const double step = (hi - lo) / (double)(n - 1);

    for (int i = 0; i < n; i++) {
        double x = lo + (double)i * step;
        if (!isfinite(x)) continue;
        double s = sin(x);
        double c = cos(x);
        double residual = s * s + c * c - 1.0;
        if (fabs(residual) >= 1e-12) {
            FAIL("sin^2 + cos^2 != 1 on uniform sweep");
            return;
        }
    }

    static const double boundary[] = {
        0.0,
        M_PI_2, -M_PI_2,
        M_PI,   -M_PI,
        2.0 * M_PI, -2.0 * M_PI,
        100.0, -100.0,
    };
    const int nb = (int)(sizeof(boundary) / sizeof(boundary[0]));
    for (int i = 0; i < nb; i++) {
        double x = boundary[i];
        if (!isfinite(x)) continue;
        double s = sin(x);
        double c = cos(x);
        double residual = s * s + c * c - 1.0;
        if (fabs(residual) >= 1e-12) {
            FAIL("sin^2 + cos^2 != 1 at boundary");
            return;
        }
    }
}

/*
 * sin(-x) == -sin(x)  (odd function, REQ-06-0663)
 * Verified across a uniform sweep plus boundary cases.
 */
static void prop_sin_odd(void) {
    const double pi = 3.14159265358979323846;
    const double lo = -10.0 * pi;
    const double hi =  10.0 * pi;
    const int n = 1000;
    const double step = (hi - lo) / (double)(n - 1);

    for (int i = 0; i < n; i++) {
        double x = lo + (double)i * step;
        if (!isfinite(x)) continue;
        double s_pos = sin(x);
        double s_neg = sin(-x);
        if (s_neg == -s_pos) continue;
        if (fabs(s_neg + s_pos) >= 1e-15) {
            FAIL("sin(-x) != -sin(x) on uniform sweep");
            return;
        }
    }

    static const double boundary[] = {
        0.0,
        M_PI_2, -M_PI_2,
        M_PI,   -M_PI,
        100.0, -100.0,
    };
    const int nb = (int)(sizeof(boundary) / sizeof(boundary[0]));
    for (int i = 0; i < nb; i++) {
        double x = boundary[i];
        if (!isfinite(x)) continue;
        double s_pos = sin(x);
        double s_neg = sin(-x);
        if (s_neg == -s_pos) continue;
        if (fabs(s_neg + s_pos) >= 1e-15) {
            FAIL("sin(-x) != -sin(x) at boundary");
            return;
        }
    }

    /* Sign preservation at zero: sin(+0.0) is +0.0, sin(-0.0) is -0.0. */
    double s_pz = sin(0.0);
    double s_nz = sin(-0.0);
    if (s_pz != 0.0 || signbit(s_pz)) {
        FAIL("sin(+0.0) != +0.0");
        return;
    }
    if (s_nz != 0.0 || !signbit(s_nz)) {
        FAIL("sin(-0.0) != -0.0");
        return;
    }
}

int main(void) {
    prop_sin_cos_pythagorean();
    prop_sin_odd();
    prop_sinpi_cospi_identity();
    prop_sinpi_odd();
    prop_cospi_even();
    prop_tanpi_odd();
    prop_asinpi_sinpi_inverse();
    prop_atanpi_tanpi_inverse();
    prop_asinpi_odd();
    prop_atanpi_odd();
    prop_atanpi_one();
    prop_sinpi_float_double();
    
    if (failures == 0) {
        printf("All property tests passed.\n");
    } else {
        printf("%d property tests failed.\n", failures);
    }
    
    return failures;
}
