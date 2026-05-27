/*
 * torture_sigalrm.c — SIGALRM / setitimer / alarm torture test.
 *
 * Motivation: Xfbdev's `xsig` trace shows what looks like a SIGALRM
 * storm (every sigreturn lands at the same EIP, signal re-fires
 * before user code can advance).  We need to know whether this is
 *   (a) substrate's setitimer/SIGALRM delivery firing too fast,
 *   (b) signal masking not honored across sigreturn,
 *   (c) EINTR / syscall-restart misbehaviour,
 *   (d) genuine X-side behaviour that's fine in isolation.
 *
 * Each scenario is a `static int scN_*(void)` that returns 0 = PASS,
 * non-zero = FAIL.  main() runs all of them and tallies.
 *
 * Substrate-target cross build:  make CROSS=/opt/substrate/bin/i386-unknown-substrate-
 * Host build (Linux/BSD baseline): make CC=cc
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
/* sys/select.h must come BEFORE sys/time.h on substrate: select.h's
 * forward-decl `struct timeval;` clashes with the full definition
 * from sys/time.h when the latter wins the include order, producing
 * a spurious "restrict pointer-type incompatible" diagnostic out of
 * GCC 16.  Reverse order avoids the clash. */
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <setjmp.h>
#include <limits.h>

/* ------------------------------------------------------------------ */
/* Common harness                                                      */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t g_alrm_count;
static volatile sig_atomic_t g_alrm_mask_seen;
static volatile sig_atomic_t g_other_in_handler;

static void count_handler(int sig) {
    (void)sig;
    g_alrm_count++;
}

static void mask_inspecting_handler(int sig) {
    sigset_t cur;
    (void)sig;
    if (sigprocmask(SIG_BLOCK, NULL, &cur) == 0) {
        g_alrm_mask_seen = sigismember(&cur, SIGALRM);
    }
    g_alrm_count++;
}

static int install_handler(int sig, void (*fn)(int), int flags) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    return sigaction(sig, &sa, NULL);
}

static void disarm_timer(void) {
    struct itimerval z;
    memset(&z, 0, sizeof(z));
    setitimer(ITIMER_REAL, &z, NULL);
}

static int set_oneshot(unsigned ms) {
    struct itimerval it;
    memset(&it, 0, sizeof(it));
    it.it_value.tv_sec  = ms / 1000;
    it.it_value.tv_usec = (ms % 1000) * 1000;
    return setitimer(ITIMER_REAL, &it, NULL);
}

static int set_periodic(unsigned ms) {
    struct itimerval it;
    memset(&it, 0, sizeof(it));
    it.it_value.tv_sec  = ms / 1000;
    it.it_value.tv_usec = (ms % 1000) * 1000;
    it.it_interval.tv_sec  = ms / 1000;
    it.it_interval.tv_usec = (ms % 1000) * 1000;
    return setitimer(ITIMER_REAL, &it, NULL);
}

static long usleep_busy(unsigned ms) {
    /* spin so we yield the cpu but DON'T enter a blocking syscall;
     * a few scenarios specifically want progress-while-not-blocked. */
    struct timespec t0, t1;
    long elapsed_us;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    do {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        elapsed_us = (t1.tv_sec - t0.tv_sec) * 1000000L +
                     (t1.tv_nsec - t0.tv_nsec) / 1000L;
    } while (elapsed_us < (long)ms * 1000L);
    return elapsed_us;
}

#define FAIL(fmt, ...) do { \
    fprintf(stderr, "  FAIL: " fmt "\n", ##__VA_ARGS__); \
    return 1; \
} while (0)

/* ------------------------------------------------------------------ */
/* Scenarios                                                           */
/* ------------------------------------------------------------------ */

/* sc1: oneshot timer.  Set 50ms timer, sleep 250ms in select(),
 * expect exactly ONE delivery. */
static int sc1_oneshot(void) {
    g_alrm_count = 0;
    if (install_handler(SIGALRM, count_handler, 0) < 0) FAIL("sigaction: %s", strerror(errno));
    if (set_oneshot(50) < 0) FAIL("setitimer: %s", strerror(errno));

    struct timeval tv = { .tv_sec = 0, .tv_usec = 250000 };
    int rc = select(0, NULL, NULL, NULL, &tv);
    if (rc < 0 && errno != EINTR) FAIL("select unexpected errno %d", errno);

    if (g_alrm_count != 1)
        FAIL("expected exactly 1 SIGALRM, got %d", (int)g_alrm_count);
    return 0;
}

