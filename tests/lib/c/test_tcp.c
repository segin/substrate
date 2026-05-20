/*
 * test_tcp.c — TCP client/server torture test.
 *
 * Portable POSIX C — runs on Linux as the reference baseline, then
 * cross-builds for substrate's libc/kernel.  Exercises the full
 * socket()/bind()/listen()/accept()/connect()/send()/recv() path on
 * AF_INET loopback: round-trips, large transfers, concurrency,
 * half-close, getpeername/getsockname, ECONNREFUSED, EPIPE.
 *
 * Build:
 *     cc -std=c99 -Wall -Wextra -pthread -o test_tcp test_tcp.c
 * Run:
 *     ./test_tcp
 *
 * Exit 0 on all-pass, 1 if any scenario fails.
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
#include <sys/socket.h>
#include <sys/types.h>
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

/* ---------- helpers ---------- */

/* Read exactly n bytes (or until EOF/error). */
static ssize_t read_all(int fd, void *buf, size_t n) {
    char *p = buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) break;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

static ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    size_t put = 0;
    while (put < n) {
        ssize_t w = write(fd, p + put, n - put);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        if (w == 0) return -1;
        put += (size_t)w;
    }
    return (ssize_t)put;
}

/* Bring up a listening socket on 127.0.0.1, ephemeral port.  Returns
 * the listen fd and writes the chosen port into *out_port. */
static int make_listener(int *out_port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0) { close(s); return -1; }
    socklen_t alen = sizeof(a);
    if (getsockname(s, (struct sockaddr *)&a, &alen) != 0) { close(s); return -1; }
    if (listen(s, 8) != 0) { close(s); return -1; }
    *out_port = ntohs(a.sin_port);
    return s;
}

static int connect_to(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    if (connect(s, (struct sockaddr *)&a, sizeof(a)) != 0) { close(s); return -1; }
    return s;
}

/* ---------- worker threads (file scope — no GCC nested functions,
 * which would need an executable stack for their trampolines) ----- */

struct echo_args {
    int listen_fd;
    int conns;       /* how many connections to service then exit */
};

static void *echo_server(void *arg) {
    struct echo_args *a = arg;
    for (int c = 0; c < a->conns; c++) {
        int cl = accept(a->listen_fd, NULL, NULL);
        if (cl < 0) break;
        /* Echo until EOF. */
        char buf[4096];
        for (;;) {
            ssize_t r = read(cl, buf, sizeof(buf));
            if (r <= 0) break;
            if (write_all(cl, buf, (size_t)r) < 0) break;
        }
        close(cl);
    }
    return NULL;
}

/* Concurrent echo server: accept ALL `conns` connections first, then
 * service each (one round-trip + close).  A plain sequential
 * echo_server would block in read() on connection 0 forever and
 * never accept 1..N — fine for sequential tests, deadlock for the
 * simultaneous-connections test. */
static void *concurrent_echo_server(void *arg) {
    struct echo_args *a = arg;
    int fds[64];
    int n = a->conns < 64 ? a->conns : 64;
    for (int i = 0; i < n; i++)
        fds[i] = accept(a->listen_fd, NULL, NULL);
    for (int i = 0; i < n; i++) {
        if (fds[i] < 0) continue;
        char buf[256];
        ssize_t r = read(fds[i], buf, sizeof(buf));
        if (r > 0) (void)write_all(fds[i], buf, (size_t)r);
        close(fds[i]);
    }
    return NULL;
}

/* Feeds `n` bytes into a socket then half-closes it (for the
 * large-transfer test, where one thread must write while the main
 * thread reads or both directions deadlock once buffers fill). */
struct writer_args { int fd; const unsigned char *buf; size_t n; };
static void *writer_thread(void *p) {
    struct writer_args *w = p;
    write_all(w->fd, w->buf, w->n);
    shutdown(w->fd, SHUT_WR);
    return NULL;
}

/* Half-close server: read to EOF, reply with the 4-byte big-endian
 * total it received, close. */
struct oneconn_args { int listen_fd; };
static void *halfclose_server(void *p) {
    struct oneconn_args *h = p;
    int cl = accept(h->listen_fd, NULL, NULL);
    if (cl < 0) return NULL;
    char buf[1024];
    uint32_t total = 0;
    for (;;) {
        ssize_t r = read(cl, buf, sizeof(buf));
        if (r <= 0) break;
        total += (uint32_t)r;
    }
    unsigned char out[4] = {
        (unsigned char)(total >> 24), (unsigned char)(total >> 16),
        (unsigned char)(total >> 8),  (unsigned char)total };
    write_all(cl, out, 4);
    close(cl);
    return NULL;
}

/* Accept-then-immediately-close server (for the EPIPE test). */
static void *closeimmediately_server(void *p) {
    struct oneconn_args *e = p;
    int cl = accept(e->listen_fd, NULL, NULL);
    if (cl >= 0) close(cl);
    return NULL;
}

/* ---------- scenarios ---------- */

