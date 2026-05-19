/*
 * <netinet/if_arp.h> — ARP packet header (RFC 826).
 */
#ifndef _SYS_NETINET_IF_ARP_H
#define _SYS_NETINET_IF_ARP_H

#include <stdint.h>

#define ARPHRD_ETHER 1
#define ARPOP_REQUEST 1
#define ARPOP_REPLY   2

struct arphdr {
    uint16_t ar_hrd;     /* hardware type */
    uint16_t ar_pro;     /* protocol type (0x0800 for IPv4) */
    uint8_t  ar_hln;     /* hw addr length (6) */
    uint8_t  ar_pln;     /* protocol addr length (4 for IPv4) */
    uint16_t ar_op;      /* opcode */
    uint8_t  ar_sha[6];  /* sender hw */
    uint8_t  ar_spa[4];  /* sender proto */
    uint8_t  ar_tha[6];  /* target hw */
    uint8_t  ar_tpa[4];  /* target proto */
} __attribute__((packed));

#endif
