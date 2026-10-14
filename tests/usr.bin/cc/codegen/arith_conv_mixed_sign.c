/*
 * The usual arithmetic conversions with operands of mixed signedness
 * (C11 6.3.1.8): a signed type of higher rank wins only when it can
 * hold every value of the unsigned one.  long long against unsigned
 * int is therefore signed everywhere; long against unsigned int is
 * signed on LP64 and unsigned on ILP32.
 */
#include <stdio.h>

long long v = -5;
unsigned u = 3;
unsigned char c = 200;
int i = -1;
unsigned long long U = 5;
long l = -1;
long long w = 4000000000LL;

int main(void) {
    printf("%d %d %d %d\n", u >= v, v < u, c > v, (long long)u > v);
    printf("%d %d\n", i < U, u < (long long)i);
    printf("%d %d\n", u < w, (unsigned)7 > w);
    printf("%d\n", l < u);
    printf("%lld %lld\n", v + u, v * u);
    printf("%d\n", (int)sizeof(v + u));
    return 0;
}
