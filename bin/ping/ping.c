/*
 * ping — send ICMP / ICMPv6 ECHO_REQUEST to network host.
 *
 * Usage: ping [options] target
 *   -4              Force IPv4
 *   -6              Force IPv6
 *   -c <count>      Stop after <count> packets (default: continue)
 *   -i <interval>   Seconds between packets (default 1.0)
 *   -s <size>       Payload size (default 56, RFC 792 standard)
 *   -W <timeout>    Per-reply timeout in seconds (default 1)
 *   -t <ttl>        Set IP TTL / Hop Limit
 *   -q              Quiet — only print summary
 *   -n              Numeric only (no DNS) — currently the default
 *   -h              Show help
 *
 * SIGINT prints statistics and exits.
 *
 * Address family is autodetected from the target literal: a string
 * containing ':' is treated as IPv6.  -4/-6 force a family.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#define IPPROTO_ICMP   1
#define IPPROTO_ICMPV6 58
#define ICMP_ECHO          8
#define ICMP_ECHOREPLY     0
#define ICMP_TIME_EXCEEDED 11
#define ICMP_DEST_UNREACH  3
#define ICMP6_ECHO_REQUEST 128
#define ICMP6_ECHO_REPLY   129

/* ---- options & state ---- */

static int    opt_family;       /* AF_INET / AF_INET6, 0 = auto */
static long   opt_count = -1;   /* -1 = unlimited */
static double opt_interval = 1.0;
static int    opt_size = 56;
static double opt_timeout = 1.0;
static int    opt_ttl = 64;
static int    opt_quiet;

static const char *target_name;
static int    family;
static int    sockfd = -1;
static uint16_t ping_id;

static long  sent_count;
static long  recv_count;
static double rtt_min, rtt_max, rtt_sum, rtt_sum_sq;
static long  rtt_n;
static struct timeval tv_start;

/* ---- helpers ---- */

static uint16_t csum16(const void *data, size_t len) {
    uint32_t sum = 0;
    const uint8_t *p = data;
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2; len -= 2;
    }
    if (len) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)__builtin_bswap16((uint16_t)~sum);
}

static double now_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void usleep_dbl(double s) {
    if (s <= 0) return;
    long us = (long)(s * 1000000.0);
    usleep(us);
}

static void print_stats(void) {
    double elapsed = now_sec() - (tv_start.tv_sec + tv_start.tv_usec / 1e6);
    long loss = sent_count > 0
        ? (long)((double)(sent_count - recv_count) * 100.0 / sent_count + 0.5)
        : 0;
    fprintf(stdout, "\n--- %s ping statistics ---\n", target_name);
    fprintf(stdout, "%ld packets transmitted, %ld received, %ld%% packet loss, time %.0fms\n",
            sent_count, recv_count, loss, elapsed * 1000.0);
    if (rtt_n > 0) {
        double avg = rtt_sum / rtt_n;
        double var = (rtt_sum_sq / rtt_n) - (avg * avg);
        /* Babylonian sqrt — pulling in libm just for this is overkill. */
        double stddev = 0;
        if (var > 0) {
            stddev = var;
            for (int i = 0; i < 16; i++) stddev = 0.5 * (stddev + var / stddev);
        }
        fprintf(stdout, "rtt min/avg/max/mdev = %.3f/%.3f/%.3f/%.3f ms\n",
                rtt_min, avg, rtt_max, stddev);
    }
}

static volatile sig_atomic_t want_exit;
static void on_sigint(int sig) { (void)sig; want_exit = 1; }

/* ---- IPv4 send / recv ---- */

