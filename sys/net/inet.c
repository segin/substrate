/*
 * inet.c — IPv4 input/output + shared helpers (eth_send, checksums,
 * route selection, AF_INET delivery glue).
 *
 * Routing is a single-entry view: each netdev carries its own
 * ip4_addr + netmask + gateway, and we pick the first netdev whose
 * subnet matches the destination (or has a gateway set).  Plenty for
 * a one-NIC test rig; multi-NIC routing comes later.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <arch/i386/intr.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <net/inet.h>
#include <netinet/icmp.h>
#include <netinet/if_arp.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/netdev.h>
#include <vm/vm_kmem.h>

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

/*
 * STACK-01: keep the transmit path's MTU-sized buffers OFF the stack.
 *
 * The worst chain runs entirely in hard-IRQ context on the 16 KiB interrupt
 * stack: icmp_input's reply[1500] (or tcp_xmit_raw's buf[1480]) calls
 * ip4_output, whose pkt[1600] calls eth_send, whose frame[1614] then calls
 * the driver -- about 4.7 KiB of nested frames before the NIC doorbell, plus
 * whatever the driver itself uses.  That is a quarter of the stack consumed
 * by buffers that never needed to be automatic.
 *
 * Hard-IRQ context on this UP kernel runs with IF=0, so exactly one flow can
 * be inside a given function at a time and a dedicated static per call site
 * is safe.  ip4_output and eth_send NEST, so they need separate ones.
 * Process context can be preempted, so it allocates instead; a failed
 * allocation drops the packet, which is what a transmit path should do
 * under memory pressure anyway.
 *
 * SMP NOTE: when APs start scheduling these statics must become per-CPU.
 * The same caveat applies to tcp_lock (audit TCP-21) and is tracked there.
 */
#define NETBUF_SIZE (NETDEV_MTU_MAX + ETH_HLEN)

static uint8_t g_irq_pktbuf[NETBUF_SIZE];   /* ip4_output / ip6_output */
static uint8_t g_irq_frmbuf[NETBUF_SIZE];   /* eth_send                */

/* Returns a buffer of at least NETBUF_SIZE bytes, or NULL.  `heap` is set
 * when the caller must free it. */
static uint8_t *netbuf_get(uint8_t *irq_slot, int *heap) {
    if (!intr_enabled()) { *heap = 0; return irq_slot; }
    *heap = 1;
    return (uint8_t *)kmalloc(NETBUF_SIZE);
}

static void netbuf_put(uint8_t *b, int heap) {
    if (heap && b) kfree(b, NETBUF_SIZE);
}

