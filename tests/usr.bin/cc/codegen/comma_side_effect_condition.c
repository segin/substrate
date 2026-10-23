/*
 * A comma expression is a constant only if its left operand is one:
 * `(list = &...[t], 1)` has the value 1, but the assignment is made.  cc
 * took any comma whose right operand was a constant for that constant,
 * and a condition known so was never evaluated: Linux's
 * for_each_free_list() -- `t < MIGRATE_TYPES && (list = ..., 1)` -- left
 * list NULL, and the kernel wrote through it in early boot.
 */
#include <stdio.h>

int main(void) {
    int list = 0, sum = 0, side = 0;
    int x;

    for (int t = 0; t < 4 && (list = t * 10, 1); t++) {
        sum += list;
    }
    if (side == 0 && (side = 5, 1)) {
        sum += side;
    }
    x = (side = 7, 1) ? side : -1;
    while ((side++, 0)) {
        sum = -1;
    }
    printf("sum %d side %d x %d\n", sum, side, x);
    return 0;
}
