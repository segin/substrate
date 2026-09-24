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
    /*
     * TCP-URG-06: the urgent pointer, as an offset from SEG.SEQ.  RFC 793
     * contradicts itself (3.1 says it points to the octet FOLLOWING the
     * urgent data, 3.9 to the last urgent octet) and RFC 1122 4.2.2.4
     * settled on "the last octet".  Deployed stacks -- BSD, and Linux
     * unless tcp_stdurg is set -- use the "octet following" form, and an
     * implementation must agree with its peers to find the right byte, so
     * Substrate uses that form on BOTH sides:
     *   send:    urg_ptr = SND.UP - SEG.SEQ, SND.UP = one past the last
     *            urgent octet (tcp_xmit_raw);
     *   receive: the urgent octet is SEG.SEQ + urg_ptr - 1, with urg_ptr
     *            first clamped to the segment's text length, since it is
     *            wire-controlled (tcp_input_locked / tcp_rx_put).
     */
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
