/*
 * torture_pipe.c — anonymous-pipe (and pipe2/FIFO) stress + wake-path
 * torture suite.  ~50 distinct tests.
 *
 * Pure POSIX C.  Builds against host pthreads + libc by default;
 * against substrate's libpthread + libc when built with
 * CROSS=/opt/substrate/bin/i386-unknown-substrate-.  Must compile with
 * no warnings and PASS on Linux first — any failure here that doesn't
 * reproduce on host libc points at a kernel/libc bug.
 *
 * This is a KERNEL-HANG-HUNTING suite.  Substrate has an intermittent
 * lost-wakeup bug on blocking pipe read/write under kernel preemption.
 * So the suite:
 *   (a) STRESSES the blocking/wakeup paths hard (the high-iteration
 *       blocking_read_wakes / blocking_write_wakes / ping-pong tests),
 *       and
 *   (b) NEVER lets one hanging test wedge the whole run — every test
 *       runs in a forked child under an alarm(2) watchdog.  alarm fires
 *       from the timer IRQ and does NOT depend on the buggy sched_wakeup
 *       path, so even a fully-wedged child gets reaped and reported as
 *       HANG while the suite continues.
 *
 * Scenarios target the wake/poll paths that bsdtar's gzip pipe pattern
 * uncovered: kernel sleeps on the wrong wait channel, or doesn't wake
 * at all, when poll/select multiplexes a pair of pipes feeding a child.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_TIMEOUT 6            /* seconds a single test may run */
static int tests_run, tests_pass, tests_fail, tests_hang, tests_skip;

/* test fn return: 0=pass, 1=skip, negative=fail */
typedef int (*testfn)(void);

static void alrm_noop(int s){ (void)s; }   /* just interrupt waitpid */

static void run_one(const char *name, testfn fn) {
    fprintf(stdout, "[%2d] %-32s ", ++tests_run, name); fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { fprintf(stdout, "FORK-FAIL errno=%d\n", errno); tests_fail++; return; }
    if (pid == 0) {                       /* child runs the test */
        int rc = fn();
        fflush(stdout);
        _exit(rc == 0 ? 0 : (rc == 1 ? 2 : 1));   /* 0 pass / 2 skip / 1 fail */
    }
    struct sigaction sa, old; memset(&sa, 0, sizeof(sa)); sa.sa_handler = alrm_noop;
    sigaction(SIGALRM, &sa, &old);
    alarm(TEST_TIMEOUT);
    int st; pid_t r = waitpid(pid, &st, 0); int e = errno;
    alarm(0); sigaction(SIGALRM, &old, NULL);
    if (r != pid) {                        /* interrupted by alarm or error */
        (void)e;
        if (waitpid(pid, &st, WNOHANG) != pid) {     /* still running => real hang */
            kill(pid, SIGKILL); waitpid(pid, &st, 0);
            fprintf(stdout, "HANG (killed after %ds)\n", TEST_TIMEOUT); tests_hang++;
            return;
        }
    }
    if (WIFSIGNALED(st)) { fprintf(stdout, "CRASH sig=%d\n", WTERMSIG(st)); tests_fail++; }
    else if (WEXITSTATUS(st) == 0) { fprintf(stdout, "PASS\n"); tests_pass++; }
    else if (WEXITSTATUS(st) == 2) { fprintf(stdout, "SKIP\n"); tests_skip++; }
    else { fprintf(stdout, "FAIL\n"); tests_fail++; }
}

#define RUN(name) run_one(#name, test_##name)
#define TEST(name) static int test_##name(void)
#define CHECK(cond, msg) do { if (!(cond)) { \
    fprintf(stdout, "\n    [%s:%d] %s errno=%d(%s) ", __FILE__, __LINE__, (msg), errno, strerror(errno)); \
    return -1; } } while (0)
#define SKIP(msg) do { (void)(msg); return 1; } while (0)

/* ------------------------------------------------------------------ *
 * helpers
 * ------------------------------------------------------------------ */

/* PIPE_BUF — POSIX guarantees atomic writes up to this size. */
#ifndef PIPE_BUF
# ifdef _POSIX_PIPE_BUF
#  define PIPE_BUF _POSIX_PIPE_BUF
# else
#  define PIPE_BUF 512
# endif
#endif

static int set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}
static int set_block(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    return fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
}

/* write exactly n bytes (blocking), looping on partial writes */
static int write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        if (w == 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

/* read exactly n bytes (blocking), 0 on success, -1 on error/short-EOF */
static int read_all(int fd, void *buf, size_t n) {
    char *p = buf;
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, p + off, n - off);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) return -1;        /* premature EOF */
        off += (size_t)r;
    }
    return 0;
}

/* fill a (nonblocking) pipe write end until EAGAIN; return bytes written */
static long fill_pipe(int wfd) {
    char chunk[4096];
    memset(chunk, 0xA5, sizeof(chunk));
    long total = 0;
    for (;;) {
        ssize_t w = write(wfd, chunk, sizeof(chunk));
        if (w < 0) break;
        if (w == 0) break;
        total += w;
        if (total > 16L * 1024 * 1024) break;   /* runaway guard */
    }
    return total;
}

/* ------------------------------------------------------------------ *
 * Basic round-trips
 * ------------------------------------------------------------------ */

TEST(basic_single_byte) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    CHECK(write(fds[1], "X", 1) == 1, "write");
    char b = 0;
    CHECK(read(fds[0], &b, 1) == 1, "read");
    CHECK(b == 'X', "payload");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(basic_string) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    const char m[] = "hello pipe";
    CHECK(write(fds[1], m, sizeof(m)) == (ssize_t)sizeof(m), "write");
    char b[32] = {0};
    CHECK(read(fds[0], b, sizeof(b)) == (ssize_t)sizeof(m), "read");
    CHECK(memcmp(b, m, sizeof(m)) == 0, "payload");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(exact_pipe_buf_write) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    char *buf = malloc(PIPE_BUF);
    CHECK(buf != NULL, "malloc");
    for (int i = 0; i < PIPE_BUF; i++) buf[i] = (char)(i & 0xff);
    CHECK(write(fds[1], buf, PIPE_BUF) == PIPE_BUF, "write PIPE_BUF");
    char *rb = malloc(PIPE_BUF);
    CHECK(rb != NULL, "malloc");
    CHECK(read_all(fds[0], rb, PIPE_BUF) == 0, "read PIPE_BUF");
    CHECK(memcmp(buf, rb, PIPE_BUF) == 0, "payload");
    free(buf); free(rb);
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(multi_chunk) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    for (int i = 0; i < 16; i++) {
        char c = (char)('A' + i);
        CHECK(write(fds[1], &c, 1) == 1, "write chunk");
    }
    char b[16] = {0};
    CHECK(read_all(fds[0], b, 16) == 0, "read");
    for (int i = 0; i < 16; i++) CHECK(b[i] == (char)('A' + i), "ordering");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(zero_length_write) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    /* A zero-length write to a pipe is a no-op returning 0. */
    CHECK(write(fds[1], "", 0) == 0, "zero write");
    /* Then a real byte must still flow. */
    CHECK(write(fds[1], "Z", 1) == 1, "write byte");
    char b = 0;
    CHECK(read(fds[0], &b, 1) == 1, "read byte");
    CHECK(b == 'Z', "payload");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(zero_length_read) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    CHECK(write(fds[1], "Q", 1) == 1, "write");
    char b = 0;
    /* read of 0 bytes returns 0, leaves data queued */
    CHECK(read(fds[0], &b, 0) == 0, "zero read");
    CHECK(read(fds[0], &b, 1) == 1, "real read");
    CHECK(b == 'Q', "payload still there");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(read_after_partial) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    const char m[] = "0123456789";
    CHECK(write(fds[1], m, 10) == 10, "write");
    char b[4] = {0};
    CHECK(read(fds[0], b, 4) == 4, "read 4");
    CHECK(memcmp(b, "0123", 4) == 0, "first chunk");
    char c[6] = {0};
    CHECK(read_all(fds[0], c, 6) == 0, "read rest");
    CHECK(memcmp(c, "456789", 6) == 0, "second chunk");
    close(fds[0]); close(fds[1]);
    return 0;
}

