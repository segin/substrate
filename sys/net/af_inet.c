/*
 * af_inet.c — AF_INET and AF_INET6 sockets (SOCK_RAW + SOCK_DGRAM).
 *
 * Dual-family in one C file because the bookkeeping is identical;
 * only the address shape differs.
 *
 * SOCK_RAW (IPv4): receives entire IP packets matching its protocol.
 * SOCK_RAW (IPv6): receives the IPv6 payload (no IPv6 header), per
 * the BSD convention.  This is what ping(8) expects.
 *
 * SOCK_DGRAM with IPPROTO_UDP: UDP send/recv via ip{4,6}_output, with
 * automatic ephemeral port allocation on first sendto if not bound.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <kern/console.h>
#include <kern/file.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <net/if.h>
#include <net/inet.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <sys/copy.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/netdev.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/termios.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

#ifndef AF_INET
#define AF_INET  2
#endif
#ifndef AF_INET6
#define AF_INET6 10
#endif
#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif
#ifndef SOCK_RAW
#define SOCK_RAW   3
#endif

/* TCP PCB opaque pointer — exported by tcp.c. */


/* Match userland struct sockaddr_in / sockaddr_in6 from
 * include/netinet/in.h. */
struct sin_kern {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t  pad[8];
};
struct sin6_kern {
    uint16_t sin6_family;
    uint16_t sin6_port;
    uint32_t sin6_flowinfo;
    uint8_t  sin6_addr[16];
    uint32_t sin6_scope_id;
};

/* ------------------------------------------------------------------ */
/* Per-datagram receive queue                                         */
/* ------------------------------------------------------------------ */

/*
 * UDP-04: the largest UDP payload that can actually cross this stack.
 *
 * This was 1500, which silently truncated on receive and refused on send
 * anything between 1501 and the real link maximum -- a datagram that the
 * NIC delivered whole was cut short with no indication to the caller.
 *
 * The audit asked for 65507 (the protocol maximum), and that is NOT what
 * this is, deliberately: reaching it requires IP fragmentation on send and
 * reassembly on receive, and this stack has neither -- ip4_input drops every
 * fragment outright (inet.c: "Drop fragments -- we don't reassemble yet")
 * and ip4_output emits a single unfragmented packet.  A >MTU datagram
 * therefore cannot arrive or leave regardless of what this constant says,
 * and raising it to 65507 would cost 32x that per socket ring for no
 * behavioural gain.  What it CAN do is stop truncating what does fit, so it
 * is now exactly MTU minus the IPv4 and UDP headers.  Real 65507 support is
 * an IP-fragmentation feature, not a socket-layer one.
 */
#define AFI_DATA_MAX (NETDEV_MTU_MAX - 20 - 8)

/*
 * UDP-RES-01: one queued datagram -- this header, then `len` payload bytes,
 * padded to 4 -- in the socket's receive byte ring.  The queue used to be 32
 * fixed slots of AFI_DATA_MAX bytes each, bounded by datagram COUNT: 32
 * eight-byte datagrams filled it, while SO_RCVBUF was ignored.
 */
typedef struct afi_rec {
    uint16_t len;       /* payload bytes stored */
    /* SOCK-06: the datagram's length as it arrived, which can exceed `len`
     * if it did not fit.  recv(MSG_TRUNC) reports this so a caller can tell
     * "your buffer was too small" from "the datagram really was this short";
     * without it the two were indistinguishable. */
    uint16_t truelen;
    uint8_t  family;    /* AF_INET or AF_INET6 */
    uint8_t  proto;
    uint16_t port;      /* source port for UDP, 0 for RAW */
    uint8_t  addr[16];  /* source address (4 bytes for v4) */
    uint32_t daddr4;    /* UDP-API-11: IPv4 destination it arrived for */
    uint32_t ifindex;   /* UDP-API-11: interface it arrived on (0 unknown) */
} afi_rec_t;

/* Ring space a record with `n` payload bytes occupies. */
#define AFI_REC_SPACE(n)   (((uint32_t)sizeof(afi_rec_t) + (uint32_t)(n) + 3u) & ~3u)
/* UDP-RES-01: SO_RCVBUF, in bytes of ring (headers included). */
#define AFI_RCVBUF_DEFAULT (64u * 1024u)
#define AFI_RCVBUF_MIN     (2u * 1024u)
#define AFI_RCVBUF_MAX     (1024u * 1024u)

/* UDP-IP-06: IPv4 multicast groups one socket may join. */
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
    int      pktinfo;      /* UDP-API-11: IP_PKTINFO requested */
    int      broadcast;    /* UDP-API-15: SO_BROADCAST */
    uint32_t owner_uid;    /* UDP-API-01: euid of the creating process */
    uint32_t rcv_timeo;    /* UDP-API-04: SO_RCVTIMEO in ticks, 0 = none */
    struct ip4_txopts txo; /* UDP-API-12: IP_TTL/IP_TOS/IP_MULTICAST_* */
    uint32_t snd_timeo;    /* UDP-API-04: SO_SNDTIMEO in ticks, 0 = none */

    /* UDP-RES-01/-02: the datagram receive queue, a byte ring of afi_rec_t
     * records.  Allocated only for SOCK_DGRAM/SOCK_RAW (a stream socket's
     * data lives in its TCP PCB); rcvbuf (SO_RCVBUF) bounds rq_used and may
     * be below rq_cap after a shrink.  count is the number of datagrams and
     * doubles as the wait channel. */
    uint8_t   *rq;
    uint32_t   rq_cap, rq_head, rq_tail, rq_used;
    uint32_t   rcvbuf;
    uint32_t   rq_drops;    /* UDP-RES-03: datagrams dropped on a full queue */
    uint32_t   count;
    void      *wait_chan;
    int        closed;
    int        rd_shut;     /* shutdown(SHUT_RD): reads return EOF */
    int        wr_shut;     /* UDP-API-19: shutdown(SHUT_WR): sends EPIPE */
    int        so_error;    /* UDP-ICMP-01: errno latched from an ICMP error;
                             * guarded by afi_lock */
    /* UDP-IP-06: IPv4 groups this socket joined (network byte order; 0 =
     * free) and the interface each was joined on.  Guarded by afi_lock. */
    uint32_t   mc_group[AFI_MC_MAX];
    netdev_t  *mc_dev[AFI_MC_MAX];

    /* NET-01: reference count guarding the socket's lifetime against the
     * hard-IRQ delivery path.  Held by the installed socket itself (the
     * fd/list reference, dropped by afinet_node_close) plus a transient
     * reference each blocking reader takes for its duration, so a
     * concurrent close() can never free the struct while inbound traffic
     * is being delivered into its ring or a reader is asleep on it. */
    int        refcount;

    fs_node_t  node;
    struct afi_sock *next;
} afi_sock_t;

/*
 * UDP-API-20 / UDP-I-03: the largest payload one send on this socket may
 * carry.  A raw socket supplies its own L4 header, so it gets everything
 * behind the IP header the stack synthesizes; a datagram socket also loses
 * the UDP header.  Per family: the IPv6 header is 40 bytes, not IPv4's 20
 * (the IPv6 datagram cap used to be computed from the IPv4 one).  One
 * helper, so sendto() and write() cannot disagree again -- sendto() used to
 * cap a raw payload with the UDP datagram limit and write() with the IP
 * layer's.
 */
static size_t afi_max_payload(int family, int type) {
    size_t iph = family == AF_INET ? 20 : 40;
    if (type == SOCK_RAW) return NETDEV_MTU_MAX - iph;
    return NETDEV_MTU_MAX - iph - 8;               /* sizeof(struct udphdr) */
}

static afi_sock_t *g_afi_head;
/* UDP-API-04: the absolute deadline for a blocking call under a timeout of
 * `timeo` ticks, or 0 for none. */
static uint64_t afi_deadline(uint32_t timeo) {
    return timeo ? get_ticks() + timeo : 0;
}

static uint16_t    g_ephemeral_next = 49152;

/* NET-01: g_afi_head and every socket's ring counters (head/tail/count),
 * closed flag and refcount are mutated from BOTH the hard-IRQ delivery
 * path (afinet_deliver_v4/v6 -> enqueue, called from netdev RX) and
 * process context (socket/accept/bind/close/recv).  This IRQ-safe
 * spinlock serialises them: an RX interrupt landing mid-close() can no
 * longer free a node out from under a delivering packet, and the list
 * walk can't observe a half-spliced link.  Must always be taken with the
 * _irq variants — it is acquired from interrupt context. */
static spinlock_t afi_lock = SPINLOCK_INIT("af_inet");

/* Free a socket's backing storage.  Never called with afi_lock held —
 * kfree may take the allocator's own locks. */
static void afi_free_sock(afi_sock_t *s) {
    if (s->rq) kfree(s->rq, s->rq_cap);
    kfree(s, sizeof(*s));
}

/* UDP-RES-01: copy into / out of the receive ring at byte offset `off`,
 * wrapping at rq_cap.  The caller holds afi_lock. */
static void rq_put(afi_sock_t *s, uint32_t off, const void *src, uint32_t n) {
    const uint8_t *b = (const uint8_t *)src;
    off %= s->rq_cap;
    uint32_t first = s->rq_cap - off < n ? s->rq_cap - off : n;
    memcpy(s->rq + off, b, first);
    if (n > first) memcpy(s->rq, b + first, n - first);
}

static void rq_get(const afi_sock_t *s, uint32_t off, void *dst, uint32_t n) {
    uint8_t *b = (uint8_t *)dst;
    off %= s->rq_cap;
    uint32_t first = s->rq_cap - off < n ? s->rq_cap - off : n;
    memcpy(b, s->rq + off, first);
    if (n > first) memcpy(b + first, s->rq, n - first);
}

/* Take (consume != 0) or look at the oldest queued record: its header into
 * *h and up to `max` payload bytes into buf.  count must be non-zero. */
static void rq_pop(afi_sock_t *s, afi_rec_t *h, uint8_t *buf, size_t max,
                   int consume) {
    rq_get(s, s->rq_tail, h, sizeof(*h));
    uint32_t n = h->len < max ? h->len : (uint32_t)max;
    rq_get(s, s->rq_tail + (uint32_t)sizeof(*h), buf, n);
    if (consume) {
        uint32_t sp = AFI_REC_SPACE(h->len);
        s->rq_tail = (s->rq_tail + sp) % s->rq_cap;
        s->rq_used -= sp;
        s->count--;
    }
}

/* Drop a reference taken under afi_lock and release the lock in one step;
 * frees the socket if this was the last reference.  Callers hold afi_lock
 * (acquired with flags `fl`) and must not touch `s` afterwards. */
static void afi_rele_unlock(afi_sock_t *s, unsigned long fl) {
    int last = (--s->refcount == 0);
    spinlock_release_irq(&afi_lock, fl);
    if (last) afi_free_sock(s);
}

/* Hand out a host-order ephemeral port in the IANA dynamic range
 * [49152, 65535], never 0.  Used by bind(port 0) so getsockname()
 * reports a concrete port immediately (POSIX/BSD semantics) rather
 * than 0 — Sun RPC's svc_reg() reads the port via getsockname right
 * after bind and registers it with rpcbind, so a 0 here makes a
 * service register port 0 and then conflict with its own real-port
 * re-registration (CDE ToolTalk's ttsession). */
static uint16_t afinet_alloc_ephemeral(void) {
    if (g_ephemeral_next < 49152) g_ephemeral_next = 49152;
    uint16_t port = g_ephemeral_next++;
    if (g_ephemeral_next == 0) g_ephemeral_next = 49152;
    return port;
}

/*
 * UDP-05: hand out an ephemeral port that is not already in use.
 *
 * The bare counter above is incremented without any check that the port it
 * lands on is free, so two sockets could be handed the same one -- after
 * which the demux (even the fixed best-match one) has an ambiguous key and
 * one of them quietly receives the other's traffic.  Sweep the whole
 * dynamic range rather than trusting the counter; 16384 candidates is a
 * bounded walk and the common case exits on the first.
 *
 * Callers must also record the result in s->local_port BEFORE anyone else
 * can allocate, and set s->bound -- an implicitly-bound socket that leaves
 * `bound` clear is invisible to afinet_port_taken_locked(), which is how the
 * collision persisted even once a check existed.
 */
static int afinet_port_taken_locked(const afi_sock_t *self, uint16_t port);

/*
 * UDP-API-18: and do it atomically.  The counter was advanced, the port
 * checked under afi_lock, the lock DROPPED, and only then did the caller
 * record the port -- so two threads binding implicitly at once could both
 * pass the check on the same port.  Advance, check and record (local_port,
 * bound) are now one afi_lock critical section.  Callers' own assignments
 * of the returned port are then redundant but harmless.
 */
static uint16_t afinet_alloc_ephemeral_free(afi_sock_t *s) {
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    for (int i = 0; i < 16384; i++) {
        uint16_t port = afinet_alloc_ephemeral();
        if (!afinet_port_taken_locked(s, port)) {
            s->local_port = port;
            s->bound = 1;
            spinlock_release_irq(&afi_lock, fl);
            return port;
        }
    }
    spinlock_release_irq(&afi_lock, fl);
    return 0;   /* range exhausted; caller reports EADDRINUSE */
}

/* ------------------------------------------------------------------ */
/* SIOC* ioctls — interface configuration via an AF_INET socket fd.   */
/* ------------------------------------------------------------------ */

static netdev_t *afinet_find_dev(const char *name) {
    if (!name || !name[0]) return NULL;
    return netdev_by_name(name);
}

