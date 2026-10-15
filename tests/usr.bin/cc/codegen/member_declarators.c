/*
 * Structure members declared with nested declarators: their layout, and
 * access through them.  A pointer-to-array member was laid out as an
 * array of pointers, moving every member after it; an array of function
 * pointers or a pointer to a function returning a function pointer was
 * rejected; a two-dimensional array member could not be indexed; and
 * (*pv->pf)() for a pf returning a pointer called into the function's
 * code.
 */
#include <stddef.h>
#include <stdio.h>

typedef int row_t[4];
typedef int (*op)(int);

struct s {
    char c;
    int (*rowp)[4];
    int *ptrs[3];
    int (*fn)(int);
    int *(*pf)(void);
    int (*fns[2])(int);
    row_t rows[2];
    row_t r;
    op ops[3];
    char grid[2][3];
    int (*(*gf)(void))(int);
    long tail;
};

static int seven = 7;

static int *get_seven(void) {
    return &seven;
}

static int add1(int x) {
    return x + 1;
}

static int twice(int x) {
    return x * 2;
}

static op get_twice(void) {
    return twice;
}

/**
 * Print the layout of struct s, then read through each kind of member.
 *
 * @return 0
 */
int main(void) {
    int arr[3][4] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    struct s v;
    struct s *pv = &v;
    int i;
    int j;

    printf("%u %u %u %u %u %u %u %u %u %u %u %u %u\n", (unsigned)sizeof(struct s),
           (unsigned)offsetof(struct s, rowp), (unsigned)offsetof(struct s, ptrs), (unsigned)offsetof(struct s, fn),
           (unsigned)offsetof(struct s, pf), (unsigned)offsetof(struct s, fns), (unsigned)offsetof(struct s, rows),
           (unsigned)offsetof(struct s, r), (unsigned)offsetof(struct s, ops), (unsigned)offsetof(struct s, grid),
           (unsigned)offsetof(struct s, gf), (unsigned)offsetof(struct s, tail), (unsigned)sizeof v.rowp);
    v.rowp = arr;
    v.ptrs[2] = &arr[2][1];
    v.fn = add1;
    v.pf = get_seven;
    v.fns[0] = add1;
    v.fns[1] = twice;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 4; j++) {
            v.rows[i][j] = i * 10 + j;
        }
    }
    v.r[3] = 33;
    v.ops[2] = twice;
    v.grid[1][2] = 'g';
    v.gf = get_twice;
    v.tail = 99;
    printf("%d %d %d %d %d %d\n", v.rowp[1][2], pv->rowp[2][3], *v.ptrs[2], v.fn(1), *v.pf(), *(*pv->pf)());
    printf("%d %d %d %d %d\n", v.fns[0](4), pv->fns[1](5), v.rows[1][2], pv->rows[0][3], v.r[3]);
    printf("%d %c %d %d %ld\n", v.ops[2](6), v.grid[1][2], v.gf()(7), (*pv->gf)()(8), v.tail);
    return 0;
}
