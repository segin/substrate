/*
 * icmp.c — ICMPv4 and ICMPv6 echo handlers.
 *
 * IPv4: ICMP echo-request → reflect as echo-reply.
 * IPv6: ICMPv6 echo-request → reflect as echo-reply.  ND
 * neighbor-solicit / neighbor-advert handled here too (smaller than a
 * separate file).
 *
 * AF_INET/AF_INET6 RAW sockets that subscribed to IPPROTO_ICMP also
 * get a copy through afinet_deliver_v{4,6}() — that's how userland
 * ping(8) sees the reply.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <arch/i386/intr.h>
#include <kern/console.h>
#include <kern/time.h>
#include <net/inet.h>
#include <netinet/icmp.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <sys/netdev.h>
#include <sys/param.h>

/* ------------------------------------------------------------------ */
/* ICMPv4                                                             */
/* ------------------------------------------------------------------ */

/*
 * UDP-ICMP-01: an ICMP error about a datagram we sent.  The message quotes
 * the offending IP header plus at least 8 octets of its data -- for UDP the
 * whole header, which gives the 4-tuple that locates the socket.  icmp_input
 * used to return on anything but an echo request, so every one of these was
 * dropped unread.  The errno follows the usual BSD/Linux mapping.
 */
static int icmp_unreach_errno(uint8_t code) {
    switch (code) {
    case ICMP_NET_UNREACH: case 6: case 9: case 11: return ENETUNREACH;
    case ICMP_PROT_UNREACH: return ENOPROTOOPT;
    case ICMP_PORT_UNREACH: return ECONNREFUSED;
    case ICMP_FRAG_NEEDED:  return EMSGSIZE;
    default:                return EHOSTUNREACH;
    }
}

static void icmp_error_input(uint8_t type, uint8_t code,
                             const uint8_t *pkt, size_t len) {
    if (len < 8 + sizeof(struct iphdr)) return;
    const struct iphdr *q = (const struct iphdr *)(pkt + 8);
    size_t qhl = IPH_HL(q) * 4;
    if (IPH_V(q) != 4 || qhl < sizeof(*q) || 8 + qhl + 8 > len) return;
    if (q->protocol != IPPROTO_UDP_NUM) return;
    const uint8_t *qudp = pkt + 8 + qhl;
    uint16_t sport = (uint16_t)((qudp[0] << 8) | qudp[1]);
    uint16_t dport = (uint16_t)((qudp[2] << 8) | qudp[3]);
    int err = type == ICMP_DEST_UNREACH  ? icmp_unreach_errno(code)
            : type == ICMP_TIME_EXCEEDED ? EHOSTUNREACH
            :                              EPROTO;
    /* The quoted datagram is one we sent: its source is our end. */
    afinet_icmp_error_v4(q->saddr, sport, q->daddr, dport, err);
}

