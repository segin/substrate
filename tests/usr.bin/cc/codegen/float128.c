/*
 * _Float128 and __float128 are IEEE binary128 on x86 (gcc's): 16 bytes,
 * 16-aligned, a 113-bit significand.  Its constants (the f128 and GNU q
 * suffixes, decimal or hexadecimal) are rounded once from their text, an
 * integer or binary floating constant converts to it exactly, and its
 * arithmetic, comparisons and conversions are libgcc's soft-fp
 * (__addtf3, __eqtf2, __extenddftf2, __fixtfdi and kin).  It is passed and
 * returned as gcc passes it -- in %xmm0 on x86-64, in memory on i386.
 * The values are shown by their encodings, as printf has no conversion
 * for them.
 *
 * cc-std: gnu2x
 */
#include <stdio.h>
#include <string.h>

static void show(const char *tag, _Float128 v) {
    unsigned long long w[2];

    memcpy(w, &v, sizeof(w));
    printf("%s %016llx%016llx\n", tag, w[1], w[0]);
}

static _Float128 axpy(_Float128 a, __float128 x, _Float128 y) {
    return a * x + y;
}

static _Float128 third = 1.0f128 / 3;
static __float128 tenth = 0.1Q;
static _Float128 table[3] = {1, -2.5, 0x1.8p-16382f128};

int main(void) {
    volatile int i = -7;
    volatile unsigned long long u = 18446744073709551615ULL;
    volatile double d = 0.1;
    volatile long double l = 0.1L;
    volatile float f = 1.5f;
    _Float128 q = 1;
    _Float128 r;
    _Float128 big = 1e4000f128;

    printf("%zu %zu\n", sizeof(_Float128), _Alignof(__float128));
    show("third", third);
    show("tenth", tenth);
    show("t1", table[1]);
    show("t2", table[2]);
    q = q / 3;
    show("q", q);
    show("q*3", q * 3);
    show("i", (_Float128)i);
    show("u", (_Float128)u);
    show("d", d);
    show("l", l);
    show("f", f);
    r = axpy(2, q, tenth);
    show("axpy", r);
    show("neg", -r);
    show("big2", big * big);
    show("tiny", 1e-4960f128 / 1e10f128);
    printf("%d %d %d %d %d %d\n", q == third, q < tenth, r > 0, q != q, big * big == big * big * 2, !(_Float128)0);
    printf("%.17g %.17g %.21Lg %d %lld %llu\n", (double)q, (double)(float)r, (long double)q, (int)(r * 100),
           (long long)(_Float128)-123456789012.75f128, (unsigned long long)(_Float128)1e19f128);
    printf("%d\n", _Generic(q, _Float128: 1, long double: 2, double: 3, default: 0));
    return 0;
}
