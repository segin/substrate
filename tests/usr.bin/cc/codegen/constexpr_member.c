/* a member of a constexpr structure is a named constant (C23 6.6p7) */
/* cc-std: c23 */
#include <stdio.h>

struct in {
    short lo;
    unsigned char hi;
};

struct s {
    int a;
    struct in in;
    struct {
        long anon;
    };
    _Bool flag;
    unsigned char narrow;
    int omitted;
};

constexpr struct s g = {.in = {.hi = 7}, .a = 3, .anon = 11, .flag = 1, .narrow = 300 - 50};

int main(void) {
    constexpr struct s l = {-2, {9}};
    constexpr int a = g.a + l.a;
    int arr[g.in.hi];
    enum { E = g.anon * 2 };
    static_assert(g.flag == 1);
    static_assert(l.in.lo == 9 && l.in.hi == 0 && l.omitted == 0);
    int which = 0;

    switch (which) {
    case g.narrow - 250:
        printf("case %d\n", g.narrow);
        break;
    default:
        printf("default\n");
    }
    printf("%d %d %d %ld %d\n", a, (int)sizeof(arr), (int)E, l.anon, g.omitted);
    return 0;
}
