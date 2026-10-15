/*
 * Static local arrays with initializers: lookup tables inside functions.
 * A two-dimensional one was filled at run time by code that took each
 * scalar for a whole row and copied a row from that address, so any
 * program using one died of SIGSEGV.  They are now laid out as data, as
 * file-scope arrays are, and keep their values from call to call.
 */
#include <stdio.h>

struct pt {
    int x;
    int y;
};

static int flat(int i, int j) {
    static int a[3][4] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    return a[i][j];
}

static int nested(int i, int j) {
    static int a[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
    return a[i][j];
}

static int ragged(int i, int j) {
    static int a[3][4] = {{1, 2}, {5}, {9, 10, 11}};
    return a[i][j];
}

static long cube(int i, int j, int k) {
    static long c[2][2][3] = {{{1, 2, 3}, {4, 5, 6}}, {{-7, -8, -9}, {10, 11, 12}}};
    return c[i][j][k];
}

static int counter(void) {
    static int hits[2][2] = {{100, 0}, {0, 0}};
    return ++hits[0][0];
}

static double weights(int i) {
    static double w[] = {0.5, 1.25, -2.0};
    return w[i];
}

static unsigned char bytes(int i, int j) {
    static unsigned char b[2][3] = {{0xff, 0x80, 1}, {2, 3}};
    return b[i][j];
}

static const char *names(int i) {
    static const char *n[2] = {"zero", "one"};
    return n[i];
}

static int points(int i) {
    static struct pt p[2] = {{1, 2}, {3, 4}};
    return p[i].x * 10 + p[i].y;
}

static int sizes(void) {
    static short s[][3] = {{1, 2, 3}, {4, 5, 6}};
    return (int)(sizeof s / sizeof s[0]);
}

/**
 * Read each table and print what it gives.
 *
 * @return 0
 */
int main(void) {
    printf("%d %d %d %d\n", flat(2, 1), flat(1, 0), nested(2, 1), nested(0, 3));
    printf("%d %d %d %d\n", ragged(0, 1), ragged(1, 1), ragged(2, 2), ragged(2, 3));
    printf("%ld %ld %ld\n", cube(0, 1, 2), cube(1, 0, 0), cube(1, 1, 1));
    counter();
    counter();
    printf("%d\n", counter());
    printf("%.2f %.2f %.2f\n", weights(0), weights(1), weights(2));
    printf("%u %u %u %u\n", bytes(0, 0), bytes(0, 1), bytes(1, 1), bytes(1, 2));
    printf("%s %s %d %d %d\n", names(0), names(1), points(0), points(1), sizes());
    return 0;
}
