/*
 * Integers wider than 64 bits: _BitInt(N) up to BITINT_MAXWIDTH, and
 * __int128 where there is one.  Every operator, the conversions to and from
 * narrower integers and floating types, wb constants of any size, ++/--,
 * compound assignment, and static initializers -- printed limb by limb, so
 * that the result must be gcc's to the bit.
 *
 * cc-std: gnu2x
 * cc-gcc: 14 (_BitInt)
 * cc-gcc-m32: 16 (_BitInt under -m32)
 */
#include <stdio.h>

typedef unsigned long long u64;
typedef _BitInt(200) S200;
typedef unsigned _BitInt(200) U200;
typedef _BitInt(130) S130;
typedef _BitInt(1000) S1000;

#define P(s, v) printf("%s %016llx %016llx %016llx %016llx\n", s, (u64)((v) >> 192), (u64)((v) >> 128), \
                       (u64)((v) >> 64), (u64)(v))
/* a 128-bit value: shifting it by 128 or more is undefined */
#define P2(s, v) printf("%s %016llx %016llx\n", s, (u64)((v) >> 64), (u64)(v))

static S200 g_a = -0x123456789abcdef0123456789abcdefwb;
static U200 g_b = (U200)-1;
static S130 g_c[3] = {1, -2, 0x7ffffffffffffffffffffffffwb};

static S200 cube(S200 x) {
    return x * x * x;
}

int main(void) {
    S200 a = 0x7fffffffffffffffLL;
    U200 b = (U200)-1;
    S130 c = -12345;
    S1000 big = 1;
    volatile double d = -1.5e40;
    volatile float f = 1e30f;
    volatile long double l = 3.25e35L;
    long long ll;
    int i;

    a = a * a;
    P("sq", a);
    P("cube", cube((S200)-987654321));
    P("div", a / -3);
    P("mod", a % 1000003);
    P("udiv", b / 7);
    P("umod", b % 1000000007);
    P("shl", a << 77);
    P("sar", (-a) >> 100);
    P("shr", b >> 199);
    P("mix", c * a + (S200)c);
    P("not", ~(U200)c);
    P("and", a & 0xffff0000ffff0000wb);
    P("or", a | ((S200)1 << 150));
    P("xor", a ^ b);
    P("lit", 0x123456789abcdef0123456789abcdef0123456789wb + 1427247692705959881058285969449495136382746623uwb);
    for (i = 0; i < 990; ++i) {
        big *= 2;
    }
    big = big / 3 + 1;
    ll = (long long)(big >> 940);
    printf("big %lld %d %d %g\n", ll, big > 0, big < 0, (double)big);
    printf("cmp %d %d %d %d %d %d\n", a > b, (U200)a < b, c < 0, c <= -12345, c >= -12344, (S130)a == (S130)a);
    printf("conv %lld %llu %d %d\n", (long long)c, (u64)b, (int)(signed char)a, (_Bool)c);
    printf("dbl %g %g %Lg\n", (double)a, (double)c, (long double)b);
    a = d;
    P("fromd", a);
    c = f;
    P("fromf", c);
    b = l;
    P("froml", b);
    c = 7;
    c++;
    ++c;
    c -= 3;
    c <<= 70;
    {
        S130 old = c--; /* P reads its argument more than once */

        P("upd", old);
        P("upd2", c);
    }
    printf("truth %d %d %d\n", !c, c && 1, c ? 5 : 6);
    P("static", g_a);
    P("static2", g_b);
    P("arr", g_c[1] + g_c[2]);
    printf("sizes %d %d %d %d %d\n", (int)sizeof(S200), (int)_Alignof(S200), (int)sizeof(S130), (int)sizeof(S1000),
           (int)sizeof(5wb));
#ifdef __SIZEOF_INT128__
    {
        __int128 x = 0x123456789abcdefLL;
        unsigned __int128 y = ~(unsigned __int128)0;

        x <<= 40;
        P2("i128", x * x + 1);
        P2("i128d", x / 1000 - (x % 1000));
        P2("u128", y >> 3);
        P2("i128f", (__int128)1e30);
        printf("i128c %d %d %g %d %d\n", x > 0, (__int128)-5 < x, (double)y, (int)sizeof(__int128),
               (int)_Alignof(__int128));
    }
#endif
    return 0;
}
