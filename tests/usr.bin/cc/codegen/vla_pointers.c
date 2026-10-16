/* Pointers to variable-length arrays, and parameters declared as arrays
 * with run-time bounds (adjusted to such pointers): they step, subscript
 * and size by the array's run-time size; sizeof of an array type name
 * with run-time bounds is computed. */
#include <stdio.h>

static int get(int n, int m, int a[n][m], int i, int j) {
    return a[i][j] + (int)sizeof a[0] * 100 + (int)sizeof *a * 10000 + (int)sizeof a[0][0] * 1000000 + n * 0;
}

static int get_ptr(int m, int (*a)[m]) {
    return a[1][2] + (int)sizeof *a * 100;
}

static long sum3(int x, int y, int z, long t[x][y][z]) {
    long s = 0;
    int i, j, k;

    for (i = 0; i < x; i++) {
        for (j = 0; j < y; j++) {
            for (k = 0; k < z; k++) {
                s += t[i][j][k] * (i + 1);
            }
        }
    }
    return s + (long)sizeof t[0] * 1000 + (long)sizeof t[0][0] * 100000;
}

static int mixed(int n, int a[n][4]) {
    return a[n - 1][3] + (int)sizeof a[0];
}

int rows_walked(int n, int m, int a[n][m]);

int rows_walked(int n, int m, int a[n][m]) {
    int (*p)[m] = a;
    int (*end)[m] = a + n;
    int s = 0;

    while (p < end) {
        s += (*p)[m - 1];
        ++p;
    }
    p = a;
    p += 1;
    s += p[0][0] * 1000;
    p++;
    s += (*p)[1] * 100000;
    s += (int)(end - p) * 10000000 + (int)(p - a) * 100000000;
    return s;
}

int main(int argc, char **argv) {
    int x[3][5];
    long t[2][3][4];
    int q[3][4];
    int k;
    int n = argc + 2, m = argc + 3;
    int local[n][m];
    int (*lp)[m] = local;

    (void)argv;
    for (k = 0; k < 15; k++) {
        x[k / 5][k % 5] = k;
    }
    for (k = 0; k < 24; k++) {
        t[k / 12][k / 4 % 3][k % 4] = k;
    }
    for (k = 0; k < 12; k++) {
        q[k / 4][k % 4] = k * 3;
    }
    for (k = 0; k < n * m; k++) {
        local[k / m][k % m] = k;
    }
    printf("get %d %d\n", get(3, 5, x, 2, 3), get(3, 5, x, 1, 4));
    printf("get_ptr %d\n", get_ptr(5, x));
    printf("sum3 %ld\n", sum3(2, 3, 4, t));
    printf("mixed %d\n", mixed(3, q));
    printf("rows %d\n", rows_walked(3, 5, x));
    printf("local %d %d %d\n", lp[2][3], (int)sizeof *lp, (int)sizeof lp);
    printf("sizeof %d %d %d %d\n", (int)sizeof(int[n][m]), (int)sizeof(int[n]), (int)sizeof(double[3][m]),
           (int)sizeof(char[n][2][m]));
    return 0;
}