void icmp_input(netdev_t *dev, uint32_t saddr, uint32_t daddr,
                const uint8_t *pkt, size_t len) {
    if (len < sizeof(struct icmphdr)) return;
    const struct icmphdr *ih = (const struct icmphdr *)pkt;
    /*
     * ICMP-02: verify the received checksum.  It was never checked, so a
     * corrupted echo request was reflected back as a perfectly-formed
     * reply carrying the corrupted payload -- we became a laundering
     * service for bit errors, and the sender saw a valid response for data
     * it never sent.  ICMP's checksum covers the whole message, so this is
     * a straight one's-complement check over len -- and it now guards the
     * error messages below as well, before their quote is trusted.
     */
    if (inet_csum(pkt, len) != 0) return;
    if (ih->type == ICMP_DEST_UNREACH || ih->type == ICMP_TIME_EXCEEDED ||
        ih->type == ICMP_PARAMETERPROB) {
        icmp_error_input(ih->type, ih->code, pkt, len);
        return;
    }
    /* Source Quench is deprecated: RFC 6633 says hosts MUST ignore it. */
    if (ih->type != ICMP_ECHO) return;


    /*
     * ICMP-01: never answer an echo request sent to a broadcast address.
     * daddr used to be discarded outright ("(void)daddr"), so a request
     * addressed to 255.255.255.255 or the subnet broadcast was answered by
     * every host that saw it, with the reply going to the attacker-chosen
     * source -- a classic smurf amplifier, and one that ran inside the NIC
     * ISR at that.  RFC 1122 3.2.2.6 requires silent discard.
     */
    if (dev) {
        uint32_t bcast = (dev->ip4_addr & dev->ip4_netmask) | ~dev->ip4_netmask;
        if (daddr == 0xFFFFFFFFu || daddr == bcast) return;
    }

    /*
     * And never reply to a request that claims an unusable source, since the
     * reply is addressed to it: 0.0.0.0, loopback from off-box, a broadcast
     * address (the reply would itself be broadcast), or multicast.
     */
    {
        uint32_t s = __builtin_bswap32(saddr);
        if (s == 0 ||                       /* "this host" */
            (s >> 24) == 127 ||             /* 127/8 arriving on a NIC */
            (s >> 28) == 0xE ||             /* 224/4 multicast */
            saddr == 0xFFFFFFFFu) return;
    }

    /* Build a reply with type=0 (reply), same id/sequence, same data. */
    uint8_t reply[1500];
    if (len > sizeof(reply)) return;
    memcpy(reply, pkt, len);
    struct icmphdr *rh = (struct icmphdr *)reply;
    rh->type = ICMP_ECHOREPLY;
    rh->code = 0;
    rh->check = 0;
    rh->check = inet_csum(reply, len);
    ip4_output(saddr, IPPROTO_ICMP, reply, len);
}

/* ------------------------------------------------------------------ */
/* ICMP errors (UDP-ICMP-02)                                          */
/* ------------------------------------------------------------------ */

/*
 * RFC 1122 3.2.2: a host SHOULD limit the rate at which it sends ICMP
 * error messages, so a flood of datagrams to closed ports cannot be turned
 * into an equal flood of replies.  One budget covers both families.
 */
#define ICMP_ERR_PER_SEC 10

static int icmp_err_ratelimit_ok(void) {
    static uint64_t window;
    static unsigned sent;
    uint64_t now = get_ticks();
    if (now - window >= HZ) {
        window = now;
        sent = 0;
    }
    return sent++ < ICMP_ERR_PER_SEC;
}

/*
 * ICMP Destination Unreachable, code 3 (port), quoting the invoking IP
 * header and the first 8 octets of its data (RFC 792) -- for UDP, the whole
 * header, which is what lets the sender map the error to its socket.
 *
 * The caller has already excluded broadcast and multicast destinations.
 * RFC 1122 3.2.2 also forbids an error about a datagram whose source does
 * not name a single host, or about a non-initial fragment.  The reply is
 * sent FROM the address the datagram was sent TO, so the sender can match
 * it against its own destination.
 */
void icmp_port_unreach(netdev_t *dev, const uint8_t *ip_pkt, size_t ip_len) {
    if (!ip_pkt || ip_len < sizeof(struct iphdr)) return;
    const struct iphdr *ih = (const struct iphdr *)ip_pkt;
    size_t hlen = IPH_HL(ih) * 4;
    if (hlen < sizeof(*ih) || hlen > ip_len) return;
    if ((__builtin_bswap16(ih->frag_off) & 0x1FFF) != 0) return;

    uint32_t s = __builtin_bswap32(ih->saddr);
    if (s == 0 || ih->saddr == 0xFFFFFFFFu || (s >> 28) == 0xE) return;
    if ((s >> 24) == 127 && !(dev && (dev->flags & NETDEV_IFF_LOOPBACK))) return;
    if (dev && dev->ip4_netmask &&
        ih->saddr == ((dev->ip4_addr & dev->ip4_netmask) | ~dev->ip4_netmask))
        return;
    if (!icmp_err_ratelimit_ok()) return;

    size_t quote = hlen + (ip_len - hlen < 8 ? ip_len - hlen : 8);
    uint8_t msg[8 + 60 + 8];
    memset(msg, 0, 8);
    msg[0] = ICMP_DEST_UNREACH;
    msg[1] = ICMP_PORT_UNREACH;
    memcpy(msg + 8, ip_pkt, quote);
    uint16_t c = inet_csum(msg, 8 + quote);
    memcpy(msg + 2, &c, 2);
    ip4_output_from(ih->daddr, ih->saddr, IPPROTO_ICMP, msg, 8 + quote);
}

