/*
 * torture_tcp_net.c — two-machine TCP torture client (substrate side).
 *
 * Drives substrate's TCP/IPv4 stack against a real remote peer
 * (tcp_partner running on another host on the LAN), so the path under
 * test is the NIC + driver + IP + TCP, not loopback.  Each scenario
 * runs on a fresh connection and verifies payload integrity byte-for-
 * byte via the shared LCG stream.
 *
 *   build:  i386-unknown-substrate-gcc -o torture_tcp_net torture_tcp_net.c
 *   run:    torture_tcp_net <partner-host> [port]
 *
 * The partner address is ALWAYS an argument — never compiled in.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "tcp_torture.h"

static const char *g_host;
static uint16_t    g_port;
static int failures = 0;

static void ok(const char *name, int pass, const char *detail) {
    printf("  %-22s %s%s%s\n", name, pass ? "PASS" : "FAIL",
           detail && *detail ? " — " : "", detail ? detail : "");
    if (!pass) failures++;
}

/* Open a fresh connection to the partner.  Returns fd or -1. */
static int tt_connect(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(g_port);
    sa.sin_addr.s_addr = inet_addr(g_host);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_req(int fd, uint32_t scen, uint32_t len, uint32_t seed, uint32_t flags) {
    struct tt_req req;
    req.magic    = htonl(TT_MAGIC);
    req.scenario = htonl(scen);
    req.length   = htonl(len);
    req.seed     = htonl(seed);
    req.flags    = htonl(flags);
    return tt_writen(fd, &req, sizeof req) == (ssize_t)sizeof req ? 0 : -1;
}

static int read_reply(int fd, struct tt_reply *rep) {
    if (tt_readn(fd, rep, sizeof *rep) != (ssize_t)sizeof *rep) return -1;
    rep->status   = ntohl(rep->status);
    rep->received = ntohl(rep->received);
    return 0;
}

/* SC_ECHO: send N LCG bytes, read them back, verify they match. */
static void sc_echo(uint32_t len, uint32_t seed) {
    int fd = tt_connect();
    if (fd < 0) { ok("echo connect", 0, strerror(errno)); return; }
    char d[48];
    if (send_req(fd, SC_ECHO, len, seed, 0) != 0) { ok("echo", 0, "req"); close(fd); return; }
    if (tt_send_stream(fd, seed, len) != 0)        { ok("echo", 0, "send"); close(fd); return; }
    uint32_t rcv = 0;
    long v = tt_verify_stream(fd, seed, len, &rcv);
    snprintf(d, sizeof d, "%u B, echo back %u B", len, rcv);
    ok("echo round-trip", v == 0, d);
    close(fd);
}

/* SC_DOWNLOAD: partner sends N LCG bytes; verify them. */
static void sc_download(uint32_t len, uint32_t seed) {
    int fd = tt_connect();
    if (fd < 0) { ok("download connect", 0, strerror(errno)); return; }
    char d[48];
    if (send_req(fd, SC_DOWNLOAD, len, seed, 0) != 0) { ok("download", 0, "req"); close(fd); return; }
    uint32_t rcv = 0;
    long v = tt_verify_stream(fd, seed, len, &rcv);
    snprintf(d, sizeof d, "got %u/%u B%s", rcv, len, v > 0 ? " (mismatch)" : "");
    ok("download verify", v == 0, d);
    close(fd);
}

/* SC_UPLOAD: send N LCG bytes; partner verifies + reports. */
static void sc_upload(uint32_t len, uint32_t seed) {
    int fd = tt_connect();
    if (fd < 0) { ok("upload connect", 0, strerror(errno)); return; }
    char d[48];
    if (send_req(fd, SC_UPLOAD, len, seed, 0) != 0) { ok("upload", 0, "req"); close(fd); return; }
    if (tt_send_stream(fd, seed, len) != 0)          { ok("upload", 0, "send"); close(fd); return; }
    struct tt_reply rep;
    if (read_reply(fd, &rep) != 0) { ok("upload", 0, "no reply"); close(fd); return; }
    snprintf(d, sizeof d, "partner got %u/%u B status=%u", rep.received, len, rep.status);
    ok("upload verify", rep.status == 0 && rep.received == len, d);
    close(fd);
}

/* SC_SLOWREAD: send N bytes to a partner that drains slowly — exercises
 * substrate's send-side flow control / zero-window persist timer. */
static void sc_slowread(uint32_t len, uint32_t seed, uint32_t delay_ms) {
    int fd = tt_connect();
    if (fd < 0) { ok("slowread connect", 0, strerror(errno)); return; }
    char d[56];
    if (send_req(fd, SC_SLOWREAD, len, seed, delay_ms) != 0) { ok("slowread", 0, "req"); close(fd); return; }
    if (tt_send_stream(fd, seed, len) != 0)                   { ok("slowread", 0, "send"); close(fd); return; }
    struct tt_reply rep;
    if (read_reply(fd, &rep) != 0) { ok("slowread", 0, "no reply"); close(fd); return; }
    snprintf(d, sizeof d, "drained %u/%u B @ %ums status=%u", rep.received, len, delay_ms, rep.status);
    ok("slowread backpressure", rep.status == 0 && rep.received == len, d);
    close(fd);
}

/* SC_HALFCLOSE: send N bytes then SHUT_WR; read reply then EOF. */
static void sc_halfclose(uint32_t len, uint32_t seed) {
    int fd = tt_connect();
    if (fd < 0) { ok("halfclose connect", 0, strerror(errno)); return; }
    char d[48];
    if (send_req(fd, SC_HALFCLOSE, len, seed, 0) != 0) { ok("halfclose", 0, "req"); close(fd); return; }
    if (tt_send_stream(fd, seed, len) != 0)             { ok("halfclose", 0, "send"); close(fd); return; }
    shutdown(fd, SHUT_WR);
    struct tt_reply rep;
    if (read_reply(fd, &rep) != 0) { ok("halfclose", 0, "no reply"); close(fd); return; }
    snprintf(d, sizeof d, "partner got %u/%u B status=%u", rep.received, len, rep.status);
    ok("halfclose drain", rep.status == 0 && rep.received == len, d);
    close(fd);
}

/* ---- substrate-as-server (inbound) scenarios ---------------------- */

/* Bind + listen on `port` (INADDR_ANY).  Returns the listen fd or -1. */
static int make_listener(uint16_t port, int backlog) {
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) return -1;
    int on = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons(port);
    if (bind(ls, (struct sockaddr *)&sa, sizeof sa) < 0) { close(ls); return -1; }
    if (listen(ls, backlog) < 0) { close(ls); return -1; }
    return ls;
}

