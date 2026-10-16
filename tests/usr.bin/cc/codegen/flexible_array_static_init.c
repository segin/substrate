/*
 * A static object's flexible array member given elements by its
 * initializer (a GNU extension).  The object was sized for them, but the
 * elements were dropped and the tail left zero.  A string was sized
 * without its terminating null, and a braced string did not compile.
 */
#include <stdio.h>
#include <wchar.h>

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

struct text {
    int n;
    char s[];
};

struct wtext {
    int n;
    wchar_t s[];
};

static int g = 42;

struct text t1 = {3, "abc"};
char after_t1 = 'Z';
struct text t2 = {2, {"hi"}};
struct wtext wt = {1, L"wq"};
struct ints one = {1, 5};

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
    printf("%s %s %c %d %d %d %d\n", t1.s, t2.s, after_t1, wt.s[0], wt.s[1], wt.s[2], one.v[0]);
    printf("%u %u\n", (unsigned)sizeof pkt, (unsigned)sizeof vi);
    return 0;
}
