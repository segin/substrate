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
/* Refresh an existing binding only; never creates.  ND-03: an unsolicited
 * Neighbor Advertisement may update what we already believe but must not be
 * able to introduce a new neighbour, which is how one forged NA could
 * redirect all IPv6 traffic. */
int  nd6_update_existing(netdev_t *dev, const uint8_t ip6[16],
                         const uint8_t mac[6]);
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
void inet_eth_input(netdev_t *dev, const uint8_t *pkt, size_t len);
void ip4_input(netdev_t *dev, const uint8_t *pkt, size_t len);

/* Build an IPv4 header for `payload` and route it out.  Saddr is
 * picked from dev->ip4_addr (or 0.0.0.0 if unconfigured).  Returns
 * 0 on success. */
int  ip4_output(uint32_t daddr, uint8_t protocol,
                const void *payload, size_t payload_len);
/* ip4_output() with an explicit source address; 0 lets routing choose it
 * (what ip4_output() does).  A transport that computes a pseudo-header
 * checksum must pass the source it summed, so the IP header cannot disagree
 * with it (TCP-HDR-01, UDP-U-02/U-03). */
int  ip4_output_from(uint32_t saddr, uint32_t daddr, uint8_t protocol,
                     const void *payload, size_t payload_len);

/* UDP-API-12: per-socket IPv4 transmit options (IP_TTL, IP_TOS and the
 * IP_MULTICAST_* trio).  ip4_txopts_init() gives the defaults: TTL 64, TOS
 * 0, multicast TTL 1 (RFC 1112 6.1), loopback on, interface by routing. */
struct ip4_txopts {
    uint8_t  ttl;           /* unicast/broadcast TTL */
    uint8_t  tos;
    uint8_t  mcast_ttl;
    uint8_t  mcast_loop;    /* deliver our own group sends locally */
    uint32_t mcast_if;      /* interface address for group sends; 0 = route */
};
void ip4_txopts_init(struct ip4_txopts *o);
/* ip4_output_from() with transmit options; NULL means the defaults. */
int  ip4_output_opts(uint32_t saddr, uint32_t daddr, uint8_t protocol,
                     const void *payload, size_t payload_len,
                     const struct ip4_txopts *o);

/* Source address routing will choose for `daddr` — needed to build a UDP
 * pseudo-header checksum before the packet reaches ip4_output/ip6_output. */
uint32_t ip4_source_for(uint32_t daddr);
/* The same, for a send with transmit options `o` (IP_MULTICAST_IF). */
uint32_t ip4_source_for_opts(uint32_t daddr, const struct ip4_txopts *o);
/* Broadcast or multicast: an address a socket may bind to receive on but
 * that is never a source. */
int ip4_is_group_addr(uint32_t a);
uint32_t ip4_path_mtu(uint32_t daddr);      /* TCP-HDR-04 */
int      ip6_source_for(const uint8_t daddr[16], uint8_t out[16]);

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
/* netpkt/netlen: the whole invoking IP datagram, header included, which an
 * ICMP error must quote.  for_bcast: the destination was a broadcast or
 * multicast address, for which no ICMP error may ever be sent. */
void udp_input(netdev_t *dev, int family,
               const void *saddr, const void *daddr,
               const uint8_t *pkt, size_t len,
               const uint8_t *netpkt, size_t netlen, int for_bcast);

/* UDP-ICMP-02: emit an ICMP Port Unreachable (ICMPv6 Destination
 * Unreachable, code 4) quoting the invoking datagram, subject to the
 * RFC 1122 3.2.2 / RFC 4443 2.4 restrictions and a rate limit. */
void icmp_port_unreach(netdev_t *dev, const uint8_t *ip_pkt, size_t ip_len);
/* ICMP Parameter Problem (type 12, code 0) quoting the invoking datagram,
 * `pointer` naming the offending octet (RFC 792), under the same source
 * restrictions and rate limit as icmp_port_unreach(). */
void icmp_param_problem(netdev_t *dev, const uint8_t *ip_pkt, size_t ip_len,
                        uint8_t pointer);
void icmp6_port_unreach(netdev_t *dev, const uint8_t *ip6_pkt, size_t len);
void tcp_input(uint32_t saddr, uint32_t daddr, const uint8_t *pkt, size_t len);
/* An ICMP error about a segment we sent from laddr:lport to raddr:rport
 * with sequence number seq; hard: a hard error (RFC 1122 4.2.3.9). */
void tcp_icmp_error(uint32_t laddr, uint16_t lport, uint32_t raddr,
                    uint16_t rport, uint32_t seq, int hard, int err);

