/*
 * A switch on a 64-bit integer compares all 64 bits (CC-106): on i386
 * the case constants were word-sized, so only the low words were
 * compared and 0x100000001 took `case 1`.  Case ranges too.  The
 * values come through a function, so nothing is folded.
 */
#include <stdio.h>

static long long id(long long x) {
    return x;
}

static int pick(long long x) {
    switch (x) {
    case 1:
        return 10;
    case 0x7fffffff:
        return 20;
    case 5 ... 9:
        return 30;
    case -1:
        return 40;
    case 0x100000000LL:
        return 50;
    default:
        return 0;
    }
}

static int upick(unsigned long long x) {
    switch (x) {
    case 0xffffffffULL:
        return 1;
    case 0xffffffffffffffffULL:
        return 2;
    default:
        return 0;
    }
}

int main(void) {
    printf("%d %d %d %d %d %d %d %d\n", pick(id(1)), pick(id(0x100000001LL)), pick(id(0x17fffffffLL)),
           pick(id(0x100000006LL)), pick(id(6)), pick(id(-1)), pick(id(0xffffffffLL)), pick(id(0x100000000LL)));
    printf("%d %d %d\n", upick((unsigned long long)id(0xffffffffLL)), upick((unsigned long long)id(-1)),
           upick((unsigned long long)id(0x1ffffffffLL)));
    return 0;
}
