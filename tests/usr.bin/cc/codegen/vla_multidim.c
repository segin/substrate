/* Variable-length arrays of more than one dimension: any mix of constant
 * and run-time bounds, indexed, sized, decayed and stepped through. */
#include <stdio.h>

struct pt {
    short x;
    char tag;
    long y;
};

static long row_sum(const long *row, int m) {
    long s = 0;
    int j;

    for (j = 0; j < m; j++) {
        s += row[j];
    }
    return s;
}

static double doubles(int n) {
    double b[n][3];
    double s = 0;
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = 0; j < 3; j++) {
            b[i][j] = i + j / 4.0;
        }
    }
    for (i = 0; i < n; i++) {
        for (j = 0; j < 3; j++) {
            s += b[i][j];
        }
    }
    return s + (double)sizeof b + (double)sizeof b[0] / 1000;
}

static int chars(int n, int m) {
    char c[n][m];
    int i, j, s = 0;

    for (i = 0; i < n; i++) {
        for (j = 0; j < m; j++) {
            c[i][j] = (char)(i * m + j);
        }
    }
    for (i = 0; i < n; i++) {
        char *p = c[i];
        for (j = 0; j < m; j++) {
            s += *(p + j) * (j + 1);
        }
    }
    return s * 1000 + (int)sizeof c + (int)sizeof c[1] * 100;
}

static long longs(int m) {
    long d[2][m];
    int j;

    for (j = 0; j < m; j++) {
        d[0][j] = j;
        d[1][j] = 100 * j;
    }
    return row_sum(d[0], m) + row_sum(d[1], m) + (long)sizeof d + (long)sizeof d[1] * 1000;
}

static int cube(int a, int b, int c) {
    int t[a][b][c];
    int i, j, k, s = 0;

    for (i = 0; i < a; i++) {
        for (j = 0; j < b; j++) {
            for (k = 0; k < c; k++) {
                t[i][j][k] = i * 100 + j * 10 + k;
            }
        }
    }
    for (i = 0; i < a; i++) {
        for (j = 0; j < b; j++) {
            s += t[i][j][c - 1] - t[i][0][0];
        }
    }
    return s + (int)sizeof t + (int)sizeof t[0] * 1000 + (int)sizeof t[0][0] * 100000 + t[a - 1][b - 1][c - 1] * 10;
}

static long addresses(int n, int m) {
    int g[n][m];
    long r = 0;

    r += (char *)&g[1][0] - (char *)&g[0][0];
    r += ((char *)&g[n - 1][m - 1] - (char *)g) * 1000;
    r += ((char *)(g + 2) - (char *)g) * 1000000;
    r += (char *)*g == (char *)g;
    return r;
}

static long structs(int n) {
    struct pt q[n][2];
    long s = 0;
    int i;

    for (i = 0; i < n; i++) {
        q[i][0].x = (short)i;
        q[i][1].x = (short)(i * 2);
        q[i][0].y = i * 1000L;
        q[i][1].tag = 'a';
    }
    for (i = 0; i < n; i++) {
        s += q[i][0].x + q[i][1].x + q[i][0].y + q[i][1].tag;
    }
    return s + (long)sizeof q + (long)sizeof q[0] * 10000;
}

static int pointers(int n) {
    int vals[4] = {1, 2, 3, 4};
    int *p[n];
    int i, s = 0;

    for (i = 0; i < n; i++) {
        p[i] = &vals[i % 4];
    }
    for (i = 0; i < n; i++) {
        s += *p[i];
    }
    return s + (int)sizeof p / (int)sizeof p[0] * 100;
}

int main(void) {
    long loop = 0;
    int i;

    for (i = 1; i < 2000; i++) {
        int w[i % 7 + 1][i % 5 + 2];

        w[i % 7][i % 5 + 1] = i;
        loop += w[i % 7][i % 5 + 1] + (long)sizeof w;
    }
    printf("doubles %.3f\n", doubles(5));
    printf("chars %d\n", chars(3, 7));
    printf("longs %ld\n", longs(6));
    printf("cube %d\n", cube(2, 3, 4));
    printf("addresses %ld\n", addresses(4, 5));
    printf("structs %ld\n", structs(3));
    printf("pointers %d\n", pointers(9));
    printf("loop %ld\n", loop);
    return 0;
}