/* Open a control connection and ask the partner to dial back `count`
 * times to our listener on `lport`, sending `len` bytes (seed `seed`) on
 * each.  Returns 0 once the partner acks the fan-out launch, else -1.
 * The control fd is closed before returning. */
static int request_reverse(uint16_t lport, uint32_t count, uint32_t len, uint32_t seed) {
    int cc = tt_connect();
    if (cc < 0) return -1;
    int rc = -1;
    if (send_req(cc, SC_REVERSE, 0, 0, 0) == 0) {
        struct tt_reverse rv;
        rv.lport  = htonl(lport);
        rv.count  = htonl(count);
        rv.length = htonl(len);
        rv.seed   = htonl(seed);
        if (tt_writen(cc, &rv, sizeof rv) == (ssize_t)sizeof rv) {
            struct tt_reply rep;
            if (read_reply(cc, &rep) == 0) rc = 0;
        }
    }
    close(cc);
    return rc;
}

/* SC_REVERSE (count=1): partner dials in and uploads; substrate accepts
 * and verifies — the inbound/server data path. */
static void sc_server_recv(uint32_t len, uint32_t seed) {
    int ls = make_listener(TT_RPORT, 4);
    if (ls < 0) { ok("server listen", 0, strerror(errno)); return; }
    if (request_reverse(TT_RPORT, 1, len, seed) != 0) {
        ok("server reverse req", 0, "control"); close(ls); return;
    }
    int c = accept(ls, NULL, NULL);
    if (c < 0) { ok("server accept", 0, strerror(errno)); close(ls); return; }
    uint32_t rcv = 0;
    long v = tt_verify_stream(c, seed, len, &rcv);
    char d[56];
    snprintf(d, sizeof d, "inbound %u/%u B%s", rcv, len, v > 0 ? " (mismatch)" : "");
    ok("server inbound recv", v == 0, d);
    close(c);
    close(ls);
}