/* ------------------------------------------------------------------ *
 * Size sweep — round-trip exact byte counts through a fork.
 * Larger-than-buffer sizes require concurrent reader to avoid deadlock.
 * ------------------------------------------------------------------ */

static int size_roundtrip(size_t n) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    uint8_t *out = malloc(n ? n : 1);
    CHECK(out != NULL, "malloc");
    for (size_t i = 0; i < n; i++) out[i] = (uint8_t)((i * 31u + 7u) & 0xff);

    pid_t pid = fork();
    CHECK(pid >= 0, "fork");
    if (pid == 0) {
        close(fds[0]);
        _exit(write_all(fds[1], out, n) == 0 ? 0 : 1);
    }
    close(fds[1]);
    uint8_t *in = malloc(n ? n : 1);
    if (!in) { free(out); _exit(1); }
    int rc = read_all(fds[0], in, n);
    int cmp = (n == 0) ? 0 : memcmp(out, in, n);
    close(fds[0]);
    int st; waitpid(pid, &st, 0);
    free(out); free(in);
    CHECK(rc == 0, "read_all");
    CHECK(cmp == 0, "payload");
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "writer child");
    return 0;
}

TEST(size_1)      { return size_roundtrip(1); }
TEST(size_2)      { return size_roundtrip(2); }
TEST(size_63)     { return size_roundtrip(63); }
TEST(size_64)     { return size_roundtrip(64); }
TEST(size_4095)   { return size_roundtrip(4095); }
TEST(size_4096)   { return size_roundtrip(4096); }
TEST(size_4097)   { return size_roundtrip(4097); }
TEST(size_65535)  { return size_roundtrip(65535); }
TEST(size_65536)  { return size_roundtrip(65536); }
TEST(size_1mib)   { return size_roundtrip(1024 * 1024); }

/* ------------------------------------------------------------------ *
 * fill_then_drain — fill the buffer, then drain it entirely
 * ------------------------------------------------------------------ */

TEST(fill_then_drain) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    CHECK(set_nonblock(fds[1]) == 0, "nonblock w");
    long total = fill_pipe(fds[1]);
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK, "EAGAIN on full");
    CHECK(total > 0, "buffer had room");
    CHECK(set_nonblock(fds[0]) == 0, "nonblock r");
    long drained = 0;
    for (;;) {
        char buf[4096];
        ssize_t r = read(fds[0], buf, sizeof(buf));
        if (r < 0) { CHECK(errno == EAGAIN || errno == EWOULDBLOCK, "EAGAIN drain"); break; }
        if (r == 0) break;
        drained += r;
    }
    CHECK(drained == total, "round-trip byte count");
    close(fds[0]); close(fds[1]);
    return 0;
}

/* ------------------------------------------------------------------ *
 * Blocking wakeup STRESS — the lost-wakeup repro.  High iteration.
 * ------------------------------------------------------------------ */

struct wk { int rfd, wfd; int iters; };

static void *wk_writer(void *arg) {
    struct wk *w = arg;
    char c = 0x5a;
    for (int i = 0; i < w->iters; i++) {
        if (write(w->wfd, &c, 1) != 1) return (void *)1;
        /* read the ack so the pipe never accumulates and we stay in
         * lock-step with the reader; this maximizes park/wake churn */
        char a;
        if (read(w->rfd, &a, 1) != 1) return (void *)2;
    }
    return NULL;
}

/* Reader blocks waiting for a byte, writer thread supplies it; lock-step.
 * 8000 iterations of park-then-wake on the read path. */
TEST(blocking_read_wakes_stress) {
    int down[2], up[2];                 /* down: writer->reader, up: ack */
    CHECK(pipe(down) == 0, "pipe down");
    CHECK(pipe(up) == 0, "pipe up");
    const int N = 8000;
    struct wk w = { .rfd = up[0], .wfd = down[1], .iters = N };
    pthread_t t;
    CHECK(pthread_create(&t, NULL, wk_writer, &w) == 0, "pthread_create");
    for (int i = 0; i < N; i++) {
        char c;
        CHECK(read(down[0], &c, 1) == 1, "blocked read woke");
        CHECK(c == 0x5a, "payload");
        CHECK(write(up[1], "a", 1) == 1, "ack");
    }
    void *rv; pthread_join(t, &rv);
    CHECK(rv == NULL, "writer thread clean");
    close(down[0]); close(down[1]); close(up[0]); close(up[1]);
    return 0;
}

struct dw { int rfd, wfd; long fillsz; int iters; };

static void *dw_drainer(void *arg) {
    struct dw *d = arg;
    char buf[4096];
    for (int i = 0; i < d->iters; i++) {
        /* drain one buffer's worth so the parked writer can proceed */
        long got = 0;
        while (got < d->fillsz) {
            ssize_t r = read(d->rfd, buf, sizeof(buf));
            if (r <= 0) return (void *)1;
            got += r;
        }
    }
    return NULL;
}

/* Writer fills the pipe, then blocks; reader thread drains so the writer
 * wakes.  5000 iterations of park-then-wake on the write path. */
