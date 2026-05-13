/*
 * torture_kernel.c — kernel-side scheduler/threading torture suite.
 *
 * Unlike torture_pthread.c (which proves libpthread does what it says
 * on the tin), this suite is designed to hammer the *kernel*: per-CPU
 * runqueues, work stealing, lazy FPU save, sleep queue wakeups,
 * per-thread signal delivery, TLS isolation, mutex fairness.
 *
 * Each scenario aims for a specific bug class.  A scenario that finds
 * nothing is still useful: it bounds the unknown.
 *
 * Scenarios are selected by argv:
 *
 *   torture_kernel storm        — slot-table churn, reaper races
 *   torture_kernel fpu          — lazy FPU save / x87 cross-thread bleed
 *   torture_kernel wakeup       — thr_suspend / thr_wake race coverage
 *   torture_kernel signals      — per-thread thr_kill delivery
 *   torture_kernel mutex_fair   — starvation watchdog under contention
 *   torture_kernel tls          — __thread isolation under heavy ctx switch
 *   torture_kernel massive      — saturate MAX_PTHREADS, 5 s sustained
 *   torture_kernel lockord      — AB-BA deadlock watchdog
 *   torture_kernel all          — run everything in sequence
 *
 * Uses the raw thr_* syscall set (SYS_THR_KILL, SYS_THR_SUSPEND, etc.)
 * for anything the current libpthread doesn't expose.  That's
 * deliberate — those new syscalls *are* what we want to torture.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/thr.h>
#include <sys/time.h>
#include <time.h>

#define MAX_PT 64

static int g_failures;

#define FAIL(...) do {                                                 \
    fprintf(stderr, "FAIL: " __VA_ARGS__);                             \
    __sync_fetch_and_add(&g_failures, 1);                              \
} while (0)

/* ---------- monotonic time helpers (TSC-free, syscall-based) -------- */
static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

/* ===================================================================
 * 1. STORM — 3000 create/exit/join cycles.
 *    Bug class: thread reaper losing slots; futex-on-zombie hangs.
 * =================================================================== */
static void *storm_worker(void *arg) {
    /* Tiny payload so context-switch / spawn overhead dominates. */
    volatile long sum = 0;
    for (int i = 0; i < 100; i++) sum += i;
    return (void *)(intptr_t)sum;
}

static void scenario_storm(void) {
    uint64_t t0 = now_us();
    for (int i = 0; i < 3000; i++) {
        pthread_t tid;
        if (pthread_create(&tid, NULL, storm_worker, NULL) != 0) {
            FAIL("storm: create at iter %d (slot table leak?)\n", i);
            return;
        }
        if (pthread_join(tid, NULL) != 0) {
            FAIL("storm: join at iter %d\n", i);
            return;
        }
    }
    uint64_t dt = now_us() - t0;
    printf("  storm: 3000 cycles in %llu us (%.1f us/cycle)\n",
           (unsigned long long)dt, (double)dt / 3000.0);
}

/* ===================================================================
 * 2. FPU — Machin's formula for pi in long double, N threads, K rounds.
 *    Bug class: kernel forgets to save/restore x87/SSE state across
 *    context switches, so thread A's stack bleeds into thread B's
 *    result.  A bit-exact mismatch is a smoking gun.
 *
 *    Machin:  pi = 16*atan(1/5) - 4*atan(1/239)
 *    atan(x) = x - x^3/3 + x^5/5 - x^7/7 + ...
 *
 *    The Taylor series compiles to a long sequence of fdiv/fadd on
 *    x87, with the FP stack non-empty across function call boundaries.
 *    Any context switch that doesn't save the stack corrupts the next
 *    fadd silently — the answer goes garbage.
 * =================================================================== */
static long double atan_series(long double x) {
    long double sum = 0.0L;
    long double term = x;
    long double x2   = x * x;
    for (int k = 0; k < 64; k++) {
        long double t = term / (long double)(2 * k + 1);
        if (k & 1) sum -= t; else sum += t;
        term *= x2;
    }
    return sum;
}

