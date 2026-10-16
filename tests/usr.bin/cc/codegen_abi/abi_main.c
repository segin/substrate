/*
 * Calls across compilers: run_codegen_abi.sh builds this with cc and
 * abi_lib.c with gcc, then the other way round.  Struct returns,
 * struct and narrow arguments, 64-bit values and libc's div() must all
 * cross the boundary intact.
 */
#include <stdio.h>
#include <stdlib.h>

struct Q {
    int a, b;
};

struct R {
    int a, b, c, d, e;
};

struct Q lib_q(int a);
struct R lib_r(int a, int b);
int lib_sum(char c, short s, int i, long long ll, double d, struct Q q);
long long lib_wide(long long a, unsigned long long b);

struct DD { double x, y; };
struct FF { float x, y; };
struct FFF { float x, y, z; };
struct DL { double x; long long y; };
struct LD { long long x; double y; };
struct C6 { char c[6]; };
struct C13 { char c[13]; };
struct X1 { long double v; };
struct XI { long double v; int i; };

struct DD lib_dd(struct DD a, struct DD b);
struct FF lib_ff(struct FF a, struct FF b);
struct FFF lib_fff(struct FFF a);
struct DL lib_dl(struct DL a, struct LD b);
struct LD lib_ld(struct LD a);
struct C6 lib_c6(struct C6 a);
struct C13 lib_c13(struct C13 a);
struct X1 lib_x1(struct X1 a, long double k);
struct XI lib_xi(struct XI a);
double lib_many(struct DD a, struct DD b, struct DD c, struct DD d, struct DD e, double f, struct FF g);
#if __STDC_VERSION__ >= 202311L
_BitInt(100) lib_b100(_BitInt(100) a, _BitInt(100) b);
unsigned _BitInt(200) lib_b200(unsigned _BitInt(200) a, int k);
_BitInt(130) lib_b130(long long x, _BitInt(130) y);
#endif
#ifdef __SIZEOF_INT128__
__int128 lib_i128(__int128 a, unsigned __int128 b, long c);
__int128 lib_i128s(long a, long b, long c, long d, long e, __int128 f);
#endif
float lib_fl(float a, double b, float c);
double lib_fl10(float a, float b, float c, float d, float e, float f, float g, float h, float i, float j);
float _Complex lib_cf(float _Complex a, float _Complex b);
double _Complex lib_cd(double _Complex a, double _Complex b);
long double _Complex lib_cl(long double _Complex a, long double _Complex b);

typedef int v4si __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));
typedef char v8qi __attribute__((vector_size(8)));
typedef short v2hi __attribute__((vector_size(4)));
typedef int v8si __attribute__((vector_size(32)));
typedef int v16si __attribute__((vector_size(64)));
v4si lib_v4(v4si a, v4si b);
v4sf lib_v4f(int n, v4sf a, double d, v4sf b);
v4si lib_v4n(int x, v4si a, v4si b, v4si c, int y, v4si d);
v2hi lib_v2(v2hi a);
v8si lib_v8(int k, v8si a);
int lib_v16(int x, v16si a, int y);
v8qi lib_v8q(int x, v8qi a, v8qi b, v8qi c, v8qi d);
_Decimal32 lib_d32(int n, _Decimal32 a, _Decimal32 b);
_Decimal64 lib_d64(_Decimal64 a, double x, _Decimal64 b);
_Decimal128 lib_d128(int n, _Decimal128 a, _Decimal64 b);

/* decimal values across the call, printed as their encodings */
static void decimals(void) {
    _Decimal32 s = lib_d32(3, 1.25DF, 0.5DF);
    _Decimal64 d = lib_d64(10.5DD, 0.25, 3);
    _Decimal128 q = lib_d128(-7, 1.000000000000000000000000000001DL, 2.5DD);
    unsigned long long w[2];

    __builtin_memcpy(w, &q, 16);
    printf("%08x %016llx %016llx%016llx\n", *(unsigned *)&s, *(unsigned long long *)&d, w[1], w[0]);
}

/* vectors across the call */
static void vectors(void) {
    v4si a = {1, -2, 3, 40}, b = {5, 6, -7, 8};
    v4si m = lib_v4(a, b);
    v4sf f = lib_v4f(2, (v4sf){1.5f, 2, 3, 4}, 0.25, (v4sf){2, -2, 0.5f, 8});
    v4si n = lib_v4n(3, a, b, a, 5, b);
    v2hi h = lib_v2((v2hi){-3, 7});
    v8si w = lib_v8(100, (v8si){1, 2, 3, 4, 5, 6, 7, 8});
    v16si big = {5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9};

    printf("%d %d %d %d | %g %g %g %g | %d %d | %d %d | %d %d | %d\n", m[0], m[1], m[2], m[3], (double)f[0],
           (double)f[1], (double)f[2], (double)f[3], n[0], n[3], h[0], h[1], w[0], w[7], lib_v16(1, big, 2));
}