static int afinet_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    if (!arg) return -EFAULT;

    /* FIONREAD: bytes available to read without blocking.  Xlib (and many
     * socket clients) call this on the connection to size the next read;
     * returning ENOTTY makes Xlib declare the display dead ("XIO: fatal IO
     * error ... (Not a typewriter)").  TCP reports its rx-ring occupancy;
     * UDP/RAW reports the next datagram's length (BSD/Linux semantics). */
    if (request == FIONREAD) {
        afi_sock_t *s = node ? (afi_sock_t *)(uintptr_t)node->impl : NULL;
        int avail = 0;
        if (s) {
            if (s->tcp) {
                avail = (int)tcp_recv_avail(s->tcp);
            } else {
                /* UDP-RES-05 (read under the lock, as recvfrom does). */
                unsigned long ffl = spinlock_acquire_irq(&afi_lock);
                if (s->count > 0) {
                    afi_rec_t h;
                    rq_get(s, s->rq_tail, &h, sizeof(h));
                    avail = (int)h.len;
                }
                spinlock_release_irq(&afi_lock, ffl);
            }
        }
        if (copyout(&avail, arg, sizeof(avail)) != 0) return -EFAULT;
        return 0;
    }

    /* SIOCGIFCONF takes struct ifconf; everything else takes struct ifreq.
     * Both `arg` and (for SIOCGIFCONF) the ifc_req array it points at are
     * user-space pointers — copy in/out rather than dereferencing them. */
    if (request == SIOCGIFCONF) {
        struct ifconf ifc;
        if (copyin(arg, &ifc, sizeof(ifc)) != 0) return -EFAULT;
        int max = ifc.ifc_len / (int)sizeof(struct ifreq);
        struct ifreq *out = ifc.ifc_req;   /* user pointer */
        int n = 0;
        for (netdev_t *d = netdev_first(); d && n < max; d = netdev_next(d)) {
            struct ifreq e;
            memset(&e, 0, sizeof(e));
            strlcpy(e.ifr_name, d->name, IFNAMSIZ);
            struct sin_kern *sin = (struct sin_kern *)&e.ifr_addr;
            sin->sin_family = AF_INET;
            sin->sin_addr   = d->ip4_addr;
            if (!out || copyout(&e, &out[n], sizeof(e)) != 0) return -EFAULT;
            n++;
        }
        ifc.ifc_len = n * (int)sizeof(struct ifreq);
        if (copyout(&ifc, arg, sizeof(ifc)) != 0) return -EFAULT;
        return 0;
    }

    /* The IPv6 commands take struct in6_ifreq (no name field — keyed
     * by ifindex).  Handle them before the by-name lookup. */
    if (request == SIOCGIFADDR_IN6 || request == SIOCSIFADDR_IN6 ||
        request == SIOCDIFADDR_IN6 || request == SIOCSIFGW_IN6) {
        /* CFG-01: same rule as the IPv4 setters below -- these mutate the
         * interface address and the v6 gateway, which is the whole of the
         * IPv6 routing state. */
        if (request != SIOCGIFADDR_IN6 &&
            (!current_process || current_process->euid != 0))
            return -EPERM;

        struct in6_ifreq kr6;
        if (copyin(arg, &kr6, sizeof(kr6)) != 0) return -EFAULT;
        struct in6_ifreq *r6 = &kr6;
        netdev_t *d6 = (r6->ifr6_ifindex > 0)
            ? netdev_by_index((uint32_t)r6->ifr6_ifindex)
            : netdev_first();
        if (!d6) return -ENODEV;
        switch (request) {
            case SIOCGIFADDR_IN6:
                memcpy(r6->ifr6_addr.s6_addr, d6->ip6_addr, 16);
                r6->ifr6_prefixlen = d6->ip6_netmask_bits;
                if (copyout(r6, arg, sizeof(*r6)) != 0) return -EFAULT;
                return 0;
            case SIOCSIFADDR_IN6:
                if (r6->ifr6_prefixlen > 128) return -EINVAL;
                memcpy(d6->ip6_addr, r6->ifr6_addr.s6_addr, 16);
                d6->ip6_netmask_bits = (uint8_t)r6->ifr6_prefixlen;
                return 0;
            case SIOCDIFADDR_IN6:
                memset(d6->ip6_addr, 0, 16);
                d6->ip6_netmask_bits = 0;
                return 0;
            case SIOCSIFGW_IN6:
                memcpy(d6->ip6_gateway, r6->ifr6_addr.s6_addr, 16);
                return 0;
        }
    }

    struct ifreq kr;
    if (copyin(arg, &kr, sizeof(kr)) != 0) return -EFAULT;
    /* Ensure the name field is NUL-terminated before using it. */
    kr.ifr_name[IFNAMSIZ - 1] = '\0';
    struct ifreq *r = &kr;
    netdev_t *dev = afinet_find_dev(r->ifr_name);
    if (!dev) return -ENODEV;

    /*
     * CFG-01: the SIOCSIF* commands mutate dev->ip4_addr / ip4_netmask /
     * ip4_gateway / hwaddr / mtu / flags directly, and on this stack those
     * fields ARE the routing table -- route_for_v4() reads nothing else.
     * With no privilege check any unprivileged process could repoint the
     * default gateway, change the interface address, or set IFF_PROMISC and
     * have the IP/ARP layers ingest every frame on the segment.  Interface
     * configuration is a root operation; the SIOCGIF* queries stay open.
     */
    switch (request) {
        case SIOCSIFFLAGS:
        case SIOCSIFMTU:
        case SIOCSIFHWADDR:
        case SIOCSIFADDR:
        case SIOCSIFNETMASK:
        case SIOCSIFBRDADDR:
        case SIOCSIFGATEWAY:
            if (!current_process || current_process->euid != 0)
                return -EPERM;
            break;
        default:
            break;
    }

    /* "Get" commands fill the kernel copy `kr` and fall through to the
     * copyout at out_get; "set"/no-op commands mutate the device and
     * return directly (nothing to hand back). */
    switch (request) {
        case SIOCGIFNAME:
            strlcpy(r->ifr_name, dev->name, IFNAMSIZ);
            goto out_get;
        case SIOCGIFINDEX:
            r->ifr_ifindex = (int)dev->ifindex;
            goto out_get;
        case SIOCGIFFLAGS:
            r->ifr_flags = (short)dev->flags;
            goto out_get;
        case SIOCSIFFLAGS: {
            uint32_t keep = dev->flags & ~((uint32_t)0xFFFFu);
            dev->flags = keep | ((uint16_t)r->ifr_flags & 0xFFFFu);
            return 0;
        }
        case SIOCGIFMTU:
            r->ifr_mtu = (int)dev->mtu;
            goto out_get;
        case SIOCSIFMTU:
            if (r->ifr_mtu < 68 || r->ifr_mtu > 65535) return -EINVAL;
            dev->mtu = (uint32_t)r->ifr_mtu;
            return 0;
        case SIOCGIFHWADDR:
            r->ifr_hwaddr.sa_family = 1;   /* ARPHRD_ETHER */
            memcpy(r->ifr_hwaddr.sa_data, dev->hwaddr, 6);
            goto out_get;
        case SIOCSIFHWADDR:
            memcpy(dev->hwaddr, r->ifr_hwaddr.sa_data, 6);
            return 0;
        case SIOCGIFADDR: {
            struct sin_kern *sin = (struct sin_kern *)&r->ifr_addr;
            sin->sin_family = AF_INET;
            sin->sin_addr   = dev->ip4_addr;
            sin->sin_port   = 0;
            goto out_get;
        }
        case SIOCSIFADDR: {
            const struct sin_kern *sin = (const struct sin_kern *)&r->ifr_addr;
            if (sin->sin_family != AF_INET) return -EAFNOSUPPORT;
            dev->ip4_addr = sin->sin_addr;
            return 0;
        }
        case SIOCGIFNETMASK: {
            struct sin_kern *sin = (struct sin_kern *)&r->ifr_netmask;
            sin->sin_family = AF_INET;
            sin->sin_addr   = dev->ip4_netmask;
            goto out_get;
        }
        case SIOCSIFNETMASK: {
            const struct sin_kern *sin = (const struct sin_kern *)&r->ifr_netmask;
            if (sin->sin_family != AF_INET) return -EAFNOSUPPORT;
            dev->ip4_netmask = sin->sin_addr;
            return 0;
        }
        case SIOCGIFBRDADDR: {
            struct sin_kern *sin = (struct sin_kern *)&r->ifr_broadaddr;
            sin->sin_family = AF_INET;
            sin->sin_addr   =
                (dev->ip4_addr & dev->ip4_netmask) | ~dev->ip4_netmask;
            goto out_get;
        }
        case SIOCSIFBRDADDR:
            /* No-op: broadcast is derived from addr/netmask. */
            return 0;
        case SIOCGIFGATEWAY: {
            struct sin_kern *sin = (struct sin_kern *)&r->ifr_addr;
            sin->sin_family = AF_INET;
            sin->sin_addr   = dev->ip4_gateway;
            goto out_get;
        }
        case SIOCSIFGATEWAY: {
            const struct sin_kern *sin = (const struct sin_kern *)&r->ifr_addr;
            if (sin->sin_family != AF_INET) return -EAFNOSUPPORT;
            dev->ip4_gateway = sin->sin_addr;
            return 0;
        }
        default:
            return -ENOTTY;
    }

out_get:
    if (copyout(r, arg, sizeof(*r)) != 0) return -EFAULT;
    return 0;
}

/* ------------------------------------------------------------------ */
/* fs_node adapter                                                    */
/* ------------------------------------------------------------------ */

size_t afinet_node_read(fs_node_t *node, off_t off, size_t size, uint8_t *buf);
static size_t afinet_node_write(fs_node_t *node, off_t off, size_t size, const uint8_t *buf);
static void   afinet_node_close(fs_node_t *node);
static int    afinet_node_poll(fs_node_t *node, void *waiter);

static int afinet_node_poll(fs_node_t *node, void *waiter)
{
    afi_sock_t *s = (afi_sock_t *)(uintptr_t)node->impl;
    if (!s) return POLLNVAL;
    if (s->tcp) {
        /* Defer to TCP for connect-state / accept / recv readiness. */
        void *chan = NULL;
        int   rv   = tcp_poll(s->tcp, POLLIN | POLLOUT, &chan);
        if (waiter && chan) *(void **)waiter = chan;
        return rv;
    }
    /* UDP/RAW: ready-to-read when the per-socket packet ring has
     * something queued; always writeable.  */
    int rv = POLLOUT;
    if (s->count > 0) rv |= POLLIN;
    if (s->so_error) rv |= POLLERR;     /* UDP-ICMP-01 */
    if (s->rd_shut) rv |= POLLIN | POLLRDHUP;   /* UDP-API-08: reads return EOF */
    if (waiter && rv == POLLOUT) *(void **)waiter = s->wait_chan;
    return rv;
}

/* read()/write() reach the socket via the fs_node, not the fd, so
 * O_NONBLOCK (canonicalised to FNONBLOCK) is not directly visible.  Find
 * the current process's fd backing this node and report its flag —
 * otherwise a non-blocking read()/write() blocks like a blocking one, and
 * a single-threaded nonblocking-pump TCP transfer self-deadlocks. */
static int afi_node_nonblock(const fs_node_t *node) {
    /* read()/write() stash the file_t on the thread (io_file) for the duration
     * of the node op, so recover O_NONBLOCK in O(1) instead of scanning all
     * MAX_FD (4096) fds on every TCP read/write.  Fall back to the scan only
     * when io_file is absent or for a different node (defensive). */
    if (current_thread && current_thread->io_file &&
        (const void *)current_thread->io_file->f_data == (const void *)node)
        return (current_thread->io_file->f_flag & FNONBLOCK) ? 1 : 0;
    if (!current_process) return 0;
    for (int fd = 0; fd < MAX_FD; fd++) {
        file_t *f = current_process->fds[fd];
        if (f && (const void *)f->f_data == (const void *)node)
            return (f->f_flag & FNONBLOCK) ? 1 : 0;
    }
    return 0;
}

/*
 * UDP-06: register on the sleep queue BEFORE dropping the ring lock.
 *
 * The receive loops used to do
 *      spinlock_release_irq(&afi_lock, fl);
 *      sched_sleep(s->wait_chan);
 * with a window in between.  A datagram delivered in that window ran
 * enqueue() -> sched_wakeup() against a thread that was not yet asleep, so
 * the wakeup hit nothing and the reader then slept on an already-full ring
 * -- woken only by the next datagram or the ~50 ms fallback deadline.  For a
 * request/response protocol that is up to a full extra deadline of latency
 * per exchange, and with no further traffic it is an outright hang until the
 * deadline fires.
 *
 * afunix_wait() already had the right shape: add to the sleep queue with
 * interrupts disabled, THEN release the lock, so a wakeup racing the release
 * finds us queued.  This is the AF_INET twin, with the ring's IRQ-safe
 * spinlock in place of the mutex.  Returns 0 normally, -EINTR if a signal is
 * pending on wake.  On return afi_lock is held again with `*fl` refreshed.
 */
static int afi_wait(afi_sock_t *s, unsigned long *fl) {
    if (!current_thread) {
        spinlock_release_irq(&afi_lock, *fl);
        sched_yield();
        *fl = spinlock_acquire_irq(&afi_lock);
        return 0;
    }
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;
    /* afi_lock was taken with spinlock_acquire_irq, so interrupts are
     * already off here -- the enqueue path cannot run between the
     * sleepq_add and the release below. */
    sleepq_add(s->wait_chan, current_thread);
    if (current_thread->sleep_expiry == 0) {
        uint32_t hz = get_hz();
        uint64_t span = hz ? (hz / 20u) : 8u;   /* ~50 ms backstop */
        if (span == 0) span = 1;
        current_thread->sleep_expiry = get_ticks() + span;
    }
    spinlock_release_irq(&afi_lock, *fl);
    if (current_thread->wait_chan == s->wait_chan)
        sched_yield();
    current_thread->sleep_expiry = 0;
    sleepq_remove_thread(current_thread);
    *fl = spinlock_acquire_irq(&afi_lock);
    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
    if (current_thread->sig_pending & ~current_thread->sig_mask)
        return -EINTR;
    return 0;
}

/*
 * SOCK-03: pin the socket BEFORE the first dereference, not after.
 *
 * NET-01 already added a reference around the datagram ring walk, but it
 * was taken well down the function -- after s->closed / s->rd_shut / s->tcp
 * had been read, and, for a stream socket, after tcp_recv() had been
 * entered and blocked.  A concurrent close() in that window frees the
 * struct out from under a sleeping reader, which is the same defect NET-01
 * fixed for the ring.  Taking the reference at entry closes the window for
 * every path through the function; the inner acquire/release stays as it
 * is (nested references are fine) so the ring code keeps working unchanged.
 */
static size_t afinet_node_read_body(fs_node_t *node, afi_sock_t *s,
                                    size_t size, uint8_t *buf);

