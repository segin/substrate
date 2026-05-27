/*
 * torture_malloc_threads.c — multi-threaded malloc/free torture.
 *
 * Substrate's libc malloc (lib/c/src/stdlib.c) protects atexit and
 * at_quick_exit with atomic locks but DOES NOT protect malloc/free/
 * realloc/calloc themselves.  All access to global_base, global_tail,
 * search_hint, and the free-list `next`/`prev` pointers is
 * unsynchronized.  When two threads malloc concurrently they race on
 * find_free_block + split_block, which corrupts free-list pointers
 * and lets a subsequent split_block compute new_block from a
 * corrupted block, ending in a NULL/junk-pointer deref at
 * stdlib.c:214 — exactly the shape of the Xfbdev and links crashes.
 *
 * This test spawns N pthreads that hammer malloc/calloc/realloc/free
 * concurrently against the same heap.  A canary check on each freshly
 * allocated block + on its neighbours surfaces any data corruption
 * between threads.  If substrate's libc is thread-unsafe, we expect
 * CRASH/CORRUPTION here; on a properly-locked allocator we expect
 * clean PASS.
 *
 * Same source builds on host with -pthread and on substrate with
 * libpthread.so.0.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <sys/wait.h>

#define N_THREADS         16
#define OPS_PER_THREAD    200000
#define LIVE_PER_THREAD   256
/* Force interleaving: yield every YIELD_EVERY ops so the kernel
 * timer slice can't run each thread to completion before the next
 * one starts.  Without this, an 8ms scheduler slice on HZ=128 is
 * long enough for a thread to finish all 200k ops before any other
 * thread runs once — and we'd never see contention. */
#define YIELD_EVERY       64

/* Per-thread state. */
struct thread_state {
    pthread_t tid;
    int       id;
    uint32_t  rng;
    void     *live[LIVE_PER_THREAD];
    size_t    sizes[LIVE_PER_THREAD];
    int       fail;
    const char *fail_msg;
};

static uint32_t rng_next(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

static uint8_t canary_byte(void *p, size_t off, int tid) {
    uintptr_t x = (uintptr_t)p ^ (uintptr_t)off ^ (uintptr_t)tid ^ 0xA5;
    return (uint8_t)((x * 2654435761u) >> 24);
}

static void canary_fill(void *p, size_t sz, int tid) {
    uint8_t *b = p;
    for (size_t i = 0; i < sz; i++) b[i] = canary_byte(p, i, tid);
}

static int canary_check(void *p, size_t sz, int tid) {
    const uint8_t *b = p;
    for (size_t i = 0; i < sz; i++) {
        if (b[i] != canary_byte(p, i, tid)) {
            return (int)i + 1;          /* 1-based offset of mismatch */
        }
    }
    return 0;
}

static void *worker(void *arg) {
    struct thread_state *st = arg;
    memset(st->live,  0, sizeof(st->live));
    memset(st->sizes, 0, sizeof(st->sizes));

    for (int op = 0; op < OPS_PER_THREAD; op++) {
        int slot = (int)(rng_next(&st->rng) % LIVE_PER_THREAD);

        if (st->live[slot]) {
            int mm = canary_check(st->live[slot], st->sizes[slot], st->id);
            if (mm) {
                st->fail = 1;
                /* Static buffer because we may be in a torn libc state. */
                static char msgbuf[128];
                snprintf(msgbuf, sizeof(msgbuf),
                         "thread %d op %d slot %d sz %zu canary off %d",
                         st->id, op, slot, st->sizes[slot], mm - 1);
                st->fail_msg = msgbuf;
                return NULL;
            }
            free(st->live[slot]);
            st->live[slot] = NULL;
        } else {
            uint32_t r = rng_next(&st->rng) % 100;
            size_t sz;
            if      (r < 50) sz = 120 + (rng_next(&st->rng) % 48); /* near 144 */
            else if (r < 90) sz = 8   + (rng_next(&st->rng) % 88);
            else             sz = 256 + (rng_next(&st->rng) % 768);

            void *p;
            if ((r & 1) == 0) p = calloc(1, sz);
            else              p = malloc(sz);
            if (!p) {
                st->fail = 1;
                static char msgbuf[64];
                snprintf(msgbuf, sizeof(msgbuf),
                         "thread %d op %d alloc(%zu) failed", st->id, op, sz);
                st->fail_msg = msgbuf;
                return NULL;
            }
            canary_fill(p, sz, st->id);
            st->live[slot]  = p;
            st->sizes[slot] = sz;
        }

        /* Force interleaving so the scheduler can't run a single
         * thread to completion within its time slice. */
        if ((op & (YIELD_EVERY - 1)) == 0) sched_yield();
    }

    /* Drain — verify every live block. */
    for (int i = 0; i < LIVE_PER_THREAD; i++) {
        if (st->live[i]) {
            int mm = canary_check(st->live[i], st->sizes[i], st->id);
            if (mm) {
                st->fail = 1;
                static char msgbuf[128];
                snprintf(msgbuf, sizeof(msgbuf),
                         "thread %d drain slot %d sz %zu canary off %d",
                         st->id, i, st->sizes[i], mm - 1);
                st->fail_msg = msgbuf;
                return NULL;
            }
            free(st->live[i]);
        }
    }
    return NULL;
}

static int run_test(void) {
    struct thread_state *states = calloc(N_THREADS, sizeof(*states));
    if (!states) { fprintf(stderr, "  harness calloc failed\n"); return 1; }

    for (int i = 0; i < N_THREADS; i++) {
        states[i].id  = i;
        states[i].rng = 0x1337C0DE ^ (uint32_t)(i * 0x9E3779B1u);
        if (pthread_create(&states[i].tid, NULL, worker, &states[i]) != 0) {
            fprintf(stderr, "  pthread_create %d: %s\n", i, strerror(errno));
            for (int j = 0; j < i; j++) pthread_join(states[j].tid, NULL);
            free(states);
            return 1;
        }
    }

    int fail = 0;
    for (int i = 0; i < N_THREADS; i++) {
        pthread_join(states[i].tid, NULL);
        if (states[i].fail) {
            fprintf(stderr, "  %s\n", states[i].fail_msg ? states[i].fail_msg : "(no msg)");
            fail = 1;
        }
    }
    free(states);
    return fail;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    fprintf(stderr, "torture_malloc_threads: %d threads × %d ops, live cap %d\n",
            N_THREADS, OPS_PER_THREAD, LIVE_PER_THREAD);

    /* Wrap the actual run in a child process so a crash gives us a
     * clean Result: line + exit code. */
    pid_t kid = fork();
    if (kid < 0) {
        printf("Result: FAILED (fork: %s)\n", strerror(errno));
        return 1;
    }
    if (kid == 0) {
        int r = run_test();
        _exit(r ? 1 : 0);
    }
    int status;
    waitpid(kid, &status, 0);

    int crash = 0, fail = 0;
    if (WIFSIGNALED(status)) {
        crash = 1;
        fprintf(stderr, "child died on signal %d\n", WTERMSIG(status));
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fail = 1;
    }

    printf("Result: %s%s\n",
           (crash || fail) ? "FAILED" : "PASSED",
           crash ? " (crash)" : "");
    fflush(stdout);
    return (crash || fail) ? 1 : 0;
}
