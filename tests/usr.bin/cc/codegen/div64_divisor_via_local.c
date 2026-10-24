/*
 * A 64-bit division by a power of two that a local holds is a shift on
 * i386, as gcc makes it, and no call of libgcc: Linux does not link it,
 * and its roundup() divides by `const typeof(y) __y = y`.  cc legalized
 * divisions before it knew the local's value, and left the kernel's
 * drivers/pci/pcie/aspm.c with five calls of __udivdi3.  The test's
 * assembly is checked for them by run_div64_pow2.sh.
 */
#include <stdio.h>

typedef unsigned long long u64;
typedef long long s64;

#define roundup(x, y) ({ const typeof(y) __y = y; (((x) + (__y - 1)) / __y) * __y; })

unsigned viavar(u64 n) {
    const int y = 32;

    return n / y;
}

unsigned rup(u64 n) {
    return roundup(n, 1024) / 1024;
}

u64 big(u64 n) {
    return roundup(n, 33554432) / 33554432;
}

s64 sdiv(s64 n) {
    const int y = 8;

    return n / y;
}

u64 narrow(unsigned v) {
    const int y = 1000;

    return (u64)v / y;
}

int main(void) {
    static const u64 un[] = {0, 1, 31, 32, 33, 1023, 1024, 1025, 0xffffffffULL, 0x100000000ULL, 0x123456789abcdefULL};
    static const s64 sn[] = {0, 7, 8, 9, -1, -7, -8, -9, -0x123456789LL, 0x7fffffffffffffffLL};
    size_t i;

    for (i = 0; i < sizeof(un) / sizeof(un[0]); ++i) {
        printf("%u %u %llu %llu\n", viavar(un[i]), rup(un[i]), big(un[i]), narrow((unsigned)un[i]));
    }
    for (i = 0; i < sizeof(sn) / sizeof(sn[0]); ++i) {
        printf("%lld\n", sdiv(sn[i]));
    }
    return 0;
}