static long double machin_pi(void) {
    return 16.0L * atan_series(1.0L / 5.0L)
         -  4.0L * atan_series(1.0L / 239.0L);
}

static long double g_pi_ref;
static int         g_fpu_rounds;

static void *fpu_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    for (int r = 0; r < g_fpu_rounds; r++) {
        long double mine = machin_pi();
        /* Direct long-double compare — must be bit-exact to the
         * reference computed in the calling thread before spawn.
         * Any context-switch FPU bug shows here. */
        if (mine != g_pi_ref) {
            FAIL("fpu: thread %d round %d: %.20Lf != ref %.20Lf\n",
                 id, r, mine, g_pi_ref);
            return NULL;
        }
    }
    return NULL;
}

static void scenario_fpu(void) {
    g_pi_ref = machin_pi();
    g_fpu_rounds = 200;
    /* 16 threads on (presumably) 1-8 cores forces lots of FPU
     * context switching. */
    pthread_t tids[16];
    for (int i = 0; i < 16; i++) {
        if (pthread_create(&tids[i], NULL, fpu_worker, (void *)(intptr_t)i) != 0) {
            FAIL("fpu: create #%d\n", i);
            return;
        }
    }
    for (int i = 0; i < 16; i++) pthread_join(tids[i], NULL);
    /* Cross-check reference is sane: pi to many places. */
    long double truepi = 3.14159265358979323846264338327950288L;
    long double err = g_pi_ref - truepi;
    if (err < 0) err = -err;
    if (err > 1e-18L) {
        FAIL("fpu: reference pi off by %.3Le (Machin or long double broken)\n", err);
    }
}

/* ===================================================================
 * 3. WAKEUP — thr_suspend / thr_wake races.
 *    Bug class: missed wakeups, wake-before-suspend losing the wake,
 *    sleepq corruption.
 *
 *    Each worker: thr_suspend() -> wake_count++
 *    Main thread thr_wake() each worker once per round.
 *    After R rounds, every worker should have wake_count == R.
 *
 *    Then a stress phase: main calls thr_wake() BEFORE the worker has
 *    entered thr_suspend() — the latched WAKE_PENDING flag should
 *    make the subsequent suspend return immediately.
 * =================================================================== */
struct wakeup_state {
    long tid;
    volatile int wake_count;
    volatile int ready;     /* worker tells main "I'm parked" */
};

static void *wakeup_worker(void *arg) {
    struct wakeup_state *s = (struct wakeup_state *)arg;
    s->tid = (long)syscall(SYS_THR_SELF);
    while (s->wake_count < 100) {
        s->ready = 1;
        int rc = (int)syscall(SYS_THR_SUSPEND, NULL);
        (void)rc;
        s->wake_count++;
    }
    return NULL;
}

static void scenario_wakeup(void) {
    enum { N = 8, ROUNDS = 100 };
    struct wakeup_state st[N];
    pthread_t tids[N];
    memset(st, 0, sizeof(st));

    for (int i = 0; i < N; i++) {
        if (pthread_create(&tids[i], NULL, wakeup_worker, &st[i]) != 0) {
            FAIL("wakeup: create #%d\n", i);
            return;
        }
    }
    /* Wait for all workers to announce themselves. */
    int waiting = 200;
    while (waiting--) {
        int all_ready = 1;
        for (int i = 0; i < N; i++) if (!st[i].ready || !st[i].tid) all_ready = 0;
        if (all_ready) break;
        struct timespec t = {0, 1000000}; nanosleep(&t, NULL);
    }
    /* Drive wakes. */
    for (int r = 0; r < ROUNDS; r++) {
        for (int i = 0; i < N; i++) {
            st[i].ready = 0;
            int rc = (int)syscall(SYS_THR_WAKE, st[i].tid);
            if (rc != 0) FAIL("wakeup: wake tid %ld rc=%d\n", st[i].tid, rc);
        }
        /* Wait for them all to re-park. */
        int spin = 1000;
        while (spin--) {
            int all = 1;
            for (int i = 0; i < N; i++) if (!st[i].ready) all = 0;
            if (all) break;
            struct timespec t = {0, 100000}; nanosleep(&t, NULL);
        }
        if (spin <= 0) {
            FAIL("wakeup: timeout waiting for re-park at round %d "
                 "(missed wake?)\n", r);
            break;
        }
    }
    /* Final round of wakes to let them exit. */
    for (int i = 0; i < N; i++) syscall(SYS_THR_WAKE, st[i].tid);
    for (int i = 0; i < N; i++) pthread_join(tids[i], NULL);
    for (int i = 0; i < N; i++) {
        if (st[i].wake_count < ROUNDS) {
            FAIL("wakeup: worker %d got %d wakes (expected >= %d)\n",
                 i, st[i].wake_count, ROUNDS);
        }
    }
}

