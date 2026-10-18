/*
 * _Complex _Float32, _Float64 and _Float64x are complex types of those
 * floating types (C23 6.2.5p13), as big as two of them, and types of their
 * own: _Generic does not take _Complex _Float64 for _Complex double.
 *
 * cc-std: gnu2x
 */
#include <stdio.h>

int main(void) {
    _Complex _Float64 a = 1;
    _Complex _Float32 b = 2;
    _Complex _Float64x c = 3;

    a = a * a + 2;
    b = b * (_Complex _Float32)(1.0f + 1.0if);
    printf("%zu %zu %zu %d\n", sizeof a, sizeof b, sizeof c, _Generic(a, _Complex double: 1, default: 0));
    printf("%g %g %g %g %g\n", (double)__real__ a, (double)__imag__ a, (double)__real__ b, (double)__imag__ b,
           (double)__real__ c);
    return 0;
}
