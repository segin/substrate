/*
 * Converting to _Bool gives 1 for any non-zero value: a floating value
 * such as 0.5 must not be truncated to an integer first, and a 64-bit
 * value is non-zero when either of its words is.
 */
#include <stdio.h>
#include <stdbool.h>

double d = 0.5;
_Bool g = 0.5;
long long big = 0x100000000LL;
static _Bool sg[2] = {0.25, 0};

int main(void) {
    _Bool a = 0.5;
    _Bool b;
    _Bool c = (_Bool)d;
    _Bool e = d;
    bool f = big;
    static _Bool s = 0.75;

    b = d;
    printf("%d %d %d %d %d %d\n", a, b, c, e, g, (_Bool)0.25);
    printf("%d %d %d %d %d\n", f, (_Bool)big, s, sg[0], (_Bool)-0.0);
    return 0;
}
