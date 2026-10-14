#include <stdlib.h>

static int use(int v) {
    return v;
}

int main(void) {
    int fds[2] = {63, 62};
    int v = use(fds[1]);
    int *p = fds;

    if (v != 62) return 1;
    if (p[0] != 63) return 2;
    if (p[1] != 62) return 3;
    return 0;
}
