/*
 * test_select.c — torture test for select(2).
 *
 * Portable POSIX C — runs on Linux as the reference baseline.
 * Same source builds against substrate's libc once host validation
 * passes.
 *
 * Build:
 *     cc -std=c99 -Wall -Wextra -pthread -o test_select test_select.c
 * Run:
 *     ./test_select
 *
 * Exit 0 on all-pass, 1 if any scenario fails.
 *
 * Motivating bug: inetutils telnet doesn't refresh the screen when
 * data arrives on the network socket until the user presses a key —
 * which strongly suggests select() isn't waking the calling thread
 * when only one of N fds becomes ready (the multi-channel-wait case).
 * The scenarios below isolate that and several adjacent corner cases.
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/* ---------- test framework ---------- */

static int passed = 0;
static int failed = 0;

#define CHECK(cond, name) do { \
    if (cond) { passed++; printf("[ OK ] %s\n", name); } \
    else      { failed++; printf("[FAIL] %s (%s:%d errno=%d:%s)\n", \
                                  name, __FILE__, __LINE__, errno, strerror(errno)); } \
} while (0)

#define CHECK_EQ(a, b, name) do { \
    long _a = (long)(a), _b = (long)(b); \
    if (_a == _b) { passed++; printf("[ OK ] %s\n", name); } \
    else { failed++; printf("[FAIL] %s: got=%ld want=%ld\n", name, _a, _b); } \
} while (0)

static long elapsed_ms_since(struct timeval *t0) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - t0->tv_sec) * 1000L +
           (now.tv_usec - t0->tv_usec) / 1000L;
}

/* ---------- scenarios ---------- */

/*
 * 1. Zero-timeout poll, no fds ready.
 *    select() should return 0 immediately, no fds set in any bitmap.
 */
static void test_zero_timeout_no_ready(void) {
    int p[2];
    if (pipe(p) != 0) { CHECK(0, "zero_timeout: pipe"); return; }

    fd_set rfds; FD_ZERO(&rfds); FD_SET(p[0], &rfds);
    struct timeval tv = {0, 0};
    int r = select(p[0] + 1, &rfds, NULL, NULL, &tv);
    CHECK_EQ(r, 0, "zero_timeout: returns 0 when no fd ready");
    CHECK(!FD_ISSET(p[0], &rfds), "zero_timeout: rfds cleared");
    close(p[0]); close(p[1]);
}

/*
 * 2. Single-fd read readiness — data is already pending.
 */
static void test_single_fd_pre_ready(void) {
    int p[2];
    if (pipe(p) != 0) { CHECK(0, "pre_ready: pipe"); return; }
    write(p[1], "x", 1);

    fd_set rfds; FD_ZERO(&rfds); FD_SET(p[0], &rfds);
    struct timeval tv = {1, 0};
    int r = select(p[0] + 1, &rfds, NULL, NULL, &tv);
    CHECK_EQ(r, 1, "pre_ready: returns 1");
    CHECK(FD_ISSET(p[0], &rfds), "pre_ready: read fd is set");
    close(p[0]); close(p[1]);
}

/*
 * 3. Single-fd write readiness — empty pipe is writable.
 */
static void test_single_fd_writable(void) {
    int p[2];
    if (pipe(p) != 0) { CHECK(0, "writable: pipe"); return; }

    fd_set wfds; FD_ZERO(&wfds); FD_SET(p[1], &wfds);
    struct timeval tv = {1, 0};
    int r = select(p[1] + 1, NULL, &wfds, NULL, &tv);
    CHECK_EQ(r, 1, "writable: returns 1 for empty pipe writability");
    CHECK(FD_ISSET(p[1], &wfds), "writable: write fd is set");
    close(p[0]); close(p[1]);
}

/*
 * 4. Pure-timeout block — select() with no ready fd and a short
 *    timeout should sleep ~that long and return 0.
 */
static void test_pure_timeout(void) {
    int p[2];
    if (pipe(p) != 0) { CHECK(0, "pure_timeout: pipe"); return; }

    fd_set rfds; FD_ZERO(&rfds); FD_SET(p[0], &rfds);
    struct timeval tv = {0, 200 * 1000};  /* 200 ms */
    struct timeval t0; gettimeofday(&t0, NULL);
    int r = select(p[0] + 1, &rfds, NULL, NULL, &tv);
    long elapsed = elapsed_ms_since(&t0);
    CHECK_EQ(r, 0, "pure_timeout: returns 0 after timeout");
    /* Tolerate jitter: 150..1000 ms is fine.  Failing this means
     * either the timer is wildly off or select() returned early. */
    CHECK(elapsed >= 150 && elapsed < 1000,
          "pure_timeout: blocked roughly the requested duration");
    close(p[0]); close(p[1]);
}

