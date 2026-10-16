/* The address of a GNU nested function that uses the enclosing function's
 * variables: called through a pointer, passed to qsort and to a callback,
 * it reaches them through a trampoline, as gcc builds one. */
#include <stdio.h>
#include <stdlib.h>
static int apply(int (*fn)(int), int v) { return fn(v); }
static void each(int *a, int n, void (*cb)(int *)) { int i; for (i = 0; i < n; i++) cb(&a[i]); }
int run(int base) {
    int total = 0;
    int scale = 3;
    int add(int y) { return y + base + total; }
    void acc(int *p) { total += *p * scale; }
    int by_key(const void *a, const void *b) { int x = *(const int *)a % base, y = *(const int *)b % base; return (x > y) - (x < y); }
    int v[5] = {7, 2, 9, 4, 5};
    int (*fp)(int) = add;
    each(v, 5, acc);
    qsort(v, 5, sizeof v[0], by_key);
    printf("%d %d %d %d%d%d%d%d\n", apply(add, 1), fp(10), total, v[0], v[1], v[2], v[3], v[4]);
    return total;
}
int main(void) { return run(4) == 81 ? 0 : 1; }
