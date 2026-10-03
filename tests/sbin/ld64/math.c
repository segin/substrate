/*
 * math.c - a dynamically linked 64-bit program using libm.so.0.
 */
#include <math.h>
#include <stdio.h>

int main(int argc, char **argv) {
    (void)argv;
    /* Not a constant the compiler can fold. */
    double x = 2.0 * argc;
    double r = sqrt(x);
    double p = pow(r, 2.0);

    printf("sqrt(%.1f)=%.6f pow=%.6f floor(2.7)=%.1f\n",
           x, r, p, floor(2.7 * argc));
    if (r < 1.414213 || r > 1.414214 || p < 1.999999 || p > 2.000001) {
        printf("math: FAIL\n");
        return 1;
    }
    printf("math: OK\n");
    return 0;
}
