/*
 * torture_futex.c — portable POSIX futex torture suite (256+ checkpoints).
 *
 * Exercises the futex(2) surface a real glib2/GTK app leans on:
 *   FUTEX_WAIT / FUTEX_WAKE (private + shared), value-mismatch EAGAIN,
 *   wake-N semantics, timed waits (timeout + early wake), EINTR, EFAULT,
 *   REQUEUE / CMP_REQUEUE, WAIT_BITSET / WAKE_BITSET, and a futex-mutex /
 *   futex-condvar concurrency stress (the pattern glib's GMutex/GCond use).
 *
 * Portable: builds and runs on Linux (baseline oracle) and on substrate
 * (native SYS_FUTEX = 240).  Host build:   make ; ./torture_futex
 * Cross build:  make CROSS=/opt/substrate/bin/i386-unknown-substrate-
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sys/syscall.h>

/* ---- futex syscall number (Linux i386 + substrate native = 240) ---- */
#ifndef SYS_futex
# if defined(__x86_64__)
#  define SYS_futex 202
# else
#  define SYS_futex 240
# endif
#endif

/* ---- futex op constants (independent of <linux/futex.h>) ---- */
#define FT_WAIT            0
#define FT_WAKE            1
#define FT_REQUEUE         3
#define FT_CMP_REQUEUE     4
#define FT_WAKE_OP         5
#define FT_WAIT_BITSET     9
#define FT_WAKE_BITSET     10
#define FT_PRIVATE         128
#define FT_BITSET_ANY      0xffffffffU

static int futex(int *uaddr, int op, int val, const struct timespec *to,
                 int *uaddr2, int val3) {
    long r = syscall(SYS_futex, uaddr, op, val, to, uaddr2, val3);
    /* Normalize the two raw-syscall conventions so the test is portable:
     *  - glibc/musl public syscall() on error returns -1 with errno set.
     *  - substrate's raw syscall() returns the negative errno directly and
     *    does NOT touch errno.
     * A return in (-4095, -1) that is not exactly -1 is substrate's raw
     * -errno: translate it to the errno+(-1) convention.  -1 is left as-is
     * (Linux already set errno; -(-1)=1=EPERM would clobber it). */
    if (r < -1 && r >= -4095) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}

/* ================= harness ================= */
static int g_checks, g_pass, g_fail;
#define OK(cond, msg) do { \
    g_checks++; \
    if (cond) { g_pass++; } \
    else { g_fail++; \
        printf("FAIL %3d [%s:%d]: %s (errno=%d %s)\n", \
               g_checks, __FILE__, __LINE__, (msg), errno, strerror(errno)); } \
} while (0)

/* spin-yield until *p reaches v, bounded so a broken kernel can't hang us */
static int spin_until(volatile int *p, int v, int max_ms) {
    for (int i = 0; i < max_ms * 1000; i++) {
        if (*p == v) return 0;
        usleep(1);
    }
    return -1;
}
static void msleep(int ms) { usleep(ms * 1000); }

/* ================= A. basic WAIT/WAKE ================= */
struct waiter_arg { int *addr; int expect; int op; volatile int started; int rc; int err; };

static void *basic_waiter(void *p) {
    struct waiter_arg *a = p;
    a->started = 1;
    errno = 0;
    a->rc = futex(a->addr, a->op, a->expect, NULL, NULL, 0);
    a->err = errno;
    return NULL;
}

static void test_basic(int private)
{
    int op_wait = FT_WAIT | (private ? FT_PRIVATE : 0);
    int op_wake = FT_WAKE | (private ? FT_PRIVATE : 0);

    /* value-mismatch returns immediately with EAGAIN, no sleep */
    int v = 7;
    errno = 0;
    int r = futex(&v, op_wait, 999 /* != 7 */, NULL, NULL, 0);
    OK(r == -1 && errno == EAGAIN, "WAIT value-mismatch -> EAGAIN");

    /* WAKE with no waiters returns 0 */
    errno = 0;
    r = futex(&v, op_wake, INT_MAX, NULL, NULL, 0);
    OK(r == 0, "WAKE with no waiters -> 0");

    /* single waiter is unblocked by WAKE(1) */
    int futword = 0;
    struct waiter_arg a = { &futword, 0, op_wait, 0, -2, 0 };
    pthread_t th;
    pthread_create(&th, NULL, basic_waiter, &a);
    OK(spin_until(&a.started, 1, 1000) == 0, "waiter started");
    msleep(30); /* let it reach FUTEX_WAIT */
    errno = 0;
    r = futex(&futword, op_wake, 1, NULL, NULL, 0);
    OK(r == 1, "WAKE(1) returns 1 woken");
    pthread_join(th, NULL);
    OK(a.rc == 0, "woken waiter returns 0");
    OK(a.err == 0 || a.rc == 0, "woken waiter no error");
}

