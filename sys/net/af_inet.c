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

#include <vfs/vfs.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/netdev.h>
#include <net/inet.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <kern/sched.h>
#include <kern/file.h>
#include <kern/console.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

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
typedef struct tcp_pcb tcp_pcb_t;
extern tcp_pcb_t *tcp_alloc(void);
extern void       tcp_free(tcp_pcb_t *p);
extern int        tcp_bind(tcp_pcb_t *p, uint32_t laddr, uint16_t lport);
extern int        tcp_listen(tcp_pcb_t *p, int backlog);
extern int        tcp_connect(tcp_pcb_t *p, uint32_t raddr, uint16_t rport);
extern int        tcp_connect_nb(tcp_pcb_t *p, uint32_t raddr, uint16_t rport);
extern int        tcp_poll(tcp_pcb_t *p, short events, void **wait_chan);
extern tcp_pcb_t *tcp_accept(tcp_pcb_t *listen_p);
extern ssize_t    tcp_send(tcp_pcb_t *p, const void *buf, size_t len);
extern ssize_t    tcp_recv(tcp_pcb_t *p, void *buf, size_t len);
extern int        tcp_close(tcp_pcb_t *p);

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

    afi_pkt_t *ring;
    uint32_t   head, tail, count;
    void      *wait_chan;
    int        closed;

    fs_node_t  node;
    struct afi_sock *next;
} afi_sock_t;

static afi_sock_t *g_afi_head;
static uint16_t    g_ephemeral_next = 49152;

/* ------------------------------------------------------------------ */
/* SIOC* ioctls — interface configuration via an AF_INET socket fd.   */
/* ------------------------------------------------------------------ */

static netdev_t *afinet_find_dev(const char *name) {
    if (!name || !name[0]) return NULL;
    return netdev_by_name(name);
}