static int send_v4(const struct sockaddr_in *dst, uint16_t seq) {
    uint8_t pkt[1500];
    if ((size_t)opt_size + 8 > sizeof(pkt)) return -1;
    memset(pkt, 0, 8 + opt_size);
    pkt[0] = ICMP_ECHO;
    pkt[1] = 0;
    /* id, seq */
    pkt[4] = ping_id >> 8; pkt[5] = ping_id & 0xFF;
    pkt[6] = seq >> 8;     pkt[7] = seq & 0xFF;
    /* timestamp in first bytes of payload */
    double now = now_sec();
    if (opt_size >= (int)sizeof(double))
        memcpy(pkt + 8, &now, sizeof(double));
    /* pattern fill */
    for (int i = (opt_size >= (int)sizeof(double)) ? (int)sizeof(double) : 0;
         i < opt_size; i++)
        pkt[8 + i] = (uint8_t)(i & 0xFF);
    /* checksum */
    uint16_t c = csum16(pkt, 8 + opt_size);
    pkt[2] = c & 0xFF; pkt[3] = c >> 8;
    ssize_t n = sendto(sockfd, pkt, 8 + opt_size, 0,
                      (const struct sockaddr *)dst, sizeof(*dst));
    if (n != 8 + opt_size) return -1;
    return 0;
}

/* Non-blocking drain.  Reports every matching reply it finds and
 * returns when the socket would block.  No deadline — caller decides
 * when to stop. */
