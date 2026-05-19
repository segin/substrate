/*
 * inet.c — IPv4 input/output + shared helpers (eth_send, checksums,
 * route selection, AF_INET delivery glue).
 *
 * Routing is a single-entry view: each netdev carries its own
 * ip4_addr + netmask + gateway, and we pick the first netdev whose
 * subnet matches the destination (or has a gateway set).  Plenty for
 * a one-NIC test rig; multi-NIC routing comes later.
 */

#include <net/inet.h>
#include <sys/netdev.h>
#include <netinet/ip.h>
#include <netinet/if_arp.h>
#include <netinet/icmp.h>
#include <netinet/udp.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Generic 16-bit one's-complement checksum                           */
/* ------------------------------------------------------------------ */

uint16_t inet_csum(const void *data, size_t len) {
    uint32_t sum = 0;
    const uint8_t *p = (const uint8_t *)data;
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len == 1) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)__builtin_bswap16((uint16_t)~sum);
}

uint16_t inet_csum_pseudo4(uint32_t saddr, uint32_t daddr,
                           uint8_t proto, uint16_t len,
                           const void *data) {
    uint32_t sum = 0;
    const uint8_t *sp = (const uint8_t *)&saddr;
    const uint8_t *dp = (const uint8_t *)&daddr;
    sum += ((uint32_t)sp[0] << 8) | sp[1];
    sum += ((uint32_t)sp[2] << 8) | sp[3];
    sum += ((uint32_t)dp[0] << 8) | dp[1];
    sum += ((uint32_t)dp[2] << 8) | dp[3];
    sum += proto;
    sum += len;
    const uint8_t *p = (const uint8_t *)data;
    size_t n = len;
    while (n > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        n -= 2;
    }
    if (n == 1) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)__builtin_bswap16((uint16_t)~sum);
}

uint16_t inet_csum_pseudo6(const uint8_t saddr[16], const uint8_t daddr[16],
                           uint8_t proto, uint32_t len, const void *data) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i += 2) {
        sum += ((uint32_t)saddr[i] << 8) | saddr[i+1];
        sum += ((uint32_t)daddr[i] << 8) | daddr[i+1];
    }
    sum += (len >> 16) & 0xFFFF;
    sum += len & 0xFFFF;
    sum += proto;
    const uint8_t *p = (const uint8_t *)data;
    size_t n = len;
    while (n > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        n -= 2;
    }
    if (n == 1) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)__builtin_bswap16((uint16_t)~sum);
}

/* ------------------------------------------------------------------ */
/* Ethernet send                                                      */
/* ------------------------------------------------------------------ */

int eth_send(netdev_t *dev, const uint8_t dst_mac[6], uint16_t ethertype,
             const void *payload, size_t payload_len) {
    if (!dev) return -ENODEV;
    if (payload_len > NETDEV_MTU_MAX) return -EMSGSIZE;

    uint8_t frame[NETDEV_MTU_MAX + ETH_HLEN];
    struct ether_hdr *eh = (struct ether_hdr *)frame;
    memcpy(eh->dst, dst_mac, 6);
    memcpy(eh->src, dev->hwaddr, 6);
    eh->ethertype = ethertype;
    memcpy(frame + ETH_HLEN, payload, payload_len);

    size_t total = ETH_HLEN + payload_len;
    if (total < 60) {
        memset(frame + total, 0, 60 - total);
        total = 60;
    }
    return netdev_xmit(dev, frame, total);
}

/* ------------------------------------------------------------------ */
/* Route selection — single entry: first netdev that matches.         */
/* ------------------------------------------------------------------ */

