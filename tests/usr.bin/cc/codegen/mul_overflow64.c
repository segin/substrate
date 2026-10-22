/*
 * __builtin_mul_overflow on 64-bit operands: on i386 cc checked the
 * product by dividing it back by an operand, a call of libgcc's
 * __udivdi3 or __divdi3, which Linux does not link (check_mul_overflow()
 * in lib/kstrtox.c, kernel/sched/fair.c, mm/backing-dev.c).  It is
 * computed from 32-bit halves now; run_div64_pow2.sh checks there is no
 * call, this the results at the edges against gcc.
 */
#include <stdio.h>

static volatile unsigned long long uv[] = {0ULL,
                                           1ULL,
                                           2ULL,
                                           3ULL,
                                           0xffffffffULL,
                                           0x100000000ULL,
                                           0x100000001ULL,
                                           0x7fffffffffffffffULL,
                                           0x8000000000000000ULL,
                                           0xffffffffffffffffULL,
                                           0x123456789ULL,
                                           0xfffffffffULL,
                                           4294967296ULL * 4294967295ULL};
static volatile long long sv[] = {0LL,
                                  1LL,
                                  -1LL,
                                  2LL,
                                  -2LL,
                                  3037000499LL,
                                  3037000500LL,
                                  -3037000500LL,
                                  0x100000000LL,
                                  -0x100000000LL,
                                  0x7fffffffffffffffLL,
                                  -0x7fffffffffffffffLL - 1,
                                  0x4000000000000000LL,
                                  -0x4000000000000000LL};

int main(void) {
    unsigned long long uh = 0;
    unsigned long long sh = 0;
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(uv) / sizeof(uv[0]); ++i) {
        for (j = 0; j < sizeof(uv) / sizeof(uv[0]); ++j) {
            unsigned long long r;
            int o = __builtin_mul_overflow(uv[i], uv[j], &r);

            uh = uh * 131 + (unsigned long long)o * 7 + r;
        }
    }
    for (i = 0; i < sizeof(sv) / sizeof(sv[0]); ++i) {
        for (j = 0; j < sizeof(sv) / sizeof(sv[0]); ++j) {
            long long r;
            int o = __builtin_mul_overflow(sv[i], sv[j], &r);

            sh = sh * 131 + (unsigned long long)o * 7 + (unsigned long long)r;
            if (i == j) {
                printf("%d", o);
            }
        }
    }
    {
        /* unsigned operands have no negative result: 2^63 and over is a large number, not an overflow */
        unsigned long long r;
        int o1 = __builtin_add_overflow(uv[8], uv[0], &r);
        int o2 = __builtin_mul_overflow(uv[8], uv[1], &r);
        int o3 = __builtin_add_overflow(uv[7], uv[1], &r);

        printf("\n%d %d %d", o1, o2, o3);
    }
    printf("\n%llx %llx\n", uh, sh);
    return 0;
}
