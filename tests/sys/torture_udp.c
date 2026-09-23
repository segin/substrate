/*
 * torture_udp.c — regression test for the UDP demux and checksum findings
 * (task #430: UDP-01, UDP-03, SOCK-07).
 *
 * Also UDP-MEM-01 (MSG_TRUNC over-copy), UDP-MEM-02 (recvmsg msg_name
 * written through a raw user pointer), UDP-U-01 (connected sendto),
 * UDP-U-04 (destination port 0), UDP-U-05 (empty datagram via sendmsg),
 * UDP-IP-02 (all of 127/8 is local), UDP-IP-08 (broadcast fan-out) and
 * UDP-IP-11 (lo's MTU), UDP-API-01 (port ownership), UDP-API-02
 * (multi-iovec sendmsg), UDP-API-03 (writev), UDP-API-04 (SO_RCVTIMEO),
 * UDP-API-06 (non-local bind) and UDP-API-07 (connect binds) from
 * docs/ip-audit-2026-09-22.md.
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
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <time.h>
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

/* Read one datagram if one arrives within 300 ms; returns bytes read or -1
 * with errno set (EAGAIN if none came).  Loopback delivers from the lo
 * kthread, asynchronously to the sender, so a bare MSG_DONTWAIT right after
 * a send can race the delivery and report a datagram as missing. */
static void wait_readable(int fd)
{
    struct pollfd pfd = { fd, POLLIN, 0 };
    poll(&pfd, 1, 300);
}

static ssize_t try_recv(int fd, char *buf, size_t n)
{
    wait_readable(fd);
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

/*
 * UDP-MEM-01: recv(..., MSG_TRUNC) returns a datagram's REAL length, which
 * can exceed the caller's buffer.  do_recv() then copied that many bytes out
 * of a kernel bounce buffer sized to the caller's length -- reading past the
 * end of the kernel allocation and writing past the end of the user buffer.
 * The 16-byte receive buffer sits at the front of a canary-filled region, so
 * any byte copied beyond 16 shows up as a clobbered canary.
 */
#define TRUNC_DGRAM 1400
#define TRUNC_BUF   16
static void check_trunc(const char *what, int rx, int tx,
                        const struct sockaddr *dst, socklen_t dstlen)
{
    static char big[TRUNC_DGRAM];
    static unsigned char region[4096];

    memset(big, 'A', sizeof(big));
    memset(region, 0xCC, sizeof(region));
    ssize_t sent = dst ? sendto(tx, big, sizeof(big), 0, dst, dstlen)
                       : send(tx, big, sizeof(big), 0);
    if (sent != (ssize_t)sizeof(big)) {
        ok(what, 0, "send failed");
        return;
    }
    wait_readable(rx);
    ssize_t got = recv(rx, region, TRUNC_BUF, MSG_TRUNC | MSG_DONTWAIT);
    int head_ok = 1, canary_ok = 1;
    for (int i = 0; i < TRUNC_BUF; i++)
        if (region[i] != 'A') head_ok = 0;
    for (size_t i = TRUNC_BUF; i < sizeof(region); i++)
        if (region[i] != 0xCC) canary_ok = 0;
    char label[96];
    snprintf(label, sizeof(label), "%s: MSG_TRUNC reports the real length", what);
    ok(label, got == TRUNC_DGRAM, "wrong return value");
    snprintf(label, sizeof(label), "%s: only the caller's %d bytes are written", what, TRUNC_BUF);
    ok(label, head_ok && canary_ok,
       canary_ok ? "payload bytes wrong" : "bytes written past the end of the user buffer");
}

static void test_msg_trunc_clamp(void)
{
    printf("UDP-MEM-01: MSG_TRUNC never copies past the caller's buffer\n");

    struct sockaddr_in dst;
    int rx = bind_udp(31990);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx < 0 || tx < 0) {
        ok("UDP sockets created", 0, "socket/bind failed");
    } else {
        lo_addr(&dst, 31990);
        check_trunc("UDP", rx, tx, (struct sockaddr *)&dst, sizeof(dst));
    }
    if (rx >= 0) close(rx);
    if (tx >= 0) close(tx);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0) {
        ok("AF_UNIX socketpair created", 0, "socketpair failed");
        return;
    }
    check_trunc("AF_UNIX", sv[1], sv[0], NULL, 0);
    close(sv[0]);
    close(sv[1]);
}

