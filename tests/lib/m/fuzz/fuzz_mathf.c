/*
 * fuzz_mathf.c — fuzz every float-precision libm entry point.
 *
 * REQ-06-0801, REQ-06-0802.  Same shape as fuzz_math.c but for the
 * `f`-suffixed single-precision family.
 */

#include <stdio.h>
#include <math.h>
#include <fenv.h>
#include <errno.h>
#include <float.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E  2.71828182845904523536
#endif

static int g_failures = 0;
#define FAIL(name) do { printf("  FAIL: %s (line %d)\n", (name), __LINE__); g_failures++; } while (0)

static float bits_to_float(uint32_t u) {
    union { uint32_t u; float f; } v = { .u = u };
    return v.f;
}

static uint32_t prng_state = 0xC0FFEE42u;
static uint32_t prng(void) {
    /* xorshift32 */
    uint32_t x = prng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return prng_state = x;
}

static const float interesting_f[] = {
    0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.0f, -2.0f,
    (float)M_PI, (float)-M_PI, (float)M_E,
    FLT_MIN, -FLT_MIN, FLT_MAX, -FLT_MAX,
    FLT_EPSILON, -FLT_EPSILON,
    1e-30f, -1e-30f, 1e30f, -1e30f,
    INFINITY, -INFINITY, NAN,
    16777215.0f, 16777216.0f,        /* boundary near 2^24 */
    1e10f, -1e10f,                   /* fprem1 reduction zone */
};
#define NINTERESTING_F (int)(sizeof(interesting_f)/sizeof(interesting_f[0]))

static int well_typed_f(float in, float out, int allow_nan_from_finite) {
    if (isnan(in)) return isnan(out);
    if (!allow_nan_from_finite && isnan(out)) return 0;
    return 1;
}

static void fuzz_unary_f(const char *name, float (*f)(float),
                         int allow_nan_from_finite) {
    for (int i = 0; i < NINTERESTING_F; i++) {
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        float y = f(interesting_f[i]);
        if (!well_typed_f(interesting_f[i], y, allow_nan_from_finite)) {
            printf("  %s(%g) = %g — NaN propagation failure\n",
                   name, interesting_f[i], y);
            FAIL(name);
        }
    }
    for (int i = 0; i < 4096; i++) {
        float x = bits_to_float(prng());
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        (void)f(x);
    }
    feclearexcept(FE_ALL_EXCEPT);
    errno = 0;
}

static void fuzz_binary_f(const char *name, float (*f)(float, float)) {
    for (int i = 0; i < NINTERESTING_F; i++)
        for (int j = 0; j < NINTERESTING_F; j++) {
            errno = 0; feclearexcept(FE_ALL_EXCEPT);
            (void)f(interesting_f[i], interesting_f[j]);
        }
    for (int i = 0; i < 1024; i++) {
        float a = bits_to_float(prng());
        float b = bits_to_float(prng());
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        (void)f(a, b);
    }
    feclearexcept(FE_ALL_EXCEPT);
    errno = 0;
    (void)name;
}

int main(void) {
    printf("fuzz_mathf: starting\n");

    fuzz_unary_f("sinf",  sinf,  1);   /* sinf(inf) = NaN per C99 F.10.1 */
    fuzz_unary_f("cosf",  cosf,  1);
    fuzz_unary_f("tanf",  tanf,  1);
    fuzz_unary_f("asinf", asinf, 1);
    fuzz_unary_f("acosf", acosf, 1);
    fuzz_unary_f("atanf", atanf, 0);
    fuzz_binary_f("atan2f", atan2f);

    fuzz_unary_f("sinhf",  sinhf,  0);
    fuzz_unary_f("coshf",  coshf,  0);
    fuzz_unary_f("tanhf",  tanhf,  0);
    fuzz_unary_f("asinhf", asinhf, 0);
    fuzz_unary_f("acoshf", acoshf, 1);
    fuzz_unary_f("atanhf", atanhf, 1);

    fuzz_unary_f("expf",   expf,   0);
    fuzz_unary_f("logf",   logf,   1);
    fuzz_unary_f("sqrtf",  sqrtf,  1);
    fuzz_unary_f("cbrtf",  cbrtf,  0);
    fuzz_binary_f("hypotf", hypotf);

    fuzz_unary_f("ceilf",      ceilf,      0);
    fuzz_unary_f("floorf",     floorf,     0);
    fuzz_unary_f("roundf",     roundf,     0);
    fuzz_unary_f("truncf",     truncf,     0);
    fuzz_unary_f("rintf",      rintf,      0);
    fuzz_unary_f("nearbyintf", nearbyintf, 0);
    fuzz_unary_f("fabsf",      fabsf,      0);
    fuzz_binary_f("fmodf",     fmodf);
    fuzz_binary_f("copysignf", copysignf);
    fuzz_binary_f("fmaxf",     fmaxf);
    fuzz_binary_f("fminf",     fminf);

    if (g_failures == 0) {
        printf("fuzz_mathf: PASS\n");
        return 0;
    }
    printf("fuzz_mathf: FAIL (%d failures)\n", g_failures);
    return 1;
}