/*
 * UDP-API-16: pin the socket behind a node.  node->impl used to be loaded
 * with no lock and the reference taken afterwards, so a close() on another
 * thread could drop the last reference and free the socket between the two
 * -- the reader or writer then incremented freed memory and later freed it
 * again (a UMA double-free panic, reproduced with a write racing close()).
 * The load and the reference are now one step under afi_lock, and close()
 * clears node->impl under the same lock.  NULL once the socket is gone.
 */
static afi_sock_t *afi_node_get(fs_node_t *node) {
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    afi_sock_t *s = (afi_sock_t *)(uintptr_t)node->impl;
    if (s) s->refcount++;
    spinlock_release_irq(&afi_lock, fl);
    return s;
}

size_t afinet_node_read(fs_node_t *node, off_t off, size_t size, uint8_t *buf) {
    (void)off;
    afi_sock_t *s = afi_node_get(node);
    if (!s) return 0;                   /* torn down: end of file */

    size_t r = afinet_node_read_body(node, s, size, buf);

    unsigned long fl1 = spinlock_acquire_irq(&afi_lock);
    afi_rele_unlock(s, fl1);            /* may free; s must not be touched */
    return r;
}

static size_t afinet_node_read_body(fs_node_t *node, afi_sock_t *s,
                                    size_t size, uint8_t *buf) {
    if (s->closed) return 0;
    if (s->rd_shut) return 0;            /* shutdown(SHUT_RD): EOF */
    int nb = afi_node_nonblock(node);
    if (s->type == SOCK_STREAM && s->tcp) {
        ssize_t n = nb ? tcp_recv_nb(s->tcp, buf, size)
                       : tcp_recv_until(s->tcp, buf, size, afi_deadline(s->rcv_timeo));
        /* Propagate errors as (size_t)-errno — the read() syscall
         * layer decodes them.  Collapsing a negative return to 0
         * here would forge a spurious EOF: a recv interrupted by a
         * signal (-EINTR) looked to userspace exactly like the peer
         * closing the connection. */
        return (size_t)n;
    }
    /* NET-01: serialise ring access against the hard-IRQ delivery path
     * and pin the socket with a reference so a concurrent close() cannot
     * free it while we are dequeuing or asleep.  The ring slot is copied
     * into a kernel-local buffer under the lock; the copy out to the
     * caller's (possibly user) buffer runs unlocked so a page fault there
     * is never taken with interrupts disabled. */
    uint64_t deadline = afi_deadline(s->rcv_timeo);
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    s->refcount++;
    for (;;) {
        if (s->count > 0) {
            afi_rec_t h;
            uint8_t tmp[AFI_DATA_MAX];
            rq_pop(s, &h, tmp, size < sizeof(tmp) ? size : sizeof(tmp), 1);
            size_t n = h.len < size ? h.len : size;
            afi_rele_unlock(s, fl);
            memcpy(buf, tmp, n);
            return n;
        }
        /* UDP-ICMP-01: read(2) reports a latched ICMP error exactly as
         * recv() does (afinet_recvfrom). */
        if (s->so_error) {
            int err = s->so_error;
            s->so_error = 0;
            afi_rele_unlock(s, fl);
            return (size_t)-err;
        }
        /* UDP-API-08: and a reader already asleep when shutdown(SHUT_RD)
         * arrives must wake to EOF, not go back to sleep. */
        if (s->rd_shut) { afi_rele_unlock(s, fl); return 0; }
        if (nb) { afi_rele_unlock(s, fl); return (size_t)-EAGAIN; }
        /* UDP-API-04: SO_RCVTIMEO expired. */
        if (deadline && get_ticks() >= deadline) {
            afi_rele_unlock(s, fl);
            return (size_t)-EAGAIN;
        }
        /* UDP-06: queue-then-release, and signal-interruptible so SIGINT
         * (and friends) yank ping/etc out of a blocked recv. */
        if (afi_wait(s, &fl) == -EINTR) {
            afi_rele_unlock(s, fl);
            return (size_t)-EINTR;
        }
        if (s->closed) { afi_rele_unlock(s, fl); return 0; }
    }
}

/*
 * UDP-03: compute the transmit checksum.
 *
 * Every UDP send path wrote `uh->check = 0` and left it there.  Over IPv4
 * that is legal-but-lazy (a zero checksum means "not computed", so silent
 * corruption of our datagrams went undetected on the wire); over IPv6 a zero
 * checksum is ILLEGAL (RFC 8200 8.1), so a conformant peer discarded every
 * v6 datagram we ever sent.  The primitives already existed and TCP, ICMPv6
 * and inet6 all used them -- UDP simply never did.
 *
 * The source address is the one routing will pick, which is why this needs
 * ip4_source_for()/ip6_source_for(): the socket does not know it.  RFC 768
 * reserves the value 0 to mean "no checksum", so a genuine 0 is transmitted
 * as 0xFFFF (the equivalent one's-complement representation).
 *
 * `dgram` covers the UDP header AND payload, so these must be called after
 * the payload has been copied in.
 */
/*
 * UDP-U-02/U-03: the source a datagram leaves with.  A socket bound to a
 * specific address sends from it -- bind()'s address used to be ignored on
 * transmit, so the peer saw (and replied to) whatever routing picked.  The
 * value is computed once and used for both the pseudo-header and the IP
 * header (ip4_output_from), so the two can never disagree.
 */
static uint32_t udp_src4(const afi_sock_t *s, uint32_t daddr) {
    uint32_t bound;
    memcpy(&bound, s->local_addr, 4);
    return bound ? bound : ip4_source_for(daddr);
}

static void udp_csum4(struct udphdr *uh, uint32_t saddr, uint32_t daddr,
                      size_t dgram_len) {
    uh->check = 0;
    uint16_t c = inet_csum_pseudo4(saddr, daddr, IPPROTO_UDP_NUM,
                                   (uint16_t)dgram_len, uh);
    uh->check = c ? c : 0xFFFF;
}

/* UDP-U-06: returns 0, or -ENETUNREACH when no source can be chosen.  It
 * used to return nothing and leave check == 0 in that case, relying on
 * ip6_output() to fail the send later -- but over IPv6 a zero UDP checksum
 * is illegal (RFC 8200 8.1), so no path may ever hand one to the output
 * routine.  The callers now abort the send instead. */
static int udp_csum6(struct udphdr *uh, const uint8_t daddr[16],
                     size_t dgram_len) {
    uint8_t saddr[16];
    uh->check = 0;
    if (ip6_source_for(daddr, saddr) != 0) return -ENETUNREACH;
    uint16_t c = inet_csum_pseudo6(saddr, daddr, IPPROTO_UDP_NUM,
                                   (uint32_t)dgram_len, uh);
    uh->check = c ? c : 0xFFFF;
    return 0;
}

/* SOCK-03 (write twin of afinet_node_read): tcp_send() blocks on a full
 * send window with nothing holding the socket, so pin at entry here too. */
static size_t afinet_node_write_body(fs_node_t *node, afi_sock_t *s,
                                     size_t size, const uint8_t *buf);

static size_t afinet_node_write(fs_node_t *node, off_t off, size_t size, const uint8_t *buf) {
    (void)off;
    /* UDP-API-16: a write to a torn-down socket is an error, not a
     * successful zero-byte transfer -- that spun every libc-style
     * "write until done" loop forever. */
    afi_sock_t *s = afi_node_get(node);
    if (!s) return (size_t)-EBADF;

    size_t r = afinet_node_write_body(node, s, size, buf);

    unsigned long fl1 = spinlock_acquire_irq(&afi_lock);
    afi_rele_unlock(s, fl1);            /* may free; s must not be touched */
    return r;
}

static size_t afinet_node_write_body(fs_node_t *node, afi_sock_t *s,
                                     size_t size, const uint8_t *buf) {
    if (s->closed) return (size_t)-EBADF;
    if (s->type == SOCK_STREAM && s->tcp) {
        ssize_t n = afi_node_nonblock(node) ? tcp_send_nb(s->tcp, buf, size)
                                            : tcp_send_until(s->tcp, buf, size, afi_deadline(s->snd_timeo));
        return (size_t)n;
    }
    /* UDP-API-19: after shutdown(SHUT_WR): EPIPE and SIGPIPE, as for a
     * pipe (write(2) has no MSG_NOSIGNAL). */
    if (s->wr_shut) {
        if (current_process) psignal(current_process, SIGPIPE);
        return (size_t)-EPIPE;
    }
    /* write() without an address only works on a connected DGRAM socket. */
    if (!s->connected) return (size_t)-EDESTADDRREQ;
    /* UDP-API-20: the raw arms below had no bound of their own. */
    if (s->type == SOCK_RAW && size > afi_max_payload(s->family, s->type))
        return (size_t)-EMSGSIZE;

    if (s->family == AF_INET) {
        if (s->type == SOCK_DGRAM) {
            uint8_t pkt[AFI_DATA_MAX + sizeof(struct udphdr)];
            if (size > afi_max_payload(s->family, s->type)) return (size_t)-EMSGSIZE;
            struct udphdr *uh = (struct udphdr *)pkt;
            /* NET-07: allocate via afinet_alloc_ephemeral() — the inline
             * ++g_ephemeral_next bypassed its wrap-to-49152 guard and is
             * non-atomic, yielding port 0 / low ports past 65535. */
            /* UDP-05: an implicit bind must pick a FREE port and record it
             * as bound, or the socket stays invisible to the collision
             * check and a later bind() can hand the same port out again. */
            if (!s->local_port) {
                uint16_t eph = afinet_alloc_ephemeral_free(s);
                if (eph == 0) return (size_t)-EADDRINUSE;
                s->local_port = eph;
                s->bound = 1;
            }
            uh->source = __builtin_bswap16(s->local_port);
            uh->dest   = __builtin_bswap16(s->peer_port);
            uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + size));
            memcpy(pkt + sizeof(*uh), buf, size);
            uint32_t daddr;
            memcpy(&daddr, s->peer_addr, 4);
            uint32_t saddr = udp_src4(s, daddr);
            udp_csum4(uh, saddr, daddr, sizeof(*uh) + size);
            int rc = ip4_output_opts(saddr, daddr, IPPROTO_UDP_NUM, pkt,
                                     sizeof(*uh) + size, &s->txo);
            if (rc >= 0) udp_stat_inc(UDP_STAT_OUT_DATAGRAMS);   /* UDP-RES-03 */
            return rc < 0 ? (size_t)rc : size;
        } else {
            uint32_t daddr;
            memcpy(&daddr, s->peer_addr, 4);
            int rc = ip4_output_opts(0, daddr, (uint8_t)s->protocol, buf, size,
                                     &s->txo);
            return rc < 0 ? (size_t)rc : size;
        }
    } else {
        if (s->type == SOCK_DGRAM) {
            uint8_t pkt[AFI_DATA_MAX + sizeof(struct udphdr)];
            if (size > afi_max_payload(s->family, s->type)) return (size_t)-EMSGSIZE;
            struct udphdr *uh = (struct udphdr *)pkt;
            /* NET-07: allocate via afinet_alloc_ephemeral() — the inline
             * ++g_ephemeral_next bypassed its wrap-to-49152 guard and is
             * non-atomic, yielding port 0 / low ports past 65535. */
            /* UDP-05: an implicit bind must pick a FREE port and record it
             * as bound, or the socket stays invisible to the collision
             * check and a later bind() can hand the same port out again. */
            if (!s->local_port) {
                uint16_t eph = afinet_alloc_ephemeral_free(s);
                if (eph == 0) return (size_t)-EADDRINUSE;
                s->local_port = eph;
                s->bound = 1;
            }
            uh->source = __builtin_bswap16(s->local_port);
            uh->dest   = __builtin_bswap16(s->peer_port);
            uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + size));
            memcpy(pkt + sizeof(*uh), buf, size);
            int rc = udp_csum6(uh, s->peer_addr, sizeof(*uh) + size);
            if (rc < 0) return (size_t)rc;
            rc = ip6_output(s->peer_addr, IPPROTO_UDP_NUM, pkt, sizeof(*uh) + size);
            if (rc >= 0) udp_stat_inc(UDP_STAT_OUT_DATAGRAMS);   /* UDP-RES-03 */
            return rc < 0 ? (size_t)rc : size;
        } else {
            int rc = ip6_output(s->peer_addr, (uint8_t)s->protocol, buf, size);
            return rc < 0 ? (size_t)rc : size;
        }
    }
}

static void afinet_node_close(fs_node_t *node) {
    /* UDP-API-16: detach under afi_lock, so afi_node_get() either pins the
     * socket first or sees it gone -- never a pointer about to be freed. */
    unsigned long cfl = spinlock_acquire_irq(&afi_lock);
    afi_sock_t *s = (afi_sock_t *)(uintptr_t)node->impl;
    node->impl = 0;
    spinlock_release_irq(&afi_lock, cfl);
    if (!s) return;
    /* tcp_close() serialises internally (its own IRQ-off critical
     * section) and may not run under afi_lock. */
    if (s->tcp) { tcp_close(s->tcp); s->tcp = NULL; }
    /* UDP-IP-06: give back this socket's group memberships, so the last
     * leave turns the NIC's all-multicast mode off again. */
    {
        uint32_t grp[AFI_MC_MAX];
        netdev_t *dev[AFI_MC_MAX];
        unsigned long mfl = spinlock_acquire_irq(&afi_lock);
        for (int i = 0; i < AFI_MC_MAX; i++) {
            grp[i] = s->mc_group[i];
            dev[i] = s->mc_dev[i];
            s->mc_group[i] = 0;
            s->mc_dev[i] = NULL;
        }
        spinlock_release_irq(&afi_lock, mfl);
        for (int i = 0; i < AFI_MC_MAX; i++)
            if (grp[i]) netdev_mc_leave(dev[i], grp[i]);
    }
    /* NET-01: mark closed, unlink from the delivery list, and drop the
     * install reference — all under afi_lock so the hard-IRQ delivery
     * path can neither be walking the list nor enqueuing into this
     * socket's ring while we unlink it.  The socket is freed here only
     * if no blocking reader still holds a reference; otherwise the last
     * afi_rele_unlock() (in the reader) frees it after it wakes. */
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    s->closed = 1;
    afi_sock_t **link = &g_afi_head;
    while (*link && *link != s) link = &(*link)->next;
    if (*link == s) *link = s->next;
    sched_wakeup(s->wait_chan);   /* wake readers so they re-check closed */
    afi_rele_unlock(s, fl);       /* drop install ref; frees if last */
}

