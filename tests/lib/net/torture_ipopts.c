/*
 * torture_ipopts.c — IPPROTO_IP socket options this stack does not
 * implement must fail ENOPROTOOPT, not report success.
 *
 * setsockopt() used to answer 0 for every IPPROTO_IP option it did not
 * recognise.  With IP_HDRINCL a raw socket's caller then built its own IP
 * header, which went out as payload behind a second, kernel-built one;
 * IP_MTU_DISCOVER claimed a DF policy no datagram carried.
 *
 *   hdrincl      IP_HDRINCL on a raw socket fails ENOPROTOOPT
 *   mtudisc      IP_MTU_DISCOVER (Linux 10) on UDP fails ENOPROTOOPT
 *   unknown      an unassigned IPPROTO_IP option (99) fails ENOPROTOOPT
 *   ttl          IP_TTL still works and reads back
 *
 * Substrate only (Linux implements the first two).  Runs as init; prints a
 * "Result:" line.  Build:
 *   i386-unknown-substrate-gcc -O2 -Wall -o tests/lib/net/torture_ipopts \
 *       tests/lib/net/torture_ipopts.c
 */
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define IP_MTU_DISCOVER_LINUX 10

static int failures;

static void expect_noprotoopt(const char *name, int fd, int opt) {
    int one = 1;
    int r = setsockopt(fd, IPPROTO_IP, opt, &one, sizeof(one));
    if (r == -1 && errno == ENOPROTOOPT) {
        printf("  ok   %s\n", name);
    } else {
        printf("  FAIL %s: setsockopt returned %d errno %d\n", name, r,
               r < 0 ? errno : 0);
        failures++;
    }
}

int main(void) {
    printf("torture_ipopts: unimplemented IPPROTO_IP options\n");

    int raw = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (raw < 0) {
        printf("  FAIL hdrincl: raw socket errno %d\n", errno);
        failures++;
    } else {
        expect_noprotoopt("hdrincl", raw, IP_HDRINCL);
        close(raw);
    }

    int u = socket(AF_INET, SOCK_DGRAM, 0);
    expect_noprotoopt("mtudisc", u, IP_MTU_DISCOVER_LINUX);
    expect_noprotoopt("unknown", u, 99);

    int ttl = 5, got = 0;
    socklen_t len = sizeof(got);
    if (setsockopt(u, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) != 0 ||
        getsockopt(u, IPPROTO_IP, IP_TTL, &got, &len) != 0 || got != 5) {
        printf("  FAIL ttl: set/get gave %d (errno %d)\n", got, errno);
        failures++;
    } else {
        printf("  ok   ttl\n");
    }
    close(u);

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    fflush(stdout);
    if (getpid() == 1)
        for (;;)
            pause();
    return failures ? 1 : 0;
}
