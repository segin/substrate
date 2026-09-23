/*
 * <netinet/icmp.h> — ICMPv4 + ICMPv6 packet structures.
 */
#ifndef _SYS_NETINET_ICMP_H
#define _SYS_NETINET_ICMP_H

#include <stdint.h>

/* ICMPv4 */
#define ICMP_ECHOREPLY   0
#define ICMP_DEST_UNREACH 3
#define ICMP_ECHO        8

#define ICMP_SOURCE_QUENCH 4
#define ICMP_TIME_EXCEEDED 11
#define ICMP_PARAMETERPROB 12

/* ICMP_DEST_UNREACH codes (RFC 792, RFC 1122 3.2.2.1) */
#define ICMP_NET_UNREACH   0
#define ICMP_HOST_UNREACH  1
#define ICMP_PROT_UNREACH  2
#define ICMP_PORT_UNREACH  3
#define ICMP_FRAG_NEEDED   4

struct icmphdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t check;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed));

/* ICMPv6 */
#define ICMP6_DST_UNREACH         1
#define ICMP6_DST_UNREACH_NOPORT  4   /* RFC 4443 3.1 code 4 */
#define ICMP6_ECHO_REQUEST 128
#define ICMP6_ECHO_REPLY   129
#define ND_ROUTER_SOLICIT  133
#define ND_ROUTER_ADVERT   134
#define ND_NEIGHBOR_SOLICIT 135
#define ND_NEIGHBOR_ADVERT  136

struct icmp6_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t check;
    uint32_t data;       /* echo: id<<16 | seq; ND: target options */
} __attribute__((packed));

#endif /* _SYS_NETINET_ICMP_H */
