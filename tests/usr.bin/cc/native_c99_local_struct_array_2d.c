#include <stdio.h>

struct cell {
    char *buf;
    long len;
};

int main(void) {
    struct cell a[2][4];

    a[1][3].len = (long)sizeof(a);
    printf("%ld\n", a[1][3].len);
    return !(a[1][3].len == (long)sizeof(struct cell) * 8);
}
