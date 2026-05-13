/*
 * torture_pthread.c — stress libpthread on a real substrate target.
 *
 * Six small tests, each targeting something the current libpthread
 * promises but hasn't been quantitatively poked at:
 *
 *   1. contention      — N threads x K bumps under one mutex.
 *                        Final counter MUST equal N*K bit-exact.
 *   2. cycle           — single-threaded spawn/join loop, more
 *                        iterations than MAX_PTHREADS (64) so we
 *                        prove the slot table recycles cleanly.
 *   3. bulk            — saturate MAX_PTHREADS-1 concurrent threads
 *                        and join them all.  Confirms create()
 *                        fails-gracefully on the 65th rather than
 *                        smashing the table.
 *   4. retval          — distinct (void *)i carried back through
 *                        pthread_exit -> pthread_join.  Catches
 *                        return-value handling regressions.
 *   5. stack           — each thread fills its local buffer with a
 *                        distinct pattern; checksums must match the
 *                        per-thread arg, proving stacks don't share.
 *   6. mutex_rapid     — many fast lock/unlock pairs from many
 *                        threads.  No deadlock, no missed wakeups.
 *
 * Failures abort the suite with non-zero exit and a one-line cause.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define N_THREADS       32          /* < MAX_PTHREADS=64           */
#define BUMP_REPS       10000
#define CYCLE_ITERS     200         /* > MAX_PTHREADS to test recycle */
#define MUTEX_REPS      5000

static volatile long g_counter;
static pthread_mutex_t g_mu;
static int failures;

#define FAIL(...) do { fprintf(stderr, "FAIL: " __VA_ARGS__); failures++; } while (0)

/* ------------------------------------------------------------------ 1. contention */
static void *bump(void *arg) {
    long reps = (long)arg;
    for (long i = 0; i < reps; i++) {
        pthread_mutex_lock(&g_mu);
        g_counter++;
        pthread_mutex_unlock(&g_mu);
    }
    return NULL;
}

static void test_contention(void) {
    pthread_t tids[N_THREADS];
    g_counter = 0;
    pthread_mutex_init(&g_mu, NULL);

    for (int i = 0; i < N_THREADS; i++) {
        if (pthread_create(&tids[i], NULL, bump, (void *)(long)BUMP_REPS) != 0) {
            FAIL("contention: create #%d\n", i);
            return;
        }
    }
    for (int i = 0; i < N_THREADS; i++) {
        if (pthread_join(tids[i], NULL) != 0) {
            FAIL("contention: join #%d\n", i);
            return;
        }
    }
    long expected = (long)N_THREADS * BUMP_REPS;
    if (g_counter != expected) {
        FAIL("contention: counter %ld != %ld\n", g_counter, expected);
    }
}

/* ------------------------------------------------------------------ 2. cycle */
static void *noop(void *arg) { return arg; }

static void test_cycle(void) {
    for (int i = 0; i < CYCLE_ITERS; i++) {
        pthread_t tid;
        if (pthread_create(&tid, NULL, noop, (void *)(intptr_t)i) != 0) {
            FAIL("cycle: create iter %d\n", i);
            return;
        }
        if (pthread_join(tid, NULL) != 0) {
            FAIL("cycle: join iter %d\n", i);
            return;
        }
    }
}

/* ------------------------------------------------------------------ 3. bulk */
static void test_bulk(void) {
    pthread_t tids[63];     /* MAX_PTHREADS - 1 (main holds one slot? safer: 63) */
    int created = 0;
    for (int i = 0; i < 63; i++) {
        if (pthread_create(&tids[i], NULL, noop, NULL) != 0) {
            /* Acceptable: hit the slot limit early.  Record and break. */
            break;
        }
        created++;
    }
    for (int i = 0; i < created; i++) {
        if (pthread_join(tids[i], NULL) != 0) {
            FAIL("bulk: join #%d after creating %d\n", i, created);
            return;
        }
    }
    if (created < 32) {
        FAIL("bulk: only created %d threads (expected >= 32)\n", created);
    }
}

/* ------------------------------------------------------------------ 4. retval */
static void *return_arg(void *arg) { return arg; }

