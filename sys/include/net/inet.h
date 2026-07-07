/*
 * sys/net/inet.h — kernel-internal IPv4/IPv6 networking API.
 *
 * Layering (top-down):
 *   AF_INET / AF_INET6 sockets  (af_inet.c)
 *     ⇡ ip_output / ip6_output
 *   IPv4 / IPv6 layer           (inet.c, inet6.c)
 *     ⇡ ip_input / ip6_input    (called from netdev_rx)
 *   ARP / ND6                   (arp.c, nd6.c)
 *     ⇡ ethernet send/recv
 *   netdev_xmit / netdev_rx     (netdev.c — already in place)
 */
#ifndef _SYS_NET_INET_H
#define _SYS_NET_INET_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>   /* socklen_t */
#include <sys/netdev.h>

struct fs_node;

/* -- Ethernet framing helper ---------------------------------------- */

struct ether_hdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;   /* network byte order */
} __attribute__((packed));

#define ETH_HLEN 14

/* -- ARP table & helpers (IPv4) ------------------------------------- */

/* Look up the MAC for an IPv4 address.  ip is in network byte order.
 * Returns 0 and fills mac[6] on hit, -1 on miss (caller may issue a
 * request and retry). */
int  arp_lookup(netdev_t *dev, uint32_t ip, uint8_t mac[6]);

/* Inject a binding into the cache.  Called by arp_input and by
 * outbound paths once a reply arrives. */
void arp_insert(netdev_t *dev, uint32_t ip, const uint8_t mac[6]);

/* Send an ARP request for `target_ip` on `dev`.  Returns 0 on success
 * (frame queued via netdev_xmit). */
int  arp_request(netdev_t *dev, uint32_t target_ip);

/* Process an incoming ARP frame (after the Ethernet header).  Replies
 * to requests directed at us; caches reply contents. */
void arp_input(netdev_t *dev, const uint8_t *arp_pkt, size_t len);

/* -- ND6 cache & helpers (IPv6) ------------------------------------- */

int  nd6_lookup(netdev_t *dev, const uint8_t ip6[16], uint8_t mac[6]);
void nd6_insert(netdev_t *dev, const uint8_t ip6[16], const uint8_t mac[6]);
int  nd6_solicit(netdev_t *dev, const uint8_t target_ip6[16]);

/* -- Ethernet send wrapper ------------------------------------------ */

/* Build an Ethernet header (dst_mac, src=dev->hwaddr, ethertype) and
 * prepend it to `payload`, then xmit.  payload[] is L3-and-up bytes
 * only.  ethertype is in network byte order. */
int  eth_send(netdev_t *dev, const uint8_t dst_mac[6],
              uint16_t ethertype,
              const void *payload, size_t payload_len);

/* -- IPv4 input/output ---------------------------------------------- */

/* Called from netdev_rx when ethertype == 0x0800.  pkt points at the
 * start of the IPv4 header (Ethernet header already stripped). */
void ip4_input(netdev_t *dev, const uint8_t *pkt, size_t len);

/* Build an IPv4 header for `payload` and route it out.  Saddr is
 * picked from dev->ip4_addr (or 0.0.0.0 if unconfigured).  Returns
 * 0 on success. */
int  ip4_output(uint32_t daddr, uint8_t protocol,
                const void *payload, size_t payload_len);

/* -- IPv6 input/output ---------------------------------------------- */

void ip6_input(netdev_t *dev, const uint8_t *pkt, size_t len);
int  ip6_output(const uint8_t daddr[16], uint8_t next_header,
                const void *payload, size_t payload_len);

/* -- L4 demux helpers ----------------------------------------------- */

/* Called by ip4_input after header validation.  payload points at the
 * L4 protocol body (typically struct icmphdr / udphdr).  saddr/daddr
 * are in network byte order. */
void icmp_input(netdev_t *dev, uint32_t saddr, uint32_t daddr,
                const uint8_t *pkt, size_t len);
void icmp6_input(netdev_t *dev, const uint8_t saddr[16], const uint8_t daddr[16],
                 const uint8_t *pkt, size_t len);
void udp_input(netdev_t *dev, int family,
               const void *saddr, const void *daddr,
               const uint8_t *pkt, size_t len);
void tcp_input(uint32_t saddr, uint32_t daddr, const uint8_t *pkt, size_t len);

/* -- Checksums ------------------------------------------------------ */

uint16_t inet_csum(const void *data, size_t len);
uint16_t inet_csum_pseudo4(uint32_t saddr, uint32_t daddr,
                           uint8_t proto, uint16_t len,
                           const void *data);
