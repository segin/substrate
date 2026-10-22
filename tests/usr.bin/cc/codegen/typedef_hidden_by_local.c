/*
 * An ordinary identifier declared in an inner scope hides a typedef of
 * the same name there (C11 6.2.1p4).  Linux's PAT code has
 * `typedef __u8 mtrr_type;` and, in a function, `u8 mtrr_type, uniform;`
 * followed by `mtrr_type = mtrr_type_lookup(...)`, which cc took for a
 * declaration ("expected identifier after declaration type").
 */
#include <stdio.h>

typedef unsigned char mtrr_type;
typedef int T;

static int look(int *u) {
    *u = 1;
    return 6;
}

int main(void) {
    int r = 0;

    {
        unsigned char mtrr_type, uniform;

        mtrr_type = look(&r);
        uniform = mtrr_type * 2;
        r += uniform;
    }
    mtrr_type after = 4; /* the typedef again, out of the block */
    {
        T T = 5; /* an object T of type T; from here T is the object */

        r += T * after;
    }
    printf("%d %d\n", r, (int)sizeof(mtrr_type));
    return 0;
}
