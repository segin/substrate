/*
 * netpeer -- a hostile peer on the wire, played from userland.
 *
 * Two NICs share one QEMU hub.  The kernel configures the first as
 * 10.0.2.15 / fec0::3 (the victim); this program speaks raw Ethernet
 * through the other with AF_PACKET as a host 10.0.2.99 / fec0::99 that
 * wears the peer NIC's MAC, so the victim's replies pass that NIC's
 * filter and reach the packet socket (the peer NIC has no address, so its
 * own stack drops them).  Boot with:
 *
 *   -netdev hubport,id=h1,hubid=0 -device e1000,netdev=h1,mac=52:54:00:12:00:01
 *   -netdev hubport,id=h2,hubid=0 -device rtl8139,netdev=h2,mac=52:54:00:12:00:02
 *
 * Cases:
 *   mss      a SYN advertising MSS 1 made every byte its own segment: the
 *            first data segment of a 200-byte send must carry >= 64 bytes.
 *   ns-hop   a Neighbor Solicitation at hop limit 64 is not answered
 *            (RFC 4861 7.1.1: on-link means 255).
 *   ns-csum  one with a bad ICMPv6 checksum is not answered.
 *   ns-ok    a valid one is answered, by an NA at hop limit 255.
 *   connect  a connect() asleep while another thread closes its socket
 *            does not write into the freed socket: a socket made after
 *            the close must still have no peer.  (Timing-dependent: the
 *            old kernel can pass it.)
 *
 * Run as init, as root.  Substrate only.
 */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef AF_PACKET
#define AF_PACKET 17
#endif

struct sockaddr_ll {
    uint16_t sll_family;
    uint16_t sll_protocol;
    int32_t  sll_ifindex;
    uint16_t sll_hatype;
    uint8_t  sll_pkttype;
    uint8_t  sll_halen;
    uint8_t  sll_addr[8];
};

static const uint8_t mac_a[6] = { 0x52, 0x54, 0x00, 0x12, 0x00, 0x01 };
static const uint8_t mac_b[6] = { 0x52, 0x54, 0x00, 0x12, 0x00, 0x02 };
static uint8_t victim_mac[6], peer_mac[6];

static const uint8_t victim_ip[4] = { 10, 0, 2, 15 };
static const uint8_t fake_ip[4]   = { 10, 0, 2, 99 };
static const uint8_t victim_ip6[16] = { 0xfe, 0xc0, [15] = 0x03 };
static const uint8_t fake_ip6[16]   = { 0xfe, 0xc0, [15] = 0x99 };

static int pfd = -1;            /* bound to the peer NIC */
static int verbose;             /* log every frame heard */
static int passed, failed;

