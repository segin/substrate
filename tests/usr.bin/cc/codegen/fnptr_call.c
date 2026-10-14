/*
 * Calls through function pointers: plain, through *, through an array
 * element and through a pointer to a function pointer.  Each call's
 * result is the function's return type -- used in arithmetic, a call
 * through t[i] once had pointer type, and q + t[1](6, 7) scaled q by 4.
 * (*pf)(...) for a pf whose function returns a pointer is left out:
 * it is CC-34 in docs/cc-audit.md.
 */
#include <stdio.h>

typedef int (*op)(int, int);

static int add(int a, int b) {
    return a + b;
}

static int mul(int a, int b) {
    return a * b;
}

static int *pick(int *a, int *b, int first) {
    return first ? a : b;
}

static op table[2] = {add, mul};

int main(void) {
    op t[2] = {add, mul};
    op f = add;
    op *pp = &f;
    int *(*pf)(int *, int *, int) = pick;
    int x = 10, y = 20;
    int r = 0, q = 5, i;

    for (i = 0; i < 2; i++) {
        r = r * 100 + t[i](6, 7);
    }
    printf("%d %d %d\n", r, q + t[1](6, 7), q * table[0](1, 2));
    printf("%d %d %d\n", (*f)(1, 2) + 1, (**pp)(3, 4) * 2, pp[0](5, 6) - 1);
    printf("%d %d\n", *pf(&x, &y, 0), *pf(&x, &y, 1) + 1);
    return 0;
}
