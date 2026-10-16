/* __builtin_signbit of float, double and long double, constant and not:
 * the sign bit, including of -0.0 and NaN. */
#include <stdio.h>

int main(void) {
    volatile float f = -0.0f, fp = 2.0f;
    volatile double d = -0.0, dp = 1.0, n = -__builtin_nan("");
    volatile long double l = -0.0L, lp = 3.0L;

    printf("%d %d %d %d\n", __builtin_signbit(-1.0) != 0, __builtin_signbit(1.0) != 0, __builtin_signbit(d) != 0,
           __builtin_signbit(-dp) != 0);
    printf("%d %d %d\n", __builtin_signbitf(f) != 0, __builtin_signbitf(fp) != 0, __builtin_signbit(n) != 0);
    printf("%d %d %d\n", __builtin_signbitl(l) != 0, __builtin_signbitl(lp) != 0, __builtin_signbitl(-lp) != 0);
    return 0;
}
