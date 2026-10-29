/*
 * GCC vector types (vector_size): layout, brace initialization, subscripts,
 * and every element-wise operator for each element type, with vector and
 * scalar operands, compound assignment, ++/--, comparisons (-1 or 0),
 * casts that keep the bits, compound literals, static initializers, and
 * vectors in structures and arrays.  Results are printed element by
 * element, so they must be gcc's exactly.
 *
 * cc-march-m32: i686 pentium4 (an 8-byte vector's alignment, without MMX and with it)
 */
#include <stdio.h>
#include <string.h>

typedef signed char v16qi __attribute__((vector_size(16)));
typedef unsigned char v8uqi __attribute__((vector_size(8)));
typedef short v8hi __attribute__((vector_size(16)));
typedef unsigned short v4uhi __attribute__((vector_size(8)));
typedef int v4si __attribute__((vector_size(16)));
typedef unsigned int v4usi __attribute__((vector_size(16)));
typedef long long v2di __attribute__((vector_size(16)));
typedef unsigned long long v2udi __attribute__((vector_size(16)));
typedef float v4sf __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));
typedef int v8si __attribute__((vector_size(32)));
typedef short v2hi __attribute__((vector_size(4)));

static void bytes(const char *tag, const void *p, size_t n) {
    const unsigned char *b = p;
    size_t i;

    printf("%s:", tag);
    for (i = 0; i < n; ++i) {
        printf("%02x", b[i]);
    }
    printf("\n");
}

#define SHOW(x) bytes(#x, &(x), sizeof(x))

/* every integer operator, with a vector, a scalar on the right and one on the left */
#define INT_OPS(T, a, b, s)                                                                                          \
    do {                                                                                                             \
        T r_add = a + b, r_sub = a - b, r_mul = a * b, r_div = a / b, r_mod = a % b;                                 \
        T r_and = a & b, r_or = a | b, r_xor = a ^ b, r_shl = a << 3, r_shr = a >> 2;                                \
        T r_neg = -a, r_not = ~a, r_pos = +a, r_ls = s * a, r_rs = a - s, r_sv = s - a;                              \
        SHOW(r_add); SHOW(r_sub); SHOW(r_mul); SHOW(r_div); SHOW(r_mod);                                             \
        SHOW(r_and); SHOW(r_or); SHOW(r_xor); SHOW(r_shl); SHOW(r_shr);                                              \
        SHOW(r_neg); SHOW(r_not); SHOW(r_pos); SHOW(r_ls); SHOW(r_rs); SHOW(r_sv);                                   \
    } while (0)

#define CMP_OPS(a, b)                                                                                                \
    do {                                                                                                             \
        __typeof__(a < b) c_lt = a < b, c_le = a <= b, c_gt = a > b, c_ge = a >= b, c_eq = a == b, c_ne = a != b;    \
        __typeof__(a < b) c_s = a > 3;                                                                               \
        SHOW(c_lt); SHOW(c_le); SHOW(c_gt); SHOW(c_ge); SHOW(c_eq); SHOW(c_ne); SHOW(c_s);                           \
    } while (0)

#define FP_OPS(T, a, b, s)                                                                                           \
    do {                                                                                                             \
        T f_add = a + b, f_sub = a - b, f_mul = a * b, f_div = a / b, f_neg = -a, f_ls = s * a, f_rs = a / s;        \
        SHOW(f_add); SHOW(f_sub); SHOW(f_mul); SHOW(f_div); SHOW(f_neg); SHOW(f_ls); SHOW(f_rs);                     \
    } while (0)

struct holder {
    char c;
    v4si v;
    v2df d[2];
};

static v4si g_v = {1, -2, 3, -4};
static v4sf g_f = {0.5f, 1.25f, -3.0f};
static struct holder g_h = {'x', {5, 6, 7, 8}, {{1.0, 2.0}, {3.0, 4.0}}};