TEST(blocking_write_wakes_stress) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    /* Measure the buffer capacity once. */
    CHECK(set_nonblock(fds[1]) == 0, "nonblock");
    long cap = fill_pipe(fds[1]);
    CHECK(cap > 0, "capacity");
    /* Drain it back out. */
    CHECK(set_nonblock(fds[0]) == 0, "nonblock r");
    { char b[4096]; while (read(fds[0], b, sizeof(b)) > 0) {} }
    CHECK(set_block(fds[0]) == 0, "block r");
    CHECK(set_block(fds[1]) == 0, "block w");

    const int N = 5000;
    struct dw d = { .rfd = fds[0], .wfd = fds[1], .fillsz = cap, .iters = N };
    pthread_t t;
    CHECK(pthread_create(&t, NULL, dw_drainer, &d) == 0, "pthread_create");
    char *chunk = malloc(cap);
    CHECK(chunk != NULL, "malloc");
    memset(chunk, 0x33, cap);
    for (int i = 0; i < N; i++) {
        /* This fills, then the last bytes block until the drainer runs. */
        CHECK(write_all(fds[1], chunk, cap) == 0, "write resumed after park");
    }
    void *rv; pthread_join(t, &rv);
    free(chunk);
    CHECK(rv == NULL, "drainer clean");
    close(fds[0]); close(fds[1]);
    return 0;
}

/* Same lost-wakeup hunt but the other side is a forked process, not a
 * thread — exercises cross-process pipe wakeups. */
TEST(blocking_read_wakes_fork_stress) {
    int down[2], up[2];
    CHECK(pipe(down) == 0, "pipe down");
    CHECK(pipe(up) == 0, "pipe up");
    const int N = 4000;
    pid_t pid = fork();
    CHECK(pid >= 0, "fork");
    if (pid == 0) {
        close(down[0]); close(up[1]);
        char c = 0x77;
        for (int i = 0; i < N; i++) {
            if (write(down[1], &c, 1) != 1) _exit(1);
            char a;
            if (read(up[0], &a, 1) != 1) _exit(2);
        }
        _exit(0);
    }
    close(down[1]); close(up[0]);
    for (int i = 0; i < N; i++) {
        char c;
        CHECK(read(down[0], &c, 1) == 1, "blocked read woke");
        CHECK(c == 0x77, "payload");
        CHECK(write(up[1], "a", 1) == 1, "ack");
    }
    int st; waitpid(pid, &st, 0);
    close(down[0]); close(up[1]);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child clean");
    return 0;
}

/* ------------------------------------------------------------------ *
 * Ping-pong round-trips
 * ------------------------------------------------------------------ */

/* Parent<->child request/response, 20000 round trips, verify the
 * sequence counter integrity each way. */
TEST(pingpong_fork_20000) {
    int toc[2], fromc[2];               /* to-child, from-child */
    CHECK(pipe(toc) == 0, "pipe toc");
    CHECK(pipe(fromc) == 0, "pipe fromc");
    const int N = 20000;
    pid_t pid = fork();
    CHECK(pid >= 0, "fork");
    if (pid == 0) {
        close(toc[1]); close(fromc[0]);
        for (int i = 0; i < N; i++) {
            uint32_t v;
            if (read_all(toc[0], &v, sizeof(v)) != 0) _exit(1);
            if (v != (uint32_t)i) _exit(2);
            v += 1;
            if (write_all(fromc[1], &v, sizeof(v)) != 0) _exit(3);
        }
        _exit(0);
    }
    close(toc[0]); close(fromc[1]);
    for (int i = 0; i < N; i++) {
        uint32_t v = (uint32_t)i;
        CHECK(write_all(toc[1], &v, sizeof(v)) == 0, "send");
        uint32_t back;
        CHECK(read_all(fromc[0], &back, sizeof(back)) == 0, "recv");
        CHECK(back == (uint32_t)i + 1, "sequence integrity");
    }
    int st; waitpid(pid, &st, 0);
    close(toc[1]); close(fromc[0]);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child clean");
    return 0;
}

struct pp { int rfd, wfd; int iters; int is_a; };

static void *pp_thread(void *arg) {
    struct pp *p = arg;
    uint32_t v;
    if (p->is_a) {
        /* A starts: send 0, then echo back+1 each reply */
        v = 0;
        if (write_all(p->wfd, &v, sizeof(v)) != 0) return (void *)1;
        for (int i = 0; i < p->iters; i++) {
            if (read_all(p->rfd, &v, sizeof(v)) != 0) return (void *)2;
            v += 1;
            if (i < p->iters - 1) {
                if (write_all(p->wfd, &v, sizeof(v)) != 0) return (void *)3;
            }
        }
    } else {
        for (int i = 0; i < p->iters; i++) {
            if (read_all(p->rfd, &v, sizeof(v)) != 0) return (void *)4;
            v += 1;
            if (write_all(p->wfd, &v, sizeof(v)) != 0) return (void *)5;
        }
    }
    return NULL;
}

/* Two pthreads bounce a token through a pipe pair 50000 times. */
TEST(pingpong_threaded_50000) {
    int a2b[2], b2a[2];
    CHECK(pipe(a2b) == 0, "pipe a2b");
    CHECK(pipe(b2a) == 0, "pipe b2a");
    const int N = 50000;
    struct pp pa = { .rfd = b2a[0], .wfd = a2b[1], .iters = N, .is_a = 1 };
    struct pp pb = { .rfd = a2b[0], .wfd = b2a[1], .iters = N, .is_a = 0 };
    pthread_t ta, tb;
    CHECK(pthread_create(&tb, NULL, pp_thread, &pb) == 0, "create b");
    CHECK(pthread_create(&ta, NULL, pp_thread, &pa) == 0, "create a");
    void *ra, *rb;
    pthread_join(ta, &ra); pthread_join(tb, &rb);
    CHECK(ra == NULL, "thread a clean");
    CHECK(rb == NULL, "thread b clean");
    close(a2b[0]); close(a2b[1]); close(b2a[0]); close(b2a[1]);
    return 0;
}

/* ------------------------------------------------------------------ *
 * N-writer atomicity — concurrent <=PIPE_BUF records must not tear
 * ------------------------------------------------------------------ */

struct rec { uint32_t writer; uint32_t seq; uint8_t pad[24]; };  /* 32 bytes <= PIPE_BUF */

struct nw { int wfd; int writer; int recs; };

static void *nw_thread(void *arg) {
    struct nw *n = arg;
    struct rec r;
    memset(&r, 0, sizeof(r));
    r.writer = (uint32_t)n->writer;
    for (int i = 0; i < n->recs; i++) {
        r.seq = (uint32_t)i;
        memset(r.pad, (int)(n->writer & 0xff), sizeof(r.pad));
        if (write(n->wfd, &r, sizeof(r)) != (ssize_t)sizeof(r)) return (void *)1;
    }
    return NULL;
}

