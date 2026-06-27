/*
 * src/atomic.c — outline C11 atomic operations for 1/2/4-byte objects.
 *
 * The compiler emits calls to these __atomic_* helpers when it can't expand an
 * atomic operation inline — chiefly for sub-word objects in libstdc++
 * (std::atomic<bool> / <char>).  Substrate ships no libatomic, so libc provides
 * the small-object set, implemented with the __sync_* builtins (LOCK-prefixed
 * cmpxchg / xadd, available on i486+).  Because libc is auto-linked *after*
 * libstdc++, these resolve libstdc++'s references without any -latomic
 * link-ordering games.
 */
#include <stdbool.h>
#include <stdint.h>

#ifndef __ATOMIC_SEQ_CST
#define __ATOMIC_SEQ_CST 5
#endif

#define ATOMIC_DEFS(N, T)                                                      \
T __atomic_load_##N(const volatile void *p, int mo) {                          \
    (void) mo;                                                                 \
    return *(const volatile T *) p;          /* x86 loads are acquire */       \
}                                                                              \
void __atomic_store_##N(volatile void *p, T v, int mo) {                       \
    *(volatile T *) p = v;                   /* x86 stores are release */      \
    if (mo == __ATOMIC_SEQ_CST)                                                \
        __sync_synchronize();                /* seq_cst needs a full fence */  \
}                                                                              \
T __atomic_exchange_##N(volatile void *p, T v, int mo) {                       \
    (void) mo;                                                                 \
    return __sync_lock_test_and_set((volatile T *) p, v);                      \
}                                                                              \
bool __atomic_compare_exchange_##N(volatile void *p, void *expected, T des,    \
                                   bool weak, int s, int f) {                   \
    (void) weak; (void) s; (void) f;                                           \
    T e = *(T *) expected;                                                     \
    T old = __sync_val_compare_and_swap((volatile T *) p, e, des);             \
    if (old == e)                                                              \
        return true;                                                           \
    *(T *) expected = old;                                                     \
    return false;                                                              \
}                                                                              \
T __atomic_fetch_add_##N(volatile void *p, T v, int mo) {                      \
    (void) mo; return __sync_fetch_and_add((volatile T *) p, v);               \
}                                                                              \
T __atomic_fetch_sub_##N(volatile void *p, T v, int mo) {                      \
    (void) mo; return __sync_fetch_and_sub((volatile T *) p, v);               \
}                                                                              \
T __atomic_fetch_and_##N(volatile void *p, T v, int mo) {                      \
    (void) mo; return __sync_fetch_and_and((volatile T *) p, v);               \
}                                                                              \
T __atomic_fetch_or_##N(volatile void *p, T v, int mo) {                       \
    (void) mo; return __sync_fetch_and_or((volatile T *) p, v);                \
}                                                                              \
T __atomic_fetch_xor_##N(volatile void *p, T v, int mo) {                      \
    (void) mo; return __sync_fetch_and_xor((volatile T *) p, v);               \
}

ATOMIC_DEFS(1, uint8_t)
ATOMIC_DEFS(2, uint16_t)
ATOMIC_DEFS(4, uint32_t)
