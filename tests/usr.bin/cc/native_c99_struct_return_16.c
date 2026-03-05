/*
 * Regression test for x86_64 SysV 16-byte aggregate return ABI.
 * This covers both user-defined struct returns and libc imaxdiv_t.
 */
#include <inttypes.h>

struct pair64 {
    long a;
    long b;
};

static struct pair64 mk_pair64(long a, long b) {
    struct pair64 p;
    p.a = a;
    p.b = b;
    return p;
}

static struct pair64 add_pair64(struct pair64 x, struct pair64 y) {
    return mk_pair64(x.a + y.a, x.b + y.b);
}

int main(void) {
    struct pair64 c = add_pair64(mk_pair64(3, 5), mk_pair64(7, 11));
    imaxdiv_t d = imaxdiv(23, 5);

    if (c.a != 10 || c.b != 16)
        return 1;
    if (d.quot != 4 || d.rem != 3)
        return 2;
    return 0;
}
