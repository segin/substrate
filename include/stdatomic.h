#ifndef _STDATOMIC_H_
#define _STDATOMIC_H_

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
typedef _Atomic long long atomic_llong;
typedef _Atomic unsigned long long atomic_ullong;
typedef void * _Atomic atomic_ptr;

#define ATOMIC_VAR_INIT(value) (value)
#define kill_dependency(value) (value)

#define atomic_init(obj, value) __atomic_store_n((obj), (value), __ATOMIC_RELAXED)
#define atomic_store_explicit(obj, value, order) __atomic_store_n((obj), (value), (order))
#define atomic_store(obj, value) __atomic_store_n((obj), (value), __ATOMIC_SEQ_CST)
#define atomic_load_explicit(obj, order) __atomic_load_n((obj), (order))
#define atomic_load(obj) __atomic_load_n((obj), __ATOMIC_SEQ_CST)
#define atomic_exchange_explicit(obj, value, order) __atomic_exchange_n((obj), (value), (order))
#define atomic_exchange(obj, value) __atomic_exchange_n((obj), (value), __ATOMIC_SEQ_CST)
#define atomic_fetch_add_explicit(obj, arg, order) __atomic_fetch_add((obj), (arg), (order))
#define atomic_fetch_add(obj, arg) __atomic_fetch_add((obj), (arg), __ATOMIC_SEQ_CST)
#define atomic_fetch_sub_explicit(obj, arg, order) __atomic_fetch_sub((obj), (arg), (order))
#define atomic_fetch_sub(obj, arg) __atomic_fetch_sub((obj), (arg), __ATOMIC_SEQ_CST)

#define atomic_compare_exchange_strong_explicit(obj, expected, desired, succ, fail) \
    __atomic_compare_exchange_n((obj), (expected), (desired), 0, (succ), (fail))
#define atomic_compare_exchange_strong(obj, expected, desired) \
    atomic_compare_exchange_strong_explicit((obj), (expected), (desired), __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

#define atomic_compare_exchange_weak_explicit(obj, expected, desired, succ, fail) \
    __atomic_compare_exchange_n((obj), (expected), (desired), 1, (succ), (fail))
#define atomic_compare_exchange_weak(obj, expected, desired) \
    atomic_compare_exchange_weak_explicit((obj), (expected), (desired), __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

#endif
