/*
 * Local multi-dimensional arrays initialized row by row.  A braced row,
 * `int a[2][2] = {{1, 2}, {3, 4}};`, did not compile ("initializer list
 * cannot be used as runtime expression in lowering"), and a list of
 * strings for rows of characters, `char s[2][6] = {"hello", "world"};`,
 * compiled and filled the array with garbage.
 */
#include <stdio.h>

struct pt {
    int x;
    int y;
};

/**
 * Initialize each kind of array and print what it holds.
 *
 * @param argc  used to give one row an element only known at run time
 * @param argv  unused
 * @return      0
 */
int main(int argc, char **argv) {
    int a[2][2] = {{1, 2}, {3, 4}};
    int r[3][4] = {{1, 2}, {5}, {9, 10, 11}};
    long c[2][2][3] = {{{1, 2, 3}, {4, 5, 6}}, {{-7, -8, -9}, {10, 11, 12}}};
    char s[2][6] = {"hello", "world"};
    char t[2][6] = {{"abc"}, {"de"}};
    int mix[2][3] = {{argc, argc + 1, 3}, 4, 5, 6};
    double d[2][2] = {{0.5, 1.5}, {2.5}};
    struct pt sp[2][2] = {{{1, 2}, {3, 4}}, {{5, 6}}};
    unsigned char b[2][3] = {{0xff, 0x80}, {1, 2, 3}};
    int i;
    int j;
    int k;

    (void)argv;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            printf("%d ", a[i][j]);
        }
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 4; j++) {
            printf("%d ", r[i][j]);
        }
    }
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            for (k = 0; k < 3; k++) {
                printf("%ld ", c[i][j][k]);
            }
        }
    }
    printf("%s %s %s %s %d ", s[0], s[1], t[0], t[1], t[1][5]);
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 3; j++) {
            printf("%d ", mix[i][j]);
        }
    }
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            printf("%.1f %d,%d ", d[i][j], sp[i][j].x, sp[i][j].y);
        }
    }
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 3; j++) {
            printf("%u ", b[i][j]);
        }
    }
    printf("\n");
    return 0;
}
