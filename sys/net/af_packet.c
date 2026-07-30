/*
 * af_packet.c — Linux-style PF_PACKET raw sockets.
 *
 * socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL)) hands the caller a
 * subscriber on every NIC.  bind(sockaddr_ll{ifindex}) pins it to
 * one NIC.  recvfrom() pops Ethernet frames from the per-socket
 * queue; sendto(sockaddr_ll{ifindex}) hands the frame to the NIC's
 * xmit op.
 *
 * Caller is responsible for Ethernet headers — substrate doesn't
 * synthesize them.  This is the API DHCP clients (dhclient, udhcpc,
 * dhcpcd) already expect.
 *
 * Socket lifetime: each fd holds one netdev_sub_t plus an fs_node_t
 * adapter so existing read(2)/write(2)/close(2) routes through us
 * without special-casing.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <kern/file.h>
#include <kern/sched.h>
#include <sys/file.h>
#include <sys/netdev.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

/* sockaddr_ll layout matches Linux's <linux/if_packet.h>.  Define
 * locally — substrate doesn't ship a Linux-style if_packet.h yet. */
struct sockaddr_ll_kern {
    uint16_t sll_family;
    uint16_t sll_protocol;
    int32_t  sll_ifindex;
    uint16_t sll_hatype;
    uint8_t  sll_pkttype;
    uint8_t  sll_halen;
    uint8_t  sll_addr[8];
};

#define ETH_P_ALL_NET 0x0003   /* "every protocol" — htons(0x0003) */

#ifndef AF_PACKET
#define AF_PACKET 17
#endif
#ifndef SOCK_RAW
#define SOCK_RAW 3
#endif

typedef struct afpkt_sock {
    struct netdev_sub *sub;
    int                ifindex_bound;   /* 0 = all NICs */
    fs_node_t          node;
    int                closed;
} afpkt_sock_t;

/* ------------------------------------------------------------------ */
/* fs_node adapter — read(2)/write(2)/close(2) on the socket fd      */
/* ------------------------------------------------------------------ */

/* Exposed (non-static) so the sys_recvfrom/sendto dispatch in
 * af_unix.c can identify AF_PACKET fds via node->read pointer
 * comparison.  Not part of any public API. */
size_t afpkt_node_read(fs_node_t *node, off_t off, size_t size, uint8_t *buf) {
    (void)off;
    afpkt_sock_t *s = (afpkt_sock_t *)(uintptr_t)node->impl;
    if (!s || s->closed) return 0;

    /*
     * Honour O_NONBLOCK.  read()/recv() stash the file_t on the thread for the
     * duration of the node op, which is how the AF_UNIX paths recover the flag
     * (see af_unix.c afunix_node_read).  Without this the sleep below is
     * unconditional, so a caller that set O_NONBLOCK with fcntl() and then
     * polls with a wall-clock deadline blocks forever on its first read and
     * the deadline loop never iterates.
     *
     * That is exactly how dhclient hung: it takes an AF_PACKET SOCK_RAW
     * socket, sets O_NONBLOCK via fcntl (sbin/dhclient/dhclient.c), and then
     * runs `while (now_sec() < deadline) { usleep(5000); recv(..., 0); }`.
     * With the flag ignored it stuck on "DHCPDISCOVER ... (try 1/4)" and never
     * retried or gave up, so boot stalled at 20-network whenever nothing
     * answered DHCP (e.g. a tap netdev with no server on the bridge).
     */
    int nonblock = current_thread && current_thread->io_file &&
                   (current_thread->io_file->f_flag & FNONBLOCK);

    for (;;) {
        uint32_t ifindex;
        ssize_t n = netdev_sub_recv(s->sub, buf, size, &ifindex);
        if (n > 0) return (size_t)n;
        if (n < 0) return (size_t)n;
        if (nonblock) return (size_t)-EAGAIN;
        /* No data — sleep on the subscriber's wake channel until a
         * frame arrives.  Interruptible so SIGINT yanks us out. */
        void *chan = netdev_sub_wait_chan(s->sub);
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep(chan);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask)
            return (size_t)-EINTR;
        if (s->closed) return 0;
    }
}