static int afinet_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    if (!arg) return -EFAULT;

    /* SIOCGIFCONF takes struct ifconf; everything else takes struct ifreq. */
    if (request == SIOCGIFCONF) {
        struct ifconf *ifc = (struct ifconf *)arg;
        int max = ifc->ifc_len / (int)sizeof(struct ifreq);
        struct ifreq *out = ifc->ifc_req;
        int n = 0;
        for (netdev_t *d = netdev_first(); d && n < max; d = netdev_next(d)) {
            memset(&out[n], 0, sizeof(out[n]));
            strncpy(out[n].ifr_name, d->name, IFNAMSIZ - 1);
            struct sin_kern *sin = (struct sin_kern *)&out[n].ifr_addr;
            sin->sin_family = AF_INET;
            sin->sin_addr   = d->ip4_addr;
            n++;
        }
        ifc->ifc_len = n * (int)sizeof(struct ifreq);
        return 0;
    }

    /* The IPv6 commands take struct in6_ifreq (no name field — keyed
     * by ifindex).  Handle them before the by-name lookup. */
    if (request == SIOCGIFADDR_IN6 || request == SIOCSIFADDR_IN6 ||
        request == SIOCDIFADDR_IN6 || request == SIOCSIFGW_IN6) {
        struct in6_ifreq *r6 = (struct in6_ifreq *)arg;
        netdev_t *d6 = (r6->ifr6_ifindex > 0)
            ? netdev_by_index((uint32_t)r6->ifr6_ifindex)
            : netdev_first();
        if (!d6) return -ENODEV;
        switch (request) {
            case SIOCGIFADDR_IN6:
                memcpy(r6->ifr6_addr.s6_addr, d6->ip6_addr, 16);
                r6->ifr6_prefixlen = d6->ip6_netmask_bits;
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

    struct ifreq *r = (struct ifreq *)arg;
    netdev_t *dev = afinet_find_dev(r->ifr_name);
    if (!dev) return -ENODEV;

    switch (request) {
        case SIOCGIFNAME:
            strncpy(r->ifr_name, dev->name, IFNAMSIZ - 1);
            return 0;
        case SIOCGIFINDEX:
            r->ifr_ifindex = (int)dev->ifindex;
            return 0;
        case SIOCGIFFLAGS:
            r->ifr_flags = (short)dev->flags;
            return 0;
        case SIOCSIFFLAGS: {
            uint32_t keep = dev->flags & ~((uint32_t)0xFFFFu);
            dev->flags = keep | ((uint16_t)r->ifr_flags & 0xFFFFu);
            return 0;
        }
        case SIOCGIFMTU:
            r->ifr_mtu = (int)dev->mtu;
            return 0;
        case SIOCSIFMTU:
            if (r->ifr_mtu < 68 || r->ifr_mtu > 65535) return -EINVAL;
            dev->mtu = (uint32_t)r->ifr_mtu;
            return 0;
        case SIOCGIFHWADDR:
            r->ifr_hwaddr.sa_family = 1;   /* ARPHRD_ETHER */
            memcpy(r->ifr_hwaddr.sa_data, dev->hwaddr, 6);
            return 0;
        case SIOCSIFHWADDR:
            memcpy(dev->hwaddr, r->ifr_hwaddr.sa_data, 6);
            return 0;
        case SIOCGIFADDR: {
            struct sin_kern *sin = (struct sin_kern *)&r->ifr_addr;
            sin->sin_family = AF_INET;
            sin->sin_addr   = dev->ip4_addr;
            sin->sin_port   = 0;
            return 0;
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
            return 0;
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
            return 0;
        }
        case SIOCSIFBRDADDR:
            /* No-op: broadcast is derived from addr/netmask. */
            return 0;
        case SIOCGIFGATEWAY: {
            struct sin_kern *sin = (struct sin_kern *)&r->ifr_addr;
            sin->sin_family = AF_INET;
            sin->sin_addr   = dev->ip4_gateway;
            return 0;
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

size_t afinet_node_read(fs_node_t *node, off_t off, size_t size, uint8_t *buf) {
    (void)off;
    afi_sock_t *s = (afi_sock_t *)(uintptr_t)node->impl;
    if (!s || s->closed) return 0;
    if (s->type == SOCK_STREAM && s->tcp) {
        ssize_t n = tcp_recv(s->tcp, buf, size);
        return n < 0 ? 0 : (size_t)n;
    }
    for (;;) {
        if (s->count > 0) {
            afi_pkt_t *p = &s->ring[s->tail];
            size_t n = p->len < size ? p->len : size;
            memcpy(buf, p->data, n);
            s->tail = (s->tail + 1) % AFI_RING_LEN;
            s->count--;
            return n;
        }
        /* Make the sleep signal-interruptible so SIGINT (and friends)
         * yank ping/etc out of a blocked recv. */
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep(s->wait_chan);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask) {
            return (size_t)-EINTR;
        }
        if (s->closed) return 0;
    }
}

static size_t afinet_node_write(fs_node_t *node, off_t off, size_t size, const uint8_t *buf) {
    (void)off;
    afi_sock_t *s = (afi_sock_t *)(uintptr_t)node->impl;
    if (!s || s->closed) return 0;
    if (s->type == SOCK_STREAM && s->tcp) {
        ssize_t n = tcp_send(s->tcp, buf, size);
        return n < 0 ? (size_t)n : (size_t)n;
    }
    /* write() without an address only works on a connected DGRAM socket. */
    if (!s->connected) return (size_t)-EDESTADDRREQ;

    if (s->family == AF_INET) {
        if (s->type == SOCK_DGRAM) {
            uint8_t pkt[AFI_DATA_MAX + sizeof(struct udphdr)];
            if (size > AFI_DATA_MAX) return (size_t)-EMSGSIZE;
            struct udphdr *uh = (struct udphdr *)pkt;
            uh->source = __builtin_bswap16(s->local_port ? s->local_port : ++g_ephemeral_next);
            if (!s->local_port) s->local_port = __builtin_bswap16(uh->source);
            uh->dest   = __builtin_bswap16(s->peer_port);
            uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + size));
            uh->check  = 0;
            memcpy(pkt + sizeof(*uh), buf, size);
            uint32_t daddr;
            memcpy(&daddr, s->peer_addr, 4);
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
            uh->source = __builtin_bswap16(s->local_port ? s->local_port : ++g_ephemeral_next);
            if (!s->local_port) s->local_port = __builtin_bswap16(uh->source);
            uh->dest   = __builtin_bswap16(s->peer_port);
            uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + size));
            uh->check  = 0;
            memcpy(pkt + sizeof(*uh), buf, size);
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
    s->closed = 1;
    sched_wakeup(s->wait_chan);
    if (s->tcp) { tcp_close(s->tcp); s->tcp = NULL; }
    /* Unlink from list. */
    afi_sock_t **link = &g_afi_head;
    while (*link && *link != s) link = &(*link)->next;
    if (*link == s) *link = s->next;
    if (s->ring) kfree(s->ring, sizeof(afi_pkt_t) * AFI_RING_LEN);
    kfree(s, sizeof(*s));
    node->impl = 0;
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
    strncpy(s->node.name, "<af_inet>", sizeof(s->node.name) - 1);
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
    if (family != AF_INET && family != AF_INET6) return -EAFNOSUPPORT;
    if (type != SOCK_RAW && type != SOCK_DGRAM && type != SOCK_STREAM)
        return -EPROTONOSUPPORT;
    if (type == SOCK_DGRAM && protocol == 0) protocol = IPPROTO_UDP_NUM;
    if (type == SOCK_STREAM && protocol == 0) protocol = 6 /*TCP*/;

    afi_sock_t *s = (afi_sock_t *)kmalloc(sizeof(*s));
    if (!s) return -ENOMEM;
    memset(s, 0, sizeof(*s));
    s->family = family;
    s->type = type;
    s->protocol = protocol;
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

    s->next = g_afi_head;
    g_afi_head = s;
    return fd;
}

int afinet_bind(int fd, const void *addr, socklen_t len) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!addr) return -EINVAL;
    if (s->family == AF_INET) {
        if (len < (socklen_t)sizeof(struct sin_kern)) return -EINVAL;
        const struct sin_kern *sin = (const struct sin_kern *)addr;
        if (sin->sin_family != AF_INET) return -EAFNOSUPPORT;
        s->local_port = __builtin_bswap16(sin->sin_port);
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
    return 0;
}

