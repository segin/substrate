/*
 * Arrays of more than four dimensions and pointers of more than four
 * levels: constant arrays with and without initializers, a static local,
 * a variable-length array, sizeof of a variable-length array type, and a
 * parameter that points to a variable-length array of five dimensions.
 * The values are gcc's.
 */
int g[2][2][2][2][2][3] = {{{{{{1, 2, 3}}}}}, {{{{{0}, {7, 8, 9}}}}}};

static long sum6(int a, int b, int c, int d, int e, int f, int (*v)[b][c][d][e][f]) {
    long s = 0;

    for (int i = 0; i < a; ++i)
        for (int j = 0; j < b; ++j)
            for (int k = 0; k < c; ++k)
                for (int l = 0; l < d; ++l)
                    for (int m = 0; m < e; ++m)
                        for (int n = 0; n < f; ++n)
                            s += v[i][j][k][l][m][n] * (n + 1);
    return s;
}

int main(void) {
    int n = 2;
    int m = 3;
    int v[n][n][n][n][m];
    static int st[2][1][1][1][1][2] = {{{{{{4, 5}}}}}, {{{{{6, 7}}}}}};
    int x = 42, *p1 = &x, **p2 = &p1, ***p3 = &p2, ****p4 = &p3, *****p5 = &p4, ******p6 = &p5;
    int *******p7 = &p6, ********p8 = &p7, *********p9 = &p8, **********p10 = &p9;
    long t = 0;

    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b)
            for (int c = 0; c < n; ++c)
                for (int d = 0; d < n; ++d)
                    for (int e = 0; e < m; ++e)
                        v[a][b][c][d][e] = a + 2 * b + 3 * c + 4 * d + 5 * e;
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b)
            for (int c = 0; c < n; ++c)
                for (int d = 0; d < n; ++d)
                    for (int e = 0; e < m; ++e)
                        t += v[a][b][c][d][e];
    if (sizeof g != 384 || sizeof(int[n][n][m][n][2]) != 192 || sizeof(v[1][1]) != 48) {
        return 1;
    }
    if (t != 480) {
        return 2;
    }
    if (g[0][0][0][0][0][2] != 3 || g[1][0][0][0][1][1] != 8 || st[1][0][0][0][0][1] != 7) {
        return 3;
    }
    if (**********p10 != 42) {
        return 4;
    }
    return sum6(2, 2, 2, 2, 2, 3, g) == 64 ? 0 : 5;
}
