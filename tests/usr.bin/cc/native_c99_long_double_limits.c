#include <float.h>
#include <stdio.h>

int main(void) {
    volatile long double mn = LDBL_MIN;
    volatile long double mx = LDBL_MAX;
    volatile long double decimal_mn = 3.362103143112093506262677817321752E-4932L;
    volatile long double decimal_mx = 1.1897314953572317650E+4932L;
    volatile long double dn = 0.0L;
#ifdef __LDBL_DENORM_MIN__
    dn = __LDBL_DENORM_MIN__;
#endif

    if (!(mn > 0.0L) || !(mn < DBL_MIN)) {
        printf("bad LDBL_MIN\n");
        return 1;
    }
    if (!(decimal_mn > 0.0L) || !(decimal_mn < DBL_MIN)) {
        printf("bad decimal long double min\n");
        return 4;
    }
    if (!(mx > DBL_MAX) || !(mx < __builtin_infl())) {
        printf("bad LDBL_MAX\n");
        return 2;
    }
    if (!(decimal_mx > DBL_MAX) || !(decimal_mx < __builtin_infl())) {
        printf("bad decimal long double max\n");
        return 5;
    }
    if (!(dn >= 0.0L) || !(dn < mn)) {
        printf("bad LDBL_DENORM_MIN\n");
        return 3;
    }
    return 0;
}
