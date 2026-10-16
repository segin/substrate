/* GNU C nested functions using the enclosing function's variables: reads
 * and writes reach the enclosing function's objects, through calls from
 * the enclosing function, from other nested functions, recursively, and
 * from a function nested in a nested function. */
#include <stdio.h>
#include <stdlib.h>

struct pt {
    int x;
    long y;
};

static int outer(int base, int n) {
    int x = 3;
    long total = 0;
    static int calls;
    struct pt p = {1, 2};
    int arr[4] = {10, 20, 30, 40};
    int *px = &x;
    int add(int y) { return x + y + base; }
    void bump(int by) {
        x += by;
        total += by;
        p.y += by;
        arr[1] += by;
        calls++;
    }
    int fact(int k) { return k <= 1 ? 1 : k * fact(k - 1); }
    int twice(int y) { return add(y) + add(y); }
    int cmp(const void *a, const void *b) {
        int ia = *(const int *)a, ib = *(const int *)b;
        return (ia > ib) - (ia < ib);
    }
    int base_hidden(int base) { return base + x; }
    int deep(int k) {
        int scale = 10;
        int inner(int j) { return j * scale + x + base; }
        return inner(k) + inner(k + 1);
    }
    int v[3] = {3, 1, 2};

    bump(5);
    qsort(v, 3, sizeof v[0], cmp);
    {
        int x = 1000;
        total += add(x);
    }
    *px += 1;
    printf("add %d total %ld p.y %ld arr %d fact %d twice %d v %d%d%d calls %d\n", add(1), total, p.y, arr[1],
           fact(n), twice(2), v[0], v[1], v[2], calls);
    printf("hidden %d deep %d x %d\n", base_hidden(7), deep(2), x);
    return x;
}

int main(void) {
    printf("ret %d\n", outer(100, 5));
    printf("ret %d\n", outer(1, 3));
    return 0;
}