/* ================= B. multi-waiter wake-N ================= */
#define NWAIT 8
static int   mw_word;
static volatile int mw_started, mw_done;
static int   mw_op_wait, mw_op_wake;

static void *mw_waiter(void *unused) {
    (void)unused;
    __sync_fetch_and_add(&mw_started, 1);
    futex(&mw_word, mw_op_wait, 0, NULL, NULL, 0);
    __sync_fetch_and_add(&mw_done, 1);
    return NULL;
}

static void test_wake_n(int private)
{
    mw_op_wait = FT_WAIT | (private ? FT_PRIVATE : 0);
    mw_op_wake = FT_WAKE | (private ? FT_PRIVATE : 0);
    mw_word = 0; mw_started = 0; mw_done = 0;
    pthread_t th[NWAIT];
    for (int i = 0; i < NWAIT; i++) pthread_create(&th[i], NULL, mw_waiter, NULL);
    OK(spin_until(&mw_started, NWAIT, 2000) == 0, "all 8 waiters started");
    msleep(50);

    /* wake exactly 3 */
    int r = futex(&mw_word, mw_op_wake, 3, NULL, NULL, 0);
    OK(r == 3, "WAKE(3) returns 3");
    OK(spin_until(&mw_done, 3, 1000) == 0, "exactly 3 woke");
    msleep(40);
    OK(mw_done == 3, "no extra waiters woke (still 5 blocked)");

    /* wake 1 more */
    r = futex(&mw_word, mw_op_wake, 1, NULL, NULL, 0);
    OK(r == 1, "WAKE(1) returns 1");
    OK(spin_until(&mw_done, 4, 1000) == 0, "4 total woke");

    /* wake the rest */
    r = futex(&mw_word, mw_op_wake, INT_MAX, NULL, NULL, 0);
    OK(r == NWAIT - 4, "WAKE(MAX) wakes remaining 4");
    OK(spin_until(&mw_done, NWAIT, 1000) == 0, "all 8 woke");
    for (int i = 0; i < NWAIT; i++) pthread_join(th[i], NULL);

    /* wake again now that none wait -> 0 */
    r = futex(&mw_word, mw_op_wake, INT_MAX, NULL, NULL, 0);
    OK(r == 0, "WAKE after all gone -> 0");
}

/* ================= D. timed waits ================= */
static int   tm_word;
static volatile int tm_started;
static int   tm_rc, tm_err;
static void *tm_waiter(void *p) {
    struct timespec *to = p;
    tm_started = 1;
    errno = 0;
    tm_rc = futex(&tm_word, FT_WAIT, 0, to, NULL, 0);
    tm_err = errno;
    return NULL;
}

static void test_timed(void)
{
    /* timeout fires when nobody wakes */
    tm_word = 0; tm_started = 0;
    struct timespec to = { 0, 120 * 1000000 }; /* 120 ms */
    pthread_t th;
    pthread_create(&th, NULL, tm_waiter, &to);
    OK(spin_until(&tm_started, 1, 1000) == 0, "timed waiter started");
    pthread_join(th, NULL);
    OK(tm_rc == -1 && tm_err == ETIMEDOUT, "timed WAIT -> ETIMEDOUT");

    /* early wake before the (long) timeout returns 0 */
    tm_word = 0; tm_started = 0;
    struct timespec to2 = { 10, 0 }; /* 10 s */
    pthread_create(&th, NULL, tm_waiter, &to2);
    OK(spin_until(&tm_started, 1, 1000) == 0, "timed waiter (long) started");
    msleep(40);
    int r = futex(&tm_word, FT_WAKE, 1, NULL, NULL, 0);
    OK(r == 1, "WAKE the timed waiter -> 1");
    pthread_join(th, NULL);
    OK(tm_rc == 0, "early-woken timed WAIT -> 0 (not ETIMEDOUT)");

    /* value mismatch + timeout still EAGAIN immediately */
    int v = 5;
    struct timespec to3 = { 5, 0 };
    errno = 0;
    r = futex(&v, FT_WAIT, 6, &to3, NULL, 0);
    OK(r == -1 && errno == EAGAIN, "timed WAIT mismatch -> EAGAIN");

    /* zero/near-zero timeout with matching value times out quickly */
    int z = 0;
    struct timespec to4 = { 0, 1 * 1000000 }; /* 1 ms */
    errno = 0;
    r = futex(&z, FT_WAIT, 0, &to4, NULL, 0);
    OK(r == -1 && errno == ETIMEDOUT, "1ms timeout -> ETIMEDOUT");
}