/* 1. Basic connect/accept + small echo round-trip. */
static void test_basic_echo(void) {
    int port;
    int ls = make_listener(&port);
    if (ls < 0) { CHECK(0, "basic_echo: listener"); return; }

    pthread_t srv;
    struct echo_args ea = { ls, 1 };
    pthread_create(&srv, NULL, echo_server, &ea);

    int cs = connect_to(port);
    CHECK(cs >= 0, "basic_echo: connect");
    if (cs >= 0) {
        const char *msg = "hello tcp";
        write_all(cs, msg, strlen(msg));
        char buf[32] = {0};
        ssize_t r = read_all(cs, buf, strlen(msg));
        CHECK_EQ(r, (long)strlen(msg), "basic_echo: round-trip length");
        CHECK(memcmp(buf, msg, strlen(msg)) == 0, "basic_echo: round-trip data");
        close(cs);
    }
    pthread_join(srv, NULL);
    close(ls);
}

/* 2. Large transfer — bigger than any single socket buffer, so the
 *    stack has to segment, flow-control, and reassemble. */
static void test_large_transfer(void) {
    int port;
    int ls = make_listener(&port);
    if (ls < 0) { CHECK(0, "large_xfer: listener"); return; }

    pthread_t srv;
    struct echo_args ea = { ls, 1 };
    pthread_create(&srv, NULL, echo_server, &ea);

    int cs = connect_to(port);
    if (cs < 0) { CHECK(0, "large_xfer: connect"); pthread_join(srv, NULL); close(ls); return; }

    const size_t N = 256 * 1024;
    unsigned char *out = malloc(N), *in = malloc(N);
    if (!out || !in) { CHECK(0, "large_xfer: malloc"); free(out); free(in);
                       close(cs); pthread_join(srv, NULL); close(ls); return; }
    for (size_t i = 0; i < N; i++) out[i] = (unsigned char)(i * 31 + 7);

    /* A writer thread feeds the socket while we drain it — a single
     * thread doing write_all then read_all would deadlock once both
     * direction buffers fill. */
    pthread_t wt;
    struct writer_args wa = { cs, out, N };
    pthread_create(&wt, NULL, writer_thread, &wa);

    ssize_t got = read_all(cs, in, N);
    pthread_join(wt, NULL);
    CHECK_EQ(got, (long)N, "large_xfer: full length echoed back");
    CHECK(got == (ssize_t)N && memcmp(out, in, N) == 0,
          "large_xfer: 256 KiB round-trips intact");

    free(out); free(in);
    close(cs);
    pthread_join(srv, NULL);
    close(ls);
}

/* 3. Many sequential connections against one listener. */
static void test_sequential_conns(void) {
    int port;
    int ls = make_listener(&port);
    if (ls < 0) { CHECK(0, "seq_conns: listener"); return; }

    const int NCONN = 16;
    pthread_t srv;
    struct echo_args ea = { ls, NCONN };
    pthread_create(&srv, NULL, echo_server, &ea);

    int ok = 1;
    for (int i = 0; i < NCONN; i++) {
        int cs = connect_to(port);
        if (cs < 0) { ok = 0; break; }
        char snd[16], rcv[16] = {0};
        int n = snprintf(snd, sizeof(snd), "conn-%d", i);
        write_all(cs, snd, (size_t)n);
        if (read_all(cs, rcv, (size_t)n) != n || memcmp(snd, rcv, (size_t)n) != 0)
            ok = 0;
        close(cs);
        if (!ok) break;
    }
    CHECK(ok, "seq_conns: 16 sequential connections all echo correctly");
    pthread_join(srv, NULL);
    close(ls);
}

/* 4. Concurrent connections — several open at once. */
static void test_concurrent_conns(void) {
    int port;
    int ls = make_listener(&port);
    if (ls < 0) { CHECK(0, "concur_conns: listener"); return; }

    const int NCONN = 8;
    pthread_t srv;
    struct echo_args ea = { ls, NCONN };
    pthread_create(&srv, NULL, concurrent_echo_server, &ea);

    int fds[8];
    int ok = 1;
    for (int i = 0; i < NCONN; i++) {
        fds[i] = connect_to(port);
        if (fds[i] < 0) ok = 0;
    }
    /* Write to all, then read from all. */
    for (int i = 0; i < NCONN && ok; i++) {
        char b[16];
        int n = snprintf(b, sizeof(b), "x%d", i);
        if (write_all(fds[i], b, (size_t)n) < 0) ok = 0;
    }
    for (int i = 0; i < NCONN && ok; i++) {
        char b[16], want[16];
        int n = snprintf(want, sizeof(want), "x%d", i);
        if (read_all(fds[i], b, (size_t)n) != n || memcmp(b, want, (size_t)n) != 0)
            ok = 0;
    }
    for (int i = 0; i < NCONN; i++) if (fds[i] >= 0) close(fds[i]);
    CHECK(ok, "concur_conns: 8 simultaneous connections all echo correctly");
    pthread_join(srv, NULL);
    close(ls);
}

