/*
 * udp.c — UDP datagram protocol (RFC 768), dual-family.
 *
 * Delivery into bound sockets is handled by af_inet.c via
 * afinet_deliver_v{4,6}; UDP only validates header/checksum then
 * passes the payload up.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <fs/procfs.h>
#include <kern/console.h>
#include <net/inet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/netdev.h>

/* UDP-RES-03 / UDP-RES-06: bumped from RX (interrupt or loopback-kthread)
 * and process context alike, so atomically. */
static uint32_t g_udp_stats[UDP_STAT_COUNT];

void udp_stat_inc(enum udp_stat which) {
    if ((unsigned)which >= UDP_STAT_COUNT) return;
    __sync_fetch_and_add(&g_udp_stats[which], 1u);
    if (which == UDP_STAT_RCVBUF_ERRORS || which == UDP_STAT_IN_CSUM_ERRORS ||
        which == UDP_STAT_MALFORMED)
        __sync_fetch_and_add(&g_udp_stats[UDP_STAT_IN_ERRORS], 1u);
}

static uint32_t udp_proc_stat(char *buf, size_t size, void *opaque) {
    (void)opaque;
    const uint32_t *c = g_udp_stats;
    int n = snprintf(buf, size,
        "Udp: InDatagrams NoPorts InErrors OutDatagrams RcvbufErrors "
        "SndbufErrors InCsumErrors\n"
        "Udp: %u %u %u %u %u 0 %u\n",
        c[UDP_STAT_IN_DATAGRAMS], c[UDP_STAT_NO_PORTS], c[UDP_STAT_IN_ERRORS],
        c[UDP_STAT_OUT_DATAGRAMS], c[UDP_STAT_RCVBUF_ERRORS],
        c[UDP_STAT_IN_CSUM_ERRORS]);
    return n < 0 ? 0 : ((size_t)n >= size ? (uint32_t)size - 1 : (uint32_t)n);
}

void udp_stats_init(void) {
    procfs_register_entry("udpstat", udp_proc_stat, NULL);
}

void udp_input(netdev_t *dev, int family,
               const void *saddr, const void *daddr,
               const uint8_t *pkt, size_t len,
               const uint8_t *netpkt, size_t netlen, int for_bcast) {
    /* UDP-RES-06: every drop path is counted. */
    if (len < sizeof(struct udphdr)) { udp_stat_inc(UDP_STAT_MALFORMED); return; }
    const struct udphdr *uh = (const struct udphdr *)pkt;
    uint16_t ulen = __builtin_bswap16(uh->len);
    if (ulen < sizeof(*uh) || ulen > len) { udp_stat_inc(UDP_STAT_MALFORMED); return; }

    /*
     * UDP-03: verify the checksum instead of "we trust it either way".
     * Corrupted datagrams were handed to the socket layer as if intact --
     * silent data corruption for every UDP consumer, with no way for
     * userland to notice.  The checksum covers a pseudo-header of the IP
     * addresses, so it also catches a datagram mis-delivered by a broken
     * relay, not just bit rot.
     *
     * RFC 768: over IPv4 an all-zero field means "sender did not compute
     * one", which is legal and must be accepted.  RFC 8200 8.1: over IPv6
     * it is not optional, so a zero field there is a malformed datagram
     * and is dropped.
     *
     * afinet_deliver_v* expects (saddr, daddr, proto, pkt, len) and walks
     * the bound socket table.
     */
    if (family == 2 /* AF_INET */) {
        uint32_t s = *(const uint32_t *)saddr;
        uint32_t d = *(const uint32_t *)daddr;
        if (uh->check != 0 &&
            inet_csum_pseudo4(s, d, IPPROTO_UDP_NUM, ulen, pkt) != 0) {
            udp_stat_inc(UDP_STAT_IN_CSUM_ERRORS);
            return;
        }
        /*
         * UDP-ICMP-02: RFC 1122 4.1.3.1 -- a datagram for a port nobody is
         * listening on is answered with an ICMP Port Unreachable.  The
         * delivered count was computed and thrown away, so a client probing
         * a closed port waited out its whole timeout instead of learning of
         * the refusal at once.  Never for a broadcast/multicast destination.
         */
        if (afinet_deliver_v4(s, d, IPPROTO_UDP_NUM, pkt, ulen, /*for_dgram=*/1,
                              /*fanout=*/for_bcast)) {
            udp_stat_inc(UDP_STAT_IN_DATAGRAMS);
        } else if (!for_bcast) {
            udp_stat_inc(UDP_STAT_NO_PORTS);
            icmp_port_unreach(dev, netpkt, netlen);
        }
    } else if (family == 10 /* AF_INET6 */) {
        if (uh->check == 0 ||                     /* illegal over IPv6 */
            inet_csum_pseudo6((const uint8_t *)saddr, (const uint8_t *)daddr,
                              IPPROTO_UDP_NUM, ulen, pkt) != 0) {
            udp_stat_inc(UDP_STAT_IN_CSUM_ERRORS);
            return;
        }
        if (afinet_deliver_v6((const uint8_t *)saddr, (const uint8_t *)daddr,
                              IPPROTO_UDP_NUM, pkt, ulen, /*for_dgram=*/1,
                              /*fanout=*/for_bcast)) {
            udp_stat_inc(UDP_STAT_IN_DATAGRAMS);
        } else if (!for_bcast) {
            udp_stat_inc(UDP_STAT_NO_PORTS);
            icmp6_port_unreach(dev, netpkt, netlen);
        }
    }
}