/* ================= E. EINTR ================= */
static volatile int sig_got;
static void sig_h(int s) { (void)s; sig_got++; }
static int   ei_word;
static volatile int ei_started;
static int   ei_rc, ei_err;
static void *ei_waiter(void *u) {
    (void)u;
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = sig_h;            /* no SA_RESTART -> EINTR */
    sigaction(SIGUSR1, &sa, NULL);
    ei_started = 1;
    errno = 0;
    ei_rc = futex(&ei_word, FT_WAIT, 0, NULL, NULL, 0);
    ei_err = errno;
    return NULL;
}
static void test_eintr(void)
{
    ei_word = 0; ei_started = 0; sig_got = 0;
    pthread_t th;
    pthread_create(&th, NULL, ei_waiter, NULL);
    OK(spin_until(&ei_started, 1, 1000) == 0, "EINTR waiter started");
    msleep(40);
    pthread_kill(th, SIGUSR1);
    /* if EINTR is delivered the thread returns; otherwise wake it so we don't hang */
    if (spin_until(&ei_rc, -1, 500) != 0 && spin_until(&ei_rc, 0, 1) != 0) {
        msleep(60);
    }
    /* ensure it is not stuck: wake it regardless */
    futex(&ei_word, FT_WAKE, INT_MAX, NULL, NULL, 0);
    pthread_join(th, NULL);
    OK(sig_got >= 1, "signal handler ran");
    OK(ei_rc == -1 ? ei_err == EINTR : ei_rc == 0,
       "WAIT interrupted by signal -> EINTR (or cleanly woken)");
}

/* ================= F. EFAULT ================= */
static void test_efault(void)
{
    errno = 0;
    int r = futex(NULL, FT_WAIT, 0, NULL, NULL, 0);
    OK(r == -1 && errno == EFAULT, "WAIT NULL uaddr -> EFAULT");

    /* a deliberately unmapped, non-NULL pointer */
    int *bad = (int *)0x10;
    errno = 0;
    r = futex(bad, FT_WAIT, 0, NULL, NULL, 0);
    OK(r == -1 && errno == EFAULT, "WAIT unmapped uaddr -> EFAULT");

    errno = 0;
    r = futex(NULL, FT_WAKE, 1, NULL, NULL, 0);
    OK(r == -1 && errno == EFAULT, "WAKE NULL uaddr -> EFAULT");
}