/*
 * Producer thread for the multi-fd latency tests.  After `delay_ms`,
 * writes one byte to `fd`.
 */
struct producer_args {
    int fd;
    int delay_ms;
};
static void *producer_thread(void *arg) {
    struct producer_args *a = arg;
    struct timespec ts = {0, (long)a->delay_ms * 1000000L};
    nanosleep(&ts, NULL);
    write(a->fd, "x", 1);
    return NULL;
}

/*
 * 5. THE BUG: multi-fd select() with no fd initially ready, then a
 *    producer thread injects a byte on ONE of the read fds.  select()
 *    must wake within a small bounded latency — not block until the
 *    other fd happens to fire or until some periodic re-poll.
 */
static void test_multi_fd_wake_on_one(void) {
    int p1[2], p2[2];
    if (pipe(p1) != 0 || pipe(p2) != 0) { CHECK(0, "multi_wake: pipe"); return; }

    /* Inject on p2 read end after 50 ms. */
    pthread_t th;
    struct producer_args a = { p2[1], 50 };
    pthread_create(&th, NULL, producer_thread, &a);

    fd_set rfds; FD_ZERO(&rfds); FD_SET(p1[0], &rfds); FD_SET(p2[0], &rfds);
    int maxfd = p1[0] > p2[0] ? p1[0] : p2[0];
    struct timeval tv = {2, 0};  /* generous 2s cap */
    struct timeval t0; gettimeofday(&t0, NULL);
    int r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    long elapsed = elapsed_ms_since(&t0);

    pthread_join(th, NULL);
    CHECK_EQ(r, 1, "multi_wake: returns 1 when p2 becomes readable");
    CHECK(!FD_ISSET(p1[0], &rfds), "multi_wake: p1 not marked ready");
    CHECK(FD_ISSET(p2[0], &rfds), "multi_wake: p2 marked ready");
    /* Latency: should be ~50ms.  Allow up to 500ms before flagging as
     * a wake-channel bug. */
    CHECK(elapsed < 500,
          "multi_wake: latency under 500 ms (telnet-bug indicator)");
    if (elapsed >= 500)
        printf("       latency was %ld ms\n", elapsed);

    close(p1[0]); close(p1[1]); close(p2[0]); close(p2[1]);
}

/*
 * 6. Telnet's read-side shape: select() with kbd + socket-read in the
 *    read set, neither initially ready, then producer fires on the
 *    socket.  Must wake promptly with sock readable and kbd not.
 *    This is the exact "incoming network text doesn't update without
 *    a keystroke" reproduction from inetutils telnet.
 */
static void test_telnet_shape(void) {
    int kbd[2];      /* fake stdin */
    int sock_r[2];   /* incoming network */
    if (pipe(kbd) != 0 || pipe(sock_r) != 0) {
        CHECK(0, "telnet_shape: pipe"); return;
    }

    pthread_t th;
    struct producer_args a = { sock_r[1], 80 };
    pthread_create(&th, NULL, producer_thread, &a);

    fd_set rfds;
    FD_ZERO(&rfds); FD_SET(kbd[0], &rfds); FD_SET(sock_r[0], &rfds);
    int maxfd = kbd[0] > sock_r[0] ? kbd[0] : sock_r[0];

    struct timeval tv = {2, 0};
    struct timeval t0; gettimeofday(&t0, NULL);
    int r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    long elapsed = elapsed_ms_since(&t0);

    pthread_join(th, NULL);
    CHECK_EQ(r, 1, "telnet_shape: returns 1");
    CHECK(FD_ISSET(sock_r[0], &rfds), "telnet_shape: sock_r readable");
    CHECK(!FD_ISSET(kbd[0], &rfds), "telnet_shape: kbd NOT readable");
    CHECK(elapsed < 500,
          "telnet_shape: woke promptly on socket data (telnet-bug indicator)");
    if (elapsed >= 500)
        printf("       latency was %ld ms\n", elapsed);

    close(kbd[0]); close(kbd[1]);
    close(sock_r[0]); close(sock_r[1]);
}

/*
 * 7. select() with NULL timeout but EINTR via SIGALRM: must return
 *    -1 with errno=EINTR.  Bounded with a 2-second worker-thread
 *    fallback wake so the test doesn't hang on platforms where
 *    setitimer/SIGALRM isn't wired up.
 */
static void sigalrm_handler(int sig) { (void)sig; }

struct eintr_fallback_args {
    pthread_t target;
    int delay_ms;
};
static void *eintr_fallback_thread(void *arg) {
    struct eintr_fallback_args *a = arg;
    struct timespec ts = {0, (long)a->delay_ms * 1000000L};
    nanosleep(&ts, NULL);
    pthread_kill(a->target, SIGUSR1);
    return NULL;
}
static void sigusr1_handler(int sig) { (void)sig; }

