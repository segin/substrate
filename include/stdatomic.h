#ifndef _STDATOMIC_H_
#define _STDATOMIC_H_

#ifdef __cplusplus
extern "C" {
#endif

#define __STDC_NO_ATOMICS__ 0

typedef enum {
	memory_order_relaxed = __ATOMIC_RELAXED,
	memory_order_consume = __ATOMIC_CONSUME,
	memory_order_acquire = __ATOMIC_ACQUIRE,
	memory_order_release = __ATOMIC_RELEASE,
	memory_order_acq_rel = __ATOMIC_ACQ_REL,
	memory_order_seq_cst = __ATOMIC_SEQ_CST
} memory_order;

typedef _Atomic char atomic_char;
typedef _Atomic signed char atomic_schar;
typedef _Atomic unsigned char atomic_uchar;
typedef _Atomic short atomic_short;
typedef _Atomic unsigned short atomic_ushort;
typedef _Atomic int atomic_int;
typedef _Atomic unsigned int atomic_uint;
typedef _Atomic long atomic_long;
typedef _Atomic unsigned long atomic_ulong;
typedef _Atomic long long atomic_llong;
typedef _Atomic unsigned long long atomic_ullong;
typedef void * _Atomic atomic_ptr;

/*
 * Sized integer atomics — C++ <atomic> typedefs std::atomic_int8_t
 * et al. as the C ones below.  Substrate's <stdint.h> already
 * provides the underlying int8_t..int64_t / uint8_t..uint64_t.
 */
#include <stdint.h>
typedef _Atomic int8_t   atomic_int8_t;
typedef _Atomic uint8_t  atomic_uint8_t;
typedef _Atomic int16_t  atomic_int16_t;
typedef _Atomic uint16_t atomic_uint16_t;
typedef _Atomic int32_t  atomic_int32_t;
typedef _Atomic uint32_t atomic_uint32_t;
typedef _Atomic int64_t  atomic_int64_t;
typedef _Atomic uint64_t atomic_uint64_t;
typedef _Atomic int_least8_t   atomic_int_least8_t;
typedef _Atomic uint_least8_t  atomic_uint_least8_t;
typedef _Atomic int_least16_t  atomic_int_least16_t;
typedef _Atomic uint_least16_t atomic_uint_least16_t;
typedef _Atomic int_least32_t  atomic_int_least32_t;
typedef _Atomic uint_least32_t atomic_uint_least32_t;
typedef _Atomic int_least64_t  atomic_int_least64_t;
typedef _Atomic uint_least64_t atomic_uint_least64_t;
typedef _Atomic int_fast8_t    atomic_int_fast8_t;
typedef _Atomic uint_fast8_t   atomic_uint_fast8_t;
typedef _Atomic int_fast16_t   atomic_int_fast16_t;
typedef _Atomic uint_fast16_t  atomic_uint_fast16_t;
typedef _Atomic int_fast32_t   atomic_int_fast32_t;
typedef _Atomic uint_fast32_t  atomic_uint_fast32_t;
typedef _Atomic int_fast64_t   atomic_int_fast64_t;
typedef _Atomic uint_fast64_t  atomic_uint_fast64_t;
typedef _Atomic intptr_t       atomic_intptr_t;
typedef _Atomic uintptr_t      atomic_uintptr_t;
typedef _Atomic intmax_t       atomic_intmax_t;
typedef _Atomic uintmax_t      atomic_uintmax_t;

#define ATOMIC_VAR_INIT(value) (value)
#define kill_dependency(value) (value)

#ifdef __clang__
/* Clang: use __c11_atomic_* builtins which accept _Atomic typed pointers */
#define atomic_init(obj, value)      __c11_atomic_store((obj), (value), __ATOMIC_RELAXED)
#define atomic_store_explicit(obj, value, order)  __c11_atomic_store((obj), (value), (order))
#define atomic_store(obj, value)     __c11_atomic_store((obj), (value), __ATOMIC_SEQ_CST)
#define atomic_load_explicit(obj, order)          __c11_atomic_load((obj), (order))
#define atomic_load(obj)             __c11_atomic_load((obj), __ATOMIC_SEQ_CST)
#define atomic_exchange_explicit(obj, value, order) __c11_atomic_exchange((obj), (value), (order))
#define atomic_exchange(obj, value)  __c11_atomic_exchange((obj), (value), __ATOMIC_SEQ_CST)
#define atomic_fetch_add_explicit(obj, arg, order) __c11_atomic_fetch_add((obj), (arg), (order))
#define atomic_fetch_add(obj, arg)   __c11_atomic_fetch_add((obj), (arg), __ATOMIC_SEQ_CST)
#define atomic_fetch_sub_explicit(obj, arg, order) __c11_atomic_fetch_sub((obj), (arg), (order))
#define atomic_fetch_sub(obj, arg)   __c11_atomic_fetch_sub((obj), (arg), __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, succ, fail) \
    __c11_atomic_compare_exchange_strong((obj), (expected), (desired), (succ), (fail))
#define atomic_compare_exchange_strong(obj, expected, desired) \
    __c11_atomic_compare_exchange_strong((obj), (expected), (desired), __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, succ, fail) \
    __c11_atomic_compare_exchange_weak((obj), (expected), (desired), (succ), (fail))
#define atomic_compare_exchange_weak(obj, expected, desired) \
    __c11_atomic_compare_exchange_weak((obj), (expected), (desired), __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#else
/* GCC: use __atomic_* builtins */
#define atomic_init(obj, value)      __atomic_store_n((obj), (value), __ATOMIC_RELAXED)
#define atomic_store_explicit(obj, value, order)  __atomic_store_n((obj), (value), (order))
#define atomic_store(obj, value)     __atomic_store_n((obj), (value), __ATOMIC_SEQ_CST)
#define atomic_load_explicit(obj, order)          __atomic_load_n((obj), (order))
#define atomic_load(obj)             __atomic_load_n((obj), __ATOMIC_SEQ_CST)
#define atomic_exchange_explicit(obj, value, order) __atomic_exchange_n((obj), (value), (order))
#define atomic_exchange(obj, value)  __atomic_exchange_n((obj), (value), __ATOMIC_SEQ_CST)
#define atomic_fetch_add_explicit(obj, arg, order) __atomic_fetch_add((obj), (arg), (order))
#define atomic_fetch_add(obj, arg)   __atomic_fetch_add((obj), (arg), __ATOMIC_SEQ_CST)
#define atomic_fetch_sub_explicit(obj, arg, order) __atomic_fetch_sub((obj), (arg), (order))
#define atomic_fetch_sub(obj, arg)   __atomic_fetch_sub((obj), (arg), __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, succ, fail) \
    __atomic_compare_exchange_n((obj), (expected), (desired), 0, (succ), (fail))
#define atomic_compare_exchange_strong(obj, expected, desired) \
    atomic_compare_exchange_strong_explicit((obj), (expected), (desired), __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, succ, fail) \
    __atomic_compare_exchange_n((obj), (expected), (desired), 1, (succ), (fail))
#define atomic_compare_exchange_weak(obj, expected, desired) \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired), __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#endif /* __clang__ */

#ifdef __cplusplus
}
#endif
#endif