uint16_t inet_csum_pseudo6(const uint8_t saddr[16], const uint8_t daddr[16],
                           uint8_t proto, uint32_t len, const void *data);

/* -- AF_INET / AF_INET6 socket entry points (af_inet.c, af_inet6.c) - */

int     afinet_socket(int family, int type, int protocol);
int     afinet_bind(int fd, const void *addr, socklen_t len);
int     afinet_connect(int fd, const void *addr, socklen_t len);
ssize_t afinet_sendto(int fd, const void *buf, size_t len, int flags,
                      const void *addr, socklen_t addrlen);
ssize_t afinet_recvfrom(int fd, void *buf, size_t len, int flags,
                        void *addr, socklen_t *addrlen);

/* Upper-half delivery into a bound socket.  Returns 1 if delivered,
 * 0 if no match (RAW or DGRAM sockets registered with matching
 * protocol/port). */
int afinet_deliver_v4(uint32_t saddr, uint32_t daddr,
                      uint8_t protocol,
                      const uint8_t *pkt, size_t len, int for_dgram);
int afinet_deliver_v6(const uint8_t saddr[16], const uint8_t daddr[16],
                      uint8_t protocol,
                      const uint8_t *pkt, size_t len, int for_dgram);

/* -- One-shot init from main.c -------------------------------------- */

void inet_init(void);

/* -- TCP Control Block (PCB) API ------------------------------------ */
struct tcp_pcb;
typedef struct tcp_pcb tcp_pcb_t;

tcp_pcb_t *tcp_alloc(void);
void       tcp_free(tcp_pcb_t *p);
int        tcp_bind(tcp_pcb_t *p, uint32_t laddr, uint16_t lport);
int        tcp_listen(tcp_pcb_t *p, int backlog);
int        tcp_connect(tcp_pcb_t *p, uint32_t raddr, uint16_t rport);
int        tcp_connect_nb(tcp_pcb_t *p, uint32_t raddr, uint16_t rport);
int        tcp_poll(tcp_pcb_t *p, short events, void **wait_chan);
tcp_pcb_t *tcp_accept(tcp_pcb_t *listen_p, int nonblock);
int        tcp_is_listening(const tcp_pcb_t *p);
int        tcp_shutdown_wr(tcp_pcb_t *p);
int        tcp_shutdown_rd(tcp_pcb_t *p);
ssize_t    tcp_send(tcp_pcb_t *p, const void *buf, size_t len);
ssize_t    tcp_send_nb(tcp_pcb_t *p, const void *buf, size_t len);
ssize_t    tcp_recv(tcp_pcb_t *p, void *buf, size_t len);
void       tcp_endpoints(const tcp_pcb_t *p, uint32_t *laddr, uint16_t *lport, uint32_t *raddr, uint16_t *rport);
ssize_t    tcp_recv_nb(tcp_pcb_t *p, void *buf, size_t len);
ssize_t    tcp_peek(tcp_pcb_t *p, void *buf, size_t len);
ssize_t    tcp_peek_nb(tcp_pcb_t *p, void *buf, size_t len);
int        tcp_close(tcp_pcb_t *p);
int        tcp_take_so_error(tcp_pcb_t *p);

/* -- AF_INET socket helper APIs -------------------------------------- */
int     afinet_listen(int fd, int backlog);
int     afinet_accept(int fd, void *addr, socklen_t *addrlen);
size_t  afinet_node_read(struct fs_node *, off_t, size_t, uint8_t *);
int     afinet_shutdown(int fd, int how);
int     afinet_getsockname(int fd, void *addr, socklen_t *addrlen);
int     afinet_getpeername(int fd, void *addr, socklen_t *addrlen);
int     afinet_set_reuseaddr(int fd, int on);
int     afinet_so_error(int fd);
int     afinet_so_type(int fd);
int     afinet_get_reuseaddr(int fd);

/* -- AF_PACKET socket helper APIs ------------------------------------ */
int     afpacket_socket(int type, int protocol);
int     afpacket_bind(int fd, const void *sll, socklen_t len);
ssize_t afpacket_sendto(int fd, const void *buf, size_t len, int flags,
                        const void *sll, socklen_t addrlen);
ssize_t afpacket_recvfrom(int fd, void *buf, size_t len, int flags,
                          void *sll, socklen_t *addrlen);
size_t  afpkt_node_read(struct fs_node *, off_t, size_t, uint8_t *);

#endif /* _SYS_NET_INET_H */