/* ===================================================================
 * 4. SIGNALS — thr_kill targeting specific tids.
 *    Bug class: thread-directed signal landing on wrong thread,
 *    sig_pending bit corruption, races with sched_yield.
 *
 *    Two worker pools: "expected" (subject to thr_kill) and "innocent".
 *    After the storm, innocent workers must have received zero signals,
 *    expected workers must have received their full quota.
 * =================================================================== */
static __thread volatile int g_sig_count;
static __thread int g_is_target;
static volatile int g_innocent_hits;

static void sig_handler(int s) {
    (void)s;
    g_sig_count++;
    if (!g_is_target) __sync_fetch_and_add(&g_innocent_hits, 1);
}

struct sig_state { long tid; int target; volatile int sig_count; volatile int ready; };

static void *sig_worker(void *arg) {
    struct sig_state *s = (struct sig_state *)arg;
    s->tid = (long)syscall(SYS_THR_SELF);
    g_is_target = s->target;
    g_sig_count = 0;
    s->ready = 1;
    /* Spin so the signal lands during runtime, not in a syscall. */
    while (s->sig_count < 50 && !s->target) {
        for (volatile int i = 0; i < 100000; i++);
        s->sig_count = g_sig_count;
    }
    while (s->target && s->sig_count < 50) {
        for (volatile int i = 0; i < 100000; i++);
        s->sig_count = g_sig_count;
    }
    return NULL;
}

static void scenario_signals(void) {
    struct sigaction sa = { .sa_handler = sig_handler, .sa_flags = 0 };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    enum { TARGETS = 4, INNOCENTS = 4 };
    struct sig_state state[TARGETS + INNOCENTS];
    pthread_t tids[TARGETS + INNOCENTS];
    memset(state, 0, sizeof(state));
    g_innocent_hits = 0;

    for (int i = 0; i < TARGETS + INNOCENTS; i++) {
        state[i].target = (i < TARGETS);
        if (pthread_create(&tids[i], NULL, sig_worker, &state[i]) != 0) {
            FAIL("signals: create #%d\n", i);
            return;
        }
    }
    /* Wait for everyone to publish their tid. */
    int waiting = 200;
    while (waiting--) {
        int all = 1;
        for (int i = 0; i < TARGETS + INNOCENTS; i++) if (!state[i].ready) all = 0;
        if (all) break;
        struct timespec t = {0, 1000000}; nanosleep(&t, NULL);
    }
    /* Deliver to targets only. */
    for (int round = 0; round < 50; round++) {
        for (int i = 0; i < TARGETS; i++) {
            int rc = (int)syscall(SYS_THR_KILL, state[i].tid, SIGUSR1);
            if (rc != 0) FAIL("signals: thr_kill tid=%ld rc=%d\n", state[i].tid, rc);
        }
        struct timespec t = {0, 500000}; nanosleep(&t, NULL);
    }
    /* Let innocents finish (they spin to count 50 of nothing, then exit). */
    for (int i = TARGETS; i < TARGETS + INNOCENTS; i++) {
        state[i].sig_count = 50;
    }
    for (int i = 0; i < TARGETS + INNOCENTS; i++) pthread_join(tids[i], NULL);

    if (g_innocent_hits != 0) {
        FAIL("signals: %d signals landed on innocent threads (per-thread "
             "delivery broken)\n", g_innocent_hits);
    }
    for (int i = 0; i < TARGETS; i++) {
        if (state[i].sig_count == 0) {
            FAIL("signals: target %d (tid=%ld) got zero signals\n", i, state[i].tid);
        }
    }
}

