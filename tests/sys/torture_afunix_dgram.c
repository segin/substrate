/*
 * torture_afunix_dgram.c — regression test for UNIX-05 (task #430).
 *
 * A bound AF_UNIX SOCK_DGRAM socket was unusable as a server in three
 * independent ways: an empty queue returned 0 (EOF) instead of waiting,
 * because "no peer" was read as "peer gone"; poll() reported a bare POLLHUP
 * for the BOUND state so it was never readable; and recvfrom() hardcoded
 * the source address length to 0, so a server could not learn who had
 * written to it and therefore could not reply.
 *
 * The test is the thing those three defects made impossible: a
 * request/response exchange between a bound server socket and a bound
 * client socket, driven through poll().
 *
 * Run as init:  qemu ... -append "init=/tmp/torture_afunix_dgram"
 */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

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

static int bind_dgram(const char *path)
{
    struct sockaddr_un un;
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    unlink(path);
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strlcpy(un.sun_path, path, sizeof(un.sun_path));
    if (bind(fd, (struct sockaddr *)&un, sizeof(un)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void test_request_response(void)
{
    printf("UNIX-05: bound datagram server can receive and reply\n");

    const char *spath = "/tmp/t-dgram-srv";
    const char *cpath = "/tmp/t-dgram-cli";

    int srv = bind_dgram(spath);
    int cli = bind_dgram(cpath);
    ok("both sockets bound", srv >= 0 && cli >= 0, "bind failed");
    if (srv < 0 || cli < 0) return;

    /* Empty server queue: a non-blocking read must say "nothing yet"
     * (EAGAIN), NOT 0.  A 0 here is the old EOF bug, and it is what made
     * every blocking server exit immediately. */
    fcntl(srv, F_SETFL, O_NONBLOCK);
    char probe[8];
    ssize_t e = recv(srv, probe, sizeof(probe), 0);
    ok("empty queue is not EOF", e < 0 && errno == EAGAIN,
       e == 0 ? "returned 0 (EOF) on an empty bound socket" : "unexpected");
    fcntl(srv, F_SETFL, 0);

    /* poll() on the idle server: not readable, but also not hung up. */
    struct pollfd pfd = { .fd = srv, .events = POLLIN, .revents = 0 };
    poll(&pfd, 1, 0);
    ok("idle server is not POLLHUP", !(pfd.revents & POLLHUP),
       "a bound datagram socket reported hangup");
    ok("idle server is not readable", !(pfd.revents & POLLIN),
       "reported readable with an empty queue");

    /* Client sends a request. */
    struct sockaddr_un to;
    memset(&to, 0, sizeof(to));
    to.sun_family = AF_UNIX;
    strlcpy(to.sun_path, spath, sizeof(to.sun_path));
    const char req[] = "PING";
    ssize_t w = sendto(cli, req, sizeof(req), 0,
                       (struct sockaddr *)&to, sizeof(to));
    ok("client request sent", w == (ssize_t)sizeof(req), "sendto failed");

    /* Now poll must report the server readable. */
    pfd.revents = 0;
    poll(&pfd, 1, 0);
    ok("server became readable", (pfd.revents & POLLIN) != 0,
       "poll never reports a bound datagram socket readable");

    /* Server receives, and must learn who sent it. */
    char buf[64];
    struct sockaddr_un from;
    socklen_t fromlen = sizeof(from);
    memset(&from, 0, sizeof(from));
    ssize_t r = recvfrom(srv, buf, sizeof(buf), 0,
                         (struct sockaddr *)&from, &fromlen);
    ok("request received intact",
       r == (ssize_t)sizeof(req) && memcmp(buf, req, sizeof(req)) == 0,
       "payload wrong");
    ok("sender address was reported", fromlen > 2,
       "recvfrom returned a zero-length source address");
    ok("sender address is the client's path",
       fromlen > 2 && strcmp(from.sun_path, cpath) == 0,
       "source path did not match the client's bound name");

    /* And the server replies to that address — the whole point. */
    const char rsp[] = "PONG";
    ssize_t w2 = sendto(srv, rsp, sizeof(rsp), 0,
                        (struct sockaddr *)&from, fromlen);
    ok("server reply sent", w2 == (ssize_t)sizeof(rsp),
       "could not send back to the reported address");

    char rbuf[64];
    ssize_t r2 = recv(cli, rbuf, sizeof(rbuf), 0);
    ok("client got the reply",
       r2 == (ssize_t)sizeof(rsp) && memcmp(rbuf, rsp, sizeof(rsp)) == 0,
       "reply never arrived");

    close(srv);
    close(cli);
    unlink(spath);
    unlink(cpath);
}

/*
 * Datagram boundaries must survive the added source header: several
 * datagrams of different sizes, from senders with different path lengths,
 * have to come back out one per read and in order.
 */
static void test_boundaries(void)
{
    printf("UNIX-05: framing survives the source header\n");

    const char *spath = "/tmp/t-dgram-b";
    int srv = bind_dgram(spath);
    if (srv < 0) { ok("bind", 0, "bind failed"); return; }

    /* Three senders with deliberately different name lengths, so the
     * per-frame srclen varies and a fixed-offset bug shows up. */
    const char *cpaths[3] = { "/tmp/a", "/tmp/bbbbbbbb", "/tmp/cccccccccccccc" };
    const char *msgs[3]   = { "one", "twotwotwo", "threethreethreethree" };
    int clis[3];
    for (int i = 0; i < 3; i++) {
        clis[i] = bind_dgram(cpaths[i]);
        if (clis[i] < 0) { ok("bind client", 0, "bind failed"); return; }
    }

    struct sockaddr_un to;
    memset(&to, 0, sizeof(to));
    to.sun_family = AF_UNIX;
    strlcpy(to.sun_path, spath, sizeof(to.sun_path));
    for (int i = 0; i < 3; i++)
        sendto(clis[i], msgs[i], strlen(msgs[i]), 0,
               (struct sockaddr *)&to, sizeof(to));

    int good = 1;
    for (int i = 0; i < 3; i++) {
        char buf[64];
        struct sockaddr_un from;
        socklen_t fl = sizeof(from);
        memset(&from, 0, sizeof(from));
        memset(buf, 0, sizeof(buf));
        ssize_t r = recvfrom(srv, buf, sizeof(buf), 0,
                             (struct sockaddr *)&from, &fl);
        if (r != (ssize_t)strlen(msgs[i]) || memcmp(buf, msgs[i], r) != 0) {
            good = 0;
            printf("        datagram %d: got %d bytes '%s'\n", i, (int)r, buf);
        }
        if (fl <= 2 || strcmp(from.sun_path, cpaths[i]) != 0) {
            good = 0;
            printf("        datagram %d: source '%s' expected '%s'\n",
                   i, fl > 2 ? from.sun_path : "(none)", cpaths[i]);
        }
    }
    ok("three datagrams, three senders, in order", good,
       "a frame boundary or source field was misparsed");

    close(srv);
    unlink(spath);
    for (int i = 0; i < 3; i++) { close(clis[i]); unlink(cpaths[i]); }
}

/* A connected datagram pair (socketpair) writes frames with no named
 * source; those must still parse correctly, since they share the format. */
static void test_socketpair_still_works(void)
{
    printf("UNIX-05: connected datagram frames still parse\n");

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0) {
        ok("socketpair", 0, "socketpair failed");
        return;
    }
    const char a[] = "first", b[] = "second-longer";
    write(sv[0], a, sizeof(a));
    write(sv[0], b, sizeof(b));

    char buf[64];
    ssize_t r1 = read(sv[1], buf, sizeof(buf));
    int ok1 = (r1 == (ssize_t)sizeof(a) && memcmp(buf, a, sizeof(a)) == 0);
    ssize_t r2 = read(sv[1], buf, sizeof(buf));
    int ok2 = (r2 == (ssize_t)sizeof(b) && memcmp(buf, b, sizeof(b)) == 0);
    ok("both datagrams came back whole and in order", ok1 && ok2,
       "connected framing broke");

    close(sv[0]);
    close(sv[1]);
}

int main(void)
{
    printf("torture_afunix_dgram: bound datagram servers (#430 UNIX-05)\n\n");

    test_request_response();
    test_boundaries();
    test_socketpair_still_works();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
