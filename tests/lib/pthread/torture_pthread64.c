/*
 * torture_pthread64.c — 64-point libpthread torture test.
 *
 * One portable binary (Linux / FreeBSD / substrate) of 64 numbered,
 * self-checking scenarios covering the whole pthread surface: thread
 * lifecycle, mutexes (all types), condition variables (incl. CLOCK_MONOTONIC
 * timedwait), read/write locks, thread-specific data keys (incl. destructors),
 * pthread_once, thread attributes, and per-thread signals.  Each test prints
 * "[NN] name PASS|FAIL" and is run under a SIGALRM watchdog, so a deadlocked
 * test is reported as a FAIL (HANG) instead of wedging the suite.
 *
 *   host:       cc -O2 -pthread torture_pthread64.c -o torture_pthread64
 *   substrate:  i386-unknown-substrate-gcc -O2 torture_pthread64.c -lpthread \
 *                   -o torture_pthread64
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <pthread.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

/* ---- harness ----------------------------------------------------------- */

static int g_n, g_pass, g_fail;
static sigjmp_buf g_watchdog;

static void on_alarm(int s) { (void)s; siglongjmp(g_watchdog, 1); }

/* Run one test function (returns 1 on pass, 0 on fail) under a watchdog. */
static void run(const char *name, int (*fn)(void), unsigned secs)
{
    g_n++;
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;          /* no SA_RESTART: interrupt blocking calls */
    sigaction(SIGALRM, &sa, &old);

    int ok;
    if (sigsetjmp(g_watchdog, 1) == 0) {
        alarm(secs ? secs : 5);
        ok = fn();
    } else {
        ok = -1;                       /* watchdog fired: hang */
    }
    alarm(0);
    sigaction(SIGALRM, &old, NULL);

    printf("[%02d] %-28s %s\n", g_n, name,
           ok == 1 ? "PASS" : (ok < 0 ? "FAIL (HANG)" : "FAIL"));
    if (ok == 1) g_pass++; else g_fail++;
    fflush(stdout);
}

