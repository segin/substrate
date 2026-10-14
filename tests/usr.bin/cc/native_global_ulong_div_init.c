#include <stdio.h>

struct pair {
    unsigned long a;
    unsigned long b;
};

static unsigned long scalar = (~0UL) / 3;
static struct pair p = {
    0xaaaaaaaaaaaaaaabUL,
    (~0UL) / 3
};

int main(void) {
    if (scalar != 0x5555555555555555UL) {
        fprintf(stderr, "scalar=%#lx\n", scalar);
        return 1;
    }
    if (p.a != 0xaaaaaaaaaaaaaaabUL || p.b != 0x5555555555555555UL) {
        fprintf(stderr, "p={%#lx,%#lx}\n", p.a, p.b);
        return 1;
    }
    return 0;
}