static netdev_t *route_for_v4(uint32_t daddr, int *via_gw_out) {
    /* 127.0.0.0/8 → loopback. */
    if ((daddr & 0xFF) == 127) {
        for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
            if (d->flags & NETDEV_IFF_LOOPBACK) {
                if (via_gw_out) *via_gw_out = 0;
                return d;
            }
        }
    }
    /* "via gateway" if dest not on any local subnet but a netdev has
     * a gateway configured. */
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
        if (!(d->flags & NETDEV_IFF_UP)) continue;
        if (d->flags & NETDEV_IFF_LOOPBACK) continue;
        if (!d->ip4_addr) continue;
        if ((d->ip4_addr & d->ip4_netmask) ==
            (daddr      & d->ip4_netmask)) {
            if (via_gw_out) *via_gw_out = 0;
            return d;
        }
    }
    /* Off-subnet: pick the first UP NIC with a gateway. */
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
        if (!(d->flags & NETDEV_IFF_UP)) continue;
        if (!d->ip4_addr || !d->ip4_gateway) continue;
        if (via_gw_out) *via_gw_out = 1;
        return d;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* IPv4 output                                                        */
/* ------------------------------------------------------------------ */

static uint16_t g_ip_id_counter;

int ip4_output(uint32_t daddr, uint8_t protocol,
               const void *payload, size_t payload_len) {
    int via_gw = 0;
    netdev_t *dev = route_for_v4(daddr, &via_gw);
    if (!dev) return -ENETUNREACH;
    if (payload_len + sizeof(struct iphdr) > NETDEV_MTU_MAX) return -EMSGSIZE;

    uint8_t pkt[NETDEV_MTU_MAX];
    struct iphdr *ih = (struct iphdr *)pkt;
    memset(ih, 0, sizeof(*ih));
    ih->ihl_version = (4 << 4) | 5;
    ih->tos = 0;
    ih->tot_len = __builtin_bswap16((uint16_t)(sizeof(*ih) + payload_len));
    ih->id = __builtin_bswap16(++g_ip_id_counter);
    ih->frag_off = 0;
    ih->ttl = 64;
    ih->protocol = protocol;
    ih->check = 0;
    ih->saddr = dev->ip4_addr;
    ih->daddr = daddr;
    ih->check = inet_csum(ih, sizeof(*ih));
    memcpy(pkt + sizeof(*ih), payload, payload_len);

    /* ARP for the next hop.  Loopback skips ARP entirely. */
    uint8_t mac[6] = { 0 };
    if (!(dev->flags & NETDEV_IFF_LOOPBACK)) {
        uint32_t nexthop = via_gw ? dev->ip4_gateway : daddr;
        if (arp_lookup(dev, nexthop, mac) != 0) {
            arp_request(dev, nexthop);
            for (int i = 0; i < 32; i++) {
                sched_yield();
                if (arp_lookup(dev, nexthop, mac) == 0) break;
            }
            if (arp_lookup(dev, nexthop, mac) != 0)
                return -EHOSTUNREACH;
        }
    }
    return eth_send(dev, mac, __builtin_bswap16(ETHERTYPE_IP),
                    pkt, sizeof(*ih) + payload_len);
}

/* ------------------------------------------------------------------ */
/* IPv4 input                                                         */
/* ------------------------------------------------------------------ */