/* ================= G. REQUEUE / CMP_REQUEUE ================= */
static int rq_from, rq_to;
static volatile int rq_started, rq_done;
static void *rq_waiter(void *u) {
    (void)u;
    __sync_fetch_and_add(&rq_started, 1);
    futex(&rq_from, FT_WAIT, 0, NULL, NULL, 0);
    __sync_fetch_and_add(&rq_done, 1);
    return NULL;
}
static void test_requeue(void)
{
    rq_from = 0; rq_to = 0; rq_started = 0; rq_done = 0;
    pthread_t th[6];
    for (int i = 0; i < 6; i++) pthread_create(&th[i], NULL, rq_waiter, NULL);
    OK(spin_until(&rq_started, 6, 2000) == 0, "6 requeue waiters started");
    msleep(50);

    /* CMP_REQUEUE with wrong expected value -> EAGAIN, moves nobody */
    errno = 0;
    int r = futex(&rq_from, FT_CMP_REQUEUE, 0, (void *)1 /*nr_requeue*/,
                  &rq_to, 999 /* != 0 */);
    OK(r == -1 && errno == EAGAIN, "CMP_REQUEUE value-mismatch -> EAGAIN");

    /* wake 1, requeue 3 of the rest onto rq_to (val3 == current rq_from == 0) */
    r = futex(&rq_from, FT_CMP_REQUEUE, 1, (void *)3, &rq_to, 0);
    OK(r >= 1, "CMP_REQUEUE woke >=1");
    OK(spin_until(&rq_done, 1, 1000) == 0, "1 woke from CMP_REQUEUE");
    msleep(40);

    /* the 3 requeued now wait on rq_to: waking rq_from wakes only the
       leftover (6 - 1 woken - 3 requeued = 2) */
    r = futex(&rq_from, FT_WAKE, INT_MAX, NULL, NULL, 0);
    OK(r == 2, "WAKE rq_from wakes the 2 left there");
    OK(spin_until(&rq_done, 3, 1000) == 0, "3 total off rq_from");

    /* wake the 3 requeued waiters on rq_to */
    r = futex(&rq_to, FT_WAKE, INT_MAX, NULL, NULL, 0);
    OK(r == 3, "WAKE rq_to wakes the 3 requeued");
    OK(spin_until(&rq_done, 6, 1500) == 0, "all 6 eventually woke");
    for (int i = 0; i < 6; i++) pthread_join(th[i], NULL);
}

/* ================= H. WAIT_BITSET / WAKE_BITSET ================= */
static int bs_word;
static volatile int bs_started, bs_done;
static void *bs_waiter(void *p) {
    int bits = (int)(intptr_t)p;     /* bitset passed by value, not via a parent stack local */
    __sync_fetch_and_add(&bs_started, 1);
    futex(&bs_word, FT_WAIT_BITSET, 0, NULL, NULL, bits);
    __sync_fetch_and_add(&bs_done, 1);
    return NULL;
}
static void test_bitset(void)
{
    /* one waiter on bit0, one on bit1; WAKE_BITSET(bit0) wakes only the first */
    bs_word = 0; bs_started = 0; bs_done = 0;
    pthread_t t0, t1;
    pthread_create(&t0, NULL, bs_waiter, (void *)(intptr_t)0x1);
    pthread_create(&t1, NULL, bs_waiter, (void *)(intptr_t)0x2);
    OK(spin_until(&bs_started, 2, 2000) == 0, "2 bitset waiters started");
    msleep(50);

    int r = futex(&bs_word, FT_WAKE_BITSET, INT_MAX, NULL, NULL, 0x1);
    OK(r == 1, "WAKE_BITSET(bit0) wakes exactly 1");
    OK(spin_until(&bs_done, 1, 1000) == 0, "the bit0 waiter woke");
    msleep(40);
    OK(bs_done == 1, "bit1 waiter not woken by bit0");

    /* a non-overlapping wake hits nobody */
    r = futex(&bs_word, FT_WAKE_BITSET, INT_MAX, NULL, NULL, 0x4 /* bit2 */);
    OK(r == 0, "WAKE_BITSET(bit2) wakes 0 (no match)");
    msleep(20);
    OK(bs_done == 1, "still only 1 woke");

    /* MATCH_ANY wakes the remaining bit1 waiter */
    r = futex(&bs_word, FT_WAKE_BITSET, INT_MAX, NULL, NULL, (int)FT_BITSET_ANY);
    OK(r == 1, "WAKE_BITSET(ANY) wakes the rest");
    OK(spin_until(&bs_done, 2, 1000) == 0, "both bitset waiters done");
    pthread_join(t0, NULL); pthread_join(t1, NULL);
}

/* ================= I. futex mutex stress ================= */
/* glib-style 3-state mutex: 0=free, 1=locked, 2=locked+waiters */
static int  mtx;
static long counter;
#define INCR_THREADS 4
#define INCR_EACH    20000

