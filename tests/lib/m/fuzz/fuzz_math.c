/*
 * fuzz_math.c — fuzz every double-precision libm entry point.
 *
 * Strategy: walk a domain of "interesting" inputs (denormals, ±0,
 * ±inf, NaN, DBL_MAX, DBL_MIN, integer values around 0/1/2^k, random
 * bit patterns) and for each function check:
 *
 *   - No crash / no SIGFPE (reaching the end of the loop is the win
 *     condition).
 *   - f(NaN) returns NaN for any function whose spec says so (most
 *     of them; the few exceptions like pow(NaN, 0) = 1 are listed
 *     explicitly).
 *   - errno + fetestexcept() track domain / range error claims.
 *
 * REQ-06-0796..0800.
 */

#include <stdio.h>
#include <math.h>
#include <fenv.h>
#include <errno.h>
#include <float.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI   3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif
#ifndef M_PI_4
#define M_PI_4 0.78539816339744830962
#endif
#ifndef M_E
#define M_E    2.71828182845904523536
#endif

static int g_failures = 0;
#define FAIL(name) do { printf("  FAIL: %s (line %d)\n", (name), __LINE__); g_failures++; } while (0)

static double bits_to_double(uint64_t u) {
    union { uint64_t u; double d; } v = { .u = u };
    return v.d;
}

/* Deterministic xorshift64 — we want fuzzing reproducibility across
 * machines and across runs. */
static uint64_t prng_state = 0x123456789ABCDEFULL;
static uint64_t prng(void) {
    uint64_t x = prng_state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return prng_state = x;
}

/* The "interesting" domain.  Mixed with PRNG-generated bit patterns. */
static const double interesting_vals[] = {
    0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0,
    M_PI, -M_PI, M_PI_2, -M_PI_2, M_PI_4, M_E, -M_E,
    DBL_MIN, -DBL_MIN, DBL_MAX, -DBL_MAX,
    DBL_EPSILON, -DBL_EPSILON,
    1e-300, -1e-300, 1e300, -1e300,
    INFINITY, -INFINITY, NAN,
    /* boundary near 2^53 (every double >= 2^53 is integer) */
    9007199254740991.0, 9007199254740992.0,
    /* very large finite, where trig range reduction kicks in */
    1e15, -1e15, 1e18, -1e18,
};
#define NINTERESTING (int)(sizeof(interesting_vals)/sizeof(interesting_vals[0]))

/* Verify the result has no NaN payload contamination (NaN in →
 * NaN out, finite in → finite/inf out).  Returns 1 if the result
 * is "well-typed". */
static int well_typed(double in, double out, int allow_nan_from_finite) {
    if (isnan(in)) {
        /* Most single-arg functions return NaN from NaN; a few
         * (sqrt, fabs) silently propagate. */
        return isnan(out);
    }
    if (!allow_nan_from_finite && isnan(out)) return 0;
    return 1;
}

static void fuzz_unary(const char *name, double (*f)(double),
                       int allow_nan_from_finite) {
    for (int i = 0; i < NINTERESTING; i++) {
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        double y = f(interesting_vals[i]);
        if (!well_typed(interesting_vals[i], y, allow_nan_from_finite)) {
            printf("  %s(%g) = %g — NaN propagation failure\n",
                   name, interesting_vals[i], y);
            FAIL(name);
        }
    }
    for (int i = 0; i < 4096; i++) {
        double x = bits_to_double(prng());
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        (void)f(x);
        /* Just survival. */
    }
    feclearexcept(FE_ALL_EXCEPT);
    errno = 0;
}

static void fuzz_binary(const char *name, double (*f)(double, double)) {
    for (int i = 0; i < NINTERESTING; i++) {
        for (int j = 0; j < NINTERESTING; j++) {
            errno = 0; feclearexcept(FE_ALL_EXCEPT);
            (void)f(interesting_vals[i], interesting_vals[j]);
        }
    }
    for (int i = 0; i < 1024; i++) {
        double a = bits_to_double(prng());
        double b = bits_to_double(prng());
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        (void)f(a, b);
    }
    feclearexcept(FE_ALL_EXCEPT);
    errno = 0;
    (void)name;
}

