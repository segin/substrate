/*
 * The atomic read-modify-write builtins on objects of 1, 2, 4 and 8
 * bytes, signed and unsigned, and four threads updating shared objects
 * through them.  Each was a load, an operation and a store: the values
 * came out right on one thread, and threads lost each other's updates
 * (an add between another CPU's load and store, a spinlock two CPUs
 * both took).  A `long long` on i386 needs `lock cmpxchg8b`.
 */
#include <pthread.h>
#include <stdio.h>

#define THREADS 4
#define ROUNDS 200000

static int counter;
static long long wide_counter;
static unsigned short short_counter;
static int lock;
static int guarded;

static void *worker(void *arg) {
    int i;

    (void)arg;
    for (i = 0; i < ROUNDS; ++i) {
        __sync_fetch_and_add(&counter, 1);
        __atomic_fetch_add(&wide_counter, 3, __ATOMIC_SEQ_CST);
        __sync_fetch_and_sub(&short_counter, 1);
        while (__sync_lock_test_and_set(&lock, 1) != 0) {
        }
        guarded = guarded + 1;
        __sync_lock_release(&lock);
    }
    return NULL;
}

int main(void) {
    signed char sc = -3;
    unsigned char uc = 250;
    short ss = -300;
    unsigned short us = 65530;
    int si = -7;
    unsigned ui = 0xfffffff0u;
    long long sll = -5;
    unsigned long long ull = 0xfffffffffffffff0ull;
    pthread_t t[THREADS];
    int i;

    printf("sc %d", __sync_fetch_and_add(&sc, 5));
    printf(" %d", __sync_sub_and_fetch(&sc, 10));
    printf(" %d", __atomic_exchange_n(&sc, -128, __ATOMIC_ACQUIRE));
    printf(" %d\n", sc);
    printf("uc %d", __sync_fetch_and_add(&uc, 10));
    printf(" %d", __sync_bool_compare_and_swap(&uc, 4, -1));
    printf(" %d", uc);
    printf(" %d", __sync_bool_compare_and_swap(&uc, -1, 7));
    printf(" %d\n", uc);
    printf("ss %d", __atomic_fetch_sub(&ss, 1000, __ATOMIC_RELAXED));
    printf(" %d", __sync_lock_test_and_set(&ss, 32767));
    printf(" %d\n", ss);
    printf("us %d", __sync_fetch_and_add(&us, 10));
    printf(" %d", __sync_sub_and_fetch(&us, 5));
    printf(" %d\n", us);
    printf("si %d", __sync_fetch_and_sub(&si, -10));
    printf(" %d", __sync_bool_compare_and_swap(&si, 2, 9));
    printf(" %d", __sync_bool_compare_and_swap(&si, 3, 9));
    printf(" %d\n", si);
    printf("ui %u", __atomic_fetch_add(&ui, 0x20, __ATOMIC_SEQ_CST));
    printf(" %u", __sync_lock_test_and_set(&ui, 1u));
    printf(" %u\n", ui);
    printf("sll %lld", __sync_fetch_and_add(&sll, 0x100000000ll));
    printf(" %lld", __sync_sub_and_fetch(&sll, 1));
    printf(" %d", __sync_bool_compare_and_swap(&sll, 0xfffffffall, 1));
    printf(" %lld", __atomic_exchange_n(&sll, -1, __ATOMIC_SEQ_CST));
    printf(" %lld\n", sll);
    printf("ull %llu", __atomic_fetch_sub(&ull, 0xffffffff0ull, __ATOMIC_SEQ_CST));
    printf(" %d", __sync_bool_compare_and_swap(&ull, 0xfffffff000000000ull, 5));
    printf(" %llu\n", ull);

    for (i = 0; i < THREADS; ++i) {
        if (pthread_create(&t[i], NULL, worker, NULL) != 0) {
            printf("pthread_create failed\n");
            return 1;
        }
    }
    for (i = 0; i < THREADS; ++i) {
        pthread_join(t[i], NULL);
    }
    printf("threads %d %lld %u %d\n", counter, wide_counter, short_counter, guarded);
    return 0;
}
