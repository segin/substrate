/*
 * radvd — minimal IPv6 Router Advertisement daemon.
 *
 * Periodically sends an ICMPv6 RA (type 134) to ff02::1 (all-nodes)
 * on the configured interface, advertising a /64 prefix and the
 * interface's link-local as the default router.  Enough for guest
 * stateless autoconfig to derive a global address.
 *
 *   radvd <iface> <prefix/64>
 *
 * Uses an AF_PACKET socket because we want to fully control the IPv6
 * source address (must be the interface's link-local) and stamp the
 * source link-layer-address option without going through the normal
 * UDP/raw-IP plumbing.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#ifndef AF_PACKET
#define AF_PACKET 17
#endif
#ifndef SOCK_RAW
#define SOCK_RAW 3
#endif

struct sockaddr_ll {
    uint16_t sll_family;
    uint16_t sll_protocol;
    int32_t  sll_ifindex;
    uint16_t sll_hatype;
    uint8_t  sll_pkttype;
    uint8_t  sll_halen;
    uint8_t  sll_addr[8];
};

#define ETH_P_IPV6 0x86DD
#define IPPROTO_ICMPV6 58

static uint16_t inet_csum_pseudo6(const uint8_t saddr[16], const uint8_t daddr[16],
                                  uint8_t proto, uint32_t len, const void *data) {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i += 2) {
        sum += ((uint32_t)saddr[i] << 8) | saddr[i+1];
        sum += ((uint32_t)daddr[i] << 8) | daddr[i+1];
    }
    sum += (len >> 16) & 0xFFFF;
    sum += len & 0xFFFF;
    sum += proto;
    const uint8_t *p = data;
    size_t n = len;
    while (n > 1) { sum += ((uint32_t)p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)__builtin_bswap16((uint16_t)~sum);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: radvd <iface> <prefix/64>\n");
        return 1;
    }
    const char *iface = argv[1];
    char *slash = strchr(argv[2], '/');
    int prefix_len = 64;
    if (slash) { *slash = '\0'; prefix_len = atoi(slash + 1); }
    uint8_t prefix[16];
    if (inet_pton(AF_INET6, argv[2], prefix) != 1) {
        fprintf(stderr, "radvd: bad prefix %s\n", argv[2]);
        return 1;
    }

    /* Look up interface hwaddr + ifindex + ip6 source. */
    int s4 = socket(AF_INET, SOCK_DGRAM, 0);
    struct ifreq r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.ifr_name, iface, sizeof(r.ifr_name));
    if (ioctl(s4, SIOCGIFHWADDR, &r) < 0) { perror("SIOCGIFHWADDR"); return 1; }
    uint8_t mac[6];
    memcpy(mac, r.ifr_hwaddr.sa_data, 6);
    if (ioctl(s4, SIOCGIFINDEX, &r) < 0) { perror("SIOCGIFINDEX"); return 1; }
    int ifindex = r.ifr_ifindex;
    struct in6_ifreq r6;
    memset(&r6, 0, sizeof(r6));
    r6.ifr6_ifindex = ifindex;
    if (ioctl(s4, SIOCGIFADDR_IN6, &r6) < 0) { perror("SIOCGIFADDR_IN6"); return 1; }
    uint8_t src[16];
    memcpy(src, r6.ifr6_addr.s6_addr, 16);
    close(s4);

    int s = socket(AF_PACKET, SOCK_RAW, __builtin_bswap16(0x0003));
    if (s < 0) { perror("socket"); return 1; }
    struct sockaddr_ll sll = { 0 };
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = __builtin_bswap16(0x0003);
    sll.sll_ifindex = ifindex;
    bind(s, (struct sockaddr *)&sll, sizeof(sll));

    fprintf(stdout, "radvd: %s ifindex=%d, prefix %s/%d\n",
            iface, ifindex, argv[2], prefix_len);

    /* All-nodes multicast: ff02::1, MAC 33:33:00:00:00:01. */
    uint8_t dst_ip[16] = { 0xff,0x02,0,0,0,0,0,0,0,0,0,0,0,0,0,1 };
    uint8_t dst_mac[6] = { 0x33,0x33,0x00,0x00,0x00,0x01 };

    for (;;) {
        /* Build the frame:
         *   Eth(14) | IPv6(40) | ICMPv6 RA + options
         * RA fixed body = 8 bytes (type/code/sum + cur-hop/M/O/flags/lifetime/reach/retrans)
         * Source LLAddr option = 8 bytes
         * Prefix option = 32 bytes
         */
        uint8_t pkt[14 + 40 + 8 + 8 + 32];
        memset(pkt, 0, sizeof(pkt));
        memcpy(pkt + 0, dst_mac, 6);
        memcpy(pkt + 6, mac, 6);
        pkt[12] = 0x86; pkt[13] = 0xdd;
        uint8_t *ip6 = pkt + 14;
        ip6[0] = 0x60;                  /* version=6 */
        uint16_t plen = 8 + 8 + 32;
        ip6[4] = plen >> 8; ip6[5] = plen & 0xFF;
        ip6[6] = IPPROTO_ICMPV6;
        ip6[7] = 255;                   /* hop limit MUST be 255 for ND */
        memcpy(ip6 + 8, src, 16);
        memcpy(ip6 + 24, dst_ip, 16);

        uint8_t *icmp = pkt + 14 + 40;
        icmp[0] = 134;                  /* RA */
        icmp[1] = 0;
        icmp[2] = 0; icmp[3] = 0;       /* checksum */
        icmp[4] = 64;                   /* cur hop limit */
        icmp[5] = 0;                    /* M=0 O=0 */
        icmp[6] = 0x07; icmp[7] = 0x08; /* router lifetime = 1800s */
        icmp[8]=icmp[9]=icmp[10]=icmp[11] = 0; /* reachable time */
        icmp[12]=icmp[13]=icmp[14]=icmp[15] = 0; /* retrans time */
        /* Source LLAddr option (type=1, len=1) */
        uint8_t *opt = icmp + 16;
        opt[0] = 1; opt[1] = 1;
        memcpy(opt + 2, mac, 6);
        /* Prefix option (type=3, len=4 = 32 bytes) */
        uint8_t *po = icmp + 24;
        po[0] = 3; po[1] = 4;
        po[2] = (uint8_t)prefix_len;
        po[3] = 0xc0;                   /* L=1 A=1 */
        po[4]=0; po[5]=0; po[6]=0x27; po[7]=0x10; /* valid 9999 */
        po[8]=0; po[9]=0; po[10]=0x27; po[11]=0x10; /* preferred 9999 */
        po[12]=po[13]=po[14]=po[15] = 0; /* reserved */
        memcpy(po + 16, prefix, 16);
        /* zero remaining bits below prefix_len */
        if (prefix_len < 128) {
            int bytes = prefix_len / 8;
            int bits  = prefix_len % 8;
            if (bits) {
                po[16 + bytes] &= (uint8_t)(0xFFu << (8 - bits));
                bytes++;
            }
            for (int i = bytes; i < 16; i++) po[16 + i] = 0;
        }

        uint16_t c = inet_csum_pseudo6(src, dst_ip, IPPROTO_ICMPV6, plen, icmp);
        icmp[2] = c & 0xFF; icmp[3] = c >> 8;

        if (sendto(s, pkt, sizeof(pkt), 0, (struct sockaddr *)&sll, sizeof(sll))
            != (ssize_t)sizeof(pkt)) {
            perror("sendto");
            break;
        }
        sleep(60);
    }
    close(s);
    return 0;
}