int main(void) {
    volatile int seven = 7;
    v16qi qa = {1, -2, 3, -4, 5, -6, 7, -8, 100, -100, 127, -128, 9, 10, 11, 12};
    v16qi qb = {3, 3, -3, -3, 2, 2, 5, 5, 7, -7, 2, 3, 4, 5, 6, 7};
    v8uqi ua = {200, 1, 2, 255, 128, 7, 9, 64}, ub = {3, 2, 1, 4, 5, 6, 7, 8};
    v8hi ha = {1000, -2000, 3000, -4000, 5, 6, 7, 32767}, hb = {7, 9, -11, 13, 2, 3, 4, 5};
    v4uhi uha = {65535, 1, 40000, 3}, uhb = {3, 7, 9, 2};
    v4si ia = {100, -200, 300, seven}, ib = {7, 9, -11, 2};
    v4usi uia = {4000000000u, 1, 2, 3}, uib = {3, 2, 7, 5};
    v2di la = {-1234567890123LL, 99}, lb = {1000, -7};
    v2udi ula = {18446744073709551000ULL, 5}, ulb = {7, 3};
    v4sf fa = {1.5f, -2.25f, 3.0f, 1e10f}, fb = {0.5f, 4.0f, -8.0f, 3.0f};
    v2df da = {1.0 / 3, -2.5}, db = {7.0, 0.125};
    v8si wide = {1, 2, 3, 4, 5, 6, 7, 8};
    v2hi tiny = {-3, 4};
    struct holder h;
    v4si arr[3] = {{1, 2, 3, 4}, {5, 6}, {0}};
    v4si *pv = &arr[1];
    int i;

    printf("%zu %zu %zu %zu %zu %zu\n", sizeof(v16qi), _Alignof(v8uqi), sizeof(v8si), _Alignof(v8si),
           _Alignof(v2hi), sizeof(struct holder));

    INT_OPS(v16qi, qa, qb, 2);
    INT_OPS(v8uqi, ua, ub, 3);
    INT_OPS(v8hi, ha, hb, 5);
    INT_OPS(v4uhi, uha, uhb, 6);
    INT_OPS(v4si, ia, ib, 9);
    INT_OPS(v4usi, uia, uib, 4);
    INT_OPS(v2di, la, lb, 11);
    INT_OPS(v2udi, ula, ulb, 2);
    CMP_OPS(qa, qb);
    CMP_OPS(ua, ub);
    CMP_OPS(ha, hb);
    CMP_OPS(uia, uib);
    CMP_OPS(la, lb);
    CMP_OPS(ula, ulb);
    CMP_OPS(fa, fb);
    CMP_OPS(da, db);
    FP_OPS(v4sf, fa, fb, 2.0f);
    FP_OPS(v2df, da, db, 3.0);

    /* compound assignment, ++ and --, subscripts as lvalues */
    ia += ib;
    ia *= 3;
    ia <<= 1;
    ++ia;
    ia--;
    ia[2] = 77;
    ia[seven - 7] += 1000;
    fa /= 2;
    fa[1]++;
    SHOW(ia);
    SHOW(fa);
    printf("%d %d %g\n", ia[3], ib[seven - 6], (double)fb[seven - 4]);

    /* casts keep the bits; a vector reads as an integer of its size */
    {
        v4si bits = (v4si)fa;
        v2df back = (v2df)ia;
        long long whole = (long long)ua;
        v4uhi from = (v4uhi)0x0001000200030004LL;
        int t = (int)tiny;

        SHOW(bits);
        SHOW(back);
        printf("%llx %x\n", whole, t);
        SHOW(from);
    }

    /* compound literals, statics, members, arrays, pointers, the conditional operator */
    {
        v4si lit = (v4si){1, 2, 3, 4} * (v4si){5, 6, 7, 8};
        v4si pick = seven > 3 ? g_v : lit;

        SHOW(lit);
        SHOW(pick);
        SHOW(g_v);
        SHOW(g_f);
        h = g_h;
        h.v[1] = -6;
        h.d[1][0] = h.d[0][1] * 10;
        SHOW(h.v);
        SHOW(h.d);
        (*pv)[3] = 44;
        arr[2] = arr[0] + *pv;
        SHOW(arr);
        wide = wide * wide - 1;
        SHOW(wide);
        tiny = tiny * tiny;
        SHOW(tiny);
    }
    for (i = 0; i < 4; ++i) {
        printf("%d ", g_v[i] + arr[2][i]);
    }
    printf("\n");
    return 0;
}
