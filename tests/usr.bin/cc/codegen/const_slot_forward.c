/*
 * From -O1 a local stored once with a constant is read as that constant,
 * so a branch on it folds, as gcc folds `bool set_uncached =
 * IS_ENABLED(...) && ...; if (set_uncached) ...` in Linux.  The values
 * must be what the loads gave: a narrow local read sign- or
 * zero-extended, one read in a loop, and locals that are not forwarded --
 * one whose address is taken and written through, one assigned twice.
 */
#include <stdio.h>

static void set_through(int *p, int v) {
    *p = v;
}

int main(int argc, char **argv) {
    signed char sc = -3;
    unsigned char uc = 250;
    short sh = -30000;
    unsigned short us = 65000;
    long long wide = -0x123456789LL;
    int addr_taken = 5;
    int twice = 1;
    int k = argc - 1; /* 0, unknown to the compiler */
    int sum = 0;
    int i;

    (void)argv;
    set_through(&addr_taken, 9);
    if (k == 0) {
        twice = 2;
    }
    for (i = 0; i < 4; ++i) {
        sum += sc + uc;
    }
    printf("%d %d %d %d %lld %d %d %d\n", sc, uc, sh, us, wide, addr_taken, twice, sum);
    return 0;
}
