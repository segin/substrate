/*
 * 64-bit integer arithmetic, which i386 legalization does in 32-bit
 * words: carries and borrows across the words, the three-multiply
 * product, division through libgcc, shifts by every count (constant and
 * variable, across the word boundary), and compares that the low words
 * decide when the high words are equal.
 */
#include <stdio.h>

static volatile unsigned long long vu[] = {
    0, 1, 0xffffffffULL, 0x100000000ULL, 0xfffffffffULL, 0x8000000000000000ULL, 0xffffffffffffffffULL,
    0x123456789abcdef0ULL, 0x00000000fffffffeULL, 0x7fffffffffffffffULL,
};
#define N (sizeof(vu) / sizeof(vu[0]))

static unsigned long long mix;

static void out(unsigned long long x) {
    mix = mix * 0x100000001b3ULL ^ x;
}

#define SHIFT_CONST(k)                                                                                             \
    do {                                                                                                           \
        out(a << (k));                                                                                             \
        out(a >> (k));                                                                                             \
        out((unsigned long long)(s >> (k)));                                                                       \
    } while (0)

int main(void) {
    unsigned i;
    unsigned j;
    volatile int n;

    for (i = 0; i < N; ++i) {
        unsigned long long a = vu[i];
        long long s = (long long)a;

        SHIFT_CONST(1);
        SHIFT_CONST(7);
        SHIFT_CONST(31);
        SHIFT_CONST(32);
        SHIFT_CONST(33);
        SHIFT_CONST(48);
        SHIFT_CONST(63);
        for (n = 0; n < 64; ++n) {
            out(a << n);
            out(a >> n);
            out((unsigned long long)(s >> n));
        }
        for (j = 0; j < N; ++j) {
            unsigned long long b = vu[j];
            long long t = (long long)b;

            out(a + b);
            out(a - b);
            out(a * b);
            out(a & b);
            out(a | b);
            out(a ^ b);
            out((unsigned long long)(s * t));
            if (b != 0) {
                out(a / b);
                out(a % b);
            }
            if (t != 0 && !(s == (long long)0x8000000000000000ULL && t == -1)) {
                out((unsigned long long)(s / t));
                out((unsigned long long)(s % t));
            }
            out((a < b) | (a <= b) << 1 | (a > b) << 2 | (a >= b) << 3 | (a == b) << 4 | (a != b) << 5);
            out((s < t) | (s <= t) << 1 | (s > t) << 2 | (s >= t) << 3);
        }
        /* a word extended into a pair, both ways */
        out((unsigned long long)(int)a + a);
        out((unsigned long long)(unsigned)a * 3);
        out(a / 10);
        out((unsigned long long)(s / -7));
    }
    printf("%016llx\n", mix);
    return 0;
}
