/*
 * af_inet_var.h -- the AF_INET/AF_INET6 socket's internal state, private to
 * sys/net/af_inet.c (BSD's _var.h convention: a protocol's own structures,
 * not an interface to it).
 */
#ifndef _NET_AF_INET_VAR_H
#define _NET_AF_INET_VAR_H

#include <stdint.h>
#include <net/inet.h>
#include <sys/netdev.h>
#include <vfs/vfs.h>

/* SO_RCVBUF, in bytes of ring (headers included). */
#define AFI_RCVBUF_DEFAULT (64u * 1024u)
#define AFI_RCVBUF_MIN     (2u * 1024u)
#define AFI_RCVBUF_MAX     (1024u * 1024u)

/* IPv4 multicast groups one socket may join. */
#define AFI_MC_MAX 8

typedef struct afi_sock {
    int      family;        /* AF_INET / AF_INET6 */
    int      type;          /* SOCK_RAW / SOCK_DGRAM / SOCK_STREAM */
    int      protocol;      /* IPPROTO_* */
    uint16_t local_port;    /* host order, 0 = unbound */
    uint8_t  local_addr[16];

    /* TCP-specific. */
    tcp_pcb_t *tcp;

    /* Connected-state peer for UDP connect(). */
    uint16_t peer_port;
    uint8_t  peer_addr[16];
    int      connected;
    int      bound;        /* explicit bind() succeeded — re-bind is EINVAL */
    int      reuseaddr;    /* SO_REUSEADDR — relaxes the EADDRINUSE check */
    int      pktinfo;      /* IP_PKTINFO requested */
    int      broadcast;    /* SO_BROADCAST */
    uint32_t owner_uid;    /* euid of the creating process */
    uint32_t rcv_timeo;    /* SO_RCVTIMEO in ticks, 0 = none */
    struct ip4_txopts txo; /* IP_TTL/IP_TOS/IP_MULTICAST_* */
    uint32_t snd_timeo;    /* SO_SNDTIMEO in ticks, 0 = none */

    /* The datagram receive queue, a byte ring of afi_rec_t
     * records.  Allocated only for SOCK_DGRAM/SOCK_RAW (a stream socket's
     * data lives in its TCP PCB); rcvbuf (SO_RCVBUF) bounds rq_used and may
     * be below rq_cap after a shrink.  count is the number of datagrams and
     * doubles as the wait channel. */
    uint8_t   *rq;
    uint32_t   rq_cap, rq_head, rq_tail, rq_used;
    uint32_t   rcvbuf;
    uint32_t   rq_drops;    /* datagrams dropped on a full queue */
    uint32_t   count;
    void      *wait_chan;
    int        closed;
    int        rd_shut;     /* shutdown(SHUT_RD): reads return EOF */
    int        wr_shut;     /* shutdown(SHUT_WR): sends EPIPE */
    int        so_error;    /* errno latched from an ICMP error;
                             * guarded by afi_lock */
    /* IPv4 groups this socket joined (network byte order; 0 =
     * free) and the interface each was joined on.  Guarded by afi_lock. */
    uint32_t   mc_group[AFI_MC_MAX];
    netdev_t  *mc_dev[AFI_MC_MAX];

    /* Reference count guarding the socket's lifetime against the
     * hard-IRQ delivery path.  Held by the installed socket itself (the
     * fd/list reference, dropped by afinet_node_close) plus a reference
     * each entry point takes for its call (afi_pin_fd()) and each blocking
     * reader for its duration, so a concurrent close() can never free the
     * struct while inbound traffic is being delivered into its ring or a
     * call is using it. */
    int        refcount;
    int        owner;       /* F_SETOWN (pid, or -pgrp) */
    int        linger_on;   /* SO_LINGER l_onoff */
    int        linger_secs; /* SO_LINGER l_linger */

    fs_node_t  node;
    struct afi_sock *next;
} afi_sock_t;

#endif /* _NET_AF_INET_VAR_H */