int afinet_listen(int fd, int backlog) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!s->tcp) return -EOPNOTSUPP;
    return tcp_listen(s->tcp, backlog);
}

int afinet_accept(int fd, void *addr, socklen_t *addrlen) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!s->tcp) return -EOPNOTSUPP;
    tcp_pcb_t *cp = tcp_accept(s->tcp);
    if (!cp) return -EAGAIN;

    /* Allocate a new afi_sock_t wrapping the accepted PCB. */
    afi_sock_t *c = (afi_sock_t *)kmalloc(sizeof(*c));
    if (!c) { tcp_close(cp); return -ENOMEM; }
    memset(c, 0, sizeof(*c));
    c->family = s->family;
    c->type = SOCK_STREAM;
    c->protocol = 6;
    c->ring = (afi_pkt_t *)kmalloc(sizeof(afi_pkt_t) * AFI_RING_LEN);
    if (!c->ring) { kfree(c, sizeof(*c)); tcp_close(cp); return -ENOMEM; }
    c->wait_chan = &c->count;
    c->tcp = cp;

    int newfd = afi_install_fd(c);
    if (newfd < 0) {
        if (c->ring) kfree(c->ring, sizeof(afi_pkt_t) * AFI_RING_LEN);
        kfree(c, sizeof(*c));
        tcp_close(cp);
        return -EMFILE;
    }
    c->next = g_afi_head;
    g_afi_head = c;
    (void)addr; (void)addrlen;
    return newfd;
}

int afinet_connect(int fd, const void *addr, socklen_t len) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!addr) return -EINVAL;

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
            if (rc < 0) return rc;
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

