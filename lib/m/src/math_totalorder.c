/*
 * math_totalorder.c — ISO C23 §7.12.11 IEEE 754-2019 total order and NaN
 *
 * Implements:
 *   totalorder{,f,l} — IEEE 754 totalOrder predicate
 *   totalordermag{,f,l} — totalOrder on |x| and |y|
 *   canonicalize{,f,l} — canonical encoding
 *   getpayload{,f,l} — NaN payload
 *   setpayload{,f,l} — quiet NaN with payload
 *   setpayloadsig{,f,l} — signaling NaN with payload
 *
 * Feature-test guard: __STDC_VERSION__ >= 202311L  (C23)
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

/* ============================================================
 * Helper: extract IEEE 754 bit fields from double
 * ============================================================ */

/* IEEE 754 double: 1 sign bit, 11 exponent bits, 52 mantissa bits.
 * NaN: exponent = all 1s (0x7FF), mantissa != 0.
 * qNaN: MSB of mantissa = 1
 * sNaN: MSB of mantissa = 0
 *
 * We access the raw bits via a union to avoid strict aliasing. */

static int get_double_sign(double x) {
    union { double f; uint64_t u; } u = { x };
    return (int)((u.u >> 63) & 1);
}

static uint64_t get_double_exp(double x) {
    union { double f; uint64_t u; } u = { x };
    return (u.u >> 52) & 0x7FF;
}

static uint64_t get_double_mant(double x) {
    union { double f; uint64_t u; } u = { x };
    return u.u & 0xFFFFFFFFFFFFFULL;
}

static int get_double_payload(double x) {
    /* Return the payload bits as a signed value.
     * For NaN: the 52-bit mantissa interpreted as a positive integer.
     * For non-NaN: return -1 (but this function is only called
     * after checking isnan()). */
    if (!isnan(x)) return -1;
    return (int)get_double_mant(x);
}

/* ============================================================
 * totalorder — IEEE 754 totalOrder predicate
 *
 * Returns nonzero iff x precedes-or-equals y in total order.
 * The total order sorts:
 *   1. All negative values (more negative = smaller)
 *   2. Negative zeros < positive zeros
 *   3. All positive values (smaller magnitude = smaller)
 *   4. NaNs (signaling < quiet, smaller payload < larger payload)
 *   5. +Inf
 *
 * This is quiet (raises no FE_INVALID) even for sNaN inputs.
 * ============================================================ */

/* Encode a finite double into a 64-bit total-order code.
 * The encoding preserves sign-magnitude comparison as unsigned integer:
 *   - Negative values: inverts all bits so they compare correctly
 *   - Positive values: sign bit + sign-magnitude (already correct)
 *   - +0 codes as 0, -0 codes as 0x8000000000000000 */
static uint64_t totalorder_code(double x) {
    uint64_t ux;
    memcpy(&ux, &x, sizeof(ux));
    int is_neg = (ux >> 63) & 1;
    if (is_neg)
        return ~ux;
    else
        return ux | ((uint64_t)1 << 63);
}

int totalorder(double x, double y) {
    /* The sign-magnitude transform in totalorder_code() yields a
     * monotone unsigned key for the COMPLETE IEEE 754 total order —
     * negative NaN < -Inf < ... < -0 < +0 < ... < +Inf < positive NaN,
     * with NaNs ordered by raw payload (and the sign-bit flip applied
     * uniformly).  No NaN special-casing is needed; the ad-hoc path
     * previously mis-ordered negative NaNs (it sorted by raw bits,
     * placing -NaN above finite values instead of below them). */
    return totalorder_code(x) <= totalorder_code(y);
}

int totalorderf(float x, float y) {
    return totalorder((double)x, (double)y);
}

int totalorderl(long double x, long double y) {
    return totalorder((double)x, (double)y);
}

/* ============================================================
 * totalordermag — totalOrder on |x| and |y|
 * ============================================================ */

int totalordermag(double x, double y) {
    /* Compare by magnitude (absolute value), ignoring sign */
    return totalorder(fabs(x), fabs(y));
}

int totalordermagf(float x, float y) {
    return totalordermag((double)x, (double)y);
}

int totalordermagl(long double x, long double y) {
    return totalordermag((double)x, (double)y);
}

/* ============================================================
 * canonicalize — produce canonical encoding
 *
 * For binary IEEE 754 formats (float, double), every value is
 * canonical (except that sNaN should be quietened).  Returns 0 on
 * success; returns nonzero if the input cannot be canonicalized.
 * For sNaN, the canonical form is the quiet NaN with the same
 * mantissa (setting the quiet bit).
 * ============================================================ */