TEST(n_writer_atomicity) {
    CHECK(sizeof(struct rec) <= PIPE_BUF, "record fits PIPE_BUF");
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    const int W = 8, R = 2000;
    pthread_t th[8];
    struct nw args[8];
    for (int i = 0; i < W; i++) {
        args[i].wfd = fds[1]; args[i].writer = i; args[i].recs = R;
        CHECK(pthread_create(&th[i], NULL, nw_thread, &args[i]) == 0, "create");
    }
    /* Reader: read exactly W*R records, verify each is internally
     * consistent (no interleave) and per-writer seq is monotonic. */
    long expect = (long)W * R;
    int next_seq[8] = {0};
    long got = 0;
    int bad = 0;
    while (got < expect) {
        struct rec r;
        if (read_all(fds[0], &r, sizeof(r)) != 0) { bad = 1; break; }
        if (r.writer >= (uint32_t)W) { bad = 2; break; }
        if (r.seq != (uint32_t)next_seq[r.writer]) { bad = 3; break; }
        /* pad must all equal writer&0xff — torn record would mismatch */
        for (size_t k = 0; k < sizeof(r.pad); k++)
            if (r.pad[k] != (uint8_t)(r.writer & 0xff)) { bad = 4; break; }
        if (bad) break;
        next_seq[r.writer]++;
        got++;
    }
    for (int i = 0; i < W; i++) pthread_join(th[i], NULL);
    close(fds[0]); close(fds[1]);
    CHECK(bad == 0, "record integrity (no tearing/interleave)");
    CHECK(got == expect, "all records received");
    return 0;
}

/* ------------------------------------------------------------------ *
 * EOF semantics
 * ------------------------------------------------------------------ */

TEST(eof_on_writer_close) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    close(fds[1]);
    char buf[8];
    CHECK(read(fds[0], buf, sizeof(buf)) == 0, "read 0 = EOF");
    /* repeated reads keep returning 0 */
    CHECK(read(fds[0], buf, sizeof(buf)) == 0, "EOF sticky");
    close(fds[0]);
    return 0;
}

TEST(partial_then_eof) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    CHECK(write(fds[1], "tail", 4) == 4, "write");
    close(fds[1]);
    char b[16] = {0};
    CHECK(read(fds[0], b, sizeof(b)) == 4, "read remaining data");
    CHECK(memcmp(b, "tail", 4) == 0, "payload");
    CHECK(read(fds[0], b, sizeof(b)) == 0, "EOF after drain");
    close(fds[0]);
    return 0;
}

/* EOF only when ALL write ends are closed (dup'd fd keeps it open). */
TEST(eof_only_after_all_writers_close) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    int dupw = dup(fds[1]);
    CHECK(dupw >= 0, "dup");
    CHECK(write(fds[1], "z", 1) == 1, "write");
    close(fds[1]);                       /* one writer still open (dupw) */
    char b[8] = {0};
    CHECK(read(fds[0], b, sizeof(b)) == 1, "read data");
    /* Not EOF yet — make it nonblocking and confirm EAGAIN, not 0 */
    CHECK(set_nonblock(fds[0]) == 0, "nonblock");
    CHECK(read(fds[0], b, sizeof(b)) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
          "EAGAIN while a writer is open");
    close(dupw);                         /* now last writer gone */
    CHECK(read(fds[0], b, sizeof(b)) == 0, "EOF after last writer");
    close(fds[0]);
    return 0;
}

/* ------------------------------------------------------------------ *
 * SIGPIPE / EPIPE
 * ------------------------------------------------------------------ */

static volatile sig_atomic_t got_sigpipe;
static void sigpipe_handler(int s) { (void)s; got_sigpipe = 1; }

/* This test installs its OWN SIGPIPE handler in its child (the suite
 * ignores SIGPIPE globally, but each test runs in a fresh fork). */
TEST(sigpipe_then_epipe) {
    struct sigaction sa, old; memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigpipe_handler; sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, &old);
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    close(fds[0]);                       /* kill the reader */
    got_sigpipe = 0;
    ssize_t w = write(fds[1], "x", 1);
    CHECK(w < 0 && errno == EPIPE, "first write EPIPE");
    CHECK(got_sigpipe == 1, "SIGPIPE delivered");
    /* second write: still EPIPE */
    ssize_t w2 = write(fds[1], "y", 1);
    CHECK(w2 < 0 && errno == EPIPE, "second write EPIPE");
    close(fds[1]);
    sigaction(SIGPIPE, &old, NULL);
    return 0;
}

/* EPIPE only (SIGPIPE ignored) — the common library pattern. */
TEST(epipe_with_sigpipe_ignored) {
    struct sigaction sa, old; memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN; sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, &old);
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    close(fds[0]);
    CHECK(write(fds[1], "x", 1) < 0 && errno == EPIPE, "EPIPE");
    close(fds[1]);
    sigaction(SIGPIPE, &old, NULL);
    return 0;
}

/* ------------------------------------------------------------------ *
 * O_NONBLOCK
 * ------------------------------------------------------------------ */

TEST(nonblock_read_eagain) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    CHECK(set_nonblock(fds[0]) == 0, "nonblock");
    char b[8];
    CHECK(read(fds[0], b, sizeof(b)) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK), "EAGAIN");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(nonblock_write_eagain) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    CHECK(set_nonblock(fds[1]) == 0, "nonblock");
    (void)fill_pipe(fds[1]);
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK, "EAGAIN on full");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(pipe2_nonblock) {
#if defined(O_NONBLOCK)
    int fds[2];
    int rc = pipe2(fds, O_NONBLOCK);
    if (rc != 0 && errno == ENOSYS) SKIP("pipe2 ENOSYS");
    CHECK(rc == 0, "pipe2 O_NONBLOCK");
    /* both ends should be nonblocking */
    char b[8];
    CHECK(read(fds[0], b, sizeof(b)) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
          "read nonblock");
    CHECK((fcntl(fds[0], F_GETFL, 0) & O_NONBLOCK) != 0, "read end flag");
    CHECK((fcntl(fds[1], F_GETFL, 0) & O_NONBLOCK) != 0, "write end flag");
    close(fds[0]); close(fds[1]);
    return 0;
#else
    SKIP("no O_NONBLOCK");
#endif
}

TEST(pipe2_cloexec) {
#if defined(O_CLOEXEC) && defined(FD_CLOEXEC)
    int fds[2];
    int rc = pipe2(fds, O_CLOEXEC);
    if (rc != 0 && errno == ENOSYS) SKIP("pipe2 ENOSYS");
    CHECK(rc == 0, "pipe2 O_CLOEXEC");
    CHECK((fcntl(fds[0], F_GETFD, 0) & FD_CLOEXEC) != 0, "read end FD_CLOEXEC");
    CHECK((fcntl(fds[1], F_GETFD, 0) & FD_CLOEXEC) != 0, "write end FD_CLOEXEC");
    close(fds[0]); close(fds[1]);
    return 0;
#else
    SKIP("no O_CLOEXEC");
#endif
}

