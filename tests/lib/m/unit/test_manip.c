/*
 * test_manip.c - Unit tests for floating-point manipulation functions
 *                (frexp, ldexp, modf, scalbn, ilogb, ...).
 */

#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <float.h>
#include <limits.h>

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

/*
 * REQ-06-0718: modf() decomposes x into integer and fractional parts.
 *   modf(x, &i) stores the integer part of x (truncated toward zero) in *i
 *   and returns the fractional part. Both have the same sign as x. Their
 *   sum must reproduce x (bit-exactly for small x; within rounding for
 *   larger magnitudes where the integer part loses precision in the sum).
 */
static void test_modf(void) {
    static const double xs[] = {
        0.0, 1.0, 1.5, 3.14159, -2.7, -0.5, 100.5, 1e10
    };
    static const char *names[] = {
        "0.0", "1.0", "1.5", "3.14159", "-2.7", "-0.5", "100.5", "1e10"
    };
    size_t n = sizeof(xs) / sizeof(xs[0]);

    for (size_t k = 0; k < n; k++) {
        double i;
        double f = modf(xs[k], &i);
        double sum = i + f;

        /* Bit-exact for moderate magnitudes; otherwise close. */
        if (fabs(xs[k]) < 1.0e9) {
            if (sum != xs[k]) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "modf(%s): i=%.17g + f=%.17g = %.17g != %s",
                         names[k], i, f, sum, names[k]);
                FAIL(msg);
            }
        } else {
            if (!isclose(sum, xs[k], 1e-15 * fabs(xs[k]))) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "modf(%s): i=%.17g + f=%.17g not close to %s",
                         names[k], i, f, names[k]);
                FAIL(msg);
            }
        }

        /* Integer part must be integer-valued. */
        if (i != trunc(i)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "modf(%s): integer part %.17g is not integer-valued",
                     names[k], i);
            FAIL(msg);
        }

        /* Fractional part must share sign of x and have |f| < 1. */
        if (xs[k] >= 0.0) {
            if (!(f >= 0.0 && f < 1.0)) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "modf(%s): fraction %.17g not in [0, 1)",
                         names[k], f);
                FAIL(msg);
            }
        } else {
            if (!(f <= 0.0 && f > -1.0)) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "modf(%s): fraction %.17g not in (-1, 0]",
                         names[k], f);
                FAIL(msg);
            }
        }
    }

    /* modf(0.0): f==0, i==0, both with positive sign. */
    {
        double i = 99.0;
        double f = modf(0.0, &i);
        if (f != 0.0 || i != 0.0) {
            FAIL("modf(0.0) did not return zeros");
        }
        if (signbit(f) || signbit(i)) {
            FAIL("modf(0.0) did not preserve positive sign");
        }
    }

    /* modf(-0.0): f==-0, i==-0, both with negative sign. */
    {
        double i = 99.0;
        double f = modf(-0.0, &i);
        if (f != 0.0 || i != 0.0) {
            FAIL("modf(-0.0) did not return zeros");
        }
        if (!signbit(f) || !signbit(i)) {
            FAIL("modf(-0.0) did not preserve negative sign");
        }
    }

    /* modf(INFINITY): f==0 (with sign of x), i==INFINITY. */
    {
        double i = 0.0;
        double f = modf(INFINITY, &i);
        if (!isinf(i) || signbit(i)) {
            FAIL("modf(INFINITY) integer part is not +INFINITY");
        }
        if (f != 0.0) {
            FAIL("modf(INFINITY) fractional part is not 0");
        }
    }
}

/*
 * REQ-06-0719: scalbn(x, n) == x * 2^n for moderate n.
 *   scalbn() scales x by 2^n via direct exponent manipulation. For finite x
 *   and moderate n (no overflow/underflow), the result must equal x * 2^n
 *   bit-exactly. Special cases: zeros preserve sign, NaN propagates, and
 *   infinities are returned unchanged.
 */
static void test_scalbn(void) {
    if (scalbn(1.0, 0) != 1.0) {
        FAIL("scalbn(1.0, 0) != 1.0");
    }
    if (scalbn(1.0, 10) != 1024.0) {
        FAIL("scalbn(1.0, 10) != 1024.0");
    }
    if (scalbn(1.0, -10) != 1.0 / 1024.0) {
        FAIL("scalbn(1.0, -10) != 1.0/1024.0");
    }
    if (scalbn(3.14159, 0) != 3.14159) {
        FAIL("scalbn(3.14159, 0) != 3.14159");
    }
    if (scalbn(2.5, 4) != 40.0) {
        FAIL("scalbn(2.5, 4) != 40.0");
    }
    if (scalbn(-1.0, 5) != -32.0) {
        FAIL("scalbn(-1.0, 5) != -32.0");
    }
    if (scalbn(0.5, 1) != 1.0) {
        FAIL("scalbn(0.5, 1) != 1.0");
    }

    /* scalbn(0.0, 100): zero preserved with positive sign. */
    {
        double r = scalbn(0.0, 100);
        if (r != 0.0) {
            FAIL("scalbn(0.0, 100) != 0.0");
        }
        if (signbit(r)) {
            FAIL("scalbn(0.0, 100) lost positive sign");
        }
    }

    /* scalbn(-0.0, 100): zero preserved with negative sign. */
    {
        double r = scalbn(-0.0, 100);
        if (r != 0.0) {
            FAIL("scalbn(-0.0, 100) != 0.0");
        }
        if (!signbit(r)) {
            FAIL("scalbn(-0.0, 100) did not preserve negative sign");
        }
    }

    /* scalbn(NAN, 5) is NaN. */
    if (!isnan(scalbn(NAN, 5))) {
        FAIL("scalbn(NAN, 5) is not NaN");
    }

    /* scalbn(INFINITY, -5) -> INFINITY. */
    {
        double r = scalbn(INFINITY, -5);
        if (!isinf(r) || signbit(r)) {
            FAIL("scalbn(INFINITY, -5) != +INFINITY");
        }
    }
}