double canonicalize(double x, double *cx) {
    int x_nan = (get_double_exp(x) == 0x7FF) && (get_double_mant(x) != 0);
    if (!x_nan) {
        *cx = x;
        return 0;
    }

    /* NaN: canonical form is quiet NaN (set mantissa MSB) */
    uint64_t ux;
    memcpy(&ux, &x, sizeof(ux));
    /* Set the quiet bit (MSB of mantissa) */
    ux |= (uint64_t)1 << 51;
    memcpy(cx, &ux, sizeof(*cx));
    return 0;
}

float canonicalizef(float x, float *cx) {
    union { float f; uint32_t u; } u = { x };
    int x_nan = ((u.u >> 23) & 0xFF) == 0xFF && ((u.u & 0x7FFFFF) != 0);
    if (!x_nan) {
        *cx = x;
        return 0;
    }
    u.u |= (uint32_t)1 << 22;
    *cx = u.f;
    return 0;
}

long double canonicalizel(long double x, long double *cx) {
    /* 80-bit extended precision on x86: 1 sign, 15 exp, 64 mantissa.
     * For binary formats, canonicalize is a copy; for signaling NaN,
     * set the quiet bit. We treat all long double as binary. */
    *cx = x;
    if (!isnanl(x)) return 0;
    /* Set the quiet bit (bit 63 of mantissa in 80-bit format) */
    union { long double f; uint8_t e[10]; } u = { x };
    u.e[9] |= 0x80;  /* Set the top bit of the mantissa */
    memcpy(cx, &u, sizeof(*cx));
    return 0;
}

/* ============================================================
 * getpayload — return the NaN payload
 * ============================================================ */

double getpayload(double *x) {
    if (!isnan(*x)) return -1.0;
    return (double)get_double_payload(*x);
}

double getpayloadf(float *x) {
    union { float f; uint32_t u; } u = { *x };
    int x_nan = ((u.u >> 23) & 0xFF) == 0xFF && ((u.u & 0x7FFFFF) != 0);
    if (!x_nan) return -1.0;
    return (double)(u.u & 0x7FFFFF);
}

double getpayloadl(long double *x) {
    if (!isnanl(*x)) return -1.0;
    /* 80-bit extended precision: mantissa bits [1..64] */
    union { long double f; uint8_t e[10]; } u = { *x };
    /* Mantissa is bytes 2-9 (big-endian in memory representation) */
    uint64_t mant;
    mant = ((uint64_t)u.e[2] << 56) | ((uint64_t)u.e[3] << 48) |
           ((uint64_t)u.e[4] << 40) | ((uint64_t)u.e[5] << 32) |
           ((uint64_t)u.e[6] << 24) | ((uint64_t)u.e[7] << 16) |
           ((uint64_t)u.e[8] << 8) | (uint64_t)u.e[9];
    return (double)mant;
}

/* ============================================================
 * setpayload — quiet NaN with integer payload
 * ============================================================ */

double setpayload(double *res, double x, int payload) {
    /* Build a quiet NaN with the given payload (mod 2^51).
     * Quiet bit is bit 51 of the mantissa (always set for qNaN).
     * Payload goes in bits 0-50 (51 bits). */
    uint64_t payload_mask = 0x000FFFFFFFFFFFFFULL;  /* 51 bits */
    if (payload < 0 || (uint32_t)payload > (uint32_t)payload_mask)
        return -1;

    /* Quiet NaN pattern: exp=0x7FF, quiet bit set, payload in low bits */
    uint64_t u = 0x7FF8000000000000ULL;  /* quiet-NaN with zero payload */
    u |= (uint32_t)payload & payload_mask;  /* apply payload */

    /* Apply sign of x */
    if (get_double_sign(x))
        u |= (uint64_t)1 << 63;

    memcpy(res, &u, sizeof(*res));
    return 0;
}

double setpayloadf(float *res, float x, int payload) {
    /* Float quiet NaN: exp=0xFF, quiet bit set (bit 22), payload in bits 0-21 */
    uint32_t payload_mask = 0x7FFFFF;  /* 23 bits (bit 22 = quiet bit) */
    if (payload < 0 || (uint32_t)payload > (uint32_t)payload_mask)
        return -1;

    uint32_t u = 0x7FC00000;  /* quiet-NaN with zero payload */
    u |= (uint32_t)payload & payload_mask;

    /* Apply sign of x */
    uint32_t sx;
    memcpy(&sx, &x, sizeof(sx));
    if (sx & (uint32_t)1 << 31)
        u |= (uint32_t)1 << 31;

    memcpy(res, &u, sizeof(*res));
    return 0;
}