/* Verify a plain pipe's fds do NOT have FD_CLOEXEC. */
TEST(pipe_no_cloexec_default) {
#if defined(FD_CLOEXEC)
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    CHECK((fcntl(fds[0], F_GETFD, 0) & FD_CLOEXEC) == 0, "read end not cloexec");
    CHECK((fcntl(fds[1], F_GETFD, 0) & FD_CLOEXEC) == 0, "write end not cloexec");
    close(fds[0]); close(fds[1]);
    return 0;
#else
    SKIP("no FD_CLOEXEC");
#endif
}

/* ------------------------------------------------------------------ *
 * poll(2)
 * ------------------------------------------------------------------ */

struct delayed { int fd; int delay_us; const char *data; size_t len; };

static void *delayed_writer(void *arg) {
    struct delayed *d = arg;
    usleep(d->delay_us);
    if (write_all(d->fd, d->data, d->len) != 0) return (void *)1;
    return NULL;
}
static void *delayed_closer(void *arg) {
    struct delayed *d = arg;
    usleep(d->delay_us);
    close(d->fd);
    return NULL;
}

/* Drain one buffer's worth from a fd after a delay (lets a parked
 * writer/poller make progress).  Frees the heap-allocated int arg. */
static void *delayed_drainer(void *arg) {
    int fd = *(int *)arg;
    free(arg);
    usleep(60000);
    char b[4096];
    (void)read(fd, b, sizeof(b));
    return NULL;
}

TEST(poll_pollin_after_park) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    struct delayed d = { .fd = fds[1], .delay_us = 60000, .data = "go", .len = 2 };
    pthread_t t;
    CHECK(pthread_create(&t, NULL, delayed_writer, &d) == 0, "create");
    struct pollfd pfd = { .fd = fds[0], .events = POLLIN, .revents = 0 };
    int n = poll(&pfd, 1, 4000);
    CHECK(n == 1, "poll returned 1");
    CHECK(pfd.revents & POLLIN, "POLLIN set");
    char b[8]; CHECK(read(fds[0], b, sizeof(b)) == 2, "read");
    pthread_join(t, NULL);
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(poll_pollout_after_drain) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    CHECK(set_nonblock(fds[1]) == 0, "nonblock");
    (void)fill_pipe(fds[1]);
    CHECK(set_block(fds[1]) == 0, "block");
    int *dfd = malloc(sizeof(int)); CHECK(dfd, "malloc");
    *dfd = fds[0];
    pthread_t t;
    CHECK(pthread_create(&t, NULL, delayed_drainer, dfd) == 0, "create");
    struct pollfd pfd = { .fd = fds[1], .events = POLLOUT, .revents = 0 };
    int n = poll(&pfd, 1, 4000);
    CHECK(n == 1, "poll returned 1");
    CHECK(pfd.revents & POLLOUT, "POLLOUT set");
    pthread_join(t, NULL);
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(poll_pollhup_after_writer_close) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    struct delayed d = { .fd = fds[1], .delay_us = 60000, .data = NULL, .len = 0 };
    pthread_t t;
    CHECK(pthread_create(&t, NULL, delayed_closer, &d) == 0, "create");
    struct pollfd pfd = { .fd = fds[0], .events = POLLIN, .revents = 0 };
    int n = poll(&pfd, 1, 4000);
    CHECK(n == 1, "poll returned 1");
    CHECK(pfd.revents & (POLLHUP | POLLIN), "POLLHUP/POLLIN set");
    pthread_join(t, NULL);
    close(fds[0]);
    return 0;
}

TEST(poll_timeout_zero) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    struct pollfd pfd = { .fd = fds[0], .events = POLLIN, .revents = 0 };
    int n = poll(&pfd, 1, 0);            /* immediate, nothing ready */
    CHECK(n == 0, "poll timeout returns 0");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(poll_timeout_expires) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    struct pollfd pfd = { .fd = fds[0], .events = POLLIN, .revents = 0 };
    int n = poll(&pfd, 1, 100);          /* 100ms, nothing ready */
    CHECK(n == 0, "poll timeout expired -> 0");
    close(fds[0]); close(fds[1]);
    return 0;
}

/* poll with negative (infinite) timeout blocks until ready. */
TEST(poll_negative_timeout_blocks) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    struct delayed d = { .fd = fds[1], .delay_us = 80000, .data = "k", .len = 1 };
    pthread_t t;
    CHECK(pthread_create(&t, NULL, delayed_writer, &d) == 0, "create");
    struct pollfd pfd = { .fd = fds[0], .events = POLLIN, .revents = 0 };
    int n = poll(&pfd, 1, -1);
    CHECK(n == 1, "poll(-1) woke");
    CHECK(pfd.revents & POLLIN, "POLLIN");
    char b; CHECK(read(fds[0], &b, 1) == 1, "read");
    pthread_join(t, NULL);
    close(fds[0]); close(fds[1]);
    return 0;
}

/* THE bsdtar/gzip pattern: poll {child-stdin-write, child-stdout-read}
 * with O_NONBLOCK on both, push data through both ends. */
struct echo2 { int rfd, wfd; };
static void *echo2_thread(void *arg) {
    struct echo2 *e = arg;
    char buf[64];
    ssize_t r = read(e->rfd, buf, sizeof(buf));
    if (r <= 0) return (void *)1;
    if (write_all(e->wfd, buf, r) != 0) return (void *)2;
    return NULL;
}

TEST(poll_two_pipes_bsdtar) {
    int in[2], out[2];
    CHECK(pipe(in) == 0,  "pipe in");
    CHECK(pipe(out) == 0, "pipe out");
    CHECK(set_nonblock(in[1]) == 0, "nb in");
    CHECK(set_nonblock(out[0]) == 0, "nb out");
    struct echo2 e = { .rfd = in[0], .wfd = out[1] };
    pthread_t t;
    CHECK(pthread_create(&t, NULL, echo2_thread, &e) == 0, "create");

    const char msg[] = "ping-pong";
    size_t to_write = sizeof(msg), written = 0, read_total = 0;
    char rbuf[64] = {0};
    int iter = 0, in_open = 1;
    while (read_total < to_write && iter++ < 500) {
        struct pollfd pfds[2] = {
            { .fd = in[1],  .events = (in_open && written < to_write) ? POLLOUT : 0, .revents = 0 },
            { .fd = out[0], .events = POLLIN, .revents = 0 },
        };
        int n = poll(pfds, 2, 50);
        if (n < 0) { CHECK(errno == EINTR, "poll EINTR-only"); continue; }
        if (n == 0) continue;
        if (pfds[0].revents & POLLOUT) {
            ssize_t w = write(in[1], msg + written, to_write - written);
            if (w > 0) { written += w; if (written == to_write) { close(in[1]); in_open = 0; } }
        }
        if (pfds[1].revents & (POLLIN | POLLHUP)) {
            ssize_t r = read(out[0], rbuf + read_total, sizeof(rbuf) - read_total);
            if (r > 0) read_total += r;
            else if (r == 0) break;
        }
    }
    pthread_join(t, NULL);
    if (read_total < to_write) {
        CHECK(set_block(out[0]) == 0, "block out");
        ssize_t r = read(out[0], rbuf + read_total, to_write - read_total);
        if (r > 0) read_total += r;
    }
    CHECK(read_total == to_write, "all bytes round-tripped");
    CHECK(memcmp(rbuf, msg, to_write) == 0, "payload survives");
    if (in_open) close(in[1]);
    close(in[0]); close(out[0]); close(out[1]);
    return 0;
}

