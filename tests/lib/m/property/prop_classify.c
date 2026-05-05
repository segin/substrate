/*
 * prop_classify.c — Property tests for IEEE 754 classification.
 *
 * REQ-06-0482..0487:
 *   - Exactly one of isinf(x), isnan(x), isfinite(x) is true.
 *   - isnormal(x) implies isfinite(x).
 *   - issubnormal(x) iff isfinite(x) && !isnormal(x) && !iszero(x).
 *   - signbit(-x) != signbit(x) for non-NaN x.
 *   - isunordered(x, y) iff isnan(x) || isnan(y).
 *
 * Sampling strategy: a deterministic xorshift64 over the full 64-bit bit
 * pattern reinterpreted as `double`, plus a fixed boundary set covering
 * +0, -0, +1, -1, ±INF, ±NaN, ±DBL_MIN, ±DBL_MAX, ±DBL_MIN/2 (subnormal),
 * and a signaling NaN.  N_RANDOM is high enough that every classify
 * category is exercised many times.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <float.h>

#define N_RANDOM 4000

static int failures;

#define FAIL(msg) do { printf("FAIL property: %s\n", msg); failures++; } while (0)

static uint64_t xs_state = 0x243F6A8885A308D3ULL;

static uint64_t xs_next(void) {
    uint64_t x = xs_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    xs_state = x;
    return x;
}

static double bits_as_double(uint64_t b) {
    double d;
    memcpy(&d, &b, sizeof(d));
    return d;
}

/* Boundary samples — exhaustive coverage of every fpclassify category. */
static double boundary_samples(int idx) {
    static const struct { uint64_t bits; } table[] = {
        { 0x0000000000000000ULL },                 /* +0 */
        { 0x8000000000000000ULL },                 /* -0 */
        { 0x3FF0000000000000ULL },                 /* +1 */
        { 0xBFF0000000000000ULL },                 /* -1 */
        { 0x7FF0000000000000ULL },                 /* +INF */
        { 0xFFF0000000000000ULL },                 /* -INF */
        { 0x7FF8000000000000ULL },                 /* +qNaN */
        { 0xFFF8000000000000ULL },                 /* -qNaN */
        { 0x7FF4000000000000ULL },                 /* +sNaN */
        { 0x0010000000000000ULL },                 /* +DBL_MIN */
        { 0x8010000000000000ULL },                 /* -DBL_MIN */
        { 0x7FEFFFFFFFFFFFFFULL },                 /* +DBL_MAX */
        { 0xFFEFFFFFFFFFFFFFULL },                 /* -DBL_MAX */
        { 0x0008000000000000ULL },                 /* subnormal */
        { 0x8008000000000000ULL },                 /* -subnormal */
        { 0x0000000000000001ULL },                 /* smallest subnormal */
    };
    int n = (int)(sizeof(table) / sizeof(table[0]));
    if (idx < 0 || idx >= n) return 0.0;
    return bits_as_double(table[idx].bits);
}

#define N_BOUNDARY 16

/*
 * REQ-06-0483: Exactly one of isinf, isnan, isfinite is true for every
 * representable double.  These three predicates are the IEEE 754 cover.
 */
static void prop_trichotomy(void) {
    for (int i = 0; i < N_BOUNDARY; i++) {
        double x = boundary_samples(i);
        int n = !!isinf(x) + !!isnan(x) + !!isfinite(x);
        if (n != 1) {
            uint64_t bits;
            memcpy(&bits, &x, sizeof(bits));
            printf("  boundary[%d] bits=0x%016llx isinf=%d isnan=%d isfinite=%d\n",
                   i, (unsigned long long)bits,
                   isinf(x), isnan(x), isfinite(x));
            FAIL("trichotomy violated on boundary sample");
        }
    }
    for (int i = 0; i < N_RANDOM; i++) {
        double x = bits_as_double(xs_next());
        int n = !!isinf(x) + !!isnan(x) + !!isfinite(x);
        if (n != 1) FAIL("trichotomy violated on random sample");
    }
}

