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

int main(void) {
    test_frexp_ldexp_roundtrip();

    if (failures != 0) {
        printf("FAILURES: %d\n", failures);
    }

    return failures != 0;
}
