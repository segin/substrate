/*
 * __builtin_memcpy, __builtin_memmove and __builtin_memset of a constant
 * size up to a few words are loads and stores, with no call of the
 * library's function (run_builtin_mem_inline.sh checks that): the bytes
 * they leave, every size from 0 to 40 and overlapping moves both ways.
 */
#include <stdio.h>

static unsigned char buf[96];

static void fill(void) {
    int i;

    for (i = 0; i < (int)sizeof(buf); ++i) {
        buf[i] = (unsigned char)(i * 7 + 3);
    }
}

static unsigned sum(void) {
    unsigned s = 0;
    int i;

    for (i = 0; i < (int)sizeof(buf); ++i) {
        s = s * 31 + buf[i];
    }
    return s;
}

#define COPY(n)                                                                                               \
    do {                                                                                                      \
        fill();                                                                                               \
        __builtin_memcpy(buf + 50, buf + 1, n);                                                               \
        printf("cpy %2d %08x\n", n, sum());                                                                   \
        fill();                                                                                               \
        __builtin_memmove(buf + 3, buf + 1, n);                                                               \
        printf("mvf %2d %08x\n", n, sum());                                                                   \
        fill();                                                                                               \
        __builtin_memmove(buf + 1, buf + 3, n);                                                               \
        printf("mvb %2d %08x\n", n, sum());                                                                   \
        fill();                                                                                               \
        __builtin_memset(buf + 5, 0xa5, n);                                                                   \
        printf("set %2d %08x\n", n, sum());                                                                   \
    } while (0)

int main(void) {
    unsigned long long v = 0;
    void *r;

    COPY(0);
    COPY(1);
    COPY(2);
    COPY(3);
    COPY(4);
    COPY(5);
    COPY(7);
    COPY(8);
    COPY(9);
    COPY(13);
    COPY(16);
    COPY(17);
    COPY(31);
    COPY(32);
    COPY(40);
    fill();
    r = __builtin_memcpy(&v, buf + 3, sizeof(v));
    printf("value %016llx %d\n", v, r == (void *)&v);
    return 0;
}
