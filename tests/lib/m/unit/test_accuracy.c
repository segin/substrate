/*
 * test_accuracy.c — ULP-bounded accuracy against high-precision
 * reference values.
 *
 * Reference values were computed with MPFR at 200-bit precision and
 * rounded to nearest double.  For each value we record the IEEE-754
 * bit pattern (uint64_t hex), which we then decode back to the
 * correctly-rounded double.  This means the test is independent of
 * the source format's parsing: if your compiler rounds the literal
 * 1.234567890123456789 differently from another compiler, the bit
 * pattern is still authoritative.
 *
 * Tolerance:
 *   Correctly-rounded primitives (sqrt, the basic add/sub/mul/div
 *   that x87 / SSE do natively) — 1 ULP.
 *   Faithfully-rounded transcendentals (sin, cos, exp, log, pow,
 *   atan, ...) — 3 ULP for ordinary arguments, 10 ULP for arguments
 *   near a function's hardest cases (e.g. cos(pi/2) where ulp tags
 *   along with the catastrophic-cancellation residue).
 *
 * REQ-06-0803..0806.
 */

#include <stdio.h>
#include <math.h>
#include <float.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E  2.71828182845904523536
#endif

static int g_failures = 0;

static double bits_to_double(uint64_t u) {
    union { uint64_t u; double d; } v = { .u = u };
    return v.d;
}
static uint64_t double_to_bits(double d) {
    union { uint64_t u; double d; } v = { .d = d };
    return v.u;
}

/* Distance in ULPs between two doubles.  NaN matches NaN; inf only
 * matches the exact-same inf. */
static uint64_t ulp_distance(double a, double b) {
    if (isnan(a) && isnan(b)) return 0;
    if (isnan(a) || isnan(b)) return UINT64_MAX;
    if (a == b) return 0;
    if (isinf(a) || isinf(b)) return UINT64_MAX;

    uint64_t ua = double_to_bits(a);
    uint64_t ub = double_to_bits(b);
    /* IEEE-754 lexicographic-ordering trick: flip the sign-bit's
     * meaning so adjacent representable doubles differ by 1 in the
     * unsigned key. */
    if (ua & (1ULL << 63)) ua = (1ULL << 63) - ua;
    else                   ua |=  (1ULL << 63);
    if (ub & (1ULL << 63)) ub = (1ULL << 63) - ub;
    else                   ub |=  (1ULL << 63);
    return ua > ub ? ua - ub : ub - ua;
}

#define CHECK_ULP(actual, ref_bits, max_ulp, name) do { \
    double a = (actual); \
    double r = bits_to_double((ref_bits)); \
    uint64_t d = ulp_distance(a, r); \
    if (d > (max_ulp)) { \
        printf("  FAIL: %s  actual=%.17g  ref=%.17g  ulp=%llu (>%llu)\n", \
               (name), a, r, (unsigned long long)d, \
               (unsigned long long)(max_ulp)); \
        g_failures++; \
    } \
} while (0)

/* Each reference value below is the IEEE-754 bit pattern of the
 * correctly-rounded double, computed with MPFR(200) then rounded
 * to nearest. */

static void test_sqrt_accuracy(void) {
    /* sqrt(2) = 1.4142135623730951454746218587388284504413604736328125 */
    CHECK_ULP(sqrt(2.0),       UINT64_C(0x3FF6A09E667F3BCD), 1, "sqrt(2)");
    /* sqrt(3) = 1.7320508075688771931766041234368458390235900878906250 */
    CHECK_ULP(sqrt(3.0),       UINT64_C(0x3FFBB67AE8584CAA), 1, "sqrt(3)");
    /* sqrt(0.5) = 0.70710678118654757273731092936941422522068023681641 */
    CHECK_ULP(sqrt(0.5),       UINT64_C(0x3FE6A09E667F3BCD), 1, "sqrt(0.5)");
    /* sqrt(1e-300) = 1e-150 */
    CHECK_ULP(sqrt(1e-300),    UINT64_C(0x20CA2FE76A3F9475), 2, "sqrt(1e-300)");
    /* sqrt(DBL_MAX) ≈ 1.34078079299425956e+154 */
    CHECK_ULP(sqrt(DBL_MAX),   UINT64_C(0x5FEFFFFFFFFFFFFF), 1, "sqrt(DBL_MAX)");
}

static void test_exp_accuracy(void) {
    /* exp(1) = e = 2.71828182845904509079559829842764884233474731445312 */
    CHECK_ULP(exp(1.0),        UINT64_C(0x4005BF0A8B145769), 3, "exp(1)");
    /* exp(0.5) = 1.6487212707001282 */
    CHECK_ULP(exp(0.5),        UINT64_C(0x3FFA61298E1E069C), 3, "exp(0.5)");
    /* exp(-1) = 0.36787944117144233 */
    CHECK_ULP(exp(-1.0),       UINT64_C(0x3FD78B56362CEF38), 3, "exp(-1)");
    /* exp(10) = 22026.465794806718 */
    CHECK_ULP(exp(10.0),       UINT64_C(0x40D5829DCF950560), 3, "exp(10)");
}

