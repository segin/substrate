/*
 * inet_csum.c — the Internet checksum (RFC 1071), plain and with the IPv4
 * and IPv6 pseudo-headers.
 *
 * Self-contained (standard integer types only) so the host test in
 * tests/sys/host_test_inet_csum.c can build it for either byte order.
 */
#include <net/inet_csum.h>

/* The sums below are accumulated as numbers (octet pairs read big-end
 * first), so the folded result is the checksum in host order.  Callers
 * store it straight into a header field, which must hold network order:
 * swap on a little-endian host only.  An unconditional swap would store
 * every checksum byte-reversed on a big-endian one (RFC 791 3.1 Header
 * Checksum). */
static inline uint16_t csum_to_net(uint16_t host) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap16(host);
#else
    return host;
#endif
}

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
    return csum_to_net((uint16_t)~sum);
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
    return csum_to_net((uint16_t)~sum);
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
    return csum_to_net((uint16_t)~sum);
}