static size_t afpkt_node_write(fs_node_t *node, off_t off, size_t size, const uint8_t *buf) {
    (void)off;
    afpkt_sock_t *s = (afpkt_sock_t *)(uintptr_t)node->impl;
    if (!s || s->closed) return 0;
    /* write() on an AF_PACKET socket sends to the bound interface.
     * If not bound, we don't have anywhere to send — drop it. */
    netdev_t *dev = s->ifindex_bound
        ? netdev_by_index((uint32_t)s->ifindex_bound)
        : NULL;
    if (!dev) return (size_t)-EDESTADDRREQ;
    int rc = netdev_xmit(dev, buf, size);
    if (rc < 0) return (size_t)rc;
    return size;
}

static void afpkt_node_close(fs_node_t *node) {
    afpkt_sock_t *s = (afpkt_sock_t *)(uintptr_t)node->impl;
    if (!s) return;
    s->closed = 1;
    if (s->sub) {
        sched_wakeup(netdev_sub_wait_chan(s->sub));
        netdev_unsubscribe(s->sub);
        s->sub = NULL;
    }
    kfree(s, sizeof(*s));
    node->impl = 0;
}

/* ------------------------------------------------------------------ */
/* Public entry points — called from the AF_UNIX socket dispatcher    */
/* when domain == AF_PACKET.  Same signatures as sys_socket etc.      */
/* ------------------------------------------------------------------ */

static int afpkt_install_fd(afpkt_sock_t *s) {
    int fd = proc_alloc_fd(current_process);
    if (fd < 0) return -1;
    file_t *f = file_alloc();
    if (!f) { proc_clear_fd(current_process, fd); return -1; }
    memset(f, 0, sizeof(*f));
    s->node.flags = FS_FILE;
    s->node.mask  = 0666;
    s->node.read  = afpkt_node_read;
    s->node.write = afpkt_node_write;
    s->node.close = afpkt_node_close;
    s->node.impl  = (uintptr_t)s;
    strlcpy(s->node.name, "<af_packet>", sizeof(s->node.name));
    f->f_data = &s->node;
    f->f_type = DTYPE_VNODE;
    f->f_flag = FREAD | FWRITE;
    f->f_count = 1;
    proc_set_fd(current_process, fd, f);
    return fd;
}

int afpacket_socket(int type, int protocol) {
    (void)protocol;  /* honor ETH_P_ALL; the netdev fanout doesn't filter */
    if (type != SOCK_RAW && type != SOCK_DGRAM)
        return -EPROTONOSUPPORT;

    afpkt_sock_t *s = (afpkt_sock_t *)kmalloc(sizeof(*s));
    if (!s) return -ENOMEM;
    memset(s, 0, sizeof(*s));
    s->sub = netdev_subscribe(0);   /* all NICs by default */
    if (!s->sub) { kfree(s, sizeof(*s)); return -ENOMEM; }

    int fd = afpkt_install_fd(s);
    if (fd < 0) {
        netdev_unsubscribe(s->sub);
        kfree(s, sizeof(*s));
        return -EMFILE;
    }
    return fd;
}

static afpkt_sock_t *afpkt_from_fd(int fd) {
    if (fd < 0 || fd >= MAX_FD) return NULL;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return NULL;
    fs_node_t *n = (fs_node_t *)f->f_data;
    if (n->read != afpkt_node_read) return NULL;   /* not an AF_PACKET fd */
    return (afpkt_sock_t *)(uintptr_t)n->impl;
}

int afpacket_bind(int fd, const struct sockaddr_ll_kern *sll, socklen_t len) {
    afpkt_sock_t *s = afpkt_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!sll || len < (socklen_t)sizeof(*sll)) return -EINVAL;
    if (sll->sll_family != AF_PACKET) return -EAFNOSUPPORT;

    /* Replace subscriber with one filtered to the chosen ifindex. */
    if (s->sub) netdev_unsubscribe(s->sub);
    s->ifindex_bound = sll->sll_ifindex;
    s->sub = netdev_subscribe((uint32_t)sll->sll_ifindex);
    if (!s->sub) return -ENOMEM;
    return 0;
}

