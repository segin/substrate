/*
 * Function definitions with nested declarators.  A function returning a
 * pointer to an array, int (*f(void))[4], did not parse; nor did one
 * returning a pointer to a function returning a function pointer,
 * int (*(*f(void))(int))(int); and the result of the first, once it
 * parsed, could not be indexed or sized.
 */
#include <stdio.h>

typedef int (*op)(int);

static int tbl[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};

static int add1(int x) {
    return x + 1;
}

static int dbl(int x) {
    return x * 2;
}

static void handler(int s) {
    (void)s;
}

int (*row_of(int i))[4] {
    return &tbl[i];
}

int (*pick(int which))(int) {
    return which ? dbl : add1;
}

op pick2(int which) {
    return which ? dbl : add1;
}

static int (*mk_inner(int w))(int) {
    return w ? dbl : add1;
}

int (*(*maker(void))(int))(int);

int (*(*maker(void))(int))(int) {
    return mk_inner;
}

void (*signal_like(int sig, void (*fn)(int)))(int) {
    (void)sig;
    return fn;
}

static int *(ptr_ret)(int *p) {
    return p + 1;
}

int (sum)(int a, int b) {
    return a + b;
}

/**
 * Call each function and print what it gives.
 *
 * @return 0
 */
int main(void) {
    printf("%d %d %d\n", (*row_of(1))[2], row_of(2)[0][3], (*row_of(0))[0]);
    printf("%d %d %d %d\n", pick(1)(5), (*pick(0))(5), pick2(0)(5), maker()(1)(7));
    printf("%d %d %d\n", signal_like(2, handler) == handler, *ptr_ret(&tbl[0][0]), sum(3, 4));
    printf("%u %u\n", (unsigned)sizeof *row_of(0), (unsigned)sizeof row_of(0));
    return 0;
}
