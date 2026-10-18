/*
 * _Complex _Float128, two binary128 parts laid out as gcc lays them (32
 * bytes, 16-aligned, passed and returned in memory): its arithmetic on
 * parts is libgcc's soft-fp, a product or quotient of two complex values
 * __multc3's or __divtc3's; a real operand stays real (Annex G.5);
 * imaginary f128 constants and static initializers fold in binary128.
 * The values are shown by their encodings.
 *
 * cc-std: gnu2x
 */
#include <stdio.h>
#include <string.h>
static void show(const char *t, _Complex _Float128 z) {
    unsigned long long w[4];
    memcpy(w, &z, 32);
    printf("%s %016llx%016llx %016llx%016llx\n", t, w[1], w[0], w[3], w[2]);
}
static _Complex _Float128 f(_Complex _Float128 a, _Complex _Float128 b) { return a * b + a; }
int main(void) {
    _Complex _Float128 a = 1.5f128, b, c;
    volatile double re = 2, im = -3;
    _Complex double cd = re + im * 1.0i;
    _Float128 q = 0.1f128;
    b = cd;
    __real__ a = 1;
    __imag__ a = 2;
    show("a", a); show("b", b);
    c = f(a, b); show("f", c);
    show("div", a / b);
    show("divr", a / q);
    show("mulr", q * b);
    show("sub", q - b);
    show("conj", ~a);
    c = a; c += 1; show("cpe", c);
    _Complex double back = c;
    printf("%g %g %d %d %d %zu %zu\n", __real__ back, __imag__ back, a == a, a != b, a == 1, sizeof a, _Alignof(_Complex _Float128));
    printf("%.17g %.17g\n", (double)__real__ (a / b), (double)(_Float128)a);
    return 0;
}