double setpayloadsig(double *res, double x, int payload) {
    /* Build a signaling NaN: exp=0x7FF, quiet bit clear (bit 51=0),
     * payload in low bits. At least one mantissa bit must be set
     * to make it NaN (not infinity). */
    uint64_t payload_mask = 0x000FFFFFFFFFFFFFULL;  /* 51 bits */
    if (payload < 0 || (uint32_t)payload > (uint32_t)payload_mask)
        return -1;

    /* sNaN pattern: exp=0x7FF, quiet bit clear, payload in low bits.
     * If payload is zero, set bit 0 to ensure mantissa != 0. */
    uint64_t u = 0x7FF0000000000000ULL;  /* NaN with zero mantissa (will be sNaN) */
    uint64_t mant = (uint32_t)payload & payload_mask;
    if (mant == 0)
        mant = 1;  /* set lowest bit so it's NaN, not infinity */
    u |= mant;
    /* Quiet bit (bit 51) stays clear = signaling NaN */

    if (get_double_sign(x))
        u |= (uint64_t)1 << 63;

    memcpy(res, &u, sizeof(*res));
    return 0;
}

double setpayloadsigf(float *res, float x, int payload) {
    uint32_t u = 0x7F800000;  /* sNaN with zero payload (quiet bit clear) */
    uint32_t payload_mask = 0x3FFFFF;  /* 22 bits for payload (bit 22 = quiet bit) */
    if (payload < 0 || (uint32_t)payload > (uint32_t)payload_mask)
        return -1;

    uint32_t mant = (uint32_t)payload & payload_mask;
    if (mant == 0)
        mant = 1;  /* ensure mantissa != 0 so it's NaN, not infinity */
    u |= mant;

    /* Apply sign of x */
    uint32_t sx;
    memcpy(&sx, &x, sizeof(sx));
    if (sx & (uint32_t)1 << 31)
        u |= (uint32_t)1 << 31;

    memcpy(res, &u, sizeof(*res));
    return 0;
}

double setpayloadl(long double *res, long double x, int payload) {
    union { long double f; uint8_t e[10]; } u;
    /* 64 explicit mantissa bits in 80-bit long double.
     * payload is int, so mask to 64 bits to reject out-of-range. */
    uint64_t payload64 = (uint64_t)(uint32_t)payload;
    /* Reject: only use lower 64 bits; if user gave more, truncate silently.
     * (IEEE 754 payload width = 64 for long double.) */
    (void)payload64;  /* payload64 used below via individual byte extraction */

    memset(&u, 0, sizeof(u));
    u.e[9] = 0x7F;    /* high byte: exp high bits + mantissa MSB */
    u.e[8] = 0xFF;    /* exp low byte */
    u.e[9] |= 0x80;   /* set mantissa MSB (the explicit bit for normalized) */

    /* Apply payload to the lower 64 mantissa bits (bytes 0-7) */
    u.e[0] = (uint8_t)payload;
    u.e[1] = (uint8_t)(payload >> 8);
    u.e[2] = (uint8_t)(payload >> 16);
    u.e[3] = (uint8_t)(payload >> 24);
    /* Bytes 4-7 carry the high 32 bits of payload; cast to uint64_t first. */
    {
        uint64_t hi = (uint64_t)(uint32_t)payload;
        u.e[4] = (uint8_t)(hi >> 16);
        u.e[5] = (uint8_t)(hi >> 24);
        u.e[6] = 0;
        u.e[7] = 0;
    }

    /* Apply sign of x */
    union { long double f; uint8_t e[10]; } sx = { x };
    if (sx.e[9] & 0x80)
        u.e[9] |= 0x80;

    *res = u.f;
    return 0;
}

double setpayloadsigl(long double *res, long double x, int payload) {
    /* Same as setpayloadl but produce a signaling NaN */
    union { long double f; uint8_t e[10]; } u;

    memset(&u, 0, sizeof(u));
    u.e[9] = 0x7F;
    u.e[8] = 0xFF;
    /* Don't set the explicit bit (MSB) — this makes it signaling */

    u.e[0] = (uint8_t)(payload);
    u.e[1] = (uint8_t)(payload >> 8);
    u.e[2] = (uint8_t)(payload >> 16);
    u.e[3] = (uint8_t)(payload >> 24);
    {
        uint64_t hi = (uint64_t)(uint32_t)payload;
        u.e[4] = (uint8_t)(hi >> 16);
        u.e[5] = (uint8_t)(hi >> 24);
        u.e[6] = 0;
        u.e[7] = 0;
    }

    union { long double f; uint8_t e[10]; } sx = { x };
    if (sx.e[9] & 0x80)
        u.e[9] |= 0x80;

    *res = u.f;
    return 0;
}
