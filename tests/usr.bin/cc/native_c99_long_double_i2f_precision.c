#include <stdint.h>
#include <stdio.h>

int main(void) {
    volatile long double val = 9223372036854775808.0L;
    intmax_t m = val / INTMAX_MAX;
    long double rem = val - (long double)INTMAX_MAX * m;
    long double rebuilt = (long double)INTMAX_MAX * m + 1;

    if (m != 1) {
        printf("bad quotient: %jd\n", m);
        return 1;
    }
    if (rem != 1.0L) {
        printf("bad remainder: %Lf\n", rem);
        return 2;
    }
    if (rebuilt != val) {
        printf("bad rebuild: %Lf\n", rebuilt);
        return 3;
    }
    return 0;
}