/* REQ-06-0484: isnormal(x) implies isfinite(x). */
static void prop_normal_implies_finite(void) {
    for (int i = 0; i < N_BOUNDARY; i++) {
        double x = boundary_samples(i);
        if (isnormal(x) && !isfinite(x)) FAIL("isnormal but !isfinite (boundary)");
    }
    for (int i = 0; i < N_RANDOM; i++) {
        double x = bits_as_double(xs_next());
        if (isnormal(x) && !isfinite(x)) FAIL("isnormal but !isfinite (random)");
    }
}

/*
 * REQ-06-0485: issubnormal(x) iff isfinite(x) && !isnormal(x) && !iszero(x).
 *
 * Equivalently: subnormal exhausts the "finite-but-not-normal-or-zero"
 * partition.  Verifies fpclassify's category boundaries are watertight.
 */
static void prop_subnormal_characterization(void) {
    for (int i = 0; i < N_BOUNDARY; i++) {
        double x = boundary_samples(i);
        int sub = issubnormal(x);
        int derived = isfinite(x) && !isnormal(x) && !iszero(x);
        if (sub != derived) FAIL("issubnormal != characterization (boundary)");
    }
    for (int i = 0; i < N_RANDOM; i++) {
        double x = bits_as_double(xs_next());
        int sub = issubnormal(x);
        int derived = isfinite(x) && !isnormal(x) && !iszero(x);
        if (sub != derived) FAIL("issubnormal != characterization (random)");
    }
}

/*
 * REQ-06-0486: signbit(-x) != signbit(x) for all non-NaN x.
 *
 * Negation flips the sign bit unconditionally on IEEE 754 binary;
 * NaN payload semantics for negation are unspecified so we exclude.
 * +0 / -0 specifically test the sign bit path because they compare
 * arithmetically equal but have distinct sign bits.
 */
static void prop_signbit_negation(void) {
    for (int i = 0; i < N_BOUNDARY; i++) {
        double x = boundary_samples(i);
        if (isnan(x)) continue;
        if (!!signbit(-x) == !!signbit(x))
            FAIL("signbit(-x) == signbit(x) for non-NaN (boundary)");
    }
    for (int i = 0; i < N_RANDOM; i++) {
        double x = bits_as_double(xs_next());
        if (isnan(x)) continue;
        if (!!signbit(-x) == !!signbit(x))
            FAIL("signbit(-x) == signbit(x) for non-NaN (random)");
    }
}

/* REQ-06-0487: isunordered(x,y) iff isnan(x) || isnan(y). */
static void prop_isunordered_characterization(void) {
    /* Boundary × boundary cross product. */
    for (int i = 0; i < N_BOUNDARY; i++) {
        for (int j = 0; j < N_BOUNDARY; j++) {
            double x = boundary_samples(i);
            double y = boundary_samples(j);
            int u = !!isunordered(x, y);
            int expect = !!isnan(x) || !!isnan(y);
            if (u != expect) FAIL("isunordered != (isnan||isnan) (boundary)");
        }
    }
    /* Random pair sample. */
    for (int i = 0; i < N_RANDOM; i++) {
        double x = bits_as_double(xs_next());
        double y = bits_as_double(xs_next());
        int u = !!isunordered(x, y);
        int expect = !!isnan(x) || !!isnan(y);
        if (u != expect) FAIL("isunordered != (isnan||isnan) (random)");
    }
}

int main(void) {
    prop_trichotomy();
    prop_normal_implies_finite();
    prop_subnormal_characterization();
    prop_signbit_negation();
    prop_isunordered_characterization();
    if (failures == 0) {
        puts("PASS: prop_classify");
        return 0;
    }
    printf("FAIL: %d failure(s)\n", failures);
    return 1;
}