static void result(const char *name, int ok, const char *what)
{
    printf("%s %-8s %s\n", ok ? "ok   " : "FAIL ", name, what);
    if (ok)
        passed++;
    else
        failed++;
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static uint32_t sum16(const uint8_t *p, size_t n, uint32_t s)
{
    for (; n > 1; p += 2, n -= 2)
        s += (uint32_t)p[0] << 8 | p[1];
    if (n)
        s += (uint32_t)p[0] << 8;
    return s;
}

static uint16_t fold(uint32_t s)
{
    while (s >> 16)
        s = (s & 0xffff) + (s >> 16);
    return (uint16_t)~s;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xff; }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int bind_if(int fd, int ifindex)
{
    struct sockaddr_ll a;
    memset(&a, 0, sizeof(a));
    a.sll_family = AF_PACKET;
    a.sll_protocol = 0x0300;            /* htons(ETH_P_ALL) */
    a.sll_ifindex = ifindex;
    return bind(fd, (struct sockaddr *)&a, sizeof(a));
}

static void eth_hdr(uint8_t *f, const uint8_t *dst, uint16_t type)
{
    memcpy(f, dst, 6);
    memcpy(f + 6, peer_mac, 6);
    put16(f + 12, type);
}

static size_t arp_frame_from(uint8_t *f, int op, const uint8_t *spa,
                             const uint8_t *tha, const uint8_t *tpa);

static size_t arp_frame(uint8_t *f, int op, const uint8_t *tha, const uint8_t *tpa)
{
    return arp_frame_from(f, op, fake_ip, tha, tpa);
}

static size_t arp_frame_from(uint8_t *f, int op, const uint8_t *spa,
                             const uint8_t *tha, const uint8_t *tpa)
{
    static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    memset(f, 0, 60);
    eth_hdr(f, op == 1 ? bcast : tha, 0x0806);
    uint8_t *a = f + 14;
    put16(a, 1);
    put16(a + 2, 0x0800);
    a[4] = 6;
    a[5] = 4;
    put16(a + 6, (uint16_t)op);
    memcpy(a + 8, peer_mac, 6);
    memcpy(a + 14, spa, 4);
    if (op == 2)
        memcpy(a + 18, tha, 6);
    memcpy(a + 24, tpa, 4);
    return 60;
}

/*
 * Read frames from the peer NIC until one satisfies `want`, answering the
 * victim's ARP requests for the fake host on the way.  Returns its length,
 * or -1 on timeout.
 */
typedef int (*want_fn)(const uint8_t *f, size_t n, void *arg);

static int rx_until(uint8_t *f, size_t cap, int ms, want_fn want, void *arg)
{
    /* A packet socket cannot be polled: it is O_NONBLOCK, and read
     * until the deadline, as dhclient does. */
    static const struct timespec ms2 = { 0, 2000000 };
    uint64_t end = now_ms() + (uint64_t)ms;
    for (;;) {
        if (now_ms() >= end)
            return -1;
        ssize_t n = recv(pfd, f, cap, 0);
        if (n < 0) {
            nanosleep(&ms2, NULL);
            continue;
        }
        if (n < 14)
            continue;
        if (verbose)
            printf("netpeer:   heard %zd bytes %02x:%02x:%02x:%02x:%02x:%02x"
                   " -> %02x:%02x:%02x:%02x:%02x:%02x type %04x op %u\n",
                   n, f[6], f[7], f[8], f[9], f[10], f[11],
                   f[0], f[1], f[2], f[3], f[4], f[5], get16(f + 12),
                   n >= 22 ? get16(f + 20) : 0);
        if (memcmp(f + 6, victim_mac, 6) != 0)
            continue;                   /* our own frames, or noise */
        uint16_t type = get16(f + 12);
        if (type == 0x0806 && n >= 42 && get16(f + 20) == 1 &&
            memcmp(f + 38, fake_ip, 4) == 0) {
            uint8_t r[60];
            arp_frame(r, 2, f + 22, f + 28);
            send(pfd, r, sizeof(r), 0);
            continue;
        }
        if (want(f, (size_t)n, arg))
            return (int)n;
    }
}

/* ------------------------------------------------------------------ */
/* Finding the NICs                                                    */
/* ------------------------------------------------------------------ */

static int is_arp_reply_from_victim(const uint8_t *f, size_t n, void *arg)
{
    (void)arg;
    return get16(f + 12) == 0x0806 && n >= 42 && get16(f + 20) == 2 &&
           memcmp(f + 28, victim_ip, 4) == 0;
}

/* ARP for 10.0.2.15 out of each interface; the one that hears the answer
 * is the peer, and the answer names the victim's MAC.  Each probe asks
 * from an address of its own (10.0.2.200 up): a wrong guess at the MAC
 * would otherwise sit in the victim's cache for the fake host. */
static int find_peer(void)
{
    uint8_t f[1600];
    uint8_t probe_ip[4] = { 10, 0, 2, 200 };
    for (int ifindex = 1; ifindex <= 6; ifindex++) {
        for (int guess = 0; guess < 2; guess++) {
            probe_ip[3]++;
            memcpy(peer_mac, guess ? mac_b : mac_a, 6);
            memcpy(victim_mac, guess ? mac_a : mac_b, 6);
            int fd = socket(AF_PACKET, SOCK_RAW, 0x0300);
            if (fd < 0) {
                printf("netpeer: AF_PACKET socket: errno %d\n", errno);
                return -1;
            }
            if (bind_if(fd, ifindex) != 0) {
                printf("netpeer: bind ifindex %d: errno %d\n", ifindex, errno);
                close(fd);
                break;
            }
            fcntl(fd, F_SETFL, O_NONBLOCK);
            pfd = fd;
            uint8_t req[60];
            arp_frame_from(req, 1, probe_ip, NULL, victim_ip);
            send(pfd, req, sizeof(req), 0);
            if (rx_until(f, sizeof(f), 300, is_arp_reply_from_victim, NULL) > 0) {
                printf("netpeer: peer NIC is ifindex %d\n", ifindex);
                return 0;
            }
            /* say what was heard, for a harness that finds nothing */
            int heard = 0;
            ssize_t n;
            while ((n = recv(fd, f, sizeof(f), 0)) > 0) {
                if (heard++ < 3)
                    printf("netpeer: ifindex %d heard %zd bytes from "
                           "%02x:%02x:%02x:%02x:%02x:%02x type %04x\n",
                           ifindex, n, f[6], f[7], f[8], f[9], f[10], f[11],
                           get16(f + 12));
            }
            close(fd);
            pfd = -1;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* TCP                                                                 */
/* ------------------------------------------------------------------ */

#define FAKE_PORT 40000
#define SRV_PORT  5555

static uint16_t ip_id = 1;

static size_t ip4_frame(uint8_t *f, uint8_t proto, const uint8_t *l4, size_t l4len)
{
    eth_hdr(f, victim_mac, 0x0800);
    uint8_t *ip = f + 14;
    memset(ip, 0, 20);
    ip[0] = 0x45;
    put16(ip + 2, (uint16_t)(20 + l4len));
    put16(ip + 4, ip_id++);
    ip[8] = 64;
    ip[9] = proto;
    memcpy(ip + 12, fake_ip, 4);
    memcpy(ip + 16, victim_ip, 4);
    put16(ip + 10, fold(sum16(ip, 20, 0)));
    memcpy(ip + 20, l4, l4len);
    size_t n = 14 + 20 + l4len;
    if (n < 60) {
        memset(f + n, 0, 60 - n);
        n = 60;
    }
    return n;
}

static void send_tcp(uint8_t flags, uint32_t seq, uint32_t ack,
                     const uint8_t *opt, size_t optlen, uint16_t sport, uint16_t dport)
{
    uint8_t t[60], f[128];
    size_t hl = 20 + optlen;
    memset(t, 0, sizeof(t));
    put16(t, sport);
    put16(t + 2, dport);
    put32(t + 4, seq);
    put32(t + 8, ack);
    t[12] = (uint8_t)((hl / 4) << 4);
    t[13] = flags;
    put16(t + 14, 65535);
    memcpy(t + 20, opt, optlen);
    uint8_t ph[12];
    memcpy(ph, fake_ip, 4);
    memcpy(ph + 4, victim_ip, 4);
    ph[8] = 0;
    ph[9] = 6;
    put16(ph + 10, (uint16_t)hl);
    put16(t + 16, fold(sum16(t, hl, sum16(ph, 12, 0))));
    size_t n = ip4_frame(f, 6, t, hl);
    send(pfd, f, n, 0);
}

struct tcp_want {
    uint16_t sport, dport;
    uint8_t  flags_all;         /* flags that must all be set */
    int      need_data;
    uint32_t seq;
    size_t   dlen;
};

static int is_tcp(const uint8_t *f, size_t n, void *arg)
{
    struct tcp_want *w = arg;
    if (get16(f + 12) != 0x0800 || n < 14 + 20 + 20)
        return 0;
    const uint8_t *ip = f + 14;
    size_t ihl = (size_t)(ip[0] & 0x0f) * 4;
    if (ip[9] != 6 || memcmp(ip + 16, fake_ip, 4) != 0)
        return 0;
    size_t tot = get16(ip + 2);
    if (tot > n - 14 || tot < ihl + 20)
        return 0;
    const uint8_t *t = ip + ihl;
    size_t thl = (size_t)(t[12] >> 4) * 4;
    if (get16(t) != w->sport || get16(t + 2) != w->dport)
        return 0;
    if ((t[13] & w->flags_all) != w->flags_all)
        return 0;
    size_t dlen = tot - ihl - thl;
    if (w->need_data && dlen == 0)
        return 0;
    w->seq = get32(t + 4);
    w->dlen = dlen;
    return 1;
}

static void case_mss(void)
{
    uint8_t f[1600];
    int l = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(SRV_PORT);
    if (l < 0 || bind(l, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(l, 4) != 0) {
        result("mss", 0, "listener");
        return;
    }

    static const uint8_t mss1[4] = { 2, 4, 0, 1 };
    send_tcp(0x02, 1000, 0, mss1, sizeof(mss1), FAKE_PORT, SRV_PORT);
    struct tcp_want w = { SRV_PORT, FAKE_PORT, 0x12, 0, 0, 0 };
    if (rx_until(f, sizeof(f), 3000, is_tcp, &w) < 0) {
        result("mss", 0, "no SYN|ACK to a SYN advertising MSS 1");
        close(l);
        return;
    }
    send_tcp(0x10, 1001, w.seq + 1, NULL, 0, FAKE_PORT, SRV_PORT);

    struct pollfd p = { l, POLLIN, 0 };
    int c = poll(&p, 1, 3000) == 1 ? accept(l, NULL, NULL) : -1;
    if (c < 0) {
        result("mss", 0, "the handshake did not complete");
        close(l);
        return;
    }
    char data[200];
    memset(data, 'x', sizeof(data));
    send(c, data, sizeof(data), 0);

    struct tcp_want d = { SRV_PORT, FAKE_PORT, 0x10, 1, 0, 0 };
    if (rx_until(f, sizeof(f), 3000, is_tcp, &d) < 0) {
        result("mss", 0, "no data segment");
    } else {
        char what[96];
        snprintf(what, sizeof(what),
                 "the first data segment to an MSS-1 peer carries %zu bytes (>= 64)",
                 d.dlen);
        result("mss", d.dlen >= 64, what);
    }
    /* RST, so no retransmission follows the next cases around */
    send_tcp(0x04, 1001, 0, NULL, 0, FAKE_PORT, SRV_PORT);
    close(c);
    close(l);
}

/* ------------------------------------------------------------------ */
/* Neighbor Discovery                                                  */
/* ------------------------------------------------------------------ */

static void send_ns(uint8_t hop, int bad_csum)
{
    uint8_t f[128];
    uint8_t m[32];
    memset(m, 0, sizeof(m));
    m[0] = 135;                         /* NS */
    memcpy(m + 8, victim_ip6, 16);
    m[24] = 1;                          /* source link-layer address */
    m[25] = 1;
    memcpy(m + 26, peer_mac, 6);

    uint32_t s = sum16(fake_ip6, 16, 0);
    s = sum16(victim_ip6, 16, s);
    s += sizeof(m);
    s += 58;
    uint16_t ck = fold(sum16(m, sizeof(m), s));
    if (bad_csum)
        ck ^= 0x5a5a;
    put16(m + 2, ck);

    eth_hdr(f, victim_mac, 0x86dd);
    uint8_t *ip = f + 14;
    memset(ip, 0, 40);
    ip[0] = 0x60;
    put16(ip + 4, sizeof(m));
    ip[6] = 58;
    ip[7] = hop;
    memcpy(ip + 8, fake_ip6, 16);
    memcpy(ip + 24, victim_ip6, 16);
    memcpy(ip + 40, m, sizeof(m));
    send(pfd, f, 14 + 40 + sizeof(m), 0);
}

struct na_want { int hop; };

static int is_na(const uint8_t *f, size_t n, void *arg)
{
    struct na_want *w = arg;
    if (get16(f + 12) != 0x86dd || n < 14 + 40 + 24)
        return 0;
    const uint8_t *ip = f + 14;
    if (ip[6] != 58 || ip[40] != 136 || memcmp(ip + 24, fake_ip6, 16) != 0)
        return 0;
    w->hop = ip[7];
    return 1;
}

static void case_nd(void)
{
    uint8_t f[1600];
    struct na_want w = { 0 };

    send_ns(64, 0);
    result("ns-hop", rx_until(f, sizeof(f), 700, is_na, &w) < 0,
           "a Neighbor Solicitation at hop limit 64 is not answered");

    send_ns(255, 1);
    result("ns-csum", rx_until(f, sizeof(f), 700, is_na, &w) < 0,
           "one with a bad checksum is not answered");

    send_ns(255, 0);
    int got = rx_until(f, sizeof(f), 2000, is_na, &w) > 0;
    char what[80];
    snprintf(what, sizeof(what), "a valid one is answered at hop limit 255 (%s %d)",
             got ? "got" : "no NA,", got ? w.hop : 0);
    result("ns-ok", got && w.hop == 255, what);
}

/* ------------------------------------------------------------------ */
/* connect() asleep across a close()                                   */
/* ------------------------------------------------------------------ */

#define RACE_PORT 7777

static int cfd = -1;
static volatile int connect_rc = 1;
static uint16_t syn_sport;
static uint32_t syn_seq;

/* A SYN from any port of the victim to RACE_PORT. */
static int is_race_syn(const uint8_t *f, size_t n, void *arg)
{
    (void)arg;
    if (get16(f + 12) != 0x0800 || n < 14 + 20 + 20)
        return 0;
    const uint8_t *ip = f + 14;
    size_t ihl = (size_t)(ip[0] & 0x0f) * 4;
    if (ip[9] != 6 || memcmp(ip + 16, fake_ip, 4) != 0)
        return 0;
    const uint8_t *t = ip + ihl;
    if (get16(t + 2) != RACE_PORT || (t[13] & 0x12) != 0x02)
        return 0;
    syn_sport = get16(t);
    syn_seq = get32(t + 4);
    return 1;
}

static void *connector(void *arg)
{
    struct sockaddr_in a;
    (void)arg;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(RACE_PORT);
    memcpy(&a.sin_addr, fake_ip, 4);
    connect_rc = connect(cfd, (struct sockaddr *)&a, sizeof(a));
    return NULL;
}

static void case_connect(void)
{
    uint8_t f[1600];
    pthread_t t;
    cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cfd < 0) {
        result("connect", 0, "socket");
        return;
    }
    pthread_create(&t, NULL, connector, NULL);

    /* its SYN */
    uint16_t cport = 0;
    if (rx_until(f, sizeof(f), 3000, is_race_syn, NULL) > 0)
        cport = syn_sport;
    if (!cport) {
        result("connect", 0, "no SYN from the connect()");
        return;
    }
    /* Accept it; then close the socket the moment the handshake lands,
     * before the connecting thread runs again, and make sockets to take
     * the freed one's memory. */
    send_tcp(0x12, 5000, syn_seq + 1, NULL, 0, RACE_PORT, cport);
    struct pollfd p = { cfd, POLLOUT, 0 };
    for (uint64_t end = now_ms() + 2000; now_ms() < end; )
        if (poll(&p, 1, 0) == 1 && (p.revents & POLLOUT))
            break;
    close(cfd);
    int fresh[8];
    for (int i = 0; i < 8; i++)
        fresh[i] = socket(AF_INET, SOCK_DGRAM, 0);
    pthread_join(t, NULL);

    int bad = 0;
    for (int i = 0; i < 8; i++) {
        struct sockaddr_in pa;
        socklen_t pl = sizeof(pa);
        if (fresh[i] >= 0 && getpeername(fresh[i], (struct sockaddr *)&pa, &pl) == 0)
            bad++;
        if (fresh[i] >= 0)
            close(fresh[i]);
    }
    char what[96];
    snprintf(what, sizeof(what),
             "no socket made after the close has a peer (%d did; connect %d)",
             bad, connect_rc);
    result("connect", bad == 0, what);
}

/* The victim's own ARP for the fake host, heard on every interface: says
 * whether the packet sockets hear the wire at all. */
static void diagnose(void)
{
    uint8_t f[1600];
    int fd = socket(AF_PACKET, SOCK_RAW, 0x0300);
    int u = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a;
    struct sockaddr_ll from;
    socklen_t fl;
    static const struct timespec ms10 = { 0, 10000000 };

    if (fd < 0 || u < 0 || bind_if(fd, 0) != 0) {
        printf("netpeer: diagnose: setup errno %d\n", errno);
        return;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(9);
    memcpy(&a.sin_addr, fake_ip, 4);
    printf("netpeer: diagnose: sendto 10.0.2.99 = %zd (errno %d)\n",
           sendto(u, "x", 1, 0, (struct sockaddr *)&a, sizeof(a)), errno);
    for (int i = 0; i < 100; i++) {
        fl = sizeof(from);
        ssize_t n = recvfrom(fd, f, sizeof(f), 0, (struct sockaddr *)&from, &fl);
        if (n < 0) {
            nanosleep(&ms10, NULL);
            continue;
        }
        printf("netpeer: diagnose: ifindex %d: %zd bytes %02x:%02x:%02x:%02x:%02x:%02x"
               " -> %02x:%02x:%02x:%02x:%02x:%02x type %04x\n",
               (int)from.sll_ifindex, n, f[6], f[7], f[8], f[9], f[10], f[11],
               f[0], f[1], f[2], f[3], f[4], f[5], get16(f + 12));
    }
    close(fd);
    close(u);
}

int main(void)
{
    /* The first probes after boot can go unanswered: try a few rounds. */
    int found = -1;
    for (int round = 0; round < 4 && found != 0; round++)
        found = find_peer();
    if (found != 0) {
        verbose = 1;
        find_peer();
        diagnose();
        printf("netpeer: no peer NIC answered -- boot with the two hub NICs\n");
        printf("netpeer: FAIL\n");
        fflush(stdout);
        _exit(2);
    }
    case_mss();
    case_nd();
    case_connect();
    printf("Result: %d passed, %d failed\n", passed, failed);
    printf("netpeer: %s\n", failed ? "FAIL" : "PASS");
    fflush(stdout);
    _exit(failed ? 1 : 0);
}