/* ------------------------------------------------------------------ *
 * select(2)
 * ------------------------------------------------------------------ */

TEST(select_readable_after_park) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    struct delayed d = { .fd = fds[1], .delay_us = 60000, .data = "s", .len = 1 };
    pthread_t t;
    CHECK(pthread_create(&t, NULL, delayed_writer, &d) == 0, "create");
    fd_set rset; FD_ZERO(&rset); FD_SET(fds[0], &rset);
    struct timeval tv = { .tv_sec = 4, .tv_usec = 0 };
    int n = select(fds[0] + 1, &rset, NULL, NULL, &tv);
    CHECK(n == 1, "select 1");
    CHECK(FD_ISSET(fds[0], &rset), "readable");
    char b; CHECK(read(fds[0], &b, 1) == 1, "read");
    pthread_join(t, NULL);
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(select_writable_after_drain) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    CHECK(set_nonblock(fds[1]) == 0, "nb");
    (void)fill_pipe(fds[1]);
    CHECK(set_block(fds[1]) == 0, "block");
    int *dfd = malloc(sizeof(int)); CHECK(dfd, "malloc"); *dfd = fds[0];
    pthread_t t;
    CHECK(pthread_create(&t, NULL, delayed_drainer, dfd) == 0, "create");
    fd_set wset; FD_ZERO(&wset); FD_SET(fds[1], &wset);
    struct timeval tv = { .tv_sec = 4, .tv_usec = 0 };
    int n = select(fds[1] + 1, NULL, &wset, NULL, &tv);
    CHECK(n == 1, "select 1");
    CHECK(FD_ISSET(fds[1], &wset), "writable");
    pthread_join(t, NULL);
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(select_timeout_expires) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    fd_set rset; FD_ZERO(&rset); FD_SET(fds[0], &rset);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
    int n = select(fds[0] + 1, &rset, NULL, NULL, &tv);
    CHECK(n == 0, "select timeout 0");
    close(fds[0]); close(fds[1]);
    return 0;
}

/* ------------------------------------------------------------------ *
 * fork streaming + checksum
 * ------------------------------------------------------------------ */

static int fork_stream(size_t N) {
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    uint8_t *payload = malloc(N);
    CHECK(payload != NULL, "malloc");
    uint64_t expect = 0;
    for (size_t i = 0; i < N; i++) { payload[i] = (uint8_t)(i ^ 0xA5); expect += payload[i]; }
    pid_t pid = fork();
    CHECK(pid >= 0, "fork");
    if (pid == 0) {
        close(fds[1]);
        uint64_t sum = 0; uint8_t buf[4096];
        for (;;) {
            ssize_t r = read(fds[0], buf, sizeof(buf));
            if (r < 0) { if (errno == EINTR) continue; break; }
            if (r == 0) break;
            for (ssize_t i = 0; i < r; i++) sum += buf[i];
        }
        close(fds[0]);
        _exit(sum == expect ? 0 : 1);
    }
    close(fds[0]);
    int rc = write_all(fds[1], payload, N);
    close(fds[1]);
    int st; waitpid(pid, &st, 0);
    free(payload);
    CHECK(rc == 0, "write_all");
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child checksum");
    return 0;
}

TEST(fork_stream_64k)  { return fork_stream(64 * 1024); }
TEST(fork_stream_256k) { return fork_stream(256 * 1024); }
TEST(fork_stream_1mib) { return fork_stream(1024 * 1024); }

/* ------------------------------------------------------------------ *
 * fd plumbing — dup2 onto stdin/stdout in a child
 * ------------------------------------------------------------------ */

/* Child reads lines off stdin, echoes them to stdout (in-process loop,
 * no exec).  Parent feeds via the pipes wired to the child's stdio. */
TEST(dup2_stdio_echo) {
    int toc[2], fromc[2];
    CHECK(pipe(toc) == 0, "pipe toc");
    CHECK(pipe(fromc) == 0, "pipe fromc");
    pid_t pid = fork();
    CHECK(pid >= 0, "fork");
    if (pid == 0) {
        dup2(toc[0], STDIN_FILENO);
        dup2(fromc[1], STDOUT_FILENO);
        close(toc[0]); close(toc[1]); close(fromc[0]); close(fromc[1]);
        char buf[128];
        ssize_t r;
        while ((r = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
            if (write_all(STDOUT_FILENO, buf, r) != 0) _exit(1);
        }
        _exit(0);
    }
    close(toc[0]); close(fromc[1]);
    const char m[] = "echo-me-back";
    CHECK(write_all(toc[1], m, sizeof(m)) == 0, "send");
    char b[sizeof(m)] = {0};
    CHECK(read_all(fromc[0], b, sizeof(m)) == 0, "recv");
    CHECK(memcmp(b, m, sizeof(m)) == 0, "echoed payload");
    close(toc[1]);                       /* EOF to child */
    int st; waitpid(pid, &st, 0);
    close(fromc[0]);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child clean");
    return 0;
}

/* ------------------------------------------------------------------ *
 * FIFO (mkfifo)
 * ------------------------------------------------------------------ */

#ifndef P_tmpdir
#define P_tmpdir "/tmp"
#endif

static void make_fifo_path(char *out, size_t n, const char *tag) {
    snprintf(out, n, "%s/tp_fifo_%ld_%s", P_tmpdir, (long)getpid(), tag);
}

TEST(fifo_roundtrip_rdwr) {
    char path[256];
    make_fifo_path(path, sizeof(path), "rdwr");
    unlink(path);
    if (mkfifo(path, 0600) != 0) {
        if (errno == ENOSYS || errno == EPERM || errno == EACCES) SKIP("mkfifo unsupported");
        CHECK(0, "mkfifo");
    }
    /* O_RDWR avoids blocking on the missing peer open */
    int fd = open(path, O_RDWR);
    if (fd < 0) { unlink(path); if (errno == ENOSYS) SKIP("fifo open ENOSYS"); CHECK(0, "open rdwr"); }
    const char m[] = "fifo-data";
    int wrc = (write_all(fd, m, sizeof(m)) == 0);
    char b[sizeof(m)] = {0};
    int rrc = (read_all(fd, b, sizeof(m)) == 0);
    close(fd);
    unlink(path);
    CHECK(wrc, "write");
    CHECK(rrc, "read");
    CHECK(memcmp(b, m, sizeof(m)) == 0, "payload");
    return 0;
}

TEST(fifo_nonblock_open_rdonly) {
    char path[256];
    make_fifo_path(path, sizeof(path), "nb");
    unlink(path);
    if (mkfifo(path, 0600) != 0) {
        if (errno == ENOSYS || errno == EPERM || errno == EACCES) SKIP("mkfifo unsupported");
        CHECK(0, "mkfifo");
    }
    /* O_RDONLY|O_NONBLOCK on a FIFO succeeds immediately even with no
     * writer (POSIX).  Read should then return EAGAIN. */
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) { unlink(path); if (errno == ENOSYS) SKIP("fifo open ENOSYS"); CHECK(0, "open"); }
    char b[8];
    ssize_t r = read(fd, b, sizeof(b));
    int ok = (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) || r == 0;
    close(fd);
    unlink(path);
    CHECK(ok, "rdonly nonblock no-writer read");
    return 0;
}

