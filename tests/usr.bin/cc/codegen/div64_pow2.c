/*
 * A 64-bit division or remainder by a constant power of two is a shift
 * and a mask on i386, as gcc makes it, with no call of libgcc's
 * __udivdi3 or __divdi3: Linux does not link libgcc, and head32.c divides
 * a 64-bit constant by PTRS_PER_PGD.  A signed quotient rounds toward zero.
 * run_div64_pow2.sh checks the calls are gone; this checks the values.
 */
#include <stdio.h>

#define LOWMEM_PAGES ((((2ULL) << 31) - 0xC0000000UL) >> 12)
#define PAGE_TABLE_SIZE(pages) ((pages) / 1024)

static unsigned long limit(unsigned long end) {
    return end + (PAGE_TABLE_SIZE(LOWMEM_PAGES) << 12);
}

int main(int argc, char **argv) {
    static const long long sv[] = {0, 1, -1, 7, -7, 8, -8, 9, -9, 1023, -1023, 1024, -1024, 0x123456789abcdefLL,
                                   -0x123456789abcdefLL, 0x7fffffffffffffffLL, -0x7fffffffffffffffLL - 1};
    unsigned long long h = 0;
    size_t i;

    (void)argv;
    for (i = 0; i < sizeof(sv) / sizeof(sv[0]); ++i) {
        long long s = sv[i] + (argc - 1);
        unsigned long long u = (unsigned long long)s;

        h = h * 31 + u / 1;
        h = h * 31 + u / 2;
        h = h * 31 + u / 1024;
        h = h * 31 + u / 0x100000000ULL;
        h = h * 31 + u / 0x8000000000000000ULL;
        h = h * 31 + u % 4096;
        h = h * 31 + u % 0x200000000ULL;
        h = h * 31 + (unsigned long long)(s / 1);
        h = h * 31 + (unsigned long long)(s / 2);
        h = h * 31 + (unsigned long long)(s / 8);
        h = h * 31 + (unsigned long long)(s / 0x100000000LL);
        h = h * 31 + (unsigned long long)(s / 0x4000000000000000LL);
        h = h * 31 + (unsigned long long)(s % 8);
        h = h * 31 + (unsigned long long)(s % 0x100000000LL);
        printf("%lld %lld %lld %llu %llu\n", s / 8, s % 8, s / 0x100000000LL, u / 1024, u % 4096);
    }
    printf("%llx %lu\n", h, limit(0x1000));
    return 0;
}