/*
 * UDP-MEM-02: a multi-iovec recvmsg() on a datagram socket wrote the source
 * sockaddr through msg_name with no validation, so msg_name could name any
 * address -- kernel memory included.  A legitimate msg_name must still get
 * the sender's address; one pointing into the kernel must fail EFAULT.
 */
static void test_recvmsg_name(void)
{
    printf("UDP-MEM-02: multi-iovec recvmsg() validates msg_name\n");

    struct sockaddr_in dst, from, src;
    socklen_t slen = sizeof(src);
    int rx = bind_udp(31991);
    int tx = bind_udp(31992);
    if (rx < 0 || tx < 0) {
        ok("sockets created", 0, "socket/bind failed");
        if (rx >= 0) close(rx);
        if (tx >= 0) close(tx);
        return;
    }
    getsockname(tx, (struct sockaddr *)&src, &slen);
    lo_addr(&dst, 31991);

    const char msg[] = "0123456789abcdef";
    char a[8], b[32];
    struct iovec iov[2] = { { a, sizeof(a) }, { b, sizeof(b) } };
    struct msghdr mh;

    /* Legitimate msg_name: data scattered, sender reported. */
    sendto(tx, msg, sizeof(msg), 0, (struct sockaddr *)&dst, sizeof(dst));
    memset(&from, 0, sizeof(from));
    memset(&mh, 0, sizeof(mh));
    mh.msg_name = &from;
    mh.msg_namelen = sizeof(from);
    mh.msg_iov = iov;
    mh.msg_iovlen = 2;
    wait_readable(rx);
    ssize_t got = recvmsg(rx, &mh, MSG_DONTWAIT);
    ok("datagram scattered across both iovecs",
       got == (ssize_t)sizeof(msg) && memcmp(a, msg, 8) == 0 &&
       memcmp(b, msg + 8, sizeof(msg) - 8) == 0, "wrong data");
    ok("msg_name holds the sender",
       mh.msg_namelen == (socklen_t)sizeof(from) &&
       from.sin_family == AF_INET && from.sin_port == src.sin_port &&
       from.sin_addr.s_addr == htonl(0x7F000001), "wrong source address");

    /* Hostile msg_name: a kernel direct-map address. */
    sendto(tx, msg, sizeof(msg), 0, (struct sockaddr *)&dst, sizeof(dst));
    memset(&mh, 0, sizeof(mh));
    mh.msg_name = (void *)0xC0000500;
    mh.msg_namelen = sizeof(from);
    mh.msg_iov = iov;
    mh.msg_iovlen = 2;
    errno = 0;
    wait_readable(rx);
    got = recvmsg(rx, &mh, MSG_DONTWAIT);
    ok("msg_name in kernel memory is refused with EFAULT",
       got < 0 && errno == EFAULT, "the kernel wrote through a kernel msg_name");

    close(rx);
    close(tx);
}

/*
 * UDP-U-01: sendto() on a CONNECTED datagram socket must honour the
 * destination it names.  afinet_sendto_k() parsed the caller's address only
 * when the socket was not connected, so after connect() every sendto() went
 * to the connected peer instead -- a resolver retargeting a second server
 * silently re-queried the first.  send() with no address still goes to the
 * peer.
 */
