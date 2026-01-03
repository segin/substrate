#ifndef _SYS_MATH_H
#define _SYS_MATH_H

#include <stdint.h>

// Software 64-bit division/modulo by 32-bit divisor
// Avoids libgcc dependencies (__udivdi3, __umoddi3, etc.)

static inline int64_t div64_32(int64_t dividend, uint32_t divisor) {
    int64_t quotient = 0;
    int64_t remainder = 0;
    int negative = 0;

    if (dividend < 0) {
        negative = 1;
        dividend = -dividend;
    }

    for (int i = 63; i >= 0; i--) {
        remainder = (remainder << 1) | ((dividend >> i) & 1);
        if ((uint64_t)remainder >= divisor) {
            remainder -= divisor;
            quotient |= (1LL << i);
        }
    }

    return negative ? -quotient : quotient;
}

static inline uint64_t udiv64_32(uint64_t dividend, uint32_t divisor) {
    uint64_t quotient = 0;
    uint64_t remainder = 0;

    for (int i = 63; i >= 0; i--) {
        remainder = (remainder << 1) | ((dividend >> i) & 1);
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= (1ULL << i);
        }
    }

    return quotient;
}

static inline uint64_t umod64_32(uint64_t dividend, uint32_t divisor) {
    uint64_t quotient = udiv64_32(dividend, divisor);
    return dividend - (quotient * divisor);
}

static inline int64_t mod64_32(int64_t dividend, uint32_t divisor) {
    int64_t quotient = div64_32(dividend, divisor);
    return dividend - (quotient * divisor);
}

#endif
