/*
 * Casts and compound literals of nested declarator types.  A cast's type
 * was kept only in the legacy encoding, so sizeof of a subscript through
 * `(int (*)[4])p` failed in lowering, and indexing through such a cast
 * loaded from the array instead of taking its address and crashed.
 */
#include <stdio.h>

#define P(e) printf("%s=%ld\n", #e, (long)(e))

/**
 * Index, size and step through casts to pointers to arrays.
 *
 * @return 0
 */
int main(void) {
    int a[3][4] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    void *p = a;
    char grid[2][3][5];
    void *g = grid;
    long (*cube)[2][3] = 0;
    unsigned u8[4];
    int lit = ((int[]){5, 6, 7})[2];

    P(((int (*)[4])p)[2][1]);
    P((*(int (*)[4])p)[3]);
    P(sizeof *(int (*)[4])p);
    P(sizeof((int (*)[4])p)[1]);
    P((char *)((int (*)[4])p + 2) - (char *)p);
    P((char *)((char (*)[3][5])g + 1) - (char *)g);
    P(sizeof((char (*)[3][5])g)[1][2]);
    P(sizeof(int[7]){0});
    P(lit);
    P(sizeof((long (*)[2][3])cube));
    P((char *)((long (*)[2][3])cube + 1) - (char *)cube);
    P((char *)u8 + 3 - (char *)u8);
    return 0;
}
