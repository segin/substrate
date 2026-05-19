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

#include <net/inet.h>
#include <sys/netdev.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/icmp.h>
#include <kern/console.h>
#include <string.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* ICMPv4                                                             */
/* ------------------------------------------------------------------ */

void icmp_input(netdev_t *dev, uint32_t saddr, uint32_t daddr,
                const uint8_t *pkt, size_t len) {
    (void)dev; (void)daddr;
    if (len < sizeof(struct icmphdr)) return;
    const struct icmphdr *ih = (const struct icmphdr *)pkt;
    if (ih->type != ICMP_ECHO) return;

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
    const uint8_t *target = pkt + sizeof(struct icmp6_hdr);
    const struct nd_opt_lladdr *opt =
        (const struct nd_opt_lladdr *)(pkt + sizeof(struct icmp6_hdr) + 16);
    if (opt->type == 2 && opt->len == 1)
        nd6_insert(dev, target, opt->mac);
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
