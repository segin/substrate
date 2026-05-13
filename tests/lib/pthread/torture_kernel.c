/*
 * torture_kernel.c — POSIX-portable scheduler/threading torture suite.
 *
 * Designed to run on any POSIX system with pthreads.  The same binary
 * is the comparison baseline on Linux/FreeBSD/macOS *and* the smoke
 * test on substrate — divergence between platforms is the signal.
 *
 * Strictly POSIX:
 *   - pthread_create / join / exit / self
 *   - pthread_mutex_{init,lock,unlock,destroy}
 *   - pthread_cond_{init,wait,signal,broadcast}
 *   - pthread_kill
 *   - sigaction + SIGUSR1
 *   - __thread storage (C11 / GCC extension, ubiquitous)
 *   - clock_gettime(CLOCK_MONOTONIC), nanosleep, sched_yield
 *
 * No raw syscall(), no platform-specific headers.  If a scenario needs
 * something substrate's libpthread doesn't yet expose (pthread_cond_*,
 * pthread_kill), that's a feature gap to close — not a reason to fork
 * the test.
 *
 * Scenarios (selectable by argv, "all" runs everything):
 *
 *   storm        — 3000 rapid create/exit/join cycles.  Slot-table /
 *                  reaper churn.
 *
 *   fpu          — N threads each compute pi via Machin's formula in
 *                  long double.  All must produce a bit-exact match
 *                  against a reference computed once before spawn.
 *                  Catches FPU context-switch bugs (forgot to save
 *                  x87/SSE state across yield).
 *
 *   wakeup       — N workers pthread_cond_wait on a predicate.  Main
 *                  thread cond_broadcasts each round; workers tally
 *                  wake counts.  Tests for missed wakeups, the
 *                  signal-before-wait race, and predicate-loop
 *                  correctness — substrate's biggest cond_var
 *                  exposure once libpthread grows them.
 *
 *   signals      — pthread_kill targets specific threads while
 *                  "innocent" threads run alongside.  Per-thread
 *                  sig_pending isolation must hold: zero hits on
 *                  innocents, full quota on targets.
 *
 *   mutex_fair   — N threads contending one mutex for 2 s.  Records
 *                  per-thread max acquire-gap.  >50x median = starvation.
 *
 *   tls          — Per-thread __thread int under sched_yield pressure.
 *                  Aliasing means the kernel leaked TLS state across
 *                  threads (gs_base on i386, fs_base on amd64, %tp on
 *                  arm64 / riscv).
 *
 *   massive      — 60 threads, 5 s sustained progress.  Every thread
 *                  must have nonzero ticks; >100x spread = scheduler
 *                  biasing one CPU.
 *
 *   lockord      — Classic AB-BA deadlock with 3 s watchdog.  Not a
 *                  PASS/FAIL on plain POSIX (which has no deadlock
 *                  detection) — calibration test for future lockdep /
 *                  WITNESS work.
 *
 * Build:
 *   cc -O2 -pthread -o torture_kernel torture_kernel.c
 *
 * Run:
 *   ./torture_kernel all
 *   ./torture_kernel fpu wakeup signals
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sched.h>

static int g_failures;

#define FAIL(...) do {                                                 \
    fprintf(stderr, "FAIL: " __VA_ARGS__);                             \
    __sync_fetch_and_add(&g_failures, 1);                              \
} while (0)

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void sleep_us(uint64_t us) {
    struct timespec t = { (time_t)(us / 1000000), (long)((us % 1000000) * 1000) };
    nanosleep(&t, NULL);
}

/* ===================================================================
 * 1. STORM — 3000 create/exit/join cycles.
 *    Bug class: thread reaper losing slots; futex-on-zombie hangs.
 * =================================================================== */
static void *storm_worker(void *arg) {
    (void)arg;
    volatile long sum = 0;
    for (int i = 0; i < 100; i++) sum += i;
    return (void *)(intptr_t)sum;
}

