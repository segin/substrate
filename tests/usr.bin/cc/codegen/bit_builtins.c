/*
 * __builtin_popcount/ctz/clz/ffs on values only known at run
 * time, for 16-, 32- and 64-bit operands.  On i386 the popcount once
 * summed its bytes with a shift by 32, which the hardware takes as 0, so
 * every count doubled; and ctzll mixed 32-bit constants into 64-bit
 * arithmetic, leaving the high half of the result undefined.
 */
#include <stdio.h>

static volatile unsigned long long seeds[] = {
    0, 1, 2, 3, 0x10, 0xff, 0x8001, 0xffff, 0x12345678, 0x80000000u, 0xffffffffu,
    0x100000000ull, 1ull << 40, 0xf0f0f0f0f0f0f0f0ull, 0x8000000000000000ull, ~0ull,
};

int main(void) {
    unsigned i;

    for (i = 0; i < sizeof(seeds) / sizeof(seeds[0]); ++i) {
        unsigned long long v = seeds[i];
        unsigned u = (unsigned)v;
        unsigned short s = (unsigned short)v;
        unsigned long l = (unsigned long)v;

        printf("%llx: pc %d %d %d %d", v, __builtin_popcount(s), __builtin_popcount(u), __builtin_popcountl(l),
               __builtin_popcountll(v));
        printf(" ffs %d %d", __builtin_ffs((int)u), __builtin_ffsll((long long)v));
        if (u != 0) {
            printf(" ctz %d clz %d", __builtin_ctz(u), __builtin_clz(u));
        }
        if (v != 0) {
            printf(" ctzll %d clzll %d", __builtin_ctzll(v), __builtin_clzll(v));
        }
        printf("\n");
    }
    return 0;
}
