/*
 * With FLT_EVAL_METHOD 2 (i386's x87, which cc and gcc both predefine) a
 * float or double operation is evaluated in long double; its type is
 * unchanged, and its greater range and precision go only at an
 * assignment, a cast, a return or an argument conversion (C11 5.2.4.2.2p9,
 * 6.3.1.8p2).  So (double)(a / b) for floats keeps the quotient's extra
 * bits, `q = a / b` loses them, and c * c / c for a float c of 1e30 does
 * not overflow.  On x86-64 (FLT_EVAL_METHOD 0) nothing is evaluated wider.
 * Compared with gcc's -std=c11, which is -fexcess-precision=standard.
 *
 * cc-std: c11
 */
#include <stdio.h>

static float sq(float x) {
    return x * x;
}

int main(void) {
    volatile float a = 1, b = 3, c = 1e30f;
    volatile double d = 1, e3 = 3, big = 1e300;
    float q = a / b;
    double r = d / e3;

    printf("%.17g %.17g %.17g\n", (double)(a / b), (double)q, a / b);
    printf("%.21Lg %.17g\n", (long double)(d / e3), r);
    printf("%d %d\n", c * c / c == c, big * big / big == big);
    printf("%.9g %d %s\n", sq(a / b), (a / b) * 3 == 1, _Generic(a / b, float: "float", default: "other"));
    printf("%zu %zu\n", sizeof(a / b), sizeof(d * e3));
    return 0;
}
