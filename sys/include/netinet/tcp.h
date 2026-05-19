/*
 * <netinet/tcp.h> — TCP header (RFC 793).
 */
#ifndef _SYS_NETINET_TCP_H
#define _SYS_NETINET_TCP_H

#include <stdint.h>

struct tcphdr {
    uint16_t source;       /* source port */
    uint16_t dest;         /* destination port */
    uint32_t seq;          /* sequence number */
    uint32_t ack_seq;      /* acknowledgement number */
    uint16_t doff_flags;   /* data offset (high 4) + reserved (3) + NS (1) | flags */
    uint16_t window;
    uint16_t check;
    uint16_t urg_ptr;
} __attribute__((packed));

/* doff_flags interpretation (network byte order): high byte is
 * data-offset(4)+reserved(4); low byte is the flag bits below. */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

#endif