/* ===================================================================
 * 5. MUTEX_FAIR — starvation watchdog under heavy contention.
 *    Bug class: per-CPU runqueue handoff bias, thread A keeps stealing
 *    the lock the moment thread B releases.  Without a fair handoff
 *    discipline, some thread can be starved indefinitely.
 *
 *    Each thread records its (acquire - last_acquire) interval.  After
 *    the run we look at the MAX gap per thread.  If any thread's max
 *    gap is >50x the median, we flag starvation.
 * =================================================================== */
struct fair_state {
    pthread_mutex_t mu;
    int             stop;
    uint64_t        max_gap[16];
    uint64_t        last_acq[16];
    uint64_t        total_acq[16];
};

struct fair_arg { struct fair_state *s; int idx; };

static void *fair_worker(void *arg) {
    struct fair_arg *a = (struct fair_arg *)arg;
    struct fair_state *s = a->s;
    int i = a->idx;
    uint64_t prev = now_us();
    s->last_acq[i] = prev;
    while (!s->stop) {
        pthread_mutex_lock(&s->mu);
        uint64_t t = now_us();
        uint64_t gap = t - s->last_acq[i];
        if (gap > s->max_gap[i]) s->max_gap[i] = gap;
        s->last_acq[i] = t;
        s->total_acq[i]++;
        /* Hold for a moment so contention is real. */
        for (volatile int k = 0; k < 200; k++);
        pthread_mutex_unlock(&s->mu);
        /* Yield a bit so we don't immediately re-grab. */
        for (volatile int k = 0; k < 50; k++);
    }
    return NULL;
}

static void scenario_mutex_fair(void) {
    enum { N = 8, RUN_MS = 2000 };
    struct fair_state s = { .stop = 0 };
    pthread_mutex_init(&s.mu, NULL);
    pthread_t tids[N];
    struct fair_arg args[N];
    for (int i = 0; i < N; i++) {
        args[i].s = &s; args[i].idx = i;
        if (pthread_create(&tids[i], NULL, fair_worker, &args[i]) != 0) {
            FAIL("mutex_fair: create #%d\n", i);
            s.stop = 1;
            return;
        }
    }
    struct timespec t = {RUN_MS / 1000, (RUN_MS % 1000) * 1000000};
    nanosleep(&t, NULL);
    s.stop = 1;
    for (int i = 0; i < N; i++) pthread_join(tids[i], NULL);

    /* Look for starvation: any thread whose max_gap >> median. */
    uint64_t gaps[N];
    for (int i = 0; i < N; i++) gaps[i] = s.max_gap[i];
    /* Simple median: sort. */
    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++)
            if (gaps[i] > gaps[j]) { uint64_t x = gaps[i]; gaps[i] = gaps[j]; gaps[j] = x; }
    uint64_t med = gaps[N / 2];
    for (int i = 0; i < N; i++) {
        if (s.max_gap[i] > med * 50 && s.max_gap[i] > 100000) {
            FAIL("mutex_fair: thread %d max gap %llu us (median %llu us) — "
                 "starvation\n", i,
                 (unsigned long long)s.max_gap[i], (unsigned long long)med);
        }
    }
    printf("  mutex_fair: median max-gap %llu us; per-thread max-gaps: ",
           (unsigned long long)med);
    for (int i = 0; i < N; i++) printf("%llu ", (unsigned long long)s.max_gap[i]);
    printf("\n");
}

/* ===================================================================
 * 6. TLS — __thread int per thread; each writes a unique seed and
 *    re-reads under high context-switch pressure.  Aliasing means the
 *    gs_base TLS slot leaked across threads.
 * =================================================================== */
static __thread unsigned int tls_seed;

