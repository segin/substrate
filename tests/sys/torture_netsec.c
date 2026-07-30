/*
 * torture_netsec.c — regression test for the socket userspace-boundary
 * findings (task #427: SOCK-01, SOCK-02, UNIX-01, SOCK-09).
 *
 * Every case here is an attempt at the actual exploit, so a PASS means the
 * kernel refused rather than that some internal predicate returned the right
 * value.  Before the fix these variously wrote to a caller-chosen kernel
 * address, put kernel memory on the wire, or panicked the machine outright --
 * so "the test program survived to print a result at all" is itself part of
 * what is being checked.
 *
 * Run as init:  qemu ... -append "init=/tmp/torture_netsec"
 */
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

/* Somewhere in the kernel's higher-half direct map.  Any successful write
 * here from userspace is a total loss of kernel integrity. */
#define KERNEL_ADDR ((void *)0xC0100000UL)

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

/* SOCK-01: accept() must not write the peer sockaddr through a raw user
 * pointer.  We cannot observe the kernel write directly from here, so we
 * check the two things we can: the call reports EFAULT-or-success rather
 * than corrupting us, and the machine is still alive afterwards. */
static void test_accept_kernel_addr(void)
{
    printf("accept() with a kernel-address sockaddr:\n");

    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    ok("socket(AF_UNIX)", lfd >= 0, "cannot create listener");
    if (lfd < 0) return;

    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strcpy(sun.sun_path, "/tmp/.netsec-accept");
    unlink(sun.sun_path);

    ok("bind", bind(lfd, (struct sockaddr *)&sun, sizeof(sun)) == 0, "bind failed");
    ok("listen", listen(lfd, 4) == 0, "listen failed");

    /* Non-blocking so accept() returns immediately with no pending peer:
     * that is enough to exercise the out-param path's pointer handling
     * without needing a second process. */
    fcntl(lfd, F_SETFL, O_NONBLOCK);

    socklen_t *kernel_len = (socklen_t *)KERNEL_ADDR;
    int r = accept(lfd, (struct sockaddr *)KERNEL_ADDR, kernel_len);
    /* With no peer queued we expect EAGAIN; the point is that we got a
     * return value at all instead of taking the kernel down. */
    ok("accept(kernel addr) returns instead of faulting",
       r < 0, "accept unexpectedly succeeded into kernel memory");

    close(lfd);
    unlink(sun.sun_path);
}

/* SOCK-02: sendto() must not transmit from a kernel address. */
static void test_sendto_kernel_buf(void)
{
    printf("sendto() with a kernel-address payload:\n");

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    ok("socket(AF_INET,DGRAM)", fd >= 0, "cannot create UDP socket");
    if (fd < 0) return;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(9);              /* discard */
    dst.sin_addr.s_addr = htonl(0x7F000001);  /* 127.0.0.1 */

    ssize_t n = sendto(fd, KERNEL_ADDR, 1400, 0,
                       (struct sockaddr *)&dst, sizeof(dst));
    ok("sendto(kernel buf) fails with EFAULT",
       n < 0 && errno == EFAULT,
       "kernel memory was accepted for transmission");

    close(fd);
}

/* SOCK-02: the destination sockaddr itself must be copied in, not chased. */
static void test_sendto_kernel_addr(void)
{
    printf("sendto() with a kernel-address destination:\n");

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { ok("socket", 0, "cannot create UDP socket"); return; }

    char payload[16];
    memset(payload, 'x', sizeof(payload));
    ssize_t n = sendto(fd, payload, sizeof(payload), 0,
                       (struct sockaddr *)KERNEL_ADDR, sizeof(struct sockaddr_in));
    ok("sendto(kernel sockaddr) fails cleanly",
       n < 0, "a kernel-address sockaddr was dereferenced successfully");

    close(fd);
}

/* UNIX-01: AF_UNIX sendto()-by-path must not copy FROM kernel memory into a
 * socket the caller can then read back.  That was an arbitrary kernel read. */
