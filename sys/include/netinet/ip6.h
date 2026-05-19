/*
 * <netinet/ip6.h> — kernel-side IPv6 packet structures.
 */
#ifndef _SYS_NETINET_IP6_H
#define _SYS_NETINET_IP6_H

#include <stdint.h>

struct ip6_hdr {
    uint32_t vtcfl;          /* version(4)|class(8)|flow(20), network byte order */
    uint16_t payload_len;
    uint8_t  next_header;
    uint8_t  hop_limit;
    uint8_t  src[16];
    uint8_t  dst[16];
} __attribute__((packed));

#define IP6_V(h)  ((((const struct ip6_hdr *)(h))->vtcfl >> 28) & 0xF)

#endif /* _SYS_NETINET_IP6_H */