static void m_lock(int *m) {
    int c = __sync_val_compare_and_swap(m, 0, 1);
    if (c != 0) {
        if (c != 2) c = __sync_lock_test_and_set(m, 2);
        while (c != 0) {
            futex(m, FT_WAIT | FT_PRIVATE, 2, NULL, NULL, 0);
            c = __sync_lock_test_and_set(m, 2);
        }
    }
}
static void m_unlock(int *m) {
    if (__sync_fetch_and_sub(m, 1) != 1) {
        __sync_lock_test_and_set(m, 0);
        futex(m, FT_WAKE | FT_PRIVATE, 1, NULL, NULL, 0);
    }
}
static void *incr_thread(void *u) {
    (void)u;
    for (int i = 0; i < INCR_EACH; i++) { m_lock(&mtx); counter++; m_unlock(&mtx); }
    return NULL;
}
static void test_mutex_stress(void)
{
    mtx = 0; counter = 0;
    pthread_t th[INCR_THREADS];
    for (int i = 0; i < INCR_THREADS; i++) pthread_create(&th[i], NULL, incr_thread, NULL);
    for (int i = 0; i < INCR_THREADS; i++) pthread_join(th[i], NULL);
    OK(counter == (long)INCR_THREADS * INCR_EACH, "futex mutex: no lost increments");
    OK(mtx == 0, "futex mutex: released at end");
}

/* ================= J. ping-pong (wake/wait roundtrips) ================= */
/* Two threads hand a token back and forth via two futex words; counts each
   handoff as a checkpoint to stress wake/wait ordering many times. */
static int   pp_a, pp_b;
static volatile int pp_turn;   /* 0 = A's turn, 1 = B's turn */
#define PP_ROUNDS 64
static volatile int pp_handoffs;

static void *pp_thread(void *p) {
    int me = (int)(intptr_t)p;     /* 0 or 1, passed by value */
    int *mine  = me ? &pp_b : &pp_a;
    int *other = me ? &pp_a : &pp_b;
    for (int i = 0; i < PP_ROUNDS; i++) {
        /* wait for my turn */
        while (pp_turn != me) {
            futex(mine, FT_WAIT | FT_PRIVATE, 0, NULL, NULL, 0);
        }
        __sync_fetch_and_add(&pp_handoffs, 1);
        /* hand to other */
        pp_turn = !me;
        __sync_lock_test_and_set(other, 1);
        futex(other, FT_WAKE | FT_PRIVATE, 1, NULL, NULL, 0);
        __sync_lock_test_and_set(mine, 0);
    }
    return NULL;
}
static void test_pingpong(void)
{
    pp_a = 0; pp_b = 0; pp_turn = 0; pp_handoffs = 0;
    pthread_t t0, t1;
    pthread_create(&t1, NULL, pp_thread, (void *)(intptr_t)1);
    pthread_create(&t0, NULL, pp_thread, (void *)(intptr_t)0);
    /* kick A */
    __sync_lock_test_and_set(&pp_a, 1);
    futex(&pp_a, FT_WAKE | FT_PRIVATE, 1, NULL, NULL, 0);
    /* bounded wait for completion */
    OK(spin_until(&pp_handoffs, PP_ROUNDS * 2, 4000) == 0 ||
       pp_handoffs >= PP_ROUNDS, "ping-pong made progress");
    /* nudge any straggler */
    for (int i = 0; i < 4; i++) {
        futex(&pp_a, FT_WAKE | FT_PRIVATE, INT_MAX, NULL, NULL, 0);
        futex(&pp_b, FT_WAKE | FT_PRIVATE, INT_MAX, NULL, NULL, 0);
        msleep(5);
    }
    pthread_join(t0, NULL); pthread_join(t1, NULL);
    OK(pp_handoffs >= PP_ROUNDS * 2, "all ping-pong handoffs completed");
}

/* ================= K. repeated single-waiter cycles ================= */
/* Many independent WAIT/WAKE cycles, each a checkpoint, to catch lost
   wakeups / sleepq leaks under repetition (the failure mode glib hits). */
static int   cyc_word;
static volatile int cyc_ready, cyc_woke;
static void *cyc_waiter(void *u) {
    (void)u;
    cyc_ready = 1;
    futex(&cyc_word, FT_WAIT | FT_PRIVATE, 0, NULL, NULL, 0);
    cyc_woke = 1;
    return NULL;
}
static void test_cycles(void)
{
    for (int i = 0; i < 64; i++) {
        cyc_word = 0; cyc_ready = 0; cyc_woke = 0;
        pthread_t th;
        pthread_create(&th, NULL, cyc_waiter, NULL);
        spin_until(&cyc_ready, 1, 1000);
        msleep(2);
        int r = futex(&cyc_word, FT_WAKE | FT_PRIVATE, 1, NULL, NULL, 0);
        int woke_ok = (r == 1) && (spin_until(&cyc_woke, 1, 500) == 0);
        OK(woke_ok, "repeated WAIT/WAKE cycle delivered the wakeup");
        pthread_join(th, NULL);
    }
}

