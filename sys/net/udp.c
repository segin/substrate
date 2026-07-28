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

    /* Checksum is optional for IPv4 (uh->check == 0 means skip),
     * mandatory for IPv6.  We trust it either way and let the socket
     * code accept or drop. */

    /* afinet_deliver_v* expects (saddr, daddr, proto, pkt, len) and
     * walks the bound socket table. */
    if (family == 2 /* AF_INET */) {
        uint32_t s = *(const uint32_t *)saddr;
        uint32_t d = *(const uint32_t *)daddr;
        afinet_deliver_v4(s, d, IPPROTO_UDP_NUM, pkt, ulen, /*for_dgram=*/1);
    } else if (family == 10 /* AF_INET6 */) {
        afinet_deliver_v6((const uint8_t *)saddr, (const uint8_t *)daddr,
                          IPPROTO_UDP_NUM, pkt, ulen, /*for_dgram=*/1);
    }
}
