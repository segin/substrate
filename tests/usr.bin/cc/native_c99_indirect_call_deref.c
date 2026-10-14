#include <stdio.h>

static int called;

static void target(int value) {
    if (value == 7) {
        called = 1;
    }
}

static void invoke(void *opaque, int value) {
    void (**fnptr)(int) = opaque;
    (*fnptr)(value);
}

int main(void) {
    void (*fn)(int) = target;

    invoke(&fn, 7);
    if (!called) {
        fprintf(stderr, "indirect call through dereferenced function-pointer pointer failed\n");
        return 1;
    }
    return 0;
}
