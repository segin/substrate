/*
 * Integer constant expressions in every context that needs one: an
 * enumerator, an array bound, a static initializer, a case label and a
 * _Static_assert.  The parser, sema and lowering each used to evaluate
 * these with an evaluator of their own, and got different answers.
 */
#include <stdio.h>

#define CHECK(n, E)                                                                \
    enum { en_##n = (int)(E) };                                                    \
    static long long g_##n = (E);                                                  \
    static char arr_##n[((E) & 0x3f) + 1];                                         \
    _Static_assert(((E) & 0x3f) + 1 > 0, #E);                                      \
    static int case_##n(long long v) {                                             \
        switch (v) {                                                               \
        case (E):                                                                  \
            return 1;                                                              \
        default:                                                                   \
            return 0;                                                              \
        }                                                                          \
    }                                                                              \
    static void show_##n(void) {                                                   \
        printf("%-28s g=%lld en=%d arr=%u case=%d\n", #E, g_##n, en_##n,          \
               (unsigned)sizeof(arr_##n), case_##n((long long)(E)));               \
    }

CHECK(1, -1u / 2)
CHECK(2, -1u >> 1)
CHECK(3, (unsigned char)-1)
CHECK(4, (signed char)200)
CHECK(5, -1 < 0u)
CHECK(6, (unsigned)-1 > 0)
CHECK(7, 0 && 1 / 0)
CHECK(8, 1 || 1 / 0)
CHECK(9, -7 / 2)
CHECK(10, -7 % 2)
CHECK(11, 1u << 31 >> 31)
CHECK(12, (short)65535)
CHECK(13, (_Bool)256)
CHECK(14, sizeof(char[5]) * 3)
CHECK(15, ~0u >> 28)
CHECK(16, !5 + !0)
CHECK(17, (long long)-1 >> 60)
CHECK(18, 0xffffffffu + 1)
CHECK(19, __builtin_popcount(255))
CHECK(20, __builtin_ctz(16))
CHECK(21, __builtin_bswap16(0x1234))
CHECK(22, __builtin_choose_expr(1, 7, 8))
CHECK(23, __builtin_constant_p(3))
CHECK(24, __builtin_expect(9, 1))
CHECK(25, sizeof("abc"))
CHECK(26, -(unsigned short)1)
CHECK(27, 1 ? -1 : 0u)
CHECK(28, (char)300 == 44)
CHECK(29, (char)100 + (char)100)
CHECK(30, (int)3.7)

int main(void) {
    show_1(); show_2(); show_3(); show_4(); show_5(); show_6(); show_7(); show_8();
    show_9(); show_10(); show_11(); show_12(); show_13(); show_14(); show_15();
    show_16(); show_17(); show_18(); show_19(); show_20(); show_21(); show_22();
    show_23(); show_24(); show_25(); show_26(); show_27(); show_28(); show_29();
    show_30();
    return 0;
}
