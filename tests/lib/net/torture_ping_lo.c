/*
 * torture_ping_lo.c — an ICMP echo to any 127/8 address is answered.
 *
 * The echo handler refused any request from a 127/8 source, meant to catch
 * a spoofed loopback source arriving on a NIC, but loopback traffic itself
 * always has one -- so `ping 127.0.0.1` never got a reply (RFC 1122
 * 3.2.2.6: every host must answer echo; 3.2.1.3(g): all of 127/8 is the
 * host's own loopback).
 *
 * Pings 127.0.0.1, 127.0.0.2, 127.1.2.3 and 127.255.255.254 on a raw ICMP
 * socket, one request each (sequence number = index), and waits up to 2 s
 * for each matching reply.  Runs as init; prints a "Result:" line.  Build:
 *   i386-unknown-substrate-gcc -O2 -Wall -o tests/lib/net/torture_ping_lo \
 *       tests/lib/net/torture_ping_lo.c
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define ECHO_ID 0x5171

static const char *const targets[] = {
    "127.0.0.1", "127.0.0.2", "127.1.2.3", "127.255.255.254",
};

static uint16_t csum(const void *p, size_t n) {
    const uint8_t *b = p;
    uint32_t s = 0;
    for (size_t i = 0; i + 1 < n; i += 2)
        s += (uint32_t)(b[i] << 8 | b[i + 1]);
    if (n & 1)
        s += (uint32_t)(b[n - 1] << 8);
    while (s >> 16)
        s = (s & 0xFFFF) + (s >> 16);
    return htons((uint16_t)~s);
}

static long ms_since(const struct timespec *t0) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t0->tv_sec) * 1000 + (t.tv_nsec - t0->tv_nsec) / 1000000;
}

/* Send one echo request to `addr` and wait for its reply. */
static int ping_once(int fd, const char *addr, uint16_t seq) {
    uint8_t req[16] = { 8, 0, 0, 0 };           /* echo request */
    uint16_t v = htons(ECHO_ID);
    memcpy(req + 4, &v, 2);
    v = htons(seq);
    memcpy(req + 6, &v, 2);
    memcpy(req + 8, "substrat", 8);
    uint16_t c = csum(req, sizeof(req));
    memcpy(req + 2, &c, 2);

    struct sockaddr_in to = { .sin_family = AF_INET };
    to.sin_addr.s_addr = inet_addr(addr);
    if (sendto(fd, req, sizeof(req), 0, (struct sockaddr *)&to, sizeof(to)) < 0) {
        printf("  FAIL %s: sendto errno %d\n", addr, errno);
        return 0;
    }
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        long left = 2000 - ms_since(&t0);
        if (left <= 0)
            break;
        struct pollfd p = { .fd = fd, .events = POLLIN };
        if (poll(&p, 1, (int)left) <= 0)
            break;
        uint8_t buf[256];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 20)
            continue;
        size_t ihl = (size_t)(buf[0] & 0xF) * 4;   /* raw v4 includes the IP header */
        if ((size_t)n < ihl + 8)
            continue;
        const uint8_t *ic = buf + ihl;
        uint16_t rid, rseq;
        memcpy(&rid, ic + 4, 2);
        memcpy(&rseq, ic + 6, 2);
        if (ic[0] == 0 && ntohs(rid) == ECHO_ID && ntohs(rseq) == seq) {
            printf("  ok   %s\n", addr);
            return 1;
        }
    }
    printf("  FAIL %s: no echo reply\n", addr);
    return 0;
}

int main(void) {
    int failures = 0;
    printf("torture_ping_lo: ICMP echo across 127/8\n");
    /* 1 is IPPROTO_ICMP, which substrate's <netinet/in.h> does not yet
     * define. */
    int fd = socket(AF_INET, SOCK_RAW, 1);
    if (fd < 0) {
        printf("  FAIL raw socket errno %d\n", errno);
        failures++;
    } else {
        for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++)
            if (!ping_once(fd, targets[i], (uint16_t)(i + 1)))
                failures++;
        close(fd);
    }
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    fflush(stdout);
    if (getpid() == 1)
        for (;;)
            pause();
    return failures ? 1 : 0;
}