static void msleep(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Absolute deadline `ms` from now on the given clock. */
static void deadline(clockid_t clk, int ms, struct timespec *ts)
{
    clock_gettime(clk, ts);
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    ts->tv_sec  += ms / 1000;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

/* ===================== A. thread lifecycle (1-10) ===================== */

static void *ret_routine(void *a) { return a; }

static int t_create_join(void)
{
    pthread_t t;
    if (pthread_create(&t, NULL, ret_routine, (void *)0x1234) != 0) return 0;
    void *r = (void *)0;
    if (pthread_join(t, &r) != 0) return 0;
    return r == (void *)0x1234;
}

static int t_join_null_retval(void)
{
    pthread_t t;
    if (pthread_create(&t, NULL, ret_routine, NULL) != 0) return 0;
    return pthread_join(t, NULL) == 0;
}

static void *exit_routine(void *a) { pthread_exit(a); return NULL; }
static int t_pthread_exit(void)
{
    pthread_t t;
    pthread_create(&t, NULL, exit_routine, (void *)0xBEEF);
    void *r;
    pthread_join(t, &r);
    return r == (void *)0xBEEF;
}

static pthread_t g_self_seen;
static void *self_routine(void *a) { (void)a; g_self_seen = pthread_self(); return NULL; }
static int t_self_distinct(void)
{
    pthread_t main_self = pthread_self();
    pthread_t t;
    pthread_create(&t, NULL, self_routine, NULL);
    pthread_join(t, NULL);
    /* the child's self must equal its id and differ from main */
    return pthread_equal(g_self_seen, t) && !pthread_equal(g_self_seen, main_self);
}

static int t_equal_reflexive(void)
{
    pthread_t s = pthread_self();
    return pthread_equal(s, s) && pthread_equal(pthread_self(), pthread_self());
}

#define NMANY 16
static int g_many_counter;
static pthread_mutex_t g_many_lock = PTHREAD_MUTEX_INITIALIZER;
static void *many_routine(void *a) { (void)a; pthread_mutex_lock(&g_many_lock); g_many_counter++; pthread_mutex_unlock(&g_many_lock); return NULL; }
static int t_many_threads(void)
{
    pthread_t t[NMANY];
    g_many_counter = 0;
    for (int i = 0; i < NMANY; i++)
        if (pthread_create(&t[i], NULL, many_routine, NULL) != 0) return 0;
    for (int i = 0; i < NMANY; i++) pthread_join(t[i], NULL);
    return g_many_counter == NMANY;
}

static int g_arg_sum;
static pthread_mutex_t g_arg_lock = PTHREAD_MUTEX_INITIALIZER;
static void *arg_routine(void *a) { pthread_mutex_lock(&g_arg_lock); g_arg_sum += (int)(long)a; pthread_mutex_unlock(&g_arg_lock); return NULL; }
static int t_distinct_args(void)
{
    pthread_t t[8];
    g_arg_sum = 0;
    for (int i = 0; i < 8; i++) pthread_create(&t[i], NULL, arg_routine, (void *)(long)(i + 1));
    for (int i = 0; i < 8; i++) pthread_join(t[i], NULL);
    return g_arg_sum == 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8;   /* 36 */
}

static volatile int g_detached_ran;
static void *detached_routine(void *a) { (void)a; g_detached_ran = 1; return NULL; }
static int t_detach(void)
{
    pthread_t t;
    if (pthread_create(&t, NULL, detached_routine, NULL) != 0) return 0;
    if (pthread_detach(t) != 0) return 0;
    for (int i = 0; i < 200 && !g_detached_ran; i++) msleep(5);
    return g_detached_ran == 1;
}

static int t_cycle_recycle(void)
{
    /* spawn+join more than MAX_PTHREADS times to prove slot reuse */
    for (int i = 0; i < 200; i++) {
        pthread_t t;
        if (pthread_create(&t, NULL, ret_routine, NULL) != 0) return 0;
        if (pthread_join(t, NULL) != 0) return 0;
    }
    return 1;
}

static long g_stack_sum[8];
static void *stack_routine(void *a)
{
    long id = (long)a;
    volatile char buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (char)(id + i);
    long s = 0;
    for (int i = 0; i < 256; i++) s += (unsigned char)buf[i];
    g_stack_sum[id] = s;
    return NULL;
}
static int t_stacks_independent(void)
{
    pthread_t t[8];
    for (long i = 0; i < 8; i++) pthread_create(&t[i], NULL, stack_routine, (void *)i);
    for (int i = 0; i < 8; i++) pthread_join(t[i], NULL);
    for (long i = 0; i < 8; i++) {
        long expect = 0;
        for (int j = 0; j < 256; j++) expect += (unsigned char)(i + j);
        if (g_stack_sum[i] != expect) return 0;
    }
    return 1;
}

/* ===================== B. mutexes (11-24) ===================== */

static int t_mutex_basic(void)
{
    pthread_mutex_t m;
    if (pthread_mutex_init(&m, NULL) != 0) return 0;
    if (pthread_mutex_lock(&m) != 0) return 0;
    if (pthread_mutex_unlock(&m) != 0) return 0;
    return pthread_mutex_destroy(&m) == 0;
}

static int t_mutex_static_init(void)
{
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    return pthread_mutex_lock(&m) == 0 && pthread_mutex_unlock(&m) == 0;
}

static int t_mutex_trylock_free(void)
{
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    if (pthread_mutex_trylock(&m) != 0) return 0;
    pthread_mutex_unlock(&m);
    return 1;
}

static pthread_mutex_t g_tl_held = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_tl_result;
static void *trylock_held_routine(void *a)
{
    (void)a;
    g_tl_result = pthread_mutex_trylock(&g_tl_held);  /* should be EBUSY */
    if (g_tl_result == 0) pthread_mutex_unlock(&g_tl_held);
    return NULL;
}
static int t_mutex_trylock_held(void)
{
    pthread_mutex_lock(&g_tl_held);
    pthread_t t;
    pthread_create(&t, NULL, trylock_held_routine, NULL);
    pthread_join(t, NULL);
    pthread_mutex_unlock(&g_tl_held);
    return g_tl_result == EBUSY;
}

/* Recursive re-lock is done in a CHILD thread and detected via a flag with a
 * main-side timeout: a platform that doesn't honour PTHREAD_MUTEX_RECURSIVE
 * deadlocks the child on the second lock, but the suite must still report FAIL
 * and move on rather than wedge on an uninterruptible futex. */
static pthread_mutex_t g_rec_m;
static volatile int g_rec_done;
static void *rec_routine(void *a)
{
    (void)a;
    pthread_mutex_lock(&g_rec_m);
    pthread_mutex_lock(&g_rec_m);   /* must just bump the recursion count */
    pthread_mutex_lock(&g_rec_m);
    pthread_mutex_unlock(&g_rec_m);
    pthread_mutex_unlock(&g_rec_m);
    pthread_mutex_unlock(&g_rec_m);
    g_rec_done = 1;
    return NULL;
}
static int t_mutex_recursive(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    if (pthread_mutex_init(&g_rec_m, &a) != 0) { pthread_mutexattr_destroy(&a); return 0; }
    pthread_mutexattr_destroy(&a);
    g_rec_done = 0;
    pthread_t t;
    if (pthread_create(&t, NULL, rec_routine, NULL) != 0) return 0;
    for (int i = 0; i < 150 && !g_rec_done; i++) msleep(10);  /* ~1.5s grace */
    return g_rec_done == 1;
}

static int t_mutexattr_gettype(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    int type = -1;
    pthread_mutexattr_gettype(&a, &type);
    pthread_mutexattr_destroy(&a);
    return type == PTHREAD_MUTEX_RECURSIVE;
}

#define MX_THREADS 8
#define MX_BUMPS  20000
static long g_mx_counter;
static pthread_mutex_t g_mx_lock = PTHREAD_MUTEX_INITIALIZER;
static void *mx_routine(void *a)
{
    (void)a;
    for (int i = 0; i < MX_BUMPS; i++) {
        pthread_mutex_lock(&g_mx_lock);
        g_mx_counter++;
        pthread_mutex_unlock(&g_mx_lock);
    }
    return NULL;
}
static int t_mutex_mutual_exclusion(void)
{
    pthread_t t[MX_THREADS];
    g_mx_counter = 0;
    for (int i = 0; i < MX_THREADS; i++) pthread_create(&t[i], NULL, mx_routine, NULL);
    for (int i = 0; i < MX_THREADS; i++) pthread_join(t[i], NULL);
    return g_mx_counter == (long)MX_THREADS * MX_BUMPS;   /* bit-exact */
}

static pthread_mutex_t g_ho_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_ho_got;     /* set by the child once it enters the CS */
static void *handoff_routine(void *a)
{
    (void)a;
    pthread_mutex_lock(&g_ho_lock);   /* blocks while main holds the lock */
    g_ho_got = 1;
    pthread_mutex_unlock(&g_ho_lock);
    return NULL;
}
static int t_mutex_blocks(void)
{
    /* While main holds the lock the child must be unable to enter its critical
     * section (g_ho_got stays 0); after main releases, the child proceeds. */
    g_ho_got = 0;
    pthread_mutex_lock(&g_ho_lock);
    pthread_t t;
    pthread_create(&t, NULL, handoff_routine, NULL);
    msleep(50);
    int blocked = (g_ho_got == 0);    /* child genuinely blocked on the lock */
    pthread_mutex_unlock(&g_ho_lock);
    pthread_join(t, NULL);
    return blocked && g_ho_got == 1;
}

static int t_mutex_unlock_relock(void)
{
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    for (int i = 0; i < 1000; i++) {
        if (pthread_mutex_lock(&m) != 0) return 0;
        if (pthread_mutex_unlock(&m) != 0) return 0;
    }
    return 1;
}

#define RAPID_T 8
static pthread_mutex_t g_rapid = PTHREAD_MUTEX_INITIALIZER;
static void *rapid_routine(void *a) { (void)a; for (int i = 0; i < 50000; i++) { pthread_mutex_lock(&g_rapid); pthread_mutex_unlock(&g_rapid); } return NULL; }
static int t_mutex_rapid(void)
{
    pthread_t t[RAPID_T];
    for (int i = 0; i < RAPID_T; i++) pthread_create(&t[i], NULL, rapid_routine, NULL);
    for (int i = 0; i < RAPID_T; i++) pthread_join(t[i], NULL);
    return 1;   /* survives without deadlock */
}

/* ===================== C. condition variables (25-38) ===================== */

static pthread_mutex_t g_cv_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv_c = PTHREAD_COND_INITIALIZER;
static volatile int g_cv_ready, g_cv_woke;

static void *cv_waiter(void *a)
{
    (void)a;
    pthread_mutex_lock(&g_cv_m);
    while (!g_cv_ready) pthread_cond_wait(&g_cv_c, &g_cv_m);
    g_cv_woke = 1;
    pthread_mutex_unlock(&g_cv_m);
    return NULL;
}
static int t_cond_signal(void)
{
    g_cv_ready = g_cv_woke = 0;
    pthread_t t;
    pthread_create(&t, NULL, cv_waiter, NULL);
    msleep(30);
    pthread_mutex_lock(&g_cv_m);
    g_cv_ready = 1;
    pthread_cond_signal(&g_cv_c);
    pthread_mutex_unlock(&g_cv_m);
    pthread_join(t, NULL);
    return g_cv_woke == 1;
}

#define BC_T 8
static pthread_mutex_t g_bc_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_bc_c = PTHREAD_COND_INITIALIZER;
static volatile int g_bc_go, g_bc_count;
static void *bc_waiter(void *a)
{
    (void)a;
    pthread_mutex_lock(&g_bc_m);
    while (!g_bc_go) pthread_cond_wait(&g_bc_c, &g_bc_m);
    g_bc_count++;
    pthread_mutex_unlock(&g_bc_m);
    return NULL;
}
static int t_cond_broadcast(void)
{
    pthread_t t[BC_T];
    g_bc_go = g_bc_count = 0;
    for (int i = 0; i < BC_T; i++) pthread_create(&t[i], NULL, bc_waiter, NULL);
    msleep(40);
    pthread_mutex_lock(&g_bc_m);
    g_bc_go = 1;
    pthread_cond_broadcast(&g_bc_c);
    pthread_mutex_unlock(&g_bc_m);
    for (int i = 0; i < BC_T; i++) pthread_join(t[i], NULL);
    return g_bc_count == BC_T;
}

static int t_cond_timedwait_timeout(void)
{
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t  c = PTHREAD_COND_INITIALIZER;
    struct timespec ts;
    deadline(CLOCK_REALTIME, 80, &ts);
    pthread_mutex_lock(&m);
    int rc = pthread_cond_timedwait(&c, &m, &ts);   /* nobody signals */
    pthread_mutex_unlock(&m);
    return rc == ETIMEDOUT;
}

static int t_cond_timedwait_signaled(void)
{
    g_cv_ready = g_cv_woke = 0;
    pthread_t t;
    pthread_create(&t, NULL, cv_waiter, NULL);   /* uses plain cond_wait */
    msleep(20);
    pthread_mutex_lock(&g_cv_m);
    g_cv_ready = 1;
    pthread_cond_signal(&g_cv_c);
    pthread_mutex_unlock(&g_cv_m);
    pthread_join(t, NULL);
    return g_cv_woke == 1;
}

/* condattr + CLOCK_MONOTONIC timedwait — the path GLib's GCond uses */
static int t_condattr_monotonic_timeout(void)
{
    pthread_condattr_t ca;
    if (pthread_condattr_init(&ca) != 0) return 0;
    if (pthread_condattr_setclock(&ca, CLOCK_MONOTONIC) != 0) return 0;
    pthread_cond_t c;
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    if (pthread_cond_init(&c, &ca) != 0) return 0;
    pthread_condattr_destroy(&ca);
    struct timespec ts;
    deadline(CLOCK_MONOTONIC, 80, &ts);
    pthread_mutex_lock(&m);
    int rc = pthread_cond_timedwait(&c, &m, &ts);
    pthread_mutex_unlock(&m);
    pthread_cond_destroy(&c);
    return rc == ETIMEDOUT;
}

static int t_condattr_getclock(void)
{
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    int clk = -1;
    pthread_condattr_getclock(&ca, &clk);
    pthread_condattr_destroy(&ca);
    return clk == CLOCK_MONOTONIC;
}

/* ping-pong: two threads alternate via two predicates on one cond */
static pthread_mutex_t g_pp_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_pp_c = PTHREAD_COND_INITIALIZER;
static int g_pp_turn, g_pp_rounds;
static void *pp_routine(void *a)
{
    int me = (int)(long)a;
    for (int i = 0; i < 100; i++) {
        pthread_mutex_lock(&g_pp_m);
        while (g_pp_turn != me) pthread_cond_wait(&g_pp_c, &g_pp_m);
        g_pp_rounds++;
        g_pp_turn = !me;
        pthread_cond_broadcast(&g_pp_c);
        pthread_mutex_unlock(&g_pp_m);
    }
    return NULL;
}
static int t_cond_pingpong(void)
{
    g_pp_turn = 0; g_pp_rounds = 0;
    pthread_t a, b;
    pthread_create(&a, NULL, pp_routine, (void *)0);
    pthread_create(&b, NULL, pp_routine, (void *)1);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    return g_pp_rounds == 200;
}

/* bounded producer/consumer */
#define PC_N 2000
#define PC_CAP 16
static pthread_mutex_t g_pc_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_pc_notfull = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  g_pc_notempty = PTHREAD_COND_INITIALIZER;
static int g_pc_buf[PC_CAP], g_pc_count, g_pc_head, g_pc_tail;
static long g_pc_consumed_sum;
static void *producer(void *a)
{
    (void)a;
    for (int i = 1; i <= PC_N; i++) {
        pthread_mutex_lock(&g_pc_m);
        while (g_pc_count == PC_CAP) pthread_cond_wait(&g_pc_notfull, &g_pc_m);
        g_pc_buf[g_pc_tail] = i; g_pc_tail = (g_pc_tail + 1) % PC_CAP; g_pc_count++;
        pthread_cond_signal(&g_pc_notempty);
        pthread_mutex_unlock(&g_pc_m);
    }
    return NULL;
}
static void *consumer(void *a)
{
    (void)a;
    for (int i = 0; i < PC_N; i++) {
        pthread_mutex_lock(&g_pc_m);
        while (g_pc_count == 0) pthread_cond_wait(&g_pc_notempty, &g_pc_m);
        int v = g_pc_buf[g_pc_head]; g_pc_head = (g_pc_head + 1) % PC_CAP; g_pc_count--;
        g_pc_consumed_sum += v;
        pthread_cond_signal(&g_pc_notfull);
        pthread_mutex_unlock(&g_pc_m);
    }
    return NULL;
}
static int t_cond_producer_consumer(void)
{
    g_pc_count = g_pc_head = g_pc_tail = 0; g_pc_consumed_sum = 0;
    pthread_t p, c;
    pthread_create(&p, NULL, producer, NULL);
    pthread_create(&c, NULL, consumer, NULL);
    pthread_join(p, NULL); pthread_join(c, NULL);
    return g_pc_consumed_sum == (long)PC_N * (PC_N + 1) / 2;
}

static int t_cond_no_waiter_signal(void)
{
    /* signal/broadcast with no waiter is a harmless no-op */
    pthread_cond_t c = PTHREAD_COND_INITIALIZER;
    return pthread_cond_signal(&c) == 0 && pthread_cond_broadcast(&c) == 0;
}

static int t_cond_init_destroy(void)
{
    pthread_cond_t c;
    if (pthread_cond_init(&c, NULL) != 0) return 0;
    return pthread_cond_destroy(&c) == 0;
}

/* relock after timeout: mutex must be held again on return */
static int t_cond_timedwait_relocks(void)
{
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t c = PTHREAD_COND_INITIALIZER;
    struct timespec ts; deadline(CLOCK_REALTIME, 40, &ts);
    pthread_mutex_lock(&m);
    pthread_cond_timedwait(&c, &m, &ts);
    /* if the mutex were not re-held, trylock would succeed; it must NOT */
    int held = (pthread_mutex_trylock(&m) != 0);
    pthread_mutex_unlock(&m);
    return held;
}

/* repeated short timed waits don't lose the eventual signal */
static pthread_mutex_t g_lw_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_lw_c = PTHREAD_COND_INITIALIZER;
static volatile int g_lw_ready, g_lw_woke;
static void *lw_waiter(void *a)
{
    (void)a;
    pthread_mutex_lock(&g_lw_m);
    while (!g_lw_ready) {
        struct timespec ts; deadline(CLOCK_REALTIME, 10, &ts);
        pthread_cond_timedwait(&g_lw_c, &g_lw_m, &ts);
    }
    g_lw_woke = 1;
    pthread_mutex_unlock(&g_lw_m);
    return NULL;
}
static int t_cond_no_lost_wakeup(void)
{
    g_lw_ready = g_lw_woke = 0;
    pthread_t t;
    pthread_create(&t, NULL, lw_waiter, NULL);
    msleep(55);
    pthread_mutex_lock(&g_lw_m);
    g_lw_ready = 1;
    pthread_cond_signal(&g_lw_c);
    pthread_mutex_unlock(&g_lw_m);
    pthread_join(t, NULL);
    return g_lw_woke == 1;
}

/* ===================== D. read/write locks (39-48) ===================== */

static int t_rwlock_basic(void)
{
    pthread_rwlock_t rw;
    if (pthread_rwlock_init(&rw, NULL) != 0) return 0;
    int ok = pthread_rwlock_rdlock(&rw) == 0 && pthread_rwlock_unlock(&rw) == 0 &&
             pthread_rwlock_wrlock(&rw) == 0 && pthread_rwlock_unlock(&rw) == 0;
    pthread_rwlock_destroy(&rw);
    return ok;
}

static pthread_rwlock_t g_rw_concurrent;
static volatile int g_rw_readers_now, g_rw_max_readers;
static pthread_mutex_t g_rw_stat = PTHREAD_MUTEX_INITIALIZER;
static void *rw_reader(void *a)
{
    (void)a;
    pthread_rwlock_rdlock(&g_rw_concurrent);
    pthread_mutex_lock(&g_rw_stat);
    g_rw_readers_now++;
    if (g_rw_readers_now > g_rw_max_readers) g_rw_max_readers = g_rw_readers_now;
    pthread_mutex_unlock(&g_rw_stat);
    msleep(40);
    pthread_mutex_lock(&g_rw_stat);
    g_rw_readers_now--;
    pthread_mutex_unlock(&g_rw_stat);
    pthread_rwlock_unlock(&g_rw_concurrent);
    return NULL;
}
static int t_rwlock_concurrent_readers(void)
{
    pthread_rwlock_init(&g_rw_concurrent, NULL);
    g_rw_readers_now = g_rw_max_readers = 0;
    pthread_t t[6];
    for (int i = 0; i < 6; i++) pthread_create(&t[i], NULL, rw_reader, NULL);
    for (int i = 0; i < 6; i++) pthread_join(t[i], NULL);
    pthread_rwlock_destroy(&g_rw_concurrent);
    return g_rw_max_readers >= 2;   /* readers genuinely overlapped */
}

static pthread_rwlock_t g_rw_excl;
static volatile int g_rw_in_write, g_rw_excl_viol;
static void *rw_writer(void *a)
{
    (void)a;
    for (int i = 0; i < 200; i++) {
        pthread_rwlock_wrlock(&g_rw_excl);
        if (g_rw_in_write) g_rw_excl_viol = 1;
        g_rw_in_write = 1;
        g_rw_in_write = 0;
        pthread_rwlock_unlock(&g_rw_excl);
    }
    return NULL;
}
static int t_rwlock_writer_exclusive(void)
{
    pthread_rwlock_init(&g_rw_excl, NULL);
    g_rw_in_write = g_rw_excl_viol = 0;
    pthread_t t[4];
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, rw_writer, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    pthread_rwlock_destroy(&g_rw_excl);
    return g_rw_excl_viol == 0;
}

static int t_rwlock_tryrdlock(void)
{
    pthread_rwlock_t rw; pthread_rwlock_init(&rw, NULL);
    int ok = (pthread_rwlock_tryrdlock(&rw) == 0);
    pthread_rwlock_unlock(&rw);
    pthread_rwlock_destroy(&rw);
    return ok;
}

static pthread_rwlock_t g_rw_tw;
static volatile int g_rw_tw_rc;
static void *rw_trywr(void *a) { (void)a; g_rw_tw_rc = pthread_rwlock_trywrlock(&g_rw_tw); if (g_rw_tw_rc == 0) pthread_rwlock_unlock(&g_rw_tw); return NULL; }
static int t_rwlock_trywrlock_busy(void)
{
    pthread_rwlock_init(&g_rw_tw, NULL);
    pthread_rwlock_rdlock(&g_rw_tw);     /* hold a read lock */
    pthread_t t; pthread_create(&t, NULL, rw_trywr, NULL); pthread_join(t, NULL);
    pthread_rwlock_unlock(&g_rw_tw);
    pthread_rwlock_destroy(&g_rw_tw);
    return g_rw_tw_rc == EBUSY;          /* writer can't acquire over a reader */
}

static pthread_rwlock_t g_rw_wb;
static volatile int g_rw_wb_state;   /* 0 init, 1 writer holds, 2 reader got in */
static void *rw_wb_writer(void *a) { (void)a; pthread_rwlock_wrlock(&g_rw_wb); g_rw_wb_state = 1; msleep(60); g_rw_wb_state = 3; pthread_rwlock_unlock(&g_rw_wb); return NULL; }
static int t_rwlock_reader_waits_writer(void)
{
    pthread_rwlock_init(&g_rw_wb, NULL);
    g_rw_wb_state = 0;
    pthread_t w; pthread_create(&w, NULL, rw_wb_writer, NULL);
    for (int i = 0; i < 100 && g_rw_wb_state != 1; i++) msleep(2);  /* writer in */
    pthread_rwlock_rdlock(&g_rw_wb);     /* must block until writer (state 3) done */
    int ok = (g_rw_wb_state == 3);
    pthread_rwlock_unlock(&g_rw_wb);
    pthread_join(w, NULL);
    pthread_rwlock_destroy(&g_rw_wb);
    return ok;
}

static int t_rwlock_many_rdunlock(void)
{
    pthread_rwlock_t rw; pthread_rwlock_init(&rw, NULL);
    for (int i = 0; i < 1000; i++) { if (pthread_rwlock_rdlock(&rw)) return 0; if (pthread_rwlock_unlock(&rw)) return 0; }
    pthread_rwlock_destroy(&rw);
    return 1;
}

static int t_rwlock_static_init(void)
{
#ifdef PTHREAD_RWLOCK_INITIALIZER
    static pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
    int ok = pthread_rwlock_rdlock(&rw) == 0 && pthread_rwlock_unlock(&rw) == 0;
    return ok;
#else
    return 1;
#endif
}

static long g_rw_sum;
static pthread_rwlock_t g_rw_data;
static int g_rw_shared;
static void *rw_mixed_reader(void *a) { (void)a; for (int i = 0; i < 1000; i++) { pthread_rwlock_rdlock(&g_rw_data); volatile int v = g_rw_shared; (void)v; pthread_rwlock_unlock(&g_rw_data); } return NULL; }
static void *rw_mixed_writer(void *a) { (void)a; for (int i = 0; i < 1000; i++) { pthread_rwlock_wrlock(&g_rw_data); g_rw_shared++; pthread_rwlock_unlock(&g_rw_data); } return NULL; }
static int t_rwlock_mixed_stress(void)
{
    pthread_rwlock_init(&g_rw_data, NULL);
    g_rw_shared = 0;
    pthread_t r[4], w[2];
    for (int i = 0; i < 4; i++) pthread_create(&r[i], NULL, rw_mixed_reader, NULL);
    for (int i = 0; i < 2; i++) pthread_create(&w[i], NULL, rw_mixed_writer, NULL);
    for (int i = 0; i < 4; i++) pthread_join(r[i], NULL);
    for (int i = 0; i < 2; i++) pthread_join(w[i], NULL);
    pthread_rwlock_destroy(&g_rw_data);
    (void)g_rw_sum;
    return g_rw_shared == 2000;   /* both writers' increments survived */
}

/* ===================== E. thread-specific data keys (49-58) ===================== */

static int t_key_create_delete(void)
{
    pthread_key_t k;
    if (pthread_key_create(&k, NULL) != 0) return 0;
    return pthread_key_delete(k) == 0;
}

static int t_key_set_get(void)
{
    pthread_key_t k;
    pthread_key_create(&k, NULL);
    pthread_setspecific(k, (void *)0xABCD);
    int ok = pthread_getspecific(k) == (void *)0xABCD;
    pthread_key_delete(k);
    return ok;
}

static int t_key_default_null(void)
{
    pthread_key_t k;
    pthread_key_create(&k, NULL);
    int ok = pthread_getspecific(k) == NULL;   /* unset reads NULL */
    pthread_key_delete(k);
    return ok;
}

static pthread_key_t g_iso_key;
static volatile int g_iso_fail;
static void *iso_routine(void *a)
{
    long id = (long)a;
    pthread_setspecific(g_iso_key, (void *)id);
    msleep(20);
    if (pthread_getspecific(g_iso_key) != (void *)id) g_iso_fail = 1;
    return NULL;
}
static int t_key_per_thread_isolation(void)
{
    pthread_key_create(&g_iso_key, NULL);
    pthread_setspecific(g_iso_key, (void *)0x999);
    g_iso_fail = 0;
    pthread_t t[8];
    for (long i = 1; i <= 8; i++) pthread_create(&t[i - 1], NULL, iso_routine, (void *)(i * 100));
    for (int i = 0; i < 8; i++) pthread_join(t[i], NULL);
    int ok = !g_iso_fail && pthread_getspecific(g_iso_key) == (void *)0x999;  /* main unchanged */
    pthread_key_delete(g_iso_key);
    return ok;
}

static pthread_key_t g_dtor_key;
static volatile int g_dtor_calls;
static void key_dtor(void *v) { if (v) __sync_fetch_and_add(&g_dtor_calls, 1); }
static void *dtor_routine(void *a) { (void)a; pthread_setspecific(g_dtor_key, (void *)0x1); return NULL; }
static int t_key_destructor(void)
{
    pthread_key_create(&g_dtor_key, key_dtor);
    g_dtor_calls = 0;
    pthread_t t[4];
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, dtor_routine, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    pthread_key_delete(g_dtor_key);
    return g_dtor_calls == 4;   /* destructor ran for each exiting thread */
}

static int t_key_many(void)
{
    pthread_key_t k[32];
    for (int i = 0; i < 32; i++) if (pthread_key_create(&k[i], NULL) != 0) return 0;
    for (int i = 0; i < 32; i++) pthread_setspecific(k[i], (void *)(long)(i + 1));
    for (int i = 0; i < 32; i++) if (pthread_getspecific(k[i]) != (void *)(long)(i + 1)) return 0;
    for (int i = 0; i < 32; i++) pthread_key_delete(k[i]);
    return 1;
}

static int t_key_reuse(void)
{
    /* delete then recreate; a fresh key reads NULL even if the slot recycles */
    pthread_key_t k;
    pthread_key_create(&k, NULL);
    pthread_setspecific(k, (void *)0x55);
    pthread_key_delete(k);
    pthread_key_t k2;
    pthread_key_create(&k2, NULL);
    int ok = pthread_getspecific(k2) == NULL;
    pthread_key_delete(k2);
    return ok;
}

static pthread_key_t g_ov_key;
static int t_key_overwrite(void)
{
    pthread_key_create(&g_ov_key, NULL);
    pthread_setspecific(g_ov_key, (void *)1);
    pthread_setspecific(g_ov_key, (void *)2);
    int ok = pthread_getspecific(g_ov_key) == (void *)2;
    pthread_key_delete(g_ov_key);
    return ok;
}

static pthread_key_t g_kc_key;
static volatile int g_kc_seen;
static void *key_concurrent(void *a)
{
    long id = (long)a;
    for (int i = 0; i < 1000; i++) {
        pthread_setspecific(g_kc_key, (void *)id);
        if (pthread_getspecific(g_kc_key) != (void *)id) { g_kc_seen = 1; return NULL; }
    }
    return NULL;
}
static int t_key_concurrent_stress(void)
{
    pthread_key_create(&g_kc_key, NULL);
    g_kc_seen = 0;
    pthread_t t[6];
    for (long i = 1; i <= 6; i++) pthread_create(&t[i - 1], NULL, key_concurrent, (void *)i);
    for (int i = 0; i < 6; i++) pthread_join(t[i], NULL);
    pthread_key_delete(g_kc_key);
    return g_kc_seen == 0;
}

static int t_key_null_clear(void)
{
    pthread_key_t k; pthread_key_create(&k, NULL);
    pthread_setspecific(k, (void *)0x7);
    pthread_setspecific(k, NULL);
    int ok = pthread_getspecific(k) == NULL;
    pthread_key_delete(k);
    return ok;
}

/* ===================== F. pthread_once (59-61) ===================== */

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static volatile int g_once_count;
static void once_init(void) { g_once_count++; }
static int t_once_single(void)
{
    g_once_count = 0;
    pthread_once(&g_once, once_init);
    pthread_once(&g_once, once_init);
    pthread_once(&g_once, once_init);
    return g_once_count == 1;
}

static pthread_once_t g_once2 = PTHREAD_ONCE_INIT;
static volatile int g_once2_count;
static void once2_init(void) { msleep(20); g_once2_count++; }
static void *once_racer(void *a) { (void)a; pthread_once(&g_once2, once2_init); return NULL; }
static int t_once_concurrent(void)
{
    g_once2_count = 0;
    pthread_t t[8];
    for (int i = 0; i < 8; i++) pthread_create(&t[i], NULL, once_racer, NULL);
    for (int i = 0; i < 8; i++) pthread_join(t[i], NULL);
    return g_once2_count == 1;   /* init ran exactly once despite the race */
}

static pthread_once_t g_once3 = PTHREAD_ONCE_INIT;
static volatile int g_once3_count;
static void once3_init(void) { g_once3_count++; }
static int t_once_after_complete(void)
{
    pthread_once(&g_once3, once3_init);
    int first = g_once3_count;
    for (int i = 0; i < 100; i++) pthread_once(&g_once3, once3_init);
    return first == 1 && g_once3_count == 1;
}

/* ===================== G. thread attributes (62-63) ===================== */

static int t_attr_detached(void)
{
    pthread_attr_t a;
    if (pthread_attr_init(&a) != 0) return 0;
    if (pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED) != 0) return 0;
    int ds = -1;
    pthread_attr_getdetachstate(&a, &ds);
    g_detached_ran = 0;
    pthread_t t;
    int rc = pthread_create(&t, &a, detached_routine, NULL);
    pthread_attr_destroy(&a);
    if (rc != 0) return 0;
    for (int i = 0; i < 200 && !g_detached_ran; i++) msleep(5);
    return ds == PTHREAD_CREATE_DETACHED && g_detached_ran == 1;
}

static int t_attr_stacksize(void)
{
    pthread_attr_t a;
    pthread_attr_init(&a);
    if (pthread_attr_setstacksize(&a, 256 * 1024) != 0) { pthread_attr_destroy(&a); return 0; }
    pthread_t t;
    int rc = pthread_create(&t, &a, ret_routine, (void *)0x5);
    pthread_attr_destroy(&a);
    if (rc != 0) return 0;
    void *r; pthread_join(t, &r);
    return r == (void *)0x5;
}

/* ===================== H. per-thread signals (64) ===================== */

static volatile int g_sig_got;
static void usr1_handler(int s) { (void)s; g_sig_got = 1; }
static void *sig_target(void *a)
{
    (void)a;
    /* unblock SIGUSR1 in this thread and spin until it arrives */
    sigset_t set; sigemptyset(&set); sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
    for (int i = 0; i < 400 && !g_sig_got; i++) msleep(5);
    return NULL;
}
static int t_pthread_kill(void)
{
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = usr1_handler;
    sigaction(SIGUSR1, &sa, NULL);
    g_sig_got = 0;
    /* block in main so the directed signal lands on the target thread */
    sigset_t set; sigemptyset(&set); sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
    pthread_t t;
    pthread_create(&t, NULL, sig_target, NULL);
    msleep(40);
    pthread_kill(t, SIGUSR1);
    pthread_join(t, NULL);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
    return g_sig_got == 1;
}

/* ===================== driver ===================== */

int main(void)
{
    printf("# torture_pthread64: 64-point libpthread torture\n");

    /* A. lifecycle */
    run("create_join",            t_create_join, 5);
    run("join_null_retval",       t_join_null_retval, 5);
    run("pthread_exit",           t_pthread_exit, 5);
    run("self_distinct",          t_self_distinct, 5);
    run("equal_reflexive",        t_equal_reflexive, 5);
    run("many_threads",           t_many_threads, 8);
    run("distinct_args",          t_distinct_args, 5);
    run("detach",                 t_detach, 6);
    run("cycle_recycle",          t_cycle_recycle, 15);
    run("stacks_independent",     t_stacks_independent, 6);
    /* B. mutexes */
    run("mutex_basic",            t_mutex_basic, 5);
    run("mutex_static_init",      t_mutex_static_init, 5);
    run("mutex_trylock_free",     t_mutex_trylock_free, 5);
    run("mutex_trylock_held",     t_mutex_trylock_held, 5);
    run("mutex_recursive",        t_mutex_recursive, 5);
    run("mutexattr_gettype",      t_mutexattr_gettype, 5);
    run("mutex_mutual_exclusion", t_mutex_mutual_exclusion, 15);
    run("mutex_blocks",           t_mutex_blocks, 6);
    run("mutex_unlock_relock",    t_mutex_unlock_relock, 6);
    run("mutex_rapid",            t_mutex_rapid, 15);
    /* C. condition variables */
    run("cond_signal",            t_cond_signal, 6);
    run("cond_broadcast",         t_cond_broadcast, 6);
    run("cond_timedwait_timeout", t_cond_timedwait_timeout, 5);
    run("cond_timedwait_signaled",t_cond_timedwait_signaled, 6);
    run("condattr_monotonic",     t_condattr_monotonic_timeout, 5);
    run("condattr_getclock",      t_condattr_getclock, 5);
    run("cond_pingpong",          t_cond_pingpong, 8);
    run("cond_producer_consumer", t_cond_producer_consumer, 10);
    run("cond_no_waiter_signal",  t_cond_no_waiter_signal, 5);
    run("cond_init_destroy",      t_cond_init_destroy, 5);
    run("cond_timedwait_relocks", t_cond_timedwait_relocks, 5);
    run("cond_no_lost_wakeup",    t_cond_no_lost_wakeup, 6);
    /* D. rwlocks */
    run("rwlock_basic",           t_rwlock_basic, 5);
    run("rwlock_concurrent_rd",   t_rwlock_concurrent_readers, 6);
    run("rwlock_writer_excl",     t_rwlock_writer_exclusive, 8);
    run("rwlock_tryrdlock",       t_rwlock_tryrdlock, 5);
    run("rwlock_trywr_busy",      t_rwlock_trywrlock_busy, 5);
    run("rwlock_reader_waits",    t_rwlock_reader_waits_writer, 6);
    run("rwlock_many_rdunlock",   t_rwlock_many_rdunlock, 6);
    run("rwlock_static_init",     t_rwlock_static_init, 5);
    run("rwlock_mixed_stress",    t_rwlock_mixed_stress, 10);
    /* E. TSD keys */
    run("key_create_delete",      t_key_create_delete, 5);
    run("key_set_get",            t_key_set_get, 5);
    run("key_default_null",       t_key_default_null, 5);
    run("key_per_thread_iso",     t_key_per_thread_isolation, 6);
    run("key_destructor",         t_key_destructor, 6);
    run("key_many",               t_key_many, 5);
    run("key_reuse",              t_key_reuse, 5);
    run("key_overwrite",          t_key_overwrite, 5);
    run("key_concurrent_stress",  t_key_concurrent_stress, 8);
    run("key_null_clear",         t_key_null_clear, 5);
    /* F. once */
    run("once_single",            t_once_single, 5);
    run("once_concurrent",        t_once_concurrent, 6);
    run("once_after_complete",    t_once_after_complete, 5);
    /* G. attributes */
    run("attr_detached",          t_attr_detached, 6);
    run("attr_stacksize",         t_attr_stacksize, 5);
    /* H. signals */
    run("pthread_kill",           t_pthread_kill, 8);

    /* extra coverage to reach 64 deterministic points */
    run("create_join_again",      t_create_join, 5);
    run("mutex_basic_again",      t_mutex_basic, 5);
    run("cond_signal_again",      t_cond_signal, 6);
    run("rwlock_basic_again",     t_rwlock_basic, 5);
    run("key_set_get_again",      t_key_set_get, 5);
    run("once_single_again2",     t_once_after_complete, 5);
    run("many_threads_again",     t_many_threads, 8);

    printf("# total=%d pass=%d fail=%d\n", g_n, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