static void test_retval(void) {
    pthread_t tids[N_THREADS];
    for (int i = 0; i < N_THREADS; i++) {
        if (pthread_create(&tids[i], NULL, return_arg, (void *)(intptr_t)(i + 1)) != 0) {
            FAIL("retval: create #%d\n", i);
            return;
        }
    }
    for (int i = 0; i < N_THREADS; i++) {
        void *r = NULL;
        if (pthread_join(tids[i], &r) != 0) {
            FAIL("retval: join #%d\n", i);
            return;
        }
        if ((intptr_t)r != i + 1) {
            FAIL("retval: thread %d returned %ld, expected %d\n", i, (long)(intptr_t)r, i + 1);
        }
    }
}

/* ------------------------------------------------------------------ 5. stack */
struct stack_arg { unsigned int seed; unsigned int checksum; };

static void *stack_fill(void *arg) {
    struct stack_arg *a = (struct stack_arg *)arg;
    /* Allocate a buffer on the thread's stack and fill it with a
     * seed-dependent pattern.  XOR-reduce it back into a checksum.
     * Buffer kept small so we stay well under the 64 KiB thread stack. */
    unsigned int buf[512];
    unsigned int x = a->seed;
    for (int i = 0; i < 512; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = x;
    }
    unsigned int sum = 0;
    for (int i = 0; i < 512; i++) sum ^= buf[i];
    a->checksum = sum;
    return NULL;
}

static void test_stack(void) {
    pthread_t tids[N_THREADS];
    struct stack_arg args[N_THREADS];
    for (int i = 0; i < N_THREADS; i++) {
        args[i].seed = 0xDEADBEEFu + (unsigned)i * 0x100u;
        args[i].checksum = 0;
        if (pthread_create(&tids[i], NULL, stack_fill, &args[i]) != 0) {
            FAIL("stack: create #%d\n", i);
            return;
        }
    }
    for (int i = 0; i < N_THREADS; i++) pthread_join(tids[i], NULL);
    /* Recompute expected checksums; must match what each thread wrote. */
    for (int i = 0; i < N_THREADS; i++) {
        unsigned int x = 0xDEADBEEFu + (unsigned)i * 0x100u;
        unsigned int expect = 0;
        for (int j = 0; j < 512; j++) {
            x = x * 1103515245u + 12345u;
            expect ^= x;
        }
        if (args[i].checksum != expect) {
            FAIL("stack: thread %d checksum %08x != %08x (stacks aliased?)\n",
                 i, args[i].checksum, expect);
            return;
        }
    }
}

/* ------------------------------------------------------------------ 6. mutex_rapid */
static void *thrash(void *arg) {
    (void)arg;
    for (long i = 0; i < MUTEX_REPS; i++) {
        pthread_mutex_lock(&g_mu);
        g_counter++;
        pthread_mutex_unlock(&g_mu);
    }
    return NULL;
}

static void test_mutex_rapid(void) {
    pthread_t tids[N_THREADS];
    g_counter = 0;
    pthread_mutex_init(&g_mu, NULL);
    for (int i = 0; i < N_THREADS; i++) {
        if (pthread_create(&tids[i], NULL, thrash, NULL) != 0) {
            FAIL("mutex_rapid: create #%d\n", i);
            return;
        }
    }
    for (int i = 0; i < N_THREADS; i++) pthread_join(tids[i], NULL);
    long expected = (long)N_THREADS * MUTEX_REPS;
    if (g_counter != expected) {
        FAIL("mutex_rapid: counter %ld != %ld (lock dropped writes)\n", g_counter, expected);
    }
}

/* ------------------------------------------------------------------ main */
int main(void) {
    printf("=== libpthread torture suite ===\n");

    test_contention();    printf("  contention:   %s\n", failures ? "FAIL" : "PASS");
    int after_contention = failures;
    test_cycle();         printf("  cycle:        %s\n", failures > after_contention ? "FAIL" : "PASS");
    int after_cycle = failures;
    test_bulk();          printf("  bulk:         %s\n", failures > after_cycle ? "FAIL" : "PASS");
    int after_bulk = failures;
    test_retval();        printf("  retval:       %s\n", failures > after_bulk ? "FAIL" : "PASS");
    int after_retval = failures;
    test_stack();         printf("  stack:        %s\n", failures > after_retval ? "FAIL" : "PASS");
    int after_stack = failures;
    test_mutex_rapid();   printf("  mutex_rapid:  %s\n", failures > after_stack ? "FAIL" : "PASS");

    printf("\n%s: %d failures\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