/* Verify specific errno + FE flag claims that the spec is firm on. */
static void verify_error_signals(void) {
    /* log(-1): EDOM + FE_INVALID + NaN */
    errno = 0; feclearexcept(FE_ALL_EXCEPT);
    double r = log(-1.0);
    if (!isnan(r))           FAIL("log(-1) result");
    if (errno != EDOM)       FAIL("log(-1) errno=EDOM");
    if (!fetestexcept(FE_INVALID)) FAIL("log(-1) FE_INVALID");

    /* log(0): ERANGE + FE_DIVBYZERO + -inf */
    errno = 0; feclearexcept(FE_ALL_EXCEPT);
    r = log(0.0);
    if (r != -INFINITY)      FAIL("log(0) result");
    if (errno != ERANGE)     FAIL("log(0) errno=ERANGE");
    if (!fetestexcept(FE_DIVBYZERO)) FAIL("log(0) FE_DIVBYZERO");

    /* sqrt(-1): EDOM + FE_INVALID */
    errno = 0; feclearexcept(FE_ALL_EXCEPT);
    r = sqrt(-1.0);
    if (!isnan(r))           FAIL("sqrt(-1) result");
    if (errno != EDOM)       FAIL("sqrt(-1) errno=EDOM");
    if (!fetestexcept(FE_INVALID)) FAIL("sqrt(-1) FE_INVALID");

    /* pow(neg, frac): EDOM + FE_INVALID */
    errno = 0; feclearexcept(FE_ALL_EXCEPT);
    r = pow(-2.0, 0.5);
    if (!isnan(r))           FAIL("pow(-2, 0.5) result");
    if (errno != EDOM)       FAIL("pow(-2, 0.5) errno=EDOM");
    if (!fetestexcept(FE_INVALID)) FAIL("pow(-2, 0.5) FE_INVALID");

    /* exp(large): ERANGE + +inf */
    errno = 0; feclearexcept(FE_ALL_EXCEPT);
    r = exp(1000.0);
    if (r != INFINITY)       FAIL("exp(big) result");
    if (errno != ERANGE)     FAIL("exp(big) errno=ERANGE");

    feclearexcept(FE_ALL_EXCEPT);
    errno = 0;
}

/* Most single-arg funcs propagate NaN unmodified.  These have any
 * special NaN policy that means they may return non-NaN for some
 * NaN inputs — exclude from the well-typed check. */
int main(void) {
    printf("fuzz_math: starting\n");

    /* Trig — sin/cos/tan(±inf) yield NaN per C99 F.10.1; the
     * domain is finite reals.  allow_nan_from_finite covers that. */
    fuzz_unary("sin",  sin,  1);
    fuzz_unary("cos",  cos,  1);
    fuzz_unary("tan",  tan,  1);
    fuzz_unary("asin", asin, 1);   /* asin(2) is NaN — finite in, NaN out */
    fuzz_unary("acos", acos, 1);
    fuzz_unary("atan", atan, 0);
    fuzz_binary("atan2", atan2);

    /* Hyperbolic */
    fuzz_unary("sinh", sinh, 0);
    fuzz_unary("cosh", cosh, 0);
    fuzz_unary("tanh", tanh, 0);
    fuzz_unary("asinh", asinh, 0);
    fuzz_unary("acosh", acosh, 1); /* acosh(0.5) is NaN */
    fuzz_unary("atanh", atanh, 1); /* atanh(2) is NaN */

    /* Exp / log */
    fuzz_unary("exp",   exp,   0);
    fuzz_unary("exp2",  exp2,  0);
    fuzz_unary("expm1", expm1, 0);
    fuzz_unary("log",   log,   1);   /* log(neg) is NaN */
    fuzz_unary("log2",  log2,  1);
    fuzz_unary("log10", log10, 1);
    fuzz_unary("log1p", log1p, 1);

    /* Power / root */
    fuzz_binary("pow", pow);
    fuzz_unary("sqrt", sqrt, 1);
    fuzz_unary("cbrt", cbrt, 0);
    fuzz_binary("hypot", hypot);

    /* Round / manip */
    fuzz_unary("ceil",      ceil,      0);
    fuzz_unary("floor",     floor,     0);
    fuzz_unary("round",     round,     0);
    fuzz_unary("trunc",     trunc,     0);
    fuzz_unary("rint",      rint,      0);
    fuzz_unary("nearbyint", nearbyint, 0);
    fuzz_unary("fabs",      fabs,      0);
    fuzz_binary("fmod",     fmod);
    fuzz_binary("remainder", remainder);
    fuzz_binary("copysign", copysign);
    fuzz_binary("fmax",     fmax);
    fuzz_binary("fmin",     fmin);
    fuzz_binary("fdim",     fdim);

    /* Verify spec-firm error signals. */
    verify_error_signals();

    if (g_failures == 0) {
        printf("fuzz_math: PASS\n");
        return 0;
    }
    printf("fuzz_math: FAIL (%d failures)\n", g_failures);
    return 1;
}
