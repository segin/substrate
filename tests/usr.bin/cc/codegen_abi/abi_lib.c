/*
 * The half of the ABI test that run_codegen_abi.sh builds with one
 * compiler while abi_main.c is built with the other.
 */
struct Q {
    int a, b;
};

struct R {
    int a, b, c, d, e;
};

struct Q lib_q(int a) {
    struct Q q = {a, a * 2};
    return q;
}

struct R lib_r(int a, int b) {
    struct R r = {a, b, a + b, a * b, 9};
    return r;
}

int lib_sum(char c, short s, int i, long long ll, double d, struct Q q) {
    return c + s + i + (int)(ll % 1000) + (int)(d * 10) + q.a * 100 + q.b;
}

long long lib_wide(long long a, unsigned long long b) {
    return a * 3 + (long long)(b >> 4);
}

/* x86-64 classifies each eightbyte: these go in xmm registers, general ones, or both */
struct DD { double x, y; };
struct FF { float x, y; };
struct FFF { float x, y, z; };
struct DL { double x; long long y; };
struct LD { long long x; double y; };
struct C6 { char c[6]; };
struct C13 { char c[13]; };
struct X1 { long double v; };
struct XI { long double v; int i; };

struct DD lib_dd(struct DD a, struct DD b) { struct DD r = {a.x + b.x, a.y * b.y}; return r; }
struct FF lib_ff(struct FF a, struct FF b) { struct FF r = {a.x + b.x, a.y * b.y}; return r; }
struct FFF lib_fff(struct FFF a) { struct FFF r = {a.z, a.y, a.x}; return r; }
struct DL lib_dl(struct DL a, struct LD b) { struct DL r = {a.x + b.y, a.y + b.x}; return r; }
struct LD lib_ld(struct LD a) { struct LD r = {a.x * 2, a.y / 2}; return r; }
struct C6 lib_c6(struct C6 a) { struct C6 r; int i; for (i = 0; i < 6; ++i) r.c[i] = (char)(a.c[5 - i] + 1); return r; }
struct C13 lib_c13(struct C13 a) { struct C13 r; int i; for (i = 0; i < 13; ++i) r.c[i] = (char)(a.c[12 - i] + 1); return r; }
struct X1 lib_x1(struct X1 a, long double k) { struct X1 r = {a.v * k}; return r; }
struct XI lib_xi(struct XI a) { struct XI r = {a.v + a.i, a.i + 1}; return r; }

/* more SSE arguments than there are xmm registers: the rest go on the stack */
double lib_many(struct DD a, struct DD b, struct DD c, struct DD d, struct DD e, double f, struct FF g) {
    return a.x + b.y + c.x + d.y + e.x + e.y + f + g.x + g.y;
}

/* a prototyped float is passed and returned in single precision */
float lib_fl(float a, double b, float c) { return a * 2 + (float)b + c; }
double lib_fl10(float a, float b, float c, float d, float e, float f, float g, float h, float i, float j) {
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 + i * 9 + j * 10;
}

#if __STDC_VERSION__ >= 202311L
/* integers wider than 64 bits: in general registers up to 16 bytes, else in memory */
_BitInt(100) lib_b100(_BitInt(100) a, _BitInt(100) b) { return a * b - (a >> 3); }
unsigned _BitInt(200) lib_b200(unsigned _BitInt(200) a, int k) { return (a << k) / 7; }
_BitInt(130) lib_b130(long long x, _BitInt(130) y) { return y + x; }
#endif
#ifdef __SIZEOF_INT128__
__int128 lib_i128(__int128 a, unsigned __int128 b, long c) { return a * c + (__int128)(b >> 1); }
/* six general registers used before it: the __int128 goes on the stack, 16-aligned */
__int128 lib_i128s(long a, long b, long c, long d, long e, __int128 f) { return f + a + b + c + d + e; }
#endif

/*
 * Vectors: 16 bytes in one xmm register (the first three on i386, the
 * rest on the stack 16-aligned), 8 bytes SSE on x86-64 and MMX on i386, 4
 * bytes as an integer, 32 and 64 bytes in memory aligned to their size.
 */
typedef int v4si __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));
typedef char v8qi __attribute__((vector_size(8)));
typedef short v2hi __attribute__((vector_size(4)));
typedef int v8si __attribute__((vector_size(32)));
typedef int v16si __attribute__((vector_size(64)));
v4si lib_v4(v4si a, v4si b) { return a * b - a; }
v4sf lib_v4f(int n, v4sf a, double d, v4sf b) { return a * b + (float)(n + d); }
v4si lib_v4n(int x, v4si a, v4si b, v4si c, int y, v4si d) { return a + b + c + d + x * y; }
v2hi lib_v2(v2hi a) { return a * a; }
v8si lib_v8(int k, v8si a) { return a + k; }
int lib_v16(int x, v16si a, int y) { return x + y + a[0] + a[15]; }
v8qi lib_v8q(int x, v8qi a, v8qi b, v8qi c, v8qi d) { return a + b + c - d + (char)x; }

float _Complex lib_cf(float _Complex a, float _Complex b) { return a * b + 1.0f; }
double _Complex lib_cd(double _Complex a, double _Complex b) { return a / b; }
long double _Complex lib_cl(long double _Complex a, long double _Complex b) { return a * b - 1.0L; }
