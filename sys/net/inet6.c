/*
 * inet6.c — IPv6 input/output + ND6 neighbor cache.
 *
 * Mirror of inet.c for the v6 family.  ND6 (NS/NA) replaces ARP at L2.
 * No autoconf or DAD yet — static config only.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <arch/i386/intr.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <net/inet.h>
#include <netinet/icmp.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <sys/netdev.h>

/* ------------------------------------------------------------------ */
/* ND6 cache                                                          */
/* ------------------------------------------------------------------ */

#define ND6_CACHE_SIZE 32

struct nd6_entry {
    uint8_t  ip6[16];   /* zero = unused */
    uint8_t  mac[6];
    uint32_t ifindex;
};

static struct nd6_entry g_nd6_cache[ND6_CACHE_SIZE];
static unsigned          g_nd6_next;

static int ip6_zero(const uint8_t a[16]) {
    for (int i = 0; i < 16; i++) if (a[i]) return 0;
    return 1;
}

int nd6_lookup(netdev_t *dev, const uint8_t ip6[16], uint8_t mac[6]) {
    if (!dev) return -1;
    for (unsigned i = 0; i < ND6_CACHE_SIZE; i++) {
        struct nd6_entry *e = &g_nd6_cache[i];
        if (e->ifindex == dev->ifindex &&
            memcmp(e->ip6, ip6, 16) == 0 &&
            !ip6_zero(e->ip6)) {
            memcpy(mac, e->mac, 6);
            return 0;
        }
    }
    return -1;
}

void nd6_insert(netdev_t *dev, const uint8_t ip6[16], const uint8_t mac[6]) {
    if (!dev || ip6_zero(ip6)) return;
    for (unsigned i = 0; i < ND6_CACHE_SIZE; i++) {
        struct nd6_entry *e = &g_nd6_cache[i];
        if (e->ifindex == dev->ifindex && memcmp(e->ip6, ip6, 16) == 0) {
            memcpy(e->mac, mac, 6);
            return;
        }
    }
    struct nd6_entry *slot = &g_nd6_cache[g_nd6_next % ND6_CACHE_SIZE];
    g_nd6_next++;
    memcpy(slot->ip6, ip6, 16);
    memcpy(slot->mac, mac, 6);
    slot->ifindex = dev->ifindex;
}

/* RFC 4861: NS goes to the solicited-node multicast — ff02::1:ffXX:XXXX
 * (last 24 bits of target).  L2 dst is 33:33:ff:XX:XX:XX. */
int nd6_solicit(netdev_t *dev, const uint8_t target_ip6[16]) {
    if (!dev) return -ENODEV;

    uint8_t pkt[sizeof(struct icmp6_hdr) + 16 + 8];
    memset(pkt, 0, sizeof(pkt));
    struct icmp6_hdr *ih = (struct icmp6_hdr *)pkt;
    ih->type = ND_NEIGHBOR_SOLICIT;
    ih->code = 0;
    ih->data = 0;
    memcpy(pkt + sizeof(*ih), target_ip6, 16);
    /* Source LLAddr option. */
    uint8_t *opt = pkt + sizeof(*ih) + 16;
    opt[0] = 1;        /* type: source LL */
    opt[1] = 1;        /* len: 1 unit (8 bytes) */
    memcpy(opt + 2, dev->hwaddr, 6);

    /* Solicited-node multicast destination. */
    uint8_t dst_ip[16] = {
        0xff,0x02, 0,0, 0,0, 0,0, 0,0,
        0,0x01, 0xff, target_ip6[13], target_ip6[14], target_ip6[15]
    };
    ih->check = inet_csum_pseudo6(dev->ip6_addr, dst_ip,
                                  IPPROTO_ICMPV6, sizeof(pkt), pkt);
    return ip6_output(dst_ip, IPPROTO_ICMPV6, pkt, sizeof(pkt));
}

/* ------------------------------------------------------------------ */
/* IPv6 output                                                        */
/* ------------------------------------------------------------------ */

static int ip6_zero_v(const uint8_t a[16]) { return ip6_zero(a); }

static int ip6_is_loopback(const uint8_t a[16]) {
    for (int i = 0; i < 15; i++) if (a[i]) return 0;
    return a[15] == 1;
}

static netdev_t *route_for_v6(const uint8_t daddr[16], int *via_gw_out) {
    /* ::1 → loopback. */
    if (ip6_is_loopback(daddr)) {
        for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
            if (d->flags & NETDEV_IFF_LOOPBACK) {
                if (via_gw_out) *via_gw_out = 0;
                return d;
            }
        }
    }
    /* Multicast destinations always go out on the first UP non-lo NIC. */
    if (daddr[0] == 0xff) {
        for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
            if (d->flags & NETDEV_IFF_LOOPBACK) continue;
            if (d->flags & NETDEV_IFF_UP) {
                if (via_gw_out) *via_gw_out = 0;
                return d;
            }
        }
        return NULL;
    }
    /* Match on netmask. */
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
        if (!(d->flags & NETDEV_IFF_UP)) continue;
        if (d->flags & NETDEV_IFF_LOOPBACK) continue;
        if (ip6_zero_v(d->ip6_addr)) continue;
        uint8_t mbits = d->ip6_netmask_bits;
        uint8_t full = mbits / 8;
        uint8_t partial = mbits % 8;
        if (memcmp(d->ip6_addr, daddr, full) == 0) {
            if (!partial) {
                if (via_gw_out) *via_gw_out = 0;
                return d;
            }
            uint8_t mask = (uint8_t)(0xFF << (8 - partial));
            if ((d->ip6_addr[full] & mask) == (daddr[full] & mask)) {
                if (via_gw_out) *via_gw_out = 0;
                return d;
            }
        }
    }
    /* Off-subnet: pick first UP NIC with a v6 gateway. */
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
        if (!(d->flags & NETDEV_IFF_UP)) continue;
        if (ip6_zero_v(d->ip6_addr)) continue;
        if (ip6_zero_v(d->ip6_gateway)) continue;
        if (via_gw_out) *via_gw_out = 1;
        return d;
    }
    return NULL;
}