/* ---- concurrent connections in both directions -------------------- */

typedef struct {
    int          fd;
    struct tt_rng rng;
    uint32_t     n, off;
    int          done, ok, inbound;
} conn_t;

static int set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* Drain whatever is readable on c->fd and verify it against the LCG
 * stream.  Returns 1 if the connection just completed, 0 if it would
 * block (more to come), -1 on mismatch / premature EOF / error.  Sets
 * c->done and c->ok on completion or failure. */
static int conn_pump(conn_t *c) {
    unsigned char buf[4096];
    for (;;) {
        uint32_t want = c->n - c->off;
        if (want > sizeof buf) want = (uint32_t)sizeof buf;
        ssize_t r = read(c->fd, buf, want);
        if (r > 0) {
            for (ssize_t i = 0; i < r; i++)
                if (buf[i] != tt_byte(&c->rng)) { c->done = 1; c->ok = 0; return -1; }
            c->off += (uint32_t)r;
            if (c->off >= c->n) { c->done = 1; c->ok = 1; return 1; }
            continue;
        }
        if (r == 0) { c->done = 1; c->ok = 0; return -1; }   /* short EOF */
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        c->done = 1; c->ok = 0; return -1;
    }
}

/* Open `k_out` outbound downloads AND have the partner dial back `k_in`
 * uploads, then drive all of them to completion at once via poll(2) —
 * exercising many simultaneous connections in both directions.  This is
 * only possible because MAX_FD is now far above the old 32 ceiling. */
