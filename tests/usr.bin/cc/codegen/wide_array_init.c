/*
 * Arrays of integers wider than 64 bits hold the whole value of each
 * initializer (C11 6.7.9p10), whatever their storage: static locals (laid
 * out as data, as file-scope arrays are), automatic arrays, and arrays of
 * structures initialized from structure values.
 *
 * cc-std: gnu2x
 * cc-gcc: 14 (_BitInt)
 * cc-gcc-m32: 16 (_BitInt under -m32)
 */
#include <stdio.h>

typedef unsigned long long u64;

#define P(s, v) printf("%s %016llx %016llx\n", s, (u64)((v) >> 64), (u64)(v))

struct pair {
    int a;
    long b;
};

static _BitInt(130) file_scope[3] = {1, -2, 0x123456789abcdef0123456789wb};

static void run(int n) {
    static _BitInt(128) sb[3] = {1, 0x123456789abcdef0123wb, -5};
    static unsigned _BitInt(200) su[2] = {7, (unsigned _BitInt(200))1 << 150};
    _BitInt(128) ab[3] = {n, (_BitInt(128))n << 100, -n};
    struct pair p = {n, 2L * n};
    struct pair pa[2] = {p, {3, 4}};

    P("sb0", sb[0]);
    P("sb1", sb[1]);
    P("sb2", sb[2]);
    printf("su %016llx %016llx\n", (u64)(su[1] >> 128), (u64)su[0]);
    P("ab0", ab[0]);
    P("ab1", ab[1]);
    P("ab2", ab[2]);
    printf("pa %d %ld %d %ld\n", pa[0].a, pa[0].b, pa[1].a, pa[1].b);
    sb[0] += 1; /* a static keeps its value from one call to the next */
}

int main(void) {
#ifdef __SIZEOF_INT128__
    static unsigned __int128 u[2] = {5, (unsigned __int128)1 << 100};

    printf("u %llu %llu\n", (u64)(u[1] >> 96), (u64)u[0]);
#endif
    P("fs1", file_scope[1]);
    P("fs2", file_scope[2]);
    run(9);
    run(11);
    return 0;
}