static void scenario_storm(void) {
    uint64_t t0 = now_us();
    for (int i = 0; i < 3000; i++) {
        pthread_t tid;
        if (pthread_create(&tid, NULL, storm_worker, NULL) != 0) {
            FAIL("storm: create at iter %d (slot/resource leak?)\n", i);
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
 *    Bug class: kernel forgets to save/restore FP state across context
 *    switches, so thread A's stack/registers bleed into thread B's
 *    result.  Bit-exact mismatch is a smoking gun.
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
    enum { N = 16 };
    pthread_t tids[N];
    for (int i = 0; i < N; i++) {
        if (pthread_create(&tids[i], NULL, fpu_worker, (void *)(intptr_t)i) != 0) {
            FAIL("fpu: create #%d\n", i);
            return;
        }
    }
    for (int i = 0; i < N; i++) pthread_join(tids[i], NULL);
    long double truepi = 3.14159265358979323846264338327950288L;
    long double err = g_pi_ref - truepi;
    if (err < 0) err = -err;
    if (err > 1e-18L) {
        FAIL("fpu: reference pi off by %.3Le (Machin or long double broken)\n", err);
    }
}

/* ===================================================================
 * 3. WAKEUP — pthread_cond_t correctness.
 *    Bug class: missed wakeups, signal-before-wait, predicate races.
 *
 *    Each worker:  lock mu; while (!my_ready) cond_wait; my_ready=0;
 *                  wake_count++; unlock mu.
 *
 *    Main, R rounds: lock mu; ready[i] = 1; cond_broadcast; unlock mu.
 *
 *    After R rounds every worker.wake_count must equal R.  Lost
 *    wakeups manifest as wake_count < R and a 5 s watchdog hang.
 *
 *    The classic signal-before-wait race is exercised because the
 *    main thread holds the same mutex when setting ready[] and
 *    broadcasting — the atomicity contract of pthread_cond_wait is
 *    what makes this work.
 * =================================================================== */
struct cv_state {
    pthread_mutex_t mu;
    pthread_cond_t  cv;
    int             ready[16];
    int             wake_count[16];
    int             go_die;
};

struct cv_arg { struct cv_state *s; int idx; };

static void *cv_worker(void *arg) {
    struct cv_arg *a = (struct cv_arg *)arg;
    struct cv_state *s = a->s;
    int i = a->idx;
    pthread_mutex_lock(&s->mu);
    while (!s->go_die) {
        while (!s->ready[i] && !s->go_die) pthread_cond_wait(&s->cv, &s->mu);
        if (s->go_die) break;
        s->ready[i] = 0;
        s->wake_count[i]++;
    }
    pthread_mutex_unlock(&s->mu);
    return NULL;
}

static void scenario_wakeup(void) {
    enum { N = 8, ROUNDS = 100 };
    struct cv_state s = { .go_die = 0 };
    pthread_mutex_init(&s.mu, NULL);
    pthread_cond_init(&s.cv, NULL);

    pthread_t tids[N];
    struct cv_arg args[N];
    for (int i = 0; i < N; i++) {
        args[i].s = &s; args[i].idx = i;
        if (pthread_create(&tids[i], NULL, cv_worker, &args[i]) != 0) {
            FAIL("wakeup: create #%d\n", i);
            return;
        }
    }
    /* Drive R rounds. */
    for (int r = 0; r < ROUNDS; r++) {
        pthread_mutex_lock(&s.mu);
        for (int i = 0; i < N; i++) s.ready[i] = 1;
        pthread_cond_broadcast(&s.cv);
        pthread_mutex_unlock(&s.mu);
        /* Wait until every worker has consumed its ready flag.  Watch-
         * dog 5 s: a single missed wakeup hangs us here. */
        uint64_t deadline = now_us() + 5000000ULL;
        for (;;) {
            pthread_mutex_lock(&s.mu);
            int all_consumed = 1;
            for (int i = 0; i < N; i++) if (s.ready[i]) { all_consumed = 0; break; }
            pthread_mutex_unlock(&s.mu);
            if (all_consumed) break;
            if (now_us() > deadline) {
                FAIL("wakeup: 5 s watchdog at round %d — missed cv_wait wakeup\n", r);
                goto done;
            }
            sleep_us(100);
        }
    }
done:
    pthread_mutex_lock(&s.mu);
    s.go_die = 1;
    pthread_cond_broadcast(&s.cv);
    pthread_mutex_unlock(&s.mu);
    for (int i = 0; i < N; i++) pthread_join(tids[i], NULL);

    for (int i = 0; i < N; i++) {
        if (s.wake_count[i] != ROUNDS) {
            FAIL("wakeup: worker %d wake_count %d != %d\n", i, s.wake_count[i], ROUNDS);
        }
    }
}

/* ===================================================================
 * 4. SIGNALS — pthread_kill thread-directed delivery.
 *    Bug class: signal landing on wrong thread, sig_pending corruption.
 *
 *    Two pools: targets (subject to pthread_kill) and innocents
 *    (parallel, no signals).  After the storm:
 *      - Every target's local count > 0.
 *      - Innocent global counter == 0.
 *
 *    Edge: targets spin in user mode, so the signal lands during
 *    runtime — exactly when per-thread delivery has to thread the
 *    needle.
 * =================================================================== */
static __thread volatile int g_sig_count;
static __thread int g_is_target;
static volatile int g_innocent_hits;

static void sig_handler(int s) {
    (void)s;
    g_sig_count++;
    if (!g_is_target) __sync_fetch_and_add(&g_innocent_hits, 1);
}

struct sig_state {
    pthread_t tid;
    int       target;
    volatile int sig_count;
    volatile int started;
    volatile int stop;
};

static void *sig_worker(void *arg) {
    struct sig_state *s = (struct sig_state *)arg;
    g_is_target = s->target;
    g_sig_count = 0;
    /* Each thread unblocks SIGUSR1 (mask is inherited from main,
     * which blocks it before spawn so pthread_kill targets are
     * deterministic). */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
    s->started = 1;
    while (!s->stop) {
        for (volatile int i = 0; i < 100000; i++);
        s->sig_count = g_sig_count;
    }
    s->sig_count = g_sig_count;
    return NULL;
}

static void scenario_signals(void) {
    /* Block SIGUSR1 in main so threads inherit blocked; workers
     * unblock themselves at start.  Without this, the signal could
     * be delivered to main between spawn and pthread_kill. */
    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &block, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    enum { TARGETS = 4, INNOCENTS = 4 };
    struct sig_state state[TARGETS + INNOCENTS];
    memset(state, 0, sizeof(state));
    g_innocent_hits = 0;

    for (int i = 0; i < TARGETS + INNOCENTS; i++) {
        state[i].target = (i < TARGETS);
        if (pthread_create(&state[i].tid, NULL, sig_worker, &state[i]) != 0) {
            FAIL("signals: create #%d\n", i);
            return;
        }
    }
    /* Wait until every worker is in its spin loop. */
    uint64_t deadline = now_us() + 2000000ULL;
    int all_up = 0;
    while (now_us() < deadline) {
        all_up = 1;
        for (int i = 0; i < TARGETS + INNOCENTS; i++) if (!state[i].started) all_up = 0;
        if (all_up) break;
        sleep_us(1000);
    }
    if (!all_up) FAIL("signals: not all workers started within 2 s\n");

    /* Deliver to targets. */
    for (int round = 0; round < 50; round++) {
        for (int i = 0; i < TARGETS; i++) {
            int rc = pthread_kill(state[i].tid, SIGUSR1);
            if (rc != 0) FAIL("signals: pthread_kill target %d rc=%d\n", i, rc);
        }
        sleep_us(500);
    }
    /* Let the dust settle, then stop. */
    sleep_us(50000);
    for (int i = 0; i < TARGETS + INNOCENTS; i++) state[i].stop = 1;
    for (int i = 0; i < TARGETS + INNOCENTS; i++) pthread_join(state[i].tid, NULL);

    if (g_innocent_hits != 0) {
        FAIL("signals: %d signals landed on innocents (thread-directed delivery broken)\n",
             g_innocent_hits);
    }
    int target_total = 0;
    for (int i = 0; i < TARGETS; i++) {
        if (state[i].sig_count == 0) {
            FAIL("signals: target %d got zero signals (lost delivery)\n", i);
        }
        target_total += state[i].sig_count;
    }
    printf("  signals: %d total deliveries across %d targets; %d innocent hits\n",
           target_total, TARGETS, g_innocent_hits);
}

/* ===================================================================
 * 5. MUTEX_FAIR — starvation watchdog.
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
    s->last_acq[i] = now_us();
    while (!s->stop) {
        pthread_mutex_lock(&s->mu);
        uint64_t t = now_us();
        uint64_t gap = t - s->last_acq[i];
        if (gap > s->max_gap[i]) s->max_gap[i] = gap;
        s->last_acq[i] = t;
        s->total_acq[i]++;
        for (volatile int k = 0; k < 200; k++);
        pthread_mutex_unlock(&s->mu);
        for (volatile int k = 0; k < 50; k++);
    }
    return NULL;
}

static void scenario_mutex_fair(void) {
    enum { N = 8, RUN_MS = 2000 };
    struct fair_state s;
    memset(&s, 0, sizeof(s));
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
    struct timespec t = { RUN_MS / 1000, (RUN_MS % 1000) * 1000000 };
    nanosleep(&t, NULL);
    s.stop = 1;
    for (int i = 0; i < N; i++) pthread_join(tids[i], NULL);

    uint64_t gaps[N];
    for (int i = 0; i < N; i++) gaps[i] = s.max_gap[i];
    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++)
            if (gaps[i] > gaps[j]) { uint64_t x = gaps[i]; gaps[i] = gaps[j]; gaps[j] = x; }
    uint64_t med = gaps[N / 2];
    for (int i = 0; i < N; i++) {
        if (s.max_gap[i] > med * 50 && s.max_gap[i] > 100000) {
            FAIL("mutex_fair: thread %d max gap %llu us (median %llu us) — starvation\n",
                 i, (unsigned long long)s.max_gap[i], (unsigned long long)med);
        }
    }
    printf("  mutex_fair: median max-gap %llu us; per-thread max-gaps: ",
           (unsigned long long)med);
    for (int i = 0; i < N; i++) printf("%llu ", (unsigned long long)s.max_gap[i]);
    printf("\n");
}

/* ===================================================================
 * 6. TLS — __thread isolation.
 * =================================================================== */
static __thread unsigned int tls_seed;

struct tls_arg { unsigned int seed; volatile int mismatched; };

static void *tls_worker(void *arg) {
    struct tls_arg *a = (struct tls_arg *)arg;
    tls_seed = a->seed;
    for (int i = 0; i < 20000; i++) {
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
            FAIL("tls: thread %d saw TLS leak (per-thread base aliased)\n", i);
        }
    }
}

/* ===================================================================
 * 7. MASSIVE — sustained progress.
 * =================================================================== */
#define MASSIVE_N 60
static volatile int g_massive_stop;
static volatile uint64_t g_massive_ticks[MASSIVE_N];

static void *massive_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    while (!g_massive_stop) {
        g_massive_ticks[id]++;
        for (volatile int k = 0; k < 1000; k++);
    }
    return NULL;
}