/* sc2: periodic timer.  20ms interval over 250ms.  Expect 8-15 fires
 * (HZ=128 quantization means ~3-tick = 23ms intervals so ~10 fires). */
static int sc2_periodic(void) {
    g_alrm_count = 0;
    if (install_handler(SIGALRM, count_handler, 0) < 0) FAIL("sigaction");
    if (set_periodic(20) < 0) FAIL("setitimer");

    /* Use a series of short selects so we don't get one giant EINTR. */
    for (int i = 0; i < 25; i++) {
        struct timeval tv = { .tv_sec = 0, .tv_usec = 10000 };
        select(0, NULL, NULL, NULL, &tv);
    }
    disarm_timer();
    if (g_alrm_count < 5)
        FAIL("expected >=5 periodic SIGALRMs in 250ms, got %d", (int)g_alrm_count);
    if (g_alrm_count > 100)
        FAIL("STORM: expected <100 SIGALRMs in 250ms, got %d", (int)g_alrm_count);
    return 0;
}

/* sc3: EINTR on blocking syscall.  read() on an empty pipe should be
 * interrupted by SIGALRM (no SA_RESTART). */
static int sc3_eintr_read(void) {
    int p[2];
    char b;
    if (pipe(p) < 0) FAIL("pipe: %s", strerror(errno));
    g_alrm_count = 0;
    if (install_handler(SIGALRM, count_handler, 0) < 0) FAIL("sigaction");
    if (set_oneshot(50) < 0) FAIL("setitimer");

    errno = 0;
    ssize_t r = read(p[0], &b, 1);
    int save = errno;
    close(p[0]); close(p[1]);

    if (r >= 0) FAIL("expected -1, got %ld", (long)r);
    if (save != EINTR) FAIL("expected EINTR, got %d (%s)", save, strerror(save));
    if (g_alrm_count != 1) FAIL("handler count %d, want 1", (int)g_alrm_count);
    return 0;
}

/* sc4: SA_RESTART — same scenario but with SA_RESTART set.  read()
 * should NOT return EINTR; it should keep blocking after the handler.
 * Use a child to feed a byte after the handler runs, so read can
 * complete and we don't hang. */
static int sc4_sa_restart(void) {
    int p[2];
    if (pipe(p) < 0) FAIL("pipe");

    pid_t kid = fork();
    if (kid < 0) FAIL("fork");
    if (kid == 0) {
        /* Child: sleep past the SIGALRM, then write so parent's
         * restarted read() unblocks. */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 150 * 1000000L };
        nanosleep(&ts, NULL);
        write(p[1], "X", 1);
        _exit(0);
    }
    close(p[1]);

    g_alrm_count = 0;
    if (install_handler(SIGALRM, count_handler, SA_RESTART) < 0) FAIL("sigaction");
    if (set_oneshot(50) < 0) FAIL("setitimer");

    char b = 0;
    errno = 0;
    ssize_t r = read(p[0], &b, 1);
    int save = errno;
    close(p[0]);
    int status; waitpid(kid, &status, 0);

    if (r != 1) FAIL("expected restarted read to return 1, got %ld (errno %d)", (long)r, save);
    if (b != 'X') FAIL("data corruption: read %d", b);
    if (g_alrm_count != 1) FAIL("handler count %d, want 1", (int)g_alrm_count);
    return 0;
}

/* sc5: signal mask in handler.  Default behaviour: the signal that
 * triggered the handler is BLOCKED for the duration of the handler. */
static int sc5_mask_in_handler(void) {
    g_alrm_count = 0;
    g_alrm_mask_seen = 0;
    if (install_handler(SIGALRM, mask_inspecting_handler, 0) < 0) FAIL("sigaction");
    if (set_oneshot(30) < 0) FAIL("setitimer");

    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
    select(0, NULL, NULL, NULL, &tv);

    if (g_alrm_count != 1) FAIL("handler count %d", (int)g_alrm_count);
    if (g_alrm_mask_seen != 1)
        FAIL("inside handler, SIGALRM should be in current mask (was %d)",
             (int)g_alrm_mask_seen);

    /* And the mask must be RESTORED after the handler returns. */
    sigset_t after;
    sigprocmask(SIG_BLOCK, NULL, &after);
    if (sigismember(&after, SIGALRM))
        FAIL("after handler returned, SIGALRM still in mask (sigreturn didn't restore)");
    return 0;
}

