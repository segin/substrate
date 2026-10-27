/*
 * cc-std: c2x
 * A zero-length array, gcc's extension, in an ISO mode: gcc takes it
 * there too and complains only under -pedantic.  cc took it only in a
 * GNU mode, so the Substrate kernel's multiboot.h (-std=c2x) stopped
 * every file including it.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct tag {
    unsigned type;
    unsigned size;
    char string[0];
};

struct both {
    int n;
    short s[0];
    long long tail[0];
};

static unsigned char buf[64] __attribute__((aligned(8)));

int main(void) {
    struct tag *t = (struct tag *)buf;
    int empty[0];

    t->type = 1;
    t->size = sizeof(struct tag) + 6;
    memcpy(t->string, "hello", 6);
    printf("%zu %zu %s\n", sizeof(struct tag), offsetof(struct tag, string), t->string);
    printf("%zu %zu %zu\n", sizeof(struct both), offsetof(struct both, s), offsetof(struct both, tail));
    printf("%zu\n", sizeof(empty));
    return 0;
}
