/*
 * A structure passed or returned by value is copied whole, whatever its
 * size.  The compiler kept the size of each value and argument in one
 * byte, so a structure over 255 bytes was copied only in part: the
 * callee saw its tail as whatever the stack held.
 */
#include <stdio.h>

struct big {
    unsigned char b[304];
};

struct huge {
    unsigned char b[1000];
    long long tail;
};

static int check_big(struct big s) {
    int i;

    for (i = 0; i < 304; ++i) {
        if (s.b[i] != (unsigned char)(i * 7)) {
            return i;
        }
    }
    return -1;
}

static int check_huge(int pad, struct huge s, int after) {
    int i;

    for (i = 0; i < 1000; ++i) {
        if (s.b[i] != (unsigned char)(i * 3)) {
            return i;
        }
    }
    return s.tail == 0x123456789abcLL && pad == 5 && after == 9 ? -1 : 1000;
}

static struct big make_big(void) {
    struct big s;
    int i;

    for (i = 0; i < 304; ++i) {
        s.b[i] = (unsigned char)(i * 7);
    }
    return s;
}

int main(void) {
    struct big a;
    struct huge h;
    int i;

    for (i = 0; i < 304; ++i) {
        a.b[i] = (unsigned char)(i * 7);
    }
    for (i = 0; i < 1000; ++i) {
        h.b[i] = (unsigned char)(i * 3);
    }
    h.tail = 0x123456789abcLL;
    printf("argument 304 bytes: %d\n", check_big(a));
    printf("argument 1008 bytes between ints: %d\n", check_huge(5, h, 9));
    a = make_big();
    printf("returned 304 bytes: %d\n", check_big(a));
    printf("returned, passed on: %d\n", check_big(make_big()));
    return 0;
}