static void test_connected_sendto(void)
{
    printf("UDP-U-01: sendto() on a connected socket uses the named address\n");

    struct sockaddr_in a, b;
    char buf[32];
    int ra = bind_udp(31970);
    int rb = bind_udp(31971);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    if (ra < 0 || rb < 0 || tx < 0) {
        ok("sockets created", 0, "socket/bind failed");
        goto out;
    }
    lo_addr(&a, 31970);
    lo_addr(&b, 31971);
    ok("connect to A", connect(tx, (struct sockaddr *)&a, sizeof(a)) == 0,
       "connect failed");
    ssize_t n = sendto(tx, "to-b", 4, 0, (struct sockaddr *)&b, sizeof(b));
    ok("sendto(B) accepted", n == 4, "sendto failed");
    ok("B received the datagram", try_recv(rb, buf, sizeof(buf)) == 4,
       "the named destination got nothing");
    ok("A (the connected peer) received nothing", try_recv(ra, buf, sizeof(buf)) < 0,
       "the datagram went to the connected peer instead");
    n = send(tx, "to-a", 4, 0);
    ok("send() without an address still reaches A",
       n == 4 && try_recv(ra, buf, sizeof(buf)) == 4, "peer default lost");
out:
    if (ra >= 0) close(ra);
    if (rb >= 0) close(rb);
    if (tx >= 0) close(tx);
}

/* UDP-U-04: destination port 0 is RFC 768's "no port"; it must never be
 * sent to or connected to. */
static void test_port_zero(void)
{
    printf("UDP-U-04: destination port 0 is refused\n");
    struct sockaddr_in z;
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    lo_addr(&z, 0);
    errno = 0;
    ssize_t n = sendto(tx, "x", 1, 0, (struct sockaddr *)&z, sizeof(z));
    ok("sendto(port 0) fails EINVAL", n < 0 && errno == EINVAL, "datagram to port 0 was sent");
    errno = 0;
    int r = connect(tx, (struct sockaddr *)&z, sizeof(z));
    ok("connect(port 0) fails EINVAL", r < 0 && errno == EINVAL, "connected to port 0");
    close(tx);
}

/* UDP-U-05: sendmsg() with no payload -- no iovecs, or only empty ones --
 * must still send one empty datagram. */
static void test_sendmsg_empty(void)
{
    printf("UDP-U-05: sendmsg() sends an empty datagram\n");
    struct sockaddr_in dst;
    char buf[8];
    int rx = bind_udp(31972);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    lo_addr(&dst, 31972);
    struct msghdr mh;
    memset(&mh, 0, sizeof(mh));
    mh.msg_name = &dst;
    mh.msg_namelen = sizeof(dst);
    ssize_t n = sendmsg(tx, &mh, 0);
    ok("sendmsg with no iovecs returns 0", n == 0, "sendmsg failed");
    errno = 0;
    ok("an empty datagram arrived", try_recv(rx, buf, sizeof(buf)) == 0,
       "nothing was sent");
    struct iovec iov[2] = { { buf, 0 }, { buf, 0 } };
    mh.msg_iov = iov;
    mh.msg_iovlen = 2;
    n = sendmsg(tx, &mh, 0);
    ok("sendmsg with two empty iovecs returns 0", n == 0, "sendmsg failed");
    ok("a second empty datagram arrived", try_recv(rx, buf, sizeof(buf)) == 0,
       "nothing was sent");
    ok("exactly two were queued", try_recv(rx, buf, sizeof(buf)) < 0,
       "extra datagram");
    close(rx);
    close(tx);
}

/* UDP-IP-02: all of 127/8 is the loopback network (RFC 1122 3.2.1.3(g)),
 * not just 127.0.0.1.  ip4_input accepted only lo's exact address and its
 * broadcast, so 127.0.0.2 was unreachable. */
