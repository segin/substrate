/*
 * A compound literal of array type with no bound, (T[]){...}, takes its
 * bound from its initializer list (C11 6.5.2.5p4, 6.7.9p22), and its
 * size is that of the complete array.  cc left the type incomplete:
 * sizeof of one was rejected ("unsupported sizeof operand in lowering").
 */
#include <stdio.h>

struct pt {
    int x, y;
};

int main(void) {
    int *p = (int[]){1, 2, 3};
    unsigned a = (unsigned)sizeof((int[]){1, 2, 3});
    unsigned b = (unsigned)sizeof((char[]){"hello"});
    unsigned c = (unsigned)sizeof((struct pt[]){{1, 2}, {3, 4}, {5, 6}});
    unsigned d = (unsigned)sizeof((int[]){[5] = 1});
    unsigned e = (unsigned)sizeof((int[4]){1});

    printf("%u %u %u %u %u %d\n", a, b, c, d, e, p[2]);
    return 0;
}
