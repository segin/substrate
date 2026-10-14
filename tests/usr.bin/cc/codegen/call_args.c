/*
 * Arguments of every width, mixed.  On i386 each stack argument takes a
 * multiple of 4 bytes; a parameter after a char or short was once read
 * from the char's own width further on, so a short after a char read 0.
 */
#include <stdio.h>

static long f1(int a, long b, char c, short d) {
    return a * 1000 + b * 100 + c * 10 + d;
}

static long f2(char c, char d, short e, long long f, unsigned char g) {
    return c + d * 10 + e * 100 + (long)(f % 1000) * 1000 + g;
}

static long many(int a, long b, char c, short d, int e, long f, int g, int h, double x, float y, int i, double z) {
    return a + b + c + d + e + f + g + h + (long)(x * 10) + (long)(y * 10) + i + (long)(z * 10);
}

static double mixed(float a, char b, double c, short d) {
    return a + b + c + d;
}

int main(void) {
    printf("%ld %ld\n", f1(1, 2, 3, 4), f2(1, 2, 3, 123456789012LL, 200));
    printf("%ld %.2f\n", many(1, 2, 3, 4, 5, 6, 7, 8, 1.5, 2.5f, 9, 3.5), mixed(0.5f, 1, 2.25, -3));
    return 0;
}
