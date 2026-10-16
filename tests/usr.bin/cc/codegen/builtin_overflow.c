/*
 * The GNU overflow builtins at both widths.  On i386 every signed 64-bit
 * case reported no overflow and stored 0, and an int sum was computed in
 * 32 bits, so it wrapped before it was checked.
 */
#include <stdio.h>
#include <limits.h>
int main(void) {
    long long r; unsigned long long u; int o, i; unsigned ui; short s; unsigned char uc;
    long long a = 9223372036854775807LL, b = 1;
    o = __builtin_add_overflow(a, b, &r); printf("%d %lld ", o, r);
    o = __builtin_sub_overflow(a, -b, &r); printf("%d %lld ", o, r);
    o = __builtin_add_overflow(-a, -b - 1, &r); printf("%d %lld ", o, r);
    o = __builtin_mul_overflow(a, 2LL, &r); printf("%d %lld ", o, r);
    o = __builtin_mul_overflow(-3LL, 5LL, &r); printf("%d %lld ", o, r);
    o = __builtin_add_overflow(~0ULL, 1ULL, &u); printf("%d %llu ", o, u);
    o = __builtin_mul_overflow(~0ULL, 2ULL, &u); printf("%d %llu ", o, u);
    o = __builtin_mul_overflow(4294967296ULL, 4294967296ULL, &u); printf("%d %llu ", o, u);
    o = __builtin_add_overflow(INT_MAX, 1, &i); printf("%d %d ", o, i);
    o = __builtin_sub_overflow(INT_MIN, 1, &i); printf("%d %d ", o, i);
    o = __builtin_mul_overflow(65536, 65536, &i); printf("%d %d ", o, i);
    o = __builtin_mul_overflow(-2, 3, &i); printf("%d %d ", o, i);
    o = __builtin_add_overflow(3u, 4294967295u, &ui); printf("%d %u ", o, ui);
    o = __builtin_sub_overflow(1, 2, &ui); printf("%d %u ", o, ui);
    o = __builtin_add_overflow(32767, 1, &s); printf("%d %d ", o, s);
    o = __builtin_add_overflow(-1, 0u, &ui); printf("%d %u ", o, ui);
    o = __builtin_add_overflow(200, 100, &uc); printf("%d %u ", o, uc);
    o = __builtin_mul_overflow(-1LL, LLONG_MIN, &r); printf("%d %lld ", o, r);
    o = __builtin_add_overflow(1, 2, &i); printf("%d %d ", o, i);
    o = __builtin_add_overflow_p(INT_MAX, 1, (int)0); printf("%d ", o);
    o = __builtin_mul_overflow_p(3LL, 4LL, (long long)0); printf("%d\n", o);
    return 0;
}
