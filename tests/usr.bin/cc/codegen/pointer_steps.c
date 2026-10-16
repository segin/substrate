/*
 * Pointer arithmetic and indexing through nested declarators: each step
 * is the size of what the pointer points to.  The step came from the
 * legacy type encoding, which got it wrong for an array of pointers to
 * arrays (rows + 1 stepped 32 bytes, not 8, so rows[1] = ... wrote past
 * the array), a pointer to an array taken with & (&grid[1] + 1 stepped
 * one inner row), and an element of such an array (&rows[0][1]).
 */
#include <stdio.h>

typedef int row_t[4];

int g2[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
long gcube[2][3][4];
int (*grows[2])[4] = {&g2[0], &g2[2]};

#define P(e) printf("%s=%ld\n", #e, (long)(e))

/**
 * Read through each kind of pointer and print the distances.
 *
 * @return 0
 */
int main(void) {
    int a[3][4] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    int (*rows[2])[4];
    long (*cube)[3][4] = gcube;
    char grid[2][3][5];
    row_t r2[3];
    int (*rp)[4] = a;
    int i;
    int j;
    int k;

    for (i = 0; i < 2; i++) {
        for (j = 0; j < 3; j++) {
            for (k = 0; k < 4; k++) {
                gcube[i][j][k] = i * 100 + j * 10 + k;
            }
            for (k = 0; k < 5; k++) {
                grid[i][j][k] = (char)('a' + i * 15 + j * 5 + k);
            }
        }
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 4; j++) {
            r2[i][j] = i * 4 + j;
        }
    }
    rows[0] = &a[1];
    rows[1] = &a[2];
    P((*rows[0])[2]);
    P(rows[1][0][3]);
    P(*rows[1][0]);
    P((*grows[1])[1]);
    P(grows[0][0][3]);
    P(cube[1][2][3]);
    P((*(cube + 1))[1][2]);
    P(*(*(*(cube + 1) + 2) + 1));
    P(grid[1][2][3]);
    P(*(*(grid[1] + 1) + 4));
    P((&grid[1])[0][0][0]);
    P((char *)(&grid[1] + 1) - (char *)grid);
    P(r2[2][3]);
    P((*(r2 + 1))[2]);
    P(rp[2][1]);
    P((rp + 1)[1][3]);
    P((char *)(rp + 2) - (char *)a);
    P((char *)(rows + 1) - (char *)rows);
    P((char *)&rows[0][1] - (char *)rows[0]);
    P(&rows[1] - &rows[0]);
    P(&grid[1][2] - &grid[1][0]);
    return 0;
}