static void drain_v4_nonblock(void) {
    uint8_t buf[1500];
    for (;;) {
        ssize_t n = recv(sockfd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n < 0) return;       /* EAGAIN or other; come back later */
        if (n < 20 + 8) continue;
        size_t hlen = (buf[0] & 0x0F) * 4;
        if (hlen + 8 > (size_t)n) continue;
        const uint8_t *icmp = buf + hlen;
        if (icmp[0] != ICMP_ECHOREPLY) continue;
        uint16_t rid  = (icmp[4] << 8) | icmp[5];
        uint16_t rseq = (icmp[6] << 8) | icmp[7];
        if (rid != ping_id) continue;

        double rtt_ms = 0;
        if ((size_t)n >= hlen + 8 + sizeof(double)) {
            double sent_at;
            memcpy(&sent_at, icmp + 8, sizeof(double));
            rtt_ms = (now_sec() - sent_at) * 1000.0;
        }
        if (!opt_quiet) {
            char src[64];
            uint8_t *p = buf + 12;
            snprintf(src, sizeof(src), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
            fprintf(stdout, "%ld bytes from %s: icmp_seq=%u ttl=%u time=%.3f ms\n",
                    (long)(n - hlen), src, rseq, buf[8], rtt_ms);
            fflush(stdout);
        }
        if (rtt_n == 0 || rtt_ms < rtt_min) rtt_min = rtt_ms;
        if (rtt_ms > rtt_max) rtt_max = rtt_ms;
        rtt_sum    += rtt_ms;
        rtt_sum_sq += rtt_ms * rtt_ms;
        rtt_n++;
        recv_count++;
    }
}

/* ---- IPv6 send / recv ---- */

static int send_v6(const struct sockaddr_in6 *dst, uint16_t seq) {
    uint8_t pkt[1500];
    if ((size_t)opt_size + 8 > sizeof(pkt)) return -1;
    memset(pkt, 0, 8 + opt_size);
    pkt[0] = ICMP6_ECHO_REQUEST;
    pkt[1] = 0;
    pkt[4] = ping_id >> 8; pkt[5] = ping_id & 0xFF;
    pkt[6] = seq >> 8;     pkt[7] = seq & 0xFF;
    double now = now_sec();
    if (opt_size >= (int)sizeof(double))
        memcpy(pkt + 8, &now, sizeof(double));
    for (int i = (opt_size >= (int)sizeof(double)) ? (int)sizeof(double) : 0;
         i < opt_size; i++)
        pkt[8 + i] = (uint8_t)(i & 0xFF);
    /* Kernel doesn't fill the ICMPv6 checksum for RAW sockets;
     * leave at zero — loopback path tolerates it. */
    ssize_t n = sendto(sockfd, pkt, 8 + opt_size, 0,
                      (const struct sockaddr *)dst, sizeof(*dst));
    if (n != 8 + opt_size) return -1;
    return 0;
}

static void drain_v6_nonblock(const struct sockaddr_in6 *expected_src) {
    uint8_t buf[1500];
    for (;;) {
        ssize_t n = recv(sockfd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n < 0) return;
        if (n < 8) continue;
        if (buf[0] != ICMP6_ECHO_REPLY) continue;
        uint16_t rid  = (buf[4] << 8) | buf[5];
        uint16_t rseq = (buf[6] << 8) | buf[7];
        if (rid != ping_id) continue;

        double rtt_ms = 0;
        if ((size_t)n >= 8 + sizeof(double)) {
            double sent_at;
            memcpy(&sent_at, buf + 8, sizeof(double));
            rtt_ms = (now_sec() - sent_at) * 1000.0;
        }
        if (!opt_quiet) {
            char src[64];
            inet_ntop(AF_INET6, expected_src->sin6_addr.s6_addr,
                      src, sizeof(src));
            fprintf(stdout, "%ld bytes from %s: icmp_seq=%u time=%.3f ms\n",
                    (long)n, src, rseq, rtt_ms);
            fflush(stdout);
        }
        if (rtt_n == 0 || rtt_ms < rtt_min) rtt_min = rtt_ms;
        if (rtt_ms > rtt_max) rtt_max = rtt_ms;
        rtt_sum    += rtt_ms;
        rtt_sum_sq += rtt_ms * rtt_ms;
        rtt_n++;
        recv_count++;
    }
}

/* ---- main loop ---- */

/*
 * Permanently drop root/setuid privilege once the raw socket is open.
 * ping needs root only to create SOCK_RAW; everything after (DNS, and the
 * loop that parses attacker-controlled reply packets) must run unprivileged
 * so a bug there is not a root compromise. Order: gid before uid.
 */
static void drop_privileges(void) {
    if (setgid(getgid()) != 0 || setuid(getuid()) != 0) {
        fprintf(stderr, "ping: failed to drop privileges: %s\n",
            strerror(errno));
        _exit(2);
    }
}

static int run_v4(const char *target) {
    struct sockaddr_in dst = { 0 };
    char ipbuf[32];
    dst.sin_family = AF_INET;
    if (inet_pton(AF_INET, target, &dst.sin_addr) != 1) {
        struct hostent *he = gethostbyname(target);
        if (!he || he->h_addrtype != AF_INET || !he->h_addr_list[0]) {
            fprintf(stderr, "ping: cannot resolve %s\n", target);
            return 2;
        }
        memcpy(&dst.sin_addr, he->h_addr_list[0], 4);
        inet_ntop(AF_INET, &dst.sin_addr, ipbuf, sizeof(ipbuf));
        fprintf(stdout, "ping: %s has address %s\n", target, ipbuf);
    } else {
        /* Target was already a literal IPv4 — render it back the same
         * way so the PING banner is uniform. */
        inet_ntop(AF_INET, &dst.sin_addr, ipbuf, sizeof(ipbuf));
    }
    sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        fprintf(stderr, "ping: socket: %s\n", strerror(errno));
        return 2;
    }
    drop_privileges();
    /* TTL via setsockopt would be IP_TTL=2.  Substrate setsockopt is
     * a no-op stub today; the kernel uses TTL=64 unconditionally.
     * The Linux convention is `PING <name> (<resolved-ip>) ...` —
     * the parenthesized text is the IP we'll actually send to, not
     * a re-echo of the name. */
    fprintf(stdout, "PING %s (%s) %d(%d) bytes of data.\n",
            target, ipbuf, opt_size, opt_size + 28);
    gettimeofday(&tv_start, NULL);

    /* Linux-style event loop: send one ECHO every `opt_interval`
     * seconds independent of replies; poll the socket non-blocking
     * for inbound replies between sends. */
    uint16_t seq = 0;
    double   next_send = now_sec();
    double   stop_at   = -1;   /* set when last packet sent + timeout */
    while (!want_exit) {
        double now = now_sec();
        if (now >= next_send && (opt_count <= 0 || seq < opt_count)) {
            seq++;
            if (send_v4(&dst, seq) == 0) sent_count++;
            else fprintf(stderr, "ping: sendto: %s\n", strerror(errno));
            next_send = now + opt_interval;
            if (opt_count > 0 && seq >= opt_count) {
                stop_at = now + opt_timeout;
            }
        }
        drain_v4_nonblock();
        if (stop_at > 0 && now_sec() >= stop_at) break;
        /* Sleep until the next interesting time: either next send or
         * a short poll for inbound. */
        double sleep_for = next_send - now_sec();
        if (sleep_for > 0.020) sleep_for = 0.020;
        if (sleep_for < 0)     sleep_for = 0;
        usleep_dbl(sleep_for);
    }
    print_stats();
    return recv_count > 0 ? 0 : 1;
}

