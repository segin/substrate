/*
 * torture_udp.c — regression test for the UDP demux and checksum findings
 * (task #430: UDP-01, UDP-03, SOCK-07).
 *
 * Each case drives the real socket API over the loopback interface, so a
 * PASS means a datagram actually took the intended path through the
 * kernel's demux, not that some internal predicate returned the right
 * value.
 *
 * Run as init:  qemu ... -append "init=/tmp/torture_udp"
 */
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

static int passed, failed;

static void ok(const char *what, int cond, const char *why)
{
    if (cond) {
        printf("  ok    %s\n", what);
        passed++;
    } else {
        printf("  FAIL  %s: %s (errno=%d)\n", what, why, errno);
        failed++;
    }
}

static void lo_addr(struct sockaddr_in *sin, unsigned short port)
{
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    sin->sin_addr.s_addr = htonl(0x7F000001);   /* 127.0.0.1 */
}

/* Bind a UDP socket to 127.0.0.1:port; -1 on failure. */
static int bind_udp(unsigned short port)
{
    struct sockaddr_in sin;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    lo_addr(&sin, port);
    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Non-blocking read; returns bytes read or -1 with errno set. */
static ssize_t try_recv(int fd, char *buf, size_t n)
{
    return recv(fd, buf, n, MSG_DONTWAIT);
}

/*
 * UDP-03: a datagram must survive a loopback round trip now that the send
 * path computes a checksum and the receive path verifies it.  If the two
 * disagree in any way -- wrong pseudo-header source, wrong length, wrong
 * byte order -- every datagram is silently dropped and nothing works at
 * all, so this is the load-bearing case for the whole change.
 */
static void test_roundtrip(void)
{
    printf("UDP-03: checksummed loopback round trip\n");

    const char msg[] = "the quick brown fox";
    char buf[64];
    struct sockaddr_in dst;
    int rx = bind_udp(31995);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);

    ok("sockets created", rx >= 0 && tx >= 0, "socket/bind failed");
    if (rx < 0 || tx < 0) return;

    lo_addr(&dst, 31995);
    ssize_t sent = sendto(tx, msg, sizeof(msg), 0,
                          (struct sockaddr *)&dst, sizeof(dst));
    ok("sendto accepted the datagram", sent == (ssize_t)sizeof(msg),
       "send failed");

    ssize_t got = try_recv(rx, buf, sizeof(buf));
    ok("datagram survived the checksum check",
       got == (ssize_t)sizeof(msg) && memcmp(buf, msg, sizeof(msg)) == 0,
       "datagram was dropped or corrupted");

    close(rx);
    close(tx);
}

/*
 * UDP-01a: two sockets on one port must not each receive a COPY.  The demux
 * matched on local_port alone and enqueued into every match, so two
 * resolvers on one port read each other's answers.
 */