ssize_t afpacket_sendto(int fd, const void *buf, size_t len, int flags,
                        const struct sockaddr_ll_kern *to, socklen_t tolen) {
    (void)flags;
    afpkt_sock_t *s = afpkt_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (s->closed) return -EBADF;

    int ifindex = s->ifindex_bound;
    if (to && tolen >= (socklen_t)sizeof(*to)) {
        if (to->sll_family != AF_PACKET) return -EAFNOSUPPORT;
        ifindex = to->sll_ifindex;
    }
    if (ifindex <= 0) return -EDESTADDRREQ;
    netdev_t *dev = netdev_by_index((uint32_t)ifindex);
    if (!dev) return -ENODEV;

    /*
     * SOCK-02: `buf` is a raw userspace pointer from send/sendto/sendmsg and
     * this used to pass it straight to netdev_xmit(), which DMAs it onto the
     * wire.  sendto(fd, (void *)0xC0000000, 1400, ...) therefore transmitted
     * kernel memory, and an unmapped buf faulted in the driver instead of
     * returning EFAULT.  One frame, so one bounce -- no chunking.
     */
    if (!buf && len) return -EINVAL;
    if (len == 0) return 0;
    /* An AF_PACKET frame carries its own link header; bound it to what the
     * device can actually put on the wire rather than trusting `len`. */
    if (len > NETDEV_MTU_MAX) return -EMSGSIZE;

    uint8_t *kbuf = kmalloc(len);
    if (!kbuf) return -ENOMEM;
    if (copyin(buf, kbuf, len) != 0) {
        kfree(kbuf, len);
        return -EFAULT;
    }
    int rc = netdev_xmit(dev, kbuf, len);
    kfree(kbuf, len);
    if (rc < 0) return rc;
    return (ssize_t)len;
}

ssize_t afpacket_recvfrom(int fd, void *buf, size_t len, int flags,
                          struct sockaddr_ll_kern *from, socklen_t *fromlen) {
    afpkt_sock_t *s = afpkt_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (s->closed) return 0;

    /*
     * Respect both the per-call MSG_DONTWAIT and the fd's O_NONBLOCK, matching
     * afinet_recvfrom's TCP arm (af_inet.c).  `flags` was previously discarded
     * outright with (void)flags, so neither mechanism worked and this sleep was
     * unconditional -- see the O_NONBLOCK note in afpkt_node_read above for the
     * dhclient hang this produced.
     */
    {
        int nb = (flags & MSG_DONTWAIT) != 0;
        if (!nb && fd >= 0 && fd < MAX_FD && current_process) {
            file_t *f = current_process->fds[fd];
            nb = (f && (f->f_flag & FNONBLOCK)) ? 1 : 0;
        }
        if (nb) {
            uint32_t ifindex;
            ssize_t n = netdev_sub_recv(s->sub, buf, len, &ifindex);
            if (n == 0) return -EAGAIN;
            if (n < 0) return n;
            if (from && fromlen && *fromlen >= (socklen_t)sizeof(*from)) {
                memset(from, 0, sizeof(*from));
                from->sll_family = AF_PACKET;
                from->sll_ifindex = (int32_t)ifindex;
                netdev_t *d = netdev_by_index(ifindex);
                if (d) {
                    from->sll_halen = NETDEV_HWADDR_LEN;
                    memcpy(from->sll_addr, d->hwaddr, NETDEV_HWADDR_LEN);
                }
                *fromlen = (socklen_t)sizeof(*from);
            }
            return n;
        }
    }

    for (;;) {
        uint32_t ifindex;
        ssize_t n = netdev_sub_recv(s->sub, buf, len, &ifindex);
        if (n > 0) {
            if (from && fromlen && *fromlen >= (socklen_t)sizeof(*from)) {
                memset(from, 0, sizeof(*from));
                from->sll_family = AF_PACKET;
                from->sll_ifindex = (int32_t)ifindex;
                netdev_t *d = netdev_by_index(ifindex);
                if (d) {
                    from->sll_halen = NETDEV_HWADDR_LEN;
                    memcpy(from->sll_addr, d->hwaddr, NETDEV_HWADDR_LEN);
                }
                *fromlen = (socklen_t)sizeof(*from);
            }
            return n;
        }
        if (n < 0) return n;
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep(netdev_sub_wait_chan(s->sub));
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask)
            return -EINTR;
        if (s->closed) return 0;
    }
}
