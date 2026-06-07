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
#include <sys/poll.h>
#include <kern/console.h>
#include <kern/cmdline.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/file.h>
#include <vm/vm_kmem.h>
#include <sys/copy.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/lock.h>
#include <errno.h>

/* X-server fd-lifecycle trace, gated behind the `xfd` kernel cmdline
 * flag (or `debug=xfd`).  Logs accept/connect/read/write/close on
 * AF_UNIX sockets so a hang in the X-server <-> xtrace handshake
 * is visible from the kernel side without having to instrument
 * Xfbdev.  Off by default — `xfd` cmdline keyword enables. */
static int xfd_trace_cached = -1;
static inline int xfd_trace(void) {
    if (xfd_trace_cached < 0) {
        xfd_trace_cached =
            (cmdline_has("xfd") || cmdline_debug_enabled("xfd")) ? 1 : 0;
    }
    return xfd_trace_cached;
}
#define XFD(fmt, ...) do { \
    if (xfd_trace()) kprintf("xfd: " fmt "\n", ##__VA_ARGS__); \
} while (0)

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
#define AFUNIX_FDQ_MAX     16    /* maximum SCM_RIGHTS fds queued per socket */

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
    /* SCM_RIGHTS-passed file references queued to be received here.
     * Writer (sendmsg) bumps file_t refcount and enqueues; reader
     * (recvmsg) dequeues, allocates a fresh fd slot in the receiving
     * process, installs the file_t, drops the ref. */
    file_t         *rx_fdq[AFUNIX_FDQ_MAX];
    int             rx_fdq_count;
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

/* Canonical AF_UNIX path length from a sockaddr_un addrlen.  A pathname socket
 * is a NUL-terminated string, so callers may pass any addrlen >= the real
 * length (SUN_LEN, or the full sizeof(sockaddr_un) with trailing padding) — bind
 * and connect must agree, so trim at the first NUL.  An abstract socket
 * (sun_path[0]=='\0') has no terminator; its whole addrlen-2 is significant. */
