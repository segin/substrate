/*
 * Constant folding at -O2 must give what the target computes.  On i386
 * a value narrower than 8 bytes is a 32-bit register, so folds of it --
 * the shift pairs that narrow to char and short among them -- are
 * 32-bit operations, and a compare folds at the width it would run at.
 */
#include <stdio.h>

int main(void) {
    unsigned a = (unsigned)((signed char)((unsigned char)254));
    int b = (short)40000;
    int c = (signed char)(3662184515U >> 3);
    unsigned d = 0x80000000u;
    int e = (int)3662184515U >> ((short)1274 & 31);
    long long f = (long long)(int)0x80000000u;
    unsigned long long g = 0xfffffffffULL;

    printf("%u %d %d %u\n", a, b, c, d >> 31);
    printf("%d %lld\n", e, f);
    printf("%d %d %d\n", (signed char)200 < 0, (unsigned)-1 > 0, (short)-1 >= 0);
    printf("%d %d\n", g > 0xffffffffULL, (unsigned char)g == 255);
    printf("%d %d\n", (int)(0x7fffffffu + 1u) < 0, 100000 * 100000u > 1000000000u);
    return 0;
}
