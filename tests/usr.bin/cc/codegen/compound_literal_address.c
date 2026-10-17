/*
 * A compound literal of any type is an lvalue (C11 6.5.2.5p4): `&` takes
 * the address of a scalar or array one as of a structure one, and the
 * object can be written through it.  cc took the address of structure
 * literals only ("address-of currently requires an identifier or member
 * lvalue").
 */
#include <stdio.h>

int main(void) {
    int *a = &(int){5};
    int (*b)[2] = &(int[2]){7, 8};
    int *c = (int[]){1, 2, 3};
    char *d = (char[]){"lit"};
    double *e = &(double){2.5};
    long double *f = &(long double){1.25L};
    char *g = &(char){65};
    long long *h = &(long long){1LL << 40};

    *a += 1;
    (*b)[1] += 1;
    c[2] += 1;
    d[0] = 'L';
    *e *= 2;
    printf("%d %d %d %s %g %Lg %c %lld %u\n", *a, (*b)[1], c[2], d, *e, *f, *g, *h, (unsigned)sizeof *b);
    return 0;
}
