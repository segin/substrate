/*
 * An integer constant operand of && or || decides it or drops out, as gcc
 * folds it at every level: `0 && x` is 0 and x is not evaluated, `x && 0`
 * evaluates x and is 0, `1 || x` is 1, `x || 0` is x's truth.  cc lowered
 * them as branches, and an arm guarded by such a condition stayed, with
 * its call to a function that may not exist: Linux's
 * `if (IS_ENABLED(CONFIG_TMPFS) && is_tmpfs) return
 * shmem_init_fs_context(fc);` in a kernel without shmem.  Here
 * no_such_function is defined nowhere: the program links only if every
 * guarded call is gone.
 */
#include <stdio.h>

extern int no_such_function(int);

static int flag;
static int calls;

static int side(int v) {
    calls++;
    return v;
}

int main(int argc, char **argv) {
    int r = 0;

    (void)argv;
    flag = argc > 5;
    if (0 && flag) {
        r += no_such_function(1);
    }
    if (flag && 0) {
        r += no_such_function(2);
    }
    if (!(1 || flag)) {
        r += no_such_function(3);
    }
    if (side(0) && 0) {
        r += no_such_function(4);
    }
    r += 0 && side(1);
    r += (side(2) || 1) * 10;
    r += (1 && side(3)) * 100;
    r += (side(0) || 0) * 1000;
    printf("%d %d\n", r, calls);
    return 0;
}
