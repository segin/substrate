/*
 * Where operations are evaluated in long double (i386), a floating
 * constant has its value in that format (C11 6.4.4.2p5), as gcc's ISO
 * modes give it: 0.1 == 0.1L, though sizeof (0.1) is a double's.
 */
/* cc-std: c11 */
#include <stdio.h>

static double sd = 0.1;
static float sf = 0.1f;
static long double sl = 0.1;
double gd = 1.0 / 3;

int main(void) {
    double d = 0.1;
    float f = 0.1f;
    long double l = 0.1f;
    volatile double v = 0.1;

    printf("%d %d %d %d\n", 0.1 == 0.1L, 0.1f == 0.1L, d == 0.1L, v == 0.1);
    printf("%d %d %d\n", (int)sizeof(0.1), (int)sizeof(0.1f), _Generic(0.1f, float: 1, default: 0));
    printf("%.20Lg %.20Lg %.20Lg\n", (long double)0.1, (long double)0.1f, l);
    printf("%.20g %.20g %.20Lg %.20g\n", sd, (double)sf, sl, gd);
    printf("%.20g %.9g %.20g\n", d, f, 0.1 * 3);
    printf("%.20Lg\n", 0.1 * 3 + 0.0L);
    return 0;
}