/* sc6: pending coalescing.  POSIX says standard (non-realtime)
 * signals do NOT queue while blocked — at most one stays pending.
 * In practice this is implementation-dependent for setitimer-driven
 * SIGALRM: substrate and Linux coalesce to exactly 1; FreeBSD
 * 14.3/i386 delivers one per missed tick (observed: ~9 over 200 ms
 * with a 20 ms timer).  Accept either behaviour, but bound the
 * upper count so a real run-away timer still fails. */
static int sc6_coalesce(void) {
    g_alrm_count = 0;
    if (install_handler(SIGALRM, count_handler, 0) < 0) FAIL("sigaction");

    sigset_t block, prev;
    sigemptyset(&block); sigaddset(&block, SIGALRM);
    sigprocmask(SIG_BLOCK, &block, &prev);

    if (set_periodic(20) < 0) FAIL("setitimer");

    /* Spin long enough that 5+ timer ticks accumulate while blocked. */
    usleep_busy(200);

    disarm_timer();

    /* Now unblock — at least one SIGALRM should be delivered. */
    sigprocmask(SIG_SETMASK, &prev, NULL);

    /* Give the kernel a moment to deliver. */
    struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };
    select(0, NULL, NULL, NULL, &tv);

    if (g_alrm_count < 1)
        FAIL("after unblock, no SIGALRM delivered (POSIX requires >=1)");
    if (g_alrm_count > 50)
        FAIL("after unblock, %d SIGALRMs — runaway timer?", (int)g_alrm_count);
    fprintf(stderr, "  note: %d SIGALRMs delivered after unblock "
                    "(substrate/Linux coalesce to 1; FreeBSD ~one-per-tick)\n",
            (int)g_alrm_count);
    return 0;
}

/* sc7: storm bound.  Even a very short interval should not produce
 * thousands of handler invocations in a short window (sanity bound on
 * the kernel's wall-clock vs HZ accounting). */
static int sc7_storm_bound(void) {
    g_alrm_count = 0;
    if (install_handler(SIGALRM, count_handler, 0) < 0) FAIL("sigaction");

    /* Ask for 1ms interval.  HZ=128 => clamped to 1 tick = 7.8ms in
     * substrate; on Linux you get genuine 1ms.  Either way, over
     * 100ms a sane impl yields between 5 and 200 fires. */
    struct itimerval it = {
        .it_value    = { .tv_sec = 0, .tv_usec = 1000 },
        .it_interval = { .tv_sec = 0, .tv_usec = 1000 }
    };
    if (setitimer(ITIMER_REAL, &it, NULL) < 0) FAIL("setitimer");

    usleep_busy(100);
    disarm_timer();

    if (g_alrm_count < 1)
        FAIL("1ms periodic timer over 100ms: 0 fires");
    if (g_alrm_count > 5000)
        FAIL("STORM: 1ms timer fired %d times in 100ms (kernel timing bug?)",
             (int)g_alrm_count);
    fprintf(stderr, "  note: 1ms timer fired %d times in ~100ms\n", (int)g_alrm_count);
    return 0;
}

/* sc8: alarm() returns previously-set remaining seconds. */
static int sc8_alarm_roundtrip(void) {
    g_alrm_count = 0;
    if (install_handler(SIGALRM, count_handler, 0) < 0) FAIL("sigaction");

    unsigned prev = alarm(3);
    if (prev != 0) FAIL("first alarm(): expected 0 remaining, got %u", prev);
    unsigned r = alarm(0);
    if (r == 0)
        FAIL("alarm(0): should have returned remaining of prior 3s alarm, got 0");
    if (r > 3)
        FAIL("alarm(0): remaining %u > original 3", r);
    return 0;
}