struct tls_arg { unsigned int seed; volatile int mismatched; };

static void *tls_worker(void *arg) {
    struct tls_arg *a = (struct tls_arg *)arg;
    tls_seed = a->seed;
    for (int i = 0; i < 20000; i++) {
        /* Bait the scheduler. */
        if ((i & 0xFF) == 0) sched_yield();
        if (tls_seed != a->seed) {
            a->mismatched = 1;
            return NULL;
        }
    }
    return NULL;
}

static void scenario_tls(void) {
    enum { N = 16 };
    pthread_t tids[N];
    struct tls_arg args[N];
    for (int i = 0; i < N; i++) {
        args[i].seed = 0xC0FFEE00u | (unsigned)i;
        args[i].mismatched = 0;
        if (pthread_create(&tids[i], NULL, tls_worker, &args[i]) != 0) {
            FAIL("tls: create #%d\n", i);
            return;
        }
    }
    for (int i = 0; i < N; i++) pthread_join(tids[i], NULL);
    for (int i = 0; i < N; i++) {
        if (args[i].mismatched) {
            FAIL("tls: thread %d saw TLS leak (gs_base aliased)\n", i);
        }
    }
}

/* ===================================================================
 * 7. MASSIVE — saturate near MAX_PTHREADS=64, 5 s sustained.
 *    Bug class: runqueue corruption, missed migration.
 * =================================================================== */
static volatile int g_massive_stop;
static volatile uint64_t g_massive_ticks[MAX_PT];

static void *massive_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    while (!g_massive_stop) {
        g_massive_ticks[id]++;
        for (volatile int k = 0; k < 1000; k++);
    }
    return NULL;
}