static void test_loopback_net(void)
{
    printf("UDP-IP-02: every 127/8 address is local\n");
    struct sockaddr_in any, dst, from;
    socklen_t flen = sizeof(from);
    char buf[16];
    int rx = socket(AF_INET, SOCK_DGRAM, 0);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&any, 0, sizeof(any));
    any.sin_family = AF_INET;
    any.sin_port = htons(31973);
    ok("wildcard bind", bind(rx, (struct sockaddr *)&any, sizeof(any)) == 0, "bind failed");
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(31973);
    dst.sin_addr.s_addr = htonl(0x7F000002);        /* 127.0.0.2 */
    ok("sendto(127.0.0.2) accepted",
       sendto(tx, "lo2", 3, 0, (struct sockaddr *)&dst, sizeof(dst)) == 3, "sendto failed");
    wait_readable(rx);
    ssize_t n = recvfrom(rx, buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr *)&from, &flen);
    ok("a datagram to 127.0.0.2 is delivered", n == 3, "datagram dropped");
    dst.sin_addr.s_addr = htonl(0x7F123456);        /* 127.18.52.86 */
    sendto(tx, "lo3", 3, 0, (struct sockaddr *)&dst, sizeof(dst));
    ok("a datagram to 127.18.52.86 is delivered",
       try_recv(rx, buf, sizeof(buf)) == 3, "datagram dropped");
    close(rx);
    close(tx);
}

/* UDP-IP-08: a broadcast datagram goes to EVERY socket that can take it,
 * not just the best match -- RFC 1122 3.3.6.  The demux picked a single
 * winner for everything, so of two listeners on a broadcast port only one
 * ever heard anything. */
