/*
 * A compound literal of a wide integer type holds the value its
 * initializer converts to (C11 6.5.2.5): (unsigned _BitInt(128)){ k } for
 * an int k of -7 is 2^128 - 7, as gcc has it, not -7 taken for the low limb
 * with the others zero.  A `wb` constant, which cc spells as the same kind
 * of literal, keeps its value.
 *
 * cc-std: gnu2x
 */
#include <stdio.h>

int main(void) {
    volatile int k = -7;
    volatile unsigned long long u = 18446744073709551615ULL;
    unsigned _BitInt(128) a = (unsigned _BitInt(128)){k};
    _BitInt(100) b = (_BitInt(100)){3};
    _BitInt(200) c = (_BitInt(200)){u};
    _BitInt(200) d = (_BitInt(200)){-1};
    unsigned _BitInt(70) w = 0x3fffffffffffffffffuwb;

    printf("%llx %llx\n", (unsigned long long)(a >> 64), (unsigned long long)a);
    printf("%lld %llx %llx %lld\n", (long long)b, (unsigned long long)(c >> 64), (unsigned long long)c,
           (long long)(d >> 150));
    printf("%llx %llx\n", (unsigned long long)(w >> 64), (unsigned long long)w);
    return 0;
}
