/*
 * A floating value of 2^63 or more converted to unsigned long long: the
 * hardware conversions are signed, so it is converted less 2^63 and given
 * its top bit back -- at run time from double, float and long double, and
 * in constant initializers.
 */
#include <stdio.h>

static unsigned long long s1 = 1.8e19;
unsigned long long s2 = 12345678901234567891.0;

int main(void) {
    volatile double d = 12345678901234567891.0;
    volatile double e = 5.75;
    volatile double z = 9223372036854775808.0;
    volatile long double l = 18446744073709551615.0L;
    volatile long double m = 1e19L;
    volatile float f = 1.5e19f;
    static unsigned long long s3 = 1.5e19;
    unsigned long long r[7];
    int i;

    r[0] = d;
    r[1] = e;
    r[2] = z;
    r[3] = l;
    r[4] = m;
    r[5] = f;
    r[6] = 12345678901234567891.0;
    for (i = 0; i < 7; ++i) {
        printf("%llu ", r[i]);
    }
    printf("%llu %llu %llu %lld %lld\n", s1, s2, s3, (long long)-d / 2, (long long)(unsigned long long)d);
    return 0;
}