/*
 * REQ-06-0720: ilogb() returns the unbiased exponent for powers of 2.
 *   ilogb(x) returns floor(log2(|x|)) as an int. For an exact power of 2,
 *   the result is the integer exponent. Sign is ignored: ilogb(-x)==ilogb(x).
 */
static void test_ilogb_basic(void) {
    if (ilogb(1.0) != 0) {
        FAIL("ilogb(1.0) != 0");
    }
    if (ilogb(2.0) != 1) {
        FAIL("ilogb(2.0) != 1");
    }
    if (ilogb(0.5) != -1) {
        FAIL("ilogb(0.5) != -1");
    }
    if (ilogb(4.0) != 2) {
        FAIL("ilogb(4.0) != 2");
    }
    if (ilogb(0.25) != -2) {
        FAIL("ilogb(0.25) != -2");
    }
    if (ilogb(1024.0) != 10) {
        FAIL("ilogb(1024.0) != 10");
    }
    if (ilogb(0.0009765625) != -10) {
        FAIL("ilogb(1.0/1024.0) != -10");
    }
    if (ilogb(-1.0) != 0) {
        FAIL("ilogb(-1.0) != 0");
    }
    if (ilogb(-2.0) != 1) {
        FAIL("ilogb(-2.0) != 1");
    }
}

/*
 * REQ-06-0721: ilogb() special values.
 *   ilogb(0) is undefined as a real exponent, so the standard requires it
 *   to return the implementation-defined sentinel FP_ILOGB0 (typically
 *   INT_MIN). ilogb(NaN) returns FP_ILOGBNAN (typically INT_MAX or INT_MIN).
 *   ilogb(+/-INFINITY) returns INT_MAX. Sign is ignored throughout.
 */
static void test_ilogb_special(void) {
    if (ilogb(0.0) != FP_ILOGB0) {
        FAIL("ilogb(0.0) != FP_ILOGB0");
    }
    if (ilogb(-0.0) != FP_ILOGB0) {
        FAIL("ilogb(-0.0) != FP_ILOGB0");
    }
    if (ilogb(NAN) != FP_ILOGBNAN) {
        FAIL("ilogb(NAN) != FP_ILOGBNAN");
    }
    if (ilogb(INFINITY) != INT_MAX) {
        FAIL("ilogb(INFINITY) != INT_MAX");
    }
    if (ilogb(-INFINITY) != INT_MAX) {
        FAIL("ilogb(-INFINITY) != INT_MAX");
    }
}

/*
 * REQ-06-0722: logb() basic and special values.
 *   logb(x) returns the unbiased exponent of x as a double: logb(1.0)==0,
 *   logb(2.0)==1, logb(0.5)==-1. Special cases: logb(+/-0) returns -INFINITY
 *   (and raises FE_DIVBYZERO), logb(+/-INFINITY) returns +INFINITY, and
 *   logb(NaN) returns NaN. Sign of x is ignored.
 */
static void test_logb(void) {
    if (logb(1.0) != 0.0) {
        FAIL("logb(1.0) != 0.0");
    }
    if (logb(2.0) != 1.0) {
        FAIL("logb(2.0) != 1.0");
    }
    if (logb(0.5) != -1.0) {
        FAIL("logb(0.5) != -1.0");
    }

    /* logb(+/-0) -> -INFINITY. */
    {
        double r = logb(0.0);
        if (!isinf(r) || !(r < 0)) {
            FAIL("logb(0.0) is not -INFINITY");
        }
    }
    {
        double r = logb(-0.0);
        if (!isinf(r) || !(r < 0)) {
            FAIL("logb(-0.0) is not -INFINITY");
        }
    }

    /* logb(+/-INFINITY) -> +INFINITY. */
    if (logb(INFINITY) != INFINITY) {
        FAIL("logb(INFINITY) != INFINITY");
    }
    if (logb(-INFINITY) != INFINITY) {
        FAIL("logb(-INFINITY) != INFINITY");
    }

    /* logb(NaN) -> NaN. */
    if (!isnan(logb(NAN))) {
        FAIL("logb(NAN) is not NaN");
    }
}

