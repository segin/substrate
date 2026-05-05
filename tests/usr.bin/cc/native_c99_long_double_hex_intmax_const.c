#include <stdint.h>
#include <stdio.h>

static long double reduce(long double val) {
    intmax_t m = val / 0x7fffffffffffffffL;
    val -= (long double)0x7fffffffffffffffL * m;
    return val;
}

int main(void) {
    long double r = reduce(1234.0L);
    if (!(r > 1233.0L && r < 1235.0L)) {
        printf("r=%Lf\n", r);
        return 1;
    }
    return 0;
}