/*
 * ICMPv6 Destination Unreachable, code 4 (port unreachable), RFC 4443 3.1:
 * as much of the invoking packet as fits without the error exceeding the
 * IPv6 minimum MTU of 1280.  RFC 4443 2.4(e) forbids errors about packets
 * sent to a multicast address (excluded by the caller) or from one that
 * does not identify a single node.
 */
#define ICMP6_ERR_MAX (1280 - 40)

void icmp6_port_unreach(netdev_t *dev, const uint8_t *ip6_pkt, size_t len) {
    (void)dev;
    if (!ip6_pkt || len < sizeof(struct ip6_hdr)) return;
    const struct ip6_hdr *h = (const struct ip6_hdr *)ip6_pkt;
    static const uint8_t unspec[16];
    if (h->src[0] == 0xff || memcmp(h->src, unspec, 16) == 0) return;
    if (!icmp_err_ratelimit_ok()) return;

    uint8_t saddr[16];
    if (ip6_source_for(h->src, saddr) != 0) return;
    /* Too big for an IRQ stack, so static -- and therefore built and sent
     * with interrupts off: the loopback kthread reaches here with them on,
     * and a NIC's RX interrupt must not re-enter mid-build.  ip6_output()
     * does not sleep with IF=0 (ND-01). */
    static uint8_t msg[ICMP6_ERR_MAX];
    uint32_t f = intr_disable();
    size_t quote = len < sizeof(msg) - 8 ? len : sizeof(msg) - 8;
    memset(msg, 0, 8);
    msg[0] = ICMP6_DST_UNREACH;
    msg[1] = ICMP6_DST_UNREACH_NOPORT;
    memcpy(msg + 8, ip6_pkt, quote);
    uint16_t c = inet_csum_pseudo6(saddr, h->src, IPPROTO_ICMPV6,
                                   (uint32_t)(8 + quote), msg);
    memcpy(msg + 2, &c, 2);
    ip6_output(h->src, IPPROTO_ICMPV6, msg, 8 + quote);
    intr_restore(f);
}

/* ------------------------------------------------------------------ */
/* ICMPv6                                                             */
/* ------------------------------------------------------------------ */

/* ND option type 1 = source link-layer addr, type 2 = target.  Each
 * option is 8 bytes for Ethernet (1 type + 1 len(8B units) + 6 MAC). */
struct nd_opt_lladdr {
    uint8_t type;
    uint8_t len;     /* 1 → 8 bytes */
    uint8_t mac[6];
} __attribute__((packed));

/* IPv6 NS payload begins with icmp6_hdr, then 16-byte target address,
 * then optional source-LLAddr option. */
static void icmp6_handle_ns(netdev_t *dev, const uint8_t saddr[16],
                            const uint8_t *pkt, size_t len) {
    if (len < sizeof(struct icmp6_hdr) + 16) return;
    const uint8_t *target = pkt + sizeof(struct icmp6_hdr);
    /* Reply only if the target is our address. */
    if (memcmp(target, dev->ip6_addr, 16) != 0) return;

    /* Snoop source MAC if option present (RFC 4861 §4.6.1). */
    if (len >= sizeof(struct icmp6_hdr) + 16 + 8) {
        const struct nd_opt_lladdr *opt =
            (const struct nd_opt_lladdr *)(pkt + sizeof(struct icmp6_hdr) + 16);
        if (opt->type == 1 && opt->len == 1)
            nd6_insert(dev, saddr, opt->mac);
    }

    /* Build NA. */
    uint8_t reply[sizeof(struct icmp6_hdr) + 16 + 8];
    memset(reply, 0, sizeof(reply));
    struct icmp6_hdr *rh = (struct icmp6_hdr *)reply;
    rh->type = ND_NEIGHBOR_ADVERT;
    rh->code = 0;
    rh->data = __builtin_bswap32(0x60000000); /* R=0 S=1 O=1 */
    memcpy(reply + sizeof(*rh), target, 16);
    struct nd_opt_lladdr *opt = (struct nd_opt_lladdr *)
        (reply + sizeof(*rh) + 16);
    opt->type = 2;       /* target LLAddr */
    opt->len  = 1;
    memcpy(opt->mac, dev->hwaddr, 6);
    rh->check = inet_csum_pseudo6(dev->ip6_addr, saddr,
                                  IPPROTO_ICMPV6, sizeof(reply), reply);
    ip6_output(saddr, IPPROTO_ICMPV6, reply, sizeof(reply));
}