/* 5. connect() to a port nobody listens on → ECONNREFUSED. */
static void test_connect_refused(void) {
    /* Bind+listen+close to obtain a port guaranteed free. */
    int port;
    int ls = make_listener(&port);
    if (ls < 0) { CHECK(0, "refused: listener"); return; }
    close(ls);   /* port is now free, nothing listening */

    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)port);
    int r = connect(s, (struct sockaddr *)&a, sizeof(a));
    CHECK(r < 0 && errno == ECONNREFUSED,
          "refused: connect to closed port gives ECONNREFUSED");
    close(s);
}

/* 6. getpeername / getsockname on a connected pair. */
static void test_peer_sock_names(void) {
    int port;
    int ls = make_listener(&port);
    if (ls < 0) { CHECK(0, "names: listener"); return; }

    pthread_t srv;
    struct echo_args ea = { ls, 1 };
    pthread_create(&srv, NULL, echo_server, &ea);

    int cs = connect_to(port);
    if (cs < 0) { CHECK(0, "names: connect"); pthread_join(srv, NULL); close(ls); return; }

    struct sockaddr_in pn = {0}, sn = {0};
    socklen_t pl = sizeof(pn), sl = sizeof(sn);
    int rp = getpeername(cs, (struct sockaddr *)&pn, &pl);
    int rs = getsockname(cs, (struct sockaddr *)&sn, &sl);
    CHECK(rp == 0, "names: getpeername succeeds");
    CHECK(rs == 0, "names: getsockname succeeds");
    CHECK(rp == 0 && ntohs(pn.sin_port) == port,
          "names: peer port matches the listener port");
    CHECK(rp == 0 && pn.sin_addr.s_addr == htonl(INADDR_LOOPBACK),
          "names: peer addr is 127.0.0.1");
    CHECK(rs == 0 && sn.sin_addr.s_addr == htonl(INADDR_LOOPBACK),
          "names: local addr is 127.0.0.1");

    /* The bytes written to the connection's iov: drain via the
     * server echo so the join doesn't block. */
    write_all(cs, "z", 1);
    char tmp[1];
    (void)read_all(cs, tmp, 1);
    close(cs);
    pthread_join(srv, NULL);
    close(ls);
}

/* 7. Half-close: client shutdown(SHUT_WR); server sees EOF, can
 *    still send its final reply; client drains it. */
static void test_half_close(void) {
    int port;
    int ls = make_listener(&port);
    if (ls < 0) { CHECK(0, "half_close: listener"); return; }

    pthread_t srv;
    struct oneconn_args ha = { ls };
    pthread_create(&srv, NULL, halfclose_server, &ha);

    int cs = connect_to(port);
    if (cs < 0) { CHECK(0, "half_close: connect"); pthread_join(srv, NULL); close(ls); return; }

    const char *payload = "half-close-payload-1234567890";
    write_all(cs, payload, strlen(payload));
    /* Close our write half — server must see EOF but our read half
     * stays open for its reply. */
    CHECK_EQ(shutdown(cs, SHUT_WR), 0, "half_close: shutdown(SHUT_WR) ok");

    unsigned char rc[4] = {0};
    ssize_t r = read_all(cs, rc, 4);
    uint32_t got = ((uint32_t)rc[0] << 24) | ((uint32_t)rc[1] << 16) |
                   ((uint32_t)rc[2] << 8) | rc[3];
    CHECK_EQ(r, 4, "half_close: reply still readable after our SHUT_WR");
    CHECK_EQ(got, (long)strlen(payload),
             "half_close: server counted every pre-shutdown byte");
    close(cs);
    pthread_join(srv, NULL);
    close(ls);
}

/* 8. Write to a connection whose peer has closed → EPIPE (with
 *    SIGPIPE ignored so we see the errno rather than dying). */
static void test_write_after_peer_close(void) {
    signal(SIGPIPE, SIG_IGN);
    int port;
    int ls = make_listener(&port);
    if (ls < 0) { CHECK(0, "epipe: listener"); return; }

    /* Server: accept, immediately close. */
    pthread_t srv;
    struct oneconn_args eca = { ls };
    pthread_create(&srv, NULL, closeimmediately_server, &eca);

    int cs = connect_to(port);
    if (cs < 0) { CHECK(0, "epipe: connect"); pthread_join(srv, NULL); close(ls); return; }
    pthread_join(srv, NULL);   /* ensure server has closed */

    /* First write may succeed (buffered) or get RST; keep writing
     * until the stack reports the broken pipe. */
    int saw_epipe = 0;
    for (int i = 0; i < 100; i++) {
        ssize_t w = write(cs, "spew", 4);
        if (w < 0 && (errno == EPIPE || errno == ECONNRESET)) { saw_epipe = 1; break; }
        struct timespec ts = { 0, 10 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }
    CHECK(saw_epipe, "epipe: write to closed peer eventually fails EPIPE/ECONNRESET");
    close(cs);
    close(ls);
    signal(SIGPIPE, SIG_DFL);
}

int main(void) {
    test_basic_echo();
    test_large_transfer();
    test_sequential_conns();
    test_concurrent_conns();
    test_connect_refused();
    test_peer_sock_names();
    test_half_close();
    test_write_after_peer_close();

    printf("\n=== test_tcp: %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