static void test_no_duplicate_delivery(void)
{
    printf("UDP-01: one datagram reaches exactly one socket\n");

    const char msg[] = "only-once";
    char buf[64];
    struct sockaddr_in dst;

    /* Two sockets on the same port requires SO_REUSEADDR; if the kernel
     * refuses the second bind the defect is unreachable by this route,
     * which is itself a pass. */
    int a = bind_udp(31997);
    int b = -1;
    if (a >= 0) {
        int on = 1;
        struct sockaddr_in sin;
        b = socket(AF_INET, SOCK_DGRAM, 0);
        if (b >= 0) {
            setsockopt(b, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
            lo_addr(&sin, 31997);
            if (bind(b, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
                close(b);
                b = -1;
            }
        }
    }
    if (a < 0) { ok("bind", 0, "could not bind the first socket"); return; }
    if (b < 0) {
        printf("  skip  second bind refused (no port sharing) -- not reachable\n");
        close(a);
        return;
    }

    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    lo_addr(&dst, 31997);
    sendto(tx, msg, sizeof(msg), 0, (struct sockaddr *)&dst, sizeof(dst));

    ssize_t ga = try_recv(a, buf, sizeof(buf));
    ssize_t gb = try_recv(b, buf, sizeof(buf));
    ok("exactly one of the two sockets got it",
       (ga > 0) != (gb > 0), "both sockets received a copy");

    close(a);
    close(b);
    close(tx);
}

/*
 * UDP-01b: a connect()ed datagram socket must receive only from its peer.
 * Without the peer check a spoofed reply from any source was accepted,
 * which is how an off-path attacker beats a real DNS server.
 */
static void test_connected_peer_filter(void)
{
    printf("UDP-01: connected socket rejects a non-peer source\n");

    char buf[64];
    struct sockaddr_in peer, dst;

    int rx = bind_udp(31999);          /* the connected socket   */
    int good = bind_udp(32001);        /* its designated peer    */
    int evil = bind_udp(32003);        /* an unrelated source    */
    if (rx < 0 || good < 0 || evil < 0) {
        ok("bind", 0, "setup binds failed");
        return;
    }

    lo_addr(&peer, 32001);
    ok("connect to the peer succeeded",
       connect(rx, (struct sockaddr *)&peer, sizeof(peer)) == 0,
       "connect failed");

    lo_addr(&dst, 31999);

    /* The impostor sends first, so if its datagram were accepted it would
     * be the one sitting at the head of the queue. */
    sendto(evil, "spoofed", 8, 0, (struct sockaddr *)&dst, sizeof(dst));
    sendto(good, "genuine", 8, 0, (struct sockaddr *)&dst, sizeof(dst));

    ssize_t got = try_recv(rx, buf, sizeof(buf));
    ok("the impostor's datagram was not delivered",
       got == 8 && memcmp(buf, "genuine", 7) == 0,
       "a datagram from a non-peer source reached a connected socket");

    /* Nothing else should be queued behind it. */
    ok("no second datagram queued", try_recv(rx, buf, sizeof(buf)) <= 0,
       "the spoofed datagram was queued too");

    close(rx);
    close(good);
    close(evil);
}

/*
 * SOCK-07: getsockopt(SO_ERROR) on an AF_UNIX socket returned -ENOTSOCK as
 * the option VALUE while reporting success, so every `if (so_error) fail()`
 * saw a phantom error.
 */
static void test_so_error_unix(void)
{
    printf("SOCK-07: SO_ERROR on AF_UNIX reports no error\n");

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ok("socketpair", 0, "socketpair failed");
        return;
    }

    int err = 0x5A5A5A;
    socklen_t len = sizeof(err);
    int rc = getsockopt(sv[0], SOL_SOCKET, SO_ERROR, &err, &len);
    ok("getsockopt(SO_ERROR) succeeded", rc == 0, "getsockopt failed");
    ok("the reported error is 0, not -ENOTSOCK", err == 0,
       "a phantom error was reported as the option value");

    close(sv[0]);
    close(sv[1]);
}

/*
 * UDP-07: a raw socket reads every packet of its protocol regardless of who
 * it was for, and writes caller-composed payloads onto the wire.  Creating
 * one required no privilege at all.
 */
static void test_raw_socket_privileged(void)
{
    printf("UDP-07: raw and packet sockets are root-only\n");

    /* init runs as root, so both must still be creatable here -- a gate that
     * refuses root would break ping(8) and dhclient outright. */
    int r = socket(AF_INET, SOCK_RAW, 1 /*ICMP*/);
    ok("root can still open SOCK_RAW", r >= 0, "root was refused");
    if (r >= 0) close(r);

    int p = socket(17 /*AF_PACKET*/, SOCK_RAW, 0);
    ok("root can still open AF_PACKET", p >= 0, "root was refused");
    if (p >= 0) close(p);

    /* Drop to an unprivileged uid in a child and try again. */
    pid_t kid = fork();
    if (kid == 0) {
        setuid(1000);
        int cr = socket(AF_INET, SOCK_RAW, 1);
        int cp = socket(17, SOCK_RAW, 0);
        if (cr >= 0) close(cr);
        if (cp >= 0) close(cp);
        _exit((cr < 0 ? 1 : 0) | (cp < 0 ? 2 : 0));
    }
    int st = 0;
    waitpid(kid, &st, 0);
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    ok("non-root is refused SOCK_RAW", (code & 1) != 0,
       "an unprivileged process opened a raw IP socket");
    ok("non-root is refused AF_PACKET", (code & 2) != 0,
       "an unprivileged process opened a packet socket");
}

int main(void)
{
    printf("torture_udp: UDP demux + checksum regressions (#430)\n\n");

    test_roundtrip();
    test_no_duplicate_delivery();
    test_connected_peer_filter();
    test_so_error_unix();
    test_raw_socket_privileged();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