static void test_eintr_signal(void) {
    int p[2];
    if (pipe(p) != 0) { CHECK(0, "eintr: pipe"); return; }

    struct sigaction sa = {0};
    sa.sa_handler = sigalrm_handler;
    sigaction(SIGALRM, &sa, NULL);
    struct sigaction su = {0};
    su.sa_handler = sigusr1_handler;
    sigaction(SIGUSR1, &su, NULL);

    fd_set rfds; FD_ZERO(&rfds); FD_SET(p[0], &rfds);
    struct itimerval it = {{0,0}, {0, 100 * 1000}};  /* 100ms */
    setitimer(ITIMER_REAL, &it, NULL);

    /* Fallback: if SIGALRM never arrives, a worker thread will kick
     * us via SIGUSR1 after 2s so we don't hang. */
    pthread_t fb;
    struct eintr_fallback_args fba = { pthread_self(), 2000 };
    pthread_create(&fb, NULL, eintr_fallback_thread, &fba);

    struct timeval t0; gettimeofday(&t0, NULL);
    struct timeval cap = {3, 0};  /* hard cap; substrate setitimer hang fail-safe */
    int r = select(p[0] + 1, &rfds, NULL, NULL, &cap);
    long elapsed = elapsed_ms_since(&t0);
    int saved_errno = errno;

    pthread_join(fb, NULL);
    struct itimerval none = {{0,0},{0,0}};
    setitimer(ITIMER_REAL, &none, NULL);
    signal(SIGALRM, SIG_DFL);
    signal(SIGUSR1, SIG_DFL);

    CHECK_EQ(r, -1, "eintr: returns -1 on signal");
    CHECK_EQ(saved_errno, EINTR, "eintr: errno is EINTR");
    CHECK(elapsed < 2500, "eintr: returned promptly (<2.5s)");
    if (elapsed >= 1500)
        printf("       SIGALRM delivery is slow or broken (took %ld ms, fallback path)\n", elapsed);
    close(p[0]); close(p[1]);
}

/*
 * 8. Same fd in both read and write sets — should produce two
 *    ready indications when both directions are ready.
 */
static void test_same_fd_read_and_write(void) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        CHECK(0, "same_fd: socketpair"); return;
    }
    write(sv[1], "x", 1);  /* make sv[0] readable */

    fd_set rfds, wfds;
    FD_ZERO(&rfds); FD_SET(sv[0], &rfds);
    FD_ZERO(&wfds); FD_SET(sv[0], &wfds);
    struct timeval tv = {1, 0};
    int r = select(sv[0] + 1, &rfds, &wfds, NULL, &tv);
    CHECK(r >= 2, "same_fd: returns >= 2 when both r+w ready");
    CHECK(FD_ISSET(sv[0], &rfds), "same_fd: read side set");
    CHECK(FD_ISSET(sv[0], &wfds), "same_fd: write side set");
    close(sv[0]); close(sv[1]);
}

/*
 * 9. Re-entry: call select() in a tight loop and verify that the
 *    bitmaps are properly reset each time.  Catches "FD_ZERO not
 *    honoured" bugs where a previous bit lingers.
 */
static void test_bitmap_reset(void) {
    int p[2];
    if (pipe(p) != 0) { CHECK(0, "reset: pipe"); return; }

    int ok = 1;
    for (int i = 0; i < 5; i++) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(p[0], &rfds);
        struct timeval tv = {0, 10 * 1000};
        int r = select(p[0] + 1, &rfds, NULL, NULL, &tv);
        if (r != 0 || FD_ISSET(p[0], &rfds)) { ok = 0; break; }
    }
    CHECK(ok, "reset: 5 zero-data iterations stay clean");
    close(p[0]); close(p[1]);
}

/*
 * 10a. The actual telnet bug: multi-fd wake on a UNIX SOCKETPAIR.
 *      pipes use one wait channel; AF_UNIX sockets use a separate
 *      af_unix wait channel.  If substrate's `multi_chan` poll fallback
 *      doesn't register on socket wait queues, telnet-shaped programs
 *      reading from one fd of a pair while waiting on kbd won't wake.
 */