/* ------------------------------------------------------------------ */
/* fd helpers                                                         */
/* ------------------------------------------------------------------ */

static int afi_install_fd(afi_sock_t *s) {
    int fd = proc_alloc_fd(current_process);
    if (fd < 0) return -1;
    file_t *f = file_alloc();
    if (!f) { proc_clear_fd(current_process, fd); return -1; }
    memset(f, 0, sizeof(*f));
    s->node.flags = FS_FILE;
    s->node.mask  = 0666;
    s->node.read  = afinet_node_read;
    s->node.write = afinet_node_write;
    s->node.close = afinet_node_close;
    s->node.ioctl = afinet_ioctl;
    s->node.poll  = afinet_node_poll;
    s->node.impl  = (uintptr_t)s;
    strlcpy(s->node.name, "<af_inet>", sizeof(s->node.name));
    f->f_data = &s->node;
    f->f_type = DTYPE_VNODE;
    f->f_flag = FREAD | FWRITE;
    f->f_count = 1;
    proc_set_fd(current_process, fd, f);
    return fd;
}

static afi_sock_t *afi_from_fd(int fd) {
    if (fd < 0 || fd >= MAX_FD) return NULL;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return NULL;
    fs_node_t *n = (fs_node_t *)f->f_data;
    if (n->read != afinet_node_read) return NULL;
    return (afi_sock_t *)(uintptr_t)n->impl;
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                */
/* ------------------------------------------------------------------ */

int afinet_socket(int family, int type, int protocol) {
    /* AF_INET6 is rejected at socket() time on purpose: substrate's
     * bind()/connect() only handle AF_INET sockaddrs, so accepting an
     * AF_INET6 socket here just defers the EAFNOSUPPORT to connect()
     * with a worse error path (ssh prints "connect to host  port :
     * Address family not supported").  Refusing it now lets
     * getaddrinfo-driven callers iterate straight to the AF_INET
     * result.  Lift this once inet6.c grows real v6 bind/connect. */
    if (family != AF_INET) return -EAFNOSUPPORT;
    if (type != SOCK_RAW && type != SOCK_DGRAM && type != SOCK_STREAM)
        return -EPROTONOSUPPORT;
    /*
     * UDP-07: a raw socket is a privileged object -- it reads every packet
     * of its protocol regardless of who they were for, and writes
     * caller-composed IP payloads straight onto the wire.  Creating one
     * required no privilege at all, so any unprivileged process could sniff
     * and forge.  (It is also the path the SOCK_RAW length bugs fixed under
     * #432 were reachable through.)  Every Unix restricts this to root; the
     * companion check for AF_PACKET is in af_packet.c.
     */
    if (type == SOCK_RAW && (!current_process || current_process->euid != 0))
        return -EACCES;
    /* Validate protocol against the type (POSIX): STREAM takes 0 or TCP,
     * DGRAM takes 0 or UDP; RAW takes any.  A bogus protocol is rejected
     * at socket() rather than silently ignored. */
    if (type == SOCK_STREAM && protocol != 0 && protocol != 6 /*TCP*/)
        return -EPROTONOSUPPORT;
    if (type == SOCK_DGRAM && protocol != 0 && protocol != IPPROTO_UDP_NUM)
        return -EPROTONOSUPPORT;
    if (type == SOCK_DGRAM && protocol == 0) protocol = IPPROTO_UDP_NUM;
    if (type == SOCK_STREAM && protocol == 0) protocol = 6 /*TCP*/;

    afi_sock_t *s = (afi_sock_t *)kmalloc(sizeof(*s));
    if (!s) return -ENOMEM;
    memset(s, 0, sizeof(*s));
    s->family = family;
    s->owner_uid = current_process ? current_process->euid : 0;
    ip4_txopts_init(&s->txo);
    s->type = type;
    s->protocol = protocol;
    s->refcount = 1;                 /* NET-01: the installed reference */
    s->wait_chan = &s->count;
    s->rcvbuf = AFI_RCVBUF_DEFAULT;
    /* UDP-RES-02: only a datagram or raw socket has a receive queue.  Every
     * AF_INET socket used to allocate a ~50 KiB contiguous ring, TCP
     * included, where it was never used. */
    if (type != SOCK_STREAM) {
        s->rq = (uint8_t *)kmalloc(AFI_RCVBUF_DEFAULT);
        if (!s->rq) { kfree(s, sizeof(*s)); return -ENOMEM; }
        s->rq_cap = AFI_RCVBUF_DEFAULT;
    }

    if (type == SOCK_STREAM) {
        s->tcp = tcp_alloc();
        if (!s->tcp) {
            kfree(s, sizeof(*s));
            return -ENOMEM;
        }
    }

    int fd = afi_install_fd(s);
    if (fd < 0) {
        if (s->tcp) tcp_free(s->tcp);
        afi_free_sock(s);
        return -EMFILE;
    }

    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    s->next = g_afi_head;
    g_afi_head = s;
    spinlock_release_irq(&afi_lock, fl);
    return fd;
}

/* True iff another live socket of the same family+type already has `port`
 * explicitly bound — the EADDRINUSE test, relaxed by SO_REUSEADDR. */
/* Is `port` bound by another socket of self's family and type?  The caller
 * holds afi_lock (NET-01: the list cannot be re-spliced mid-scan). */
static int afinet_port_taken_locked(const afi_sock_t *self, uint16_t port) {
    for (afi_sock_t *o = g_afi_head; o; o = o->next) {
        if (o == self || o->closed) continue;
        if (o->bound && o->local_port == port &&
            o->family == self->family && o->type == self->type)
            return 1;
    }
    return 0;
}


static int addr_is_wild(const uint8_t *a, size_t n);


/*
 * UDP-API-01: may `self` bind laddr:port?  Another bound socket of the same
 * family and type on the same port conflicts when their local addresses
 * overlap (either is the wildcard, or they are equal) -- unless BOTH set
 * SO_REUSEADDR and both belong to the same user.  Only the newcomer's flag
 * used to be consulted, so any process could bind on top of any other's
 * port simply by asking; and the address was ignored, so 127.0.0.1:P and
 * 127.0.0.2:P, which do not overlap, were refused.
 */
static int afinet_bind_conflict(const afi_sock_t *self, uint16_t port,
                                const uint8_t *laddr, size_t alen) {
    int conflict = 0;
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    for (afi_sock_t *o = g_afi_head; o; o = o->next) {
        if (o == self || o->closed || !o->bound || o->local_port != port) continue;
        if (o->family != self->family || o->type != self->type) continue;
        if (!addr_is_wild(laddr, alen) && !addr_is_wild(o->local_addr, alen) &&
            memcmp(laddr, o->local_addr, alen) != 0)
            continue;                                  /* disjoint addresses */
        if (self->reuseaddr && o->reuseaddr && self->owner_uid == o->owner_uid)
            continue;                                  /* consented sharing */
        conflict = 1;
        break;
    }
    spinlock_release_irq(&afi_lock, fl);
    return conflict;
}

/*
 * UDP-API-06: may a socket bind this IPv4 address?  Only one it can receive
 * on: the wildcard, an interface address, anything in 127/8 (lo takes all of
 * it, UDP-IP-02), an interface's broadcast or the limited broadcast, or a
 * class D group.  Any other address was accepted and left the socket
 * permanently deaf with no error.
 */
static int afinet_addr_bindable4(uint32_t a) {
    if (a == 0 || a == 0xFFFFFFFFu) return 1;
    if ((a & 0xFF) == 127 || ((a & 0xFF) >> 4) == 0xE) return 1;
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
        if (!d->ip4_addr) continue;
        if (a == d->ip4_addr) return 1;
        if (d->ip4_netmask &&
            a == ((d->ip4_addr & d->ip4_netmask) | ~d->ip4_netmask))
            return 1;
    }
    return 0;
}

/* The IPv6 counterpart: ::, ::1, an interface address, or multicast. */
static int afinet_addr_bindable6(const uint8_t a[16]) {
    static const uint8_t lo6[16] = { [15] = 1 };
    if (addr_is_wild(a, 16) || memcmp(a, lo6, 16) == 0 || a[0] == 0xff) return 1;
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d))
        if (memcmp(a, d->ip6_addr, 16) == 0) return 1;
    return 0;
}

/* UDP-API-15: is this IPv4 destination a broadcast -- limited, or some
 * interface's directed broadcast? */
static int afinet_is_bcast4(uint32_t a) {
    if (a == 0xFFFFFFFFu) return 1;
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d))
        if (d->ip4_addr && d->ip4_netmask &&
            a == ((d->ip4_addr & d->ip4_netmask) | ~d->ip4_netmask))
            return 1;
    return 0;
}

/* UDP-API-01 / TCP-API-18: ports below IPPORT_RESERVED belong to root. */
static int afinet_port_reserved(uint16_t port) {
    return port != 0 && port < 1024 &&
           (!current_process || current_process->euid != 0);
}

int afinet_bind(int fd, const void *addr, socklen_t len) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!addr) return -EINVAL;
    if (s->bound) return -EINVAL;            /* already bound */
    if (s->family == AF_INET) {
        if (len < (socklen_t)sizeof(struct sin_kern)) return -EINVAL;
        const struct sin_kern *sin = (const struct sin_kern *)addr;
        if (sin->sin_family != AF_INET) return -EAFNOSUPPORT;
        uint16_t req = __builtin_bswap16(sin->sin_port);
        if (afinet_port_reserved(req)) return -EACCES;
        if (!afinet_addr_bindable4(sin->sin_addr)) return -EADDRNOTAVAIL;
        uint8_t la[16];
        memset(la, 0, sizeof(la));
        memcpy(la, &sin->sin_addr, 4);
        if (req && afinet_bind_conflict(s, req, la, 4))
            return -EADDRINUSE;
        /* bind(port 0): assign an ephemeral port now so getsockname()
         * reflects it (POSIX/BSD) — see afinet_alloc_ephemeral(). */
        if (req) {
            s->local_port = req;
        } else {
            uint16_t eph = afinet_alloc_ephemeral_free(s);
            if (eph == 0) return -EADDRINUSE;
            s->local_port = eph;
        }
        memcpy(s->local_addr, &sin->sin_addr, 4);
        if (s->tcp) {
            uint32_t la; memcpy(&la, s->local_addr, 4);
            tcp_bind(s->tcp, la, s->local_port);
        }
    } else {
        if (len < (socklen_t)sizeof(struct sin6_kern)) return -EINVAL;
        const struct sin6_kern *sin6 = (const struct sin6_kern *)addr;
        if (sin6->sin6_family != AF_INET6) return -EAFNOSUPPORT;
        uint16_t req6 = __builtin_bswap16(sin6->sin6_port);
        if (afinet_port_reserved(req6)) return -EACCES;
        if (!afinet_addr_bindable6(sin6->sin6_addr)) return -EADDRNOTAVAIL;
        if (req6 && afinet_bind_conflict(s, req6, sin6->sin6_addr, 16))
            return -EADDRINUSE;
        s->local_port = req6;
        if (s->local_port == 0) {
            uint16_t eph = afinet_alloc_ephemeral_free(s);
            if (eph == 0) return -EADDRINUSE;
            s->local_port = eph;
        }
        memcpy(s->local_addr, sin6->sin6_addr, 16);
        /* tcp_pcb_t is IPv4-only today; the v6 bind still has to
         * propagate the PORT into the PCB or the LISTEN socket
         * ends up with lport=0 and incoming SYNs (which all arrive
         * as v4 segments — we don't have v6 TCP transport yet)
         * never match.  Bind to v4-wildcard for the same port so
         * IPv4 traffic on the bound port reaches this PCB.  This
         * mirrors how glibc on Linux turns a `[::]:23` bind into a
         * dual-stack listener.  When v6 TCP lands, replace this
         * with a real v6 bind path.  */
        if (s->tcp) {
            tcp_bind(s->tcp, 0, s->local_port);
        }
    }
    s->bound = 1;
    return 0;
}

/* SO_REUSEADDR plumbing for the getsockopt/setsockopt dispatch in
 * af_unix.c.  Both no-op (return -ENOTSOCK) on a non-AF_INET fd. */
/*
 * UDP-IP-06: IP_ADD_MEMBERSHIP / IP_DROP_MEMBERSHIP.  setsockopt used to
 * return 0 for both while recording nothing (UDP-I-04), so every multicast
 * application's error path was dead and the symptom was a silent absence of
 * datagrams.  The interface is chosen by index (struct ip_mreqn), by
 * address, or -- for INADDR_ANY -- is the first UP multicast-capable one.
 */
int afinet_mc_membership(int fd, int add, uint32_t group, uint32_t ifaddr,
                         int ifindex) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (s->family != AF_INET || (s->type != SOCK_DGRAM && s->type != SOCK_RAW))
        return -EINVAL;
    if (((group & 0xFF) >> 4) != 0xE) return -EINVAL;

    netdev_t *dev = NULL;
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
        if (!(d->flags & NETDEV_IFF_MULTICAST) || (d->flags & NETDEV_IFF_LOOPBACK))
            continue;
        if (ifindex > 0 ? d->ifindex == (uint32_t)ifindex
                        : ifaddr ? d->ip4_addr == ifaddr
                                 : (d->flags & NETDEV_IFF_UP) != 0) {
            dev = d;
            break;
        }
    }
    if (!dev) return ifindex > 0 ? -ENODEV : -EADDRNOTAVAIL;

    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    int at = -1, freeslot = -1;
    for (int i = 0; i < AFI_MC_MAX; i++) {
        if (s->mc_group[i] == group && s->mc_dev[i] == dev) at = i;
        else if (!s->mc_group[i] && freeslot < 0) freeslot = i;
    }
    if (add) {
        if (at >= 0) { spinlock_release_irq(&afi_lock, fl); return -EADDRINUSE; }
        if (freeslot < 0) { spinlock_release_irq(&afi_lock, fl); return -ENOBUFS; }
        s->mc_group[freeslot] = group;
        s->mc_dev[freeslot] = dev;
        spinlock_release_irq(&afi_lock, fl);
        int rc = netdev_mc_join(dev, group);
        if (rc < 0) {
            fl = spinlock_acquire_irq(&afi_lock);
            s->mc_group[freeslot] = 0;
            s->mc_dev[freeslot] = NULL;
            spinlock_release_irq(&afi_lock, fl);
        }
        return rc;
    }
    if (at < 0) { spinlock_release_irq(&afi_lock, fl); return -EADDRNOTAVAIL; }
    s->mc_group[at] = 0;
    s->mc_dev[at] = NULL;
    spinlock_release_irq(&afi_lock, fl);
    return netdev_mc_leave(dev, group);
}

