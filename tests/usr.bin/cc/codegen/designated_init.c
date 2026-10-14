/*
 * Designated initializers: an undesignated element follows the last
 * element written, even when a designator went backwards (C11 6.7.9p17),
 * and may overwrite one written earlier.
 */
#include <stdio.h>

struct S {
    int a, b, c;
};

int g[6] = {[4] = 40, [1] = 10, 11};
static int s[4] = {[2] = 2, 3, [0] = 9};
struct S gs[3] = {[2] = {.c = 3}, [0] = {1, 2}, {.b = 5}};

static void show(const int *v, int n) {
    int i;
    for (i = 0; i < n; i++) {
        printf("%d ", v[i]);
    }
    printf("| ");
}

int main(void) {
    int l[6] = {[4] = 40, [1] = 10, 11};
    int m[5] = {1, [3] = 3, 4};
    int o[5] = {1, 2, 3, [1] = 7, 8};
    int r[8] = {[1 ... 3] = 5, 6, [0] = 1};
    struct S st = {.c = 3, .a = 1};
    int i;

    show(g, 6);
    show(s, 4);
    show(l, 6);
    show(m, 5);
    show(o, 5);
    show(r, 8);
    printf("\n%d %d %d\n", st.a, st.b, st.c);
    for (i = 0; i < 3; i++) {
        printf("%d %d %d\n", gs[i].a, gs[i].b, gs[i].c);
    }
    return 0;
}