static void icmp6_handle_na(netdev_t *dev, const uint8_t *pkt, size_t len) {
    if (len < sizeof(struct icmp6_hdr) + 16 + 8) return;
    const struct icmp6_hdr *nh = (const struct icmp6_hdr *)pkt;
    const uint8_t *target = pkt + sizeof(struct icmp6_hdr);
    const struct nd_opt_lladdr *opt =
        (const struct nd_opt_lladdr *)(pkt + sizeof(struct icmp6_hdr) + 16);
    if (opt->type != 2 || opt->len != 1) return;

    /*
     * ND-03: an advertisement may REFRESH a binding we already hold; it may
     * not CREATE one.  This used to call nd6_insert() unconditionally, which
     * creates -- with no solicitation match, no source check and without
     * even looking at the Solicited/Override flags -- so one forged NA
     * naming the router redirected all IPv6 traffic, and 32 of them evicted
     * the whole cache.  The ARP path already restricts creation this way
     * (arp.c: only a reply to a request we sent creates an entry); ND did
     * not.  An address we have never resolved is simply not interesting to
     * us, so dropping the NA costs nothing: if we later need that neighbour
     * we send our own solicitation and take the answer to it.
     *
     * RFC 4861 4.4: the Override flag (0x20000000 in the flags word) says
     * whether the sender may replace a cached link-layer address at all.
     * Honour it rather than overwriting unconditionally.
     */
    uint32_t flags = __builtin_bswap32(nh->data);
    if (!(flags & 0x20000000u)) return;      /* O=0: do not override */

    (void)nd6_update_existing(dev, target, opt->mac);
}

static void icmp6_handle_echo(const uint8_t saddr[16], const uint8_t daddr[16],
                              const uint8_t *pkt, size_t len) {
    (void)daddr;
    uint8_t reply[1500];
    if (len > sizeof(reply)) return;
    memcpy(reply, pkt, len);
    struct icmp6_hdr *rh = (struct icmp6_hdr *)reply;
    rh->type = ICMP6_ECHO_REPLY;
    rh->code = 0;
    rh->check = 0;
    /* checksum is computed in ip6_output via pseudo-header; we set 0
     * here and the caller will replace.  But ip6_output doesn't know
     * about ICMPv6's checksum slot, so we compute it now using daddr
     * as source (we'll send from daddr→saddr). */
    rh->check = inet_csum_pseudo6(daddr, saddr, IPPROTO_ICMPV6,
                                  (uint32_t)len, reply);
    ip6_output(saddr, IPPROTO_ICMPV6, reply, len);
}

void icmp6_input(netdev_t *dev, const uint8_t saddr[16], const uint8_t daddr[16],
                 const uint8_t *pkt, size_t len) {
    if (len < sizeof(struct icmp6_hdr)) return;
    const struct icmp6_hdr *ih = (const struct icmp6_hdr *)pkt;
    switch (ih->type) {
        case ND_NEIGHBOR_SOLICIT:
            icmp6_handle_ns(dev, saddr, pkt, len);
            break;
        case ND_NEIGHBOR_ADVERT:
            icmp6_handle_na(dev, pkt, len);
            break;
        case ICMP6_ECHO_REQUEST:
            icmp6_handle_echo(saddr, daddr, pkt, len);
            break;
        default:
            break;
    }
}