/* UDP-RES-03 / UDP-RES-06: UDP counters, the Udp: line of Linux's
 * /proc/net/snmp, published as /proc/udpstat.  Every drop used to be
 * silent -- no counter, no log -- so a lossy receiver or a checksum-mangling
 * path could not be diagnosed. */
enum udp_stat {
    UDP_STAT_IN_DATAGRAMS,      /* delivered to a socket */
    UDP_STAT_NO_PORTS,          /* unicast, no socket on the port */
    UDP_STAT_IN_ERRORS,         /* every receive-side drop below, together */
    UDP_STAT_OUT_DATAGRAMS,     /* sent */
    UDP_STAT_RCVBUF_ERRORS,     /* receive queue full (SO_RCVBUF) */
    UDP_STAT_IN_CSUM_ERRORS,    /* checksum failed, or zero over IPv6 */
    UDP_STAT_MALFORMED,         /* too short, or a bad Length field */
    UDP_STAT_COUNT
};
void udp_stat_inc(enum udp_stat which);
void udp_stats_init(void);   /* registers /proc/udpstat */

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
/* UDP-API-02: as afinet_sendto(), but `kbuf` is kernel memory (the
 * sendmsg() gather buffer) and is not copied in. */
ssize_t afinet_sendto_kbuf(int fd, const void *kbuf, size_t len, int flags,
                           const void *addr, socklen_t addrlen);
ssize_t afinet_recvfrom(int fd, void *buf, size_t len, int flags,
                        void *addr, socklen_t *addrlen);
/* UDP-API-11: where the datagram a receive returned was addressed -- the
 * IP_PKTINFO data.  valid is 0 when no datagram was taken (or the socket
 * is not an IPv4 datagram socket).  Addresses in network byte order. */
struct afi_rxinfo {
    int      valid;
    uint32_t ifindex;       /* ipi_ifindex */
    uint32_t spec_dst;      /* ipi_spec_dst: the local address to reply from */
    uint32_t addr;          /* ipi_addr: the header's destination */
};
ssize_t afinet_recvfrom_rx(int fd, void *buf, size_t len, int flags,
                           void *addr, socklen_t *addrlen,
                           struct afi_rxinfo *rx);
/* UDP-API-11: has IP_PKTINFO been enabled on this AF_INET socket? */
int     afinet_pktinfo_on(int fd);

/* Upper-half delivery into a bound socket.  Returns 1 if delivered,
 * 0 if no match (RAW or DGRAM sockets registered with matching
 * protocol/port).  fanout (datagram path only): the destination was a
 * broadcast or multicast address, so every matching socket gets a copy
 * rather than only the best match (UDP-IP-08). */
int afinet_deliver_v4(uint32_t saddr, uint32_t daddr,
                      uint8_t protocol,
                      const uint8_t *pkt, size_t len, int for_dgram,
                      int fanout);
/* UDP-ICMP-01: an ICMP error arrived about a UDP datagram we sent from
 * laddr:lport to raddr:rport.  Latch err on the connected socket for that
 * 4-tuple, if any, and wake it.  All in network byte order except the
 * ports and err. */
void afinet_icmp_error_v4(uint32_t laddr, uint16_t lport,
                          uint32_t raddr, uint16_t rport, int err);
int afinet_deliver_v6(const uint8_t saddr[16], const uint8_t daddr[16],
                      uint8_t protocol,
                      const uint8_t *pkt, size_t len, int for_dgram,
                      int fanout);

/* -- One-shot init from main.c -------------------------------------- */

void inet_init(void);
void loopback_init(void);
void rtl8139_init(void);
void e1000_init(void);
void r8168_init(void);

/* -- TCP Control Block (PCB) API ------------------------------------ */
struct tcp_pcb;
typedef struct tcp_pcb tcp_pcb_t;

tcp_pcb_t *tcp_alloc(void);
void       tcp_free(tcp_pcb_t *p);
int        tcp_bind(tcp_pcb_t *p, uint32_t laddr, uint16_t lport, int reuseaddr);   /* TCP-API-04 */
int        tcp_listen(tcp_pcb_t *p, int backlog);
int        tcp_connect(tcp_pcb_t *p, uint32_t raddr, uint16_t rport);
int        tcp_connect_nb(tcp_pcb_t *p, uint32_t raddr, uint16_t rport);
int        tcp_poll(tcp_pcb_t *p, short events, void **wait_chan);
tcp_pcb_t *tcp_accept(tcp_pcb_t *listen_p, int nonblock);
int        tcp_is_listening(const tcp_pcb_t *p);
int        tcp_is_synchronized(const tcp_pcb_t *p);             /* TCP-API-22 */
int        tcp_has_connection(const tcp_pcb_t *p);
int        tcp_shutdown_wr(tcp_pcb_t *p);
int        tcp_shutdown_rd(tcp_pcb_t *p);
ssize_t    tcp_send(tcp_pcb_t *p, const void *buf, size_t len);
/* UDP-API-04: the blocking calls with an absolute tick deadline (SO_SNDTIMEO
 * / SO_RCVTIMEO); 0 means none.  Past it they return -EAGAIN (or, for send,
 * the bytes already queued). */
