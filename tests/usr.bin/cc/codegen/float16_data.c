/*
 * A _Float16 object holds the value it is given (C23 Annex H): as static
 * data, in arrays of it, and through a pointer on both targets; the f16
 * suffix makes a _Float16 constant.  Floating arrays initialized by
 * integers hold the converted values.
 *
 * cc-std: gnu2x
 * cc-march-m32: pentium4 (gcc has _Float16 on i386 only with SSE2)
 */
#include <stdio.h>

#define KIND(x) _Generic((x), _Float16: "f16", float: "float", double: "double", long double: "ldouble", default: "?")

_Float16 h = 1.5f;
_Float16 ha[5] = {0.25, -2, 65504, 1e-7, 3.140625f16};
float fa[3] = {1, -2, 3u};
double da[2] = {3, 0.5};
static _Float16 hs = 6.5e-5; /* a subnormal half */

static void put(_Float16 *p, double v) { *p = v; }
static double get(const _Float16 *p) { return *p; }

int main(void) {
    _Float16 a[3];
    _Float16 c = 1.5f16;
    int i;

    put(&a[0], 2.5);
    put(&a[1], -0.125);
    a[2] = a[0] * a[1];
    put(&h, h + 0.5f16);
    printf("h %g c %g hs %g\n", (double)h, (double)c, (double)hs);
    for (i = 0; i < 5; ++i) {
        printf("ha[%d] %g\n", i, (double)ha[i]);
    }
    printf("a %g %g %g\n", get(&a[0]), get(&a[1]), (double)a[2]);
    printf("fa %g %g %g da %g %g\n", fa[0], fa[1], fa[2], da[0], da[1]);
    printf("%s %s %s %u\n", KIND(1.0f16), KIND(1.0f), KIND(1.0), (unsigned)sizeof(1.0f16));
    return 0;
}
