/*
 * torture_pthread_ext.c — substrate-target functional test for the POSIX
 * threading primitives added for Open POSIX Test Suite coverage:
 *
 *   1. barrier      — N threads rendezvous on a pthread_barrier_t; EXACTLY
 *                     one waiter must receive PTHREAD_BARRIER_SERIAL_THREAD,
 *                     and every thread must observe all N having arrived.
 *   2. spinlock     — N threads x M increments under one pthread_spinlock_t;
 *                     the final counter must equal N*M bit-exact.
 *   3. timedlock    — pthread_mutex_timedlock on a held mutex must return
 *                     ETIMEDOUT near the deadline, then succeed once free;
 *                     pthread_rwlock_timedwrlock/timedrdlock on a held write
 *                     lock must likewise time out.
 *   4. atfork       — pthread_atfork handlers must run around fork(): prepare
 *                     and parent in the parent, child in the child.
 *
 * Each subtest prints "ok N - <name>" on success or "FAIL - <name>: ..." on
 * failure; the process exits non-zero if any subtest fails.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

#define NB_THREADS   6          /* barrier participants */
#define NS_THREADS   8          /* spinlock contenders  */
#define SPIN_REPS    20000

static int failures;
static int testno;

static void ok(const char *name)   { printf("ok %d - %s\n", ++testno, name); }
static void bad(const char *name, const char *why) {
    printf("FAIL %d - %s: %s\n", ++testno, name, why);
    failures++;
}

/* -------------------- 1. barrier -------------------- */
static pthread_barrier_t g_barrier;
static volatile int      g_barrier_serial;   /* count of SERIAL returns */
static volatile int      g_barrier_arrived;  /* incremented after wait  */

static void *barrier_worker(void *arg) {
    (void)arg;
    int rc = pthread_barrier_wait(&g_barrier);
    if (rc == PTHREAD_BARRIER_SERIAL_THREAD)
        __sync_fetch_and_add(&g_barrier_serial, 1);
    else if (rc != 0)
        __sync_fetch_and_add(&failures, 1);
    __sync_fetch_and_add(&g_barrier_arrived, 1);
    return NULL;
}

static void test_barrier(void) {
    pthread_t t[NB_THREADS];
    pthread_barrierattr_t ba;
    int ps = -1;

    if (pthread_barrierattr_init(&ba) != 0 ||
        pthread_barrierattr_getpshared(&ba, &ps) != 0 ||
        ps != PTHREAD_PROCESS_PRIVATE) {
        bad("barrier", "barrierattr default pshared wrong");
        return;
    }
    pthread_barrierattr_destroy(&ba);

    if (pthread_barrier_init(&g_barrier, NULL, NB_THREADS) != 0) {
        bad("barrier", "init failed");
        return;
    }
    for (int i = 0; i < NB_THREADS; i++)
        if (pthread_create(&t[i], NULL, barrier_worker, NULL) != 0) {
            bad("barrier", "pthread_create failed");
            return;
        }
    for (int i = 0; i < NB_THREADS; i++)
        pthread_join(t[i], NULL);
    pthread_barrier_destroy(&g_barrier);

    if (g_barrier_serial != 1)
        bad("barrier", "not exactly one SERIAL_THREAD winner");
    else if (g_barrier_arrived != NB_THREADS)
        bad("barrier", "not all threads passed the barrier");
    else
        ok("barrier");
}

/* -------------------- 2. spinlock -------------------- */
static pthread_spinlock_t g_spin;
static long               g_spin_counter;

static void *spin_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < SPIN_REPS; i++) {
        pthread_spin_lock(&g_spin);
        g_spin_counter++;
        pthread_spin_unlock(&g_spin);
    }
    return NULL;
}

static void test_spinlock(void) {
    pthread_t t[NS_THREADS];

    if (pthread_spin_init(&g_spin, PTHREAD_PROCESS_PRIVATE) != 0) {
        bad("spinlock", "init failed");
        return;
    }
    /* trylock/unlock smoke test */
    if (pthread_spin_trylock(&g_spin) != 0) { bad("spinlock", "trylock free"); return; }
    if (pthread_spin_trylock(&g_spin) != EBUSY) { bad("spinlock", "trylock held !EBUSY"); return; }
    pthread_spin_unlock(&g_spin);

    for (int i = 0; i < NS_THREADS; i++)
        if (pthread_create(&t[i], NULL, spin_worker, NULL) != 0) {
            bad("spinlock", "pthread_create failed");
            return;
        }
    for (int i = 0; i < NS_THREADS; i++)
        pthread_join(t[i], NULL);
    pthread_spin_destroy(&g_spin);

    if (g_spin_counter != (long)NS_THREADS * SPIN_REPS)
        bad("spinlock", "lost updates under contention");
    else
        ok("spinlock");
}

