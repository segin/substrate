/*
 * va_arg of every kind of argument the ABIs treat differently: integers
 * and doubles past the registers (the overflow area on x86-64), long
 * double (always in memory, aligned to 16), small and large structures,
 * and a va_copy that must advance on its own.
 */
#include <stdarg.h>
#include <stdio.h>

struct small {
    int a;
    int b;
};

struct big {
    long a;
    long b;
    long c;
};

static long sum_ints(int n, ...) {
    va_list ap;
    long s = 0;
    int i;

    va_start(ap, n);
    for (i = 0; i < n; ++i) {
        s = s * 3 + va_arg(ap, int);
    }
    va_end(ap);
    return s;
}

static double sum_doubles(int n, ...) {
    va_list ap;
    double s = 0;
    int i;

    va_start(ap, n);
    for (i = 0; i < n; ++i) {
        s = s * 2 + va_arg(ap, double);
    }
    va_end(ap);
    return s;
}

static long double mixed(int n, ...) {
    va_list ap;
    long double s = 0;

    va_start(ap, n);
    s += va_arg(ap, int);
    s += va_arg(ap, long double);
    s += va_arg(ap, double);
    s += va_arg(ap, long double);
    s += va_arg(ap, long long);
    va_end(ap);
    return s + n;
}

static long structs(int n, ...) {
    va_list ap;
    struct small sm;
    struct big bg;
    long r;

    va_start(ap, n);
    sm = va_arg(ap, struct small);
    bg = va_arg(ap, struct big);
    r = sm.a * 1000 + sm.b * 100 + bg.a * 10 + bg.b + bg.c * 7 + va_arg(ap, int);
    va_end(ap);
    return r + n;
}

static int copied(int n, ...) {
    va_list ap;
    va_list cp;
    int a;
    int b;

    va_start(ap, n);
    a = va_arg(ap, int);
    va_copy(cp, ap);
    b = va_arg(cp, int) * 10 + va_arg(cp, int);
    a = a * 100 + va_arg(ap, int);
    va_end(cp);
    va_end(ap);
    return a * 1000 + b + n;
}

int main(void) {
    struct small sm = {3, 4};
    struct big bg = {5, 6, 7};

    printf("%ld\n", sum_ints(9, 1, 2, 3, 4, 5, 6, 7, 8, 9));
    printf("%.3f\n", sum_doubles(10, 0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5));
    printf("%.3Lf\n", mixed(1, 2, (long double)3.25, 4.5, (long double)5.125, 6LL));
    printf("%ld\n", structs(1, sm, bg, 8));
    printf("%d\n", copied(1, 2, 3, 4));
    return 0;
}
