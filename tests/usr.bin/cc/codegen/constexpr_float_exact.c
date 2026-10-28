/* constexpr floating objects whose initializers keep their values in the object's type (C23 6.7.1p6) */
/* cc-std: c23 */
/* cc-gcc: 14 (-std=c23) */
#include <stdio.h>

constexpr float g1 = 0.5;
constexpr double g2 = 16777217;
constexpr float g3 = (float)0.1;

int main(void) {
    constexpr float f1 = 16777216;
    constexpr float f2 = -0.25;
    constexpr float f3 = 0.1 > 0 ? 1.5 : 0.1;
    constexpr float f4 = 2.5f + 1;
    constexpr long double l1 = 0.5;
    constexpr double d1 = 0.75f;
    constexpr float f5 = {0.125};

    printf("%g %g %.9g\n", g1, g2, g3);
    printf("%g %g %g %g %Lg %g %g\n", f1, f2, f3, f4, l1, d1, f5);
    return 0;
}
