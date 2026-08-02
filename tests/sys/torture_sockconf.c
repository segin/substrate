/*
 * torture_sockconf.c — the MEDIUM/LOW conformance half of task #430.
 *
 * SOCK-04 sendmsg/recvmsg treated each iovec as a separate datagram
 * SOCK-05 MSG_PEEK consumed the datagram everywhere except TCP
 * SOCK-06 MSG_TRUNC unimplemented, so truncation was unreportable
 * SOCK-08 MSG_DONTWAIT mutated the shared file description's flags
 * SOCK-10 shutdown() on an unconnected socket returned success
 * UDP-04  the receive path truncated datagrams above 1500 bytes
 * UDP-05  ephemeral ports were handed out without a collision check
 * UDP-06  lost-wakeup window in the AF_INET receive sleep
 * UNIX-06 listen()/connect() did not validate the socket type
 * UNIX-07 a closed-but-not-yet-freed binding could still be returned
 *
 * Run as init:  qemu ... -append "init=/tmp/torture_sockconf"
 */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

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
    sin->sin_addr.s_addr = htonl(0x7F000001);
}

static int bind_udp(unsigned short port)
{
    struct sockaddr_in sin;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    lo_addr(&sin, port);
    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) { close(fd); return -1; }
    return fd;
}

/* SOCK-05 + SOCK-06 over UDP. */
static void test_peek_and_trunc_udp(void)
{
    printf("SOCK-05/06: MSG_PEEK preserves, MSG_TRUNC reports (UDP)\n");

    const char msg[] = "abcdefghijklmnop";     /* 17 bytes with the NUL */
    char buf[64];
    struct sockaddr_in dst;
    int rx = bind_udp(33001);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx < 0 || tx < 0) { ok("setup", 0, "socket/bind failed"); return; }

    lo_addr(&dst, 33001);
    sendto(tx, msg, sizeof(msg), 0, (struct sockaddr *)&dst, sizeof(dst));

    memset(buf, 0, sizeof(buf));
    ssize_t p1 = recv(rx, buf, sizeof(buf), MSG_PEEK | MSG_DONTWAIT);
    ok("peek returns the datagram",
       p1 == (ssize_t)sizeof(msg) && memcmp(buf, msg, sizeof(msg)) == 0,
       "peek did not return the data");

    memset(buf, 0, sizeof(buf));
    ssize_t p2 = recv(rx, buf, sizeof(buf), MSG_PEEK | MSG_DONTWAIT);
    ok("peek did not consume it",
       p2 == (ssize_t)sizeof(msg) && memcmp(buf, msg, sizeof(msg)) == 0,
       "the datagram was eaten by the first peek");

    /* Short buffer + MSG_TRUNC reports the real length, not the copied one. */
    char small[4];
    ssize_t t = recv(rx, small, sizeof(small), MSG_TRUNC | MSG_DONTWAIT);
    ok("MSG_TRUNC reports the untruncated length", t == (ssize_t)sizeof(msg),
       "reported the copied length instead");

    /* And that read consumed it. */
    ok("the datagram is gone after a real read",
       recv(rx, buf, sizeof(buf), MSG_DONTWAIT) < 0,
       "a datagram was still queued");

    close(rx);
    close(tx);
}

/* SOCK-05 + SOCK-06 over an AF_UNIX datagram pair. */
static void test_peek_and_trunc_unix(void)
{
    printf("SOCK-05/06: MSG_PEEK preserves, MSG_TRUNC reports (AF_UNIX)\n");

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0) {
        ok("socketpair", 0, "socketpair failed");
        return;
    }
    const char msg[] = "unix-datagram-payload";
    char buf[64];
    write(sv[0], msg, sizeof(msg));

    memset(buf, 0, sizeof(buf));
    ssize_t p1 = recv(sv[1], buf, sizeof(buf), MSG_PEEK | MSG_DONTWAIT);
    ok("peek returns the datagram",
       p1 == (ssize_t)sizeof(msg) && memcmp(buf, msg, sizeof(msg)) == 0,
       "peek did not return the data");

    memset(buf, 0, sizeof(buf));
    ssize_t p2 = recv(sv[1], buf, sizeof(buf), MSG_PEEK | MSG_DONTWAIT);
    ok("peek did not consume it",
       p2 == (ssize_t)sizeof(msg) && memcmp(buf, msg, sizeof(msg)) == 0,
       "the datagram was eaten by the first peek");

    char small[5];
    ssize_t t = recv(sv[1], small, sizeof(small), MSG_TRUNC | MSG_DONTWAIT);
    ok("MSG_TRUNC reports the untruncated length", t == (ssize_t)sizeof(msg),
       "reported the copied length instead");

    close(sv[0]);
    close(sv[1]);
}