ssize_t    tcp_send_until(tcp_pcb_t *p, const void *buf, size_t len, uint64_t deadline);
ssize_t    tcp_recv_until(tcp_pcb_t *p, void *buf, size_t len, uint64_t deadline);
ssize_t    tcp_peek_until(tcp_pcb_t *p, void *buf, size_t len, uint64_t deadline);
ssize_t    tcp_send_nb(tcp_pcb_t *p, const void *buf, size_t len);
ssize_t    tcp_send_urg_until(tcp_pcb_t *p, const void *buf, size_t len,
                              int nonblock, uint64_t deadline);   /* TCP-URG-02 */
ssize_t    tcp_recv(tcp_pcb_t *p, void *buf, size_t len);
void       tcp_endpoints(const tcp_pcb_t *p, uint32_t *laddr, uint16_t *lport, uint32_t *raddr, uint16_t *rport);
ssize_t    tcp_recv_nb(tcp_pcb_t *p, void *buf, size_t len);
/* Bytes currently sitting in the receive ring (for ioctl(FIONREAD)). */
size_t     tcp_recv_avail(const tcp_pcb_t *p);
ssize_t    tcp_peek(tcp_pcb_t *p, void *buf, size_t len);
ssize_t    tcp_peek_nb(tcp_pcb_t *p, void *buf, size_t len);
int        tcp_close(tcp_pcb_t *p);
int        tcp_abort(tcp_pcb_t *p);                             /* TCP-API-11 */
int        tcp_take_so_error(tcp_pcb_t *p);
void       tcp_set_txopts(tcp_pcb_t *p, const struct ip4_txopts *o);
int        tcp_set_user_timeout(tcp_pcb_t *p, uint32_t ms);     /* TCP-WIN-13 */
uint32_t   tcp_get_user_timeout(const tcp_pcb_t *p);
int        tcp_sockatmark(tcp_pcb_t *p);                        /* TCP-URG-01 */
ssize_t    tcp_recv_oob(tcp_pcb_t *p, void *buf, size_t len, int peek); /* TCP-URG-04 */
void       tcp_set_owner(tcp_pcb_t *p, int owner);
int        tcp_get_owner(const tcp_pcb_t *p);

/* -- AF_INET socket helper APIs -------------------------------------- */
int     afinet_listen(int fd, int backlog);
int     afinet_accept(int fd, void *addr, socklen_t *addrlen);
size_t  afinet_node_read(struct fs_node *, off_t, size_t, uint8_t *);
int     afinet_shutdown(int fd, int how);
int     afinet_getsockname(int fd, void *addr, socklen_t *addrlen);
int     afinet_getpeername(int fd, void *addr, socklen_t *addrlen);
int     afinet_set_reuseaddr(int fd, int on);
/* UDP-API-15: SO_BROADCAST; -ENOTSOCK on a non-AF_INET fd. */
int     afinet_set_broadcast(int fd, int on);
int     afinet_get_broadcast(int fd);
/* UDP-API-14: SO_RCVBUF (rcv != 0) / SO_SNDBUF capacity of an AF_INET socket. */
int     afinet_bufsize(int fd, int rcv);
/* UDP-RES-01: set SO_RCVBUF, in bytes; -ENOTSOCK on a non-AF_INET fd. */
int     afinet_set_rcvbuf(int fd, int val);
/* UDP-API-11/-12: IPPROTO_IP options -- IP_TOS (1), IP_TTL (2),
 * IP_PKTINFO (8, receive side),
 * IP_MULTICAST_IF (32, val unused, addr = interface address),
 * IP_MULTICAST_TTL (33), IP_MULTICAST_LOOP (34).  set: -ENOTSOCK on a
 * non-AF_INET fd, -EINVAL for an out-of-range value, -ENOPROTOOPT for any
 * other option; get likewise, returning the value (or the address). */