/*
 * An 8-byte vector comes back in %mm0 on i386, and gcc issues no emms
 * after one, so its caller's x87 is unusable until the next: this runs
 * last and prints integers only.
 */
static void vector_mmx(void) {
    v8qi q = lib_v8q(1, (v8qi){1, 2, 3}, (v8qi){10, 20}, (v8qi){7, 0, 0, 0, 0, 0, 0, 9}, (v8qi){4, 5});

    printf("%d %d %d %d\n", q[0], q[1], q[2], q[7]);
}

/* structures whose eightbytes travel in xmm registers, general ones, or memory; complex values */
static void classes(void) {
    struct DD dd1 = {1.5, 2.0}, dd2 = {0.25, 4.0}, dd = lib_dd(dd1, dd2);
    struct FF ff1 = {1.5f, 2.0f}, ff2 = {0.25f, 4.0f}, ff = lib_ff(ff1, ff2);
    struct FFF f3 = {1.0f, 2.0f, 3.0f}, fff = lib_fff(f3);
    struct DL dl1 = {0.5, 7}, dl;
    struct LD ld1 = {9, 3.0}, ld;
    struct C6 c6 = {{1, 2, 3, 4, 5, 6}}, c6r = lib_c6(c6);
    struct C13 c13 = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}}, c13r = lib_c13(c13);
    struct X1 x1 = {2.5L}, x1r = lib_x1(x1, 3);
    struct XI xi = {1.25L, 4}, xir = lib_xi(xi);
    float _Complex cf = lib_cf(1.0f + 2.0fi, 3.0f - 1.0fi);
    double _Complex cd = lib_cd(1.0 + 2.0i, 3.0 + 4.0i);
    long double _Complex cl = lib_cl(1.5L - 2.0Li, 0.5L + 4.0Li);
    int i;

    dl = lib_dl(dl1, ld1);
    ld = lib_ld(ld1);
    printf("%g %g | %g %g | %g %g %g\n", dd.x, dd.y, (double)ff.x, (double)ff.y, (double)fff.x, (double)fff.y,
           (double)fff.z);
    printf("%g %lld | %lld %g |", dl.x, dl.y, ld.x, ld.y);
    for (i = 0; i < 6; ++i) {
        printf(" %d", c6r.c[i]);
    }
    printf(" |");
    for (i = 0; i < 13; ++i) {
        printf(" %d", c13r.c[i]);
    }
    printf("\n%Lg | %Lg %d\n", x1r.v, xir.v, xir.i);
    printf("%g\n", lib_many(dd1, dd2, dd1, dd2, dd1, 0.5, ff1));
    printf("%g %g\n", (double)lib_fl(1.5f, 2.0, 0.25f),
           lib_fl10(0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f, 8.5f, 9.5f));
#if __STDC_VERSION__ >= 202311L
    {
        _BitInt(100) b100 = lib_b100((_BitInt(100))123456789012345LL * 1000003, -987654321);
        unsigned _BitInt(200) b200 = lib_b200((unsigned _BitInt(200))0xfedcba9876543210ULL << 64, 70);
        _BitInt(130) b130 = lib_b130(-5, (_BitInt(130))1 << 129);

        printf("%llx %llx | %llx %llx %llx | %llx %llx\n", (unsigned long long)(b100 >> 64), (unsigned long long)b100,
               (unsigned long long)(b200 >> 128), (unsigned long long)(b200 >> 64), (unsigned long long)b200,
               (unsigned long long)(b130 >> 64), (unsigned long long)b130);
    }
#endif
#ifdef __SIZEOF_INT128__
    {
        __int128 i1 = lib_i128((__int128)-3 << 70, ~(unsigned __int128)0, 9);
        __int128 i2 = lib_i128s(1, 2, 3, 4, 5, (__int128)7 << 100);

        printf("%llx %llx | %llx %llx\n", (unsigned long long)(i1 >> 64), (unsigned long long)i1,
               (unsigned long long)(i2 >> 64), (unsigned long long)i2);
    }
#endif
    printf("%g %g | %.17g %.17g | %Lg %Lg\n", (double)__real__ cf, (double)__imag__ cf, __real__ cd, __imag__ cd,
           __real__ cl, __imag__ cl);
}

int main(void) {
    struct Q q = lib_q(5);
    struct R r = lib_r(3, 4);
    div_t dv = div(17, 5);
    ldiv_t lv = ldiv(-100L, 7L);
    struct Q arg = {7, 8};

    printf("%d %d | %d %d %d %d %d\n", q.a, q.b, r.a, r.b, r.c, r.d, r.e);
    printf("%d %d %ld %ld\n", dv.quot, dv.rem, lv.quot, lv.rem);
    printf("%d %lld\n", lib_sum(1, 2, 3, 4000000005LL, 1.5, arg), lib_wide(-5000000000LL, 0xfedcba9876543210ULL));
    classes();
    vectors();
    decimals();
    vector_mmx();
    return 0;
}