/*
 * UDP-API-12: IP_TOS, IP_TTL and the IP_MULTICAST_* options.  setsockopt()
 * used to return 0 for all of them and record nothing: every datagram left
 * with TTL 64 and TOS 0, getsockopt(IP_TTL) answered 0, and a multicast
 * sender could neither widen its scope nor keep its own group sends from
 * looping back.  A TCP socket's options are pushed into its PCB.
 */
int afinet_set_ipopt(int fd, int optname, int val, uint32_t addr) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (s->family != AF_INET) return -ENOPROTOOPT;
    if (optname == 8) {         /* UDP-API-11: IP_PKTINFO */
        s->pktinfo = val ? 1 : 0;
        return 0;
    }
    struct ip4_txopts o = s->txo;
    switch (optname) {
    case 1:  /* IP_TOS */
        o.tos = (uint8_t)val;
        break;
    case 2:  /* IP_TTL: 1..255, or -1 for the default (Linux) */
        if (val == -1) val = 64;
        if (val < 1 || val > 255) return -EINVAL;
        o.ttl = (uint8_t)val;
        break;
    case 32: /* IP_MULTICAST_IF */
        if (addr) {
            int found = 0;
            for (netdev_t *d = netdev_first(); d; d = netdev_next(d))
                if ((d->flags & NETDEV_IFF_MULTICAST) && d->ip4_addr == addr)
                    found = 1;
            if (!found) return -EADDRNOTAVAIL;
        }
        o.mcast_if = addr;
        break;
    case 33: /* IP_MULTICAST_TTL: 0..255, or -1 for the default */
        if (val == -1) val = 1;
        if (val < 0 || val > 255) return -EINVAL;
        o.mcast_ttl = (uint8_t)val;
        break;
    case 34: /* IP_MULTICAST_LOOP */
        o.mcast_loop = val ? 1 : 0;
        break;
    default:
        return -ENOPROTOOPT;
    }
    s->txo = o;
    if (s->tcp) tcp_set_txopts(s->tcp, &o);
    return 0;
}

/*
 * TCP-WIN-13: IPPROTO_TCP options.  TCP_USER_TIMEOUT (18, RFC 5482; the
 * Linux number) sets RFC 793's per-connection user timeout in milliseconds.
 * -ENOTSOCK when fd is not an AF_INET socket, -ENOPROTOOPT for anything
 * else, including a non-stream socket.
 */
int afinet_set_tcpopt(int fd, int optname, int val) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (s->type != SOCK_STREAM || !s->tcp) return -ENOPROTOOPT;
    switch (optname) {
    case 18: /* TCP_USER_TIMEOUT */
        if (val < 0) return -EINVAL;
        return tcp_set_user_timeout(s->tcp, (uint32_t)val);
    default:
        return -ENOPROTOOPT;
    }
}

int afinet_get_tcpopt(int fd, int optname, int *val) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (s->type != SOCK_STREAM || !s->tcp) return -ENOPROTOOPT;
    switch (optname) {
    case 18: *val = (int)tcp_get_user_timeout(s->tcp); return 0;
    default: return -ENOPROTOOPT;
    }
}

int afinet_get_ipopt(int fd, int optname, int *val, uint32_t *addr) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (s->family != AF_INET) return -ENOPROTOOPT;
    *addr = 0;
    switch (optname) {
    case 8:  *val = s->pktinfo; break;          /* UDP-API-11 */
    case 1:  *val = s->txo.tos; break;
    case 2:  *val = s->txo.ttl; break;
    case 32: *val = 0; *addr = s->txo.mcast_if; break;
    case 33: *val = s->txo.mcast_ttl; break;
    case 34: *val = s->txo.mcast_loop; break;
    default: return -ENOPROTOOPT;
    }
    return 0;
}

/*
 * UDP-API-14 / UDP-RES-01: what SO_RCVBUF / SO_SNDBUF report for an AF_INET
 * socket -- the capacity the implementation actually has, not a number
 * borrowed from AF_UNIX.  A datagram socket's receive queue holds SO_RCVBUF
 * bytes of records and it sends one datagram at a time; a stream socket has
 * TCP's 32 KiB ring.  -ENOTSOCK on a non-AF_INET fd.
 */
int afinet_bufsize(int fd, int rcv) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (s->type == SOCK_STREAM) return 32 * 1024;
    return rcv ? (int)s->rcvbuf : AFI_DATA_MAX;
}

/*
 * UDP-RES-01: SO_RCVBUF on a datagram socket.  Growing reallocates the ring,
 * moving any queued records to the front of the new one; shrinking lowers
 * the admission limit and keeps what is already queued.  Clamped to
 * [AFI_RCVBUF_MIN, AFI_RCVBUF_MAX].  (A stream socket's TCP ring is fixed.)
 */
int afinet_set_rcvbuf(int fd, int val) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    uint32_t want = val < (int)AFI_RCVBUF_MIN ? AFI_RCVBUF_MIN
                  : (uint32_t)val > AFI_RCVBUF_MAX ? AFI_RCVBUF_MAX : (uint32_t)val;
    want = (want + 3u) & ~3u;
    if (!s->rq) { s->rcvbuf = want; return 0; }
    if (want <= s->rq_cap) {
        unsigned long fl = spinlock_acquire_irq(&afi_lock);
        s->rcvbuf = want;
        spinlock_release_irq(&afi_lock, fl);
        return 0;
    }
    uint8_t *nb = (uint8_t *)kmalloc(want);
    if (!nb) return -ENOBUFS;
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    uint8_t *old = s->rq;
    uint32_t oldcap = s->rq_cap;
    rq_get(s, s->rq_tail, nb, s->rq_used);           /* linearize */
    s->rq = nb;
    s->rq_cap = want;
    s->rq_tail = 0;
    s->rq_head = s->rq_used;
    s->rcvbuf = want;
    spinlock_release_irq(&afi_lock, fl);
    kfree(old, oldcap);
    return 0;
}

/* UDP-API-04: SO_RCVTIMEO / SO_SNDTIMEO. */
int afinet_set_timeo(int fd, int rcv, int64_t sec, int64_t usec) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (sec < 0 || usec < 0 || usec >= 1000000) return -EDOM;
    uint64_t hz = get_hz();
    if (!hz) hz = 100;
    uint64_t ticks = (uint64_t)sec * hz + ((uint64_t)usec * hz + 999999u) / 1000000u;
    if (ticks > 0xFFFFFFFFu) ticks = 0xFFFFFFFFu;
    if (rcv) s->rcv_timeo = (uint32_t)ticks;
    else     s->snd_timeo = (uint32_t)ticks;
    return 0;
}

int afinet_get_timeo(int fd, int rcv, int64_t *sec, int64_t *usec) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    uint64_t hz = get_hz();
    if (!hz) hz = 100;
    uint64_t t = rcv ? s->rcv_timeo : s->snd_timeo;
    *sec = (int64_t)(t / hz);
    *usec = (int64_t)((t % hz) * 1000000u / hz);
    return 0;
}

/* UDP-API-15: SO_BROADCAST. */
int afinet_set_broadcast(int fd, int on) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    s->broadcast = on ? 1 : 0;
    return 0;
}

int afinet_get_broadcast(int fd) {
    afi_sock_t *s = afi_from_fd(fd);
    return s ? s->broadcast : -ENOTSOCK;
}

int afinet_set_reuseaddr(int fd, int on) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    s->reuseaddr = on ? 1 : 0;
    return 0;
}
int afinet_get_reuseaddr(int fd) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    return s->reuseaddr;
}

int afinet_listen(int fd, int backlog) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!s->tcp) return -EOPNOTSUPP;
    return tcp_listen(s->tcp, backlog);
}

/* shutdown(2) for AF_INET sockets.  SHUT_RD forces recv() to EOF;
 * SHUT_WR sends a FIN (half-close) so the peer sees EOF while this
 * socket can still read.  Routed here from sys_shutdown(). */
int afinet_shutdown(int fd, int how) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR)
        return -EINVAL;
    /*
     * SOCK-10: POSIX requires ENOTCONN when the socket is not connected.
     * The check only covered TCP, so shutdown() on an unconnected datagram
     * or raw socket reported success and did nothing -- a caller using the
     * return value to decide whether a teardown happened was misled.  A
     * datagram socket becomes "connected" via connect(), same as a stream.
     */
    if (!s->connected) return -ENOTCONN;
    if (how == SHUT_RD || how == SHUT_RDWR) {
        s->rd_shut = 1;
        sched_wakeup(s->wait_chan);          /* UDP/RAW blocked readers */
        if (s->tcp) tcp_shutdown_rd(s->tcp); /* TCP: EOF + wake reader */
    }
    if (how == SHUT_WR || how == SHUT_RDWR) {
        /* UDP-API-19: a datagram socket's write side shuts too.  This was a
         * silent no-op -- sends after SHUT_WR went out as if nothing had
         * happened. */
        s->wr_shut = 1;
        if (s->tcp) tcp_shutdown_wr(s->tcp);
    }
    return 0;
}

static int afinet_pack_sockaddr(int family, uint16_t hport,
                                const uint8_t addr_bytes[16],
                                void *out, socklen_t *outlen);

int afinet_accept(int fd, void *addr, socklen_t *addrlen) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!s->tcp) return -EOPNOTSUPP;
    /* accept() is only valid on a listening socket. */
    if (!tcp_is_listening(s->tcp)) return -EINVAL;
    /* Honour O_NONBLOCK from the listening fd: an empty accept queue
     * then returns EAGAIN instead of blocking the caller. */
    int nonblock = 0;
    if (fd >= 0 && fd < MAX_FD && current_process) {
        file_t *lf = current_process->fds[fd];
        if (lf && (lf->f_flag & FNONBLOCK)) nonblock = 1;
    }
    tcp_pcb_t *cp = tcp_accept(s->tcp, nonblock);
    if (!cp) return nonblock ? -EAGAIN : -EINTR;

    /* Allocate a new afi_sock_t wrapping the accepted PCB. */
    afi_sock_t *c = (afi_sock_t *)kmalloc(sizeof(*c));
    if (!c) { tcp_close(cp); return -ENOMEM; }
    memset(c, 0, sizeof(*c));
    c->family = s->family;
    c->owner_uid = s->owner_uid;
    c->txo = s->txo;
    c->type = SOCK_STREAM;
    c->protocol = 6;
    c->refcount = 1;                 /* NET-01: the installed reference */
    c->wait_chan = &c->count;          /* UDP-RES-02: a stream has no rq */
    c->rcvbuf = AFI_RCVBUF_DEFAULT;
    c->tcp = cp;

    /* Copy the established connection's endpoints from the accepted
     * PCB into the socket so getpeername()/getsockname() work.  Both
     * peer_addr and laddr/raddr are stored network-byte-order;
     * lport/rport are host-order in the PCB, which is exactly what
     * afinet_pack_sockaddr expects.  Without this the accepted
     * socket has connected==0 and getpeername returns ENOTCONN —
     * which broke sshd-session ("getpeername failed: Transport
     * endpoint is not connected"). */
    {
        uint32_t laddr = 0, raddr = 0;
        uint16_t lport = 0, rport = 0;
        tcp_endpoints(cp, &laddr, &lport, &raddr, &rport);
        c->connected  = 1;
        c->peer_port  = rport;
        memcpy(c->peer_addr,  &raddr, 4);
        c->local_port = lport;
        memcpy(c->local_addr, &laddr, 4);
    }

    int newfd = afi_install_fd(c);
    if (newfd < 0) {
        if (c->rq) kfree(c->rq, c->rq_cap);
        kfree(c, sizeof(*c));
        tcp_close(cp);
        return -EMFILE;
    }
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    c->next = g_afi_head;
    g_afi_head = c;
    spinlock_release_irq(&afi_lock, fl);

    /*
     * Fill the accept() out-param with the peer's address, BSD/POSIX
     * convention.  addr may be NULL if the caller doesn't want it.
     *
     * SOCK-01: addr/addrlen are raw userspace pointers straight off the
     * syscall table.  This used to pack the sockaddr directly through `addr`
     * and assign through `*addrlen`, so
     *     accept(lfd, (void *)0xC0100000, &len)
     * wrote a sockaddr wherever the caller pointed -- an arbitrary kernel
     * write from an unprivileged process -- and an addrlen pointing into
     * kernel memory read it back out.  Build in a kernel buffer, then copy
     * out under length validation.
     */
    {
        uint8_t kaddr[SOCK_UADDR_MAX];
        socklen_t klen = sizeof(kaddr);

        memset(kaddr, 0, sizeof(kaddr));
        if (afinet_pack_sockaddr(c->family, c->peer_port, c->peer_addr,
                                 kaddr, &klen) == 0) {
            if (klen > (socklen_t)sizeof(kaddr)) klen = sizeof(kaddr);
            /* A bad user pointer must not cost us the connection we just
             * accepted: report the failure but keep the fd installed, since
             * the peer is already connected and unwinding it here would
             * silently drop an established connection. */
            (void)sock_copyout_sockaddr(kaddr, klen, addr, addrlen);
        }
    }
    return newfd;
}

/*
 * Userspace boundary helpers shared by the socket syscalls.  See the
 * commentary in <net/inet.h> for why these exist; the short version is that
 * accept/sendto/recvfrom were writing and reading through raw user pointers,
 * which is an arbitrary kernel write and an unrecoverable kernel fault on a
 * bad pointer respectively.
 */
int sock_copyin_addrlen(const socklen_t *ulen, socklen_t *out)
{
    socklen_t v;

    if (!ulen || !out) return -EINVAL;
    if (copyin(ulen, &v, sizeof(v)) != 0) return -EFAULT;
    /* socklen_t is signed on some ABIs and userspace controls this value;
     * a negative capacity must not become a huge unsigned copy length. */
    if ((int)v < 0) return -EINVAL;
    *out = v;
    return 0;
}