int     afinet_set_ipopt(int fd, int optname, int val, uint32_t addr);
int     afinet_get_ipopt(int fd, int optname, int *val, uint32_t *addr);
int     afinet_set_tcpopt(int fd, int optname, int val);     /* TCP-WIN-13 */
int     afinet_get_tcpopt(int fd, int optname, int *val);
int     afinet_setown(int fd, int owner);                   /* TCP-URG-01 */
int     afinet_getown(int fd, int *owner);
int     afinet_set_linger(int fd, int onoff, int secs);     /* TCP-API-11 */
int     afinet_get_linger(int fd, int *onoff, int *secs);
/* UDP-API-04: SO_RCVTIMEO (rcv != 0) / SO_SNDTIMEO, as seconds plus
 * microseconds; both zero means no timeout.  -ENOTSOCK on a non-AF_INET fd,
 * -EDOM for a negative or out-of-range value. */
int     afinet_set_timeo(int fd, int rcv, int64_t sec, int64_t usec);
int     afinet_get_timeo(int fd, int rcv, int64_t *sec, int64_t *usec);
/* UDP-IP-06: IP_ADD_MEMBERSHIP (add != 0) / IP_DROP_MEMBERSHIP.  group and
 * ifaddr in network byte order; ifindex > 0 selects the interface by index
 * (struct ip_mreqn) and overrides ifaddr. */
int     afinet_mc_membership(int fd, int add, uint32_t group, uint32_t ifaddr,
                             int ifindex);
int     afinet_so_error(int fd);
int     afinet_so_type(int fd);
int     afinet_get_reuseaddr(int fd);

/* -- AF_PACKET socket helper APIs ------------------------------------ */
int     afpacket_socket(int type, int protocol);
int     afpacket_bind(int fd, const void *sll, socklen_t len);
ssize_t afpacket_sendto(int fd, const void *buf, size_t len, int flags,
                        const void *sll, socklen_t addrlen);
/* UDP-API-02: as afpacket_sendto(), but `kbuf` is kernel memory (the
 * sendmsg() gather buffer) and is not copied in. */
ssize_t afpacket_sendto_kbuf(int fd, const void *kbuf, size_t len, int flags,
                             const void *sll, socklen_t addrlen);
ssize_t afpacket_recvfrom(int fd, void *buf, size_t len, int flags,
                          void *sll, socklen_t *addrlen);
size_t  afpkt_node_read(struct fs_node *, off_t, size_t, uint8_t *);

/* A kernel copy of one iovec (its iov_base is still a user pointer).  The
 * personality iovec types share this layout. */
struct iovec_local { void *iov_base; size_t iov_len; };

/* UDP-API-03: is fd a datagram socket (AF_UNIX/AF_INET SOCK_DGRAM, or
 * SOCK_RAW)?  And send a kernel-copied iovec array on one as a single
 * datagram (sendmsg/writev), returning the bytes sent or -errno. */
int     sock_fd_is_dgram(int fd);
ssize_t sock_dgram_sendv(int fd, const struct iovec_local *kiov, int iovcnt,
                         int flags, const struct sockaddr *uaddr,
                         socklen_t addrlen);

/* -- userspace boundary helpers for the socket syscalls --------------- *
 *
 * The socket syscalls receive raw userspace pointers.  Dereferencing one
 * directly is both a security hole (a caller-chosen kernel address gets
 * written or read) and a stability hole (an unmapped pointer takes an
 * unrecoverable kernel fault instead of returning EFAULT).  Task #156
 * (NET-03) fixed this for bind/getsockname/getsockopt; these helpers exist
 * so accept/sendto/recvfrom get the same treatment without each call site
 * re-deriving the copyin/copyout dance.
 *
 * SOCK_UADDR_MAX bounds the on-stack sockaddr copy.  It covers
 * sockaddr_un (2 + 108), sockaddr_in6 (28) and sockaddr_ll (20) with room
 * to spare; a caller naming more than this gets its address truncated to
 * the bound, which is exactly what the BSD API says may happen.
 */
#define SOCK_UADDR_MAX 128

/* Read a user socklen_t.  Returns 0 and stores the value, or -EFAULT. */
int sock_copyin_addrlen(const socklen_t *ulen, socklen_t *out);

/*
 * Copy a kernel-built sockaddr out to a user buffer whose capacity is named
 * by *ulen, then write back the address length.
 *
 * At most *ulen bytes are copied, but the value written back is `srclen` --
 * the full, untruncated size.  That is deliberate and required: POSIX says
 * the returned length "shall refer to the value before truncation", which is
 * the only way a caller can tell that its buffer was too small.  Reporting
 * the copied length instead would silently hide truncation.
 *
 * addr == NULL or ulen == NULL is a no-op success, matching accept(2).
 */
int sock_copyout_sockaddr(const void *src, socklen_t srclen,
                          void *addr, socklen_t *ulen);

#endif /* _SYS_NET_INET_H */