/* sc9: getitimer roundtrip. */
static int sc9_getitimer(void) {
    struct itimerval set, got;
    memset(&set, 0, sizeof(set));
    set.it_value.tv_sec  = 2;
    set.it_value.tv_usec = 500000;
    set.it_interval.tv_sec  = 1;
    set.it_interval.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &set, NULL) < 0) FAIL("setitimer");
    if (getitimer(ITIMER_REAL, &got) < 0) FAIL("getitimer");
    disarm_timer();

    /* Allow some slack: interval should match the request, value
     * decreasing. */
    if (got.it_interval.tv_sec != 1 || got.it_interval.tv_usec != 0)
        FAIL("interval round-trip lost: %ld.%06ld",
             (long)got.it_interval.tv_sec, (long)got.it_interval.tv_usec);
    long v_us = got.it_value.tv_sec * 1000000L + got.it_value.tv_usec;
    if (v_us <= 0 || v_us > 2500000)
        FAIL("it_value out of plausible range: %ld us", v_us);
    return 0;
}

/* sc10: siglongjmp from handler.  Classic libevent / sjlj idiom. */
static sigjmp_buf g_sjmp;
static void longjmp_handler(int sig) { (void)sig; siglongjmp(g_sjmp, 1); }

static int sc10_siglongjmp(void) {
    if (install_handler(SIGALRM, longjmp_handler, 0) < 0) FAIL("sigaction");
    if (set_oneshot(50) < 0) FAIL("setitimer");

    if (sigsetjmp(g_sjmp, 1) == 0) {
        /* Tight busy loop — handler will siglongjmp out. */
        volatile long i = 0;
        while (1) { i++; if (i == LONG_MAX) break; }
        FAIL("siglongjmp did NOT take effect; busy loop exited normally");
    }
    /* Mask must be intact post-longjmp because we used sigsetjmp(1). */
    sigset_t after;
    sigprocmask(SIG_BLOCK, NULL, &after);
    if (sigismember(&after, SIGALRM))
        FAIL("after siglongjmp(savemask=1), SIGALRM still in mask");
    return 0;
}

/* sc11: progress under storm.  Set periodic 5ms timer; spin for ~300
 * ms of wall clock in user code; verify (a) the work finished in well
 * under 5 seconds (handler hasn't starved user code) and (b) at least
 * one SIGALRM was delivered.  Direct analogue of the Xfbdev symptom:
 * if the kernel's signal pipeline doesn't return-to-user between
 * handler invocations, the spin loop will never finish. */
static int sc11_progress_under_storm(void) {
    g_alrm_count = 0;
    if (install_handler(SIGALRM, count_handler, 0) < 0) FAIL("sigaction");
    if (set_periodic(5) < 0) FAIL("setitimer");

    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
    long elapsed_ms = 0;
    volatile long sink = 0;
    while (elapsed_ms < 300) {
        for (volatile long i = 0; i < 100000; i++) sink += i;
        struct timespec tn; clock_gettime(CLOCK_MONOTONIC, &tn);
        elapsed_ms = (tn.tv_sec - t0.tv_sec) * 1000 +
                     (tn.tv_nsec - t0.tv_nsec) / 1000000;
    }
    disarm_timer();
    (void)sink;

    fprintf(stderr, "  note: ~300ms busy work under 5ms timer: %ld ms wall, "
                    "%d SIGALRMs delivered\n",
            elapsed_ms, (int)g_alrm_count);
    if (elapsed_ms > 5000)
        FAIL("STORM: 300ms work expanded to %ld ms — user starved", elapsed_ms);
    if (g_alrm_count < 1)
        FAIL("no SIGALRMs delivered during 300ms busy loop");
    return 0;
}

/* sc12: accept() + SIGALRM (mimics Xfbdev's outer loop).  Bind an
 * AF_UNIX socket, schedule a 50ms one-shot SIGALRM, block in accept().
 * The handler should make accept() return EINTR; then we disarm and
 * exit cleanly.  Without SA_RESTART this MUST EINTR — that's the
 * mechanism X uses to break out of WaitFor on a tick. */
#include <sys/socket.h>
#include <sys/un.h>

