/*
 * torture_srcaddr.c — the IPv4 source address a socket sends from.
 *
 * TCP-HDR-01, UDP-U-02, UDP-U-03 (docs/ip-audit-2026-09-22.md).  Transports
 * summed their pseudo-header checksum over one source address and then let
 * ip4_output() stamp another into the IP header, chosen by routing.  So:
 *
 *   udp-bound   a UDP socket bound to the NIC address sending to 127.0.0.1
 *               was seen by the receiver as coming from 127.0.0.1 -- bind()'s
 *               address was ignored on transmit.
 *   tcp-bound   a TCP socket bound to the NIC address connecting to
 *               127.0.0.1 sent a SYN whose checksum covered 10.0.2.15 inside
 *               an IP header saying 127.0.0.1, so every peer discarded it.
 *               A raw socket captures that SYN and verifies its checksum
 *               against the IP header's own addresses.  (Completing that
 *               handshake also needs the reply to 10.0.2.15 to be looped
 *               back rather than ARPed for on the wire -- UDP-IP-03 -- so
 *               the test checks the SYN, not the connect.)
 *   lo-guard    once the bound source is honoured, a socket bound to
 *               127.0.0.1 must not put that source on a real wire (RFC 1122
 *               3.2.1.3(g)): sending off-host fails EINVAL.
 *
 * Needs a configured NIC (10.0.2.15): boot with a network device.
 * Run as init; prints a "Result:" line.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define NIC_ADDR "10.0.2.15"

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

static void sin_set(struct sockaddr_in *sa, const char *ip, unsigned short port)
{
    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    sa->sin_port = htons(port);
    sa->sin_addr.s_addr = inet_addr(ip);
}

static void test_udp_bound(void)
{
    printf("UDP-U-02: a bound UDP socket sends from its bound address\n");
    struct sockaddr_in a, from;
    socklen_t flen = sizeof(from);
    char buf[32];
    int rx = socket(AF_INET, SOCK_DGRAM, 0);
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    sin_set(&a, "127.0.0.1", 31980);
    int r1 = bind(rx, (struct sockaddr *)&a, sizeof(a));
    sin_set(&a, NIC_ADDR, 31981);
    int r2 = bind(tx, (struct sockaddr *)&a, sizeof(a));
    ok("sockets bound", rx >= 0 && tx >= 0 && r1 == 0 && r2 == 0, "socket/bind failed");
    sin_set(&a, "127.0.0.1", 31980);
    ssize_t n = sendto(tx, "hello", 5, 0, (struct sockaddr *)&a, sizeof(a));
    ok("sendto accepted", n == 5, "sendto failed");
    memset(&from, 0, sizeof(from));
    n = recvfrom(rx, buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr *)&from, &flen);
    ok("datagram arrived with a valid checksum", n == 5, "datagram dropped");
    ok("receiver sees the bound source address",
       n == 5 && from.sin_addr.s_addr == inet_addr(NIC_ADDR) &&
       from.sin_port == htons(31981), "source was not the bound address");
    close(rx);
    close(tx);
}

static unsigned short csum16(const unsigned char *b, size_t n, unsigned long sum)
{
    for (; n > 1; b += 2, n -= 2)
        sum += (unsigned long)((b[0] << 8) | b[1]);
    if (n)
        sum += (unsigned long)(b[0] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (unsigned short)~sum;
}

/* Wait for the SYN to dport on a raw IPPROTO_TCP socket; returns the IP
 * datagram length, 0 if none arrived. */
static ssize_t capture_syn(int raw, unsigned char *pkt, size_t cap,
                           unsigned short dport)
{
    for (int tries = 0; tries < 40; tries++) {
        ssize_t n = recv(raw, pkt, cap, MSG_DONTWAIT);
        if (n >= 40) {
            size_t ihl = (size_t)(pkt[0] & 0xF) * 4;
            const unsigned char *t = pkt + ihl;
            if (pkt[9] == 6 && (size_t)n >= ihl + 20 &&
                ((t[2] << 8) | t[3]) == dport && (t[13] & 0x02))
                return n;
            continue;
        }
        usleep(50000);
    }
    return 0;
}

static void test_tcp_bound(void)
{
    printf("TCP-HDR-01: a bound TCP socket's segments carry a matching source\n");
    struct sockaddr_in a;
    int l = socket(AF_INET, SOCK_STREAM, 0);
    int c = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sin_set(&a, "127.0.0.1", 31982);
    int r1 = bind(l, (struct sockaddr *)&a, sizeof(a));
    int r2 = listen(l, 4);
    sin_set(&a, NIC_ADDR, 0);
    int r3 = bind(c, (struct sockaddr *)&a, sizeof(a));
    ok("sockets set up", l >= 0 && c >= 0 && !r1 && !r2 && !r3, "socket/bind/listen failed");

    int raw = socket(AF_INET, SOCK_RAW, 6);
    ok("raw TCP capture socket", raw >= 0, "socket(SOCK_RAW) failed");
    fcntl(c, F_SETFL, fcntl(c, F_GETFL) | O_NONBLOCK);
    sin_set(&a, "127.0.0.1", 31982);
    connect(c, (struct sockaddr *)&a, sizeof(a));

    unsigned char pkt[256];
    ssize_t n = raw >= 0 ? capture_syn(raw, pkt, sizeof(pkt), 31982) : 0;
    ok("the SYN was captured", n > 0, "no SYN seen");
    if (n > 0) {
        size_t ihl = (size_t)(pkt[0] & 0xF) * 4;
        size_t tlen = (size_t)n - ihl;
        unsigned long sum = 0;
        for (int i = 12; i < 20; i += 2)            /* pseudo-header: src, dst */
            sum += (unsigned long)((pkt[i] << 8) | pkt[i + 1]);
        sum += 6 + tlen;                            /* zero, PTCL, TCP length */
        struct in_addr src;
        memcpy(&src, pkt + 12, 4);
        ok("the SYN's IP source is the bound address",
           src.s_addr == inet_addr(NIC_ADDR), "IP header carries another source");
        ok("the SYN's checksum verifies against its own IP header",
           csum16(pkt + ihl, tlen, sum) == 0,
           "checksum was computed over a different source address");
    }
    if (raw >= 0) close(raw);
    close(c);
    close(l);
}

static void test_lo_guard(void)
{
    printf("RFC 1122 3.2.1.3(g): a 127/8 source never leaves the host\n");
    struct sockaddr_in a;
    int tx = socket(AF_INET, SOCK_DGRAM, 0);
    sin_set(&a, "127.0.0.1", 31983);
    int r = bind(tx, (struct sockaddr *)&a, sizeof(a));
    ok("socket bound to 127.0.0.1", tx >= 0 && r == 0, "bind failed");
    sin_set(&a, "10.0.2.2", 31984);
    errno = 0;
    ssize_t n = sendto(tx, "x", 1, 0, (struct sockaddr *)&a, sizeof(a));
    ok("off-host send from 127.0.0.1 fails EINVAL", n < 0 && errno == EINVAL,
       "the off-host send from a 127.0.0.1-bound socket succeeded");
    close(tx);
}

int main(void)
{
    printf("torture_srcaddr: IPv4 source address selection\n\n");
    test_udp_bound();
    test_tcp_bound();
    test_lo_guard();
    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
