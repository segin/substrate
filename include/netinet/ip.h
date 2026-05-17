/*
 * <netinet/ip.h> — IPv4 packet header definitions (userland view).
 *
 * Two flavors are provided so both Linux-style and BSD-style code
 * compile against substrate:
 *
 *   struct iphdr   — Linux layout, bit-field IHL/version.
 *   struct ip      — BSD layout, ip_v/ip_hl bit fields and
 *                    ip_src/ip_dst as struct in_addr.
 *
 * libicmp inside inetutils (callers of ip_hl, ip_src.s_addr, etc.)
 * expects the BSD shape.
 */
#ifndef _NETINET_IP_H
#define _NETINET_IP_H

#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>

#include <endian.h>

/* ----- BSD layout (struct ip) ------------------------------------- */
struct ip {
#if defined(__LITTLE_ENDIAN) || (defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN)
    unsigned int ip_hl:4;
    unsigned int ip_v:4;
#else
    unsigned int ip_v:4;
    unsigned int ip_hl:4;
#endif
    uint8_t       ip_tos;
    n_short       ip_len;
    n_short       ip_id;
    n_short       ip_off;
#define IP_RF      0x8000
#define IP_DF      0x4000
#define IP_MF      0x2000
#define IP_OFFMASK 0x1fff
    uint8_t       ip_ttl;
    uint8_t       ip_p;
    n_short       ip_sum;
    struct in_addr ip_src;
    struct in_addr ip_dst;
};

#define IP_MAXPACKET    65535

/* ----- Linux layout (struct iphdr) -------------------------------- */
#ifndef _SYS_NETINET_IP_H        /* don't fight the kernel header */
struct iphdr {
#if defined(__LITTLE_ENDIAN) || (defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN)
    unsigned int ihl:4;
    unsigned int version:4;
#else
    unsigned int version:4;
    unsigned int ihl:4;
#endif
    uint8_t      tos;
    uint16_t     tot_len;
    uint16_t     id;
    uint16_t     frag_off;
    uint8_t      ttl;
    uint8_t      protocol;
    uint16_t     check;
    uint32_t     saddr;
    uint32_t     daddr;
};
#endif

#endif /* _NETINET_IP_H */
