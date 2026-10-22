/*
 * From -O1 a value set on several paths is known after they join when
 * every path gives it the same constant -- `a || b` with b constant
 * true is true whatever a is -- and x & 0 and x * 0 are 0, as gcc folds
 * them; run_inline_dead_call.sh checks the calls they guard go.  The
 * values must be what the paths gave: paths that agree and paths that do
 * not, a constant operand on either side of || and &&, and a variable a
 * loop sets, which a join from later in the code must not take as known.
 */
#include <stdio.h>

static inline int always(void) {
    return 1;
}

static inline int never(void) {
    return 0;
}

int main(int argc, char **argv) {
    int k = argc - 1; /* 0, unknown to the compiler */
    unsigned flags = 0x1234u + (unsigned)k;
    int same = k ? 7 : 7;
    int differ = k ? 7 : 8;
    int or_true = (k > 5) || always();
    int or_var = (k > 5) || never();
    int and_false = (k == 0) && never();
    int and_var = (k == 0) && always();
    int masked = (int)(flags & 0u);
    int times_zero = (int)flags * 0;
    int looped = 0;
    int i;

    (void)argv;
    for (i = 0; i < 3; ++i) {
        looped = looped ? looped * 2 : 1;
    }
    printf("%d %d %d %d %d %d %d %d %d\n", same, differ, or_true, or_var, and_false, and_var, masked, times_zero,
           looped);
    return 0;
}
