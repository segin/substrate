/* static and imaginary _Complex _Float128 constants, folded in binary128
 *
 * cc-std: gnu2x
 */
#include <stdio.h>
#include <string.h>
static _Complex _Float128 sz = 3;
static _Complex _Float128 sw = 1.5f128 + 2.0if128;
int main(void) {
    unsigned long long w[4];
    _Complex _Float128 im = 2.0if128;
    _Complex _Float64 a = 1.0 + 2.0i;
    _Complex _Float32 b = a * a;
    memcpy(w, &sz, 32); printf("%016llx %016llx\n", w[1], w[3]);
    memcpy(w, &sw, 32); printf("%016llx %016llx\n", w[1], w[3]);
    memcpy(w, &im, 32); printf("%016llx %016llx\n", w[1], w[3]);
    printf("%g %g %zu\n", (double)__real__ b, (double)__imag__ b, sizeof b);
    return 0;
}
