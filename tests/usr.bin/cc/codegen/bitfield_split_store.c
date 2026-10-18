/*
 * A store to a bit-field leaves every other member alone (C11 3.14p2:
 * distinct members are distinct memory locations, a bit-field and the
 * ordinary members beside it among them).  `int b : 24` between `char a`
 * and `char c` takes bytes 1..3, and no 1-, 2-, 4- or 8-byte unit holds
 * them without a or c, so the store is made byte by byte, as gcc makes it.
 * Assignment, compound assignment, increment and an initializer in a
 * function, through a pointer and on an object.
 */
#include <stdio.h>

struct s {
    char a;
    int b : 24;
    char c;
};

struct t {
    short h;
    unsigned w : 20;
    unsigned char tail[2];
};

static void store(struct s *p, int v) {
    p->b = v;
}

int main(void) {
    struct s x = {1, 2, 3};
    struct t y = {7, 9, {5, 6}};
    struct s z;

    store(&x, -5);
    printf("%d %d %d\n", x.a, x.b, x.c);
    x.b += 1000000;
    x.b++;
    printf("%d %d %d\n", x.a, x.b, x.c);
    y.w = 0xfffff;
    printf("%d %u %d %d\n", y.h, y.w, y.tail[0], y.tail[1]);
    z.a = 'a';
    z.c = 'c';
    z.b = 0x7fffff;
    printf("%c %d %c %zu %zu\n", z.a, z.b, z.c, sizeof(struct s), sizeof(struct t));
    return 0;
}