int sock_copyout_sockaddr(const void *src, socklen_t srclen,
                          void *addr, socklen_t *ulen)
{
    socklen_t cap, cpy;
    int rc;

    /* accept(2): a caller that does not want the peer address passes NULL. */
    if (!addr || !ulen) return 0;
    if (!src) return -EINVAL;

    rc = sock_copyin_addrlen(ulen, &cap);
    if (rc != 0) return rc;

    cpy = (cap < srclen) ? cap : srclen;
    if (cpy > 0 && copyout(src, addr, cpy) != 0) return -EFAULT;
    /* Untruncated length, per POSIX -- see the header comment. */
    if (copyout(&srclen, ulen, sizeof(srclen)) != 0) return -EFAULT;
    return 0;
}

/* Common helper: fill a struct sockaddr_in / sockaddr_in6 from
 * a (port, addr-bytes) pair, honouring the buffer size hint
 * conventional to getsockname/getpeername.  */
static int afinet_pack_sockaddr(int family, uint16_t hport,
                                const uint8_t addr_bytes[16],
                                void *out, socklen_t *outlen)
{
    if (!out || !outlen) return -EINVAL;
    if (family == AF_INET) {
        struct sin_kern sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port   = __builtin_bswap16(hport);
        memcpy(&sin.sin_addr, addr_bytes, 4);
        socklen_t want = sizeof(sin);
        socklen_t cpy  = (*outlen < want) ? *outlen : want;
        memcpy(out, &sin, cpy);
        *outlen = want;
    } else {
        struct sin6_kern sin6;
        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_family = AF_INET6;
        sin6.sin6_port   = __builtin_bswap16(hport);
        memcpy(sin6.sin6_addr, addr_bytes, 16);
        socklen_t want = sizeof(sin6);
        socklen_t cpy  = (*outlen < want) ? *outlen : want;
        memcpy(out, &sin6, cpy);
        *outlen = want;
    }
    return 0;
}

/* Read-and-clear the per-socket pending-error.  Mirrors BSD
 * SO_ERROR semantics: each getsockopt returns the latest error and
 * resets the slot.  For TCP we also pull the value from the PCB
 * (set by tcp_kill_pcb when the connection failed).  */


int afinet_so_error(int fd) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (s->tcp) return tcp_take_so_error(s->tcp);
    /* UDP-ICMP-01: this used to be a hard 0 for every datagram socket, so
     * an ICMP error could never be observed through SO_ERROR.  Reading it
     * clears it, as on BSD and Linux. */
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    int err = s->so_error;
    s->so_error = 0;
    spinlock_release_irq(&afi_lock, fl);
    return err;
}

/* SO_TYPE for an AF_INET/AF_INET6 fd: SOCK_STREAM / SOCK_DGRAM / SOCK_RAW, or
 * -ENOTSOCK if the fd is not one of ours. */
int afinet_so_type(int fd) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    return s->type;
}

int afinet_getsockname(int fd, void *addr, socklen_t *addrlen) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    return afinet_pack_sockaddr(s->family, s->local_port, s->local_addr,
                                addr, addrlen);
}

int afinet_getpeername(int fd, void *addr, socklen_t *addrlen) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    /* afinet_connect sets s->connected on both UDP and TCP paths;
     * for TCP it's set right after tcp_connect{,_nb} returns
     * success (or EINPROGRESS).  Good enough as a "has-a-peer"
     * signal for the rare callers that bother checking.  */
    if (!s->connected) return -ENOTCONN;
    return afinet_pack_sockaddr(s->family, s->peer_port, s->peer_addr,
                                addr, addrlen);
}

int afinet_connect(int fd, const void *addr, socklen_t len) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!addr) return -EINVAL;
    /* A connected stream socket cannot be reconnected — return EISCONN
     * immediately rather than attempting another handshake (which wedged
     * the caller forever). */
    if (s->tcp && s->connected) return -EISCONN;

    /* Honour O_NONBLOCK on the underlying fd — clients like curl
     * fcntl() the socket non-blocking and then expect connect() to
     * return -EINPROGRESS so they can poll for POLLOUT.  The kernel
     * canonicalises O_NONBLOCK to FNONBLOCK internally (see
     * proc_apply_status_flags); check that, not the userland bit.  */
    file_t *f = (fd >= 0 && fd < MAX_FD) ? current_process->fds[fd] : NULL;
    int nonblock = f && (f->f_flag & FNONBLOCK);

    if (s->family == AF_INET) {
        if (len < (socklen_t)sizeof(struct sin_kern)) return -EINVAL;
        const struct sin_kern *sin = (const struct sin_kern *)addr;
        if (sin->sin_family != AF_INET) return -EAFNOSUPPORT;
        /* UDP-U-04: RFC 768 reserves port 0 as "no port"; a datagram socket
         * cannot be connected to it. */
        if (s->type == SOCK_DGRAM && sin->sin_port == 0) return -EINVAL;
        /* UDP-API-15: nor to a broadcast address without SO_BROADCAST. */
        if (s->type == SOCK_DGRAM && !s->broadcast && afinet_is_bcast4(sin->sin_addr))
            return -EACCES;
        s->peer_port = __builtin_bswap16(sin->sin_port);
        memcpy(s->peer_addr, &sin->sin_addr, 4);
        if (s->tcp) {
            uint32_t ra; memcpy(&ra, s->peer_addr, 4);
            int rc = nonblock ? tcp_connect_nb(s->tcp, ra, s->peer_port)
                              : tcp_connect   (s->tcp, ra, s->peer_port);
            /* -EINPROGRESS is the success-but-async return for the
             * non-blocking path.  Record the peer + the connected
             * state BEFORE returning so a follow-up sendto() sees
             * s->connected and uses the stored peer_addr/port,
             * rather than failing with EDESTADDRREQ. */
            if (rc < 0 && rc != -EINPROGRESS) return rc;
            /* Sync the kernel-assigned local endpoint back so getsockname()
             * reflects the ephemeral local port the SYN used; otherwise it
             * reports 0 while the connection runs on the real port and the
             * server's getpeername() can't match the client's local port. */
            {
                uint32_t la = 0, ra2 = 0; uint16_t lp = 0, rp = 0;
                tcp_endpoints(s->tcp, &la, &lp, &ra2, &rp);
                s->local_port = lp;
                memcpy(s->local_addr, &la, 4);
            }
            if (rc == -EINPROGRESS) {
                s->connected = 1;
                return -EINPROGRESS;
            }
        }
    } else {
        if (len < (socklen_t)sizeof(struct sin6_kern)) return -EINVAL;
        const struct sin6_kern *sin6 = (const struct sin6_kern *)addr;
        if (sin6->sin6_family != AF_INET6) return -EAFNOSUPPORT;
        if (s->type == SOCK_DGRAM && sin6->sin6_port == 0) return -EINVAL;
        s->peer_port = __builtin_bswap16(sin6->sin6_port);
        memcpy(s->peer_addr, sin6->sin6_addr, 16);
    }
    /*
     * UDP-API-07: connecting an unbound datagram socket binds it, as on BSD
     * and Linux -- an ephemeral port and the source address routing picks
     * toward the peer.  It stayed at port 0 until its first send, so it
     * could not receive a peer that spoke first, and getsockname()
     * reported 0.
     */
    if (s->type == SOCK_DGRAM && s->local_port == 0) {
        uint16_t eph = afinet_alloc_ephemeral_free(s);
        if (eph == 0) return -EADDRINUSE;
        s->local_port = eph;
        s->bound = 1;
        if (addr_is_wild(s->local_addr, s->family == AF_INET ? 4 : 16)) {
            if (s->family == AF_INET) {
                uint32_t src = ip4_source_for(*(const uint32_t *)s->peer_addr);
                memcpy(s->local_addr, &src, 4);
            } else {
                (void)ip6_source_for(s->peer_addr, s->local_addr);
            }
        }
    }
    s->connected = 1;
    return 0;
}

/*
 * Kernel-buffer core of afinet_sendto().  `buf` MUST already be kernel
 * memory: everything below memcpy()s it into a packet or hands it to
 * tcp_send / ip4_output / ip6_output, none of which can take a fault.  The
 * public entry point below is what copies the caller's payload in.
 */
static ssize_t afinet_sendto_k(int fd, const void *buf, size_t len, int flags,
                               const void *addr, socklen_t addrlen) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    /* UDP-API-19: after shutdown(SHUT_WR) a datagram send fails EPIPE, with
     * SIGPIPE unless the caller passed MSG_NOSIGNAL. */
    if (s->wr_shut && !(s->type == SOCK_STREAM && s->tcp)) {
        if (!(flags & MSG_NOSIGNAL) && current_process)
            psignal(current_process, SIGPIPE);
        return -EPIPE;
    }
    if (!buf && len) return -EINVAL;   /* len==0 is a valid empty datagram */
    /* TCP: connected stream socket goes through the tcp_send queue
     * regardless of whether the caller passed an addr.  Previously
     * a foot-gun — curl uses sendto() with addr=NULL on a connected
     * stream, and we fell into the UDP path below, looking for a
     * dest addr that wasn't there.  */
    if (s->type == SOCK_STREAM && s->tcp) {
        return tcp_send_until(s->tcp, buf, len, afi_deadline(s->snd_timeo));
    }

    /* Resolve target addr/port.
     *
     * UDP-U-01: an address the caller names wins, connected or not.  This
     * used to test s->connected first and parse addr only on the else arm,
     * so after connect() every sendto() silently went to the connected peer
     * -- RFC 768's send operation specifies the destination, and a resolver
     * retargeting a second server re-queried the first.  This is what Linux
     * does; the peer is the default only when no address is given. */
    uint16_t dport = s->peer_port;
    uint8_t  daddr_buf[16];
    if (addr) {
        if (s->family == AF_INET) {
            if (addrlen < (socklen_t)sizeof(struct sin_kern)) return -EINVAL;
            const struct sin_kern *sin = (const struct sin_kern *)addr;
            if (sin->sin_family != AF_INET) return -EAFNOSUPPORT;
            dport = __builtin_bswap16(sin->sin_port);
            memcpy(daddr_buf, &sin->sin_addr, 4);
        } else {
            if (addrlen < (socklen_t)sizeof(struct sin6_kern)) return -EINVAL;
            const struct sin6_kern *sin6 = (const struct sin6_kern *)addr;
            if (sin6->sin6_family != AF_INET6) return -EAFNOSUPPORT;
            dport = __builtin_bswap16(sin6->sin6_port);
            memcpy(daddr_buf, sin6->sin6_addr, 16);
        }
    } else if (s->connected) {
        memcpy(daddr_buf, s->peer_addr, 16);
    } else {
        return -EDESTADDRREQ;
    }
    /* UDP-U-04: never put destination port 0 -- RFC 768's "no port" -- on
     * the wire. */
    if (s->type == SOCK_DGRAM && dport == 0) return -EINVAL;
    /* UDP-API-15: a broadcast needs SO_BROADCAST (BSD and Linux both fail
     * EACCES), so a program cannot flood the segment by mistyping an
     * address.  The option was not even stored. */
    if (s->family == AF_INET && s->type != SOCK_STREAM && !s->broadcast &&
        afinet_is_bcast4(*(const uint32_t *)daddr_buf))
        return -EACCES;

    /* RAW: caller writes the L4 (and for v4 RAW with IP_HDRINCL it'd be
     * the IP header too — not supported yet; we always synthesize the
     * v4 IP header). */
    if (s->type == SOCK_RAW && len > afi_max_payload(s->family, s->type))
        return -EMSGSIZE;                               /* UDP-API-20 */
    if (s->family == AF_INET) {
        if (s->type == SOCK_RAW) {
            uint32_t d;
            memcpy(&d, daddr_buf, 4);
            int rc = ip4_output_opts(0, d, (uint8_t)s->protocol, buf, len, &s->txo);
            return rc < 0 ? rc : (ssize_t)len;
        }
        /* DGRAM/UDP */
        /* NET-07: use the wrap-guarded ephemeral allocator, not the raw
         * (non-atomic, unguarded) ++g_ephemeral_next. */
        /* UDP-05: as above -- free port, recorded, marked bound. */
        if (!s->local_port) {
            uint16_t eph = afinet_alloc_ephemeral_free(s);
            if (eph == 0) return -EADDRINUSE;
            s->local_port = eph;
            s->bound = 1;
        }
        uint16_t sport = s->local_port;
        uint8_t pkt[AFI_DATA_MAX + sizeof(struct udphdr)];
        if (len > afi_max_payload(s->family, s->type)) return -EMSGSIZE;
        struct udphdr *uh = (struct udphdr *)pkt;
        uh->source = __builtin_bswap16(sport);
        uh->dest   = __builtin_bswap16(dport);
        uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + len));
        memcpy(pkt + sizeof(*uh), buf, len);
        uint32_t d;
        memcpy(&d, daddr_buf, 4);
        uint32_t src = udp_src4(s, d);
        udp_csum4(uh, src, d, sizeof(*uh) + len);
        int rc = ip4_output_opts(src, d, IPPROTO_UDP_NUM, pkt, sizeof(*uh) + len,
                                 &s->txo);
        if (rc >= 0) udp_stat_inc(UDP_STAT_OUT_DATAGRAMS);   /* UDP-RES-03 */
        return rc < 0 ? rc : (ssize_t)len;
    } else {
        if (s->type == SOCK_RAW) {
            int rc = ip6_output(daddr_buf, (uint8_t)s->protocol, buf, len);
            return rc < 0 ? rc : (ssize_t)len;
        }
        /* NET-07: use the wrap-guarded ephemeral allocator, not the raw
         * (non-atomic, unguarded) ++g_ephemeral_next. */
        /* UDP-05: as above -- free port, recorded, marked bound. */
        if (!s->local_port) {
            uint16_t eph = afinet_alloc_ephemeral_free(s);
            if (eph == 0) return -EADDRINUSE;
            s->local_port = eph;
            s->bound = 1;
        }
        uint16_t sport = s->local_port;
        uint8_t pkt[AFI_DATA_MAX + sizeof(struct udphdr)];
        if (len > afi_max_payload(s->family, s->type)) return -EMSGSIZE;
        struct udphdr *uh = (struct udphdr *)pkt;
        uh->source = __builtin_bswap16(sport);
        uh->dest   = __builtin_bswap16(dport);
        uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + len));
        memcpy(pkt + sizeof(*uh), buf, len);
        int rc = udp_csum6(uh, daddr_buf, sizeof(*uh) + len);
        if (rc < 0) return rc;
        rc = ip6_output(daddr_buf, IPPROTO_UDP_NUM, pkt, sizeof(*uh) + len);
        if (rc >= 0) udp_stat_inc(UDP_STAT_OUT_DATAGRAMS);   /* UDP-RES-03 */
        return rc < 0 ? rc : (ssize_t)len;
    }
}

