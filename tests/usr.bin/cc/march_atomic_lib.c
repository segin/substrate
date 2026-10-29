/*
 * The __sync_*_N and __atomic_*_N functions gcc and cc call for an atomic
 * builtin on an i386 -march without the instruction (march_atomics.c);
 * libgcc on x86 has none of them.  Built by gcc for an i686, where each
 * builtin is the instruction itself.
 */

#define SYNC2(op, N, T)                                                                                           \
    T sync_##op##_##N(volatile T *p, T v) __asm__("__sync_" #op "_" #N);                                          \
    T sync_##op##_##N(volatile T *p, T v) { return __sync_##op(p, v); }
#define ATOMIC3(op, N, T)                                                                                         \
    T atomic_##op##_##N(volatile T *p, T v, int mo) __asm__("__atomic_" #op "_" #N);                              \
    T atomic_##op##_##N(volatile T *p, T v, int mo) {                                                             \
        (void)mo;                                                                                                 \
        return __atomic_##op(p, v, __ATOMIC_SEQ_CST);                                                             \
    }

#define ALL(N, T)                                                                                                 \
    SYNC2(fetch_and_add, N, T)                                                                                    \
    SYNC2(fetch_and_or, N, T)                                                                                     \
    SYNC2(fetch_and_xor, N, T)                                                                                    \
    SYNC2(sub_and_fetch, N, T)                                                                                    \
    SYNC2(and_and_fetch, N, T)                                                                                    \
    SYNC2(nand_and_fetch, N, T)                                                                                   \
    SYNC2(lock_test_and_set, N, T)                                                                                \
    ATOMIC3(fetch_add, N, T)                                                                                      \
    ATOMIC3(sub_fetch, N, T)                                                                                      \
    ATOMIC3(fetch_nand, N, T)                                                                                     \
    T atomic_exchange_##N(volatile T *p, T v, int mo) __asm__("__atomic_exchange_" #N);                           \
    T atomic_exchange_##N(volatile T *p, T v, int mo) {                                                           \
        (void)mo;                                                                                                 \
        return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);                                                       \
    }                                                                                                             \
    T sync_val_cas_##N(volatile T *p, T o, T n) __asm__("__sync_val_compare_and_swap_" #N);                       \
    T sync_val_cas_##N(volatile T *p, T o, T n) { return __sync_val_compare_and_swap(p, o, n); }                  \
    _Bool sync_bool_cas_##N(volatile T *p, T o, T n) __asm__("__sync_bool_compare_and_swap_" #N);                 \
    _Bool sync_bool_cas_##N(volatile T *p, T o, T n) { return __sync_bool_compare_and_swap(p, o, n); }            \
    _Bool atomic_cas_##N(volatile T *p, T *e, T n, _Bool weak, int so, int fo)                                    \
        __asm__("__atomic_compare_exchange_" #N);                                                                 \
    _Bool atomic_cas_##N(volatile T *p, T *e, T n, _Bool weak, int so, int fo) {                                  \
        (void)weak;                                                                                               \
        (void)so;                                                                                                 \
        (void)fo;                                                                                                 \
        return __atomic_compare_exchange_n(p, e, n, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);                       \
    }

ALL(1, unsigned char)
ALL(2, unsigned short)
ALL(4, unsigned int)
ALL(8, unsigned long long)

/* before the Pentium an 8-byte load or store is a call too */
unsigned long long atomic_load_8(volatile unsigned long long *p, int mo) __asm__("__atomic_load_8");
unsigned long long atomic_load_8(volatile unsigned long long *p, int mo) {
    (void)mo;
    return __atomic_load_n(p, __ATOMIC_SEQ_CST);
}
void atomic_store_8(volatile unsigned long long *p, unsigned long long v, int mo) __asm__("__atomic_store_8");
void atomic_store_8(volatile unsigned long long *p, unsigned long long v, int mo) {
    (void)mo;
    __atomic_store_n(p, v, __ATOMIC_SEQ_CST);
}
