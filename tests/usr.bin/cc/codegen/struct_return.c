/*
 * Structs returned and passed by value.  On i386 every struct is
 * returned through a hidden pointer the caller passes first and the
 * callee pops; once, the callee returned the address of its own local
 * copy and the caller read it after the stack had been reused.
 */
#include <stdio.h>

struct P {
    int x;
    char c;
    double d;
    short s;
};

struct Q {
    int a, b;
};

struct R {
    int a, b, c;
};

struct One {
    char c;
};

static struct P mk(int a) {
    struct P p;
    p.x = a;
    p.c = 'A' + a;
    p.d = a * 1.5;
    p.s = (short)(a * 3);
    return p;
}

static struct Q mq(int a) {
    struct Q q = {a, a + 1};
    return q;
}

static struct R mr(int a) {
    struct R r = {a, a + 1, a + 2};
    return r;
}

static struct One mo(char c) {
    struct One o = {c};
    return o;
}

static int sumq(struct Q q) {
    return q.a * 10 + q.b;
}

static int sumr(int k, struct R r, int j) {
    return k + r.a * 100 + r.b * 10 + r.c + j * 1000;
}

static struct R pass_through(struct R r, int add) {
    r.c += add;
    return r;
}

int main(void) {
    struct P p = mk(7);
    struct P q2 = p;
    struct Q q = mq(3);
    struct R r = mr(4);
    struct R r2;
    struct One o = mo('z');

    printf("%d %c %.2f %d\n", p.x, p.c, p.d, p.s);
    q2.x++;
    printf("%d %d %zu\n", p.x, q2.x, sizeof(struct P));
    printf("%d %d | %d %d %d | %c\n", q.a, q.b, r.a, r.b, r.c, o.c);
    printf("%d %d %d\n", sumq(q), sumr(1, r, 2), sumq(mq(8)));
    r2 = pass_through(mr(1), 10);
    printf("%d %d %d %d\n", r2.a, r2.b, r2.c, mk(2).x + mr(5).c);
    return 0;
}