/* FIFO blocking handshake: writer opens (blocks for reader), reader opens.
 * Use a child for the reader side. */
TEST(fifo_blocking_handshake) {
    char path[256];
    make_fifo_path(path, sizeof(path), "hs");
    unlink(path);
    if (mkfifo(path, 0600) != 0) {
        if (errno == ENOSYS || errno == EPERM || errno == EACCES) SKIP("mkfifo unsupported");
        CHECK(0, "mkfifo");
    }
    pid_t pid = fork();
    CHECK(pid >= 0, "fork");
    if (pid == 0) {
        int rfd = open(path, O_RDONLY);   /* blocks until writer opens */
        if (rfd < 0) _exit(3);
        char b[16] = {0};
        int rc = read_all(rfd, b, 6);
        close(rfd);
        _exit((rc == 0 && memcmp(b, "fifohs", 6) == 0) ? 0 : 1);
    }
    int wfd = open(path, O_WRONLY);       /* blocks until reader opens */
    if (wfd < 0) { unlink(path); CHECK(0, "open wronly"); }
    int wrc = (write_all(wfd, "fifohs", 6) == 0);
    close(wfd);
    int st; waitpid(pid, &st, 0);
    unlink(path);
    CHECK(wrc, "write");
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "reader child verified");
    return 0;
}

/* ------------------------------------------------------------------ *
 * F_GETPIPE_SZ / F_SETPIPE_SZ (Linux-specific, guarded)
 * ------------------------------------------------------------------ */

TEST(fcntl_getpipe_sz) {
#ifdef F_GETPIPE_SZ
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    int sz = fcntl(fds[1], F_GETPIPE_SZ);
    if (sz < 0 && (errno == EINVAL || errno == ENOSYS || errno == ENOTTY)) {
        close(fds[0]); close(fds[1]); SKIP("F_GETPIPE_SZ unsupported");
    }
    CHECK(sz > 0, "pipe size positive");
    close(fds[0]); close(fds[1]);
    return 0;
#else
    SKIP("no F_GETPIPE_SZ");
#endif
}

TEST(fcntl_setpipe_sz) {
#if defined(F_GETPIPE_SZ) && defined(F_SETPIPE_SZ)
    int fds[2]; CHECK(pipe(fds) == 0, "pipe");
    int cur = fcntl(fds[1], F_GETPIPE_SZ);
    if (cur < 0 && (errno == EINVAL || errno == ENOSYS || errno == ENOTTY)) {
        close(fds[0]); close(fds[1]); SKIP("F_GETPIPE_SZ unsupported");
    }
    CHECK(cur > 0, "current size");
    int want = cur * 2;
    int got = fcntl(fds[1], F_SETPIPE_SZ, want);
    if (got < 0 && (errno == EINVAL || errno == ENOSYS || errno == EPERM || errno == ENOTTY)) {
        close(fds[0]); close(fds[1]); SKIP("F_SETPIPE_SZ unsupported");
    }
    CHECK(got >= want || got > 0, "set returned a size");
    /* the buffer should now hold more than before */
    CHECK(fcntl(fds[1], F_GETPIPE_SZ) >= cur, "size grew or held");
    close(fds[0]); close(fds[1]);
    return 0;
#else
    SKIP("no F_SETPIPE_SZ");
#endif
}

/* ------------------------------------------------------------------ *
 * Many concurrent pipes
 * ------------------------------------------------------------------ */

TEST(many_pipes_roundtrip) {
    enum { NP = 200 };
    int p[NP][2];
    int opened = 0;
    for (int i = 0; i < NP; i++) {
        if (pipe(p[i]) != 0) break;       /* might hit fd limit */
        opened++;
    }
    CHECK(opened > 0, "at least one pipe");
    /* round-trip a tag through each */
    int ok = 1;
    for (int i = 0; i < opened; i++) {
        int v = i ^ 0x5a5a;
        if (write(p[i][1], &v, sizeof(v)) != (ssize_t)sizeof(v)) { ok = 0; break; }
        int back = 0;
        if (read(p[i][0], &back, sizeof(back)) != (ssize_t)sizeof(back)) { ok = 0; break; }
        if (back != v) { ok = 0; break; }
    }
    for (int i = 0; i < opened; i++) { close(p[i][0]); close(p[i][1]); }
    CHECK(ok, "all pipes round-trip");
    if (opened < NP) SKIP("fd limit < 200 pipes");  /* still exercised what we could */
    return 0;
}

/* ------------------------------------------------------------------ *
 * Interleaved large bidirectional via select-multiplex (4 MiB)
 * ------------------------------------------------------------------ */

