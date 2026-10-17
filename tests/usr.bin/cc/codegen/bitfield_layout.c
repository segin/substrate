/*
 * Bit-fields are allocated as the System V psABI and gcc allocate them:
 * at the next free bit, unless the field would then cross a boundary of
 * its type's alignment its type's size does not allow, so a bit-field may
 * share the bytes of the members before it.  Unnamed and zero-width
 * bit-fields do not raise the structure's alignment; `aligned` moves a
 * bit-field; packed ones start at any bit.  Each field is set to all ones
 * in a zeroed object, and the bytes show where it lives; then the
 * structures are initialized, copied and passed by value.
 */
#include <stdio.h>
#include <string.h>

#define SHOW(T, f)                                                                                      \
    do {                                                                                                \
        T o;                                                                                            \
        size_t i;                                                                                       \
        memset(&o, 0, sizeof(o));                                                                       \
        o.f = -1;                                                                                       \
        printf("  " #f ":");                                                                            \
        for (i = 0; i < sizeof(o); ++i)                                                                 \
            printf(" %02x", ((unsigned char *)&o)[i]);                                                  \
        printf("\n");                                                                                   \
    } while (0)
#define SIZE(T) printf(#T " size %u align %u\n", (unsigned)sizeof(T), (unsigned)_Alignof(T))

struct s1 { char a; int b : 3; short e; };
struct s2 { char a; int b : 30; char c; };
struct s3 { short a; int b : 17; int c : 17; };
struct s4 { char a; long long b : 40; char c; };
struct s5 { char a; int : 0; char b; };
struct s6 { char a; int : 5; char b; };
struct s7 { int a : 3; char b; int c : 7; };
struct s8 { char a; long long b : 8; };
struct s9 { unsigned char a : 4; unsigned short b : 9; unsigned char c : 7; };
struct __attribute__((packed)) s10 { char a; int b : 30; char c; };
struct s11 { char a; int b : 4 __attribute__((aligned(8))); };
struct s12 { _Bool a : 1; long long b : 63; int c : 2; };
union u1 { char a; int b : 3; };
union u2 { char a; long long b : 33; };
struct s13 { char a; struct { int x : 4; } in; int b : 4; };
struct s14 { long a : 20; long b : 20; long c : 30; };

static struct s1 g1 = {7, -2, 300};
static struct s9 g9 = {.c = 100, .a = 9, .b = 400};
static struct s4 g4 = {1, 0x7f12345678LL, 3};

static int sum1(struct s1 v) { return v.a + v.b + v.e; }
static long long sum4(struct s4 v) { return v.a + v.b + v.c; }
static struct s9 make9(int a, int b, int c) {
    struct s9 r = {(unsigned char)a, (unsigned short)b, (unsigned char)c};
    return r;
}

int main(void) {
    struct s1 l1 = {5, 3, -7};
    struct s2 l2 = {.c = 9, .b = -100000};
    struct s7 l7 = {-3, 'x', 50};
    struct s9 r9;
    struct s1 copy;

    SIZE(struct s1); SHOW(struct s1, a); SHOW(struct s1, b); SHOW(struct s1, e);
    SIZE(struct s2); SHOW(struct s2, b); SHOW(struct s2, c);
    SIZE(struct s3); SHOW(struct s3, b); SHOW(struct s3, c);
    SIZE(struct s4); SHOW(struct s4, b); SHOW(struct s4, c);
    SIZE(struct s5); SHOW(struct s5, b);
    SIZE(struct s6); SHOW(struct s6, b);
    SIZE(struct s7); SHOW(struct s7, a); SHOW(struct s7, b); SHOW(struct s7, c);
    SIZE(struct s8); SHOW(struct s8, b);
    SIZE(struct s9); SHOW(struct s9, a); SHOW(struct s9, b); SHOW(struct s9, c);
    SIZE(struct s10); SHOW(struct s10, b); SHOW(struct s10, c);
    SIZE(struct s11); SHOW(struct s11, b);
    SIZE(struct s12); SHOW(struct s12, a); SHOW(struct s12, b); SHOW(struct s12, c);
    SIZE(union u1); SHOW(union u1, b);
    SIZE(union u2); SHOW(union u2, b);
    SIZE(struct s13); SHOW(struct s13, b);
    SIZE(struct s14); SHOW(struct s14, a); SHOW(struct s14, b); SHOW(struct s14, c);

    printf("g1 %d %d %d, g9 %u %u %u, g4 %d %lld %d\n", g1.a, g1.b, g1.e, g9.a, g9.b, g9.c, g4.a,
           (long long)g4.b, g4.c);
    printf("l1 %d %d %d, l2 %d %d, l7 %d %c %d\n", l1.a, l1.b, l1.e, l2.b, l2.c, l7.a, l7.b, l7.c);
    copy = l1;
    copy.b = 1;
    printf("copy %d %d %d, l1.b %d\n", copy.a, copy.b, copy.e, l1.b);
    printf("sum1 %d %d, sum4 %lld\n", sum1(g1), sum1(l1), sum4(g4));
    r9 = make9(15, 511, 127);
    printf("make9 %u %u %u\n", r9.a, r9.b, r9.c);
    return 0;
}
