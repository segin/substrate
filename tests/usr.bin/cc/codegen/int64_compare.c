/*
 * Comparing 64-bit integers.  On i386 the high words decide unless they
 * are equal, and then the low words decide as unsigned numbers; a value
 * used as a condition is true when either word is non-zero.
 */
#include <stdio.h>

long long a = -5662727221982286579LL;
long long b = 32768;
unsigned long long u = 1322827018092045688ULL;
unsigned w = 4147328756U;
long long p = 0x100000000LL;
long long q = 1;
long long lo_only_differs = 0x100000005LL;
long long lo_only_base = 0x100000003LL;

int main(void) {
    long long r;

    printf("%d %d %d %d %d %d\n", a > b, a < b, w > u, u > w, p > q, p == q);
    printf("%d %d %d\n", a > 32768, a != 0, p != 0x200000000LL);
    printf("%d %d %d\n", lo_only_differs > lo_only_base, lo_only_differs <= lo_only_base,
           (unsigned long long)a >= u);
    if (p > q) {
        printf("if-gt\n");
    } else {
        printf("if-le\n");
    }
    if (p) {
        printf("p is true\n");
    }
    r = p ? a : b;
    printf("%lld %d\n", r, !p);
    return 0;
}