static void test_broadcast_fanout(void)
{
    printf("UDP-IP-08: a broadcast reaches every listener on the port\n");
    struct sockaddr_in any, dst;
    char buf[16];
    int one = 1;
    int r1 = socket(AF_INET, SOCK_DGRAM, 0);
    int r2 = socket(AF_INET, SOCK_DGRAM, 0);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(r1, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(r2, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&any, 0, sizeof(any));
    any.sin_family = AF_INET;
    any.sin_port = htons(31974);
    int b1 = bind(r1, (struct sockaddr *)&any, sizeof(any));
    int b2 = bind(r2, (struct sockaddr *)&any, sizeof(any));
    ok("two wildcard listeners on one port", b1 == 0 && b2 == 0, "bind failed");
    setsockopt(tx, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(31974);
    dst.sin_addr.s_addr = htonl(0x7FFFFFFF);        /* 127.255.255.255 */
    ok("broadcast sent", sendto(tx, "all", 3, 0, (struct sockaddr *)&dst, sizeof(dst)) == 3,
       "sendto failed");
    ok("the first listener got it", try_recv(r1, buf, sizeof(buf)) == 3, "missed");
    ok("the second listener got it", try_recv(r2, buf, sizeof(buf)) == 3, "missed");
    /* A unicast datagram still goes to exactly one (UDP-01). */
    dst.sin_addr.s_addr = htonl(0x7F000001);
    sendto(tx, "one", 3, 0, (struct sockaddr *)&dst, sizeof(dst));
    int got = (try_recv(r1, buf, sizeof(buf)) == 3) + (try_recv(r2, buf, sizeof(buf)) == 3);
    ok("a unicast still reaches exactly one", got == 1, "unicast fanned out");
    close(r1);
    close(r2);
    close(tx);
}

/* UDP-IP-11: lo reports the MTU its ring can actually carry (1686 = the
 * 1700-byte frame limit minus the Ethernet header), not 16384. */
static void test_lo_mtu(void)
{
    printf("UDP-IP-11: lo's MTU is what it can carry\n");
    struct ifreq ifr;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "lo", sizeof(ifr.ifr_name) - 1);
    int r = ioctl(fd, SIOCGIFMTU, &ifr);
    ok("SIOCGIFMTU(lo) succeeds", r == 0, "ioctl failed");
    ok("lo's MTU fits its 1700-byte frames", r == 0 && ifr.ifr_mtu > 0 && ifr.ifr_mtu <= 1686,
       "MTU larger than the device can carry");
    close(fd);
}

/*
 * UDP-API-01 (and TCP-API-18): bind() must not let one user take over
 * another's port.  Only the NEW socket's SO_REUSEADDR was consulted, never
 * the incumbent's or the owner's; there was no reserved-port check; and a
 * tie in the demux went to the newest socket -- so an unprivileged bind to
 * a root daemon's port captured its traffic.
 */
static void test_port_ownership(void)
{
    printf("UDP-API-01: a bound port cannot be taken over\n");
    struct sockaddr_in a;
    int one = 1;
    int root = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(31960);
    ok("root binds *:31960 (no SO_REUSEADDR)",
       bind(root, (struct sockaddr *)&a, sizeof(a)) == 0, "bind failed");

    pid_t kid = fork();
    if (kid == 0) {
        setuid(1000);
        int res = 0;
        int s1 = socket(AF_INET, SOCK_DGRAM, 0);
        setsockopt(s1, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in b = a;
        errno = 0;
        if (bind(s1, (struct sockaddr *)&b, sizeof(b)) == 0) res |= 1;
        else if (errno != EADDRINUSE) res |= 4;
        int s2 = socket(AF_INET, SOCK_DGRAM, 0);
        b.sin_port = htons(530);
        errno = 0;
        if (bind(s2, (struct sockaddr *)&b, sizeof(b)) == 0) res |= 2;
        else if (errno != EACCES) res |= 8;
        _exit(res);
    }
    int st = 0;
    waitpid(kid, &st, 0);
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : 255;
    ok("another user cannot bind the port, even with SO_REUSEADDR",
       (code & 1) == 0, "an unprivileged socket took over root's port");
    ok("...and is told EADDRINUSE", (code & 5) == 0, "wrong errno");
    ok("a non-root bind below 1024 fails EACCES", (code & 10) == 0,
       (code & 2) ? "an unprivileged process bound port 530" : "wrong errno");
    close(root);

    /* Same owner, both SO_REUSEADDR: may share; the OLDER keeps unicast. */
    int o1 = socket(AF_INET, SOCK_DGRAM, 0);
    int o2 = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(o1, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(o2, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    a.sin_port = htons(31962);
    int b1 = bind(o1, (struct sockaddr *)&a, sizeof(a));
    int b2 = bind(o2, (struct sockaddr *)&a, sizeof(a));
    ok("same owner, both SO_REUSEADDR: may share", b1 == 0 && b2 == 0, "bind failed");
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    lo_addr(&a, 31962);
    sendto(tx, "u", 1, 0, (struct sockaddr *)&a, sizeof(a));
    char buf[8];
    ok("a unicast goes to the first-bound socket",
       try_recv(o1, buf, sizeof(buf)) == 1 && try_recv(o2, buf, sizeof(buf)) < 0,
       "the later bind captured it");
    close(o1);
    close(o2);

    /* Different specific addresses do not conflict at all. */
    int l1 = socket(AF_INET, SOCK_DGRAM, 0);
    int l2 = socket(AF_INET, SOCK_DGRAM, 0);
    lo_addr(&a, 31963);
    int c1 = bind(l1, (struct sockaddr *)&a, sizeof(a));
    a.sin_addr.s_addr = htonl(0x7F000002);
    int c2 = bind(l2, (struct sockaddr *)&a, sizeof(a));
    ok("127.0.0.1:P and 127.0.0.2:P coexist", c1 == 0 && c2 == 0, "overlap refused");
    close(l1);
    close(l2);
    close(tx);
}

/* UDP-API-02: sendmsg() with more than one iovec on a UDP socket sends ONE
 * datagram with the iovecs concatenated.  The gather buffer is kernel
 * memory, and the AF_INET route ran it through copyin(), so every such call
 * failed EFAULT. */
static void test_sendmsg_gather(void)
{
    printf("UDP-API-02: multi-iovec sendmsg() on UDP\n");
    struct sockaddr_in dst;
    char buf[32];
    int rx = bind_udp(31964);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    lo_addr(&dst, 31964);
    struct iovec iov[2] = { { (void *)"head-", 5 }, { (void *)"body", 4 } };
    struct msghdr mh;
    memset(&mh, 0, sizeof(mh));
    mh.msg_name = &dst;
    mh.msg_namelen = sizeof(dst);
    mh.msg_iov = iov;
    mh.msg_iovlen = 2;
    errno = 0;
    ssize_t n = sendmsg(tx, &mh, 0);
    ok("two-iovec sendmsg returns the total", n == 9, "sendmsg failed");
    memset(buf, 0, sizeof(buf));
    ssize_t r = try_recv(rx, buf, sizeof(buf));
    ok("one datagram with both iovecs arrived",
       r == 9 && memcmp(buf, "head-body", 9) == 0, "wrong or no datagram");
    ok("and only one", try_recv(rx, buf, sizeof(buf)) < 0, "split into several");
    close(rx);
    close(tx);
}

/* UDP-API-03: writev() on a connected datagram socket sends ONE datagram
 * with the iovecs concatenated, as write() of the concatenation would.  It
 * issued one write per iovec -- one datagram each. */
static void test_writev_dgram(void)
{
    printf("UDP-API-03: writev() on UDP is one datagram\n");
    struct sockaddr_in dst;
    char buf[32];
    int rx = bind_udp(31965);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    lo_addr(&dst, 31965);
    connect(tx, (struct sockaddr *)&dst, sizeof(dst));
    struct iovec iov[3] = { { (void *)"a", 1 }, { (void *)"bc", 2 }, { (void *)"def", 3 } };
    ssize_t n = writev(tx, iov, 3);
    ok("writev returns the total", n == 6, "writev failed");
    memset(buf, 0, sizeof(buf));
    ssize_t r = try_recv(rx, buf, sizeof(buf));
    ok("one datagram, the concatenation", r == 6 && memcmp(buf, "abcdef", 6) == 0,
       "framing split");
    ok("and only one", try_recv(rx, buf, sizeof(buf)) < 0, "several datagrams");
    close(rx);
    close(tx);
}

static long elapsed_ms(const struct timespec *a)
{
    struct timespec b;
    clock_gettime(CLOCK_MONOTONIC, &b);
    return (long)(b.tv_sec - a->tv_sec) * 1000 + (b.tv_nsec - a->tv_nsec) / 1000000;
}

/* UDP-API-04: SO_RCVTIMEO bounds a blocking receive.  It was accepted and
 * discarded, so recv() on a silent socket slept forever. */
static void test_rcvtimeo(void)
{
    printf("UDP-API-04: SO_RCVTIMEO bounds a blocking receive\n");
    struct timeval tv = { 0, 250000 }, got;
    socklen_t gl = sizeof(got);
    char buf[8];
    struct timespec t0;

    int u = bind_udp(31966);
    ok("setsockopt(SO_RCVTIMEO) on UDP",
       setsockopt(u, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0, "refused");
    memset(&got, 0, sizeof(got));
    getsockopt(u, SOL_SOCKET, SO_RCVTIMEO, &got, &gl);
    ok("getsockopt reads it back",
       got.tv_sec == 0 && got.tv_usec >= 240000 && got.tv_usec <= 260000, "wrong value");
    clock_gettime(CLOCK_MONOTONIC, &t0);
    errno = 0;
    ssize_t n = recv(u, buf, sizeof(buf), 0);
    long ms = elapsed_ms(&t0);
    ok("UDP recv() times out with EAGAIN", n < 0 && errno == EAGAIN, "did not time out");
    ok("after about 250 ms", ms >= 200 && ms < 2000, "wrong duration");
    close(u);

    int one = 1;
    struct sockaddr_in a;
    int l = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    lo_addr(&a, 31967);
    bind(l, (struct sockaddr *)&a, sizeof(a));
    listen(l, 1);
    int c = socket(AF_INET, SOCK_STREAM, 0);
    connect(c, (struct sockaddr *)&a, sizeof(a));
    int sv = accept(l, NULL, NULL);
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    clock_gettime(CLOCK_MONOTONIC, &t0);
    errno = 0;
    n = recv(c, buf, sizeof(buf), 0);
    ms = elapsed_ms(&t0);
    ok("TCP recv() times out with EAGAIN", n < 0 && errno == EAGAIN, "did not time out");
    ok("after about 250 ms", ms >= 200 && ms < 2000, "wrong duration");
    close(sv);
    close(c);
    close(l);
}

/* UDP-API-06: bind() to an address this host does not own must fail
 * EADDRNOTAVAIL.  Any address was accepted, leaving a socket that could
 * never receive anything, with no error. */
static void test_bind_nonlocal(void)
{
    printf("UDP-API-06: bind() only to addresses we can receive on\n");
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(31968);
    int s1 = socket(AF_INET, SOCK_DGRAM, 0);
    a.sin_addr.s_addr = htonl(0x0A090909);          /* 10.9.9.9: not ours */
    errno = 0;
    ok("a foreign address fails EADDRNOTAVAIL",
       bind(s1, (struct sockaddr *)&a, sizeof(a)) < 0 && errno == EADDRNOTAVAIL,
       "bound to an address we do not own");
    close(s1);
    int s2 = socket(AF_INET, SOCK_DGRAM, 0);
    a.sin_addr.s_addr = htonl(0x7F000005);          /* 127.0.0.5 */
    ok("any 127/8 address is fine", bind(s2, (struct sockaddr *)&a, sizeof(a)) == 0,
       "loopback address refused");
    close(s2);
    int s3 = socket(AF_INET, SOCK_DGRAM, 0);
    a.sin_addr.s_addr = htonl(0xEF010101);          /* 239.1.1.1 */
    ok("a multicast group is fine", bind(s3, (struct sockaddr *)&a, sizeof(a)) == 0,
       "group address refused");
    close(s3);
}

/* UDP-API-07: connect() on an unbound UDP socket binds it -- an ephemeral
 * port and the source address toward the peer -- so it can receive the
 * peer's first datagram before sending anything, and getsockname() tells
 * the truth.  It stayed at port 0 until the first send. */
static void test_connect_binds(void)
{
    printf("UDP-API-07: connect() assigns the local endpoint\n");
    struct sockaddr_in peer, me;
    socklen_t ml = sizeof(me);
    char buf[8];
    int p = bind_udp(31969);
    int c = socket(AF_INET, SOCK_DGRAM, 0);
    lo_addr(&peer, 31969);
    ok("connect", connect(c, (struct sockaddr *)&peer, sizeof(peer)) == 0, "failed");
    memset(&me, 0, sizeof(me));
    getsockname(c, (struct sockaddr *)&me, &ml);
    ok("getsockname reports a port", me.sin_port != 0, "still port 0");
    ok("and the source address toward the peer",
       me.sin_addr.s_addr == htonl(0x7F000001), "wrong address");
    /* The peer answers first. */
    sendto(p, "hi", 2, 0, (struct sockaddr *)&me, sizeof(me));
    ok("the peer's datagram is received before we ever send",
       try_recv(c, buf, sizeof(buf)) == 2, "nothing arrived");
    close(p);
    close(c);
}

int main(void)
{
    printf("torture_udp: UDP demux + checksum regressions (#430)\n\n");

    test_roundtrip();
    test_no_duplicate_delivery();
    test_connected_peer_filter();
    test_so_error_unix();
    test_raw_socket_privileged();
    test_msg_trunc_clamp();
    test_recvmsg_name();
    test_connected_sendto();
    test_port_zero();
    test_sendmsg_empty();
    test_loopback_net();
    test_broadcast_fanout();
    test_lo_mtu();
    test_port_ownership();
    test_sendmsg_gather();
    test_writev_dgram();
    test_rcvtimeo();
    test_bind_nonlocal();
    test_connect_binds();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
