/*
 * gcc's own <stdatomic.h>, which is written in its generic __atomic
 * builtins (values through pointers), __auto_type, `__typeof__ ((void)0,
 * *p)` to drop _Atomic, an _Atomic structure for atomic_flag, and the
 * __CHAR16_TYPE__ and __GCC_ATOMIC_*_LOCK_FREE macros.  Two threads count
 * through atomic_fetch_add, a compare-exchange loop and an atomic_flag
 * spinlock; the values each operation gives are printed too.
 * run_stdatomic_gcc.sh builds it with gcc and with cc, the same flags
 * for both, and the two must print the same.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#define ROUNDS 200000

static atomic_int hits;
static atomic_llong big = 0x0123456789abcdefLL;
static atomic_uint mask;
static atomic_flag lock = ATOMIC_FLAG_INIT;
static long locked_count;
static _Atomic(double) total;

static void *work(void *arg) {
    int i;

    (void)arg;
    for (i = 0; i < ROUNDS; ++i) {
        long long seen = atomic_load(&big);
        double t = atomic_load_explicit(&total, memory_order_relaxed);

        atomic_fetch_add(&hits, 1);
        atomic_fetch_or_explicit(&mask, 1u << (i & 31), memory_order_relaxed);
        while (!atomic_compare_exchange_weak(&big, &seen, seen + 1)) {
        }
        while (!atomic_compare_exchange_strong(&total, &t, t + 0.5)) {
        }
        while (atomic_flag_test_and_set_explicit(&lock, memory_order_acquire)) {
        }
        ++locked_count;
        atomic_flag_clear_explicit(&lock, memory_order_release);
    }
    return NULL;
}

int main(void) {
    pthread_t t;
    atomic_int v;
    int expected = 4;
    atomic_char16_t c16 = 7;
    atomic_char32_t c32 = 9;

    int r[4];

    /* one operation per statement: the order of a call's arguments is the compiler's */
    atomic_init(&v, 3);
    r[0] = atomic_is_lock_free(&big);
    printf("lock-free %d %d %d %d %d %d\n", ATOMIC_BOOL_LOCK_FREE, ATOMIC_CHAR_LOCK_FREE, ATOMIC_INT_LOCK_FREE,
           ATOMIC_LLONG_LOCK_FREE, ATOMIC_POINTER_LOCK_FREE, r[0]);
    r[0] = atomic_exchange(&v, 4);
    r[1] = atomic_load(&v);
    printf("%d %d %u %u\n", r[0], r[1], (unsigned)atomic_load(&c16), (unsigned)atomic_load(&c32));
    r[0] = atomic_compare_exchange_strong(&v, &expected, 10);
    printf("%d %d ", r[0], expected);
    expected = 99;
    r[0] = atomic_compare_exchange_strong(&v, &expected, 11);
    printf("%d %d %d\n", r[0], expected, atomic_load(&v));
    atomic_store(&v, -5);
    r[0] = atomic_fetch_sub(&v, 2);
    r[1] = atomic_fetch_xor(&v, 0xf);
    r[2] = atomic_fetch_and(&v, 0x3c);
    r[3] = atomic_load_explicit(&v, memory_order_acquire);
    printf("%d %d %d %d\n", r[0], r[1], r[2], r[3]);
    r[0] = atomic_flag_test_and_set(&lock);
    r[1] = atomic_flag_test_and_set(&lock);
    printf("%d %d\n", r[0], r[1]);
    atomic_flag_clear(&lock);
    atomic_thread_fence(memory_order_seq_cst);
    atomic_signal_fence(memory_order_seq_cst);
    printf("%lld\n", (long long)kill_dependency(atomic_load(&big)));

    if (pthread_create(&t, NULL, work, NULL) != 0) {
        return 2;
    }
    work(NULL);
    pthread_join(t, NULL);
    printf("hits %d big %llx mask %x locked %ld total %g\n", atomic_load(&hits), (long long)atomic_load(&big),
           atomic_load(&mask), locked_count, atomic_load(&total));
    return 0;
}