static void sc_concurrent(int k_out, int k_in, uint32_t len) {
    const uint32_t seed_out = 0x8000u, seed_in = 0x9000u;
    int total = k_out + k_in;

    int ls = make_listener(TT_RPORT, k_in + 4);
    if (ls < 0) { ok("concurrent listen", 0, strerror(errno)); return; }
    set_nonblock(ls);

    if (request_reverse(TT_RPORT, (uint32_t)k_in, len, seed_in) != 0) {
        ok("concurrent reverse req", 0, "control"); close(ls); return;
    }

    conn_t *conns = calloc((size_t)total, sizeof *conns);
    struct pollfd *pfd = calloc((size_t)(total + 1), sizeof *pfd);
    int *map = calloc((size_t)(total + 1), sizeof *map);
    if (!conns || !pfd || !map) {
        ok("concurrent alloc", 0, "oom");
        free(conns); free(pfd); free(map); close(ls); return;
    }

    int nconn = 0, connect_fail = 0;
    for (int i = 0; i < k_out; i++) {
        int fd = tt_connect();
        if (fd < 0) { connect_fail++; continue; }
        if (send_req(fd, SC_DOWNLOAD, len, seed_out, 0) != 0) { close(fd); connect_fail++; continue; }
        set_nonblock(fd);
        conn_t *cn = &conns[nconn++];
        cn->fd = fd; cn->n = len; cn->off = 0; cn->done = 0; cn->inbound = 0;
        tt_seed(&cn->rng, seed_out);
    }

    int in_accepted = 0, done_cnt = 0, stalls = 0;
    while (done_cnt < nconn || in_accepted < k_in) {
        int np = 0;
        if (in_accepted < k_in) {
            pfd[np].fd = ls; pfd[np].events = POLLIN; pfd[np].revents = 0;
            map[np] = -1; np++;
        }
        for (int i = 0; i < nconn; i++) {
            if (conns[i].done) continue;
            pfd[np].fd = conns[i].fd; pfd[np].events = POLLIN; pfd[np].revents = 0;
            map[np] = i; np++;
        }
        if (np == 0) break;

        int pr = poll(pfd, (nfds_t)np, 8000);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr == 0) { if (++stalls >= 2) break; else continue; }
        stalls = 0;

        for (int j = 0; j < np; j++) {
            if (!(pfd[j].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            if (map[j] == -1) {
                while (in_accepted < k_in) {
                    int a = accept(ls, NULL, NULL);
                    if (a < 0) break;
                    set_nonblock(a);
                    conn_t *cn = &conns[nconn++];
                    cn->fd = a; cn->n = len; cn->off = 0; cn->done = 0; cn->inbound = 1;
                    tt_seed(&cn->rng, seed_in);
                    in_accepted++;
                }
            } else {
                conn_t *cn = &conns[map[j]];
                if (cn->done) continue;
                if (conn_pump(cn) != 0) done_cnt++;
            }
        }
    }

    int passed = 0, failed = 0;
    for (int i = 0; i < nconn; i++) {
        if (conns[i].done && conns[i].ok) passed++; else failed++;
        if (conns[i].fd >= 0) close(conns[i].fd);
    }
    int all_ok = (failed == 0) && (connect_fail == 0) &&
                 (in_accepted == k_in) && (passed == nconn) && (nconn == total);
    char d[96];
    snprintf(d, sizeof d, "%d/%d ok (%d out + %d in live), %d connect-fail",
             passed, nconn, k_out - connect_fail, in_accepted, connect_fail);
    ok("concurrent both-ways", all_ok, d);

    free(conns); free(pfd); free(map);
    close(ls);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <partner-host> [port]\n", argv[0]);
        return 2;
    }
    g_host = argv[1];
    g_port = (uint16_t)((argc > 2) ? atoi(argv[2]) : atoi(TT_PORT));
    printf("torture_tcp_net: partner %s:%u\n\n", g_host, g_port);

    printf("sc1: small echo round-trip\n");
    sc_echo(64, 0x1111);

    printf("sc2: connection churn (40 short echoes)\n");
    {
        int bad = 0;
        for (int i = 0; i < 40; i++) {
            int fd = tt_connect();
            if (fd < 0) { bad++; continue; }
            uint32_t seed = 0x2000u + (uint32_t)i;
            if (send_req(fd, SC_ECHO, 200, seed, 0) != 0 ||
                tt_send_stream(fd, seed, 200) != 0 ||
                tt_verify_stream(fd, seed, 200, NULL) != 0) bad++;
            close(fd);
        }
        char d[32]; snprintf(d, sizeof d, "%d/40 ok", 40 - bad);
        ok("churn 40 conns", bad == 0, d);
    }

    printf("sc3: bulk download (4 MiB)\n");
    sc_download(4u * 1024 * 1024, 0x3333);

    printf("sc4: bulk upload (4 MiB)\n");
    sc_upload(4u * 1024 * 1024, 0x4444);

    printf("sc5: slow-reader backpressure (256 KiB @ 20ms/4K)\n");
    sc_slowread(256u * 1024, 0x5555, 20);

    printf("sc6: half-close (128 KiB then SHUT_WR)\n");
    sc_halfclose(128u * 1024, 0x6666);

    printf("sc7: inbound server (partner dials in, 256 KiB upload)\n");
    sc_server_recv(256u * 1024, 0x7777);

    printf("sc8: concurrent both-ways (24 out + 24 in @ 64 KiB)\n");
    sc_concurrent(24, 24, 64u * 1024);

    printf("\nResult: %s (%d failure%s)\n",
           failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures;
}