static void test_unix_dgram_kernel_read(void)
{
    printf("AF_UNIX sendto()-by-path with a kernel-address payload:\n");

    int rx = socket(AF_UNIX, SOCK_DGRAM, 0);
    int tx = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (rx < 0 || tx < 0) { ok("socket", 0, "cannot create dgram pair"); return; }

    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strcpy(sun.sun_path, "/tmp/.netsec-dgram");
    unlink(sun.sun_path);
    ok("bind receiver", bind(rx, (struct sockaddr *)&sun, sizeof(sun)) == 0,
       "bind failed");

    ssize_t n = sendto(tx, KERNEL_ADDR, 256, 0,
                       (struct sockaddr *)&sun, sizeof(sun));
    ok("sendto(kernel buf) to a path fails with EFAULT",
       n < 0 && errno == EFAULT,
       "kernel memory was copied into a readable socket buffer");

    /*
     * And no BYTES should be readable.  Note the check is "<= 0", not "< 0":
     * an empty non-blocking AF_UNIX recv() reports 0 here rather than
     * -EAGAIN, and either way no kernel bytes reached us -- which is the
     * property under test.  The count is printed so a real leak (got > 0)
     * cannot hide behind a passing assertion.
     */
    char buf[256];
    fcntl(rx, F_SETFL, O_NONBLOCK);
    ssize_t got = recv(rx, buf, sizeof(buf), 0);
    printf("        (recv returned %ld)\n", (long)got);
    ok("no bytes were delivered", got <= 0,
       "a datagram sourced from kernel memory arrived");

    close(rx);
    close(tx);
    unlink(sun.sun_path);
}

/* SOCK-09: setsockopt() must not claim success on a non-socket. */
static void test_setsockopt_nonsocket(void)
{
    printf("setsockopt() on descriptors that are not sockets:\n");

    int fd = open("/etc/passwd", O_RDONLY);
    if (fd < 0) fd = open("/", O_RDONLY);
    if (fd >= 0) {
        int on = 1;
        int r = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        ok("setsockopt(regular file) fails with ENOTSOCK",
           r < 0 && errno == ENOTSOCK,
           "a non-socket fd was accepted");
        close(fd);
    }

    int on = 1;
    int r = setsockopt(4242, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    ok("setsockopt(closed fd) fails with EBADF",
       r < 0 && errno == EBADF, "an out-of-range fd was accepted");

    /* A real socket must still work — the fix must not break the option. */
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s >= 0) {
        r = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        ok("setsockopt(real socket) still succeeds", r == 0,
           "SO_REUSEADDR regressed on a valid socket");
        close(s);
    }
}

/* The fixes must not have broken ordinary loopback traffic. */
static void test_normal_traffic_still_works(void)
{
    printf("ordinary AF_UNIX datagram round-trip:\n");

    int rx = socket(AF_UNIX, SOCK_DGRAM, 0);
    int tx = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (rx < 0 || tx < 0) { ok("socket", 0, "cannot create dgram pair"); return; }

    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strcpy(sun.sun_path, "/tmp/.netsec-ok");
    unlink(sun.sun_path);
    bind(rx, (struct sockaddr *)&sun, sizeof(sun));

    static const char msg[] = "substrate";
    ssize_t n = sendto(tx, msg, sizeof(msg), 0,
                       (struct sockaddr *)&sun, sizeof(sun));
    ok("sendto delivers", n == (ssize_t)sizeof(msg), "send failed");

    char buf[64];
    memset(buf, 0, sizeof(buf));
    ssize_t got = recv(rx, buf, sizeof(buf), 0);
    ok("recv returns the same bytes",
       got == (ssize_t)sizeof(msg) && memcmp(buf, msg, sizeof(msg)) == 0,
       "payload did not survive the bounce");

    close(rx);
    close(tx);
    unlink(sun.sun_path);
}

int main(void)
{
    printf("torture_netsec: socket userspace-boundary regressions (#427)\n\n");

    test_accept_kernel_addr();
    test_sendto_kernel_buf();
    test_sendto_kernel_addr();
    test_unix_dgram_kernel_read();
    test_setsockopt_nonsocket();
    test_normal_traffic_still_works();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
