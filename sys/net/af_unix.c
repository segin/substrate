/*
 * sys/net/af_unix.c — POSIX local-IPC sockets.
 *
 * Minimum-viable AF_UNIX:
 *   - SOCK_STREAM only (datagrams + seqpacket follow the same shape
 *     once a real consumer needs them)
 *   - Filesystem-namespace paths via /run-style globals (a simple
 *     hash-of-the-path linked list; the abstract namespace and the
 *     "tied to a VFS inode" model are deferred)
 *   - socket / socketpair / bind / listen / accept / connect
 *   - read / write through the fs_node_t adapter (so the existing
 *     fd-based read(2)/write(2) syscalls already work)
 *   - send / recv / sendto / recvfrom / sendmsg / recvmsg / shutdown /
 *     getsockname / getpeername / getsockopt / setsockopt — most as
 *     thin wrappers around read/write since substrate has no protocol
 *     options to honour and no scatter-gather buffer machinery yet
 *
 * Buffer model: each socket has ONE rx buffer.  Writes go into the
 * PEER's rx buffer.  Reads come from own rx buffer.  Blocking via
 * sleepq channels on rx (reader-side wait) and tx-room (writer-side
 * wait).
 *
 * Connection establishment:
 *   server: socket → bind(path) → listen → accept (blocks)
 *   client: socket → connect(path) wakes server's accept queue
 *           server's accept allocates a paired peer socket, peers it
 *           with the client, returns new fd
 *   socketpair: skip the namespace dance — directly peer two fresh
 *               sockets and return both.
 *
 * Path lookup is O(N) over the bound list; N is "number of named
 * AF_UNIX servers", typically a handful.  A hash is easy to add.
 *
 * Deferred:
 *   - SCM_RIGHTS (fd passing via cmsg)
 *   - Abstract namespace (\0-prefixed sun_path)
 *   - SOCK_DGRAM (message-oriented; would need per-write framing)
 *   - O_NONBLOCK semantics (always blocking today)
 *   - True SO_* options
 */

#include <vfs/vfs.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/file.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/lock.h>
#include <errno.h>

/* Substrate uses BSD-style msghdr; mirror the user-visible field set
 * for the iov walk in sys_send/recvmsg.  Kernel socket.h has a
 * narrower form, so cast through this struct's interpretation. */
struct iovec_local { void *iov_base; size_t iov_len; };

/* ============================================================
 * Buffer
 * ============================================================ */

#define AFUNIX_BUF_SIZE 4096
#define AFUNIX_PATH_MAX 108
#define AFUNIX_BACKLOG_MAX 32

