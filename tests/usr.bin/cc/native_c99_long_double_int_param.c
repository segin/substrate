#include <stdio.h>

static long double f(long double base, int exp) {
    long double r = 1;
    while (exp-- > 0) {
        r *= base;
    }
    return r;
}

int main(void) {
    long double v = f(10, 3);
    if (v != 1000.0L) {
        printf("bad int to long double parameter: %Lf\n", v);
        return 1;
    }
    return 0;
}
