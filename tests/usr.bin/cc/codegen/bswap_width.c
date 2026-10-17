/*
 * __builtin_bswap16/32/64 at their full widths (CC-103): on i386 a
 * 64-bit swap was computed in 32-bit words and returned 0x8070000 for
 * 0x0102030405060708.  The operands come through a function, so the
 * swaps are not folded.
 */
#include <stdio.h>

static unsigned long long id(unsigned long long x) {
    return x;
}

int main(void) {
    unsigned long long v = id(0x0102030405060708ULL);
    unsigned long long w = id(0xfedcba9876543210ULL);

    printf("%llx %llx\n", __builtin_bswap64(v), __builtin_bswap64(w));
    printf("%x %x\n", __builtin_bswap32((unsigned)v), __builtin_bswap32((unsigned)w));
    printf("%x %x\n", (unsigned)__builtin_bswap16((unsigned short)v), (unsigned)__builtin_bswap16((unsigned short)w));
    printf("%llx\n", __builtin_bswap64(__builtin_bswap64(w)));
    return 0;
}
