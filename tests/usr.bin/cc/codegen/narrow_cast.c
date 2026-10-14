/*
 * Converting a 64-bit value to a 32-bit type takes its low word.  On
 * i386 the conversion must not relabel the 64-bit value itself as 32
 * bits: that made the operation computing it -- a shift by 48 here --
 * run as a 32-bit one.
 */
#include <stdio.h>

int v = 0x12345;
int n = 48;
long long big = 0x123456789abcLL;

struct S {
    signed f : 4;
};

int main(void) {
    struct S s;
    int a = (int)((unsigned long long)v << n);
    int b = (unsigned long long)v << n;
    unsigned long long t = (unsigned long long)v << n;
    int c = (int)t;
    int d = (int)((unsigned long long)v << 48);
    int e = (int)((unsigned long long)v * 0x1000000000000ULL);
    short f = (short)(big >> 8);
    signed char g = (signed char)(big + 0x80);

    s.f = -3;
    printf("%x %x %x %x %x\n", a, b, c, d, e);
    printf("%d %d %d\n", f, g, s.f);
    return 0;
}