typedef struct {
    uint8_t  data[AFUNIX_BUF_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} afunix_buf_t;

static size_t afbuf_write(afunix_buf_t *b, const uint8_t *src, size_t n) {
    size_t i = 0;
    while (i < n && b->count < AFUNIX_BUF_SIZE) {
        b->data[b->head] = src[i++];
        b->head = (b->head + 1) % AFUNIX_BUF_SIZE;
        b->count++;
    }
    return i;
}

static size_t afbuf_read(afunix_buf_t *b, uint8_t *dst, size_t n) {
    size_t i = 0;
    while (i < n && b->count > 0) {
        dst[i++] = b->data[b->tail];
        b->tail = (b->tail + 1) % AFUNIX_BUF_SIZE;
        b->count--;
    }
    return i;
}

/* ============================================================
 * Socket
 * ============================================================ */

typedef enum {
    AFUS_UNCONNECTED = 0,
    AFUS_BOUND,
    AFUS_LISTENING,
    AFUS_CONNECTED,
    AFUS_DISCONNECTED,
} afunix_state_t;

typedef struct afunix_sock {
    int             type;       /* SOCK_STREAM only for now */
    afunix_state_t  state;

    /* For data exchange when CONNECTED */
    struct afunix_sock *peer;
    afunix_buf_t   rx;          /* writes from peer land here */

    /* For listeners */
    struct afunix_sock *accept_q[AFUNIX_BACKLOG_MAX];
    int             accept_head;
    int             accept_tail;
    int             accept_count;
    int             backlog;

    /* Wait channels */
    void           *rx_chan;       /* readers wait here */
    void           *tx_chan;       /* writers wait when peer's rx is full */
    void           *accept_chan;   /* accept() waits here */
    void           *connect_chan;  /* connect() waits here for peer setup */

    /* Path namespace */
    char            path[AFUNIX_PATH_MAX];
    int             pathlen;

    /* fs_node_t adapter so the fd can be read/written. */
    fs_node_t       node;
    int             refcount;
    int             closed;
    /* Per-direction half-close state, set by shutdown(2):
     *   rd_closed: this side will no longer accept reads — recv returns EOF.
     *   wr_closed: this side will no longer accept writes — send returns EPIPE,
     *              and peer's reads return EOF once their rx is drained. */
    int             rd_closed;
    int             wr_closed;
    mutex_t         lock;
} afunix_sock_t;

/* Bound-paths list — singly-linked, protected by g_bound_lock. */
static mutex_t        g_bound_lock;
static int            g_bound_lock_inited = 0;

static void g_bound_lock_init(void) {
    if (!g_bound_lock_inited) {
        mutex_init(&g_bound_lock, "af_unix_bound");
        g_bound_lock_inited = 1;
    }
}

static void g_bound_link  (afunix_sock_t *s);
static void g_bound_unlink(afunix_sock_t *s);

/* Bound list embedded via reuse of the unused 'next' slot — store
 * via a separate field to avoid ABI surprises with fs_node_t. */
typedef struct afunix_bound_node {
    afunix_sock_t           *sock;
    struct afunix_bound_node *next;
} afunix_bound_node_t;

static afunix_bound_node_t *g_bound_list = NULL;

static void g_bound_link(afunix_sock_t *s) {
    afunix_bound_node_t *n = (afunix_bound_node_t *)kmalloc(sizeof(*n));
    if (!n) return;
    n->sock = s;
    g_bound_lock_init();
    mutex_lock(&g_bound_lock);
    n->next = g_bound_list;
    g_bound_list = n;
    mutex_unlock(&g_bound_lock);
}

static void g_bound_unlink(afunix_sock_t *s) {
    g_bound_lock_init();
    mutex_lock(&g_bound_lock);
    afunix_bound_node_t **link = &g_bound_list;
    while (*link) {
        if ((*link)->sock == s) {
            afunix_bound_node_t *del = *link;
            *link = del->next;
            mutex_unlock(&g_bound_lock);
            kfree(del, sizeof(*del));
            return;
        }
        link = &(*link)->next;
    }
    mutex_unlock(&g_bound_lock);
}

static afunix_sock_t *afunix_find_bound(const char *path, int len) {
    g_bound_lock_init();
    mutex_lock(&g_bound_lock);
    for (afunix_bound_node_t *n = g_bound_list; n; n = n->next) {
        if (n->sock->pathlen == len && memcmp(n->sock->path, path, len) == 0) {
            afunix_sock_t *s = n->sock;
            mutex_unlock(&g_bound_lock);
            return s;
        }
    }
    mutex_unlock(&g_bound_lock);
    return NULL;
}

/* ============================================================
 * fs_node_t adapter — so existing read(2)/write(2) work on socket fds
 * ============================================================ */

static size_t afunix_node_read(fs_node_t *node, off_t off, size_t size, uint8_t *buf) {
    (void)off;
    afunix_sock_t *s = (afunix_sock_t *)(uintptr_t)node->impl;
    if (!s || s->closed) return 0;
    mutex_lock(&s->lock);
    /* Self-side SHUT_RD: every read is EOF, even with bytes in rx. */
    if (s->rd_closed) { mutex_unlock(&s->lock); return 0; }
    while (s->rx.count == 0) {
        /* EOF conditions: peer gone, peer fully closed, or peer
         * SHUT_WR (which means peer will never produce more). */
        if (!s->peer || s->peer->closed || s->peer->wr_closed) {
            mutex_unlock(&s->lock);
            return 0;
        }
        sleepq_add(s->rx_chan, current_thread);
        mutex_unlock(&s->lock);
        sched_yield();
        mutex_lock(&s->lock);
        /* Re-check the self-side flag in case shutdown(SHUT_RD) raced us. */
        if (s->rd_closed) { mutex_unlock(&s->lock); return 0; }
    }
    size_t r = afbuf_read(&s->rx, buf, size);
    /* Wake the peer's writers — there's space now. */
    if (s->peer) sleepq_wake_all(s->peer->tx_chan);
    mutex_unlock(&s->lock);
    return r;
}

static size_t afunix_node_write(fs_node_t *node, off_t off, size_t size, const uint8_t *buf) {
    (void)off;
    afunix_sock_t *s = (afunix_sock_t *)(uintptr_t)node->impl;
    if (!s || s->closed || !s->peer) return 0;
    /* Own SHUT_WR: no more writes from us. */
    if (s->wr_closed) return 0;
    size_t written = 0;
    while (written < size) {
        afunix_sock_t *peer = s->peer;
        if (!peer || peer->closed || peer->rd_closed) return written;
        mutex_lock(&peer->lock);
        while (peer->rx.count == AFUNIX_BUF_SIZE
               && !peer->closed && !peer->rd_closed && !s->wr_closed) {
            sleepq_add(s->tx_chan, current_thread);
            mutex_unlock(&peer->lock);
            sched_yield();
            mutex_lock(&peer->lock);
        }
        if (peer->closed || peer->rd_closed || s->wr_closed) {
            mutex_unlock(&peer->lock);
            return written;
        }
        size_t w = afbuf_write(&peer->rx, buf + written, size - written);
        written += w;
        sleepq_wake_all(peer->rx_chan);
        mutex_unlock(&peer->lock);
    }
    return written;
}

static void afunix_node_close(fs_node_t *node) {
    afunix_sock_t *s = (afunix_sock_t *)(uintptr_t)node->impl;
    if (!s) return;
    mutex_lock(&s->lock);
    if (--s->refcount > 0) { mutex_unlock(&s->lock); return; }
    s->closed = 1;
    if (s->peer) {
        /* Notify peer of our departure: wake their readers/writers
         * so they see 0-byte read (EOF). */
        afunix_sock_t *peer = s->peer;
        s->peer = NULL;
        peer->peer = NULL;
        sleepq_wake_all(peer->rx_chan);
        sleepq_wake_all(peer->tx_chan);
    }
    if (s->pathlen > 0) g_bound_unlink(s);
    /* Wake any pending accept/connect waiters. */
    sleepq_wake_all(s->accept_chan);
    sleepq_wake_all(s->connect_chan);
    mutex_unlock(&s->lock);
    /* TODO: kfree(s) when refcount reaches 0 AND no waiters.  For
     * now leak the struct so a stray waiter can't UAF.  Small N. */
}

/* ============================================================
 * Helper: create a fresh socket struct
 * ============================================================ */

static afunix_sock_t *afunix_alloc(int type) {
    afunix_sock_t *s = (afunix_sock_t *)kmalloc(sizeof(*s));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    s->type = type;
    s->state = AFUS_UNCONNECTED;
    s->refcount = 1;
    mutex_init(&s->lock, "af_unix_sock");

    /* Wait channels: just unique addresses per-socket; we use the
     * address of fields of the struct itself. */
    s->rx_chan      = &s->rx;
    s->tx_chan      = &s->rx.count;
    s->accept_chan  = &s->accept_q;
    s->connect_chan = &s->state;

    /* fs_node adapter */
    s->node.flags = FS_FILE;   /* not really a file but the dispatch table is uniform */
    s->node.mask  = 0666;
    s->node.read  = afunix_node_read;
    s->node.write = afunix_node_write;
    s->node.close = afunix_node_close;
    s->node.impl  = (uintptr_t)s;
    strncpy(s->node.name, "<socket>", sizeof(s->node.name) - 1);
    return s;
}

/* Install `s` into a fresh fd; return the fd (-1 on failure). */
static int afunix_install_fd(afunix_sock_t *s) {
    int fd = proc_alloc_fd(current_process);
    if (fd < 0) return -1;
    file_t *f = file_alloc();
    if (!f) { proc_clear_fd(current_process, fd); return -1; }
    memset(f, 0, sizeof(*f));
    f->f_data = &s->node;
    f->f_type = DTYPE_VNODE;
    f->f_flag = FREAD | FWRITE;
    f->f_count = 1;
    proc_set_fd(current_process, fd, f);
    return fd;
}

static afunix_sock_t *afunix_from_fd(int fd) {
    if (fd < 0 || fd >= MAX_FD) return NULL;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return NULL;
    fs_node_t *n = (fs_node_t *)f->f_data;
    if (n->read != afunix_node_read) return NULL;   /* not an AF_UNIX fd */
    return (afunix_sock_t *)(uintptr_t)n->impl;
}

/* ============================================================
 * Syscalls
 * ============================================================ */

/* Forward decls for AF_PACKET — sys/net/af_packet.c provides these. */
extern int     afpacket_socket(int type, int protocol);
extern int     afpacket_bind(int fd, const void *sll, socklen_t len);
extern ssize_t afpacket_sendto(int fd, const void *buf, size_t len, int flags,
                               const void *to, socklen_t tolen);
extern ssize_t afpacket_recvfrom(int fd, void *buf, size_t len, int flags,
                                 void *from, socklen_t *fromlen);

/* Forward decls for AF_INET / AF_INET6 — sys/net/af_inet.c provides. */
extern int     afinet_socket(int family, int type, int protocol);
extern int     afinet_bind(int fd, const void *addr, socklen_t len);
extern int     afinet_listen(int fd, int backlog);
extern int     afinet_accept(int fd, void *addr, socklen_t *addrlen);
extern int     afinet_connect(int fd, const void *addr, socklen_t len);
extern ssize_t afinet_sendto(int fd, const void *buf, size_t len, int flags,
                             const void *addr, socklen_t addrlen);
extern ssize_t afinet_recvfrom(int fd, void *buf, size_t len, int flags,
                               void *addr, socklen_t *addrlen);
extern size_t  afinet_node_read(fs_node_t *, off_t, size_t, uint8_t *);

#ifndef AF_PACKET
#define AF_PACKET 17
#endif
#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 10
#endif

int sys_socket(int domain, int type, int protocol) {
    (void)protocol;
    if (domain == AF_PACKET) {
        return afpacket_socket(type, protocol);
    }
    if (domain == AF_INET || domain == AF_INET6) {
        return afinet_socket(domain, type, protocol);
    }
    if (domain != AF_UNIX) { return -EAFNOSUPPORT; }
    if (type != SOCK_STREAM) { return -EPROTONOSUPPORT; }
    afunix_sock_t *s = afunix_alloc(type);
    if (!s) return -ENOMEM;
    int fd = afunix_install_fd(s);
    if (fd < 0) { kfree(s, sizeof(*s)); return -EMFILE; }
    return fd;
}

int sys_socketpair(int domain, int type, int protocol, int sv[2]) {
    (void)protocol;
    if (!sv) return -EFAULT;
    if (domain != AF_UNIX) return -EAFNOSUPPORT;
    if (type != SOCK_STREAM) return -EPROTONOSUPPORT;
    afunix_sock_t *a = afunix_alloc(type);
    afunix_sock_t *b = afunix_alloc(type);
    if (!a || !b) {
        if (a) kfree(a, sizeof(*a));
        if (b) kfree(b, sizeof(*b));
        return -ENOMEM;
    }
    a->peer = b; b->peer = a;
    a->state = b->state = AFUS_CONNECTED;
    int fa = afunix_install_fd(a);
    int fb = afunix_install_fd(b);
    if (fa < 0 || fb < 0) {
        if (fa >= 0) proc_clear_fd(current_process, fa);
        if (fb >= 0) proc_clear_fd(current_process, fb);
        kfree(a, sizeof(*a));
        kfree(b, sizeof(*b));
        return -EMFILE;
    }
    sv[0] = fa; sv[1] = fb;
    return 0;
}

int sys_bind(int fd, const struct sockaddr *addr, socklen_t addrlen) {
    if (!addr || addrlen < 2) return -EINVAL;
    if (addr->sa_family == AF_PACKET) {
        return afpacket_bind(fd, addr, addrlen);
    }
    if (addr->sa_family == AF_INET || addr->sa_family == AF_INET6) {
        return afinet_bind(fd, addr, addrlen);
    }
    if (addr->sa_family != AF_UNIX) return -EAFNOSUPPORT;
    if (addrlen > AFUNIX_PATH_MAX + 2) return -EINVAL;
    afunix_sock_t *s = afunix_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (s->state != AFUS_UNCONNECTED) return -EINVAL;
    socklen_t pathlen = addrlen - 2;
    const char *path = ((const struct sockaddr_un *)addr)->sun_path;
    if (pathlen > AFUNIX_PATH_MAX) pathlen = AFUNIX_PATH_MAX;
    /* Reject already-bound paths. */
    if (afunix_find_bound(path, pathlen)) return -EADDRINUSE;
    mutex_lock(&s->lock);
    memcpy(s->path, path, pathlen);
    s->pathlen = pathlen;
    s->state = AFUS_BOUND;
    mutex_unlock(&s->lock);
    g_bound_link(s);
    return 0;
}

int sys_listen(int fd, int backlog) {
    /* Try AF_INET TCP first. */
    if (fd >= 0 && fd < MAX_FD && current_process) {
        file_t *f = current_process->fds[fd];
        if (f && f->f_data) {
            fs_node_t *n = (fs_node_t *)f->f_data;
            if (n->read == (void *)afinet_node_read)
                return afinet_listen(fd, backlog);
        }
    }
    afunix_sock_t *s = afunix_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (s->state != AFUS_BOUND) return -EINVAL;
    mutex_lock(&s->lock);
    if (backlog < 1) backlog = 1;
    if (backlog > AFUNIX_BACKLOG_MAX) backlog = AFUNIX_BACKLOG_MAX;
    s->backlog = backlog;
    s->state = AFUS_LISTENING;
    mutex_unlock(&s->lock);
    return 0;
}

int sys_accept(int fd, struct sockaddr *addr, socklen_t *addrlen) {
    /* AF_INET TCP first. */
    if (fd >= 0 && fd < MAX_FD && current_process) {
        file_t *f = current_process->fds[fd];
        if (f && f->f_data) {
            fs_node_t *n = (fs_node_t *)f->f_data;
            if (n->read == (void *)afinet_node_read)
                return afinet_accept(fd, addr, addrlen);
        }
    }
    afunix_sock_t *server = afunix_from_fd(fd);
    if (!server) return -ENOTSOCK;
    if (server->state != AFUS_LISTENING) return -EINVAL;

    mutex_lock(&server->lock);
    while (server->accept_count == 0) {
        if (server->closed) { mutex_unlock(&server->lock); return -EBADF; }
        sleepq_add(server->accept_chan, current_thread);
        mutex_unlock(&server->lock);
        sched_yield();
        mutex_lock(&server->lock);
    }
    /* connect() already allocated and peered the server-side socket;
     * we just need to install it into the caller's fd table. */
    afunix_sock_t *server_side = server->accept_q[server->accept_tail];
    server->accept_tail = (server->accept_tail + 1) % AFUNIX_BACKLOG_MAX;
    server->accept_count--;
    mutex_unlock(&server->lock);

    int newfd = afunix_install_fd(server_side);
    if (newfd < 0) {
        /* Tear down: detach from peer and free the orphan. */
        if (server_side->peer) server_side->peer->peer = NULL;
        kfree(server_side, sizeof(*server_side));
        return -EMFILE;
    }
    if (addr && addrlen && *addrlen >= 2) {
        /* No peer address on AF_UNIX without explicit bind. */
        ((struct sockaddr_un *)addr)->sun_family = AF_UNIX;
        ((struct sockaddr_un *)addr)->sun_path[0] = '\0';
        *addrlen = 2;
    }
    return newfd;
}

int sys_connect(int fd, const struct sockaddr *addr, socklen_t addrlen) {
    if (!addr || addrlen < 2) return -EINVAL;
    if (addr->sa_family == AF_INET || addr->sa_family == AF_INET6) {
        return afinet_connect(fd, addr, addrlen);
    }
    if (addr->sa_family != AF_UNIX) return -EAFNOSUPPORT;
    afunix_sock_t *client = afunix_from_fd(fd);
    if (!client) return -ENOTSOCK;
    if (client->state == AFUS_CONNECTED) return -EISCONN;

    socklen_t pathlen = addrlen - 2;
    const char *path = ((const struct sockaddr_un *)addr)->sun_path;
    if (pathlen > AFUNIX_PATH_MAX) pathlen = AFUNIX_PATH_MAX;

    afunix_sock_t *server = afunix_find_bound(path, pathlen);
    if (!server) return -ECONNREFUSED;
    if (server->state != AFUS_LISTENING) return -ECONNREFUSED;

    /* Allocate the server-side socket now and fully peer both halves
     * BEFORE returning from connect().  This matches BSD/Linux: a
     * successful connect() returns as soon as the kernel has queued
     * the connection onto the listen backlog — it does not wait for
     * the server process to actually invoke accept(). */
    afunix_sock_t *server_side = afunix_alloc(SOCK_STREAM);
    if (!server_side) return -ENOMEM;
    server_side->state = AFUS_CONNECTED;
    server_side->peer  = client;
    client->peer       = server_side;
    client->state      = AFUS_CONNECTED;
    /* Inherit the listener's bound path onto server_side so
     * getsockname() on the accepted fd and getpeername() on the
     * client both report the path the client connected to.
     * server_side itself is not entered into the bound-paths list —
     * only the listener is. */
    if (server->pathlen > 0) {
        int n = server->pathlen;
        if (n > AFUNIX_PATH_MAX) n = AFUNIX_PATH_MAX;
        memcpy(server_side->path, server->path, n);
        server_side->pathlen = n;
    }

    mutex_lock(&server->lock);
    if (server->accept_count >= server->backlog) {
        mutex_unlock(&server->lock);
        client->state = AFUS_UNCONNECTED;
        client->peer  = NULL;
        kfree(server_side, sizeof(*server_side));
        return -ECONNREFUSED;
    }
    server->accept_q[server->accept_head] = server_side;
    server->accept_head = (server->accept_head + 1) % AFUNIX_BACKLOG_MAX;
    server->accept_count++;
    sleepq_wake_all(server->accept_chan);
    mutex_unlock(&server->lock);
    return 0;
}

/* Send / recv: degrade to write / read on the socket fd.  flags
 * mostly ignored — MSG_DONTWAIT support would need O_NONBLOCK
 * plumbing through the rx/tx waits.  No SCM cmsg support. */

/* AF_PACKET dispatcher: if `fd` is an AF_PACKET socket, route to
 * afpacket_{send,recv}to with a NULL addr (broadcast to current
 * bound ifindex).  Otherwise fall back to AF_UNIX semantics. */
ssize_t sys_send(int fd, const void *buf, size_t len, int flags) {
    if (fd >= 0 && fd < MAX_FD && current_process) {
        file_t *f = current_process->fds[fd];
        if (f && f->f_data) {
            fs_node_t *n = (fs_node_t *)f->f_data;
            extern size_t afpkt_node_read(fs_node_t *, off_t, size_t, uint8_t *);
            if (n->read == (void *)afpkt_node_read) {
                return afpacket_sendto(fd, buf, len, flags, NULL, 0);
            }
            if (n->read == (void *)afinet_node_read) {
                return afinet_sendto(fd, buf, len, flags, NULL, 0);
            }
        }
    }
    (void)flags;
    afunix_sock_t *s = afunix_from_fd(fd);
    if (!s) return -ENOTSOCK;
    return afunix_node_write(&s->node, 0, len, (const uint8_t *)buf);
}

ssize_t sys_recv(int fd, void *buf, size_t len, int flags) {
    if (fd >= 0 && fd < MAX_FD && current_process) {
        file_t *f = current_process->fds[fd];
        if (f && f->f_data) {
            fs_node_t *n = (fs_node_t *)f->f_data;
            extern size_t afpkt_node_read(fs_node_t *, off_t, size_t, uint8_t *);
            if (n->read == (void *)afpkt_node_read) {
                socklen_t zero = 0;
                return afpacket_recvfrom(fd, buf, len, flags, NULL, &zero);
            }
            if (n->read == (void *)afinet_node_read) {
                socklen_t zero = 0;
                return afinet_recvfrom(fd, buf, len, flags, NULL, &zero);
            }
        }
    }
    (void)flags;
    afunix_sock_t *s = afunix_from_fd(fd);
    if (!s) return -ENOTSOCK;
    return afunix_node_read(&s->node, 0, len, (uint8_t *)buf);
}

ssize_t sys_sendto(int fd, const void *buf, size_t len, int flags,
                   const struct sockaddr *addr, socklen_t addrlen) {
    /* Route by destination address family first. */
    if (addr && addrlen >= 2) {
        if (addr->sa_family == AF_PACKET)
            return afpacket_sendto(fd, buf, len, flags, addr, addrlen);
        if (addr->sa_family == AF_INET || addr->sa_family == AF_INET6)
            return afinet_sendto(fd, buf, len, flags, addr, addrlen);
    }
    /* No addr: route by fd type. */
    if (fd >= 0 && fd < MAX_FD && current_process) {
        file_t *f = current_process->fds[fd];
        if (f && f->f_data) {
            fs_node_t *n = (fs_node_t *)f->f_data;
            if (n->read && n->read != afunix_node_read) {
                extern size_t afpkt_node_read(fs_node_t *, off_t, size_t, uint8_t *);
                if (n->read == (void *)afpkt_node_read)
                    return afpacket_sendto(fd, buf, len, flags, addr, addrlen);
                if (n->read == (void *)afinet_node_read)
                    return afinet_sendto(fd, buf, len, flags, addr, addrlen);
            }
        }
    }
    (void)addrlen;
    return sys_send(fd, buf, len, flags);
}

ssize_t sys_recvfrom(int fd, void *buf, size_t len, int flags,
                     struct sockaddr *addr, socklen_t *addrlen) {
    if (fd >= 0 && fd < MAX_FD && current_process) {
        file_t *f = current_process->fds[fd];
        if (f && f->f_data) {
            fs_node_t *n = (fs_node_t *)f->f_data;
            extern size_t afpkt_node_read(fs_node_t *, off_t, size_t, uint8_t *);
            if (n->read == (void *)afpkt_node_read) {
                return afpacket_recvfrom(fd, buf, len, flags, addr, addrlen);
            }
            if (n->read == (void *)afinet_node_read) {
                return afinet_recvfrom(fd, buf, len, flags, addr, addrlen);
            }
        }
    }
    (void)addr; (void)addrlen;
    return sys_recv(fd, buf, len, flags);
}

ssize_t sys_sendmsg(int fd, const struct msghdr *msg, int flags) {
    if (!msg) return -EFAULT;
    ssize_t total = 0;
    struct iovec_local *iov = (struct iovec_local *)msg->msg_iov;
    for (int i = 0; i < (int)msg->msg_iovlen; i++) {
        ssize_t r = sys_send(fd, iov[i].iov_base, iov[i].iov_len, flags);
        if (r < 0) return total > 0 ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}

ssize_t sys_recvmsg(int fd, struct msghdr *msg, int flags) {
    if (!msg) return -EFAULT;
    ssize_t total = 0;
    struct iovec_local *iov = (struct iovec_local *)msg->msg_iov;
    for (int i = 0; i < (int)msg->msg_iovlen; i++) {
        ssize_t r = sys_recv(fd, iov[i].iov_base, iov[i].iov_len, flags);
        if (r < 0) return total > 0 ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}

int sys_shutdown(int fd, int how) {
    afunix_sock_t *s = afunix_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR) return -EINVAL;

    mutex_lock(&s->lock);
    if (how == SHUT_RD || how == SHUT_RDWR) {
        s->rd_closed = 1;
        /* Wake our own readers so they return 0 (EOF) immediately. */
        sleepq_wake_all(s->rx_chan);
        /* Peer's writers may have been blocked on full rx; with our
         * read side dead, their writes should error out. */
        if (s->peer) sleepq_wake_all(s->peer->tx_chan);
    }
    if (how == SHUT_WR || how == SHUT_RDWR) {
        s->wr_closed = 1;
        /* Wake our own writers (they can no longer write). */
        sleepq_wake_all(s->tx_chan);
        /* Peer's readers see EOF once their buffer drains. */
        if (s->peer) sleepq_wake_all(s->peer->rx_chan);
    }
    mutex_unlock(&s->lock);
    return 0;
}

/* AF_INET surface lives in af_inet.c.  Try it first — most callers
 * (curl, inetd, every internet daemon) are looking up AF_INET fds.
 * Fall through to AF_UNIX only on ENOTSOCK.  */
extern int afinet_getsockname(int fd, void *addr, socklen_t *addrlen);
extern int afinet_getpeername(int fd, void *addr, socklen_t *addrlen);

int sys_getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen) {
    if (!addr || !addrlen) return -EINVAL;
    int rc = afinet_getsockname(fd, addr, addrlen);
    if (rc != -ENOTSOCK) return rc;

    afunix_sock_t *s = afunix_from_fd(fd);
    if (!s) return -ENOTSOCK;
    struct sockaddr_un *un = (struct sockaddr_un *)addr;
    un->sun_family = AF_UNIX;
    int plen = s->pathlen;
    if (plen > (int)*addrlen - 2) plen = *addrlen - 2;
    if (plen > 0) memcpy(un->sun_path, s->path, plen);
    if (plen < AFUNIX_PATH_MAX) un->sun_path[plen] = '\0';
    *addrlen = 2 + plen;
    return 0;
}

int sys_getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen) {
    if (!addr || !addrlen) return -EINVAL;
    int rc = afinet_getpeername(fd, addr, addrlen);
    if (rc != -ENOTSOCK) return rc;

    afunix_sock_t *s = afunix_from_fd(fd);
    if (!s) return -ENOTSOCK;
    if (!s->peer) return -ENOTCONN;
    struct sockaddr_un *un = (struct sockaddr_un *)addr;
    un->sun_family = AF_UNIX;
    int plen = s->peer->pathlen;
    if (plen > (int)*addrlen - 2) plen = *addrlen - 2;
    if (plen > 0) memcpy(un->sun_path, s->peer->path, plen);
    if (plen < AFUNIX_PATH_MAX) un->sun_path[plen] = '\0';
    *addrlen = 2 + plen;
    return 0;
}

