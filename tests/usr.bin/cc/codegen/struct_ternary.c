/*
 * A conditional expression of structure type: the structure of whichever
 * arm is taken, copied whole -- from a global and from a local, of 16 bytes
 * and of a size that is not a register's.
 */
#include <stdio.h>

struct s16 {
    int a[4];
};

struct s7 {
    char c[7];
};

static struct s16 g16 = {{1, 2, 3, 4}};
static struct s7 g7 = {{'g', 'l', 'o', 'b', 'a', 'l', 0}};

int main(void) {
    volatile int seven = 7;
    struct s16 l16 = {{5, 6, 7, 8}};
    struct s7 l7 = {{'l', 'o', 'c', 'a', 'l', 0, 0}};
    struct s16 p = seven > 3 ? g16 : l16;
    struct s16 q = seven < 3 ? g16 : l16;
    struct s7 r = seven > 3 ? g7 : l7;
    struct s7 t = seven < 3 ? g7 : l7;

    printf("%d %d %d %d | %d %d | %s %s\n", p.a[0], p.a[1], p.a[2], p.a[3], q.a[0], q.a[3], r.c, t.c);
    return 0;
}
