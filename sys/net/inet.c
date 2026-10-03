/*
 * inet.c — IPv4 input/output + shared helpers (eth_send, checksums,
 * route selection, AF_INET delivery glue).
 *
 * Routing is per interface: each netdev carries a primary ip4_addr +
 * netmask + gateway and up to NETDEV_IP4_ALIASES further addresses, and
 * we pick the first netdev one of whose subnets holds the destination
 * (or, failing that, the first with a gateway).  There is no separate
 * routing table.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <machine/intr.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <net/inet.h>
#include <netinet/icmp.h>
#include <netinet/if_arp.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/lock.h>
#include <sys/netdev.h>
#include <sys/param.h>
#include <sys/random.h>
#include <vm/vm_kmem.h>

/* The Internet checksum lives in inet_csum.c. */

/* ------------------------------------------------------------------ */
/* Ethernet send                                                      */
/* ------------------------------------------------------------------ */

/*
 * Keep the transmit path's MTU-sized buffers OFF the stack.
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
 * The same caveat applies to tcp_lock.
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
    /* And never more than the device can carry -- a frame past
     * the device MTU is a "baby giant" that a conformant switch or peer
     * NIC silently discards. */
    if (dev->mtu && payload_len > dev->mtu) return -EMSGSIZE;

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
/* Route selection: first netdev one of whose subnets matches.        */
/* ------------------------------------------------------------------ */

/*
 * An interface's IPv4 addresses: the primary ip4_addr/ip4_netmask and any
 * aliases added with SIOCAIFADDR (RFC 791 3.2 allows a host several).
 * Entry i of ip4_dev_entry() for i in [0, 1 + ip4_nalias); an entry with a
 * zero address is unused.
 */
static inline void ip4_dev_entry(const netdev_t *d, unsigned i,
                                 uint32_t *addr, uint32_t *mask) {
    if (i == 0) {
        *addr = d->ip4_addr;
        *mask = d->ip4_netmask;
    } else {
        *addr = d->ip4_alias[i - 1].addr;
        *mask = d->ip4_alias[i - 1].mask;
    }
}

int ip4_dev_has_addr(const netdev_t *d, uint32_t a) {
    if (!a) return 0;
    for (unsigned i = 0; i < 1u + d->ip4_nalias; i++) {
        uint32_t addr, mask;
        ip4_dev_entry(d, i, &addr, &mask);
        if (addr == a) return 1;
    }
    return 0;
}

int ip4_dev_is_dbcast(const netdev_t *d, uint32_t a) {
    for (unsigned i = 0; i < 1u + d->ip4_nalias; i++) {
        uint32_t addr, mask;
        ip4_dev_entry(d, i, &addr, &mask);
        if (addr && mask && a == ((addr & mask) | ~mask)) return 1;
    }
    return 0;
}

uint32_t ip4_dev_onlink_addr(const netdev_t *d, uint32_t a) {
    for (unsigned i = 0; i < 1u + d->ip4_nalias; i++) {
        uint32_t addr, mask;
        ip4_dev_entry(d, i, &addr, &mask);
        /* No netmask means no known on-link subnet: masking with 0 would
         * claim every destination. */
        if (addr && mask && (addr & mask) == (a & mask)) return addr;
    }
    return 0;
}

uint32_t ip4_dev_mask_of(const netdev_t *d, uint32_t a) {
    for (unsigned i = 0; i < 1u + d->ip4_nalias; i++) {
        uint32_t addr, mask;
        ip4_dev_entry(d, i, &addr, &mask);
        if (a && addr == a) return mask;
    }
    return 0;
}

/* Is `a` an address of one of our (non-loopback) interfaces? */
static int ip4_is_local_ifaddr(uint32_t a) {
    if (!a) return 0;
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d))
        if (!(d->flags & NETDEV_IFF_LOOPBACK) && ip4_dev_has_addr(d, a))
            return 1;
    return 0;
}

/* Class D, 224/4 (network byte order: the first octet is the
 * low byte). */
static inline int ip4_is_mcast(uint32_t a) {
    return ((a & 0xFF) >> 4) == 0xE;
}

#define IP4_ALLHOSTS 0x010000E0u    /* 224.0.0.1, network byte order */

static netdev_t *ip4_loopback_dev(void) {
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d))
        if (d->flags & NETDEV_IFF_LOOPBACK) return d;
    return NULL;
}

/* Accept a multicast datagram arriving on dev?  224.0.0.1 always
 * (RFC 1122 3.3.7); otherwise only a group joined on dev.  The loopback
 * device carries our own looped-back multicast, so there it is a group
 * joined on ANY interface. */
static int ip4_mc_accept(const netdev_t *dev, uint32_t group) {
    if (dev->flags & NETDEV_IFF_LOOPBACK) {
        for (netdev_t *d = netdev_first(); d; d = netdev_next(d))
            if (netdev_mc_member(d, group)) return 1;
        return 0;
    }
    if (!(dev->flags & NETDEV_IFF_MULTICAST)) return 0;
    return group == IP4_ALLHOSTS || netdev_mc_member(dev, group);
}

/* Is `daddr` a broadcast address on `dev` -- limited, or dev's subnet's
 * directed broadcast? */
static int ip4_is_bcast_on(const netdev_t *dev, uint32_t daddr) {
    if (daddr == 0xFFFFFFFFu) return 1;
    return ip4_dev_is_dbcast(dev, daddr);
}

/* Is `a` a broadcast (limited or any interface's directed) or multicast
 * address -- one that can be bound to receive on but never sent from? */
int ip4_is_group_addr(uint32_t a) {
    if (ip4_is_mcast(a)) return 1;
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d))
        if (ip4_is_bcast_on(d, a)) return 1;
    return a == 0xFFFFFFFFu;
}

