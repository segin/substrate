#include <stdio.h>
#include <string.h>

static int same(double a, double b) {
    double d = a - b;
    if (d < 0)
        d = -d;
    return d < 1e-12;
}

int main(void) {
    unsigned short raw = 0x3c00u;
    _Float16 h;

    if (sizeof(h) != 2)
        return 1;
    memcpy(&h, &raw, sizeof(h));
    if (!same((double)h, 1.0))
        return 2;
    h = (_Float16)0.333251953125;
    if (!same((double)h, 0.333251953125))
        return 3;
    return 0;
}
