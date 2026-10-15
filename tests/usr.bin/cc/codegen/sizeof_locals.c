/*
 * sizeof of local variables declared with nested declarators.  The legacy
 * type encoding cannot tell an array of pointers to arrays from an array
 * of arrays, so `sizeof rows` below came out as the size of the latter.
 */
#include <stdio.h>

static int inc(int x) {
    return x + 1;
}

/**
 * Print the size of each local, and use each one so the sizes are of
 * live objects.
 *
 * @return 0
 */
int main(void) {
    int arr[3][4] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    int (*row)[4] = arr;
    int *ptrs[2];
    int (*rows[2])[4];
    int (*fns[3])(int);
    char grid[2][3][5];
    int auto_len[] = {1, 2, 3, 4, 5};
    long (*cube)[2][3];

    ptrs[0] = &arr[0][1];
    ptrs[1] = &arr[2][3];
    rows[0] = &arr[0];
    rows[1] = &arr[1];
    fns[0] = inc;
    fns[1] = inc;
    fns[2] = inc;
    grid[1][2][4] = 'x';
    cube = 0;
    printf("arr %u row %u ptrs %u rows %u fns %u grid %u auto_len %u cube %u\n", (unsigned)sizeof arr,
           (unsigned)sizeof row, (unsigned)sizeof(ptrs), (unsigned)sizeof rows, (unsigned)sizeof fns,
           (unsigned)sizeof grid, (unsigned)sizeof auto_len, (unsigned)sizeof cube);
    printf("%d %d %d %d %c %d %d\n", row[1][2], *ptrs[1], (*rows[1])[3], fns[2](41), grid[1][2][4], auto_len[4],
           cube == 0);
    return 0;
}
