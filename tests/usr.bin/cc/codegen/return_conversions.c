/*
 * A returned value is converted to the function's return type.  On i386
 * `return 7;` from a long long function left the high word of the result
 * as whatever the stack held.
 */
#include <stdio.h>

static long long r1(void) { return 7; }
static long long r2(int x) { return x; }
static unsigned long long r3(unsigned x) { return x; }
static long long r4(void) { return -5; }
static unsigned long long r5(void) { char c = -1; return c; }
static short r6(int x) { return x; }
static unsigned char r7(int x) { return x; }

int main(void) {
    printf("%lld %lld %llu %lld %llx %d %d\n", r1(), r2(-3), r3(4000000000u), r4(), r5(), r6(70000), r7(300));
    return 0;
}
