/*
 * <netinet/udp.h> — UDP datagram header.
 */
#ifndef _SYS_NETINET_UDP_H
#define _SYS_NETINET_UDP_H

#include <stdint.h>

struct udphdr {
    uint16_t source;
    uint16_t dest;
    uint16_t len;
    uint16_t check;
} __attribute__((packed));

#endif
