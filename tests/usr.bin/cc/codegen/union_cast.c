/*
 * GNU casts to union type: the value goes into the member of its own
 * type (qualifiers aside), never converted into an earlier member it
 * would convert to.  A double used to land in the int member as an
 * int, and a long long in the int member too.
 */
#include <stdio.h>

union u {
    int i;
    double d;
    char *p;
    long long ll;
    unsigned char c;
};

static void show(union u v, int k) {
    switch (k) {
    case 0:
        printf("%d ", v.i);
        break;
    case 1:
        printf("%g ", v.d);
        break;
    case 2:
        printf("%s ", v.p);
        break;
    case 3:
        printf("%lld ", v.ll);
        break;
    default:
        printf("%u ", v.c);
        break;
    }
}

int main(void) {
    double x = 2.5;
    const int n = 7;
    char *s = "hi";
    volatile long long q = 1LL << 40;
    unsigned char c = 200;
    char buf[] = "arr";

    show((union u)x, 1);
    show((union u)n, 0);
    show((union u)s, 2);
    show((union u)q, 3);
    show((union u)c, 4);
    show((union u)buf, 2);
    show((union u)(x * 2), 1);
    printf("\n");
    return 0;
}
