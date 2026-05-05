#include <stdio.h>

static long double absld(long double v) {
    return v < 0 ? -v : v;
}

int main(void) {
    long double a = absld(2000.0L);
    long double b = absld(-12.5L);

    if (!(a >= 1000.0L) || !(b > 12.0L && b < 13.0L)) {
        printf("a=%Lf b=%Lf\n", a, b);
        return 1;
    }
    return 0;
}
