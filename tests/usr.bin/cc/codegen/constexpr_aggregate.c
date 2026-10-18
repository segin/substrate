/* constexpr objects of floating and aggregate type, initialized by constant expressions (C23 6.7.1) */
/* cc-std: c23 */
#include <stdio.h>

struct s {
    int a;
    double b;
};

constexpr double gc = 3;
enum { E = 4 };

int main(void) {
    constexpr double d1 = 1.5 * 2;
    constexpr double d2 = d1 + 1;
    constexpr struct s s1 = {1, d2};
    constexpr double d3 = sizeof(int) ? 1.0 : 2.0;
    constexpr double d4 = gc * E;
    constexpr struct s s2 = {.b = 2, .a = E};
    constexpr int a[3] = {1, E, (int)d1};
    constexpr double d5 = s1.b;

    printf("%g %g %d %g %g %g %d %g\n", d1, d2, s1.a, s1.b, d3, d4, s2.a, s2.b);
    printf("%d %d %d %g\n", a[0], a[1], a[2], d5);
    return 0;
}
