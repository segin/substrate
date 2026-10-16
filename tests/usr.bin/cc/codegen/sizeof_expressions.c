/*
 * sizeof of expressions built on variables with nested declarators.
 * Subscripting or dereferencing an array of pointers to arrays, taking
 * the address of an array element, and pointer arithmetic on such an
 * array gave the sizes the legacy type encoding computed (rows[0] as an
 * array of 4 pointers, &grid[1] as the 30-byte row it points to).
 */
#include <stdio.h>

typedef int row_t[4];

struct s {
    char c;
    int (*rowp)[4];
    int rows[2][4];
    int (*fns[3])(int);
    long (*cube)[2][3];
};

int (*grows[2])[4];
long gcube[2][3][5];

#define P(e) printf("%s=%u\n", #e, (unsigned)sizeof(e))

/**
 * Print the size of each expression.
 *
 * @return 0
 */
int main(void) {
    int (*rows[2])[4];
    long (*cube)[2][3];
    char grid[2][3][5];
    row_t r2[3];
    int (*fns[3])(int);
    struct s v;
    struct s *pv = &v;

    P(rows[0]);
    P(*rows[0]);
    P((*rows[1])[2]);
    P(*cube);
    P(**cube);
    P(cube[1]);
    P(grid[1]);
    P(grid[1][2]);
    P(r2[1]);
    P(*r2);
    P(fns[0]);
    P(v.rowp);
    P(*v.rowp);
    P(v.rows[1]);
    P(pv->rows);
    P(*pv->cube);
    P(v.fns);
    P(grows[1]);
    P(*grows[1]);
    P(gcube[1]);
    P(gcube[1][2]);
    P(&rows);
    P(&grid[1]);
    P(rows + 1);
    P(&r2[2][1]);
    return 0;
}
