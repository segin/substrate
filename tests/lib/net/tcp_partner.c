/*
 * tcp_partner.c — remote peer for the substrate TCP torture test.
 *
 * Runs on a SECOND machine (e.g. a Linux host on the same LAN as the
 * substrate guest).  Listens on a TCP port, and for each connection
 * reads a tt_req header and executes the requested scenario against the
 * substrate client.  Stateless per connection; loops forever.
 *
 *   build:  cc -O2 -o tcp_partner tcp_partner.c
 *   run:    ./tcp_partner [port]        (default 5430)
 *
 * Portable POSIX — no substrate dependencies.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "tcp_torture.h"

static void msleep(unsigned ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void handle(int c) {
    struct tt_req req;
    if (tt_readn(c, &req, sizeof req) != (ssize_t)sizeof req) return;
    if (ntohl(req.magic) != TT_MAGIC) {
        fprintf(stderr, "partner: bad magic 0x%08x\n", ntohl(req.magic));
        return;
    }
    uint32_t scen = ntohl(req.scenario);
    uint32_t len  = ntohl(req.length);
    uint32_t seed = ntohl(req.seed);
    uint32_t flg  = ntohl(req.flags);

    switch (scen) {
    case SC_ECHO: {
        /* Echo exactly `len` bytes back as they arrive. */
        unsigned char buf[8192];
        uint32_t left = len;
        while (left) {
            uint32_t w = left < sizeof buf ? left : (uint32_t)sizeof buf;
            ssize_t got = tt_readn(c, buf, w);
            if (got <= 0) break;
            if (tt_writen(c, buf, (size_t)got) != got) break;
            left -= (uint32_t)got;
        }
        break;
    }
    case SC_DOWNLOAD:
        /* Partner is the sender; client verifies. */
        tt_send_stream(c, seed, len);
        break;
    case SC_UPLOAD:
    case SC_SLOWREAD: {
        /* Partner verifies the client's stream, optionally draining
         * slowly to exercise the client's send-side flow control. */
        struct tt_rng r; tt_seed(&r, seed);
        unsigned char buf[4096];
        uint32_t off = 0; long mism = 0;
        unsigned delay = (scen == SC_SLOWREAD) ? flg : 0;
        while (off < len) {
            uint32_t want = len - off < sizeof buf ? len - off : (uint32_t)sizeof buf;
            ssize_t got = tt_readn(c, buf, want);
            if (got < 0) { off = off; break; }
            if (got == 0) break;
            for (ssize_t i = 0; i < got; i++)
                if (buf[i] != tt_byte(&r)) { mism = (long)(off + (uint32_t)i) + 1; break; }
            off += (uint32_t)got;
            if (mism) break;
            if (delay) msleep(delay);
        }
        struct tt_reply rep;
        rep.status   = htonl(mism ? (uint32_t)mism : (off < len ? 0xFFFFFFFFu : 0));
        rep.received = htonl(off);
        tt_writen(c, &rep, sizeof rep);
        break;
    }
    case SC_HALFCLOSE: {
        /* Client sends then SHUT_WR; drain to EOF, verify, reply. */
        struct tt_rng r; tt_seed(&r, seed);
        unsigned char buf[4096];
        uint32_t off = 0; long mism = 0;
        for (;;) {
            ssize_t got = read(c, buf, sizeof buf);
            if (got < 0) { if (errno == EINTR) continue; break; }
            if (got == 0) break;                  /* client's SHUT_WR */
            for (ssize_t i = 0; i < got; i++)
                if (!mism && buf[i] != tt_byte(&r))
                    mism = (long)(off + (uint32_t)i) + 1;
            off += (uint32_t)got;
        }
        struct tt_reply rep;
        rep.status   = htonl(mism ? (uint32_t)mism : (off != len ? 0xFFFFFFFFu : 0));
        rep.received = htonl(off);
        tt_writen(c, &rep, sizeof rep);
        break;
    }
    case SC_REVERSE: {
        /* Substrate is the SERVER for this one: dial `count` fresh
         * connections back to it and stream `length` bytes on each, so
         * substrate's listen/accept + inbound RX path is what's under
         * test.  We learn substrate's address from the control
         * connection's peer. */
        struct tt_reverse rv;
        if (tt_readn(c, &rv, sizeof rv) != (ssize_t)sizeof rv) return;
        uint16_t lport = (uint16_t)ntohl(rv.lport);
        uint32_t count = ntohl(rv.count);
        uint32_t rlen  = ntohl(rv.length);
        uint32_t rseed = ntohl(rv.seed);

        struct sockaddr_in peer;
        socklen_t pl = sizeof peer;
        if (getpeername(c, (struct sockaddr *)&peer, &pl) < 0) {
            perror("partner: getpeername");
            return;
        }
        peer.sin_port = htons(lport);

        for (uint32_t i = 0; i < count; i++) {
            pid_t k = fork();
            if (k == 0) {              /* dial-back child */
                int d = socket(AF_INET, SOCK_STREAM, 0);
                if (d >= 0 &&
                    connect(d, (struct sockaddr *)&peer, sizeof peer) == 0) {
                    int one = 1;
                    setsockopt(d, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                    tt_send_stream(d, rseed, rlen);
                }
                if (d >= 0) close(d);
                _exit(0);
            }
        }
        /* Acknowledge that the fan-out has been launched. */
        struct tt_reply rep;
        rep.status   = htonl(0);
        rep.received = htonl(count);
        tt_writen(c, &rep, sizeof rep);
        break;
    }
    default:
        fprintf(stderr, "partner: unknown scenario %u\n", scen);
        break;
    }
}

int main(int argc, char **argv) {
    const char *port = (argc > 1) ? argv[1] : TT_PORT;
    signal(SIGPIPE, SIG_IGN);
    /* Reap children automatically so fork-per-connection leaves no
     * zombies. */
    signal(SIGCHLD, SIG_IGN);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((uint16_t)atoi(port));
    if (bind(s, (struct sockaddr *)&sa, sizeof sa) < 0) { perror("bind"); return 1; }
    if (listen(s, 16) < 0) { perror("listen"); return 1; }
    fprintf(stderr, "tcp_partner: listening on 0.0.0.0:%s\n", port);

    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; perror("accept"); break; }
        int one = 1;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        /* Fork per connection: a connection whose peer vanished mid-
         * transfer (e.g. the substrate VM was killed by the test
         * timeout) would otherwise block the single accept loop in a
         * read() forever, wedging the partner for all later runs. */
        pid_t pid = fork();
        if (pid == 0) {            /* child: handle one connection */
            close(s);
            handle(c);
            close(c);
            _exit(0);
        }
        close(c);                  /* parent: keep accepting */
    }
    close(s);
    return 0;
}