int ip6_output(const uint8_t daddr[16], uint8_t next_header,
               const void *payload, size_t payload_len) {
    int via_gw = 0;
    netdev_t *dev = route_for_v6(daddr, &via_gw);
    if (!dev) return -ENETUNREACH;
    /*
     * Subtract rather than add: payload_len is a size_t, so the old
     * `payload_len + sizeof(struct ip6_hdr) > NETDEV_MTU_MAX` wrapped for
     * payload_len >= 0xFFFFFFD0 and let the memcpy below run off the end of
     * pkt[] and up the kernel stack.  Same defect as the IPv4 path.
     */
    if (payload_len > NETDEV_MTU_MAX - sizeof(struct ip6_hdr)) return -EMSGSIZE;

    uint8_t pkt[NETDEV_MTU_MAX];
    struct ip6_hdr *h = (struct ip6_hdr *)pkt;
    memset(h, 0, sizeof(*h));
    h->vtcfl = __builtin_bswap32(6u << 28);
    h->payload_len = __builtin_bswap16((uint16_t)payload_len);
    h->next_header = next_header;
    h->hop_limit = 64;
    memcpy(h->src, dev->ip6_addr, 16);
    memcpy(h->dst, daddr, 16);
    memcpy(pkt + sizeof(*h), payload, payload_len);

    /* Next-hop MAC. */
    uint8_t mac[6] = { 0 };
    if (dev->flags & NETDEV_IFF_LOOPBACK) {
        /* Loopback: no L2 resolution. */
    } else if (daddr[0] == 0xff) {
        /* IPv6 multicast → 33:33:XX:XX:XX:XX (last 32 bits). */
        mac[0] = 0x33; mac[1] = 0x33;
        mac[2] = daddr[12]; mac[3] = daddr[13];
        mac[4] = daddr[14]; mac[5] = daddr[15];
    } else {
        const uint8_t *nh = via_gw ? dev->ip6_gateway : daddr;
        if (nd6_lookup(dev, nh, mac) != 0) {
            nd6_solicit(dev, nh);
            /*
             * ND-01: the same rule ip4_output got as NET-05, which this
             * path never received.  ip6_output is reachable from hard IRQ
             * context with IF=0 -- rtl_irq -> netdev_rx -> ip6_input ->
             * icmp6_input -> icmp6_handle_echo/ns -> ip6_output -- and the
             * sched_yield() spin below sleeps.  Switching away from an
             * interrupt handler corrupts the interrupted thread's state,
             * and a single ICMPv6 echo from a source not already in the
             * 32-entry ND cache was enough to reach it.  Only wait for the
             * advertisement when interrupts are enabled; otherwise drop the
             * packet after firing the solicitation and let the upper layer
             * resend once the cache is populated.
             */
            if (!intr_enabled())
                return -EHOSTUNREACH;
            for (int i = 0; i < 32; i++) {
                sched_yield();
                if (nd6_lookup(dev, nh, mac) == 0) break;
            }
            if (nd6_lookup(dev, nh, mac) != 0)
                return -EHOSTUNREACH;
        }
    }
    return eth_send(dev, mac, __builtin_bswap16(0x86DD),
                    pkt, sizeof(*h) + payload_len);
}

/* ------------------------------------------------------------------ */
/* IPv6 input                                                         */
/* ------------------------------------------------------------------ */

void ip6_input(netdev_t *dev, const uint8_t *pkt, size_t len) {
    if (!dev || len < sizeof(struct ip6_hdr)) return;
    const struct ip6_hdr *h = (const struct ip6_hdr *)pkt;
    uint32_t vtcfl = __builtin_bswap32(h->vtcfl);
    if ((vtcfl >> 28) != 6) return;
    uint16_t plen = __builtin_bswap16(h->payload_len);
    if (sizeof(*h) + plen > len) return;

    /* Accept if destination is our address, link-local solicited-node
     * multicast, or any-multicast we joined. */
    int for_us = 0;
    if (memcmp(h->dst, dev->ip6_addr, 16) == 0) for_us = 1;
    else if (h->dst[0] == 0xff) for_us = 1;
    if (!for_us) return;

    const uint8_t *l4 = pkt + sizeof(*h);
    size_t l4_len = plen;
    switch (h->next_header) {
        case IPPROTO_ICMPV6:
            icmp6_input(dev, h->src, h->dst, l4, l4_len);
            break;
        case IPPROTO_UDP_NUM:
            udp_input(dev, /*AF_INET6=*/10, h->src, h->dst, l4, l4_len);
            break;
        default:
            break;
    }
    afinet_deliver_v6(h->src, h->dst, h->next_header, pkt, sizeof(*h) + plen, /*for_dgram=*/0);
}
