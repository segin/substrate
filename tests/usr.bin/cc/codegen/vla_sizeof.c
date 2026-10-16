/*
 * sizeof of a variable-length array, and of a type name with a run-time
 * bound, is computed when the sizeof is evaluated.  Both were rejected:
 * "unsupported sizeof operand in lowering" and "sizeof unsupported for
 * this type".
 */
#include <stdio.h>

struct pair {
    int x;
    int y;
};

/**
 * Print sizes that depend on n and m.
 *
 * @param n  one bound
 * @param m  another
 */
static void sizes(int n, int m) {
    int a[n];
    char c[n];
    long l[n + m];
    struct pair s[n];
    double d[n * 2];
    int i = 0;
    unsigned long typed = sizeof(int[++i + n]);

    a[n - 1] = n;
    l[0] = m;
    printf("%lu %lu %lu %lu %lu %lu ", (unsigned long)sizeof a, (unsigned long)sizeof c,
           (unsigned long)(sizeof l / sizeof l[0]), (unsigned long)sizeof s, (unsigned long)sizeof d,
           (unsigned long)sizeof(c));
    printf("%lu %d %lu %lu %lu %d %ld\n", typed, i, (unsigned long)sizeof(double[n + m]),
           (unsigned long)sizeof(char[n][3]), (unsigned long)(sizeof(int (*)[n]) == sizeof(void *)), a[n - 1],
           l[0]);
}

/**
 * Call sizes() with a few bounds.
 *
 * @return 0
 */
int main(void) {
    sizes(5, 4);
    sizes(3, 7);
    sizes(1, 1);
    return 0;
}
