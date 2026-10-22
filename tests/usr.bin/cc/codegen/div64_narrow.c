/*
 * A 64-bit division or remainder of a zero-extended 32-bit value by a
 * constant from 1 to 2^31 - 1 is one 32-bit unsigned division on i386,
 * as gcc narrows it from the value's range, with no call of libgcc's
 * __divdi3 or __udivdi3: Linux's mm/backing-dev.c divides
 * `(long long)bdi->min_ratio` by BDI_RATIO_SCALE, and does not link
 * libgcc.  run_div64_pow2.sh checks there is no call; this the values,
 * the dividend up to 2^32 - 1.
 */
#include <stdio.h>

static volatile unsigned int xs[] = {0u, 1u, 9999u, 10000u, 123456789u, 0x7fffffffu, 0x80000000u, 0xffffffffu};

int main(void) {
    size_t i;

    for (i = 0; i < sizeof(xs) / sizeof(xs[0]); ++i) {
        unsigned int x = xs[i];

        printf("%lld %lld %llu %llu %lld\n", (long long)x / 10000, (long long)x % 7, (unsigned long long)x / 3,
               (unsigned long long)x % 1000003, (long long)x / 2147483647);
    }
    return 0;
}