ssize_t afinet_sendto(int fd, const void *buf, size_t len, int flags,
                      const void *addr, socklen_t addrlen) {
    (void)flags;
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!buf || len == 0) return -EINVAL;

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
        uint16_t sport = s->local_port ? s->local_port : ++g_ephemeral_next;
        if (!s->local_port) s->local_port = sport;
        uint8_t pkt[AFI_DATA_MAX + sizeof(struct udphdr)];
        if (len > AFI_DATA_MAX) return -EMSGSIZE;
        struct udphdr *uh = (struct udphdr *)pkt;
        uh->source = __builtin_bswap16(sport);
        uh->dest   = __builtin_bswap16(dport);
        uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + len));
        uh->check  = 0;
        memcpy(pkt + sizeof(*uh), buf, len);
        uint32_t d;
        memcpy(&d, daddr_buf, 4);
        int rc = ip4_output(d, IPPROTO_UDP_NUM, pkt, sizeof(*uh) + len);
        return rc < 0 ? rc : (ssize_t)len;
    } else {
        if (s->type == SOCK_RAW) {
            int rc = ip6_output(daddr_buf, (uint8_t)s->protocol, buf, len);
            return rc < 0 ? rc : (ssize_t)len;
        }
        uint16_t sport = s->local_port ? s->local_port : ++g_ephemeral_next;
        if (!s->local_port) s->local_port = sport;
        uint8_t pkt[AFI_DATA_MAX + sizeof(struct udphdr)];
        if (len > AFI_DATA_MAX) return -EMSGSIZE;
        struct udphdr *uh = (struct udphdr *)pkt;
        uh->source = __builtin_bswap16(sport);
        uh->dest   = __builtin_bswap16(dport);
        uh->len    = __builtin_bswap16((uint16_t)(sizeof(*uh) + len));
        uh->check  = 0;
        memcpy(pkt + sizeof(*uh), buf, len);
        int rc = ip6_output(daddr_buf, IPPROTO_UDP_NUM, pkt, sizeof(*uh) + len);
        return rc < 0 ? rc : (ssize_t)len;
    }
}

ssize_t afinet_recvfrom(int fd, void *buf, size_t len, int flags,
                        void *addr, socklen_t *addrlen) {
    afi_sock_t *s = afi_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!buf) return -EINVAL;

    for (;;) {
        if (s->count > 0) {
            afi_pkt_t *p = &s->ring[s->tail];
            size_t n = p->len < len ? p->len : len;
            memcpy(buf, p->data, n);
            if (addr && addrlen) {
                if (s->family == AF_INET && *addrlen >= (socklen_t)sizeof(struct sin_kern)) {
                    struct sin_kern *sin = (struct sin_kern *)addr;
                    memset(sin, 0, sizeof(*sin));
                    sin->sin_family = AF_INET;
                    sin->sin_port = __builtin_bswap16(p->port);
                    memcpy(&sin->sin_addr, p->addr, 4);
                    *addrlen = sizeof(*sin);
                } else if (s->family == AF_INET6 && *addrlen >= (socklen_t)sizeof(struct sin6_kern)) {
                    struct sin6_kern *sin6 = (struct sin6_kern *)addr;
                    memset(sin6, 0, sizeof(*sin6));
                    sin6->sin6_family = AF_INET6;
                    sin6->sin6_port = __builtin_bswap16(p->port);
                    memcpy(sin6->sin6_addr, p->addr, 16);
                    *addrlen = sizeof(*sin6);
                }
            }
            s->tail = (s->tail + 1) % AFI_RING_LEN;
            s->count--;
            return (ssize_t)n;
        }
        /* Non-blocking via MSG_DONTWAIT (Linux convention). */
        if (flags & 0x40 /* MSG_DONTWAIT */) return -EAGAIN;
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep(s->wait_chan);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask)
            return -EINTR;
        if (s->closed) return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Upper-half delivery — called from IP/UDP input paths               */