static int afunix_canon_pathlen(const char *path, socklen_t addrlen) {
    int max = (addrlen >= 2) ? (int)addrlen - 2 : 0;
    if (max > AFUNIX_PATH_MAX) max = AFUNIX_PATH_MAX;
    if (max > 0 && path[0] == '\0')
        return max;                 /* abstract: length is significant */
    int n = 0;
    while (n < max && path[n] != '\0') n++;
    return n;
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
    XFD("node_read pid=%d s=%p closed=%d rx.count=%u size=%u",
        current_process ? (int)current_process->pid : -1,
        s, s ? s->closed : -1,
        s ? (unsigned)s->rx.count : 0,
        (unsigned)size);
    if (!s || s->closed) return 0;
    int nonblock = current_thread && current_thread->io_file &&
                   (current_thread->io_file->f_flag & FNONBLOCK);
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
        /* Empty buffer with a live peer.  Non-blocking fd: EAGAIN, don't
         * sleep.  Otherwise sleep INTERRUPTIBLY so a signal can break the
         * read out instead of wedging the reader forever. */
        if (nonblock) { mutex_unlock(&s->lock); return (size_t)-EAGAIN; }
        if (current_thread &&
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            mutex_unlock(&s->lock);
            return (size_t)-EINTR;
        }
        if (current_thread)
            current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sleepq_add(s->rx_chan, current_thread);
        mutex_unlock(&s->lock);
        sched_yield();
        if (current_thread)
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread &&
            (current_thread->sig_pending & ~current_thread->sig_mask))
            return (size_t)-EINTR;
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
    XFD("node_write pid=%d s=%p closed=%d peer=%p size=%u",
        current_process ? (int)current_process->pid : -1,
        s, s ? s->closed : -1,
        s ? s->peer : NULL,
        (unsigned)size);
    if (!s || s->closed || !s->peer) return 0;
    /* Own SHUT_WR: no more writes from us. */
    if (s->wr_closed) return 0;
    /* O_NONBLOCK is carried on the file_t, which only the syscall layer sees;
     * it stashes it on the thread (current_thread->io_file) for the duration
     * of the write so we can reach it from this fs_node callback. */
    int nonblock = current_thread && current_thread->io_file &&
                   (current_thread->io_file->f_flag & FNONBLOCK);
    size_t written = 0;
    while (written < size) {
        afunix_sock_t *peer = s->peer;
        if (!peer || peer->closed || peer->rd_closed) return written;
        mutex_lock(&peer->lock);
        while (peer->rx.count == AFUNIX_BUF_SIZE
               && !peer->closed && !peer->rd_closed && !s->wr_closed) {
            /* Send buffer full.  A non-blocking fd must not sleep: hand back
             * the partial count, or EAGAIN if nothing went out yet. */
            if (nonblock) {
                mutex_unlock(&peer->lock);
                return written ? written : (size_t)-EAGAIN;
            }
            /* Blocking, but INTERRUPTIBLY: a pending signal has to break the
             * write out.  Without this a peer that never drains wedges the
             * writer in an uninterruptible sleep — the AF_UNIX X client/server
             * deadlock that froze the desktop. */
            if (current_thread &&
                (current_thread->sig_pending & ~current_thread->sig_mask)) {
                mutex_unlock(&peer->lock);
                return written ? written : (size_t)-EINTR;
            }
            if (current_thread)
                current_thread->flags |= THREAD_F_INTERRUPTIBLE;
            sleepq_add(s->tx_chan, current_thread);
            mutex_unlock(&peer->lock);
            sched_yield();
            if (current_thread)
                current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
            if (current_thread &&
                (current_thread->sig_pending & ~current_thread->sig_mask))
                return written ? written : (size_t)-EINTR;
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
    XFD("node_close pid=%d s=%p refcount=%d state=%d closed=%d peer=%p",
        current_process ? (int)current_process->pid : -1,
        s,
        s ? s->refcount : -1,
        s ? (int)s->state : -1,
        s ? s->closed : -1,
        s ? s->peer : NULL);
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

/*
 * poll(2) / select(2) readiness for an AF_UNIX socket.
 *
 * Without an explicit handler the generic poll_fs() treats the
 * socket's fs_node as a regular file and reports it permanently
 * readable+writable — so a server polling an idle listener sees a
 * phantom connection, calls accept(), and blocks forever.
 */
static int afunix_node_poll(fs_node_t *node, void *waiter) {
    afunix_sock_t *s = (afunix_sock_t *)(uintptr_t)node->impl;
    if (!s) return POLLNVAL;

    int ev = 0;
    mutex_lock(&s->lock);

    if (s->closed) {
        mutex_unlock(&s->lock);
        return POLLHUP;
    }

    switch (s->state) {
    case AFUS_LISTENING:
        /* Readable only when accept() will actually return a fd. */
        if (s->accept_count > 0)
            ev |= POLLIN | POLLRDNORM;
        else if (waiter)
            *(void **)waiter = s->accept_chan;
        break;

    case AFUS_CONNECTED: {
        afunix_sock_t *peer = s->peer;
        /* Readable: buffered data, or EOF — recv() returns without
         * blocking when the peer is gone / write-closed, or this
         * side's read half is shut down. */
        if (s->rx.count > 0 || s->rd_closed ||
            !peer || peer->closed || peer->wr_closed)
            ev |= POLLIN | POLLRDNORM;
        /* Writable: peer alive, still reading, and has buffer room. */
        if (peer && !peer->closed && !peer->rd_closed && !s->wr_closed &&
            peer->rx.count < AFUNIX_BUF_SIZE)
            ev |= POLLOUT | POLLWRNORM;
        if (!peer || peer->closed)
            ev |= POLLHUP;
        if (!(ev & (POLLIN | POLLRDNORM)) && waiter)
            *(void **)waiter = s->rx_chan;
        break;
    }

    default:    /* UNCONNECTED / BOUND / DISCONNECTED */
        ev |= POLLHUP;
        break;
    }

    mutex_unlock(&s->lock);
    return ev;
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
    s->node.poll  = afunix_node_poll;
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
extern int     afinet_shutdown(int fd, int how);

#ifndef AF_PACKET
#define AF_PACKET 17
#endif
#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 10
#endif

/* Apply SOCK_CLOEXEC / SOCK_NONBLOCK to a freshly-created socket fd.
 * Callers pass the original (unmasked) type so the flag bits are
 * still visible here. */
static void socket_apply_type_flags(int fd, int orig_type) {
    if (fd < 0 || fd >= MAX_FD) return;
    if (orig_type & SOCK_CLOEXEC) {
        fdset_set(current_process->fd_cloexec, fd);
    }
    if (orig_type & SOCK_NONBLOCK) {
        file_t *f = current_process->fds[fd];
        if (f) f->f_flag |= FNONBLOCK;
    }
}

int sys_socket(int domain, int type, int protocol) {
    (void)protocol;
    /* Strip the SOCK_CLOEXEC / SOCK_NONBLOCK flag bits before the
     * base-type check — modern callers (OpenSSH, curl, ...) OR them
     * into `type`, and an exact-equality compare against SOCK_STREAM
     * would otherwise reject every CLOEXEC socket with
     * EPROTONOSUPPORT. */
    int base = type & SOCK_TYPE_MASK;
    int fd;

    if (domain == AF_PACKET) {
        fd = afpacket_socket(base, protocol);
    } else if (domain == AF_INET || domain == AF_INET6) {
        fd = afinet_socket(domain, base, protocol);
    } else if (domain != AF_UNIX) {
        return -EAFNOSUPPORT;
    } else if (base != SOCK_STREAM) {
        return -EPROTONOSUPPORT;
    } else {
        afunix_sock_t *s = afunix_alloc(base);
        if (!s) return -ENOMEM;
        fd = afunix_install_fd(s);
        if (fd < 0) { kfree(s, sizeof(*s)); return -EMFILE; }
    }

    if (fd >= 0) socket_apply_type_flags(fd, type);
    return fd;
}

int sys_socketpair(int domain, int type, int protocol, int sv[2]) {
    (void)protocol;
    if (!sv) return -EFAULT;
    if (domain != AF_UNIX) return -EAFNOSUPPORT;
    /* Strip SOCK_CLOEXEC / SOCK_NONBLOCK before the base-type check. */
    int base = type & SOCK_TYPE_MASK;
    if (base != SOCK_STREAM) return -EPROTONOSUPPORT;
    afunix_sock_t *a = afunix_alloc(base);
    afunix_sock_t *b = afunix_alloc(base);
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
    socket_apply_type_flags(fa, type);
    socket_apply_type_flags(fb, type);
    sv[0] = fa; sv[1] = fb;
    return 0;
}

int sys_bind(int fd, const struct sockaddr *addr, socklen_t addrlen) {
    if (addr && addr->sa_family == AF_UNIX) {
        const char *p = ((const struct sockaddr_un *)addr)->sun_path;
        XFD("sys_bind pid=%d fd=%d af_unix path='%.*s'",
            current_process ? (int)current_process->pid : -1, fd,
            (int)(addrlen >= 2 ? addrlen - 2 : 0),
            p ? p : "(null)");
    }
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
    const char *path = ((const struct sockaddr_un *)addr)->sun_path;
    socklen_t pathlen = afunix_canon_pathlen(path, addrlen);
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
    XFD("sys_listen pid=%d fd=%d backlog=%d",
        current_process ? (int)current_process->pid : -1, fd, backlog);
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
    XFD("sys_accept ENTER pid=%d fd=%d",
        current_process ? (int)current_process->pid : -1, fd);
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
    if (!server) { XFD("sys_accept fd=%d ENOTSOCK", fd); return -ENOTSOCK; }
    if (server->state != AFUS_LISTENING) {
        XFD("sys_accept fd=%d not LISTENING (state=%d)", fd, server->state);
        return -EINVAL;
    }

    mutex_lock(&server->lock);
    while (server->accept_count == 0) {
        if (server->closed) { mutex_unlock(&server->lock); return -EBADF; }
        /* Interruptible wait: a pending signal must break accept()
         * out, otherwise a wedged server cannot be killed and holds
         * its controlling terminal forever. */
        if (current_thread->sig_pending & ~current_thread->sig_mask) {
            mutex_unlock(&server->lock);
            return -EINTR;
        }
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sleepq_add(server->accept_chan, current_thread);
        mutex_unlock(&server->lock);
        sched_yield();
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        if (current_thread->sig_pending & ~current_thread->sig_mask)
            return -EINTR;
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
        XFD("sys_accept fd=%d install_fd failed (EMFILE)", fd);
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
    XFD("sys_accept fd=%d -> newfd=%d server_side=%p peer=%p",
        fd, newfd, server_side, server_side->peer);
    return newfd;
}

int sys_connect(int fd, const struct sockaddr *addr, socklen_t addrlen) {
    XFD("sys_connect ENTER pid=%d fd=%d family=%d",
        current_process ? (int)current_process->pid : -1,
        fd, addr ? addr->sa_family : -1);
    if (!addr || addrlen < 2) return -EINVAL;
    if (addr->sa_family == AF_INET || addr->sa_family == AF_INET6) {
        return afinet_connect(fd, addr, addrlen);
    }
    if (addr->sa_family != AF_UNIX) return -EAFNOSUPPORT;
    afunix_sock_t *client = afunix_from_fd(fd);
    if (!client) return -ENOTSOCK;
    if (client->state == AFUS_CONNECTED) return -EISCONN;

    const char *path = ((const struct sockaddr_un *)addr)->sun_path;
    socklen_t pathlen = afunix_canon_pathlen(path, addrlen);

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
    XFD("sys_connect fd=%d OK client=%p server_side=%p accept_count=%d",
        fd, client, server_side, server->accept_count);
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

/* Bytes copied through the kernel bounce buffer in one recv call.  Stream
 * recv may return short (the caller loops); UDP / RAW / AF_PACKET datagrams
 * are <= 1500 so they always fit; only an AF_UNIX datagram larger than this
 * would truncate, which no real caller hits.  kmalloc handles this via its
 * large-allocation path (KMEM_MAX_ALLOC is 128 MiB). */
#define RECV_BOUNCE_CAP (64U * 1024U)

/*
 * Deliver a received message into a KERNEL buffer, dispatching by fd type.
 * The transport readers (tcp_recv, afinet/afpacket/afunix) write straight
 * to the buffer they are handed; routing them through a kernel buffer here
 * — rather than the raw userspace pointer — is what keeps a not-present or
 * unbacked user page from taking an unrecoverable kernel page fault (the
 * recv() OOM panic).  kaddr/kaddrlen, when non-NULL, receive the source
 * address in kernel space for the caller to copy out.
 */
static ssize_t recv_into_kbuf(int fd, void *kbuf, size_t len, int flags,
                              struct sockaddr *kaddr, socklen_t *kaddrlen) {
    file_t *f = current_process->fds[fd];
    if (f && f->f_data) {
        fs_node_t *n = (fs_node_t *)f->f_data;
        extern size_t afpkt_node_read(fs_node_t *, off_t, size_t, uint8_t *);
        socklen_t zero = 0;
        socklen_t *alp = kaddrlen ? kaddrlen : &zero;
        if (n->read == (void *)afpkt_node_read)
            return afpacket_recvfrom(fd, kbuf, len, flags, kaddr, alp);
        if (n->read == (void *)afinet_node_read)
            return afinet_recvfrom(fd, kbuf, len, flags, kaddr, alp);
    }
    if (kaddrlen) *kaddrlen = 0;       /* AF_UNIX has no source address */
    afunix_sock_t *s = afunix_from_fd(fd);
    if (!s) return -ENOTSOCK;
    return afunix_node_read(&s->node, 0, len, (uint8_t *)kbuf);
}

/* Common recv/recvfrom body: receive into a kernel bounce buffer, then copy
 * the data (and any source address) out to userspace fault-safely. */
static ssize_t do_recv(int fd, void *buf, size_t len, int flags,
                       struct sockaddr *addr, socklen_t *addrlen) {
    uint8_t    kaddr[128];
    socklen_t  kaddrlen = sizeof(kaddr);
    void      *kbuf;
    size_t     cap;
    ssize_t    n;

    /* A closed or out-of-range fd is EBADF — distinct from a live fd that
     * simply is not a socket (ENOTSOCK, returned by recv_into_kbuf). */
    if (fd < 0 || fd >= MAX_FD || !current_process || !current_process->fds[fd])
        return -EBADF;
    if (len == 0)
        return 0;
    memset(kaddr, 0, sizeof(kaddr));

    cap = len < RECV_BOUNCE_CAP ? len : RECV_BOUNCE_CAP;
    kbuf = kmalloc(cap);
    if (!kbuf)
        return -ENOMEM;

    n = recv_into_kbuf(fd, kbuf, cap, flags,
                       addr ? (struct sockaddr *)kaddr : NULL,
                       addr ? &kaddrlen : NULL);
    if (n < 0) {
        kfree(kbuf, cap);
        return n;
    }
    if (n > 0 && copyout(kbuf, buf, (size_t)n) != 0) {
        kfree(kbuf, cap);
        return -EFAULT;
    }
    kfree(kbuf, cap);

    if (addr && addrlen) {
        socklen_t user_cap = 0;
        if (copyin(addrlen, &user_cap, sizeof(user_cap)) != 0)
            return -EFAULT;
        if (kaddrlen > sizeof(kaddr))
            kaddrlen = sizeof(kaddr);
        socklen_t out = kaddrlen < user_cap ? kaddrlen : user_cap;
        if (out > 0 && copyout(kaddr, addr, out) != 0)
            return -EFAULT;
        if (copyout(&kaddrlen, addrlen, sizeof(kaddrlen)) != 0)
            return -EFAULT;
    }
    return n;
}

ssize_t sys_recv(int fd, void *buf, size_t len, int flags) {
    return do_recv(fd, buf, len, flags, NULL, NULL);
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
    return do_recv(fd, buf, len, flags, addr, addrlen);
}

/* Kernel-side cmsghdr accessor — mirrors the user-space CMSG_*
 * macros for parsing/building the ancillary-data area. */
struct kcmsghdr {
    uint32_t cmsg_len;
    int      cmsg_level;
    int      cmsg_type;
};
#define KCMSG_ALIGN(n)  (((n) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))

/* sendmsg with SCM_RIGHTS support — parse the cmsghdr area for any
 * SCM_RIGHTS records, bump file_t refcount for each fd, queue the
 * file_t on the peer socket's rx_fdq.  Then run the iov loop as a
 * plain send() chain.  msg_control is read directly from the user
 * pointer (substrate's syscall layer treats msghdr fields as already
 * validated; tightening this is a separate audit). */
ssize_t sys_sendmsg(int fd, const struct msghdr *msg, int flags) {
    if (!msg) return -EFAULT;
    afunix_sock_t *s = afunix_from_fd(fd);

    /* SCM_RIGHTS plumbing only applies to AF_UNIX peers.  AF_INET
     * sockets fall through to the iov-only path below. */
    if (s && s->peer && msg->msg_control && msg->msg_controllen > 0) {
        const unsigned char *cmsgbuf = (const unsigned char *)msg->msg_control;
        size_t cmsglen = msg->msg_controllen;
        size_t off = 0;
        while (off + sizeof(struct kcmsghdr) <= cmsglen) {
            const struct kcmsghdr *c = (const struct kcmsghdr *)(cmsgbuf + off);
            if (c->cmsg_len < sizeof(*c) || off + c->cmsg_len > cmsglen)
                return -EINVAL;
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
                size_t datalen = c->cmsg_len - sizeof(*c);
                if (datalen % sizeof(int) != 0) return -EINVAL;
                int nfds = (int)(datalen / sizeof(int));
                const int *fds = (const int *)(cmsgbuf + off + sizeof(*c));
                /* Look up each fd in the sender's table.  Queue them
                 * on the peer's rx_fdq.  All-or-nothing: if any fd is
                 * invalid or the queue would overflow, undo previous
                 * fref's and fail. */
                mutex_lock(&s->peer->lock);
                if (s->peer->rx_fdq_count + nfds > AFUNIX_FDQ_MAX) {
                    mutex_unlock(&s->peer->lock);
                    return -EMSGSIZE;
                }
                int queued = 0;
                for (int i = 0; i < nfds; i++) {
                    if (fds[i] < 0 || fds[i] >= MAX_FD) goto fd_fail;
                    file_t *f = current_process->fds[fds[i]];
                    if (!f) goto fd_fail;
                    f->f_count++;
                    s->peer->rx_fdq[s->peer->rx_fdq_count + queued] = f;
                    queued++;
                    continue;
                fd_fail:
                    /* Roll back. */
                    for (int j = 0; j < queued; j++) {
                        file_t *qf = s->peer->rx_fdq[s->peer->rx_fdq_count + j];
                        if (qf && qf->f_count > 0) qf->f_count--;
                        s->peer->rx_fdq[s->peer->rx_fdq_count + j] = NULL;
                    }
                    mutex_unlock(&s->peer->lock);
                    return -EBADF;
                }
                s->peer->rx_fdq_count += queued;
                mutex_unlock(&s->peer->lock);
            }
            off += KCMSG_ALIGN(c->cmsg_len);
        }
    }

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

/* recvmsg with SCM_RIGHTS support — run the iov reads first, then
 * if the AF_UNIX socket has pending fds in rx_fdq, install them into
 * the calling process's fd table and emit a SCM_RIGHTS cmsg into
 * msg_control. */
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

    /* AF_UNIX rx_fdq drain.  Only applies once the iov loop above has
     * pulled at least one byte (recvmsg without data wouldn't carry
     * an SCM payload on a stream socket). */
    afunix_sock_t *s = afunix_from_fd(fd);
    if (s && msg->msg_control && (size_t)msg->msg_controllen >= sizeof(struct kcmsghdr)) {
        mutex_lock(&s->lock);
        int nfds = s->rx_fdq_count;
        if (nfds > 0) {
            size_t need = KCMSG_ALIGN(sizeof(struct kcmsghdr) + (size_t)nfds * sizeof(int));
            if (need > (size_t)msg->msg_controllen) {
                /* Truncate — fewer fds than queued — but still deliver
                 * what fits and mark MSG_CTRUNC. */
                nfds = (int)(((size_t)msg->msg_controllen - sizeof(struct kcmsghdr)) / sizeof(int));
                if (nfds < 0) nfds = 0;
                msg->msg_flags |= MSG_CTRUNC;
            }
            /* Allocate fds in the receiver process. */
            int allocated[AFUNIX_FDQ_MAX];
            int got = 0;
            for (int i = 0; i < nfds; i++) {
                int newfd = proc_alloc_fd(current_process);
                if (newfd < 0) {
                    /* Out of fds — roll back, leave undelivered in queue. */
                    for (int j = 0; j < got; j++) {
                        proc_set_fd(current_process, allocated[j], NULL);
                    }
                    nfds = got;
                    msg->msg_flags |= MSG_CTRUNC;
                    break;
                }
                proc_set_fd(current_process, newfd, s->rx_fdq[i]);
                /* refcount was already bumped on the sender side;
                 * proc_set_fd takes ownership of that reference, so
                 * we do NOT bump again here. */
                allocated[got++] = newfd;
            }
            /* Write the cmsg out. */
            struct kcmsghdr *c = (struct kcmsghdr *)msg->msg_control;
            c->cmsg_len   = sizeof(*c) + (uint32_t)got * sizeof(int);
            c->cmsg_level = SOL_SOCKET;
            c->cmsg_type  = SCM_RIGHTS;
            int *outfds = (int *)((unsigned char *)msg->msg_control + sizeof(*c));
            for (int i = 0; i < got; i++) outfds[i] = allocated[i];
            msg->msg_controllen = (uint32_t)(sizeof(*c) + (uint32_t)got * sizeof(int));
            /* Shift remaining unqueued fds down. */
            for (int i = got; i < s->rx_fdq_count; i++) {
                s->rx_fdq[i - got] = s->rx_fdq[i];
            }
            s->rx_fdq_count -= got;
        } else {
            msg->msg_controllen = 0;
        }
        mutex_unlock(&s->lock);
    } else if (msg->msg_control && msg->msg_controllen > 0) {
        msg->msg_controllen = 0;
    }
    return total;
}

int sys_shutdown(int fd, int how) {
    /* AF_INET first — route by fd type, same as sys_accept/sys_recv. */
    if (fd >= 0 && fd < MAX_FD && current_process) {
        file_t *f = current_process->fds[fd];
        if (f && f->f_data) {
            fs_node_t *n = (fs_node_t *)f->f_data;
            if (n->read == (void *)afinet_node_read)
                return afinet_shutdown(fd, how);
        }
    }
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
            return 0;
        }
        if (optname == SO_TYPE_K) {
            /* Report the socket's actual type (SOCK_STREAM / SOCK_DGRAM / ...).
             * libtirpc's svc_tli_create switches on getsockopt(SO_TYPE) to pick
             * its transport; returning 0 here made it reject every RPC server
             * socket with "bad service type" (broke rpcbind / ToolTalk). */
            extern int afinet_so_type(int fd);
            afunix_sock_t *u = afunix_from_fd(fd);
            int t = u ? u->type : afinet_so_type(fd);
            if (t < 0)
                return -ENOTSOCK;
            *(int *)optval = t;
            *optlen = sizeof(int);
            return 0;
        }
    }
    *(int *)optval = 0;
    *optlen = sizeof(int);
    return 0;
}