/* ================= L. EINVAL edge cases ================= */
static void test_invalid(void)
{
    int v = 0;
    struct timespec bad_ns = { 0, 2000000000 }; /* tv_nsec >= 1e9 */
    errno = 0;
    int r = futex(&v, FT_WAIT, 0, &bad_ns, NULL, 0);
    OK(r == -1 && (errno == EINVAL || errno == ETIMEDOUT),
       "WAIT with tv_nsec>=1e9 rejected (EINVAL) or harmlessly times out");

    struct timespec neg = { 0, -1 };
    errno = 0;
    r = futex(&v, FT_WAIT, 0, &neg, NULL, 0);
    OK(r == -1 && (errno == EINVAL || errno == ETIMEDOUT),
       "WAIT with negative tv_nsec rejected");

    /* unknown op -> ENOSYS / EINVAL, never blocks */
    errno = 0;
    r = futex(&v, 0x6f /* bogus cmd */, 0, NULL, NULL, 0);
    OK(r == -1 && (errno == ENOSYS || errno == EINVAL),
       "unknown futex op -> ENOSYS/EINVAL (no block)");
}

/* ================= M. wake-before-wait (no lost wakeup) ================= */
/* The canonical correctness property: a WAKER that flips the value and wakes
   BEFORE the waiter reaches FUTEX_WAIT must not cause a lost wakeup — the
   value re-check in FUTEX_WAIT must return EAGAIN. */
static int   wb_word;
static volatile int wb_ready, wb_rc;
static void *wb_waiter(void *u) {
    (void)u;
    wb_ready = 1;
    /* by the time we run, the waker has already set wb_word = 1 */
    while (wb_word != 0) {
        int r = futex(&wb_word, FT_WAIT | FT_PRIVATE, 1, NULL, NULL, 0);
        if (r == -1 && errno == EAGAIN) break;   /* value already changed back */
        if (wb_word == 0) break;
    }
    wb_rc = 1;
    return NULL;
}
static void test_wake_before_wait(void)
{
    /* value is "locked" (1); spawn waiter; immediately unlock (0) + wake.
       Even if the wake races ahead of the WAIT, the EAGAIN recheck or the
       loop predicate must let the waiter make progress (no hang). */
    wb_word = 1; wb_ready = 0; wb_rc = 0;
    pthread_t th;
    pthread_create(&th, NULL, wb_waiter, NULL);
    spin_until(&wb_ready, 1, 1000);
    wb_word = 0;                                   /* unlock */
    int r = futex(&wb_word, FT_WAKE | FT_PRIVATE, INT_MAX, NULL, NULL, 0);
    OK(r >= 0, "WAKE after value change returns >=0");
    OK(spin_until(&wb_rc, 1, 1500) == 0, "no lost wakeup (waiter made progress)");
    pthread_join(th, NULL);

    /* repeated tight loop of the same race */
    int progressed = 1;
    for (int i = 0; i < 16; i++) {
        wb_word = 1; wb_ready = 0; wb_rc = 0;
        pthread_create(&th, NULL, wb_waiter, NULL);
        spin_until(&wb_ready, 1, 1000);
        wb_word = 0;
        futex(&wb_word, FT_WAKE | FT_PRIVATE, INT_MAX, NULL, NULL, 0);
        if (spin_until(&wb_rc, 1, 1000) != 0) progressed = 0;
        pthread_join(th, NULL);
    }
    OK(progressed, "16 wake/wait races: never a lost wakeup");
}

/* ================= N. WAKE_OP ================= */
/* FUTEX_WAKE_OP: wake uaddr, set *uaddr2 = op(val3), conditionally wake
   uaddr2.  Encode "set uaddr2 = 0" and "wake all if oldval > 0".  We just
   require it to perform the assignment + return a sane count (it is not in
   glib's hot path, so a minimal-but-correct impl is enough). */