TEST(bidir_select_4mib) {
    int p[2]; CHECK(pipe(p) == 0, "pipe");
    CHECK(set_nonblock(p[0]) == 0, "nb r");
    CHECK(set_nonblock(p[1]) == 0, "nb w");
    const size_t N = 4 * 1024 * 1024;
    size_t written = 0, read_total = 0;
    uint8_t wbuf[4096], rbuf[4096];
    for (size_t i = 0; i < sizeof(wbuf); i++) wbuf[i] = (uint8_t)i;
    uint64_t wsum = 0, rsum = 0;
    int spin = 0;
    while (read_total < N && spin++ < 2000000) {
        fd_set rset, wset;
        FD_ZERO(&rset); FD_ZERO(&wset);
        FD_SET(p[0], &rset);
        if (written < N) FD_SET(p[1], &wset);
        int maxfd = (p[0] > p[1] ? p[0] : p[1]) + 1;
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int n = select(maxfd, &rset, (written < N) ? &wset : NULL, NULL, &tv);
        if (n < 0) { CHECK(errno == EINTR, "select EINTR-only"); continue; }
        if (n == 0) break;                /* timeout = stalled */
        if (written < N && FD_ISSET(p[1], &wset)) {
            size_t want = N - written; if (want > sizeof(wbuf)) want = sizeof(wbuf);
            ssize_t w = write(p[1], wbuf, want);
            if (w > 0) { for (ssize_t i = 0; i < w; i++) wsum += wbuf[i]; written += w; }
        }
        if (FD_ISSET(p[0], &rset)) {
            ssize_t r = read(p[0], rbuf, sizeof(rbuf));
            if (r > 0) { for (ssize_t i = 0; i < r; i++) rsum += rbuf[i]; read_total += r; }
        }
    }
    close(p[0]); close(p[1]);
    CHECK(read_total == N, "4 MiB streamed");
    CHECK(wsum == rsum, "checksum");
    return 0;
}

/* Two pipes wired in one process, poll-multiplexed, 1 MiB each way. */
TEST(bidir_two_pipes_poll_1mib) {
    int a[2], b[2];
    CHECK(pipe(a) == 0, "pipe a");
    CHECK(pipe(b) == 0, "pipe b");
    CHECK(set_nonblock(a[0]) == 0 && set_nonblock(a[1]) == 0, "nb a");
    CHECK(set_nonblock(b[0]) == 0 && set_nonblock(b[1]) == 0, "nb b");
    const size_t N = 1024 * 1024;
    /* write into a[1], read from a[0]; write into b[1], read from b[0] */
    size_t wa = 0, ra = 0, wb = 0, rb = 0;
    uint8_t buf[4096];
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)(i * 7);
    uint64_t wsa = 0, rsa = 0, wsb = 0, rsb = 0;
    char tmp[4096];
    int spin = 0;
    while ((ra < N || rb < N) && spin++ < 4000000) {
        struct pollfd pf[4] = {
            { .fd = a[1], .events = (wa < N) ? POLLOUT : 0, .revents = 0 },
            { .fd = a[0], .events = (ra < N) ? POLLIN  : 0, .revents = 0 },
            { .fd = b[1], .events = (wb < N) ? POLLOUT : 0, .revents = 0 },
            { .fd = b[0], .events = (rb < N) ? POLLIN  : 0, .revents = 0 },
        };
        int n = poll(pf, 4, 1000);
        if (n < 0) { CHECK(errno == EINTR, "poll EINTR-only"); continue; }
        if (n == 0) break;
        if ((pf[0].revents & POLLOUT) && wa < N) {
            size_t want = N - wa; if (want > sizeof(buf)) want = sizeof(buf);
            ssize_t w = write(a[1], buf, want);
            if (w > 0) { for (ssize_t i = 0; i < w; i++) wsa += buf[i]; wa += w; }
        }
        if (pf[1].revents & POLLIN) {
            ssize_t r = read(a[0], tmp, sizeof(tmp));
            if (r > 0) { for (ssize_t i = 0; i < r; i++) rsa += (uint8_t)tmp[i]; ra += r; }
        }
        if ((pf[2].revents & POLLOUT) && wb < N) {
            size_t want = N - wb; if (want > sizeof(buf)) want = sizeof(buf);
            ssize_t w = write(b[1], buf, want);
            if (w > 0) { for (ssize_t i = 0; i < w; i++) wsb += buf[i]; wb += w; }
        }
        if (pf[3].revents & POLLIN) {
            ssize_t r = read(b[0], tmp, sizeof(tmp));
            if (r > 0) { for (ssize_t i = 0; i < r; i++) rsb += (uint8_t)tmp[i]; rb += r; }
        }
    }
    close(a[0]); close(a[1]); close(b[0]); close(b[1]);
    CHECK(ra == N && rb == N, "1 MiB each direction");
    CHECK(wsa == rsa && wsb == rsb, "checksums match");
    return 0;
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */

int main(void) {
    signal(SIGPIPE, SIG_IGN);            /* dedicated SIGPIPE test re-arms in its child */

    RUN(basic_single_byte);
    RUN(basic_string);
    RUN(exact_pipe_buf_write);
    RUN(multi_chunk);
    RUN(zero_length_write);
    RUN(zero_length_read);
    RUN(read_after_partial);

    RUN(size_1);
    RUN(size_2);
    RUN(size_63);
    RUN(size_64);
    RUN(size_4095);
    RUN(size_4096);
    RUN(size_4097);
    RUN(size_65535);
    RUN(size_65536);
    RUN(size_1mib);

    RUN(fill_then_drain);

    RUN(blocking_read_wakes_stress);
    RUN(blocking_write_wakes_stress);
    RUN(blocking_read_wakes_fork_stress);

    RUN(pingpong_fork_20000);
    RUN(pingpong_threaded_50000);

    RUN(n_writer_atomicity);

    RUN(eof_on_writer_close);
    RUN(partial_then_eof);
    RUN(eof_only_after_all_writers_close);

    RUN(sigpipe_then_epipe);
    RUN(epipe_with_sigpipe_ignored);

    RUN(nonblock_read_eagain);
    RUN(nonblock_write_eagain);
    RUN(pipe2_nonblock);
    RUN(pipe2_cloexec);
    RUN(pipe_no_cloexec_default);

    RUN(poll_pollin_after_park);
    RUN(poll_pollout_after_drain);
    RUN(poll_pollhup_after_writer_close);
    RUN(poll_timeout_zero);
    RUN(poll_timeout_expires);
    RUN(poll_negative_timeout_blocks);
    RUN(poll_two_pipes_bsdtar);

    RUN(select_readable_after_park);
    RUN(select_writable_after_drain);
    RUN(select_timeout_expires);

    RUN(fork_stream_64k);
    RUN(fork_stream_256k);
    RUN(fork_stream_1mib);

    RUN(dup2_stdio_echo);

    RUN(fifo_roundtrip_rdwr);
    RUN(fifo_nonblock_open_rdonly);
    RUN(fifo_blocking_handshake);

    RUN(fcntl_getpipe_sz);
    RUN(fcntl_setpipe_sz);

    RUN(many_pipes_roundtrip);

    RUN(bidir_select_4mib);
    RUN(bidir_two_pipes_poll_1mib);

    fprintf(stdout, "Result: %d/%d passed", tests_pass, tests_run);
    fprintf(stdout, " (FAILED=%d HANG=%d SKIP=%d)\n", tests_fail, tests_hang, tests_skip);
    return (tests_fail || tests_hang) ? 1 : 0;
}