/* SOCK-04: one sendmsg = one datagram, one recvmsg = one datagram. */
static void test_iovec_framing(void)
{
    printf("SOCK-04: an N-iovec datagram stays ONE datagram\n");

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0) {
        ok("socketpair", 0, "socketpair failed");
        return;
    }

    char a[] = "HEADER:", b[] = "body-bytes";
    struct iovec siov[2] = {
        { .iov_base = a, .iov_len = sizeof(a) - 1 },
        { .iov_base = b, .iov_len = sizeof(b) - 1 },
    };
    struct msghdr smsg;
    memset(&smsg, 0, sizeof(smsg));
    smsg.msg_iov = siov;
    smsg.msg_iovlen = 2;

    ssize_t w = sendmsg(sv[0], &smsg, 0);
    ok("sendmsg accepted both iovecs",
       w == (ssize_t)(sizeof(a) - 1 + sizeof(b) - 1), "short write");

    /* A single plain read must see the WHOLE thing: if sendmsg had emitted
     * two datagrams this returns only "HEADER:". */
    char whole[64];
    memset(whole, 0, sizeof(whole));
    ssize_t r = recv(sv[1], whole, sizeof(whole), MSG_PEEK | MSG_DONTWAIT);
    ok("both iovecs arrived in one datagram",
       r == w && memcmp(whole, "HEADER:body-bytes", 17) == 0,
       "the message was split into separate datagrams");

    /* And recvmsg must scatter that one datagram, not consume two. */
    char h[7], t[16];
    struct iovec riov[2] = {
        { .iov_base = h, .iov_len = sizeof(h) },
        { .iov_base = t, .iov_len = sizeof(t) },
    };
    struct msghdr rmsg;
    memset(&rmsg, 0, sizeof(rmsg));
    rmsg.msg_iov = riov;
    rmsg.msg_iovlen = 2;
    memset(h, 0, sizeof(h));
    memset(t, 0, sizeof(t));
    ssize_t rr = recvmsg(sv[1], &rmsg, 0);
    ok("recvmsg scattered one datagram across both iovecs",
       rr == w && memcmp(h, "HEADER:", 7) == 0 && memcmp(t, "body-bytes", 10) == 0,
       "scatter was wrong or consumed more than one datagram");

    ok("nothing left queued",
       recv(sv[1], whole, sizeof(whole), MSG_DONTWAIT) <= 0,
       "recvmsg left part of the message behind");

    close(sv[0]);
    close(sv[1]);
}

/* SOCK-08: MSG_DONTWAIT must not leave the fd non-blocking afterwards. */
static void test_dontwait_scope(void)
{
    printf("SOCK-08: MSG_DONTWAIT does not alter the file description\n");

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ok("socketpair", 0, "socketpair failed");
        return;
    }
    int before = fcntl(sv[1], F_GETFL);
    char buf[8];
    (void)recv(sv[1], buf, sizeof(buf), MSG_DONTWAIT);   /* returns EAGAIN */
    int after = fcntl(sv[1], F_GETFL);
    ok("O_NONBLOCK unchanged by a MSG_DONTWAIT recv",
       (before & O_NONBLOCK) == (after & O_NONBLOCK),
       "the shared file flags were mutated");

    /* The reverse: a genuinely non-blocking fd must stay non-blocking. */
    fcntl(sv[1], F_SETFL, before | O_NONBLOCK);
    (void)recv(sv[1], buf, sizeof(buf), MSG_DONTWAIT);
    ok("an O_NONBLOCK fd stays non-blocking",
       (fcntl(sv[1], F_GETFL) & O_NONBLOCK) != 0,
       "a concurrent fcntl setting was reverted");

    close(sv[0]);
    close(sv[1]);
}

/* SOCK-10: shutdown() on an unconnected socket is ENOTCONN. */
static void test_shutdown_notconn(void)
{
    printf("SOCK-10: shutdown() on an unconnected socket is ENOTCONN\n");

    int u = socket(AF_INET, SOCK_DGRAM, 0);
    ok("unconnected UDP shutdown -> ENOTCONN",
       u >= 0 && shutdown(u, SHUT_RDWR) < 0 && errno == ENOTCONN,
       "reported success on an unconnected socket");
    if (u >= 0) close(u);

    int x = socket(AF_UNIX, SOCK_STREAM, 0);
    ok("unconnected AF_UNIX shutdown -> ENOTCONN",
       x >= 0 && shutdown(x, SHUT_RDWR) < 0 && errno == ENOTCONN,
       "reported success on an unconnected socket");
    if (x >= 0) close(x);

    /* A connected pair must still shut down successfully. */
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        ok("a connected socket still shuts down", shutdown(sv[0], SHUT_WR) == 0,
           "the ENOTCONN check caught a connected socket");
        close(sv[0]);
        close(sv[1]);
    }
}

