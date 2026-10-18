/*
 * _Float32, _Float64, _Float32x and _Float64x are types of their own (C23
 * 6.2.5p10-13), in the formats of float, double, double and long double:
 * _Generic and __builtin_types_compatible_p tell them from the standard
 * types, the f32/f64/f32x/f64x suffixes give them, and the usual
 * arithmetic conversions pick, of two types in one format, the
 * interchange type, then the standard one, then the extended one, as gcc
 * does.  Their sizes and values are their formats'.
 *
 * cc-std: gnu2x
 */
#include <stdio.h>

#define K(e)                                                                                                       \
    _Generic((e), float: "float", double: "double", long double: "ldouble", _Float32: "f32", _Float64: "f64",       \
             _Float32x: "f32x", _Float64x: "f64x", default: "other")

int main(void) {
    float f = 1;
    double d = 1;
    long double l = 1;
    _Float32 a = 1;
    _Float64 b = 1;
    _Float32x c = 1;
    _Float64x x = 1;
    _Float32 qa;
    _Float32x qc;
    _Float64x qx;

    printf("%s %s %s %s %s %s\n", K(f + a), K(a + f), K(d + b), K(b + c), K(d + c), K(l + x));
    printf("%s %s %s %s %s %s\n", K(a + d), K(a + b), K(c + x), K(1.0f32), K(1.0f32x), K(a + 1));
    printf("%s %s %s %s\n", K(1.0f64x), K(1.0f64), K(b + l), K(x + d));
    printf("%zu %zu %zu %zu | %d %d %d %d\n", sizeof a, sizeof b, sizeof c, sizeof x,
           __builtin_types_compatible_p(_Float32x, double), __builtin_types_compatible_p(_Float64, _Float64),
           __builtin_types_compatible_p(_Float64x, long double), __builtin_types_compatible_p(_Float32, float));
    qa = a / 3;
    qc = c / 3;
    qx = x / 3;
    printf("%.17g %.17g %.21Lg\n", (double)qa, (double)qc, (long double)qx);
    return 0;
}