void ip4_input(netdev_t *dev, const uint8_t *pkt, size_t len) {
    if (!dev || len < sizeof(struct iphdr)) return;
    const struct iphdr *ih = (const struct iphdr *)pkt;
    if (IPH_V(ih) != 4) return;
    size_t hlen = IPH_HL(ih) * 4;
    if (hlen < sizeof(*ih) || hlen > len) return;
    uint16_t tot = __builtin_bswap16(ih->tot_len);
    if (tot > len || tot < hlen) return;

    /* Validate header checksum. */
    if (inet_csum(ih, hlen) != 0) return;

    /* Drop fragments — we don't reassemble yet. */
    if ((__builtin_bswap16(ih->frag_off) & 0x3FFF) != 0) return;

    /* Accept if dst is ours, broadcast, or limited-broadcast. */
    uint32_t bcast = (dev->ip4_addr & dev->ip4_netmask) | ~dev->ip4_netmask;
    if (ih->daddr != dev->ip4_addr &&
        ih->daddr != 0xFFFFFFFFu &&
        ih->daddr != bcast) {
        return;
    }

    const uint8_t *l4 = pkt + hlen;
    size_t l4_len = tot - hlen;
    switch (ih->protocol) {
        case IPPROTO_ICMP:
            icmp_input(dev, ih->saddr, ih->daddr, l4, l4_len);
            break;
        case IPPROTO_UDP_NUM:
            udp_input(dev, /*AF_INET=*/2, &ih->saddr, &ih->daddr, l4, l4_len);
            break;
        case 6 /*IPPROTO_TCP*/:
            {
                extern void tcp_input(uint32_t, uint32_t,
                                      const uint8_t *, size_t);
                tcp_input(ih->saddr, ih->daddr, l4, l4_len);
            }
            break;
        default:
            break;
    }
    /* RAW sockets get a copy regardless of protocol. */
    afinet_deliver_v4(ih->saddr, ih->daddr, ih->protocol, pkt, tot);
}

/* ------------------------------------------------------------------ */
/* netdev_rx upcall hook — called from netdev.c                       */
/* ------------------------------------------------------------------ */

void inet_eth_input(netdev_t *dev, const uint8_t *frame, size_t len);
void inet_eth_input(netdev_t *dev, const uint8_t *frame, size_t len) {
    if (len < ETH_HLEN) return;
    const struct ether_hdr *eh = (const struct ether_hdr *)frame;
    uint16_t et = __builtin_bswap16(eh->ethertype);
    const uint8_t *l3 = frame + ETH_HLEN;
    size_t l3_len = len - ETH_HLEN;
    switch (et) {
        case ETHERTYPE_ARP:
            arp_input(dev, l3, l3_len);
            break;
        case ETHERTYPE_IP:
            ip4_input(dev, l3, l3_len);
            break;
        case ETHERTYPE_IPV6:
            ip6_input(dev, l3, l3_len);
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* One-shot init from main.c                                          */
/*                                                                    */
/* Applies a sane static config to the first NIC: QEMU SLIRP default  */
/* assignment (10.0.2.15/24 via 10.0.2.2, IPv6 link-local from MAC,   */
/* fec0::2 gateway).  Once a userland ifconfig exists, replace the    */
/* hardcoded values with an ioctl.                                    */
/* ------------------------------------------------------------------ */

static inline uint32_t v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
}

void inet_init(void) {
    /* Pick the first non-loopback NIC. */
    netdev_t *dev = NULL;
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
        if (d->flags & NETDEV_IFF_LOOPBACK) continue;
        dev = d;
        break;
    }
    if (!dev) return;

    /* IPv4: 10.0.2.15/24 via 10.0.2.2 (QEMU user-mode defaults). */
    if (!dev->ip4_addr) {
        dev->ip4_addr    = v4(10, 0, 2, 15);
        dev->ip4_netmask = v4(255, 255, 255, 0);
        dev->ip4_gateway = v4(10, 0, 2, 2);
    }

    /* IPv6: pick fec0::3 to match QEMU SLIRP's default guest address.
     * SLIRP won't reply to ND across scopes (link-local source ↔
     * site-local target), so we must come from the same /64. */
    int any = 0;
    for (int i = 0; i < 16; i++) if (dev->ip6_addr[i]) { any = 1; break; }
    if (!any) {
        dev->ip6_addr[0]  = 0xfe; dev->ip6_addr[1]  = 0xc0;
        dev->ip6_addr[15] = 0x03;
        dev->ip6_netmask_bits = 64;
        dev->ip6_gateway[0] = 0xfe; dev->ip6_gateway[1] = 0xc0;
        dev->ip6_gateway[15] = 0x02;
    }

    kprintf("inet: %s configured 10.0.2.15/24 gw 10.0.2.2, "
            "fec0::3/64 gw fec0::2\n",
            dev->name);
}
