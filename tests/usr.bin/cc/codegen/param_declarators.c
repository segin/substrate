/*
 * Parameter declarators: arrays and functions adjusted to pointers,
 * array-bound qualifiers, parenthesized names, pointers to arrays and to
 * functions, typedef'd arrays, and abstract declarators in prototypes.
 */
#include <stdio.h>

typedef int row_t[4];

static int sum_static(int a[static 3]) {
    return a[0] + a[1] + a[2];
}

static int first_restrict(int a[restrict], int b[const 2]) {
    return a[0] * 10 + b[1];
}

static int paren_name(int (x)) {
    return x * 2;
}

static int apply(int f(int), int v) {
    return f(v);
}

static int apply_ptr(int (*f)(int), int v) {
    return f(v);
}

static int twice(int v) {
    return v * 2;
}

static int grid_get(int g[][4], int r, int c) {
    return g[r][c];
}

static int row_ptr_get(int (*g)[4], int r, int c) {
    return g[r][c];
}

static int typedef_row(row_t r) {
    return r[3];
}

static int typedef_rows(row_t *rows, int i) {
    return rows[i][2];
}

static int seven = 7;

static int *get_seven(void) {
    return &seven;
}

static int call_returning_ptr(int *(*pf)(void)) {
    return *pf();
}

static int argv_len(char *argv[]) {
    int n = 0;
    while (argv[n] != 0) {
        ++n;
    }
    return n;
}

static int (*pick(int which, int (*a)(int), int (*b)(int)))(int) {
    return which ? a : b;
}

static unsigned sizes(int a[10], int g[][4], row_t r, int f(int)) {
    return (unsigned)(sizeof a + sizeof g + sizeof r + sizeof f + sizeof *g + sizeof g[0][0]);
}

static int proto_only(int[], int (*)(int), int *(*)(void));

static int proto_only(int a[], int (*f)(int), int *(*pf)(void)) {
    return a[1] + f(1) + *pf();
}

/**
 * Exercise each parameter shape and print what it gives.
 *
 * @return 0
 */
int main(void) {
    int arr[3] = {1, 2, 3};
    int grid[2][4] = {1, 2, 3, 4, 5, 6, 7, 8};
    row_t rows[2] = {10, 11, 12, 13, 14, 15, 16, 17};
    char *names[] = {"a", "b", "c", 0};

    printf("%d %d %d %d %d\n", sum_static(arr), first_restrict(arr, arr), paren_name(21), apply(twice, 4),
           apply_ptr(twice, 5));
    printf("%d %d %d %d %d %d\n", grid_get(grid, 1, 2), row_ptr_get(grid, 0, 3), typedef_row(rows[1]),
           typedef_rows(rows, 1), call_returning_ptr(get_seven), argv_len(names));
    printf("%d %d %u %d\n", pick(1, twice, twice)(8), pick(0, twice, twice)(9),
           sizes(arr, grid, rows[0], twice) / (unsigned)sizeof(void *), proto_only(arr, twice, get_seven));
    return 0;
}
