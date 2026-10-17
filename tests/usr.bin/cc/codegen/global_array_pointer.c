/*
 * A file-scope pointer to an array is a pointer: reading it loads its
 * value.  cc took it for the array it points to (the legacy encoding
 * gives `int (*q)[2]` the dimensions of `int q[2]`), so `(*q)[i]` read q
 * itself and `l = q` stored q's address.
 */
#include <stdio.h>

static int arr[2] = {7, 8};
static int grid[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
static int (*q)[2] = &arr;
static int (*rows)[4] = grid;
int (*ext)[4] = &grid[1];

int main(void) {
    int (*l)[2] = q;

    (*q)[1] += 10;
    rows[2][3] *= 2;
    printf("%d %d %d %d %d %d %d\n", (*q)[0], (*q)[1], (*l)[1], rows[1][2], rows[2][3], (*ext)[0], ext[1][1]);
    printf("%u %u\n", (unsigned)sizeof *q, (unsigned)sizeof *rows);
    return 0;
}
