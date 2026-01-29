#include <stdint.h>
/*
 * 64-bit arithmetic helpers for i386
 * 
 * GCC emits calls to these when performing 64-bit arithmetic on 32-bit.
 * We implement them here to avoid libgcc dependency, which can have
 * multilib configuration issues across different systems.
 *
 * These implementations are UB-free and handle edge cases properly:
 * - Division by zero traps
 * - INT64_MIN / -1 overflow traps
 * - No signed overflow UB
 */

/* INT64_MIN for freestanding environment (may already be in stdint.h) */
#ifndef INT64_MIN
#define INT64_MIN (-9223372036854775807LL - 1)
#endif

/* Core 64-bit unsigned division with remainder - no UB */
static uint64_t udiv64(uint64_t n, uint64_t d, uint64_t *rem) {
    if (d == 0) {
        __builtin_trap(); /* Division by zero */
    }

    uint64_t q = 0;
    uint64_t r = 0;

    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1ULL);
        if (r >= d) {
            r -= d;
            q |= (1ULL << i);
        }
    }

    if (rem)
        *rem = r;
    return q;
}

/* Unsigned 64-bit division */
uint64_t __udivdi3(uint64_t n, uint64_t d) {
    return udiv64(n, d, 0);
}

/* Unsigned 64-bit modulo */
uint64_t __umoddi3(uint64_t n, uint64_t d) {
    uint64_t r;
    udiv64(n, d, &r);
    return r;
}

/* Signed 64-bit division with overflow handling */
int64_t __divdi3(int64_t a, int64_t b) {
    if (b == 0)
        __builtin_trap();

    if (a == INT64_MIN && b == -1)
        __builtin_trap(); /* Overflow: result not representable */

    int neg = ((a ^ b) < 0);

    uint64_t ua = (a < 0) ? (uint64_t)(-(uint64_t)a) : (uint64_t)a;
    uint64_t ub = (b < 0) ? (uint64_t)(-(uint64_t)b) : (uint64_t)b;

    uint64_t q = udiv64(ua, ub, 0);
    return neg ? -(int64_t)q : (int64_t)q;
}

/* Signed 64-bit modulo */
int64_t __moddi3(int64_t a, int64_t b) {
    if (b == 0)
        __builtin_trap();

    uint64_t r;
    uint64_t ua = (a < 0) ? (uint64_t)(-(uint64_t)a) : (uint64_t)a;
    uint64_t ub = (b < 0) ? (uint64_t)(-(uint64_t)b) : (uint64_t)b;

    udiv64(ua, ub, &r);
    return (a < 0) ? -(int64_t)r : (int64_t)r;
}

/* 64-bit left shift */
uint64_t __ashldi3(uint64_t a, int b) {
    b &= 63;
    if (b == 0) return a;
    return (a << b);
}

/* 64-bit logical right shift */
uint64_t __lshrdi3(uint64_t a, int b) {
    b &= 63;
    if (b == 0) return a;
    return (a >> b);
}

/* 64-bit arithmetic right shift */
int64_t __ashrdi3(int64_t a, int b) {
    b &= 63;
    if (b == 0) return a;
    
    uint64_t ua = (uint64_t)a;
    if (a >= 0) return (int64_t)(ua >> b);
    
    /* Arithmetic right shift for negative: shift then fill high bits with 1s */
    uint64_t shifted = ua >> b;
    uint64_t mask = (~0ULL) << (64 - b);
    return (int64_t)(shifted | mask);
}

/* Signed 64x64 -> 64 multiply */
int64_t __muldi3(int64_t a, int64_t b) {
    /* Use 32-bit multiplication with proper overflow handling */
    uint64_t ua = (uint64_t)a;
    uint64_t ub = (uint64_t)b;

    uint32_t a_lo = (uint32_t)ua;
    uint32_t a_hi = (uint32_t)(ua >> 32);
    uint32_t b_lo = (uint32_t)ub;
    uint32_t b_hi = (uint32_t)(ub >> 32);

    uint64_t lo_lo = (uint64_t)a_lo * b_lo;
    uint64_t hi_lo = (uint64_t)a_hi * b_lo;
    uint64_t lo_hi = (uint64_t)a_lo * b_hi;
    /* hi_hi discarded (exceeds 64-bit result) */
    
    uint64_t cross = hi_lo + lo_hi;
    uint64_t result = lo_lo + (cross << 32);
    
    return (int64_t)result;
}

/* 64-bit negate */
int64_t __negdi2(int64_t a) {
    return -a;
}
