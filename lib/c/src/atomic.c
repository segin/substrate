/*
 * src/atomic.c — outline C11 atomic operations for 1/2/4/8-byte objects.
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
#include <stddef.h>
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

/*
 * 8-byte objects.  The i486 baseline has no cmpxchg8b, so the __sync_*
 * builtins the macro above relies on do not exist at this width and the
 * compiler emits calls to the helpers below instead -- OpenSSL built with
 * threads, and any C++ std::atomic<int64_t>, land here.
 *
 * Implemented the way libatomic does it: a small pool of spinlocks indexed by
 * a hash of the object address.  Correct on any CPU, but not lock-free, which
 * is what __atomic_is_lock_free reports.  The lock word itself is 32-bit, so
 * it uses the always-available xchg path.
 */
#define ATOMIC8_NLOCKS 64
static volatile int atomic8_locks[ATOMIC8_NLOCKS];

static inline volatile int *atomic8_lock_for(const volatile void *p) {
    uintptr_t a = (uintptr_t) p;
    a >>= 3;                                  /* 8-byte objects are 8-aligned */
    a ^= a >> 8;
    return &atomic8_locks[a & (ATOMIC8_NLOCKS - 1)];
}

static inline void atomic8_lock(volatile int *l) {
    while (__sync_lock_test_and_set(l, 1))
        while (*l)
            __asm__ __volatile__("" ::: "memory");   /* spin, re-read *l */
}

static inline void atomic8_unlock(volatile int *l) {
    __sync_lock_release(l);
}

uint64_t __atomic_load_8(const volatile void *p, int mo) {
    (void) mo;
    volatile int *l = atomic8_lock_for(p);
    atomic8_lock(l);
    uint64_t v = *(const volatile uint64_t *) p;
    atomic8_unlock(l);
    return v;
}

void __atomic_store_8(volatile void *p, uint64_t v, int mo) {
    (void) mo;
    volatile int *l = atomic8_lock_for(p);
    atomic8_lock(l);
    *(volatile uint64_t *) p = v;
    atomic8_unlock(l);
}

uint64_t __atomic_exchange_8(volatile void *p, uint64_t v, int mo) {
    (void) mo;
    volatile int *l = atomic8_lock_for(p);
    atomic8_lock(l);
    uint64_t old = *(volatile uint64_t *) p;
    *(volatile uint64_t *) p = v;
    atomic8_unlock(l);
    return old;
}

bool __atomic_compare_exchange_8(volatile void *p, void *expected, uint64_t des,
                                 bool weak, int s, int f) {
    (void) weak; (void) s; (void) f;
    volatile int *l = atomic8_lock_for(p);
    atomic8_lock(l);
    uint64_t old = *(volatile uint64_t *) p;
    bool eq = (old == *(uint64_t *) expected);
    if (eq)
        *(volatile uint64_t *) p = des;
    else
        *(uint64_t *) expected = old;
    atomic8_unlock(l);
    return eq;
}

#define ATOMIC8_FETCH_OP(name, op)                                             \
uint64_t __atomic_fetch_##name##_8(volatile void *p, uint64_t v, int mo) {     \
    (void) mo;                                                                 \
    volatile int *l = atomic8_lock_for(p);                                     \
    atomic8_lock(l);                                                           \
    uint64_t old = *(volatile uint64_t *) p;                                   \
    *(volatile uint64_t *) p = old op v;                                       \
    atomic8_unlock(l);                                                         \
    return old;                                                                \
}

ATOMIC8_FETCH_OP(add, +)
ATOMIC8_FETCH_OP(sub, -)
ATOMIC8_FETCH_OP(and, &)
ATOMIC8_FETCH_OP(or,  |)
ATOMIC8_FETCH_OP(xor, ^)

/*
 * Runtime half of std::atomic<T>::is_lock_free().  1/2/4-byte objects use the
 * inline LOCK-prefixed instructions above; 8-byte objects go through the lock
 * pool, so they are not lock-free.  An unaligned object of any size is not
 * either.
 */
bool __atomic_is_lock_free(size_t size, const volatile void *p) {
    uintptr_t a = (uintptr_t) p;
    switch (size) {
    case 1: return true;
    case 2: return a == 0 || (a & 1) == 0;
    case 4: return a == 0 || (a & 3) == 0;
    default: return false;
    }
}
