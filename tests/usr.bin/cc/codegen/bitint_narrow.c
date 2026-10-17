/*
 * Bit-precise integers no wider than a register: a value converted to
 * _BitInt(N) keeps its low N bits, sign-extended, and one converted to
 * unsigned _BitInt(N) keeps them zero-extended -- on assignment,
 * initialization, a cast, an argument, a return, ++, compound assignment
 * and arithmetic.  They used to wrap at their size in bytes, and
 * unsigned _BitInt(N) was signed.
 *
 * cc-std: gnu2x
 */
#include <stdio.h>

/* a bit-precise object next to bytes it must not touch */
struct guarded {
    _BitInt(7) a;
    unsigned char tail[7];
};

static _BitInt(7) g7;
static unsigned _BitInt(12) gu;

static unsigned _BitInt(12) twice(unsigned _BitInt(12) x) {
    return x * 2;
}

static _BitInt(7) id7(int x) {
    return x;
}

int main(void) {
    volatile int x = 100;
    _BitInt(7) a = x;
    unsigned _BitInt(12) u = -x;
    unsigned _BitInt(12) w = u;
    _BitInt(7) b = 60;

    g7 = x;
    gu = -x;
    w += 4000;
    b++;
    b += 10;
    printf("%d %u %d %d\n", (int)a, (unsigned)(u >> 4), (int)sizeof(a), (int)sizeof(u));
    printf("%u %d %d %u\n", (unsigned)w, (int)b, (int)g7, (unsigned)gu);
    printf("%u %d %u %u\n", (unsigned)twice(u), (int)id7(x), (unsigned)~u, (unsigned)-u);
    printf("%d %d %d\n", (int)(x ? a : b), u > 4000, (int)(a + b));
    printf("%d %u\n", (int)(_BitInt(7))x, (unsigned)(unsigned _BitInt(5))x);
    {
        /* ++ and loads through a pointer take the object's size, not a word's */
        struct guarded gd = {-5, {1, 2, 3, 4, 5, 6, 7}};
        _BitInt(7) *pp = &gd.a;

        (*pp)++;
        printf("%d %d %d\n", (int)*pp, gd.tail[0], gd.tail[6]);
    }
    return 0;
}
