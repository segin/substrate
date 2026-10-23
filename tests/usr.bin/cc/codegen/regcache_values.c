/*
 * Values the i386 emitter keeps in registers across instructions, and the
 * optimizer's scalar locals made values, copies read as their sources and
 * identities folded: each must still read what the program wrote.
 */
#include <stdio.h>

static volatile int sink;

__attribute__((noinline)) static int three(int a, int b, int c) {
    return a * 100 + b * 10 + c;
}

/* a local whose address is taken, changed through the pointer while its value is in a register */
__attribute__((noinline)) static int through_pointer(int a) {
    int x = a;
    int *p = &x;
    int y = x + 1;

    *p = 5;
    y += x;
    *p += y;
    return x * 1000 + y;
}

/* constants an instruction reads from their slots: call arguments, a 64-bit store */
__attribute__((noinline)) static long long constants(int k) {
    long long w = 7;

    w += three(1, 2, k);
    return w + three(4, 5, 6);
}

/* every integer compare as a branch, signed and unsigned */
__attribute__((noinline)) static unsigned compares(int a, int b, unsigned ua, unsigned ub) {
    unsigned m = 0;

    if (a < b) m |= 1u << 0;
    if (a <= b) m |= 1u << 1;
    if (a > b) m |= 1u << 2;
    if (a >= b) m |= 1u << 3;
    if (a == b) m |= 1u << 4;
    if (a != b) m |= 1u << 5;
    if (ua < ub) m |= 1u << 6;
    if (ua <= ub) m |= 1u << 7;
    if (ua > ub) m |= 1u << 8;
    if (ua >= ub) m |= 1u << 9;
    if (a < 3) m |= 1u << 10;
    if (ua > 3u) m |= 1u << 11;
    return m;
}

/* truth values tested again, and joined by && and || */
__attribute__((noinline)) static int truths(int a, int b) {
    int t = (a < b) != 0;
    int u = !!(a & b);
    int v = (a > 0 && b > 0) || a == -b;

    return t * 100 + u * 10 + v;
}

/* the words of a 64-bit value taken apart and put back */
__attribute__((noinline)) static unsigned pairs(unsigned lo, unsigned hi) {
    unsigned long long r = ((unsigned long long)hi << 32) | lo;
    unsigned long long z = r & ~0ULL;

    z = (z | 0) + 0;
    return (unsigned)z + (unsigned)(z >> 32) * 3u;
}

/* masks and moves that change nothing, and ones that do */
__attribute__((noinline)) static unsigned identities(unsigned x, signed char c) {
    unsigned a = x & 0xffffffffu;
    unsigned b = (a | 0) ^ 0;
    unsigned d = (b << 0) >> 0;
    unsigned e = d * 1 - 0;
    unsigned char uc = (unsigned char)c;

    return e + (unsigned)(c & 0xff) + uc + ((unsigned)c & 0xffu);
}

/* a scalar local in a loop, as a value */
__attribute__((noinline)) static unsigned long length(const char *s) {
    const char *p = s;

    while (*p)
        p++;
    return (unsigned long)(p - s);
}

/* many values, and a 64-bit result whose high word is read last */
__attribute__((noinline)) static unsigned long long wide_return(const unsigned *v) {
    unsigned a0 = v[0] + 1, a1 = v[1] + 2, a2 = v[2] + 3, a3 = v[3] + 4;
    unsigned b0 = a0 * a1, b1 = a1 * a2, b2 = a2 * a3, b3 = a3 * a0;
    unsigned c0 = b0 ^ b1, c1 = b1 ^ b2, c2 = b2 ^ b3, c3 = b3 ^ b0;
    unsigned d0 = c0 + c1, d1 = c1 + c2, d2 = c2 + c3, d3 = c3 + c0;
    unsigned e0 = d0 - d1, e1 = d1 - d2, e2 = d2 - d3, e3 = d3 - d0;
    unsigned f0 = e0 | e1, f1 = e1 & e2, f2 = e2 | e3, f3 = e3 & e0;
    unsigned g0 = f0 + f1 + f2 + f3;
    unsigned g1 = f0 ^ f1 ^ f2 ^ f3;
    const unsigned long long *q = (const unsigned long long *)(const void *)(v + 4);

    sink = (int)(g0 + g1);
    return *q + ((unsigned long long)g1 << 32) + g0;
}

/* a dead 64-bit constant left in a register while its slot goes to another value (the random test's seed 20) */
static signed char mix_v2 = 2;
static unsigned short mix_v4 = 0x8c;
unsigned char mix_v1 = 0;

__attribute__((noinline)) static unsigned mixed_widths(void) {
    int l0 = -1;

    mix_v2 = (signed char)(((unsigned long long)(((unsigned)(((unsigned)(mix_v4) << ((0ULL) & 31))) +
                                                  (unsigned)(((unsigned long long)(l0) ^
                                                              (unsigned long long)(((unsigned char)208)))))) -
                            (unsigned long long)(((int)(((unsigned long long)(((unsigned char)173)) |
                                                         (unsigned long long)(((unsigned char)126)))) /
                                                  ((int)((mix_v1) & 15) + 1)))));
    return (unsigned char)mix_v2;
}

int main(void) {
    unsigned v[6] = {3, 5, 7, 11, 0x89abcdefu, 0x01234567u};

    printf("%d %d\n", through_pointer(2), through_pointer(-9));
    printf("%lld %lld\n", constants(3), constants(-40));
    printf("%x %x %x\n", compares(1, 2, 1u, 2u), compares(5, 5, 7u, 7u), compares(-4, 2, 0xfffffffcu, 2u));
    printf("%d %d %d\n", truths(1, 2), truths(3, -3), truths(6, 4));
    printf("%x %x\n", pairs(0x11111111u, 0x2u), pairs(0xffffffffu, 0xffffffffu));
    printf("%x %x\n", identities(0x12345678u, (signed char)-3), identities(0u, 100));
    printf("%lu %lu\n", length("substrate"), length(""));
    printf("%llx\n", wide_return(v));
    printf("%x\n", mixed_widths());
    return 0;
}