/* ------------------------------------------------------------------ */

static int sock_matches_v4(afi_sock_t *s, uint8_t proto, uint16_t dport) {
    if (s->closed) return 0;
    if (s->family != AF_INET) return 0;
    if (s->type == SOCK_RAW) {
        return s->protocol == 0 || s->protocol == (int)proto;
    }
    /* DGRAM/UDP. */
    if (proto != IPPROTO_UDP_NUM) return 0;
    return s->local_port == 0 || s->local_port == dport;
}
static int sock_matches_v6(afi_sock_t *s, uint8_t proto, uint16_t dport) {
    if (s->closed) return 0;
    if (s->family != AF_INET6) return 0;
    if (s->type == SOCK_RAW) {
        return s->protocol == 0 || s->protocol == (int)proto;
    }
    if (proto != IPPROTO_UDP_NUM) return 0;
    return s->local_port == 0 || s->local_port == dport;
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
                      const uint8_t *pkt, size_t len) {
    (void)daddr;
    int delivered = 0;
    uint16_t sport = 0, dport = 0;

    /* For UDP, find ports and trim header. */
    const uint8_t *payload = pkt;
    size_t payload_len = len;
    if (protocol == IPPROTO_UDP_NUM) {
        /* pkt is the IP packet for RAW deliveries, or the UDP datagram
         * for udp_input deliveries.  Detect: if first byte high nibble
         * == 4, it's the full IP packet → step past header. */
        if (len >= sizeof(struct iphdr) && (pkt[0] >> 4) == 4) {
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

    for (afi_sock_t *s = g_afi_head; s; s = s->next) {
        if (!sock_matches_v4(s, protocol, dport)) continue;
        if (s->type == SOCK_RAW) {
            /* RAW v4 receives the full IP packet. */
            enqueue(s, AF_INET, protocol, sport, &saddr, pkt, len);
        } else {
            /* DGRAM: payload after UDP header. */
            enqueue(s, AF_INET, protocol, sport, &saddr,
                    payload + sizeof(struct udphdr),
                    payload_len - sizeof(struct udphdr));
        }
        delivered = 1;
    }
    return delivered;
}

int afinet_deliver_v6(const uint8_t saddr[16], const uint8_t daddr[16],
                      uint8_t protocol,
                      const uint8_t *pkt, size_t len) {
    (void)daddr;
    int delivered = 0;
    uint16_t sport = 0, dport = 0;

    const uint8_t *payload = pkt;
    size_t payload_len = len;
    if (protocol == IPPROTO_UDP_NUM) {
        if (len >= sizeof(struct ip6_hdr) &&
            (pkt[0] >> 4) == 6) {
            payload = pkt + sizeof(struct ip6_hdr);
            payload_len = len - sizeof(struct ip6_hdr);
        }
        if (payload_len < sizeof(struct udphdr)) return 0;
        const struct udphdr *uh = (const struct udphdr *)payload;
        sport = __builtin_bswap16(uh->source);
        dport = __builtin_bswap16(uh->dest);
    }

    for (afi_sock_t *s = g_afi_head; s; s = s->next) {
        if (!sock_matches_v6(s, protocol, dport)) continue;
        if (s->type == SOCK_RAW) {
            /* RAW v6 traditionally gets the payload (no IPv6 hdr).
             * Strip if we have a full IP6 packet. */
            const uint8_t *body = pkt;
            size_t blen = len;
            if (len >= sizeof(struct ip6_hdr) && (pkt[0] >> 4) == 6) {
                body = pkt + sizeof(struct ip6_hdr);
                blen = len - sizeof(struct ip6_hdr);
            }
            enqueue(s, AF_INET6, protocol, sport, saddr, body, blen);
        } else {
            enqueue(s, AF_INET6, protocol, sport, saddr,
                    payload + sizeof(struct udphdr),
                    payload_len - sizeof(struct udphdr));
        }
        delivered = 1;
    }
    return delivered;
}