/* -------------------- 3. timed locks -------------------- */
static void abstime_in(struct timespec *ts, long ms) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_nsec += (ms % 1000) * 1000000L;
    ts->tv_sec  += ms / 1000;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

static void test_mutex_timedlock(void) {
    pthread_mutex_t m;
    struct timespec ts;
    pthread_mutex_init(&m, NULL);

    /* uncontended: immediate acquire */
    abstime_in(&ts, 1000);
    if (pthread_mutex_timedlock(&m, &ts) != 0) {
        bad("mutex_timedlock", "failed on free mutex");
        return;
    }
    /* held by us: a second timedlock must time out */
    abstime_in(&ts, 150);
    int rc = pthread_mutex_timedlock(&m, &ts);
    if (rc != ETIMEDOUT) {
        char buf[64];
        snprintf(buf, sizeof buf, "expected ETIMEDOUT, got %d", rc);
        bad("mutex_timedlock", buf);
        pthread_mutex_unlock(&m);
        return;
    }
    pthread_mutex_unlock(&m);
    pthread_mutex_destroy(&m);
    ok("mutex_timedlock");
}

static pthread_rwlock_t g_rw;
static pthread_barrier_t g_rw_ready;   /* holder has taken the write lock */
static pthread_barrier_t g_rw_go;      /* main is done with timed attempts */

static void *rw_holder(void *arg) {
    (void)arg;
    pthread_rwlock_wrlock(&g_rw);
    pthread_barrier_wait(&g_rw_ready);
    pthread_barrier_wait(&g_rw_go);
    pthread_rwlock_unlock(&g_rw);
    return NULL;
}

static void test_rwlock_timedlock(void) {
    pthread_t holder;
    struct timespec ts;

    pthread_rwlock_init(&g_rw, NULL);
    pthread_barrier_init(&g_rw_ready, NULL, 2);
    pthread_barrier_init(&g_rw_go, NULL, 2);

    if (pthread_create(&holder, NULL, rw_holder, NULL) != 0) {
        bad("rwlock_timedlock", "pthread_create failed");
        return;
    }
    /* Wait until another thread genuinely holds the write lock. */
    pthread_barrier_wait(&g_rw_ready);

    abstime_in(&ts, 150);
    if (pthread_rwlock_timedrdlock(&g_rw, &ts) != ETIMEDOUT) {
        bad("rwlock_timedlock", "timedrdlock did not time out");
        pthread_barrier_wait(&g_rw_go); pthread_join(holder, NULL);
        return;
    }
    abstime_in(&ts, 150);
    if (pthread_rwlock_timedwrlock(&g_rw, &ts) != ETIMEDOUT) {
        bad("rwlock_timedlock", "timedwrlock did not time out");
        pthread_barrier_wait(&g_rw_go); pthread_join(holder, NULL);
        return;
    }

    /* Release the holder, then a timed read lock must now succeed. */
    pthread_barrier_wait(&g_rw_go);
    pthread_join(holder, NULL);

    abstime_in(&ts, 1000);
    if (pthread_rwlock_timedrdlock(&g_rw, &ts) != 0) {
        bad("rwlock_timedlock", "timedrdlock failed on free lock");
        return;
    }
    pthread_rwlock_unlock(&g_rw);
    pthread_rwlock_destroy(&g_rw);
    pthread_barrier_destroy(&g_rw_ready);
    pthread_barrier_destroy(&g_rw_go);
    ok("rwlock_timedlock");
}

/* -------------------- 4. atfork -------------------- */
static int af_prep, af_parent, af_child;
static void af_prepare_h(void) { af_prep = 1; }
static void af_parent_h(void)  { af_parent = 1; }
static void af_child_h(void)   { af_child = 1; }

static void test_atfork(void) {
    if (pthread_atfork(af_prepare_h, af_parent_h, af_child_h) != 0) {
        bad("atfork", "registration failed");
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        bad("atfork", "fork failed");
        return;
    }
    if (pid == 0) {
        /* Child: the child handler must have run. */
        _exit(af_child ? 42 : 43);
    }
    int status = 0;
    waitpid(pid, &status, 0);

    if (!af_prep)
        bad("atfork", "prepare handler did not run");
    else if (!af_parent)
        bad("atfork", "parent handler did not run");
    else if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
        bad("atfork", "child handler did not run");
    else
        ok("atfork");
}

int main(void) {
    printf("torture_pthread_ext: barrier / spinlock / timedlock / atfork\n");
    test_barrier();
    test_spinlock();
    test_mutex_timedlock();
    test_rwlock_timedlock();
    test_atfork();
    if (failures) {
        printf("RESULT: FAIL (%d subtest failure%s)\n",
               failures, failures == 1 ? "" : "s");
        return 1;
    }
    printf("RESULT: PASS (all %d subtests)\n", testno);
    return 0;
}
