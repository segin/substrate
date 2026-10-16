/* long double keeps x87 extended precision between operations (i386 as on
 * x86-64), and 64-bit integers convert to floating whole. */
#include <stdio.h>
#include <float.h>
struct s { char c; long double d; int i; };
static long double add(long double a, long double b) { return a + b; }
static long double mulsub(long double a, long double b, long double c) { return a * b - c; }
static double back(long double x) { return (double)x; }
int main(void) {
    long double x = 1.0L, y = 0x1p-60L, z;
    long double arr[3] = {1.5L, 2.25L, 0.1L};
    struct s v = {1, 2.5L, 3};
    long long ll = 1234567890123456789LL;
    long double fl = (long double)ll;
    int i;
    z = add(x, y);
    printf("%d %d %d\n", z != 1.0L, (int)sizeof(long double), LDBL_MANT_DIG);
    printf("%.21Lg %.21Lg\n", z, mulsub(3.0L, 1.0L / 3.0L, 1.0L));
    printf("%.21Lg %lld %.17g\n", fl, (long long)fl, back(z));
    printf("%.21Lg %.21Lg %.21Lg\n", arr[0] * arr[1], arr[2], LDBL_MAX / 2);
    for (i = 0; i < 3; i++) arr[i] += y;
    printf("%d %d %Lg %d\n", arr[0] > 1.5L, arr[1] == 2.25L, v.d * 2, v.i);
    printf("%.21Lg\n", (long double)0.1 - 0.1L);
    return 0;
}