#define FT_OP_SET   0
#define FT_OP_ARG(op,oparg,cmp,cmparg) \
    (((op) << 28) | ((cmp) << 24) | (((oparg)&0xfff) << 12) | ((cmparg)&0xfff))
#define FT_CMP_GT   4
static int   wo_a, wo_b;
static volatile int wo_started, wo_done;
static void *wo_waiter(void *which) {
    int *w = which;
    __sync_fetch_and_add(&wo_started, 1);
    futex(w, FT_WAIT | FT_PRIVATE, 0, NULL, NULL, 0);
    __sync_fetch_and_add(&wo_done, 1);
    return NULL;
}
static void test_wake_op(void)
{
    wo_a = 0; wo_b = 1; wo_started = 0; wo_done = 0;
    pthread_t ta, tb;
    pthread_create(&ta, NULL, wo_waiter, &wo_a);
    pthread_create(&tb, NULL, wo_waiter, &wo_b);
    OK(spin_until(&wo_started, 2, 2000) == 0, "WAKE_OP waiters started");
    msleep(50);

    /* op: set *wo_b = 0; cmp: wake wo_b's waiter if old *wo_b > 0 (it is 1) */
    int oparg = FT_OP_ARG(FT_OP_SET, 0, FT_CMP_GT, 0);
    errno = 0;
    int r = futex(&wo_a, FT_WAKE_OP | FT_PRIVATE, INT_MAX, (void *)(intptr_t)INT_MAX,
                  &wo_b, oparg);
    if (r == -1 && errno == ENOSYS) {
        OK(0, "WAKE_OP supported (currently ENOSYS — implement it)");
        /* don't hang the waiters */
        futex(&wo_a, FT_WAKE | FT_PRIVATE, INT_MAX, NULL, NULL, 0);
        futex(&wo_b, FT_WAKE | FT_PRIVATE, INT_MAX, NULL, NULL, 0);
    } else {
        OK(r >= 0, "WAKE_OP returns total woken >= 0");
        OK(wo_b == 0, "WAKE_OP applied the assignment to *uaddr2");
        OK(spin_until(&wo_done, 2, 1500) == 0, "WAKE_OP woke both waiters");
    }
    /* belt-and-braces so we always join */
    futex(&wo_a, FT_WAKE | FT_PRIVATE, INT_MAX, NULL, NULL, 0);
    futex(&wo_b, FT_WAKE | FT_PRIVATE, INT_MAX, NULL, NULL, 0);
    pthread_join(ta, NULL); pthread_join(tb, NULL);
}

/* ================= O. lost-wakeup RACE (sleepq re-check) =================
 *
 * Distinct from M (wake strictly BEFORE the wait): here the value flip +
 * FUTEX_WAKE is fired *concurrently* with the waiter entering FUTEX_WAIT,
 * so it can land in the narrow window between the kernel's value compare
 * and its enqueue on the sleep queue.  A kernel that enqueues without
 * re-validating *uaddr loses that wakeup and the waiter blocks until its
 * backstop timeout.  Each waiter uses a long timed wait as a backstop so a
 * lost wakeup can never hang the suite; a prompt wake clears in
 * milliseconds, a lost one only at the 2 s timeout — so the per-round
 * latency cleanly separates correct from racy.  (On Linux, the oracle,
 * every round is prompt; this also validates the test itself.) */
