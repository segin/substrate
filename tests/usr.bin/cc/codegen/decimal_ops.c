/*
 * Decimal floating types in BID, gcc's encoding on x86: constants of each
 * width (rounded, signed zero, infinity), the folding of integer and binary
 * floating constants as gcc folds them, arithmetic and comparisons through
 * libgcc, conversions to and from integers, binary floating types and
 * the other widths, compound assignment, ++/--, truth values, and decimals
 * in statics, arrays, structures and function calls.  Every result is
 * printed as its encoding, so it must be gcc's to the bit.
 */
#include <stdio.h>

static void bytes(const char *tag, const void *p, size_t n) {
    const unsigned char *b = p;

    printf("%s:", tag);
    while (n-- > 0) {
        printf("%02x", b[n]);
    }
    printf("\n");
}

#define SHOW(x) bytes(#x, &(x), sizeof(x))

struct rec {
    char tag;
    _Decimal32 s;
    _Decimal128 q;
    _Decimal64 d[2];
};

static _Decimal32 g32 = 1.5DF;
static _Decimal64 g64[] = {0.1DD, 0.1, 7, -2.5e10DD};
static _Decimal128 g128 = 123456789012345678901234567890123456.DL;
_Decimal64 gneg = -0.0DD, gbig = 1e400DD, gtiny = 1e-500DD;
_Decimal32 grnd = 1.2345675DF;

static _Decimal64 scale(_Decimal64 x, int n) {
    while (n-- > 0) {
        x *= 10;
    }
    return x;
}

static _Decimal128 mix(_Decimal32 a, _Decimal64 b, _Decimal128 c) {
    return a + b * c;
}

int main(void) {
    volatile int seven = 7;
    volatile double dv = 0.3;
    volatile long double lv = 2.5L;
    _Decimal64 a = 1.10DD, b = 2.205DD;
    _Decimal32 s = 0.1DF;
    _Decimal128 q = 1.5DL;
    struct rec r = {'r', 2.5DF, 3.25DL, {1, 2}};
    _Decimal64 arr[3];
    int i;

    SHOW(g32);
    SHOW(g64);
    SHOW(g128);
    SHOW(gneg);
    SHOW(gbig);
    SHOW(gtiny);
    SHOW(grnd);
    {
        _Decimal64 add = a + b, sub = a - b, mul = a * b, dvd = b / 3, neg = -a, mixi = a - seven;
        _Decimal32 s3 = s * 3, sdiv = s / 7;
        _Decimal128 qd = q / 7 + a, qm = q * q * q;
        _Decimal64 inf = a / 0, nan = (a - a) / 0;

        SHOW(add);
        SHOW(sub);
        SHOW(mul);
        SHOW(dvd);
        SHOW(neg);
        SHOW(mixi);
        SHOW(s3);
        SHOW(sdiv);
        SHOW(qd);
        SHOW(qm);
        SHOW(inf);
        printf("%d %d %d %d %d %d | %d %d %d\n", a < b, a <= b, a > b, a >= b, a == 1.1DD, a != 1.10DD, nan == nan,
               nan != nan, nan < a);
    }
    {
        /* conversions */
        _Decimal64 fi = seven, fu = 4000000000u, fll = -1234567890123456789LL, fd = dv, fl = lv;
        _Decimal32 fs = (float)dv, nar = b;
        _Decimal128 wid = b, fq = 0.1;
        double d1 = (double)b, d2 = (double)q;
        long double l1 = b * 1000;
        float f1 = s;
        int i1 = (int)(b * 1000), i2 = (int)-b;
        unsigned u1 = (unsigned)(q * 3);
        long long ll1 = (long long)fll;
        _Bool t1 = s, t2 = s - s;

        SHOW(fi);
        SHOW(fu);
        SHOW(fll);
        SHOW(fd);
        SHOW(fl);
        SHOW(fs);
        SHOW(nar);
        SHOW(wid);
        SHOW(fq);
        printf("%.17g %.17g %.21Lg %.9g %d %d %u %lld %d %d\n", d1, d2, l1, (double)f1, i1, i2, u1, ll1, t1, t2);
    }
    {
        /* assignment operators, ++/--, truth values, conditions */
        _Decimal64 x = 1.5DD;

        x += 2;
        x *= 1.5DD;
        x -= 0.25DD;
        x /= 2;
        ++x;
        x--;
        SHOW(x);
        printf("%d %d %d\n", !x, x && s, (s - s) || 0);
        if (x) {
            printf("true\n");
        }
        x = x > 2 ? x : -x;
        SHOW(x);
    }
    {
        /* aggregates and calls */
        _Decimal64 sc = scale(1.25DD, 3);
        _Decimal128 mx = mix(2.5DF, 1.1DD, 3);

        r.d[1] += r.s;
        r.q = r.q * r.d[0];
        for (i = 0; i < 3; ++i) {
            arr[i] = g64[i] + i;
        }
        SHOW(sc);
        SHOW(mx);
        SHOW(r.s);
        SHOW(r.q);
        SHOW(r.d);
        SHOW(arr);
    }
    printf("%zu %zu %zu %zu %zu %zu %zu\n", sizeof(_Decimal32), _Alignof(_Decimal32), sizeof(_Decimal64),
           _Alignof(_Decimal64), sizeof(_Decimal128), _Alignof(_Decimal128), sizeof(struct rec));
    return 0;
}