/* First UP, non-loopback interface with capability `flag`, preferring one
 * that has an address: a broadcast or group send from an addressed interface
 * carries that address as its source, where an unaddressed one could only
 * send from 0.0.0.0.  The unaddressed one is returned only when no
 * interface is configured, which is the DHCP case. */
static netdev_t *route_first_capable(uint32_t flag) {
    netdev_t *bare = NULL;
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
        if (!(d->flags & NETDEV_IFF_UP) || !(d->flags & flag) ||
            (d->flags & NETDEV_IFF_LOOPBACK))
            continue;
        if (d->ip4_addr)
            return d;
        if (!bare)
            bare = d;
    }
    return bare;
}

static netdev_t *route_for_v4(uint32_t daddr, int *via_gw_out) {
    /*
     * The limited broadcast goes out directly on a broadcast-
     * capable interface.  It matched no subnet below and fell through to
     * the default gateway, so 255.255.255.255 was unicast to the router's
     * MAC and no other host ever saw it.  No address is required: the
     * limited broadcast is what an unconfigured host (DHCP) must use.
     */
    if (daddr == 0xFFFFFFFFu) {
        if (via_gw_out) *via_gw_out = 0;
        return route_first_capable(NETDEV_IFF_BROADCAST);
    }
    /*
     * A group address is on-link, never via the gateway (RFC
     * 1112 6.4).  It used to fall through to the gateway arm -- or return
     * NULL with no gateway, making even 224.0.0.1 ENETUNREACH.  First UP,
     * multicast-capable interface.
     */
    if (ip4_is_mcast(daddr)) {
        if (via_gw_out) *via_gw_out = 0;
        return route_first_capable(NETDEV_IFF_MULTICAST);
    }
    /* 127.0.0.0/8 → loopback.  So is any address of our own --
     * a datagram to the host's own NIC address used to match that NIC's
     * subnet below, go out on the wire, and ARP for ourselves, failing
     * EHOSTUNREACH.  Traffic to a local address never leaves the host. */
    if ((daddr & 0xFF) == 127 || ip4_is_local_ifaddr(daddr)) {
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
        /* On-link when one of the interface's subnets, primary or alias,
         * holds the destination.  (An address with no netmask has no known
         * subnet and matches nothing, so off-link traffic still goes to the
         * gateway instead of being ARPed for directly.) */
        if (ip4_dev_onlink_addr(d, daddr)) {
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
 * The source address a datagram will actually leave with, so a
 * caller can build the pseudo-header checksum before handing the packet to
 * ip4_output().  UDP had no checksum at all -- it wrote uh->check = 0 on
 * every transmit -- because the source address is chosen here by routing,
 * not by the socket, and the send path had no way to ask for it.  Now it
 * can.  Returns 0.0.0.0 when the destination is unroutable; the send will
 * fail with ENETUNREACH a moment later anyway.
 */
/* The source routing picks for `daddr` out of `dev`.  For a local address
 * looped back through lo that is the address itself (as Linux's "local"
 * route does), not 127.0.0.1 -- otherwise a socket talking to our own NIC
 * address sees its peer as 127.0.0.1. */
/* Out of an interface with several addresses, the one on the destination's
 * subnet -- or, off-link, on the gateway's -- so the peer can answer it;
 * otherwise the primary. */
static uint32_t route_src4(const netdev_t *dev, uint32_t daddr) {
    if ((dev->flags & NETDEV_IFF_LOOPBACK) && (daddr & 0xFF) != 127)
        return daddr;
    uint32_t a = ip4_dev_onlink_addr(dev, daddr);
    if (!a && dev->ip4_gateway)
        a = ip4_dev_onlink_addr(dev, dev->ip4_gateway);
    return a ? a : dev->ip4_addr;
}

/* The interface a datagram to `daddr` leaves by.  A group send honours the
 * socket's IP_MULTICAST_IF when that address belongs to a multicast-capable
 * interface; everything else is routed. */
static netdev_t *route_out4(uint32_t daddr, const struct ip4_txopts *o,
                            int *via_gw_out) {
    if (o && o->mcast_if && ip4_is_mcast(daddr)) {
        for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
            if ((d->flags & NETDEV_IFF_MULTICAST) &&
                ip4_dev_has_addr(d, o->mcast_if)) {
                if (via_gw_out) *via_gw_out = 0;
                return d;
            }
        }
    }
    return route_for_v4(daddr, via_gw_out);
}

/* The source a datagram to `daddr` sent with options `o` (NULL: defaults)
 * leaves with.  It comes from the same interface choice ip4_output_opts
 * makes, so a group send through IP_MULTICAST_IF carries that interface's
 * address rather than the routed interface's (RFC 1112 6.1). */
uint32_t ip4_source_for_opts(uint32_t daddr, const struct ip4_txopts *o) {
    int via_gw = 0;
    netdev_t *dev = route_out4(daddr, o, &via_gw);
    return dev ? route_src4(dev, daddr) : 0;
}

uint32_t ip4_source_for(uint32_t daddr) {
    return ip4_source_for_opts(daddr, NULL);
}

/* The MTU of the interface a datagram to daddr would leave by,
 * or 0 when there is no route. */
/*
 * Path MTU Discovery (RFC 1191): the smallest MTU learned for a destination
 * from ICMP "fragmentation needed" messages, below the egress interface's.
 * An entry ages out after IP4_PMTU_TICKS so a path that has grown is
 * rediscovered (RFC 1191 6.3 suggests ten minutes).  The table is small and
 * a full one evicts its oldest entry.
 */
#define IP4_PMTU_SLOTS  16
#define IP4_PMTU_TICKS  ((uint64_t)600 * HZ)
#define IP4_MIN_MTU     68                      /* RFC 791 3.2 */
/* The least path MTU a report may impose, as Linux's min_pmtu: an ICMP
 * message about a UDP datagram cannot be checked against any state, so a
 * forged one could otherwise shrink every later connection to a host to
 * 28-octet segments.  A report below this pins the path here and "locks"
 * it: WANT-mode sockets then send to it without DF, so a real path that
 * small still works through fragmentation by its routers. */
#define IP4_PMTU_FLOOR  552

static struct {
    uint32_t daddr;             /* network byte order; 0 = free */
    uint32_t mtu;
    uint8_t  locked;            /* reported below IP4_PMTU_FLOOR */
    uint64_t born;
} g_pmtu[IP4_PMTU_SLOTS];
static spinlock_t g_pmtu_lock = SPINLOCK_INIT("ip4_pmtu");

/* The learned MTU toward daddr, or 0 when none is current; *locked says
 * whether it is pinned at the floor. */
static uint32_t ip4_pmtu_lookup(uint32_t daddr, int *locked) {
    uint32_t mtu = 0;
    uint64_t now = get_ticks();
    if (locked) *locked = 0;
    unsigned long f = spinlock_acquire_irq(&g_pmtu_lock);
    for (int i = 0; i < IP4_PMTU_SLOTS; i++) {
        if (g_pmtu[i].daddr != daddr)
            continue;
        if (now - g_pmtu[i].born >= IP4_PMTU_TICKS) {
            g_pmtu[i].daddr = 0;                /* expired */
        } else {
            mtu = g_pmtu[i].mtu;
            if (locked) *locked = g_pmtu[i].locked;
        }
        break;
    }
    spinlock_release_irq(&g_pmtu_lock, f);
    return mtu;
}

uint32_t ip4_path_mtu(uint32_t daddr) {
    int via_gw = 0;
    netdev_t *dev = route_for_v4(daddr, &via_gw);
    if (!dev)
        return 0;
    uint32_t mtu = dev->mtu;
    uint32_t learned = ip4_pmtu_lookup(daddr, NULL);
    if (learned && learned < mtu)
        mtu = learned;
    return mtu;
}

/* RFC 1191 7: MTU plateaus, for a router that reports a next-hop MTU of 0
 * (an RFC 792 router): the largest plateau below the datagram it dropped. */
static uint32_t ip4_pmtu_plateau(uint32_t tot_len) {
    static const uint16_t plateau[] = {
        32000, 17914, 8166, 4352, 2002, 1492, 1006, 508, 296, IP4_MIN_MTU,
    };
    for (size_t i = 0; i < sizeof(plateau) / sizeof(plateau[0]); i++)
        if (plateau[i] < tot_len)
            return plateau[i];
    return IP4_MIN_MTU;
}

uint32_t ip4_pmtu_update(uint32_t daddr, uint32_t next_hop_mtu,
                         uint32_t dropped_len) {
    uint32_t mtu = next_hop_mtu ? next_hop_mtu : ip4_pmtu_plateau(dropped_len);
    int locked = 0;
    if (mtu < IP4_PMTU_FLOOR) {
        mtu = IP4_PMTU_FLOOR;
        locked = 1;
    }
    uint32_t cur = ip4_path_mtu(daddr);
    if (!cur || mtu > cur || (mtu == cur && !locked))
        return cur;             /* no route, or not a reduction: ignore */
    uint64_t now = get_ticks();
    unsigned long f = spinlock_acquire_irq(&g_pmtu_lock);
    int slot = -1, oldest = 0;
    for (int i = 0; i < IP4_PMTU_SLOTS; i++) {
        if (g_pmtu[i].daddr == daddr) {
            slot = i;
            break;
        }
        if (slot < 0 && !g_pmtu[i].daddr)
            slot = i;
        if (g_pmtu[i].born < g_pmtu[oldest].born)
            oldest = i;
    }
    if (slot < 0)
        slot = oldest;
    g_pmtu[slot].daddr  = daddr;
    g_pmtu[slot].mtu    = mtu;
    g_pmtu[slot].locked = (uint8_t)locked;
    g_pmtu[slot].born   = now;
    spinlock_release_irq(&g_pmtu_lock, f);
    return mtu;
}

int ip4_output(uint32_t daddr, uint8_t protocol,
               const void *payload, size_t payload_len) {
    return ip4_output_from(0, daddr, protocol, payload, payload_len);
}

/*
 * The source address is the caller's to choose.
 * ip4_output() used to stamp dev->ip4_addr unconditionally, so a transport
 * that summed its pseudo-header over any other source -- a socket bound to a
 * specific address, an RST answering a segment sent to one of our addresses
 * -- shipped a checksum the peer silently rejected, and UDP's bind() address
 * was ignored on transmit altogether.  saddr == 0 keeps the routing choice.
 *
 * RFC 1122 3.2.1.3(g): a 127/8 source must never leave the host, so it is
 * refused on anything but the loopback device.
 */
void ip4_txopts_init(struct ip4_txopts *o) {
    o->ttl = 64;
    o->tos = 0;
    o->mcast_ttl = 1;
    o->mcast_loop = 1;
    o->mcast_if = 0;
    o->df = IP4_PMTUDISC_DONT;
    o->optlen = 0;
}

static size_t ip4_check_options(const uint8_t *pkt, size_t hlen,
                                int *route_pending);

/* The option list is well formed, is at most 40 octets, and is stored in
 * `o` padded with End of Option List to a multiple of 4 (RFC 791 3.1).
 * Validation is the input walker's, so nothing is sent that we would
 * ourselves answer with Parameter Problem.  len 0 clears.  Returns 0 or
 * -EINVAL. */
int ip4_txopts_set_options(struct ip4_txopts *o, const uint8_t *opts,
                           size_t len) {
    if (len > sizeof(o->opts))
        return -EINVAL;
    uint8_t hdr[sizeof(struct iphdr) + sizeof(o->opts)];
    size_t padded = (len + 3) & ~(size_t)3;
    memset(hdr, 0, sizeof(hdr));
    if (len)
        memcpy(hdr + sizeof(struct iphdr), opts, len);
    int route_pending;
    if (padded &&
        ip4_check_options(hdr, sizeof(struct iphdr) + padded, &route_pending))
        return -EINVAL;
    memcpy(o->opts, hdr + sizeof(struct iphdr), padded);
    o->optlen = (uint8_t)padded;
    return 0;
}

int ip4_output_from(uint32_t saddr, uint32_t daddr, uint8_t protocol,
                    const void *payload, size_t payload_len) {
    return ip4_output_opts(saddr, daddr, protocol, payload, payload_len, NULL);
}

int ip4_output_opts(uint32_t saddr, uint32_t daddr, uint8_t protocol,
                    const void *payload, size_t payload_len,
                    const struct ip4_txopts *o) {
    struct ip4_txopts defaults;
    if (!o) {
        ip4_txopts_init(&defaults);
        o = &defaults;
    }
    /* 0/8 means "this network" and is never a destination (RFC 791 3.2;
     * RFC 1122 3.2.1.3(a)).  Routed like any other address it went to the
     * default gateway. */
    if ((daddr & 0xFF) == 0)
        return -EINVAL;
    int via_gw = 0;
    netdev_t *dev = route_out4(daddr, o, &via_gw);
    if (!dev) return -ENETUNREACH;
    /* The header: the fixed 20 octets plus the socket's IP_OPTIONS, which
     * are stored already padded to a multiple of 4 (RFC 791 3.1). */
    size_t hlen = sizeof(struct iphdr) + o->optlen;
    /*
     * Bound the datagram by the egress device's MTU, not by the
     * compile-time NETDEV_MTU_MAX alone.  A 1572-byte UDP payload became a
     * 1614-byte frame on a 1500-byte Ethernet and was reported as sent --
     * silent loss, or success/EMSGSIZE/corruption depending on the NIC
     * model.  We do not fragment (RFC 1122 3.3.3 leaves that optional), so
     * fail the send loudly instead.
     */
    if (dev->mtu && payload_len > dev->mtu - hlen)
        return -EMSGSIZE;
    /* With Don't Fragment set, a datagram larger than the learned path MTU
     * would only be dropped by the router that reported it; refuse it here
     * so the sender learns at once (RFC 1191 5). */
    int set_df = o->df != IP4_PMTUDISC_DONT;
    if (set_df && !(dev->flags & NETDEV_IFF_LOOPBACK)) {
        int locked;
        uint32_t pmtu = ip4_pmtu_lookup(daddr, &locked);
        if (locked && o->df == IP4_PMTUDISC_WANT) {
            set_df = 0;         /* path below the floor: let routers fragment */
        } else if (o->df != IP4_PMTUDISC_PROBE && pmtu &&
                   payload_len > pmtu - hlen) {
            return -EMSGSIZE;
        }
    }
    /* RFC 791 3.3: the source must be one of this host's addresses.  A
     * caller-supplied one is checked against the interfaces as they are
     * now: a socket bound to a broadcast or multicast address, or to an
     * address since removed by SIOCSIFADDR, must not put it on the wire.
     * 127/8 is ours only on loopback. */
    if (saddr == 0)
        saddr = route_src4(dev, daddr);
    else if ((saddr & 0xFF) == 127) {
        if (!(dev->flags & NETDEV_IFF_LOOPBACK))
            return -EINVAL;
    } else if (!ip4_is_local_ifaddr(saddr)) {
        return -EADDRNOTAVAIL;
    }
    /* 0.0.0.0 is a valid source only while the host is learning its own
     * address, and only toward the limited broadcast (RFC 1122 3.2.1.3(a),
     * RFC 2131 4.1).  A group send needs a real source. */
    if (saddr == 0 && ip4_is_mcast(daddr))
        return -EADDRNOTAVAIL;
    /*
     * Bound payload_len by SUBTRACTING from the buffer size rather than
     * adding to the payload length.  payload_len is a size_t, so the old
     * `payload_len + sizeof(struct iphdr) > NETDEV_MTU_MAX` wrapped for
     * payload_len >= 0xFFFFFFEC: the sum came out small, the check passed,
     * and the memcpy below then ran off the end of pkt[] and up the kernel
     * stack.  When written, the SOCK_RAW send path passed the caller's
     * length through unchecked and SOCK_RAW creation was unprivileged.
     * Neither is true now -- raw sockets are root-only and the raw
     * send arms are bounded by afi_max_payload() -- but this
     * check is the last line and stays.
     *
     * NETDEV_MTU_MAX is much larger than the header, options included, so
     * the subtraction cannot underflow.
     */
    if (payload_len > NETDEV_MTU_MAX - hlen) return -EMSGSIZE;

    /* See netbuf_get() -- this used to be pkt[NETDEV_MTU_MAX] on
     * the (interrupt) stack, nested inside eth_send's frame buffer. */
    int heap = 0;
    uint8_t *pkt = netbuf_get(g_irq_pktbuf, &heap);
    if (!pkt) return -ENOMEM;
    struct iphdr *ih = (struct iphdr *)pkt;
    memset(ih, 0, sizeof(*ih));
    ih->ihl_version = (uint8_t)((4 << 4) | (hlen / 4));
    ih->tos = o->tos;                                  /* IP_TOS */
    ih->tot_len = __builtin_bswap16((uint16_t)(hlen + payload_len));
    /* One atomic increment per datagram: a plain ++ is a load and a store,
     * and a send from interrupt context (tcp_input answering with a RST or
     * ACK) landing between them stamps a second datagram with the same
     * Identification (RFC 791 3.2).  A datagram that never leaves the host
     * cannot be fragmented or confused with another host's, so loopback
     * takes none: it used to drain the 16-bit space other destinations
     * need unique within a reassembly lifetime (RFC 6864 4.1). */
    ih->id = (dev->flags & NETDEV_IFF_LOOPBACK) ? 0 : __builtin_bswap16(
        __atomic_add_fetch(&g_ip_id_counter, 1, __ATOMIC_RELAXED));
    ih->frag_off = set_df ? __builtin_bswap16(0x4000) : 0;      /* IP_DF */
    /* RFC 1112 6.1 -- a multicast datagram defaults to TTL 1, so
     * a group send stays on the local link unless the sender asks. */
    ih->ttl = ip4_is_mcast(daddr) ? o->mcast_ttl : o->ttl;   /* IP_TTL */
    ih->protocol = protocol;
    ih->check = 0;
    ih->saddr = saddr;
    ih->daddr = daddr;
    if (o->optlen)
        memcpy(pkt + sizeof(*ih), o->opts, o->optlen);
    ih->check = inet_csum(ih, hlen);
    memcpy(pkt + hlen, payload, payload_len);

    /* ARP for the next hop.  Loopback skips ARP entirely.
     *
     * So does a broadcast -- RFC 1122 3.3.6, it goes out as a
     * link-layer broadcast.  A subnet broadcast used to be ARPed for like a
     * host: the request normally went unanswered and the datagram was
     * dropped, and any on-link host that did answer captured every one of
     * our broadcasts until the entry expired.  (arp.c now refuses to learn
     * such a mapping at all.)  Mirrors inet6.c's multicast mapping. */
    uint8_t mac[6] = { 0 };
    if (!(dev->flags & NETDEV_IFF_LOOPBACK) && ip4_is_bcast_on(dev, daddr)) {
        memset(mac, 0xFF, sizeof(mac));
    } else if (!(dev->flags & NETDEV_IFF_LOOPBACK) && ip4_is_mcast(daddr)) {
        /* RFC 1112 6.4 -- 01:00:5e plus the low 23 bits of the
         * group; no resolution.  (It used to be ARPed like a unicast
         * next hop and leave addressed to the router.) */
        const uint8_t *g = (const uint8_t *)&daddr;
        mac[0] = 0x01; mac[1] = 0x00; mac[2] = 0x5e;
        mac[3] = g[1] & 0x7F; mac[4] = g[2]; mac[5] = g[3];
    } else if (!(dev->flags & NETDEV_IFF_LOOPBACK)) {
        uint32_t nexthop = via_gw ? dev->ip4_gateway : daddr;
        if (arp_lookup(dev, nexthop, mac) != 0) {
            arp_request(dev, nexthop);
            /*
             * ip4_output is reachable from tcp_input()/ip4_input(),
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
                      pkt, hlen + payload_len);
    /* RFC 1112 6.1 -- if this host is itself a member of the
     * group, deliver a copy locally too (the IP_MULTICAST_LOOP default).
     * A NIC does not hear its own transmission, so loop it through lo,
     * whose input path accepts a group joined on any interface. */
    if (ip4_is_mcast(daddr) && o->mcast_loop) {        /* IP_MULTICAST_LOOP */
        netdev_t *lo = ip4_loopback_dev();
        if (lo && lo != dev && ip4_mc_accept(lo, daddr))
            eth_send(lo, mac, __builtin_bswap16(ETHERTYPE_IP),
                     pkt, hlen + payload_len);
    }
    netbuf_put(pkt, heap);
    return rc;
}

/* ------------------------------------------------------------------ */
/* IPv4 input                                                         */
/* ------------------------------------------------------------------ */

static void ip4_deliver(netdev_t *dev, const uint8_t *pkt, size_t tot,
                        size_t hlen, int for_bcast);

/*
 * Reassembly (RFC 791 3.2, RFC 1122 3.3.2).  Every fragment used
 * to be dropped, so no datagram larger than one frame could arrive and the
 * usable UDP length range was 8..1472, not the 8..65507 RFC 768 allows.
 *
 * A small fixed table of datagrams in progress, keyed on (source,
 * destination, identification, protocol).  Each holds a buffer for the
 * largest possible datagram, with room in front for the header, and a
 * bitmap of the 8-octet blocks received so far; overlapping fragments
 * simply overwrite.  The datagram is complete once the last fragment
 * (MF clear) has fixed its length, the offset-0 fragment has supplied the
 * header, and every block below the end is present -- in any arrival
 * order.  It is then delivered from the slot's buffer exactly as an
 * unfragmented datagram would be.
 *
 * Bounds: IP4_REASM_SLOTS datagrams at once, each at most 64 KiB.  An
 * entry expires IP4_REASM_TICKS after its first fragment (RFC 1122
 * suggests 60-120 s; 30 s keeps a stalled sender from pinning a buffer
 * long), swept whenever a fragment arrives.  When the table is full the
 * oldest entry is evicted, so a fragment flood cannot wedge it.  No ICMP
 * Time Exceeded is sent on expiry (RFC 1122 3.3.2 SHOULD): the sweep is
 * lazy, so it would be late anyway.
 */
#define IP4_REASM_SLOTS  8
#define IP4_REASM_TICKS  (30u * HZ)
#define IP4_REASM_HROOM  60u                    /* the largest IP header */
#define IP4_REASM_DATA   (65535u - 20u)         /* the largest payload */
#define IP4_REASM_BLOCKS ((IP4_REASM_DATA + 7u) / 8u)

typedef struct ip4_reasm {
    int       used;
    uint32_t  saddr, daddr;     /* network byte order */
    uint16_t  id;
    uint8_t   proto;
    uint8_t   for_bcast;
    uint8_t   hlen;             /* 0 until the offset-0 fragment arrives */
    uint64_t  born;             /* tick of the first fragment */
    uint32_t  data_len;         /* fixed by the last fragment, else 0 */
    netdev_t *dev;
    uint8_t  *buf;              /* IP4_REASM_HROOM + IP4_REASM_DATA */
    uint8_t   hdr[IP4_REASM_HROOM];
    uint8_t   have[(IP4_REASM_BLOCKS + 7u) / 8u];
} ip4_reasm_t;

static ip4_reasm_t g_reasm[IP4_REASM_SLOTS];
static spinlock_t g_reasm_lock = SPINLOCK_INIT("ip4_reasm");

/* Caller holds g_reasm_lock.  kfree() is IRQ-safe. */
static void ip4_reasm_drop_locked(ip4_reasm_t *r) {
    if (r->buf) kfree(r->buf, IP4_REASM_HROOM + IP4_REASM_DATA);
    r->buf  = NULL;
    r->used = 0;
}

/* A whole datagram arrived with the same (source, destination, protocol,
 * Identification) as a reassembly in progress: that reassembly belongs to an
 * earlier datagram whose ID the sender has since reused, and completing it
 * later with a stray fragment would splice two datagrams together (RFC 791
 * 3.2, Example Reassembly Procedure).  Discard it. */
static void ip4_reasm_flush(const struct iphdr *ih) {
    unsigned long f = spinlock_acquire_irq(&g_reasm_lock);
    for (int i = 0; i < IP4_REASM_SLOTS; i++) {
        ip4_reasm_t *e = &g_reasm[i];
        if (e->used && e->saddr == ih->saddr && e->daddr == ih->daddr &&
            e->id == ih->id && e->proto == ih->protocol)
            ip4_reasm_drop_locked(e);
    }
    spinlock_release_irq(&g_reasm_lock, f);
}

/* Every block below data_len present?  Caller holds g_reasm_lock. */
static int ip4_reasm_complete(const ip4_reasm_t *r) {
    if (!r->hlen || !r->data_len) return 0;
    uint32_t nb = (r->data_len + 7u) / 8u;
    for (uint32_t b = 0; b < nb; b++)
        if (!(r->have[b >> 3] & (1u << (b & 7)))) return 0;
    return 1;
}

static void ip4_reasm_input(netdev_t *dev, const uint8_t *pkt, size_t hlen,
                            size_t tot, int for_bcast) {
    const struct iphdr *ih = (const struct iphdr *)pkt;
    uint16_t fo  = __builtin_bswap16(ih->frag_off);
    uint32_t off = (uint32_t)(fo & 0x1FFF) * 8u;
    int      mf  = (fo & 0x2000) != 0;
    uint32_t len = (uint32_t)(tot - hlen);
    /* Only the last fragment may end off an 8-octet boundary, and nothing
     * may reach past the largest datagram (the "ping of death"). */
    if (len == 0 || (mf && (len & 7u)) || off + len > IP4_REASM_DATA)
        return;

    uint64_t now = get_ticks();
    uint8_t *done = NULL;
    size_t   done_tot = 0, done_hlen = 0;
    int      done_bcast = 0;
    netdev_t *done_dev = NULL;

    unsigned long f = spinlock_acquire_irq(&g_reasm_lock);
    ip4_reasm_t *r = NULL, *slot = NULL, *oldest = NULL;
    for (int i = 0; i < IP4_REASM_SLOTS; i++) {
        ip4_reasm_t *e = &g_reasm[i];
        if (e->used && now - e->born >= IP4_REASM_TICKS)
            ip4_reasm_drop_locked(e);                   /* timed out */
        if (!e->used) {
            if (!slot) slot = e;
            continue;
        }
        if (e->saddr == ih->saddr && e->daddr == ih->daddr &&
            e->id == ih->id && e->proto == ih->protocol)
            r = e;
        if (!oldest || e->born < oldest->born)
            oldest = e;
    }
    if (!r) {
        if (!slot) {
            slot = oldest;                              /* table full */
            ip4_reasm_drop_locked(slot);
        }
        uint8_t *b = (uint8_t *)kmalloc(IP4_REASM_HROOM + IP4_REASM_DATA);
        if (!b) {
            spinlock_release_irq(&g_reasm_lock, f);
            return;
        }
        r = slot;
        memset(r, 0, sizeof(*r));
        r->used      = 1;
        r->saddr     = ih->saddr;
        r->daddr     = ih->daddr;
        r->id        = ih->id;
        r->proto     = ih->protocol;
        r->for_bcast = (uint8_t)for_bcast;
        r->born      = now;
        r->dev       = dev;
        r->buf       = b;
    }

    /* The last fragment fixes the length; anything that contradicts it
     * (a second, different end, or data beyond it) spoils the datagram. */
    if (!mf) {
        if (r->data_len && r->data_len != off + len) goto spoil;
        r->data_len = off + len;
        uint32_t nb = (r->data_len + 7u) / 8u;
        for (uint32_t b = nb; b < IP4_REASM_BLOCKS; b++)
            if (r->have[b >> 3] & (1u << (b & 7))) goto spoil;
    } else if (r->data_len && off + len > r->data_len) {
        goto spoil;
    }
    memcpy(r->buf + IP4_REASM_HROOM + off, pkt + hlen, len);
    for (uint32_t b = off / 8u; b < (off + len + 7u) / 8u; b++)
        r->have[b >> 3] |= (uint8_t)(1u << (b & 7));
    if (off == 0) {
        memcpy(r->hdr, pkt, hlen);
        r->hlen = (uint8_t)hlen;
    }

    if (ip4_reasm_complete(r)) {
        if (r->hlen + r->data_len > 65535u) goto spoil;
        /* The first fragment's header, rewritten as a whole datagram's,
         * goes directly in front of the data. */
        uint8_t *h = r->buf + IP4_REASM_HROOM - r->hlen;
        memcpy(h, r->hdr, r->hlen);
        struct iphdr *nh = (struct iphdr *)h;
        nh->tot_len  = __builtin_bswap16((uint16_t)(r->hlen + r->data_len));
        nh->frag_off = 0;
        nh->check    = 0;
        nh->check    = inet_csum(h, r->hlen);
        done       = r->buf;
        done_tot   = r->hlen + r->data_len;
        done_hlen  = r->hlen;
        done_bcast = r->for_bcast;
        done_dev   = r->dev;
        r->buf  = NULL;                 /* ownership passes to `done` */
        r->used = 0;
    }
    spinlock_release_irq(&g_reasm_lock, f);

    if (done) {
        ip4_deliver(done_dev, done + IP4_REASM_HROOM - done_hlen, done_tot,
                    done_hlen, done_bcast);
        kfree(done, IP4_REASM_HROOM + IP4_REASM_DATA);
    }
    return;

spoil:
    ip4_reasm_drop_locked(r);
    spinlock_release_irq(&g_reasm_lock, f);
}

/* IPv4 option types (RFC 791 3.1). */
#define IPOPT_EOL   0
#define IPOPT_NOP   1
#define IPOPT_RR    7
#define IPOPT_TS    68
#define IPOPT_LSRR  131
#define IPOPT_SSRR  137

/*
 * Walk the option area of a received datagram (RFC 791 3.1, 3.2 Options).
 * End of Option List ends it and No Operation is a single octet; every
 * other option carries a length, which must be at least 2 and must not run
 * past the header.  Record Route and the source routes need a pointer of
 * at least 4, Timestamp at least 5.  Returns 0 when the options are well
 * formed; otherwise the octet offset, from the start of the IP header, of
 * the first bad field -- the Parameter Problem pointer.
 *
 * *route_pending is set when a loose or strict source route still has hops
 * to visit (pointer <= length): the datagram's final destination is further
 * along the route, not this host.
 */
static size_t ip4_check_options(const uint8_t *pkt, size_t hlen,
                                int *route_pending) {
    size_t off = sizeof(struct iphdr);
    *route_pending = 0;
    while (off < hlen) {
        uint8_t type = pkt[off];
        if (type == IPOPT_EOL)
            break;
        if (type == IPOPT_NOP) {
            off++;
            continue;
        }
        if (off + 1 >= hlen)
            return off;                     /* no room for the length */
        uint8_t olen = pkt[off + 1];
        if (olen < 2 || off + olen > hlen)
            return off + 1;
        if (type == IPOPT_RR || type == IPOPT_LSRR || type == IPOPT_SSRR ||
            type == IPOPT_TS) {
            uint8_t min_ptr = type == IPOPT_TS ? 5 : 4;
            if (olen < 3)
                return off + 1;
            if (pkt[off + 2] < min_ptr)
                return off + 2;
            if ((type == IPOPT_LSRR || type == IPOPT_SSRR) &&
                pkt[off + 2] <= olen)
                *route_pending = 1;
        }
        off += olen;
    }
    return 0;
}

/* link_group: the frame carrying the datagram was addressed to a link-layer
 * broadcast or multicast address rather than to this interface. */
static void ip4_input_link(netdev_t *dev, const uint8_t *pkt, size_t len,
                           int link_group) {
    if (!dev || len < sizeof(struct iphdr)) return;
    const struct iphdr *ih = (const struct iphdr *)pkt;
    if (IPH_V(ih) != 4) return;
    size_t hlen = IPH_HL(ih) * 4;
    if (hlen < sizeof(*ih) || hlen > len) return;
    uint16_t tot = __builtin_bswap16(ih->tot_len);
    if (tot > len || tot < hlen) return;

    /* Validate header checksum. */
    if (inet_csum(ih, hlen) != 0) return;

    /*
     * Reject martian source addresses.  Only the destination used to
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
        /* A source equal to a broadcast address of one of this link's
         * subnets is equally bogus. */
        if (ip4_dev_is_dbcast(dev, ih->saddr)) return;
    }

    /* Accept if dst is ours, broadcast, or limited-broadcast.
     *
     * On the loopback device every 127/8 address is ours (RFC
     * 1122 3.2.1.3(g) -- "the internal host loopback address"), not only
     * lo's single configured 127.0.0.1.  route_for_v4() already sends all
     * of 127/8 to lo and the martian filter above already treats it as
     * lo-only, but the exact-address test here dropped 127.0.0.2 et al. */
    int for_bcast = (ih->daddr == 0xFFFFFFFFu ||
                     ip4_dev_is_dbcast(dev, ih->daddr));
    /* And lo carries traffic to our own interface addresses. */
    int for_lo = (dev->flags & NETDEV_IFF_LOOPBACK) &&
                 ((ih->daddr & 0xFF) == 127 || ip4_is_local_ifaddr(ih->daddr));
    /* A class D destination is accepted for a group this host
     * has joined (224.0.0.1 always).  It is treated as a broadcast from here
     * on: TCP discards it, UDP fans it out to every member socket, and no
     * ICMP error is ever sent about it. */
    int for_mcast = ip4_is_mcast(ih->daddr) && ip4_mc_accept(dev, ih->daddr);
    if (for_mcast)
        for_bcast = 1;
    /* RFC 791 3.2: a zero network field "means this network" and appears
     * only in certain ICMP messages; RFC 1122 3.2.1.3(a) allows {0, 0} only
     * as a source.  So 0/8 is never a destination.  And an interface with
     * no address (ip4_addr 0) has no unicast address at all: comparing
     * against its 0 would accept a datagram to 0.0.0.0 as ours, letting
     * any host on that link reach every wildcard-bound service.  It still
     * takes the limited broadcast, which DHCP needs. */
    if ((ih->daddr & 0xFF) == 0)
        return;
    int for_me = ip4_dev_has_addr(dev, ih->daddr);  /* primary or alias */
    if (!for_me && !for_bcast && !for_lo) {
        return;
    }
    /* RFC 1122 3.3.6: a datagram that arrived in a link-layer broadcast or
     * multicast frame is discarded unless its IP destination is itself a
     * broadcast or multicast address.  Taking it as unicast would let one
     * frame to ff:ff:ff:ff:ff:ff reach every host's services at once and
     * draw a reply (TCP RST, ICMP error) from each. */
    if (link_group && !for_bcast)
        return;

    /* Malformed options: discard, and say where (RFC 1122 3.2.2.5) --
     * unless the datagram was a broadcast or multicast, which never draws
     * an ICMP error (RFC 1122 3.2.2). */
    if (hlen > sizeof(struct iphdr)) {
        int route_pending;
        size_t bad = ip4_check_options(pkt, hlen, &route_pending);
        if (bad) {
            if (!for_bcast)
                icmp_param_problem(dev, pkt, tot, (uint8_t)bad);
            return;
        }
        /* A source route with hops left is addressed through us, not to
         * us, and a host does not forward (RFC 1122 3.3.5): delivering it
         * locally would let a sender reach this host's services under an
         * address the route never arrived at. */
        if (route_pending)
            return;
    }

    /* A fragment (MF set or a nonzero offset) goes to
     * reassembly, which delivers the whole datagram when it completes. */
    if ((__builtin_bswap16(ih->frag_off) & 0x3FFF) != 0) {
        ip4_reasm_input(dev, pkt, hlen, tot, for_bcast);
        return;
    }
    ip4_reasm_flush(ih);
    ip4_deliver(dev, pkt, tot, hlen, for_bcast);
}

void ip4_input(netdev_t *dev, const uint8_t *pkt, size_t len) {
    ip4_input_link(dev, pkt, len, 0);
}

/* Hand one whole datagram -- as received, or reassembled -- to its
 * protocol and to RAW sockets. */
static void ip4_deliver(netdev_t *dev, const uint8_t *pkt, size_t tot,
                        size_t hlen, int for_bcast) {
    const struct iphdr *ih = (const struct iphdr *)pkt;
    const uint8_t *l4 = pkt + hlen;
    size_t l4_len = tot - hlen;
    switch (ih->protocol) {
        case IPPROTO_ICMP:
            icmp_input(dev, ih->saddr, ih->daddr, pkt + sizeof(*ih),
                       hlen - sizeof(*ih), l4, l4_len);
            break;
        case IPPROTO_UDP_NUM:
            udp_input(dev, /*AF_INET=*/2, &ih->saddr, &ih->daddr, l4, l4_len,
                      pkt, tot, for_bcast);
            break;
        case 6 /*IPPROTO_TCP*/:
            /*
             * TCP has no broadcast or multicast semantics, and
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
    int raw = afinet_deliver_v4(ih->saddr, ih->daddr, ih->protocol, pkt, tot,
                                /*for_dgram=*/0, /*fanout=*/0);
    /* A protocol the stack does not implement, which no raw socket took
     * either, has no receiver: say so with Protocol Unreachable (RFC 1122
     * 3.2.2.1) -- never about a broadcast or multicast datagram (3.2.2).
     * Reassembled datagrams arrive here whole, so this is sent once. */
    if (raw == 0 && !for_bcast && ih->protocol != IPPROTO_ICMP &&
        ih->protocol != IPPROTO_UDP_NUM && ih->protocol != 6 /*TCP*/)
        icmp_dest_unreach(dev, ICMP_PROT_UNREACH, pkt, tot);
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
    /* A frame to another station's unicast MAC is not ours, even when the
     * NIC hands it up (qemu's virtio-net, or any NIC left promiscuous).
     * Group addresses -- the I/G bit, which covers broadcast, 01:00:5e IPv4
     * multicast and 33:33 IPv6 multicast -- are taken, and remembered so
     * IPv4 can check the datagram's destination against them. */
    int link_group = 0;
    if (!(dev->flags & NETDEV_IFF_LOOPBACK)) {
        link_group = eh->dst[0] & 1;
        if (!link_group && memcmp(eh->dst, dev->hwaddr, 6) != 0)
            return;
    }
    switch (et) {
        case ETHERTYPE_ARP:
            arp_input(dev, l3, l3_len);
            break;
        case ETHERTYPE_IP:
            ip4_input_link(dev, l3, l3_len, link_group);
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
    udp_stats_init();                       /* /proc/udpstat */
    /* Start the Identification counter somewhere unpredictable: from 1 on
     * every boot, datagrams sent shortly after a reboot reused the IDs of
     * ones sent shortly after the previous boot, which a receiver still
     * holding their fragments would splice together (RFC 791 3.2).
     * GRND_INSECURE: this runs during boot, where waiting for the pool to
     * be fully seeded could stall it, and a starting point needs to vary
     * between boots, not to be secret. */
    {
        uint16_t seed;
        if (random_get_bytes_flags(&seed, sizeof(seed), GRND_INSECURE) ==
            (int)sizeof(seed))
            g_ip_id_counter = seed;
    }
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
