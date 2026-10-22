/*
 * Local objects of size 0 -- a GNU zero-length array, an array of empty
 * structures, an array with a zero inner bound -- have an address and a
 * sizeof of 0, as gcc gives them.  cc refused them while laying out the
 * frame ("unsupported hoisted local array element type"); Linux reaches
 * them through config-dependent array sizes.
 */
#include <stdio.h>

struct empty {};

static int f(int k) {
    char z[0];
    struct empty es[3];
    int y[2][0];
    int after = k;

    printf("%d %d %d\n", (int)sizeof z, (int)sizeof es, (int)sizeof y);
    printf("%d\n", (void *)z != (void *)0 && (void *)es != (void *)0 && (void *)y != (void *)0);
    return after * 2;
}

int main(void) {
    printf("%d\n", f(21));
    return 0;
}
