/*
 * __builtin_parity, __builtin_parityl and __builtin_parityll on values
 * known only at run time: each argument is converted to the builtin's
 * width (unsigned int, unsigned long, unsigned long long) and the parity
 * of its set bits returned.  cc folded constant arguments but rejected
 * any other ("unsupported builtin function").
 */
#include <stdio.h>

static volatile long long seeds[] = {
    0, 1, 2, 3, 7, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x12345678, 0x7fffffff, (long long)0x80000000u,
    (long long)0xffffffffu, 0x100000000LL, 0x123456789abcdefLL, -1, -2, -3, -128, -129, -32768,
    (long long)0x8000000000000000ULL, 0x7fffffffffffffffLL,
};

/**
 * Fold the parity of each seed, through arguments of each width, into a
 * few printed lines.
 *
 * @return 0
 */
int main(void) {
    unsigned i;
    unsigned long bits = 0;

    for (i = 0; i < sizeof seeds / sizeof seeds[0]; ++i) {
        long long s = seeds[i];
        signed char sc = (signed char)s;
        unsigned char uc = (unsigned char)s;
        short ss = (short)s;
        int si = (int)s;
        unsigned ui = (unsigned)s;
        long sl = (long)s;
        unsigned long long ull = (unsigned long long)s;
        int row = __builtin_parity(sc) | __builtin_parity(uc) << 1 | __builtin_parity(ss) << 2 |
                  __builtin_parity(si) << 3 | __builtin_parity(ui) << 4 | __builtin_parityl(sl) << 5 |
                  __builtin_parityl(si) << 6 | __builtin_parityll(ull) << 7 | __builtin_parityll(sc) << 8 |
                  __builtin_parity(ull) << 9;
        printf("%2u %03x\n", i, row);
        bits = bits * 31 + (unsigned long)row;
    }
    printf("%lu %d %d\n", bits % 1000003, __builtin_parity(0x7u), __builtin_parityll(0x3ull));
    return 0;
}
