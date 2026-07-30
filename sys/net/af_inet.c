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

#define AFI_RING_LEN 32
#define AFI_DATA_MAX 1500

typedef struct afi_pkt {
    uint8_t  family;    /* AF_INET or AF_INET6 */
    uint8_t  proto;
    uint16_t port;      /* source port for UDP, 0 for RAW */
    uint8_t  addr[16];  /* source address (4 bytes for v4) */
    uint16_t len;
    uint8_t  data[AFI_DATA_MAX];
} afi_pkt_t;

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

    afi_pkt_t *ring;
    uint32_t   head, tail, count;
    void      *wait_chan;
    int        closed;
    int        rd_shut;     /* shutdown(SHUT_RD): reads return EOF */

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

static afi_sock_t *g_afi_head;
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
    if (s->ring) kfree(s->ring, sizeof(afi_pkt_t) * AFI_RING_LEN);
    kfree(s, sizeof(*s));
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
            } else if (s->count > 0) {
                avail = (int)s->ring[s->tail].len;
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

size_t afinet_node_read(fs_node_t *node, off_t off, size_t size, uint8_t *buf) {
    (void)off;
    afi_sock_t *s = (afi_sock_t *)(uintptr_t)node->impl;
    if (!s || s->closed) return 0;
    if (s->rd_shut) return 0;            /* shutdown(SHUT_RD): EOF */
    int nb = afi_node_nonblock(node);
    if (s->type == SOCK_STREAM && s->tcp) {
        ssize_t n = nb ? tcp_recv_nb(s->tcp, buf, size)
                       : tcp_recv(s->tcp, buf, size);
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
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    s->refcount++;
    for (;;) {
        if (s->count > 0) {
            afi_pkt_t *p = &s->ring[s->tail];
            size_t n = p->len < size ? p->len : size;
            uint8_t tmp[AFI_DATA_MAX];
            memcpy(tmp, p->data, n);
            s->tail = (s->tail + 1) % AFI_RING_LEN;
            s->count--;
            afi_rele_unlock(s, fl);
            memcpy(buf, tmp, n);
            return n;
        }
        if (nb) { afi_rele_unlock(s, fl); return (size_t)-EAGAIN; }
        spinlock_release_irq(&afi_lock, fl);
        /* Make the sleep signal-interruptible so SIGINT (and friends)
         * yank ping/etc out of a blocked recv. */
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep(s->wait_chan);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        fl = spinlock_acquire_irq(&afi_lock);
        if (current_thread->sig_pending & ~current_thread->sig_mask) {
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
static void udp_csum4(struct udphdr *uh, uint32_t daddr, size_t dgram_len) {
    uh->check = 0;
    uint32_t saddr = ip4_source_for(daddr);
    uint16_t c = inet_csum_pseudo4(saddr, daddr, IPPROTO_UDP_NUM,
                                   (uint16_t)dgram_len, uh);
    uh->check = c ? c : 0xFFFF;
}

static void udp_csum6(struct udphdr *uh, const uint8_t daddr[16],
                      size_t dgram_len) {
    uint8_t saddr[16];
    uh->check = 0;
    if (ip6_source_for(daddr, saddr) != 0) return;  /* unroutable; send fails */
    uint16_t c = inet_csum_pseudo6(saddr, daddr, IPPROTO_UDP_NUM,
                                   (uint32_t)dgram_len, uh);
    uh->check = c ? c : 0xFFFF;
}

static size_t afinet_node_write(fs_node_t *node, off_t off, size_t size, const uint8_t *buf) {
    (void)off;
    afi_sock_t *s = (afi_sock_t *)(uintptr_t)node->impl;
    if (!s || s->closed) return 0;
    if (s->type == SOCK_STREAM && s->tcp) {
        ssize_t n = afi_node_nonblock(node) ? tcp_send_nb(s->tcp, buf, size)
                                            : tcp_send(s->tcp, buf, size);
        return (size_t)n;
    }
    /* write() without an address only works on a connected DGRAM socket. */
    if (!s->connected) return (size_t)-EDESTADDRREQ;

    if (s->family == AF_INET) {
        if (s->type == SOCK_DGRAM) {
            uint8_t pkt[AFI_DATA_MAX + sizeof(struct udphdr)];
            if (size > AFI_DATA_MAX) return (size_t)-EMSGSIZE;
            struct udphdr *uh = (struct udphdr *)pkt;
            /* NET-07: allocate via afinet_alloc_ephemeral() — the inline
             * ++g_ephemeral_next bypassed its wrap-to-49152 guard and is
             * non-atomic, yielding port 0 / low ports past 65535. */
            uh->source = __builtin_bswap16(s->local_port ? s->local_port : afinet_alloc_ephemeral());
            if (!s->local_port) s->local_port = __builtin_bswap16(uh->source);
            uh->dest   = __builtin_bswap16(s->peer_port);
            uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + size));
            memcpy(pkt + sizeof(*uh), buf, size);
            uint32_t daddr;
            memcpy(&daddr, s->peer_addr, 4);
            udp_csum4(uh, daddr, sizeof(*uh) + size);
            int rc = ip4_output(daddr, IPPROTO_UDP_NUM, pkt, sizeof(*uh) + size);
            return rc < 0 ? (size_t)rc : size;
        } else {
            uint32_t daddr;
            memcpy(&daddr, s->peer_addr, 4);
            int rc = ip4_output(daddr, (uint8_t)s->protocol, buf, size);
            return rc < 0 ? (size_t)rc : size;
        }
    } else {
        if (s->type == SOCK_DGRAM) {
            uint8_t pkt[AFI_DATA_MAX + sizeof(struct udphdr)];
            if (size > AFI_DATA_MAX) return (size_t)-EMSGSIZE;
            struct udphdr *uh = (struct udphdr *)pkt;
            /* NET-07: allocate via afinet_alloc_ephemeral() — the inline
             * ++g_ephemeral_next bypassed its wrap-to-49152 guard and is
             * non-atomic, yielding port 0 / low ports past 65535. */
            uh->source = __builtin_bswap16(s->local_port ? s->local_port : afinet_alloc_ephemeral());
            if (!s->local_port) s->local_port = __builtin_bswap16(uh->source);
            uh->dest   = __builtin_bswap16(s->peer_port);
            uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + size));
            memcpy(pkt + sizeof(*uh), buf, size);
            udp_csum6(uh, s->peer_addr, sizeof(*uh) + size);
            int rc = ip6_output(s->peer_addr, IPPROTO_UDP_NUM, pkt, sizeof(*uh) + size);
            return rc < 0 ? (size_t)rc : size;
        } else {
            int rc = ip6_output(s->peer_addr, (uint8_t)s->protocol, buf, size);
            return rc < 0 ? (size_t)rc : size;
        }
    }
}

static void afinet_node_close(fs_node_t *node) {
    afi_sock_t *s = (afi_sock_t *)(uintptr_t)node->impl;
    if (!s) return;
    node->impl = 0;
    /* tcp_close() serialises internally (its own IRQ-off critical
     * section) and may not run under afi_lock. */
    if (s->tcp) { tcp_close(s->tcp); s->tcp = NULL; }
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
    s->type = type;
    s->protocol = protocol;
    s->refcount = 1;                 /* NET-01: the installed reference */
    s->ring = (afi_pkt_t *)kmalloc(sizeof(afi_pkt_t) * AFI_RING_LEN);
    if (!s->ring) { kfree(s, sizeof(*s)); return -ENOMEM; }
    s->wait_chan = &s->count;

    if (type == SOCK_STREAM) {
        s->tcp = tcp_alloc();
        if (!s->tcp) {
            kfree(s->ring, sizeof(afi_pkt_t) * AFI_RING_LEN);
            kfree(s, sizeof(*s));
            return -ENOMEM;
        }
    }

    int fd = afi_install_fd(s);
    if (fd < 0) {
        if (s->tcp) tcp_free(s->tcp);
        kfree(s->ring, sizeof(afi_pkt_t) * AFI_RING_LEN);
        kfree(s, sizeof(*s));
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
static int afinet_port_taken(const afi_sock_t *self, uint16_t port) {
    int taken = 0;
    /* NET-01: walk the delivery list under afi_lock so it can't be
     * re-spliced by socket()/accept()/close() (or an IRQ delivery walk)
     * mid-scan. */
    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    for (afi_sock_t *o = g_afi_head; o; o = o->next) {
        if (o == self || o->closed) continue;
        if (o->bound && o->local_port == port &&
            o->family == self->family && o->type == self->type) {
            taken = 1;
            break;
        }
    }
    spinlock_release_irq(&afi_lock, fl);
    return taken;
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
        /* A specific port already owned by another socket is EADDRINUSE
         * unless this socket set SO_REUSEADDR. */
        if (req && !s->reuseaddr && afinet_port_taken(s, req))
            return -EADDRINUSE;
        /* bind(port 0): assign an ephemeral port now so getsockname()
         * reflects it (POSIX/BSD) — see afinet_alloc_ephemeral(). */
        s->local_port = req ? req : afinet_alloc_ephemeral();
        memcpy(s->local_addr, &sin->sin_addr, 4);
        if (s->tcp) {
            uint32_t la; memcpy(&la, s->local_addr, 4);
            tcp_bind(s->tcp, la, s->local_port);
        }
    } else {
        if (len < (socklen_t)sizeof(struct sin6_kern)) return -EINVAL;
        const struct sin6_kern *sin6 = (const struct sin6_kern *)addr;
        if (sin6->sin6_family != AF_INET6) return -EAFNOSUPPORT;
        s->local_port = __builtin_bswap16(sin6->sin6_port);
        if (s->local_port == 0)
            s->local_port = afinet_alloc_ephemeral();
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
    /* shutdown on a socket that was never connected -> ENOTCONN (TCP). */
    if (s->tcp && !s->connected) return -ENOTCONN;
    if (how == SHUT_RD || how == SHUT_RDWR) {
        s->rd_shut = 1;
        sched_wakeup(s->wait_chan);          /* UDP/RAW blocked readers */
        if (s->tcp) tcp_shutdown_rd(s->tcp); /* TCP: EOF + wake reader */
    }
    if (how == SHUT_WR || how == SHUT_RDWR) {
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
    c->type = SOCK_STREAM;
    c->protocol = 6;
    c->refcount = 1;                 /* NET-01: the installed reference */
    c->ring = (afi_pkt_t *)kmalloc(sizeof(afi_pkt_t) * AFI_RING_LEN);
    if (!c->ring) { kfree(c, sizeof(*c)); tcp_close(cp); return -ENOMEM; }
    c->wait_chan = &c->count;
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
        if (c->ring) kfree(c->ring, sizeof(afi_pkt_t) * AFI_RING_LEN);
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
    return 0;
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
        s->peer_port = __builtin_bswap16(sin6->sin6_port);
        memcpy(s->peer_addr, sin6->sin6_addr, 16);
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
    (void)flags;
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!buf && len) return -EINVAL;   /* len==0 is a valid empty datagram */
    /* TCP: connected stream socket goes through the tcp_send queue
     * regardless of whether the caller passed an addr.  Previously
     * a foot-gun — curl uses sendto() with addr=NULL on a connected
     * stream, and we fell into the UDP path below, looking for a
     * dest addr that wasn't there.  */
    if (s->type == SOCK_STREAM && s->tcp) {
        return tcp_send(s->tcp, buf, len);
    }

    /* Resolve target addr/port. */
    uint16_t dport = s->peer_port;
    uint8_t  daddr_buf[16];
    if (s->connected) {
        memcpy(daddr_buf, s->peer_addr, 16);
    } else if (addr) {
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
    } else {
        return -EDESTADDRREQ;
    }

    /* RAW: caller writes the L4 (and for v4 RAW with IP_HDRINCL it'd be
     * the IP header too — not supported yet; we always synthesize the
     * v4 IP header). */
    if (s->family == AF_INET) {
        if (s->type == SOCK_RAW) {
            uint32_t d;
            memcpy(&d, daddr_buf, 4);
            int rc = ip4_output(d, (uint8_t)s->protocol, buf, len);
            return rc < 0 ? rc : (ssize_t)len;
        }
        /* DGRAM/UDP */
        /* NET-07: use the wrap-guarded ephemeral allocator, not the raw
         * (non-atomic, unguarded) ++g_ephemeral_next. */
        uint16_t sport = s->local_port ? s->local_port : afinet_alloc_ephemeral();
        if (!s->local_port) s->local_port = sport;
        uint8_t pkt[AFI_DATA_MAX + sizeof(struct udphdr)];
        if (len > AFI_DATA_MAX) return -EMSGSIZE;
        struct udphdr *uh = (struct udphdr *)pkt;
        uh->source = __builtin_bswap16(sport);
        uh->dest   = __builtin_bswap16(dport);
        uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + len));
        memcpy(pkt + sizeof(*uh), buf, len);
        uint32_t d;
        memcpy(&d, daddr_buf, 4);
        udp_csum4(uh, d, sizeof(*uh) + len);
        int rc = ip4_output(d, IPPROTO_UDP_NUM, pkt, sizeof(*uh) + len);
        return rc < 0 ? rc : (ssize_t)len;
    } else {
        if (s->type == SOCK_RAW) {
            int rc = ip6_output(daddr_buf, (uint8_t)s->protocol, buf, len);
            return rc < 0 ? rc : (ssize_t)len;
        }
        /* NET-07: use the wrap-guarded ephemeral allocator, not the raw
         * (non-atomic, unguarded) ++g_ephemeral_next. */
        uint16_t sport = s->local_port ? s->local_port : afinet_alloc_ephemeral();
        if (!s->local_port) s->local_port = sport;
        uint8_t pkt[AFI_DATA_MAX + sizeof(struct udphdr)];
        if (len > AFI_DATA_MAX) return -EMSGSIZE;
        struct udphdr *uh = (struct udphdr *)pkt;
        uh->source = __builtin_bswap16(sport);
        uh->dest   = __builtin_bswap16(dport);
        uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + len));
        memcpy(pkt + sizeof(*uh), buf, len);
        udp_csum6(uh, daddr_buf, sizeof(*uh) + len);
        int rc = ip6_output(daddr_buf, IPPROTO_UDP_NUM, pkt, sizeof(*uh) + len);
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

ssize_t afinet_sendto(int fd, const void *ubuf, size_t len, int flags,
                      const void *addr, socklen_t addrlen) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!ubuf && len) return -EINVAL;   /* len==0 is a valid empty datagram */
    if (len == 0)
        return afinet_sendto_k(fd, ubuf, 0, flags, addr, addrlen);

    int stream = (s->type == SOCK_STREAM && s->tcp) ? 1 : 0;

    /* Reject an oversized datagram before allocating for it. */
    if (!stream && len > AFI_DATA_MAX)
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
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!buf) return -EINVAL;

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
        int nb = (flags & 0x40 /*MSG_DONTWAIT*/) ||
                 (f && (f->f_flag & FNONBLOCK));
        if (flags & 0x02 /*MSG_PEEK*/)
            return nb ? tcp_peek_nb(s->tcp, buf, len)
                      : tcp_peek   (s->tcp, buf, len);
        return nb ? tcp_recv_nb(s->tcp, buf, len)
                  : tcp_recv   (s->tcp, buf, len);
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

    unsigned long fl = spinlock_acquire_irq(&afi_lock);
    s->refcount++;
    int fam = s->family;
    for (;;) {
        if (s->count > 0) {
            afi_pkt_t *p = &s->ring[s->tail];
            size_t n = p->len < len ? p->len : len;
            uint8_t tmp[AFI_DATA_MAX];
            uint8_t paddr[16];
            uint16_t pport = p->port;
            memcpy(tmp, p->data, n);
            memcpy(paddr, p->addr, 16);
            s->tail = (s->tail + 1) % AFI_RING_LEN;
            s->count--;
            afi_rele_unlock(s, fl);
            memcpy(buf, tmp, n);
            if (addr && addrlen) {
                if (fam == AF_INET && *addrlen >= (socklen_t)sizeof(struct sin_kern)) {
                    struct sin_kern *sin = (struct sin_kern *)addr;
                    memset(sin, 0, sizeof(*sin));
                    sin->sin_family = AF_INET;
                    sin->sin_port = __builtin_bswap16(pport);
                    memcpy(&sin->sin_addr, paddr, 4);
                    *addrlen = sizeof(*sin);
                } else if (fam == AF_INET6 && *addrlen >= (socklen_t)sizeof(struct sin6_kern)) {
                    struct sin6_kern *sin6 = (struct sin6_kern *)addr;
                    memset(sin6, 0, sizeof(*sin6));
                    sin6->sin6_family = AF_INET6;
                    sin6->sin6_port = __builtin_bswap16(pport);
                    memcpy(sin6->sin6_addr, paddr, 16);
                    *addrlen = sizeof(*sin6);
                }
            }
            return (ssize_t)n;
        }
        /* Non-blocking: MSG_DONTWAIT (Linux convention) or the fd's
         * FNONBLOCK, resolved above. */
        if (nb_dgram) { afi_rele_unlock(s, fl); return -EAGAIN; }
        spinlock_release_irq(&afi_lock, fl);
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep(s->wait_chan);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        fl = spinlock_acquire_irq(&afi_lock);
        if (current_thread->sig_pending & ~current_thread->sig_mask) {
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
    if (s->type == SOCK_RAW)
        return (s->protocol == 0 || s->protocol == (int)proto) ? 0 : -1;
    /* DGRAM/UDP: a SOCK_STREAM socket never matches here, and an unbound
     * socket (local_port==0) is NOT a promiscuous catch-all — that made
     * every stray TCP/UDP socket swallow a copy of every datagram. */
    if (s->type != SOCK_DGRAM) return -1;
    if (proto != IPPROTO_UDP_NUM) return -1;
    if (s->local_port == 0 || s->local_port != dport) return -1;

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

static void enqueue(afi_sock_t *s, uint8_t family, uint8_t proto, uint16_t port,
                    const void *addr, const uint8_t *data, size_t len) {
    if (s->count >= AFI_RING_LEN) return;  /* drop */
    afi_pkt_t *p = &s->ring[s->head];
    p->family = family;
    p->proto = proto;
    p->port = port;
    if (family == AF_INET) memcpy(p->addr, addr, 4);
    else                   memcpy(p->addr, addr, 16);
    size_t n = len > AFI_DATA_MAX ? AFI_DATA_MAX : len;
    memcpy(p->data, data, n);
    p->len = (uint16_t)n;
    s->head = (s->head + 1) % AFI_RING_LEN;
    s->count++;
    sched_wakeup(s->wait_chan);
}

int afinet_deliver_v4(uint32_t saddr, uint32_t daddr,
                      uint8_t protocol,
                      const uint8_t *pkt, size_t len, int for_dgram) {
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
            enqueue(s, AF_INET, protocol, sport, &saddr, pkt, len);
            delivered = 1;
        } else {
            /* DGRAM: delivered only via udp_input, to the single best match
             * (UDP-01) rather than to every socket on the port. */
            if (!for_dgram) continue;
            if (score > best_score) { best_score = score; best = s; }
        }
    }
    if (best) {
        enqueue(best, AF_INET, protocol, sport, &saddr,
                payload + sizeof(struct udphdr),
                payload_len - sizeof(struct udphdr));
        delivered = 1;
    }
    spinlock_release_irq(&afi_lock, fl);
    return delivered;
}

int afinet_deliver_v6(const uint8_t saddr[16], const uint8_t daddr[16],
                      uint8_t protocol,
                      const uint8_t *pkt, size_t len, int for_dgram) {
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
            enqueue(s, AF_INET6, protocol, sport, saddr, body, blen);
            delivered = 1;
        } else {
            /* UDP-01: single best match, not a copy to every socket. */
            if (!for_dgram) continue;
            if (score > best_score) { best_score = score; best = s; }
        }
    }
    if (best) {
        enqueue(best, AF_INET6, protocol, sport, saddr,
                payload + sizeof(struct udphdr),
                payload_len - sizeof(struct udphdr));
        delivered = 1;
    }
    spinlock_release_irq(&afi_lock, fl);
    return delivered;
}