/*
 * REQ-06-0723: nextafter() steps to the next representable double.
 *   nextafter(x, y) returns the next representable value after x in the
 *   direction of y. For x == 1.0 toward 2.0, the next representable double
 *   is exactly 1.0 + DBL_EPSILON (DBL_EPSILON is the gap between 1.0 and
 *   the next double above it). Stepping below 1.0 gives 1.0 - DBL_EPSILON/2
 *   because the binade [0.5, 1.0) has half the spacing of [1.0, 2.0).
 *   Across the binade boundary at 2.0, the spacing doubles, so the step
 *   from 2.0 toward 3.0 is 2 * DBL_EPSILON.
 */
static void test_nextafter_basic(void) {
    double up = nextafter(1.0, 2.0);
    if (!(up > 1.0)) {
        FAIL("nextafter(1.0, 2.0) is not greater than 1.0");
    }
    if (up - 1.0 != DBL_EPSILON) {
        FAIL("nextafter(1.0, 2.0) - 1.0 != DBL_EPSILON");
    }

    double down = nextafter(1.0, 0.0);
    if (!(down < 1.0)) {
        FAIL("nextafter(1.0, 0.0) is not less than 1.0");
    }
    if (1.0 - down != DBL_EPSILON / 2.0) {
        FAIL("1.0 - nextafter(1.0, 0.0) != DBL_EPSILON/2");
    }

    double up2 = nextafter(2.0, 3.0);
    if (up2 - 2.0 != 2.0 * DBL_EPSILON) {
        FAIL("nextafter(2.0, 3.0) - 2.0 != 2*DBL_EPSILON");
    }
}

/*
 * REQ-06-0724: nextafter(0.0, 1.0) returns the smallest positive denormal.
 *   Stepping from zero toward a positive value yields the smallest positive
 *   subnormal double, which is 2^-1074 (0x1p-1074). This is strictly less
 *   than DBL_MIN, the smallest positive *normal* double (2^-1022). The sign
 *   of the zero argument does not affect the result: nextafter(-0.0, 1.0)
 *   also yields +0x1p-1074 because the direction is determined by the second
 *   argument. Stepping from 0 toward a negative value gives -0x1p-1074.
 */
static void test_nextafter_denormal(void) {
    double n = nextafter(0.0, 1.0);
    if (!(n > 0.0 && n < DBL_MIN)) {
        FAIL("nextafter(0.0, 1.0) not in (0, DBL_MIN)");
    }
    if (n != 0x1p-1074) {
        FAIL("nextafter(0.0, 1.0) != 0x1p-1074");
    }

    double neg = nextafter(0.0, -1.0);
    if (neg != -0x1p-1074) {
        FAIL("nextafter(0.0, -1.0) != -0x1p-1074");
    }

    double from_negzero = nextafter(-0.0, 1.0);
    if (from_negzero != 0x1p-1074) {
        FAIL("nextafter(-0.0, 1.0) != 0x1p-1074");
    }
}

/*
 * REQ-06-0725: nextafter(x, x) == x for all x.
 *   When the two arguments are equal, the standard requires nextafter to
 *   return the second argument unchanged (no step is taken). This must hold
 *   for every finite value, both signed zeros, the subnormals at the edges
 *   of representability (DBL_MIN is the smallest normal), the largest finite
 *   double (DBL_MAX), and the infinities. NaN is excluded because NaN != NaN
 *   makes "x == x" meaningless and nextafter(NaN, NaN) returns NaN by spec.
 *   For +/-0 the C standard returns the second argument as-is, so the sign
 *   bit must match y's signbit, not x's.
 */
static void test_nextafter_idempotent(void) {
    static const double xs[] = {
        0.0, -0.0, 1.0, -1.0, 3.14, -3.14,
        DBL_MIN, DBL_MAX, INFINITY, -INFINITY
    };
    static const char *names[] = {
        "0.0", "-0.0", "1.0", "-1.0", "3.14", "-3.14",
        "DBL_MIN", "DBL_MAX", "INFINITY", "-INFINITY"
    };
    size_t n = sizeof(xs) / sizeof(xs[0]);

    for (size_t i = 0; i < n; i++) {
        double r = nextafter(xs[i], xs[i]);

        /* For zeros, signbit of result must match the second argument. */
        if (xs[i] == 0.0) {
            if (r != 0.0) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "nextafter(%s, %s) != 0.0", names[i], names[i]);
                FAIL(msg);
            }
            if (signbit(r) != signbit(xs[i])) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "nextafter(%s, %s) signbit mismatch",
                         names[i], names[i]);
                FAIL(msg);
            }
            continue;
        }

        /* Bit-exact comparison via memcmp on the IEEE-754 representation. */
        if (r != xs[i]) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "nextafter(%s, %s) != %s", names[i], names[i], names[i]);
            FAIL(msg);
        }
    }
}

int main(void) {
    test_frexp_ldexp_roundtrip();
    test_frexp_range();
    test_frexp_zero();
    test_modf();
    test_scalbn();
    test_ilogb_basic();
    test_ilogb_special();
    test_logb();
    test_nextafter_basic();
    test_nextafter_denormal();
    test_nextafter_idempotent();

    if (failures != 0) {
        printf("FAILURES: %d\n", failures);
    }

    return failures != 0;
}
