#include <stdint.h>
#include <stdio.h>

int main(void) {
    volatile unsigned long long one = 1ULL;
    volatile unsigned long long high = 9223372036854775808ULL;
    volatile long double a = one;
    volatile long double b = high;

    if (a != 1.0L) {
        printf("bad one: %Lf\n", a);
        return 1;
    }
    if (b != 9223372036854775808.0L) {
        printf("bad high: %Lf\n", b);
        return 2;
    }
    return 0;
}
