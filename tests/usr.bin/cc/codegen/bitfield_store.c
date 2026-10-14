/*
 * Storing into a bit-field clears the field's old bits with a 64-bit
 * mask built at compile time.  On i386 that constant must keep its
 * high word, or the clear does nothing and the old bits survive the
 * store.
 */
#include <stdio.h>

struct B {
    unsigned a : 3;
    unsigned b : 5;
    signed c : 4;
    unsigned d : 20;
};

int main(void) {
    struct B s = {0};

    s.a = 7;
    s.b = 31;
    s.c = -3;
    s.d = 0xABCDE;
    s.a++;
    printf("%u %u %d %x %zu\n", s.a, s.b, s.c, s.d, sizeof s);
    s.a = 6;
    s.a += 3;
    s.c = 7;
    s.c++;
    printf("%u %u %d %x\n", s.a, s.b, s.c, s.d);
    s.d = 0xFFFFF;
    s.d += 2;
    printf("%u %u %d %x\n", s.a, s.b, s.c, s.d);
    return 0;
}
