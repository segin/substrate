/*
 * The __sync and __atomic read-modify-write builtins on objects of 1, 2, 4
 * and 8 bytes, each printing what it returned and what it left; and code
 * a later CPU has instructions for (a byte swap, a maximum, a floating
 * compare).  run_march_opcodes.sh builds it for each i386 -march and
 * holds cc to that CPU's instructions: below the 486 the builtins are
 * calls, as gcc makes them (march_atomic_lib.c answers them).
 */
#include <stdio.h>

#define RMW(T, fmt)                                                                                               \
    static void rmw_##T(void) {                                                                                   \
        static volatile T o;                                                                                      \
        T r;                                                                                                      \
        int ok;                                                                                                   \
                                                                                                                  \
        o = 100;                                                                                                  \
        r = __sync_fetch_and_add(&o, 7);                                                                          \
        printf(#T " fetch_and_add " fmt " " fmt "\n", r, o);                                                      \
        r = __sync_sub_and_fetch(&o, 3);                                                                          \
        printf(#T " sub_and_fetch " fmt " " fmt "\n", r, o);                                                      \
        r = __sync_fetch_and_or(&o, 0x30);                                                                        \
        printf(#T " fetch_and_or " fmt " " fmt "\n", r, o);                                                       \
        r = __sync_and_and_fetch(&o, 0x7c);                                                                       \
        printf(#T " and_and_fetch " fmt " " fmt "\n", r, o);                                                      \
        r = __sync_fetch_and_xor(&o, 0x0f);                                                                       \
        printf(#T " fetch_and_xor " fmt " " fmt "\n", r, o);                                                      \
        r = __sync_nand_and_fetch(&o, 0x55);                                                                      \
        printf(#T " nand_and_fetch " fmt " " fmt "\n", r, o);                                                     \
        r = __sync_val_compare_and_swap(&o, o, 42);                                                               \
        printf(#T " val_cas " fmt " " fmt "\n", r, o);                                                            \
        r = __sync_val_compare_and_swap(&o, 41, 9);                                                               \
        printf(#T " val_cas_miss " fmt " " fmt "\n", r, o);                                                       \
        ok = __sync_bool_compare_and_swap(&o, 42, 17);                                                            \
        printf(#T " bool_cas %d " fmt "\n", ok, o);                                                               \
        r = __sync_lock_test_and_set(&o, 5);                                                                      \
        printf(#T " test_and_set " fmt " " fmt "\n", r, o);                                                       \
        r = __atomic_fetch_add(&o, 11, __ATOMIC_SEQ_CST);                                                         \
        printf(#T " atomic_fetch_add " fmt " " fmt "\n", r, o);                                                   \
        r = __atomic_sub_fetch(&o, 2, __ATOMIC_RELAXED);                                                          \
        printf(#T " atomic_sub_fetch " fmt " " fmt "\n", r, o);                                                   \
        r = __atomic_fetch_nand(&o, 6, __ATOMIC_ACQ_REL);                                                         \
        printf(#T " atomic_fetch_nand " fmt " " fmt "\n", r, o);                                                  \
        r = __atomic_exchange_n(&o, 3, __ATOMIC_SEQ_CST);                                                         \
        printf(#T " atomic_exchange " fmt " " fmt "\n", r, o);                                                    \
    }

RMW(char, "%d")
RMW(short, "%d")
RMW(int, "%d")
typedef long long llong;
RMW(llong, "%lld")

static unsigned swap32(unsigned x) { return __builtin_bswap32(x); }
static unsigned long long swap64(unsigned long long x) { return __builtin_bswap64(x); }
static int max(int a, int b) { return a > b ? a : b; }
static int less(double a, double b) { return a < b; }

int main(void) {
    volatile unsigned u = 0x12345678;
    volatile unsigned long long w = 0x0123456789abcdefULL;
    volatile int a = 3, b = -4;
    volatile double x = 1.5, y = 2.5;

    rmw_char();
    rmw_short();
    rmw_int();
    rmw_llong();
    printf("%x %llx %d %d %d\n", swap32(u), swap64(w), max(a, b), less(x, y), less(y, x));
    return 0;
}
