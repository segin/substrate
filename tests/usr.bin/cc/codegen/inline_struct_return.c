/*
 * A static inline function returning a small structure by value, which
 * x86-64 returns in registers.  The call writes that result to the
 * caller's slot as well; cc inlined the body at -O1 and up without that
 * store, so the caller read a zeroed structure.
 */
#include <stdio.h>

struct bits {
    unsigned a : 4;
    unsigned b : 9;
    unsigned c : 7;
};

struct pair {
    long x;
    long y;
};

static inline struct bits make_bits(int a, int b, int c) {
    struct bits r = {(unsigned char)a, (unsigned short)b, (unsigned char)c};
    return r;
}

static inline struct pair make_pair(long x, long y) {
    struct pair p = {x * 3, y - 1};
    return p;
}

int main(void) {
    struct bits b = make_bits(15, 511, 127);
    struct pair p = make_pair(14, 100);

    printf("bits %u %u %u\n", b.a, b.b, b.c);
    printf("pair %ld %ld\n", p.x, p.y);
    return 0;
}
