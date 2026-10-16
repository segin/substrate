/*
 * A static object's flexible array member given elements by its
 * initializer (a GNU extension).  The object was sized for them, but the
 * elements were dropped and the tail left zero.
 */
#include <stdio.h>

struct item {
    unsigned char a;
    unsigned char b;
};

struct packet {
    unsigned char tag;
    struct item items[];
};

struct ints {
    int n;
    int v[];
};

struct wide {
    long tag;
    struct item it[];
};

struct ptrs {
    int n;
    int *ptr[];
};

static int g = 42;

struct packet pkt = {9, {1, 2, 3}};
struct packet pkt2 = {7, {{4, 5}, {6}, 7, 8}};
struct ints vi = {3, {10, 20, 30}};
struct wide w = {.tag = 5, .it = {{9, 8}}};
struct ptrs ps = {2, {&g, 0}};

/**
 * Print every initialized element and the objects' sizes.
 *
 * @return 0
 */
int main(void) {
    printf("%d %d %d %d %d\n", pkt.tag, pkt.items[0].a, pkt.items[0].b, pkt.items[1].a, pkt.items[1].b);
    printf("%d %d %d %d %d %d %d\n", pkt2.tag, pkt2.items[0].a, pkt2.items[0].b, pkt2.items[1].a, pkt2.items[1].b,
           pkt2.items[2].a, pkt2.items[2].b);
    printf("%d %d %d %d\n", vi.n, vi.v[0], vi.v[1], vi.v[2]);
    printf("%ld %d %d\n", w.tag, w.it[0].a, w.it[0].b);
    printf("%d %d %d\n", ps.n, *ps.ptr[0], ps.ptr[1] == 0);
    printf("%u %u\n", (unsigned)sizeof pkt, (unsigned)sizeof vi);
    return 0;
}