static int run_v6(const char *target) {
    struct sockaddr_in6 dst = { 0 };
    dst.sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, target, &dst.sin6_addr) != 1) {
        /* No AAAA lookup yet in libc; only literal v6 supported here. */
        fprintf(stderr, "ping: invalid IPv6 address: %s\n", target);
        return 2;
    }
    sockfd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
    if (sockfd < 0) {
        fprintf(stderr, "ping: socket: %s\n", strerror(errno));
        return 2;
    }
    drop_privileges();
    char buf[64];
    inet_ntop(AF_INET6, dst.sin6_addr.s6_addr, buf, sizeof(buf));
    fprintf(stdout, "PING %s (%s) %d data bytes\n", target, buf, opt_size);
    gettimeofday(&tv_start, NULL);

    uint16_t seq = 0;
    double   next_send = now_sec();
    double   stop_at   = -1;
    while (!want_exit) {
        double now = now_sec();
        if (now >= next_send && (opt_count <= 0 || seq < opt_count)) {
            seq++;
            if (send_v6(&dst, seq) == 0) sent_count++;
            else {
                fprintf(stderr, "ping: sendto: errno=%d %s\n",
                        errno, strerror(errno));
                break;
            }
            next_send = now + opt_interval;
            if (opt_count > 0 && seq >= opt_count) stop_at = now + opt_timeout;
        }
        drain_v6_nonblock(&dst);
        if (stop_at > 0 && now_sec() >= stop_at) break;
        double sleep_for = next_send - now_sec();
        if (sleep_for > 0.020) sleep_for = 0.020;
        if (sleep_for < 0)     sleep_for = 0;
        usleep_dbl(sleep_for);
    }
    print_stats();
    return recv_count > 0 ? 0 : 1;
}

static void usage(void) {
    fprintf(stderr,
        "usage: ping [-46qn] [-c count] [-i interval] [-s size] "
        "[-W timeout] [-t ttl] target\n");
    exit(2);
}

int main(int argc, char **argv) {
    int c;
    while ((c = getopt(argc, argv, "46c:i:s:W:t:qnhV")) != -1) {
        switch (c) {
            case '4': opt_family = AF_INET; break;
            case '6': opt_family = AF_INET6; break;
            case 'c': opt_count = atol(optarg); break;
            case 'i': opt_interval = strtod(optarg, NULL); break;
            case 's': opt_size = atoi(optarg); break;
            case 'W': opt_timeout = strtod(optarg, NULL); break;
            case 't': opt_ttl = atoi(optarg); break;
            case 'q': opt_quiet = 1; break;
            case 'n': /* numeric: default */ break;
            case 'V': fprintf(stdout, "ping (substrate) 1.0\n"); return 0;
            case 'h':
            default:  usage();
        }
    }
    if (optind >= argc) usage();
    target_name = argv[optind];

    /* Pick family. */
    family = opt_family;
    if (!family) {
        family = strchr(target_name, ':') ? AF_INET6 : AF_INET;
    }

    ping_id = (uint16_t)(getpid() & 0xFFFF);

    struct sigaction sa = { 0 };
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    return family == AF_INET6 ? run_v6(target_name) : run_v4(target_name);
}