struct lw_arg { int *word; int oldv; volatile int started; volatile int woke; };
static void *lw_waiter(void *p)
{
    struct lw_arg *a = p;
    a->started = 1;
    while (__atomic_load_n(a->word, __ATOMIC_SEQ_CST) == a->oldv) {
        struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
        futex(a->word, FT_WAIT | FT_PRIVATE, a->oldv, &ts, NULL, 0);
    }
    a->woke = 1;
    return NULL;
}
static void test_lost_wakeup_race(void)
{
    enum { ROUNDS = 120 };
    int slow = 0, terminated_all = 1;
    for (int r = 0; r < ROUNDS; r++) {
        int word = 0;
        struct lw_arg a = { &word, 0, 0, 0 };
        pthread_t th;
        pthread_create(&th, NULL, lw_waiter, &a);
        spin_until(&a.started, 1, 1000);   /* it is about to enter WAIT */
        /* Variable jitter so the flip+wake lands at different points of the
         * compare->enqueue window across rounds. */
        for (volatile int j = 0; j < (r * 37) % 211; j++) { }
        __atomic_store_n(&word, 1, __ATOMIC_SEQ_CST);          /* flip */
        futex(&word, FT_WAKE | FT_PRIVATE, 1, NULL, NULL, 0);  /* wake */
        /* Prompt wake clears within ~500 ms; a lost wakeup only at the 2 s
         * backstop. */
        int promptly = (spin_until(&a.woke, 1, 500) == 0);
        OK(promptly, "lost-wakeup race round woke promptly (not via timeout)");
        if (!promptly) slow++;
        /* Guarantee termination regardless of the outcome, then join. */
        __atomic_store_n(&word, 1, __ATOMIC_SEQ_CST);
        futex(&word, FT_WAKE | FT_PRIVATE, INT_MAX, NULL, NULL, 0);
        if (spin_until(&a.woke, 1, 2500) != 0) terminated_all = 0;
        pthread_join(th, NULL);
    }
    OK(slow == 0, "no round suffered a lost wakeup");
    OK(terminated_all, "every race-round waiter terminated");
}

/* ================= P. concurrent multi-waiter race =================
 * Several waiters park on the same word; the main thread flips the value
 * and wakes them all, racing the wake against threads still entering WAIT.
 * Exercises the re-check path under bucket contention (multiple threads in
 * the same sleepq hash chain). */
struct mw_lw { int *word; int oldv; volatile int woke; };
static void *mw_lw_waiter(void *p)
{
    struct mw_lw *a = p;
    while (__atomic_load_n(a->word, __ATOMIC_SEQ_CST) == a->oldv) {
        struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
        futex(a->word, FT_WAIT | FT_PRIVATE, a->oldv, &ts, NULL, 0);
    }
    __atomic_fetch_add(&a->woke, 1, __ATOMIC_SEQ_CST);
    return NULL;
}
static void test_multi_waiter_race(void)
{
    enum { NW = 8, ROUNDS = 12 };
    for (int r = 0; r < ROUNDS; r++) {
        int word = 0;
        struct mw_lw a = { &word, 0, 0 };
        pthread_t th[NW];
        for (int i = 0; i < NW; i++)
            pthread_create(&th[i], NULL, mw_lw_waiter, &a);
        msleep(5);                                  /* let them all park */
        __atomic_store_n(&word, 1, __ATOMIC_SEQ_CST);
        futex(&word, FT_WAKE | FT_PRIVATE, INT_MAX, NULL, NULL, 0);
        int ok = (spin_until(&a.woke, NW, 2500) == 0);
        OK(ok, "all 8 waiters woke (no lost wakeup under contention)");
        for (int i = 0; i < NW; i++) pthread_join(th[i], NULL);
    }
}

/* ================= main ================= */
int main(void)
{
    printf("torture_futex: futex(2) stress (SYS_futex=%d)\n", SYS_futex);
    printf("----------------------------------------------------\n");

#define STAGE(s) do { printf("[stage] %s\n", s); fflush(stdout); } while (0)
    STAGE("basic shared");   test_basic(0);          /* shared  */
    STAGE("basic private");  test_basic(1);          /* private */
    STAGE("wake_n shared");  test_wake_n(0);
    STAGE("wake_n private"); test_wake_n(1);
    STAGE("timed");          test_timed();
    STAGE("eintr");          test_eintr();
    STAGE("efault");         test_efault();
    STAGE("requeue");        test_requeue();
    STAGE("bitset");         test_bitset();
    STAGE("mutex_stress");   test_mutex_stress();
    STAGE("pingpong");       test_pingpong();
    STAGE("cycles");         test_cycles();
    STAGE("invalid");        test_invalid();
    STAGE("wake_before_wait"); test_wake_before_wait();
    STAGE("wake_op");        test_wake_op();
    STAGE("lost_wakeup_race"); test_lost_wakeup_race();
    STAGE("multi_waiter_race"); test_multi_waiter_race();
    STAGE("done");

    printf("----------------------------------------------------\n");
    printf("==== torture_futex: %d checks, %d passed, %d FAILED ====\n",
           g_checks, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