/*
 * SOCK-02: bounce the caller's payload into kernel memory before it reaches
 * the transmit path.
 *
 * send/sendto/sendmsg used to hand the raw user pointer all the way down to
 * memcpy(pkt + sizeof(*uh), buf, len) / ip4_output() / tcp_send(), so
 *     sendto(fd, (void *)0xC0000000, 1400, 0, &dst, 16)
 * put 1400 bytes of kernel memory on the wire -- remote kernel memory
 * disclosure from an unprivileged process -- and an unmapped buf took an
 * unrecoverable kernel fault ("Unhandled Kernel Exception") instead of
 * returning EFAULT.  write(2) was never affected because kern_write()
 * already bounces.
 *
 * A datagram must be copied whole (it is one packet, and the per-family size
 * limits below reject anything oversized anyway); a stream may be chunked,
 * which is what keeps a multi-megabyte TCP send working without a
 * multi-megabyte kernel allocation.
 */
#define AFI_SEND_CHUNK (64U * 1024U)

ssize_t afinet_sendto_kbuf(int fd, const void *kbuf, size_t len, int flags,
                           const void *addr, socklen_t addrlen) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!kbuf && len) return -EINVAL;
    if (!(s->type == SOCK_STREAM && s->tcp) && len > afi_max_payload(s->family, s->type))
        return -EMSGSIZE;
    return afinet_sendto_k(fd, kbuf, len, flags, addr, addrlen);
}

ssize_t afinet_sendto(int fd, const void *ubuf, size_t len, int flags,
                      const void *addr, socklen_t addrlen) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!ubuf && len) return -EINVAL;   /* len==0 is a valid empty datagram */
    if (len == 0)
        return afinet_sendto_k(fd, ubuf, 0, flags, addr, addrlen);

    int stream = (s->type == SOCK_STREAM && s->tcp) ? 1 : 0;

    /* Reject an oversized datagram before allocating for it. */
    if (!stream && len > afi_max_payload(s->family, s->type))
        return -EMSGSIZE;

    size_t cap = stream ? (len < AFI_SEND_CHUNK ? len : AFI_SEND_CHUNK) : len;
    uint8_t *kbuf = kmalloc(cap);
    if (!kbuf) return -ENOMEM;

    ssize_t total = 0;
    while ((size_t)total < len) {
        size_t chunk = len - (size_t)total;
        if (chunk > cap) chunk = cap;
        if (copyin((const uint8_t *)ubuf + total, kbuf, chunk) != 0) {
            kfree(kbuf, cap);
            return total ? total : -EFAULT;
        }
        ssize_t w = afinet_sendto_k(fd, kbuf, chunk, flags, addr, addrlen);
        if (w < 0) {
            kfree(kbuf, cap);
            return total ? total : w;
        }
        total += w;
        if ((size_t)w < chunk)   /* partial send (nonblock / window full) */
            break;
        if (!stream)             /* exactly one datagram */
            break;
    }
    kfree(kbuf, cap);
    return total;
}

ssize_t afinet_recvfrom(int fd, void *buf, size_t len, int flags,
                        void *addr, socklen_t *addrlen) {
    return afinet_recvfrom_rx(fd, buf, len, flags, addr, addrlen, NULL);
}

int afinet_pktinfo_on(int fd) {
    afi_sock_t *s = afi_from_fd(fd);
    return s && s->family == AF_INET && s->pktinfo;
}

ssize_t afinet_recvfrom_rx(int fd, void *buf, size_t len, int flags,
                           void *addr, socklen_t *addrlen,
                           struct afi_rxinfo *rx) {
    if (rx) rx->valid = 0;
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!buf) return -EINVAL;
    /* UDP-API-10: no source address unless a datagram is actually taken.
     * The caller's *addrlen (do_recv's 128-byte bounce capacity) was left
     * untouched on EOF, EAGAIN and the stream path, so recvfrom() reported
     * a 128-byte "address" of zeros.  Only the dequeue path sets it.
     * *addrlen is in/out: keep the capacity it brought in. */
    socklen_t acap = addrlen ? *addrlen : 0;
    if (addrlen) *addrlen = 0;

    /* TCP path — data lives in the PCB's rxbuf, NOT the per-socket
     * UDP/RAW ring below.  Without this branch every TCP recv hung
     * forever waiting for s->count to become non-zero, which it
     * never did because the TCP input path only touches the PCB's
     * own ring.  Respect both per-call MSG_DONTWAIT and the fd's
     * FNONBLOCK flag.  */
    if (s->type == SOCK_STREAM && s->tcp) {
        /* recv() on a listening socket is a misuse -> ENOTCONN, and must
         * NOT block waiting for data that can never arrive. */
        if (tcp_is_listening(s->tcp)) return -ENOTCONN;
        file_t *f = (fd >= 0 && fd < MAX_FD)
                        ? current_process->fds[fd] : NULL;
        int nb = (flags & MSG_DONTWAIT) ||
                 (f && (f->f_flag & FNONBLOCK));
        uint64_t dl = afi_deadline(s->rcv_timeo);
        if (flags & MSG_PEEK)
            return nb ? tcp_peek_nb(s->tcp, buf, len)
                      : tcp_peek_until(s->tcp, buf, len, dl);
        return nb ? tcp_recv_nb(s->tcp, buf, len)
                  : tcp_recv_until(s->tcp, buf, len, dl);
    }

    /* NET-01: pin the socket and take the ring lock — see afinet_node_read.
     * The datagram payload and its source address are snapshotted into
     * kernel-local storage under the lock; the fill-out of the caller's
     * buffers runs unlocked.  `addr`/`addrlen` here are the kernel bounce
     * buffers supplied by recv_into_kbuf(), so the copy is safe. */
    /*
     * Resolve non-blocking BEFORE taking the ring lock, and honour both the
     * per-call MSG_DONTWAIT and the fd's FNONBLOCK -- exactly what the TCP arm
     * above already does.  The datagram path used to test MSG_DONTWAIT only,
     * so a socket made non-blocking with fcntl(F_SETFL, O_NONBLOCK) and then
     * read with recv(..., 0) slept in sched_sleep() forever instead of
     * returning EAGAIN.  That silently breaks every poll/deadline-driven UDP
     * client (DNS resolvers, rpcbind, DHCP), which believe the read cannot
     * block; the AF_PACKET twin of this bug is what hung dhclient at
     * "DHCPDISCOVER ... (try 1/4)".
     */
    int nb_dgram;
    {
        file_t *nf = (current_process && fd >= 0 && fd < MAX_FD)
                         ? current_process->fds[fd] : NULL;
        nb_dgram = (flags & MSG_DONTWAIT) ||
                   (nf && (nf->f_flag & FNONBLOCK));
    }

    uint64_t deadline = afi_deadline(s->rcv_timeo);
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    s->refcount++;
    int fam = s->family;
    for (;;) {
        if (s->count > 0) {
            afi_rec_t h;
            uint8_t tmp[AFI_DATA_MAX];
            /*
             * SOCK-05: MSG_PEEK has to LEAVE the datagram queued.  The ring
             * was advanced unconditionally, so a peek consumed it -- the
             * caller got its look at the data and the datagram was gone,
             * which is the exact opposite of what MSG_PEEK means and
             * silently loses a message for anyone who peeks before deciding
             * how large a buffer to allocate.
             */
            rq_pop(s, &h, tmp, len < sizeof(tmp) ? len : sizeof(tmp),
                   !(flags & MSG_PEEK));
            size_t n = h.len < len ? h.len : len;
            uint8_t paddr[16];
            uint16_t pport = h.port;
            uint16_t ptrue = h.truelen;
            uint32_t pdaddr = h.daddr4, pifindex = h.ifindex;   /* UDP-API-11 */
            memcpy(paddr, h.addr, 16);
            afi_rele_unlock(s, fl);
            memcpy(buf, tmp, n);
            if (addr && addrlen) {
                if (fam == AF_INET && acap >= (socklen_t)sizeof(struct sin_kern)) {
                    struct sin_kern *sin = (struct sin_kern *)addr;
                    memset(sin, 0, sizeof(*sin));
                    sin->sin_family = AF_INET;
                    sin->sin_port = __builtin_bswap16(pport);
                    memcpy(&sin->sin_addr, paddr, 4);
                    *addrlen = sizeof(*sin);
                } else if (fam == AF_INET6 && acap >= (socklen_t)sizeof(struct sin6_kern)) {
                    struct sin6_kern *sin6 = (struct sin6_kern *)addr;
                    memset(sin6, 0, sizeof(*sin6));
                    sin6->sin6_family = AF_INET6;
                    sin6->sin6_port = __builtin_bswap16(pport);
                    memcpy(sin6->sin6_addr, paddr, 16);
                    *addrlen = sizeof(*sin6);
                }
            }
            /*
             * SOCK-06: with MSG_TRUNC, report the datagram's real length
             * rather than how much of it fitted.  Without it a short buffer
             * and a short datagram are indistinguishable, so a caller can
             * never tell that it lost the tail of a message.
             */
            if (rx && fam == AF_INET && pdaddr) {
                rx->valid = 1;
                rx->ifindex = pifindex;
                rx->addr = pdaddr;
                /* A reply to a broadcast or group datagram goes from the
                 * interface's own address. */
                netdev_t *d = pifindex ? netdev_by_index(pifindex) : NULL;
                int unicast = !(pdaddr == 0xFFFFFFFFu || ((pdaddr & 0xFF) >> 4) == 0xE ||
                                (d && d->ip4_netmask && pdaddr ==
                                 ((d->ip4_addr & d->ip4_netmask) | ~d->ip4_netmask)));
                rx->spec_dst = (unicast || !d) ? pdaddr : d->ip4_addr;
            }
            if (flags & MSG_TRUNC) return (ssize_t)ptrue;
            return (ssize_t)n;
        }
        /* UDP-ICMP-01: queued datagrams first, then a pending ICMP error --
         * which a blocked reader is woken to collect, instead of sleeping
         * out its whole timeout against a port that has already refused. */
        if (s->so_error) {
            int err = s->so_error;
            s->so_error = 0;
            afi_rele_unlock(s, fl);
            return -err;
        }
        /* UDP-API-08: after shutdown(SHUT_RD), an empty queue is end of
         * file -- as read() already reported -- not a reason to sleep. */
        if (s->rd_shut) { afi_rele_unlock(s, fl); return 0; }
        /* Non-blocking: MSG_DONTWAIT (Linux convention) or the fd's
         * FNONBLOCK, resolved above. */
        if (nb_dgram) { afi_rele_unlock(s, fl); return -EAGAIN; }
        /* UDP-API-04: SO_RCVTIMEO expired -- it used to be accepted and
         * discarded, so this loop slept forever against a silent peer. */
        if (deadline && get_ticks() >= deadline) {
            afi_rele_unlock(s, fl);
            return -EAGAIN;
        }
        /* UDP-06: queue-then-release; see afi_wait(). */
        if (afi_wait(s, &fl) == -EINTR) {
            afi_rele_unlock(s, fl);
            return -EINTR;
        }
        if (s->closed) { afi_rele_unlock(s, fl); return 0; }
    }
}

/* ------------------------------------------------------------------ */
/* Upper-half delivery — called from IP/UDP input paths               */
/* ------------------------------------------------------------------ */

/*
 * UDP-01: score a datagram socket against a received datagram's full
 * 4-tuple, not just its local port.
 *
 * The demux used to be "local_port == dport" and nothing else -- daddr was
 * explicitly thrown away ("(void)daddr;") and the delivery loop enqueued a
 * COPY into every socket that matched.  Three separate defects fell out of
 * that:
 *
 *   - a connect()ed UDP socket accepted datagrams from any source, so a
 *     spoofed reply beat the real server's; POSIX requires that a connected
 *     datagram socket receive only from its peer;
 *   - bind(127.0.0.1, X) received datagrams that arrived on a real
 *     interface, because the bound local address was never compared;
 *   - two sockets sharing a port each got the whole datagram, so two
 *     resolvers read each other's answers.
 *
 * Returns -1 for "does not match at all", otherwise a specificity score;
 * the caller delivers a unicast datagram to the single highest-scoring
 * socket, which is the BSD best-match rule.  A wildcard bind (0.0.0.0/::)
 * and an unconnected socket both still match -- they just lose to a socket
 * that named the address or the peer.
 */
static int addr_is_wild(const uint8_t *a, size_t n) {
    for (size_t i = 0; i < n; i++) if (a[i]) return 0;
    return 1;
}

static int sock_score(afi_sock_t *s, int family, uint8_t proto,
                      const void *saddr, const void *daddr,
                      uint16_t sport, uint16_t dport, size_t alen) {
    if (s->closed) return -1;
    if (s->family != family) return -1;
    if (s->type == SOCK_RAW) {
        if (s->protocol != 0 && s->protocol != (int)proto) return -1;
        /* UDP-API-17: a bound raw socket takes only datagrams to its
         * address, a connected one only those from its peer -- as on BSD
         * and Linux.  Matching on the protocol alone handed every raw
         * socket the whole host's traffic for it. */
        if (!addr_is_wild(s->local_addr, alen) &&
            memcmp(s->local_addr, daddr, alen) != 0) return -1;
        if (s->connected && !addr_is_wild(s->peer_addr, alen) &&
            memcmp(s->peer_addr, saddr, alen) != 0) return -1;
        return 0;
    }
    /* DGRAM/UDP: a SOCK_STREAM socket never matches here, and an unbound
     * socket (local_port==0) is NOT a promiscuous catch-all — that made
     * every stray TCP/UDP socket swallow a copy of every datagram. */
    if (s->type != SOCK_DGRAM) return -1;
    if (proto != IPPROTO_UDP_NUM) return -1;
    if (s->local_port == 0 || s->local_port != dport) return -1;
    /* UDP-IP-06: a group datagram is for sockets that joined the group (BSD
     * semantics -- not, as on Linux by default, every socket bound to the
     * port once anything on the host has joined). */
    if (family == AF_INET && ((*(const uint8_t *)daddr) >> 4) == 0xE) {
        uint32_t g;
        memcpy(&g, daddr, 4);
        int joined = 0;
        for (int i = 0; i < AFI_MC_MAX && !joined; i++)
            if (s->mc_group[i] == g) joined = 1;
        if (!joined) return -1;
    }

    int score = 0;
    if (!addr_is_wild(s->local_addr, alen)) {
        if (memcmp(s->local_addr, daddr, alen) != 0) return -1;
        score += 1;
    }
    if (s->connected) {
        if (s->peer_port != sport) return -1;
        if (!addr_is_wild(s->peer_addr, alen) &&
            memcmp(s->peer_addr, saddr, alen) != 0) return -1;
        score += 2;
    }
    return score;
}

