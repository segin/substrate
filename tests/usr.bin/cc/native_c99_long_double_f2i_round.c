#include <stdint.h>
#include <stdio.h>

static intmax_t trunc_ld(long double v) {
    return v;
}

int main(void) {
    long double a = 1234.0L;
    long double b = -1234.75L;
    intmax_t ai = trunc_ld(a);
    intmax_t bi = trunc_ld(b);

    if (ai != 1234 || bi != -1234) {
        printf("ai=%jd bi=%jd\n", ai, bi);
        return 1;
    }
    return 0;
}
