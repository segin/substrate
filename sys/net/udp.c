/*
 * udp.c — UDP datagram protocol (RFC 768), dual-family.
 *
 * Delivery into bound sockets is handled by af_inet.c via
 * afinet_deliver_v{4,6}; UDP only validates header/checksum then
 * passes the payload up.
 */

#include <stddef.h>
#include <string.h>

#include <kern/console.h>
#include <net/inet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/netdev.h>

void udp_input(netdev_t *dev, int family,
               const void *saddr, const void *daddr,
               const uint8_t *pkt, size_t len) {
    (void)dev;
    if (len < sizeof(struct udphdr)) return;
    const struct udphdr *uh = (const struct udphdr *)pkt;
    uint16_t ulen = __builtin_bswap16(uh->len);
    if (ulen < sizeof(*uh) || ulen > len) return;

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
            inet_csum_pseudo4(s, d, IPPROTO_UDP_NUM, ulen, pkt) != 0)
            return;
        afinet_deliver_v4(s, d, IPPROTO_UDP_NUM, pkt, ulen, /*for_dgram=*/1);
    } else if (family == 10 /* AF_INET6 */) {
        if (uh->check == 0) return;               /* illegal over IPv6 */
        if (inet_csum_pseudo6((const uint8_t *)saddr, (const uint8_t *)daddr,
                              IPPROTO_UDP_NUM, ulen, pkt) != 0)
            return;
        afinet_deliver_v6((const uint8_t *)saddr, (const uint8_t *)daddr,
                          IPPROTO_UDP_NUM, pkt, ulen, /*for_dgram=*/1);
    }
}