int eth_send(netdev_t *dev, const uint8_t dst_mac[6], uint16_t ethertype,
             const void *payload, size_t payload_len) {
    if (!dev) return -ENODEV;
    if (payload_len > NETDEV_MTU_MAX) return -EMSGSIZE;

    int heap = 0;
    uint8_t *frame = netbuf_get(g_irq_frmbuf, &heap);
    if (!frame) return -ENOMEM;
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
    int rc = netdev_xmit(dev, frame, total);
    netbuf_put(frame, heap);
    return rc;
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

/*
 * UDP-03: the source address a datagram will actually leave with, so a
 * caller can build the pseudo-header checksum before handing the packet to
 * ip4_output().  UDP had no checksum at all -- it wrote uh->check = 0 on
 * every transmit -- because the source address is chosen here by routing,
 * not by the socket, and the send path had no way to ask for it.  Now it
 * can.  Returns 0.0.0.0 when the destination is unroutable; the send will
 * fail with ENETUNREACH a moment later anyway.
 */
uint32_t ip4_source_for(uint32_t daddr) {
    int via_gw = 0;
    netdev_t *dev = route_for_v4(daddr, &via_gw);
    return dev ? dev->ip4_addr : 0;
}

int ip4_output(uint32_t daddr, uint8_t protocol,
               const void *payload, size_t payload_len) {
    int via_gw = 0;
    netdev_t *dev = route_for_v4(daddr, &via_gw);
    if (!dev) return -ENETUNREACH;
    /*
     * Bound payload_len by SUBTRACTING from the buffer size rather than
     * adding to the payload length.  payload_len is a size_t, so the old
     * `payload_len + sizeof(struct iphdr) > NETDEV_MTU_MAX` wrapped for
     * payload_len >= 0xFFFFFFEC: the sum came out small, the check passed,
     * and the memcpy below then ran off the end of pkt[] and up the kernel
     * stack.  That was reachable from unprivileged userland, because the
     * SOCK_RAW send path hands this function the caller's length with no
     * validation of its own (af_inet.c: the SOCK_RAW branch, unlike the
     * DGRAM branch beside it, has no AFI_DATA_MAX check) and SOCK_RAW
     * creation is not privileged.
     *
     * NETDEV_MTU_MAX is much larger than the header, so the subtraction
     * cannot underflow and folds to a constant.
     */
    if (payload_len > NETDEV_MTU_MAX - sizeof(struct iphdr)) return -EMSGSIZE;

    /* STACK-01: see netbuf_get() -- this used to be pkt[NETDEV_MTU_MAX] on
     * the (interrupt) stack, nested inside eth_send's frame buffer. */
    int heap = 0;
    uint8_t *pkt = netbuf_get(g_irq_pktbuf, &heap);
    if (!pkt) return -ENOMEM;
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
            /*
             * NET-05: ip4_output is reachable from tcp_input()/ip4_input(),
             * which run in hard IRQ context (netdev RX upcall, IF=0).  The
             * sched_yield() spin below sleeps — switching away from an
             * interrupt handler is illegal and corrupts the interrupted
             * thread's state.  Only wait for the ARP reply when interrupts
             * are enabled (process / kthread context); in interrupt or
             * atomic context, drop the packet after firing the ARP request.
             * The next-hop MAC populates the cache from the reply, and the
             * upper layer (TCP retransmit timer, higher-level retry) resends
             * once it does — a one-RTT delay, never a sleep in IRQ.
             */
            if (!intr_enabled()) {
                netbuf_put(pkt, heap);
                return -EHOSTUNREACH;
            }
            for (int i = 0; i < 32; i++) {
                sched_yield();
                if (arp_lookup(dev, nexthop, mac) == 0) break;
            }
            if (arp_lookup(dev, nexthop, mac) != 0) {
                netbuf_put(pkt, heap);
                return -EHOSTUNREACH;
            }
        }
    }
    int rc = eth_send(dev, mac, __builtin_bswap16(ETHERTYPE_IP),
                      pkt, sizeof(*ih) + payload_len);
    netbuf_put(pkt, heap);
    return rc;
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

    /*
     * IP-02: reject martian source addresses.  Only the destination used to
     * be checked, so a frame arriving on a real NIC claiming saddr =
     * 127.0.0.1 was accepted and handed up -- defeating any userland
     * "the peer is localhost, therefore trusted" check -- and a broadcast
     * source made replies (ICMP echo, TCP RST) route to the whole segment.
     * A loopback source is legitimate only on the loopback device.
     */
    {
        uint32_t s = __builtin_bswap32(ih->saddr);
        if (s == 0 ||                                  /* 0.0.0.0 */
            (s >> 28) == 0xE ||                        /* 224/4 multicast */
            ih->saddr == 0xFFFFFFFFu) {                /* limited broadcast */
            return;
        }
        if ((s >> 24) == 127 && !(dev->flags & NETDEV_IFF_LOOPBACK)) {
            return;                                    /* 127/8 off-box */
        }
        /* A source equal to this link's broadcast address is equally bogus. */
        {
            uint32_t bcast = (dev->ip4_addr & dev->ip4_netmask) |
                             ~dev->ip4_netmask;
            if (dev->ip4_netmask != 0 && ih->saddr == bcast) return;
        }
    }

    /* Accept if dst is ours, broadcast, or limited-broadcast. */
    uint32_t bcast = (dev->ip4_addr & dev->ip4_netmask) | ~dev->ip4_netmask;
    int for_bcast = (ih->daddr == 0xFFFFFFFFu ||
                     (dev->ip4_netmask != 0 && ih->daddr == bcast));
    if (ih->daddr != dev->ip4_addr && !for_bcast) {
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
            /*
             * IP-03: TCP has no broadcast or multicast semantics, and
             * broadcast delivery was not flagged to L4 at all -- so a
             * broadcast segment reached tcp_input, matched no PCB, and every
             * host on the segment emitted a RST at whatever source address
             * the attacker chose.  One frame, N reflected RSTs.  RFC 1122
             * 4.2.3.10 requires a broadcast or multicast segment to be
             * silently discarded.
             */
            if (for_bcast) break;
            tcp_input(ih->saddr, ih->daddr, l4, l4_len);
            break;
        default:
            break;
    }
    /* RAW sockets get a copy regardless of protocol. */
    afinet_deliver_v4(ih->saddr, ih->daddr, ih->protocol, pkt, tot, /*for_dgram=*/0);
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
