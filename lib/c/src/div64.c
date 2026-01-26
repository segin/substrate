#include <stdint.h>

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