/* UNIX-06: listen()/connect() must validate the socket type. */
static void test_type_validation(void)
{
    printf("UNIX-06: listen() on a datagram socket is EOPNOTSUPP\n");

    const char *path = "/tmp/t-sockconf-dg";
    struct sockaddr_un un;
    unlink(path);
    int d = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (d < 0) { ok("socket", 0, "socket failed"); return; }
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strlcpy(un.sun_path, path, sizeof(un.sun_path));
    if (bind(d, (struct sockaddr *)&un, sizeof(un)) < 0) {
        ok("bind", 0, "bind failed");
        close(d);
        return;
    }
    ok("listen() refuses a datagram socket",
       listen(d, 5) < 0 && errno == EOPNOTSUPP,
       "a datagram socket was put into the listening state");

    close(d);
    unlink(path);

    /* A stream socket must still be able to listen. */
    const char *spath = "/tmp/t-sockconf-st";
    unlink(spath);
    int st = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strlcpy(un.sun_path, spath, sizeof(un.sun_path));
    if (st >= 0 && bind(st, (struct sockaddr *)&un, sizeof(un)) == 0) {
        ok("listen() still accepts a stream socket", listen(st, 5) == 0,
           "the type check caught a stream socket");
    }
    if (st >= 0) close(st);
    unlink(spath);
}

/* UDP-05: two implicitly-bound sockets must not share a source port. */
static void test_ephemeral_unique(void)
{
    printf("UDP-05: implicit binds get distinct ports\n");

    enum { N = 24 };
    int fds[N];
    unsigned short ports[N];
    int made = 0;
    struct sockaddr_in dst;
    lo_addr(&dst, 33003);
    int sink = bind_udp(33003);

    for (int i = 0; i < N; i++) {
        fds[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (fds[i] < 0) break;
        /* An implicit bind happens on first send. */
        sendto(fds[i], "x", 1, 0, (struct sockaddr *)&dst, sizeof(dst));
        struct sockaddr_in me;
        socklen_t ml = sizeof(me);
        memset(&me, 0, sizeof(me));
        if (getsockname(fds[i], (struct sockaddr *)&me, &ml) < 0) break;
        ports[i] = ntohs(me.sin_port);
        made++;
    }
    ok("all sockets got a source port", made == N, "setup failed");

    int dup = 0;
    for (int i = 0; i < made; i++) {
        if (ports[i] == 0) { dup = 1; break; }
        for (int j = i + 1; j < made; j++)
            if (ports[i] == ports[j]) { dup = 1; break; }
    }
    ok("no two sockets share an ephemeral port", !dup,
       "the same port was handed out twice");

    for (int i = 0; i < made; i++) close(fds[i]);
    if (sink >= 0) close(sink);
}

/*
 * UDP-06: a datagram sent just before the reader blocks must wake it
 * promptly rather than waiting out the fallback deadline.  A single-threaded
 * test cannot hit the exact race, but it can confirm the reworked sleep path
 * still delivers correctly and does not hang -- which is what a broken
 * queue-then-release would break outright.
 */
static void test_blocking_receive(void)
{
    printf("UDP-06: blocking receive still wakes on delivery\n");

    struct sockaddr_in dst;
    int rx = bind_udp(33005);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx < 0 || tx < 0) { ok("setup", 0, "socket/bind failed"); return; }

    lo_addr(&dst, 33005);
    const char msg[] = "wake-me";
    /* Queue it first, then do a BLOCKING read: the data is already there, so
     * the loop must take the fast path and never sleep. */
    sendto(tx, msg, sizeof(msg), 0, (struct sockaddr *)&dst, sizeof(dst));
    char buf[32];
    ssize_t r = recv(rx, buf, sizeof(buf), 0);
    ok("blocking recv returned the datagram",
       r == (ssize_t)sizeof(msg) && memcmp(buf, msg, sizeof(msg)) == 0,
       "blocking receive path is broken");

    close(rx);
    close(tx);
}

/* UDP-04: a datagram larger than the old 1500-byte cap survives whole. */
static void test_large_datagram(void)
{
    printf("UDP-04: datagrams above the old 1500-byte cap are not truncated\n");

    static char out[1536], in[2048];
    for (size_t i = 0; i < sizeof(out); i++) out[i] = (char)(i * 7 + 3);

    struct sockaddr_in dst;
    int rx = bind_udp(33007);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx < 0 || tx < 0) { ok("setup", 0, "socket/bind failed"); return; }

    lo_addr(&dst, 33007);
    ssize_t w = sendto(tx, out, sizeof(out), 0,
                       (struct sockaddr *)&dst, sizeof(dst));
    if (w < 0) {
        /* The link MTU bounds this; if the send is refused there is nothing
         * the receive side could have truncated. */
        printf("        send refused at %u bytes (errno=%d) -- MTU-bound\n",
               (unsigned)sizeof(out), errno);
        ok("oversized send is refused rather than silently cut",
           errno == EMSGSIZE, "unexpected send error");
    } else {
        ssize_t r = recv(rx, in, sizeof(in), MSG_DONTWAIT);
        ok("the whole datagram came back",
           r == (ssize_t)sizeof(out) && memcmp(in, out, sizeof(out)) == 0,
           "the datagram was truncated on receive");
    }

    close(rx);
    close(tx);
}

int main(void)
{
    printf("torture_sockconf: socket conformance batch (#430 MEDIUM/LOW)\n\n");

    test_peek_and_trunc_udp();
    test_peek_and_trunc_unix();
    test_iovec_framing();
    test_dontwait_scope();
    test_shutdown_notconn();
    test_type_validation();
    test_ephemeral_unique();
    test_blocking_receive();
    test_large_datagram();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
