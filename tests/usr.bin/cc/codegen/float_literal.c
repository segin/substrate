/*
 * A float constant (an f or F suffix) is the float nearest its digits,
 * not the double: 0.1f widened to double is 0.10000000149011612, and a
 * float variable holding 0.1f compares equal to the constant.
 */
#include <stdio.h>

int main(void) {
    float x = 0.1f;
    double d = 0.1f;
    double e = 1e30f;
    float g = 0x1.fffffep127f;

    printf("%d %d %d\n", x == 0.1f, 1e30f == (float)1e30, 0.1f == (float)0.1);
    printf("%.17g %.17g %.17g %a\n", d, (double)0.1f, e, (double)g);
    printf("%.17g\n", 0.1f + 0.2);
    return 0;
}
