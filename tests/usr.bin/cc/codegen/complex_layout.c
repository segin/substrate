/* The size and alignment of float, double and long double _Complex, alone,
 * as sizeof of the type and as structure members. */
#include <stdio.h>
struct s { char c; float _Complex f; };
struct t { char c; long double _Complex l; };
int main(void) {
    float _Complex f; double _Complex d; long double _Complex l;
    printf("%d %d %d %d %d %d %d\n", (int)sizeof f, (int)sizeof d, (int)sizeof l, (int)sizeof(float _Complex),
           (int)sizeof(struct s), (int)sizeof(struct t), (int)_Alignof(long double _Complex));
    return 0;
}