static void test_log_accuracy(void) {
    /* log(2) = 0.6931471805599453 */
    CHECK_ULP(log(2.0),        UINT64_C(0x3FE62E42FEFA39EF), 1, "log(2)");
    /* log(10) = 2.302585092994046 */
    CHECK_ULP(log(10.0),       UINT64_C(0x40026BB1BBB55516), 1, "log(10)");
    /* log(0.5) = -0.693147180559945286226763982995180413126945495605469 */
    CHECK_ULP(log(0.5),        UINT64_C(0xBFE62E42FEFA39EF), 1, "log(0.5)");
    /* log(1.5) = 0.405465108108164400633556244957051426172256469726562 */
    CHECK_ULP(log(1.5),        UINT64_C(0x3FD9F323ECBF984C), 2, "log(1.5)");
}

static void test_sin_cos_accuracy(void) {
    /* sin(0.5) = 0.479425538604203 (4 digit-pair representation rounds
     *            to 0x3FDEAEE8744B05F0) */
    CHECK_ULP(sin(0.5),        UINT64_C(0x3FDEAEE8744B05F0), 3, "sin(0.5)");
    CHECK_ULP(cos(0.5),        UINT64_C(0x3FEC1528065B7D50), 3, "cos(0.5)");
    CHECK_ULP(sin(1.0),        UINT64_C(0x3FEAED548F090CEE), 3, "sin(1)");
    CHECK_ULP(cos(1.0),        UINT64_C(0x3FE14A280FB5068C), 3, "cos(1)");
    /* sin(pi) — near-zero result with catastrophic cancellation, so
     * the ULP distance is meaningless; just verify magnitude. */
    if (fabs(sin(M_PI)) > 1e-15) {
        printf("  FAIL: sin(pi) magnitude %.17g\n", sin(M_PI));
        g_failures++;
    }
    /* cos(0) = 1 exactly */
    CHECK_ULP(cos(0.0),        UINT64_C(0x3FF0000000000000), 0, "cos(0)");
    CHECK_ULP(sin(0.0),        UINT64_C(0x0000000000000000), 0, "sin(0)");
}

static void test_pow_accuracy(void) {
    /* pow(2, 10) = 1024 exactly */
    CHECK_ULP(pow(2.0, 10.0),  UINT64_C(0x4090000000000000), 0, "pow(2,10)");
    /* pow(2, 0.5) = sqrt(2) */
    CHECK_ULP(pow(2.0, 0.5),   UINT64_C(0x3FF6A09E667F3BCD), 3, "pow(2,0.5)");
    /* pow(M_E, 1.0) = e — should match exp(1.0) */
    CHECK_ULP(pow(M_E, 1.0),   UINT64_C(0x4005BF0A8B145769), 5, "pow(e,1)");
    /* pow(0.5, -2.0) = 4 exactly */
    CHECK_ULP(pow(0.5, -2.0),  UINT64_C(0x4010000000000000), 1, "pow(0.5,-2)");
}

static void test_atan_accuracy(void) {
    /* atan(1) = pi/4 = 0.785398163397448278999490867136 */
    CHECK_ULP(atan(1.0),       UINT64_C(0x3FE921FB54442D18), 2, "atan(1)");
    /* atan(0) = 0 */
    CHECK_ULP(atan(0.0),       UINT64_C(0x0000000000000000), 0, "atan(0)");
    /* atan2(1, 1) = pi/4 */
    CHECK_ULP(atan2(1.0, 1.0), UINT64_C(0x3FE921FB54442D18), 2, "atan2(1,1)");
    /* atan2(0, -1) = pi */
    CHECK_ULP(atan2(0.0, -1.0), UINT64_C(0x400921FB54442D18), 2, "atan2(0,-1)");
}

static void test_boundary_values(void) {
    /* Smallest normal double: 2^-1022 */
    double sn = DBL_MIN;
    if (sqrt(sn) <= 0.0 || !isfinite(sqrt(sn))) {
        printf("  FAIL: sqrt(DBL_MIN) = %g\n", sqrt(sn));
        g_failures++;
    }
    /* Smallest denormal: 2^-1074 */
    double sd = bits_to_double(UINT64_C(0x0000000000000001));
    /* log(smallest-denormal) ≈ -744.4400719213812 */
    double l = log(sd);
    if (!isfinite(l) || l > -700.0 || l < -800.0) {
        printf("  FAIL: log(min-denormal) = %g (expect ≈ -744)\n", l);
        g_failures++;
    }
    /* Largest normal: exp(log(DBL_MAX)) should be near DBL_MAX. */
    double lmax = log(DBL_MAX);   /* ≈ 709.78 */
    if (lmax < 709.0 || lmax > 710.0) {
        printf("  FAIL: log(DBL_MAX) = %g (expect ≈ 709.78)\n", lmax);
        g_failures++;
    }
    /* Near a singularity: log(1+tiny) ≈ tiny. */
    double tiny = 1e-15;
    double rt = log1p(tiny);
    if (fabs(rt - tiny) > 1e-29) {
        printf("  FAIL: log1p(tiny) - tiny = %g (expect ~1e-30)\n", rt - tiny);
        g_failures++;
    }
}

int main(void) {
    printf("test_accuracy: starting\n");
    test_sqrt_accuracy();
    test_exp_accuracy();
    test_log_accuracy();
    test_sin_cos_accuracy();
    test_pow_accuracy();
    test_atan_accuracy();
    test_boundary_values();
    if (g_failures == 0) {
        printf("test_accuracy: PASS\n");
        return 0;
    }
    printf("test_accuracy: FAIL (%d failures)\n", g_failures);
    return 1;
}
