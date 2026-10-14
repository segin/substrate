/*
 * Converting floating values to integers truncates toward zero, and a
 * value too big for int still converts whole to unsigned and long long.
 * On i386 the x87 conversion once kept nothing (its result register was
 * dropped), and a conversion to long long kept 32 bits.
 */
#include <stdio.h>

double a = 2.99;
double b = -2.99;
double c = 3.9e9;
double n = -7.5e12;
float f = 2.5e10f;
float third = 1.0f / 3.0f;

int main(void) {
    long long big = c * 4;
    unsigned long long ubig = c * 1000;
    long long neg = n;
    long long fl = f;
    unsigned w = c;
    int i = a;
    short s = b * 1000;

    printf("%d %d %u %d\n", (int)a, (int)b, (unsigned)c, (int)(third * 9));
    printf("%lld %llu %lld %lld %u\n", big, ubig, neg, fl, w);
    printf("%lld %d %d\n", (long long)(c * 4), i, s);
    return 0;
}
