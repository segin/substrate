/*
 * A 64-bit constant expression is computed at compile time, at every
 * optimization level, as gcc's front end does: on i386 cc split its
 * operations into 32-bit words first and called libgcc's __divdi3 for a
 * division of constants (Linux's KTIME_MAX / NSEC_PER_SEC, which it
 * does not link libgcc for).  run_div64_pow2.sh checks there is no call;
 * this checks the values.
 */
#include <stdio.h>

#define KTIME_MAX ((long long)~((unsigned long long)1 << 63))
#define NSEC_PER_SEC 1000000000L
#define KTIME_SEC_MAX (KTIME_MAX / NSEC_PER_SEC)
#define TIME_UPTIME_SEC_MAX (30LL * 365 * 24 * 3600)
#define TIME_SETTOD_SEC_MAX (KTIME_SEC_MAX - TIME_UPTIME_SEC_MAX)

static int valid_settod(long long sec) {
    return (unsigned long long)sec < TIME_SETTOD_SEC_MAX;
}

int main(int argc, char **argv) {
    long long k = argc - 1; /* 0, unknown to the compiler */

    (void)argv;
    printf("%lld %lld %d %d\n", KTIME_SEC_MAX, TIME_SETTOD_SEC_MAX, valid_settod(k + 5), valid_settod(k - 1));
    printf("%llu %lld %lld %llu\n", 1000000000000ULL / 3 + k, -1000000000000LL / 7 + k, -1000000000000LL % 7 + k,
           0xfedcba9876543210ULL % 1000003 + k);
    printf("%lld %llu %lld\n", (1LL << 40) * 3 - 1 + k, 0xffffffffffffffffULL >> 7, -(1LL << 62) >> 3);
    return 0;
}
