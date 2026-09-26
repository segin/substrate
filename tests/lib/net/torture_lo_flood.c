/*
 * torture_lo_flood.c — a loopback send that is dropped says so.
 *
 * lo_xmit() dropped a frame when its 128-slot ring was full but returned
 * success, so a sender flooding 127.0.0.1 was told every datagram went out
 * (RFC 791 3.3 SEND returns a result).
 *
 * Sends 5000 back-to-back 1000-octet datagrams to 127.0.0.1.  The sender
 * outruns the loopback drain thread, so the ring fills: every send must
 * either succeed or fail ENOBUFS, and at least one must fail.  (Should the
 * drain thread ever keep up with a sender that never blocks, the last
 * check would need a slower drain to provoke the overflow.)  Runs as init;
 * prints a "Result:" line.  Build:
 *   i386-unknown-substrate-gcc -O2 -Wall -o tests/lib/net/torture_lo_flood \
 *       tests/lib/net/torture_lo_flood.c
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SENDS 5000

int main(void) {
    int failures = 0;
    printf("torture_lo_flood: %d datagrams to 127.0.0.1\n", SENDS);
    int r = socket(AF_INET, SOCK_DGRAM, 0);
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(7415) };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (r < 0 || s < 0 || bind(r, (struct sockaddr *)&a, sizeof(a)) < 0) {
        printf("  FAIL setup errno %d\n", errno);
        failures++;
        goto out;
    }
    static char buf[1000];
    memset(buf, 'f', sizeof(buf));
    int ok = 0, nobufs = 0, other = 0, other_errno = 0;
    for (int i = 0; i < SENDS; i++) {
        if (sendto(s, buf, sizeof(buf), 0, (struct sockaddr *)&a, sizeof(a)) >= 0)
            ok++;
        else if (errno == ENOBUFS)
            nobufs++;
        else {
            other++;
            other_errno = errno;
        }
    }
    printf("  %d sent, %d ENOBUFS, %d other\n", ok, nobufs, other);
    if (other) {
        printf("  FAIL unexpected send error %d\n", other_errno);
        failures++;
    }
    if (nobufs == 0) {
        printf("  FAIL the ring overflowed silently: no send reported ENOBUFS\n");
        failures++;
    }
out:
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    fflush(stdout);
    if (getpid() == 1)
        for (;;)
            pause();
    return failures ? 1 : 0;
}
