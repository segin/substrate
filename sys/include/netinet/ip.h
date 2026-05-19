/*
 * <netinet/ip.h> — kernel-side IPv4 packet structures.
 */
#ifndef _SYS_NETINET_IP_H
#define _SYS_NETINET_IP_H

#include <stdint.h>

#define ETHERTYPE_IP   0x0800
#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IPV6 0x86DD

#define IPPROTO_ICMP      1
#define IPPROTO_TCP_NUM   6
#define IPPROTO_UDP_NUM  17
#define IPPROTO_ICMPV6   58

#define IP_HLEN_DEFAULT 20

struct iphdr {
    uint8_t  ihl_version;   /* low 4 bits IHL, high 4 bits version */
    uint8_t  tos;
    uint16_t tot_len;       /* network byte order */
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t check;
    uint32_t saddr;         /* network byte order */
    uint32_t daddr;
} __attribute__((packed));

#define IPH_V(h)   ((h)->ihl_version >> 4)
#define IPH_HL(h)  ((h)->ihl_version & 0x0F)

#endif /* _SYS_NETINET_IP_H */