static void scenario_massive(void) {
    int run_ms = 5000;
    pthread_t tids[MASSIVE_N];
    int created = 0;
    g_massive_stop = 0;
    memset((void *)g_massive_ticks, 0, sizeof(g_massive_ticks));
    for (int i = 0; i < MASSIVE_N; i++) {
        if (pthread_create(&tids[i], NULL, massive_worker, (void *)(intptr_t)i) != 0) break;
        created++;
    }
    if (created < 32) {
        FAIL("massive: only created %d threads (expected >= 32)\n", created);
        g_massive_stop = 1;
        for (int i = 0; i < created; i++) pthread_join(tids[i], NULL);
        return;
    }
    struct timespec t = { run_ms / 1000, 0 };
    nanosleep(&t, NULL);
    g_massive_stop = 1;
    for (int i = 0; i < created; i++) pthread_join(tids[i], NULL);

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
 * =================================================================== */
static pthread_mutex_t lo_m1, lo_m2;
static volatile int    lo_done;

static void *lo_thread_a(void *arg) {
    (void)arg;
    pthread_mutex_lock(&lo_m1);
    sleep_us(10000);
    pthread_mutex_lock(&lo_m2);
    pthread_mutex_unlock(&lo_m2);
    pthread_mutex_unlock(&lo_m1);
    lo_done = 1;
    return NULL;
}

static void *lo_thread_b(void *arg) {
    (void)arg;
    pthread_mutex_lock(&lo_m2);
    sleep_us(10000);
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
    uint64_t deadline = now_us() + 3000000ULL;
    while (!lo_done && now_us() < deadline) sleep_us(10000);
    if (lo_done) {
        printf("  lockord: AB-BA completed (no deadlock — fortunate ordering)\n");
        pthread_join(a, NULL);
        pthread_join(b, NULL);
    } else {
        printf("  lockord: AB-BA wedged at 3 s watchdog "
               "(expected on POSIX without lockdep/WITNESS)\n");
        /* Don't join — would hang.  Leak the threads; the kernel
         * reaps them on _exit. */
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