int sys_setsockopt(int fd, int level, int optname,
                   const void *optval, socklen_t optlen) {
    (void)fd; (void)level; (void)optname; (void)optval; (void)optlen;
    /* No options honoured; accept silently. */
    return 0;
}

/* Forward-decl the AF_INET helper.  af_inet.c exposes the per-socket
 * so_error so SO_ERROR can return the real connect-failure errno
 * instead of always 0 (which made curl think the connection had
 * succeeded and proceed to send into a dead PCB).  */
extern int afinet_so_error(int fd);

int sys_getsockopt(int fd, int level, int optname,
                   void *optval, socklen_t *optlen) {
    if (!optval || !optlen || *optlen < (socklen_t)sizeof(int))
        return -EINVAL;

    #define SOL_SOCKET_K   1
    #define SO_ERROR_K     4
    #define SO_TYPE_K      3
    #define SO_ACCEPTCONN_K 30
    if (level == SOL_SOCKET_K) {
        if (optname == SO_ERROR_K) {
            int err = afinet_so_error(fd);
            *(int *)optval = err;
            *optlen = sizeof(int);
            kprintf("getsockopt(SO_ERROR) fd=%d -> %d\n", fd, err);
            return 0;
        }
    }
    *(int *)optval = 0;
    *optlen = sizeof(int);
    return 0;
}
