/*
 * test_manip.c - Unit tests for floating-point manipulation functions
 *                (frexp, ldexp, modf, scalbn, ilogb, ...).
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
 * REQ-06-0715: frexp()/ldexp() round-trip.
 *   For any finite x, ldexp(frexp(x, &e), e) must reproduce x bit-exactly.
 *   frexp() splits x into a normalized fraction f in [0.5, 1.0) (or 0) and
 *   an integer exponent e such that x = f * 2^e. ldexp() reconstructs x
 *   from that pair via x = f * 2^e using exponent manipulation, so the
 *   round-trip must be exact (no rounding) for representable doubles.
 */
static void test_frexp_ldexp_roundtrip(void) {
    static const double xs[] = {
        1.0, 2.0, 0.5, 3.14159, 1e10, -1e10, 1e-10, -1e-10,
        1.5, -1.5, DBL_MIN, DBL_MAX / 2.0
    };
    static const char *names[] = {
        "1.0", "2.0", "0.5", "3.14159", "1e10", "-1e10", "1e-10", "-1e-10",
        "1.5", "-1.5", "DBL_MIN", "DBL_MAX/2"
    };
    size_t n = sizeof(xs) / sizeof(xs[0]);

    for (size_t i = 0; i < n; i++) {
        int e;
        double f = frexp(xs[i], &e);
        double r = ldexp(f, e);
        if (r != xs[i]) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "ldexp(frexp(%s, &e), e) != %s", names[i], names[i]);
            FAIL(msg);
        }
    }

    /* Silence unused-helper warnings on first-test-only build. */
    (void)isclose;
    (void)isclosef;
}

/*
 * REQ-06-0716: frexp() result range.
 *   For any finite nonzero x, frexp(x, &e) returns a normalized fraction f
 *   such that |f| is in [0.5, 1.0). For positive x, f is in [0.5, 1.0);
 *   for negative x, f is in (-1.0, -0.5].
 */
static void test_frexp_range(void) {
    static const double pos[] = {
        1.0, 2.0, 0.5, 3.14159, 1e10, 1e-10, 1.5,
        DBL_MIN, DBL_MAX / 2.0, 7.0, 8.0, 16.0
    };
    static const char *pos_names[] = {
        "1.0", "2.0", "0.5", "3.14159", "1e10", "1e-10", "1.5",
        "DBL_MIN", "DBL_MAX/2", "7.0", "8.0", "16.0"
    };
    size_t np = sizeof(pos) / sizeof(pos[0]);

    for (size_t i = 0; i < np; i++) {
        int e;
        double f = frexp(pos[i], &e);
        if (!(f >= 0.5 && f < 1.0)) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "frexp(%s) fraction %.17g not in [0.5, 1.0)",
                     pos_names[i], f);
            FAIL(msg);
        }
    }

    static const double neg[] = { -1.0, -2.0, -3.14159 };
    static const char *neg_names[] = { "-1.0", "-2.0", "-3.14159" };
    size_t nn = sizeof(neg) / sizeof(neg[0]);

    for (size_t i = 0; i < nn; i++) {
        int e;
        double f = frexp(neg[i], &e);
        if (!(f > -1.0 && f <= -0.5)) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "frexp(%s) fraction %.17g not in (-1.0, -0.5]",
                     neg_names[i], f);
            FAIL(msg);
        }
    }
}

/*
 * REQ-06-0717: frexp(0.0) returns 0.0 with exponent 0.
 *   For zero input, frexp() must set the exponent to 0 and return zero
 *   with the sign of the input preserved (frexp(-0.0) returns -0.0).
 */
static void test_frexp_zero(void) {
    int e = 12345;
    double f = frexp(0.0, &e);
    if (f != 0.0 || e != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "frexp(0.0) returned f=%.17g e=%d; expected 0.0, 0", f, e);
        FAIL(msg);
    }
    if (signbit(f)) {
        FAIL("frexp(0.0) result has negative sign bit");
    }

    e = 12345;
    f = frexp(-0.0, &e);
    if (f != 0.0 || e != 0) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "frexp(-0.0) returned f=%.17g e=%d; expected -0.0, 0", f, e);
        FAIL(msg);
    }
    if (!signbit(f)) {
        FAIL("frexp(-0.0) did not preserve negative sign bit");
    }
}

int main(void) {
    test_frexp_ldexp_roundtrip();
    test_frexp_range();
    test_frexp_zero();

    if (failures != 0) {
        printf("FAILURES: %d\n", failures);
    }

    return failures != 0;
}
