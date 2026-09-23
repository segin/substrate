/*
 * torture_bsdsockopt.c -- UDP-API-05 (docs/ip-audit-2026-09-22.md): socket
 * options set by a BSD binary under Substrate's BSD personalities.
 *
 * sys_setsockopt()/sys_getsockopt() use Linux numbering; a BSD binary passes
 * SOL_SOCKET 0xffff and BSD SO_* values, so every option it set was lost.
 * This is plain BSD socket code: build it ON a FreeBSD or NetBSD i386 host
 * (it must pass there first -- that host is the oracle), then run the
 * static binary as init on Substrate:
 *
 *     cc -static -O2 -o torture_bsdsockopt torture_bsdsockopt.c
 *
 * NetBSD renumbers SO_SNDTIMEO/SO_RCVTIMEO (0x100b/0x100c) and uses a
 * 12-byte struct timeval on i386; FreeBSD keeps 0x1005/0x1006 and 8 bytes.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int fails;
static void ok(const char *w, int c) { printf("  %s %s\n", c ? "ok  " : "FAIL", w); if (!c) fails++; }
int main(void) {
    printf("torture_bsdsockopt\n");
    int one = 1, t = 0;
    socklen_t l = sizeof t;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    ok("getsockopt(SO_TYPE) == SOCK_DGRAM",
       getsockopt(s, SOL_SOCKET, SO_TYPE, &t, &l) == 0 && t == SOCK_DGRAM);
    struct timeval tv = { 0, 250000 }, got;
    socklen_t gl = sizeof got;
    ok("setsockopt(SO_RCVTIMEO)", setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) == 0);
    memset(&got, 0, sizeof got);
    ok("getsockopt(SO_RCVTIMEO) reads it back",
       getsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &got, &gl) == 0 &&
       got.tv_sec == 0 && got.tv_usec >= 240000 && got.tv_usec <= 260000);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(31950);
    a.sin_addr.s_addr = htonl(0x7f000001);
    bind(s, (struct sockaddr *)&a, sizeof a);
    char buf[8];
    /* gettimeofday, not CLOCK_MONOTONIC: under Substrate's NetBSD
     * personality the monotonic clock does not advance (reported
     * separately), which would void this measurement. */
    struct timeval t0, t1;
    gettimeofday(&t0, 0);
    errno = 0;
    ssize_t n = recv(s, buf, sizeof buf, 0);
    gettimeofday(&t1, 0);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_usec - t0.tv_usec) / 1000;
    ok("recv() times out with EAGAIN", n < 0 && errno == EAGAIN);
    printf("  (elapsed %ld ms)\n", ms);
    ok("after about 250 ms", ms >= 200 && ms < 2000);
    close(s);
    int r1 = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(r1, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    int v = 0;
    l = sizeof v;
    ok("getsockopt(SO_REUSEADDR) reads 1",
       getsockopt(r1, SOL_SOCKET, SO_REUSEADDR, &v, &l) == 0 && v != 0);
    printf("Result: %s\n", fails ? "FAILED" : "PASSED");
    fflush(stdout);
    for (;;) pause();
}
