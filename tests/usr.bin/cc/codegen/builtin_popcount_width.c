/*
 * __builtin_popcount, __builtin_popcountl and __builtin_popcountll count
 * the set bits of their argument converted to unsigned int, unsigned long
 * and unsigned long long.  cc counted at the argument's own width, so
 * __builtin_popcount(-1LL) was 64 (gcc: 32) and
 * __builtin_popcount((signed char)-1) was 8 (gcc: 32, the sign extension
 * included).
 */
#include <stdio.h>

static volatile long long seeds[] = {
    0, 1, 2, 3, 7, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x12345678, 0x7fffffff, (long long)0x80000000u,
    (long long)0xffffffffu, 0x100000000LL, 0x123456789abcdefLL, -1, -2, -3, -128, -129, -32768,
    (long long)0x8000000000000000ULL, 0x7fffffffffffffffLL,
};

/**
 * Print the counts each builtin gives for each seed through arguments of
 * each width.
 *
 * @return 0
 */
int main(void) {
    unsigned i;

    for (i = 0; i < sizeof seeds / sizeof seeds[0]; ++i) {
        long long s = seeds[i];
        signed char sc = (signed char)s;
        unsigned char uc = (unsigned char)s;
        short ss = (short)s;
        int si = (int)s;
        unsigned ui = (unsigned)s;
        long sl = (long)s;
        unsigned long long ull = (unsigned long long)s;

        printf("%2u: %2d %2d %2d %2d %2d | %2d %2d | %2d %2d %2d | %2d\n", i, __builtin_popcount(sc),
               __builtin_popcount(uc), __builtin_popcount(ss), __builtin_popcount(si), __builtin_popcount(ui),
               __builtin_popcountl(sl), __builtin_popcountl(si), __builtin_popcountll(ull),
               __builtin_popcountll(sc), __builtin_popcountll(si), __builtin_popcount(ull));
    }
    return 0;
}
