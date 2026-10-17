/*
 * A typedef of a function type names that function type, parameters and
 * all (C11 6.7.8): a pointer to it is a function pointer, a declaration
 * through it declares a function, and its prototype converts arguments.
 */
#include <stdarg.h>
#include <stdio.h>

typedef int binop_t(int, int);
typedef long sum_t(int n, ...);
typedef double half_t(double);

binop_t add;  /* declares the function defined below */
extern binop_t sub;

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

static long sum(int n, ...) {
    va_list ap;
    long s = 0;

    va_start(ap, n);
    while (n-- > 0) {
        s += va_arg(ap, int);
    }
    va_end(ap);
    return s;
}

static double half(double x) { return x / 2; }

struct ops {
    binop_t *op;
    half_t *h;
};

static int apply(binop_t *f, int a, int b) { return f(a, b); }

_Static_assert(__builtin_types_compatible_p(binop_t *, int (*)(int, int)), "binop_t * is int (*)(int, int)");
_Static_assert(__builtin_types_compatible_p(sum_t *, long (*)(int, ...)), "sum_t * is variadic");
_Static_assert(sizeof(binop_t *) == sizeof(void (*)(void)), "a function pointer's size");

int main(void) {
    struct ops o = {sub, half};
    sum_t *s = sum;
    binop_t *table[2] = {add, sub};

    printf("%d %d\n", apply(add, 40, 2), apply(table[1], 50, 8));
    printf("%d %.2f\n", o.op(9, 4), o.h(5)); /* 5 converts to double through the prototype */
    printf("%ld\n", s(4, 1, 2, 3, 4));
    return 0;
}
