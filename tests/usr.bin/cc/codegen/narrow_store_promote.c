/*
 * A local narrower than what is stored to it -- a char, a short or a _Bool
 * given an int, an int given a long -- is a value the optimizer keeps in a
 * register, holding the stored value truncated (run_dead_bool_call.sh
 * checks what that lets it fold): read back at its own size and widened,
 * signed and unsigned, in a loop and across a branch.
 */
#include <stdio.h>

static volatile int vol = 7;

static int __attribute__((noinline)) narrow(int x, long y) {
    signed char c = x;
    unsigned char uc = x;
    short s = x * 3;
    unsigned short us = x * 5;
    _Bool b = x & 0x100;
    int i = y;
    unsigned u = y >> 3;
    int r = 0;
    int k;

    for (k = 0; k < 3; ++k) {
        c += k * 100;
        uc -= k * 90;
        s ^= k << 14;
        us += 0x7fff;
        r += c + uc + s + us + b + (i >> 4) + (int)(u & 0xffff);
        if (r & 1) {
            b = r;
        } else {
            i = y + r;
        }
    }
    return r;
}

static int __attribute__((noinline)) flag(void *a, void *p) {
    _Bool x = (a != p);

    x &= vol > 3;
    return x ? 11 : 22;
}

int main(void) {
    printf("%d %d %d\n", narrow(0x1234, 0x123456789L), narrow(-0x7fff, -5L), narrow(0x17f, 0x7fffffff));
    printf("%d %d\n", flag(&vol, &vol), flag(&vol, 0));
    return 0;
}
