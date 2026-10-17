/*
 * An automatic object is as aligned as its declaration asks (C11 6.7.5),
 * beyond the 16 bytes of the stack frame: _Alignas, the GNU attribute and
 * [[gnu::aligned]] on arrays, scalars whose address is taken, and a
 * structure whose type asks it, on every call whatever the caller's
 * stack depth.
 */
#include <stdint.h>
#include <stdio.h>

struct big {
    _Alignas(64) char c;
};

static int misaligned(int k) {
    char p0 = (char)k;
    _Alignas(64) char a[3];
    char p1 = (char)k;
    _Alignas(32) int b;
    __attribute__((aligned(128))) short c;
    _Alignas(16) char s[2];
    struct big e;
    int bad;

    a[0] = p0;
    b = p1;
    c = 1;
    s[0] = 2;
    e.c = 3;
    bad = ((uintptr_t)a % 64 != 0) + ((uintptr_t)&b % 32 != 0) + ((uintptr_t)&c % 128 != 0) +
          ((uintptr_t)s % 16 != 0) + ((uintptr_t)&e % 64 != 0);
    return bad + (a[0] + b + c + s[0] + e.c != 2 * k + 6);
}

int main(void) {
    int i;
    int bad = 0;

    for (i = 0; i < 8; ++i) {
        char pad[1 + 8 * i]; /* a different stack depth each call */

        pad[0] = 0;
        bad += misaligned(i) + pad[0];
    }
    printf("misaligned: %d\n", bad);
    return 0;
}