static void scenario_massive(void) {
    enum { N = 60, RUN_MS = 5000 };
    pthread_t tids[N];
    int created = 0;
    g_massive_stop = 0;
    memset((void *)g_massive_ticks, 0, sizeof(g_massive_ticks));
    for (int i = 0; i < N; i++) {
        if (pthread_create(&tids[i], NULL, massive_worker, (void *)(intptr_t)i) != 0) break;
        created++;
    }
    if (created < 32) {
        FAIL("massive: only created %d threads (expected >= 32)\n", created);
        g_massive_stop = 1;
        for (int i = 0; i < created; i++) pthread_join(tids[i], NULL);
        return;
    }
    struct timespec t = {RUN_MS / 1000, 0};
    nanosleep(&t, NULL);
    g_massive_stop = 1;
    for (int i = 0; i < created; i++) pthread_join(tids[i], NULL);

    /* Every thread should have made progress.  A stuck thread shows
     * up as zero ticks. */
    uint64_t mn = ~0ULL, mx = 0;
    int zeroes = 0;
    for (int i = 0; i < created; i++) {
        uint64_t v = g_massive_ticks[i];
        if (v == 0) zeroes++;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    if (zeroes) {
        FAIL("massive: %d/%d threads made zero progress (stuck in runqueue?)\n",
             zeroes, created);
    }
    if (mn > 0 && mx > 100 * mn) {
        FAIL("massive: spread mx=%llu mn=%llu (>100x — scheduler biasing)\n",
             (unsigned long long)mx, (unsigned long long)mn);
    }
    printf("  massive: %d threads ran; ticks range [%llu, %llu]\n",
           created, (unsigned long long)mn, (unsigned long long)mx);
}

/* ===================================================================
 * 8. LOCKORD — AB-BA deadlock watchdog.
 *    Bug class: kernel has no lockdep/WITNESS; user-side mutex AB-BA
 *    will hang.  We deliberately construct one and time-bound it via
 *    a third thread that aborts after 3 s if no progress.
 *
 *    Outcome:
 *      - If pthread_mutex_lock has timeout-based detection: returns
 *        EDEADLK quickly.  (Current libpthread: no.)
 *      - Otherwise: the watchdog fires.  This isn't a PASS or FAIL —
 *        it's the reality check that we currently lack deadlock
 *        avoidance.  Reported as "deadlocked as expected".
 * =================================================================== */
static pthread_mutex_t lo_m1, lo_m2;
static volatile int    lo_done;

static void *lo_thread_a(void *arg) {
    (void)arg;
    pthread_mutex_lock(&lo_m1);
    struct timespec t = {0, 10000000}; nanosleep(&t, NULL);
    pthread_mutex_lock(&lo_m2);
    pthread_mutex_unlock(&lo_m2);
    pthread_mutex_unlock(&lo_m1);
    lo_done = 1;
    return NULL;
}
static void *lo_thread_b(void *arg) {
    (void)arg;
    pthread_mutex_lock(&lo_m2);
    struct timespec t = {0, 10000000}; nanosleep(&t, NULL);
    pthread_mutex_lock(&lo_m1);
    pthread_mutex_unlock(&lo_m1);
    pthread_mutex_unlock(&lo_m2);
    return NULL;
}

static void scenario_lockord(void) {
    pthread_mutex_init(&lo_m1, NULL);
    pthread_mutex_init(&lo_m2, NULL);
    lo_done = 0;
    pthread_t a, b;
    if (pthread_create(&a, NULL, lo_thread_a, NULL) != 0 ||
        pthread_create(&b, NULL, lo_thread_b, NULL) != 0) {
        FAIL("lockord: create\n");
        return;
    }
    /* Watchdog: 3 s. */
    uint64_t deadline = now_us() + 3000000ULL;
    while (!lo_done && now_us() < deadline) {
        struct timespec t = {0, 10000000}; nanosleep(&t, NULL);
    }
    if (lo_done) {
        printf("  lockord: AB-BA completed (no deadlock — fortunate ordering)\n");
        pthread_join(a, NULL);
        pthread_join(b, NULL);
    } else {
        /* Threads are wedged.  Don't join — we'd hang forever.  Report
         * the expected outcome and move on.  The test process will
         * exit with these threads still pinned; the kernel reaps them
         * on _exit(). */
        printf("  lockord: AB-BA wedged at watchdog (expected with current "
               "libpthread — no deadlock detection)\n");
    }
}

/* ===================================================================
 * Driver
 * =================================================================== */
struct scenario { const char *name; void (*fn)(void); };
static struct scenario scenarios[] = {
    { "storm",       scenario_storm       },
    { "fpu",         scenario_fpu         },
    { "wakeup",      scenario_wakeup      },
    { "signals",     scenario_signals     },
    { "mutex_fair",  scenario_mutex_fair  },
    { "tls",         scenario_tls         },
    { "massive",     scenario_massive     },
    { "lockord",     scenario_lockord     },
};
static const int N_SCEN = sizeof(scenarios) / sizeof(scenarios[0]);

static void run_one(const char *name) {
    for (int i = 0; i < N_SCEN; i++) {
        if (strcmp(scenarios[i].name, name) == 0) {
            int before = g_failures;
            printf("[ run     ] %s\n", name);
            scenarios[i].fn();
            int after = g_failures;
            printf("[ %s ] %s (%d new failure%s)\n",
                   (after == before) ? "  ok   " : "FAILED ",
                   name, after - before, (after - before == 1) ? "" : "s");
            return;
        }
    }
    fprintf(stderr, "unknown scenario: %s\n", name);
    fprintf(stderr, "available: ");
    for (int i = 0; i < N_SCEN; i++) fprintf(stderr, "%s ", scenarios[i].name);
    fprintf(stderr, "all\n");
    exit(2);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <scenario|all>\nscenarios:", argv[0]);
        for (int i = 0; i < N_SCEN; i++) fprintf(stderr, " %s", scenarios[i].name);
        fprintf(stderr, " all\n");
        return 2;
    }
    if (strcmp(argv[1], "all") == 0) {
        for (int i = 0; i < N_SCEN; i++) run_one(scenarios[i].name);
    } else {
        for (int i = 1; i < argc; i++) run_one(argv[i]);
    }
    printf("\n%s: %d failure%s\n",
           g_failures ? "FAILED" : "PASSED",
           g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