static void test_socketpair_multi_wake(void) {
    int sv[2], kbd[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        CHECK(0, "sp_multi: socketpair"); return;
    }
    if (pipe(kbd) != 0) { CHECK(0, "sp_multi: pipe"); close(sv[0]); close(sv[1]); return; }

    pthread_t th;
    struct producer_args a = { sv[1], 80 };  /* peer sends a byte after 80 ms */
    pthread_create(&th, NULL, producer_thread, &a);

    fd_set rfds; FD_ZERO(&rfds); FD_SET(kbd[0], &rfds); FD_SET(sv[0], &rfds);
    int maxfd = kbd[0] > sv[0] ? kbd[0] : sv[0];
    struct timeval tv = {2, 0};
    struct timeval t0; gettimeofday(&t0, NULL);
    int r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    long elapsed = elapsed_ms_since(&t0);
    pthread_join(th, NULL);

    CHECK_EQ(r, 1, "sp_multi: returns 1 on socket data");
    CHECK(FD_ISSET(sv[0], &rfds),  "sp_multi: socket marked readable");
    CHECK(!FD_ISSET(kbd[0], &rfds), "sp_multi: kbd NOT readable");
    CHECK(elapsed < 500, "sp_multi: latency under 500 ms");
    if (elapsed >= 500)
        printf("       latency was %ld ms (telnet-bug indicator: af_unix wake)\n", elapsed);

    close(sv[0]); close(sv[1]); close(kbd[0]); close(kbd[1]);
}

/*
 * 10b. AF_INET TCP loopback — the actual telnet code path.  Spin up
 *      a listener on 127.0.0.1:0, connect, then have a worker send
 *      data after a delay.  select() on {kbd, tcp_client} must wake
 *      promptly when bytes arrive on the TCP fd.
 */
struct tcp_send_args {
    int sock;
    int delay_ms;
};
static void *tcp_send_thread(void *arg) {
    struct tcp_send_args *a = arg;
    struct timespec ts = {0, (long)a->delay_ms * 1000000L};
    nanosleep(&ts, NULL);
    send(a->sock, "x", 1, 0);
    return NULL;
}

static void test_tcp_multi_wake(void) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { CHECK(0, "tcp_multi: socket(listener)"); return; }
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        CHECK(0, "tcp_multi: bind"); close(listener); return;
    }
    socklen_t alen = sizeof(addr);
    if (getsockname(listener, (struct sockaddr *)&addr, &alen) != 0) {
        CHECK(0, "tcp_multi: getsockname"); close(listener); return;
    }
    if (listen(listener, 1) != 0) {
        CHECK(0, "tcp_multi: listen"); close(listener); return;
    }

    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) { CHECK(0, "tcp_multi: socket(client)"); close(listener); return; }
    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        CHECK(0, "tcp_multi: connect"); close(client); close(listener); return;
    }
    int server = accept(listener, NULL, NULL);
    if (server < 0) {
        CHECK(0, "tcp_multi: accept"); close(client); close(listener); return;
    }

    int kbd[2];
    if (pipe(kbd) != 0) {
        CHECK(0, "tcp_multi: pipe"); close(server); close(client); close(listener); return;
    }

    pthread_t th;
    struct tcp_send_args a = { server, 80 };
    pthread_create(&th, NULL, tcp_send_thread, &a);

    fd_set rfds; FD_ZERO(&rfds); FD_SET(kbd[0], &rfds); FD_SET(client, &rfds);
    int maxfd = kbd[0] > client ? kbd[0] : client;
    struct timeval tv = {2, 0};
    struct timeval t0; gettimeofday(&t0, NULL);
    int r = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    long elapsed = elapsed_ms_since(&t0);
    pthread_join(th, NULL);

    CHECK_EQ(r, 1, "tcp_multi: returns 1 on TCP data");
    CHECK(FD_ISSET(client, &rfds), "tcp_multi: TCP fd marked readable");
    CHECK(!FD_ISSET(kbd[0], &rfds), "tcp_multi: kbd NOT readable");
    CHECK(elapsed < 500, "tcp_multi: latency under 500 ms (the telnet bug)");
    if (elapsed >= 500)
        printf("       latency was %ld ms (telnet-bug indicator: tcp_poll wake)\n", elapsed);

    close(kbd[0]); close(kbd[1]);
    close(client); close(server); close(listener);
}

/*
 * 10. nfds = 0 with all-NULL sets and a tiny timeout.  Should behave
 *     like usleep(): block for the timeout, return 0.
 */
static void test_select_as_sleep(void) {
    struct timeval tv = {0, 100 * 1000};  /* 100ms */
    struct timeval t0; gettimeofday(&t0, NULL);
    int r = select(0, NULL, NULL, NULL, &tv);
    long elapsed = elapsed_ms_since(&t0);
    CHECK_EQ(r, 0, "as_sleep: returns 0");
    CHECK(elapsed >= 80 && elapsed < 500, "as_sleep: blocked ~100ms");
}

int main(void) {
    test_zero_timeout_no_ready();
    test_single_fd_pre_ready();
    test_single_fd_writable();
    test_pure_timeout();
    test_multi_fd_wake_on_one();
    test_telnet_shape();
    test_eintr_signal();
    test_same_fd_read_and_write();
    test_bitmap_reset();
    test_socketpair_multi_wake();
    test_tcp_multi_wake();
    test_select_as_sleep();

    printf("\n=== test_select: %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