/* UDP-API-11: the interface a datagram for `daddr` arrived on, for
 * IP_PKTINFO's ipi_ifindex -- the one owning the address or broadcast, lo
 * for 127/8, the member interface for a group.  0 if none matches. */
static uint32_t afi_ifindex_for(uint32_t daddr) {
    netdev_t *any_mc = NULL;
    for (netdev_t *d = netdev_first(); d; d = netdev_next(d)) {
        if ((d->flags & NETDEV_IFF_LOOPBACK) && (daddr & 0xFF) == 127)
            return d->ifindex;
        if (d->ip4_addr && (daddr == d->ip4_addr ||
            (d->ip4_netmask &&
             daddr == ((d->ip4_addr & d->ip4_netmask) | ~d->ip4_netmask))))
            return d->ifindex;
        if (((daddr & 0xFF) >> 4) == 0xE && netdev_mc_member(d, daddr) && !any_mc)
            any_mc = d;
    }
    return any_mc ? any_mc->ifindex : 0;
}

/* Queue one datagram on s.  The caller holds afi_lock and, if this returns
 * 1, wakes s->wait_chan AFTER releasing it (UDP-RES-04). */
static int enqueue(afi_sock_t *s, uint8_t family, uint8_t proto, uint16_t port,
                   const void *addr, const uint8_t *data, size_t len,
                   uint32_t daddr4) {
    if (!s->rq) return 0;
    size_t n = len > AFI_DATA_MAX ? AFI_DATA_MAX : len;
    uint32_t need = AFI_REC_SPACE(n);
    /* UDP-RES-01: admission is by bytes against SO_RCVBUF, not by count. */
    uint32_t limit = s->rcvbuf < s->rq_cap ? s->rcvbuf : s->rq_cap;
    if (s->rq_used + need > limit) {
        /* UDP-RES-03: counted, per socket and (for UDP) globally -- a full
         * queue used to drop without a trace. */
        s->rq_drops++;
        if (proto == IPPROTO_UDP_NUM) udp_stat_inc(UDP_STAT_RCVBUF_ERRORS);
        return 0;
    }
    afi_rec_t h;
    memset(&h, 0, sizeof(h));
    h.family = family;
    h.proto = proto;
    h.port = port;
    if (family == AF_INET) memcpy(h.addr, addr, 4);
    else                   memcpy(h.addr, addr, 16);
    h.daddr4 = daddr4;                               /* UDP-API-11 */
    h.ifindex = daddr4 ? afi_ifindex_for(daddr4) : 0;
    h.len = (uint16_t)n;
    h.truelen = (uint16_t)(len > 0xFFFF ? 0xFFFF : len);
    rq_put(s, s->rq_head, &h, sizeof(h));
    rq_put(s, s->rq_head + (uint32_t)sizeof(h), data, (uint32_t)n);
    s->rq_head = (s->rq_head + need) % s->rq_cap;
    s->rq_used += need;
    s->count++;
    return 1;
}

/*
 * UDP-RES-04: wake the readers of the sockets a delivery queued to, after
 * afi_lock is released.  sched_wakeup() walks the whole thread registry, and
 * it used to run once per receiving socket inside the IRQ-off critical
 * section -- a broadcast to N listeners held interrupts off for N registry
 * walks.  netdev_rx() uses the same collect-then-wake shape.
 */
#define AFI_WAKE_MAX 16

struct afi_wakeset {
    void *chan[AFI_WAKE_MAX];
    int   n;
    int   overflow;     /* more sockets than slots: wake by rescanning */
};

static void afi_wake_add(struct afi_wakeset *w, afi_sock_t *s) {
    for (int i = 0; i < w->n; i++)
        if (w->chan[i] == s->wait_chan) return;
    if (w->n < AFI_WAKE_MAX) w->chan[w->n++] = s->wait_chan;
    else w->overflow = 1;
}

static void afi_wake_all(struct afi_wakeset *w) {
    for (int i = 0; i < w->n; i++)
        sched_wakeup(w->chan[i]);
    if (w->overflow) {
        /* Rare: more than AFI_WAKE_MAX sockets took this datagram.  Wake
         * every socket with data queued; a spurious wakeup is harmless. */
        unsigned long fl = spinlock_acquire_irq(&afi_lock);
        void *extra[AFI_WAKE_MAX];
        int n = 0;
        for (afi_sock_t *s = g_afi_head; s && n < AFI_WAKE_MAX; s = s->next)
            if (s->count) extra[n++] = s->wait_chan;
        spinlock_release_irq(&afi_lock, fl);
        for (int i = 0; i < n; i++) sched_wakeup(extra[i]);
    }
}

int afinet_deliver_v4(uint32_t saddr, uint32_t daddr,
                      uint8_t protocol,
                      const uint8_t *pkt, size_t len, int for_dgram,
                      int fanout) {
    /* UDP-01: daddr is now part of the demux key (see sock_score). */
    int delivered = 0;
    uint16_t sport = 0, dport = 0;

    /* For UDP, find ports and trim header. */
    const uint8_t *payload = pkt;
    size_t payload_len = len;
    if (protocol == IPPROTO_UDP_NUM) {
        /* pkt is the IP packet for RAW deliveries (for_dgram==0), or the
         * bare UDP datagram for udp_input deliveries (for_dgram==1).
         * NET-08: only strip an IP header on the RAW path — gating on the
         * for_dgram flag, not the (pkt[0]>>4)==4 heuristic, which misfires
         * when a UDP source port's high byte is 0x4X on the datagram
         * path and wrongly strips IPH_HL*4 bytes as a phantom IP header. */
        if (!for_dgram && len >= sizeof(struct iphdr) && (pkt[0] >> 4) == 4) {
            const struct iphdr *ih = (const struct iphdr *)pkt;
            size_t hlen = IPH_HL(ih) * 4;
            if (hlen + sizeof(struct udphdr) > len) return 0;
            payload = pkt + hlen;
            payload_len = len - hlen;
        }
        if (payload_len < sizeof(struct udphdr)) return 0;
        const struct udphdr *uh = (const struct udphdr *)payload;
        sport = __builtin_bswap16(uh->source);
        dport = __builtin_bswap16(uh->dest);
        /* Strip UDP header for DGRAM sockets. */
    }

    /* NET-01: walk + enqueue under afi_lock (IRQ-safe).  This runs in
     * hard IRQ context; holding the lock across the whole walk means a
     * concurrent close() cannot unlink and free a socket while we are
     * about to enqueue into its ring, and enqueue()'s ring-counter
     * mutation is serialised against process-context readers. */
    struct afi_wakeset wake;
    wake.n = 0;
    wake.overflow = 0;
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    afi_sock_t *best = NULL;
    int best_score = -1;
    for (afi_sock_t *s = g_afi_head; s; s = s->next) {
        int score = sock_score(s, AF_INET, protocol, &saddr, &daddr,
                               sport, dport, 4);
        if (score < 0) continue;
        if (s->type == SOCK_RAW) {
            /* RAW gets the full IP packet, delivered only via the ip4_input
             * path (for_dgram==0) — NOT also from udp_input, else every
             * datagram is enqueued twice.  RAW legitimately fans out: every
             * subscriber to a protocol sees every packet of it. */
            if (for_dgram) continue;
            if (enqueue(s, AF_INET, protocol, sport, &saddr, pkt, len, daddr)) afi_wake_add(&wake, s);
            delivered = 1;
        } else {
            /* DGRAM: delivered only via udp_input, to the single best match
             * (UDP-01) rather than to every socket on the port. */
            if (!for_dgram) continue;
            if (fanout) {
                /* UDP-IP-08: a broadcast/multicast datagram is for every
                 * socket that can take it (RFC 1122 3.3.6), not only the
                 * best match -- two listeners on a broadcast port used to
                 * split the traffic between them, one datagram each. */
                if (enqueue(s, AF_INET, protocol, sport, &saddr,
                        payload + sizeof(struct udphdr),
                        payload_len - sizeof(struct udphdr), daddr)) afi_wake_add(&wake, s);
                delivered = 1;
                continue;
            }
            /* UDP-API-01: g_afi_head is newest-first, so `>=` leaves a tie to
             * the OLDEST socket -- a later bind cannot capture an existing
             * socket's traffic by tying its score. */
            if (score >= best_score) { best_score = score; best = s; }
        }
    }
    if (best) {
        if (enqueue(best, AF_INET, protocol, sport, &saddr,
                payload + sizeof(struct udphdr),
                payload_len - sizeof(struct udphdr), daddr)) afi_wake_add(&wake, best);
        delivered = 1;
    }
    spinlock_release_irq(&afi_lock, fl);
    afi_wake_all(&wake);                     /* UDP-RES-04 */
    return delivered;
}

/*
 * UDP-ICMP-01: RFC 1122 4.1.3.3 -- UDP must pass ICMP errors to the
 * application.  Only a CONNECTED socket whose 4-tuple matches the quoted
 * datagram hears about it, as on BSD and Linux: an unconnected socket has no
 * per-destination error channel, and reporting one client's unreachable
 * port to a server's next recv() would let any peer break it.
 */
void afinet_icmp_error_v4(uint32_t laddr, uint16_t lport,
                          uint32_t raddr, uint16_t rport, int err) {
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    for (afi_sock_t *s = g_afi_head; s; s = s->next) {
        if (s->closed || s->family != AF_INET || s->type != SOCK_DGRAM) continue;
        if (!s->connected || s->local_port != lport || s->peer_port != rport)
            continue;
        if (memcmp(s->peer_addr, &raddr, 4) != 0) continue;
        if (!addr_is_wild(s->local_addr, 4) &&
            memcmp(s->local_addr, &laddr, 4) != 0) continue;
        s->so_error = err;
        sched_wakeup(s->wait_chan);
    }
    spinlock_release_irq(&afi_lock, fl);
}

int afinet_deliver_v6(const uint8_t saddr[16], const uint8_t daddr[16],
                      uint8_t protocol,
                      const uint8_t *pkt, size_t len, int for_dgram,
                      int fanout) {
    /* UDP-01: daddr is now part of the demux key (see sock_score). */
    int delivered = 0;
    uint16_t sport = 0, dport = 0;

    const uint8_t *payload = pkt;
    size_t payload_len = len;
    if (protocol == IPPROTO_UDP_NUM) {
        /* NET-08 (v6 twin): only strip a leading IPv6 header on the RAW path
         * (for_dgram==0); on the bare-datagram path the (pkt[0]>>4)==6
         * heuristic can misfire on datagram bytes. */
        if (!for_dgram && len >= sizeof(struct ip6_hdr) &&
            (pkt[0] >> 4) == 6) {
            payload = pkt + sizeof(struct ip6_hdr);
            payload_len = len - sizeof(struct ip6_hdr);
        }
        if (payload_len < sizeof(struct udphdr)) return 0;
        const struct udphdr *uh = (const struct udphdr *)payload;
        sport = __builtin_bswap16(uh->source);
        dport = __builtin_bswap16(uh->dest);
    }

    /* NET-01: walk + enqueue under afi_lock — see afinet_deliver_v4. */
    struct afi_wakeset wake;
    wake.n = 0;
    wake.overflow = 0;
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    afi_sock_t *best = NULL;
    int best_score = -1;
    for (afi_sock_t *s = g_afi_head; s; s = s->next) {
        int score = sock_score(s, AF_INET6, protocol, saddr, daddr,
                               sport, dport, 16);
        if (score < 0) continue;
        if (s->type == SOCK_RAW) {
            if (for_dgram) continue;
            /* RAW v6 traditionally gets the payload (no IPv6 hdr).
             * Strip if we have a full IP6 packet. */
            const uint8_t *body = pkt;
            size_t blen = len;
            if (len >= sizeof(struct ip6_hdr) && (pkt[0] >> 4) == 6) {
                body = pkt + sizeof(struct ip6_hdr);
                blen = len - sizeof(struct ip6_hdr);
            }
            if (enqueue(s, AF_INET6, protocol, sport, saddr, body, blen, 0)) afi_wake_add(&wake, s);
            delivered = 1;
        } else {
            /* UDP-01: single best match, not a copy to every socket. */
            if (!for_dgram) continue;
            if (fanout) {
                /* UDP-IP-08: a broadcast/multicast datagram is for every
                 * socket that can take it (RFC 1122 3.3.6), not only the
                 * best match -- two listeners on a broadcast port used to
                 * split the traffic between them, one datagram each. */
                if (enqueue(s, AF_INET6, protocol, sport, saddr,
                        payload + sizeof(struct udphdr),
                        payload_len - sizeof(struct udphdr), 0)) afi_wake_add(&wake, s);
                delivered = 1;
                continue;
            }
            /* UDP-API-01: g_afi_head is newest-first, so `>=` leaves a tie to
             * the OLDEST socket -- a later bind cannot capture an existing
             * socket's traffic by tying its score. */
            if (score >= best_score) { best_score = score; best = s; }
        }
    }
    if (best) {
        if (enqueue(best, AF_INET6, protocol, sport, saddr,
                payload + sizeof(struct udphdr),
                payload_len - sizeof(struct udphdr), 0)) afi_wake_add(&wake, best);
        delivered = 1;
    }
    spinlock_release_irq(&afi_lock, fl);
    afi_wake_all(&wake);                     /* UDP-RES-04 */
    return delivered;
}