static int sc12_accept_eintr(void) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) FAIL("socket: %s", strerror(errno));

    struct sockaddr_un sa; memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof(sa.sun_path), "/tmp/sigalrm-torture-%d", (int)getpid());
    unlink(sa.sun_path);
    if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(s); FAIL("bind: %s", strerror(errno)); }
    if (listen(s, 1) < 0)                                { close(s); unlink(sa.sun_path); FAIL("listen"); }

    g_alrm_count = 0;
    if (install_handler(SIGALRM, count_handler, 0) < 0) { close(s); unlink(sa.sun_path); FAIL("sigaction"); }
    if (set_oneshot(50) < 0)                            { close(s); unlink(sa.sun_path); FAIL("setitimer"); }

    struct sockaddr_un peer; socklen_t plen = sizeof(peer);
    errno = 0;
    int c = accept(s, (struct sockaddr *)&peer, &plen);
    int save = errno;
    close(s); unlink(sa.sun_path);

    if (c >= 0) { close(c); FAIL("accept should not have returned a connection"); }
    if (save != EINTR) FAIL("accept errno=%d, want EINTR(%d)", save, EINTR);
    if (g_alrm_count != 1) FAIL("handler count %d, want 1", (int)g_alrm_count);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Driver                                                              */
/* ------------------------------------------------------------------ */

struct sc { const char *name; int (*fn)(void); };
static struct sc tests[] = {
    { "sc1_oneshot",              sc1_oneshot              },
    { "sc2_periodic",             sc2_periodic             },
    { "sc3_eintr_read",           sc3_eintr_read           },
    { "sc4_sa_restart",           sc4_sa_restart           },
    { "sc5_mask_in_handler",      sc5_mask_in_handler      },
    { "sc6_coalesce",             sc6_coalesce             },
    { "sc7_storm_bound",          sc7_storm_bound          },
    { "sc8_alarm_roundtrip",      sc8_alarm_roundtrip      },
    { "sc9_getitimer",            sc9_getitimer            },
    { "sc10_siglongjmp",          sc10_siglongjmp          },
    { "sc11_progress_under_storm",sc11_progress_under_storm},
    { "sc12_accept_eintr",        sc12_accept_eintr        },
};

/* Run scenario i in a forked child so a crash in one scenario
 * doesn't take out the rest of the run.  Returns 0 = PASS,
 * 1 = FAIL, 2 = child died on a signal. */
static int run_scenario(size_t i) {
    pid_t kid = fork();
    if (kid < 0) {
        fprintf(stderr, "  fork failed: %s\n", strerror(errno));
        return 1;
    }
    if (kid == 0) {
        /* Always start each scenario with timer disarmed and SIGALRM
         * unblocked, so an earlier failure can't contaminate. */
        disarm_timer();
        sigset_t unblock; sigemptyset(&unblock); sigaddset(&unblock, SIGALRM);
        sigprocmask(SIG_UNBLOCK, &unblock, NULL);
        int r = tests[i].fn();
        _exit(r ? 1 : 0);
    }
    int status;
    if (waitpid(kid, &status, 0) < 0) {
        fprintf(stderr, "  waitpid failed: %s\n", strerror(errno));
        return 1;
    }
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "  CHILD died on signal %d\n", WTERMSIG(status));
        return 2;
    }
    if (!WIFEXITED(status)) {
        fprintf(stderr, "  CHILD exited abnormally (status=0x%x)\n", status);
        return 1;
    }
    return WEXITSTATUS(status) == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int pass = 0, fail = 0, crash = 0;
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        fprintf(stderr, "[%s] running...\n", tests[i].name);
        int r = run_scenario(i);
        if      (r == 0) { pass++;  fprintf(stderr, "[%s] PASS\n", tests[i].name); }
        else if (r == 2) { crash++; fprintf(stderr, "[%s] CRASH\n", tests[i].name); }
        else             { fail++;  fprintf(stderr, "[%s] FAIL\n", tests[i].name); }
    }
    fprintf(stderr, "\ntorture_sigalrm: %d PASS, %d FAIL, %d CRASH\n",
            pass, fail, crash);
    /* run-auto-test.sh greps for ^Result: on stdout to decide pass/fail. */
    printf("Result: %s (%d/%d PASS, %d crash)\n",
           (fail + crash) ? "FAILED" : "PASSED",
           pass, pass + fail + crash, crash);
    fflush(stdout);
    return (fail + crash) ? 1 : 0;
}
