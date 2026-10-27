/*
 * The atomic builtins cc did not know: and, or, xor and nand, in both
 * their fetch-then-op and op-then-fetch forms, __sync_add_and_fetch,
 * __atomic_add_fetch/__atomic_sub_fetch and __sync_val_compare_and_swap,
 * on objects of 1, 2, 4 and 8 bytes.  Each was an implicit declaration
 * in a GNU mode and an error in an ISO one (the Substrate kernel's
 * spinlocks and reference counts, -std=c2x).  Four threads then set
 * and clear bits of their own in shared words: a bit set or cleared
 * by a plain load and store loses another thread's.
 */
#include <pthread.h>
#include <stdio.h>

#define THREADS 4
#define ROUNDS 100000

static unsigned flags;
static unsigned long long wide_flags;
static int sum;

static void *worker(void *arg) {
    unsigned bit = 1u << (unsigned)(long)arg;
    unsigned long long wbit = 1ull << (40 + (unsigned)(long)arg);
    int i;

    for (i = 0; i < ROUNDS; ++i) {
        __sync_fetch_and_or(&flags, bit);
        __atomic_fetch_and(&flags, ~bit, __ATOMIC_SEQ_CST);
        __sync_or_and_fetch(&flags, bit << 8);
        __atomic_xor_fetch(&wide_flags, wbit, __ATOMIC_ACQ_REL);
        __sync_add_and_fetch(&sum, 2);
        __atomic_sub_fetch(&sum, 1, __ATOMIC_RELAXED);
    }
    return NULL;
}

int main(void) {
    signed char sc = 0x5a;
    unsigned char uc = 0xf0;
    short ss = -2;
    unsigned short us = 0x00ff;
    int si = 6;
    unsigned ui = 0xffff0000u;
    long long sll = -1;
    unsigned long long ull = 0x00000000ffffffffull;
    pthread_t t[THREADS];
    long i;

    printf("sc %d", __sync_fetch_and_and(&sc, 0x0f));
    printf(" %d", __sync_or_and_fetch(&sc, -128));
    printf(" %d", __atomic_fetch_xor(&sc, 0x7f, __ATOMIC_SEQ_CST));
    printf(" %d\n", sc);
    printf("uc %d", __sync_fetch_and_nand(&uc, 0x3c));
    printf(" %d", __atomic_nand_fetch(&uc, 0xff, __ATOMIC_SEQ_CST));
    printf(" %d", __sync_val_compare_and_swap(&uc, 0x30, 0x99));
    printf(" %d", __sync_val_compare_and_swap(&uc, 0x99, 0x11));
    printf(" %d\n", uc);
    printf("ss %d", __sync_fetch_and_xor(&ss, 0x7fff));
    printf(" %d", __atomic_and_fetch(&ss, 0x00f0, __ATOMIC_SEQ_CST));
    printf(" %d", __sync_add_and_fetch(&ss, -300));
    printf(" %d\n", ss);
    printf("us %d", __atomic_fetch_or(&us, 0xff00, __ATOMIC_RELAXED));
    printf(" %d", __sync_xor_and_fetch(&us, 0x0f0f));
    printf(" %d", __sync_val_compare_and_swap(&us, 0xf0f0, 7));
    printf(" %d\n", us);
    printf("si %d", __atomic_add_fetch(&si, 10, __ATOMIC_SEQ_CST));
    printf(" %d", __atomic_sub_fetch(&si, 20, __ATOMIC_SEQ_CST));
    printf(" %d", __sync_nand_and_fetch(&si, 5));
    printf(" %d", __sync_val_compare_and_swap(&si, -1, 12));
    printf(" %d\n", si);
    printf("ui %u", __atomic_fetch_nand(&ui, 0x0ff00ff0u, __ATOMIC_SEQ_CST));
    printf(" %u", __atomic_or_fetch(&ui, 1u, __ATOMIC_SEQ_CST));
    printf(" %u\n", ui);
    printf("sll %lld", __sync_fetch_and_and(&sll, 0x123456789ll));
    printf(" %lld", __atomic_xor_fetch(&sll, -1, __ATOMIC_SEQ_CST));
    printf(" %lld", __sync_val_compare_and_swap(&sll, ~0x123456789ll, 1ll << 40));
    printf(" %lld\n", sll);
    printf("ull %llu", __sync_fetch_and_or(&ull, 0xff00000000000000ull));
    printf(" %llu", __atomic_nand_fetch(&ull, 0x8000000000000001ull, __ATOMIC_SEQ_CST));
    printf(" %llu", __sync_add_and_fetch(&ull, 3));
    printf(" %llu\n", ull);

    for (i = 0; i < THREADS; ++i) {
        if (pthread_create(&t[i], NULL, worker, (void *)i) != 0) {
            printf("pthread_create failed\n");
            return 1;
        }
    }
    for (i = 0; i < THREADS; ++i) {
        pthread_join(t[i], NULL);
    }
    printf("threads %#x %#llx %d\n", flags, wide_flags, sum);
    return 0;
}
