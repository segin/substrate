#include <stdio.h>

static long double powerld(long double base, int x) {
    long double result = base;
    if (x == 0) {
        return 1;
    }
    while (--x) {
        result *= base;
    }
    return result;
}

static int accept(const long double val) {
    long double v = val;
    int i;
    for (i = 0; i < 10; ++i) {
        v /= 1000;
    }
    return v == 1.0L;
}

int main(void) {
    long double v = powerld(1000, 10);
    if (!accept(v)) {
        printf("v=%.30Lf\n", v);
        return 1;
    }
    return 0;
}
