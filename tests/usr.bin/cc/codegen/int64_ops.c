/*
 * 64-bit integer arithmetic.  On i386 each long long is a pair of
 * 32-bit words, and every operator must carry into, or read from, the
 * high word: + - * / % << >> and the bitwise ones.
 */
#include <stdio.h>

long long a = 0x123456789LL;
long long b = 0x1000000LL;
unsigned long long ua = 0xfedcba9876543210ULL;
int n = 36;

static long long mul(long long x, long long y) {
    return x * y;
}

int main(void) {
    long long c = a;

    printf("add %llx\n", (unsigned long long)(a + b));
    printf("sub %llx\n", (unsigned long long)(a - b));
    printf("mul %llx\n", (unsigned long long)(a * b));
    printf("mulf %lld\n", mul(-3, 5000000000LL));
    printf("udiv %llx\n", ua / 7);
    printf("sdiv %lld\n", -a / 7);
    printf("umod %llx\n", ua % 1000);
    printf("smod %lld\n", -a % 10);
    printf("shl %llx\n", (unsigned long long)(a << n));
    printf("shr %llx\n", ua >> n);
    printf("sar %llx\n", (unsigned long long)((long long)ua >> n));
    printf("and %llx\n", ua & (unsigned long long)a);
    printf("neg %llx\n", (unsigned long long)-a);
    c *= c;
    c /= 3;
    c %= 1000000007;
    printf("assign %lld\n", c);
    return 0;
}
