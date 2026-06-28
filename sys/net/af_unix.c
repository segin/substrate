/*
 * sys/net/af_unix.c — POSIX local-IPC sockets.
 *
 * Minimum-viable AF_UNIX:
 *   - SOCK_STREAM and SOCK_DGRAM (datagrams are length-framed in the rx
 *     ring; seqpacket follows the same shape once a consumer needs it)
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
 *   - O_NONBLOCK semantics (always blocking today)
 *   - True SO_* options
 */

#include <vfs/vfs.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <kern/console.h>
#include <kern/cmdline.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <arch/i386/intr.h>
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

/* POSIX EBADF vs ENOTSOCK: a syscall on an fd that isn't open at all is
 * EBADF; a valid-but-not-a-socket fd is ENOTSOCK (handled downstream).
 * The socket dispatchers check this up front so bind/listen/accept/...
 * on a closed/garbage fd return EBADF rather than ENOTSOCK. */
static int sock_fd_invalid(int fd) {
    return fd < 0 || fd >= MAX_FD || !current_process || !current_process->fds[fd];
}

/* Substrate uses BSD-style msghdr; mirror the user-visible field set
 * for the iov walk in sys_send/recvmsg.  Kernel socket.h has a
 * narrower form, so cast through this struct's interpretation. */
struct iovec_local { void *iov_base; size_t iov_len; };

/* ============================================================
 * Buffer
 * ============================================================ */

#define AFUNIX_BUF_SIZE 262144
#define AFUNIX_PATH_MAX 108
#define AFUNIX_BACKLOG_MAX 128
#define AFUNIX_FDQ_MAX     16    /* maximum SCM_RIGHTS fds queued per socket */

typedef struct {
    uint8_t  data[AFUNIX_BUF_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} afunix_buf_t;

/* Ring copy with at most two memcpy()s (one when the run wraps the end of the
 * buffer) instead of a byte-at-a-time loop with a modulo (a divide) per byte.
 * Every X request, DCOP/ICE message and AF_UNIX byte goes through here, so
 * the per-byte modulo was a real throughput sink on the desktop. */
static size_t afbuf_write(afunix_buf_t *b, const uint8_t *src, size_t n) {
    size_t space = AFUNIX_BUF_SIZE - b->count;
    if (n > space) n = space;
    if (n == 0) return 0;
    size_t first = AFUNIX_BUF_SIZE - b->head;     /* contiguous run to the end */
    if (first > n) first = n;
    memcpy(b->data + b->head, src, first);
    if (n > first) memcpy(b->data, src + first, n - first);
    b->head = (uint32_t)((b->head + n) % AFUNIX_BUF_SIZE);
    b->count += (uint32_t)n;
    return n;
}

static size_t afbuf_read(afunix_buf_t *b, uint8_t *dst, size_t n) {
    if (n > b->count) n = b->count;
    if (n == 0) return 0;
    size_t first = AFUNIX_BUF_SIZE - b->tail;     /* contiguous run to the end */
    if (first > n) first = n;
    memcpy(dst, b->data + b->tail, first);
    if (n > first) memcpy(dst + first, b->data, n - first);
    b->tail = (uint32_t)((b->tail + n) % AFUNIX_BUF_SIZE);
    b->count -= (uint32_t)n;
    return n;
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

    /* SO_PEERCRED: the credentials of the process that owns this socket,
     * captured at creation time from current_process.  getsockopt(2)
     * SO_PEERCRED returns the PEER's creds (peer->cr_*), which is how
     * libICE/DCOP's peerIsUs() decides a server is same-user and accepts
     * its calls — without it every TDE app rejects all inter-app DCOP. */
    uint32_t        cr_uid;
    uint32_t        cr_gid;
    uint32_t        cr_pid;

    /* For data exchange when CONNECTED */
    struct afunix_sock *peer;
    afunix_buf_t   rx;          /* writes from peer land here */

    /* For listeners */
    struct afunix_sock *accept_q[AFUNIX_BACKLOG_MAX];
    int             accept_head;
    int             accept_tail;
    int             accept_count;
    int             backlog;
    /* For a server-side socket while it is still queued in a listener's
     * accept_q (i.e. connect() has run but accept() has not): points at that
     * listener so that if the connecting client closes before being accepted,
     * close() can pull this entry out of the backlog and free the slot.  NULL
     * once accepted (or for a non-server-side socket). */
    struct afunix_sock *listener;

    /* Wait channels */
    void           *rx_chan;       /* readers wait here */
    void           *tx_chan;       /* writers wait when peer's rx is full */
    void           *accept_chan;   /* accept() waits here */
    void           *connect_chan;  /* connect() waits here for peer setup */

    /* Path namespace */
    char            path[AFUNIX_PATH_MAX];
    int             pathlen;
    /* For a pathname (non-abstract) binding: the inode of the filesystem
     * socket node created at bind() time.  AF_UNIX is keyed by the file's
     * inode on Linux, not by the literal path string — so a client that
     * connect()s via a different but equivalent path (e.g. through a
     * symlinked directory: ~/.trinity/socket-host -> /tmp/tdesocket-host)
     * still reaches the same listener.  0 for abstract/unbound sockets. */
    uint64_t        bound_inode;

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

/*
 * Find a bound pathname socket by the inode of its filesystem node.  This is
 * the AF_UNIX-is-keyed-by-inode fallback: a literal-path-string compare misses
 * when the client reaches the socket through an equivalent but differently
 * spelled path (a symlinked parent directory — e.g. TDE's
 * ~/.trinity/socket-host -> /tmp/tdesocket-host), so when the string compare
 * fails we resolve the path to its inode and match on that instead.  A
 * LISTENING binding is preferred over a merely-BOUND one when several share an
 * inode (a server restarted with unlink()+bind() can briefly leave two).
 */
static afunix_sock_t *afunix_find_bound_by_inode(uint64_t inode) {
    if (inode == 0) return NULL;
    g_bound_lock_init();
    mutex_lock(&g_bound_lock);
    afunix_sock_t *match = NULL;
    for (afunix_bound_node_t *n = g_bound_list; n; n = n->next) {
        if (n->sock->bound_inode == inode) {
            match = n->sock;
            if (n->sock->state == AFUS_LISTENING) break;
        }
    }
    mutex_unlock(&g_bound_lock);
    return match;
}

/*
 * Normalise a pathname AF_UNIX address length.  Callers disagree on what
 * addrlen to pass for sun_path: some pass the exact byte count
 * (offsetof(sun_path) + strlen), others the full sizeof(struct sockaddr_un)
 * with sun_path NUL-padded to 108 bytes.  X11 is the canonical victim — the
 * server's Xtrans binds "/tmp/.X11-unix/X0" with pathlen 17, while libxcb
 * connects with pathlen 108 (the whole padded buffer), so the exact-length
 * key compare in afunix_find_bound() never matches and every client silently
 * falls back to the TCP transport.  For a pathname socket the address IS the
 * NUL-terminated string in sun_path, so reduce to its string length; bind and
 * connect then produce the same key regardless of how addrlen was framed.
 * Abstract sockets (leading NUL) carry an opaque, possibly NUL-containing
 * name and keep their raw length.
 */
static socklen_t afunix_norm_pathlen(const char *path, socklen_t pathlen) {
    if (pathlen > 0 && path[0] != '\0') {
        socklen_t n = 0;
        while (n < pathlen && path[n] != '\0')
            n++;
        return n;
    }
    return pathlen;
}

/*
 * Park on `chan` until woken, dropping `m` while blocked and re-acquiring it
 * before return.  This is the AF_UNIX twin of pipe_wait() (sys/fs/pipe.c) and
 * exists for the same two preemption races the naive sleepq_add+sched_yield
 * the rx/tx loops used was exposed to:
 *
 *   1. sleepq_add flips us to THREAD_BLOCKED while we still hold `m`.  A timer
 *      tick in the window before mutex_unlock deschedules us *holding the
 *      lock* (a BLOCKED thread is never re-selected), and the peer that needs
 *      `m` to write+wake us deadlocks on it.
 *   2. A wakeup landing in the park window can be lost outright; with no
 *      deadline the socket then hangs forever.
 *
 * Both wedged X clients the moment they used the local AF_UNIX transport
 * (xset/xterm/the whole TDE session hung).  The fix mirrors pipe_wait:
 * interrupts off across the sleepq_add..mutex_unlock register so we cannot be
 * preempted while BLOCKED and holding `m`; a ~50 ms fallback deadline so
 * sched_tick re-readies us if the wake was lost; only yield while still
 * queued; and a self-dequeue on wake (sched_tick can't remove us from IRQ
 * context).  Returns 0, or -EINTR if a signal is pending.  Caller holds `m`
 * on entry and exit and re-checks its own condition afterwards.
 */
static int afunix_wait(void *chan, mutex_t *m) {
    if (!current_thread) {
        mutex_unlock(m);
        sched_yield();
        mutex_lock(m);
        return 0;
    }
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;
    uint32_t pf = intr_disable();
    sleepq_add(chan, current_thread);
    if (current_thread->sleep_expiry == 0) {
        uint32_t hz = get_hz();
        uint64_t span = hz ? (hz / 20u) : 8u;   /* ~50 ms */
        if (span == 0) span = 1;
        current_thread->sleep_expiry = get_ticks() + span;
    }
    mutex_unlock(m);
    intr_restore(pf);
    if (current_thread->wait_chan == chan)
        sched_yield();
    current_thread->sleep_expiry = 0;
    sleepq_remove_thread(current_thread);
    mutex_lock(m);
    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
    if (current_thread->sig_pending & ~current_thread->sig_mask)
        return -EINTR;
    return 0;
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
    if (s->type == SOCK_DGRAM) {
        /* Datagram socket: messages are framed in rx as [u16 len][payload]
         * (written atomically), so return exactly one datagram per read and
         * preserve message boundaries. */
        while (s->rx.count < 2) {
            if (!s->peer || s->peer->closed || s->peer->wr_closed) {
                mutex_unlock(&s->lock); return 0;
            }
            if (nonblock) { mutex_unlock(&s->lock); return (size_t)-EAGAIN; }
            if (current_thread &&
                (current_thread->sig_pending & ~current_thread->sig_mask)) {
                mutex_unlock(&s->lock); return (size_t)-EINTR;
            }
            if (afunix_wait(s->rx_chan, &s->lock) == -EINTR) {
                mutex_unlock(&s->lock); return (size_t)-EINTR;
            }
            if (s->rd_closed) { mutex_unlock(&s->lock); return 0; }
        }
        uint8_t hdr[2];
        afbuf_read(&s->rx, hdr, 2);
        size_t mlen = ((size_t)hdr[0] << 8) | hdr[1];
        size_t n = mlen < size ? mlen : size;
        if (n) afbuf_read(&s->rx, buf, n);
        for (size_t rem = mlen - n; rem; ) {     /* drop truncated tail */
            uint8_t junk[128];
            size_t d = rem < sizeof(junk) ? rem : sizeof(junk);
            afbuf_read(&s->rx, junk, d);
            rem -= d;
        }
        if (s->peer) sleepq_wake_all(s->peer->tx_chan);
        /* Also wake our own tx_chan: unconnected senders (sendto-by-path)
         * block on the destination socket's tx_chan when its rx is full. */
        sleepq_wake_all(s->tx_chan);
        mutex_unlock(&s->lock);
        return n;
    }
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
        if (afunix_wait(s->rx_chan, &s->lock) == -EINTR) {
            mutex_unlock(&s->lock);
            return (size_t)-EINTR;
        }
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
    if (!s) return 0;
    /* Peer gone (disconnected) or our socket closed: the connection is broken,
     * so a stream write must fail with EPIPE — NOT return 0.  A 0 return makes
     * the writer's `while (written < len) write(...)` loop spin forever (it
     * never advances), which is exactly what wedged the single-threaded X
     * server when a client disconnected during CDE teardown (fd had peer=NULL,
     * closed=0). */
    if (s->closed || !s->peer) return (size_t)-EPIPE;
    /* Own SHUT_WR: no more writes from us. */
    if (s->wr_closed) return (size_t)-EPIPE;
    /* O_NONBLOCK is carried on the file_t, which only the syscall layer sees;
     * it stashes it on the thread (current_thread->io_file) for the duration
     * of the write so we can reach it from this fs_node callback. */
    int nonblock = current_thread && current_thread->io_file &&
                   (current_thread->io_file->f_flag & FNONBLOCK);
    if (s->type == SOCK_DGRAM) {
        /* Datagram: frame the whole message as [u16 len][payload] and write
         * it atomically so the peer's reader sees one intact datagram. */
        if (size + 2 > AFUNIX_BUF_SIZE) return (size_t)-EMSGSIZE;
        afunix_sock_t *peer = s->peer;
        if (!peer || peer->closed || peer->rd_closed) return (size_t)-EPIPE;
        mutex_lock(&peer->lock);
        while (peer->rx.count + size + 2 > AFUNIX_BUF_SIZE
               && !peer->closed && !peer->rd_closed && !s->wr_closed) {
            if (nonblock) { mutex_unlock(&peer->lock); return (size_t)-EAGAIN; }
            if (current_thread &&
                (current_thread->sig_pending & ~current_thread->sig_mask)) {
                mutex_unlock(&peer->lock); return (size_t)-EINTR;
            }
            if (afunix_wait(s->tx_chan, &peer->lock) == -EINTR) {
                mutex_unlock(&peer->lock); return (size_t)-EINTR;
            }
        }
        if (peer->closed || peer->rd_closed || s->wr_closed) {
            mutex_unlock(&peer->lock); return (size_t)-EPIPE;
        }
        uint8_t hdr[2] = { (uint8_t)(size >> 8), (uint8_t)(size & 0xFF) };
        afbuf_write(&peer->rx, hdr, 2);
        if (size) afbuf_write(&peer->rx, buf, size);
        sleepq_wake_all(peer->rx_chan);
        mutex_unlock(&peer->lock);
        return size;
    }
    size_t written = 0;
    while (written < size) {
        afunix_sock_t *peer = s->peer;
        if (!peer || peer->closed || peer->rd_closed)
            return written ? written : (size_t)-EPIPE;
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
            if (afunix_wait(s->tx_chan, &peer->lock) == -EINTR) {
                mutex_unlock(&peer->lock);
                return written ? written : (size_t)-EINTR;
            }
        }
        if (peer->closed || peer->rd_closed || s->wr_closed) {
            mutex_unlock(&peer->lock);
            return written ? written : (size_t)-EPIPE;
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
        afunix_sock_t *peer = s->peer;
        s->peer = NULL;
        /* If the peer is a server-side socket still sitting un-accepted in a
         * listener's backlog (we are the connecting client and the server never
         * accept()ed us), pull it out of the listener's accept_q and free it.
         * Otherwise an abandoned connect permanently holds a backlog slot, and
         * once `backlog` of them accumulate the listener refuses EVERY further
         * connect with ECONNREFUSED — which made tdeinit's clients believe it
         * had died and respawn it in a loop, blocking the desktop.  A
         * never-accepted server_side is owned by no fd, so we free it here. */
        afunix_sock_t *lst = peer->listener;
        if (lst) {
            mutex_lock(&lst->lock);
            if (peer->listener == lst) {
                for (int i = 0; i < lst->accept_count; i++) {
                    int idx = (lst->accept_tail + i) % AFUNIX_BACKLOG_MAX;
                    if (lst->accept_q[idx] == peer) {
                        for (int j = i; j < lst->accept_count - 1; j++) {
                            int a = (lst->accept_tail + j) % AFUNIX_BACKLOG_MAX;
                            int b = (lst->accept_tail + j + 1) % AFUNIX_BACKLOG_MAX;
                            lst->accept_q[a] = lst->accept_q[b];
                        }
                        lst->accept_head =
                            (lst->accept_head - 1 + AFUNIX_BACKLOG_MAX) % AFUNIX_BACKLOG_MAX;
                        lst->accept_count--;
                        peer->listener = NULL;
                        mutex_unlock(&lst->lock);
                        kfree(peer, sizeof(*peer));
                        peer = NULL;
                        break;
                    }
                }
                if (peer) mutex_unlock(&lst->lock);
            } else {
                mutex_unlock(&lst->lock);
            }
        }
        if (peer) {
            /* Notify peer of our departure: wake their readers/writers
             * so they see 0-byte read (EOF). */
            peer->peer = NULL;
            sleepq_wake_all(peer->rx_chan);
            sleepq_wake_all(peer->tx_chan);
        }
    }
    if (s->pathlen > 0) g_bound_unlink(s);
    /* Drop any SCM_RIGHTS file references still queued on our rx_fdq:
     * fds passed to us that were never recvmsg()'d.  Each carries a
     * file_t reference the sender bumped; if we just freed the socket
     * (or leaked it) those references would never be released, leaking
     * the underlying open files forever.  Release them under the lock. */
    for (int i = 0; i < s->rx_fdq_count; i++) {
        file_t *qf = s->rx_fdq[i];
        s->rx_fdq[i] = NULL;
        if (qf) file_close_ptr(qf);
    }
    s->rx_fdq_count = 0;
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
    /* Record the owning process's credentials for SO_PEERCRED.  Use the
     * effective ids (what a same-user check cares about); falls back to
     * root/0 for kernel-context allocations where current_process is NULL. */
    if (current_process) {
        s->cr_uid = current_process->euid;
        s->cr_gid = current_process->egid;
        s->cr_pid = (uint32_t)current_process->pid;
    }
    mutex_init(&s->lock, "af_unix_sock");

    /* Wait channels: just unique addresses per-socket; we use the
     * address of fields of the struct itself. */
    s->rx_chan      = &s->rx;
    s->tx_chan      = &s->rx.count;
    s->accept_chan  = &s->accept_q;
    s->connect_chan = &s->state;

    /* fs_node adapter.  FS_SOCKET so fstat(2) on the socket fd reports
     * S_IFSOCK; read/write/poll/close dispatch through the node's own
     * function pointers, not the type, so this only affects stat. */
    s->node.flags = FS_SOCKET;
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
    } else if (base != SOCK_STREAM && base != SOCK_DGRAM) {
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
    if (base != SOCK_STREAM && base != SOCK_DGRAM) return -EPROTONOSUPPORT;
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
    if (sock_fd_invalid(fd)) return -EBADF;
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
    socklen_t pathlen = addrlen - 2;
    const char *path = ((const struct sockaddr_un *)addr)->sun_path;
    if (pathlen > AFUNIX_PATH_MAX) pathlen = AFUNIX_PATH_MAX;
    pathlen = afunix_norm_pathlen(path, pathlen);

    if (pathlen > 0 && path[0] != '\0') {
        /* Pathname socket: the filesystem node IS the binding's identity,
         * exactly like Linux's per-inode AF_UNIX namespace.  Create a real
         * S_IFSOCK node so the path is stat / chmod / access / unlink-able
         * (POSIX; TDE's tdeinit binds its socket, chmod()s it 0600 and
         * re-stat()s it — without a node those returned ENOENT, "Can't set
         * permissions on socket: error 2").
         *
         * Crucially, EADDRINUSE is decided by the node's existence, NOT the
         * in-core name table.  Servers restart with the standard
         * unlink(path); bind(path) idiom: after the unlink the node is gone,
         * so the rebind must succeed even though a path-keyed table entry
         * for the still-open old socket lingers (it is harmless — g_bound_link
         * prepends, so afunix_find_bound returns the fresh binding, and the
         * stale entry is reaped when the old socket finally closes).  Keying
         * EADDRINUSE off the table instead broke tdeinit's restart with a
         * spurious "bind() failed: error 98". */
        char kpath[AFUNIX_PATH_MAX + 1];
        memcpy(kpath, path, pathlen);
        kpath[pathlen] = '\0';
        uint16_t nmode = (uint16_t)(S_IFSOCK |
            (0777 & ~(current_process ? current_process->umask : 0)));
        int mret = vfs_mknod(kpath, nmode, 0);
        if (mret == -EEXIST) return -EADDRINUSE;  /* node present -> name taken */
        if (mret != 0) return mret;               /* ENOENT/EACCES/ENOTDIR... */
        /* Record the new node's inode so connect() can match this binding by
         * the resolved file identity, not just the literal path string. */
        fs_node_t *bnode = vfs_lookup(fs_root, kpath);
        s->bound_inode = bnode ? bnode->inode : 0;
    } else {
        /* Abstract / autobind socket: no filesystem presence, so the in-core
         * name table is the only arbiter of the namespace. */
        if (afunix_find_bound(path, pathlen)) return -EADDRINUSE;
        s->bound_inode = 0;
    }

    mutex_lock(&s->lock);
    memcpy(s->path, path, pathlen);
    s->pathlen = pathlen;
    s->state = AFUS_BOUND;
    mutex_unlock(&s->lock);
    g_bound_link(s);
    return 0;
}

int sys_listen(int fd, int backlog) {
    if (sock_fd_invalid(fd)) return -EBADF;
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
    if (sock_fd_invalid(fd)) return -EBADF;
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

    /* O_NONBLOCK listener: return EAGAIN instead of blocking when no
     * connection is queued.  The canonical poll-driven server drains with
     * "while (accept() >= 0) ;" and relies on EAGAIN to stop; without it the
     * server blocks on the last accept and never services its existing
     * clients — every connected client then starves (this wedged
     * multi-client servers entirely, the desktop included). */
    int accept_nonblock = 0;
    if (fd >= 0 && fd < MAX_FD && current_process) {
        file_t *lf = current_process->fds[fd];
        if (lf && (lf->f_flag & FNONBLOCK)) accept_nonblock = 1;
    }

    mutex_lock(&server->lock);
    while (server->accept_count == 0) {
        if (server->closed) { mutex_unlock(&server->lock); return -EBADF; }
        if (accept_nonblock) { mutex_unlock(&server->lock); return -EAGAIN; }
        /* Interruptible wait: a pending signal must break accept()
         * out, otherwise a wedged server cannot be killed and holds
         * its controlling terminal forever. */
        if (current_thread->sig_pending & ~current_thread->sig_mask) {
            mutex_unlock(&server->lock);
            return -EINTR;
        }
        if (afunix_wait(server->accept_chan, &server->lock) == -EINTR) {
            mutex_unlock(&server->lock);
            return -EINTR;
        }
    }
    /* connect() already allocated and peered the server-side socket;
     * we just need to install it into the caller's fd table. */
    afunix_sock_t *server_side = server->accept_q[server->accept_tail];
    server->accept_tail = (server->accept_tail + 1) % AFUNIX_BACKLOG_MAX;
    server->accept_count--;
    server_side->listener = NULL;   /* dequeued: no longer reclaimable by close() */
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
    if (sock_fd_invalid(fd)) return -EBADF;
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

    socklen_t pathlen = addrlen - 2;
    const char *path = ((const struct sockaddr_un *)addr)->sun_path;
    if (pathlen > AFUNIX_PATH_MAX) pathlen = AFUNIX_PATH_MAX;
    pathlen = afunix_norm_pathlen(path, pathlen);

    afunix_sock_t *server = afunix_find_bound(path, pathlen);
    if (!server && pathlen > 0 && path[0] != '\0') {
        /* Literal-path match missed.  AF_UNIX is keyed by the file's inode,
         * not by the path text, so a client reaching the socket through an
         * equivalent path (a symlinked parent dir — e.g. TDE's
         * ~/.trinity/socket-host -> /tmp/tdesocket-host) must still find the
         * listener.  Resolve the path to its node and retry by inode. */
        char kpath[AFUNIX_PATH_MAX + 1];
        memcpy(kpath, path, pathlen);
        kpath[pathlen] = '\0';
        fs_node_t *cnode = vfs_lookup(fs_root, kpath);
        if (cnode == NULL) return -ENOENT;           /* path doesn't exist */
        server = afunix_find_bound_by_inode(cnode->inode);
        if (!server) return -ECONNREFUSED;           /* node exists, no listener */
    } else if (!server) {
        return -ECONNREFUSED;                          /* abstract name, no binding */
    }
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
    /* The accepted (server-side) socket belongs to the listener's owner,
     * not to the connecting client that allocated it here — inherit the
     * listener's creds so the client's SO_PEERCRED reports the server. */
    server_side->cr_uid = server->cr_uid;
    server_side->cr_gid = server->cr_gid;
    server_side->cr_pid = server->cr_pid;
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
    server_side->listener = server;   /* queued: close() can reclaim the slot */
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
    if (sock_fd_invalid(fd)) return -EBADF;
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
    afunix_sock_t *s = afunix_from_fd(fd);
    if (!s) return -ENOTSOCK;
    /*
     * afunix_node_write() reads the fd's O_NONBLOCK off
     * current_thread->io_file.  write(2) routes through kern_write() which
     * stashes it, but send(2) reaches the node write directly and did not —
     * so a non-blocking AF_UNIX send() was treated as blocking and wedged
     * when the peer's rx filled.  libxcb writes every X request with send(),
     * so under multi-client load (the server slow to drain) the client's
     * send() blocked and deadlocked the display.  Stash it (and honour
     * MSG_DONTWAIT), mirroring recv_into_kbuf().
     */
    file_t *sf = current_process ? current_process->fds[fd] : NULL;
    file_t *saved = current_thread ? current_thread->io_file : NULL;
    short saved_flag = sf ? sf->f_flag : 0;
    if (sf && (flags & MSG_DONTWAIT)) sf->f_flag |= FNONBLOCK;
    if (current_thread) current_thread->io_file = sf;
    ssize_t r = (ssize_t)afunix_node_write(&s->node, 0, len, (const uint8_t *)buf);
    if (current_thread) current_thread->io_file = saved;
    if (sf && (flags & MSG_DONTWAIT)) sf->f_flag = saved_flag;
    return r;
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
    /*
     * afunix_node_read() reads the fd's O_NONBLOCK state off
     * current_thread->io_file, which the read(2) syscall stashes but recv(2)
     * did not — so a non-blocking AF_UNIX socket reached via recv() was
     * treated as blocking and its empty-buffer read wedged forever.  Every
     * libxcb X connection fcntl()s its fd O_NONBLOCK and then recv()s on it,
     * so this hung the entire local X transport (xset/xterm/the TDE session
     * froze).  Stash the file_t here too; honour MSG_DONTWAIT as well.  (The
     * af_inet path is immune — it reads f->f_flag directly.)
     */
    file_t *saved = current_thread ? current_thread->io_file : NULL;
    short saved_flag = f ? f->f_flag : 0;
    if (f && (flags & MSG_DONTWAIT)) f->f_flag |= FNONBLOCK;
    if (current_thread) current_thread->io_file = f;
    ssize_t r = (ssize_t)afunix_node_read(&s->node, 0, len, (uint8_t *)kbuf);
    if (current_thread) current_thread->io_file = saved;
    if (f && (flags & MSG_DONTWAIT)) f->f_flag = saved_flag;
    return r;
}

/* Common recv/recvfrom body: receive into a kernel bounce buffer, then copy
 * the data (and any source address) out to userspace fault-safely. */
static ssize_t do_recv(int fd, void *buf, size_t len, int flags,
                       struct sockaddr *addr, socklen_t *addrlen) {
    if (sock_fd_invalid(fd)) return -EBADF;
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
    if (sock_fd_invalid(fd)) return -EBADF;
    return do_recv(fd, buf, len, flags, NULL, NULL);
}

/* Deliver one datagram to a bound destination AF_UNIX SOCK_DGRAM socket,
 * [u16 len][payload]-framed exactly like the connected path, blocking
 * interruptibly while the destination rx buffer is full (the destination's
 * reader wakes its own tx_chan for these connectionless senders).  Returns
 * the byte count sent, or a negative errno. */
static ssize_t afunix_dgram_deliver(afunix_sock_t *dst, const uint8_t *buf,
                                    size_t size, int nonblock) {
    if (size + 2 > AFUNIX_BUF_SIZE) return -EMSGSIZE;
    mutex_lock(&dst->lock);
    while (dst->rx.count + size + 2 > AFUNIX_BUF_SIZE &&
           !dst->closed && !dst->rd_closed) {
        if (nonblock) { mutex_unlock(&dst->lock); return -EAGAIN; }
        if (current_thread &&
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            mutex_unlock(&dst->lock); return -EINTR;
        }
        if (afunix_wait(dst->tx_chan, &dst->lock) == -EINTR) {
            mutex_unlock(&dst->lock); return -EINTR;
        }
    }
    if (dst->closed || dst->rd_closed) {
        mutex_unlock(&dst->lock); return -ECONNREFUSED;
    }
    uint8_t hdr[2] = { (uint8_t)(size >> 8), (uint8_t)(size & 0xFF) };
    afbuf_write(&dst->rx, hdr, 2);
    if (size) afbuf_write(&dst->rx, buf, size);
    sleepq_wake_all(dst->rx_chan);
    mutex_unlock(&dst->lock);
    return (ssize_t)size;
}

ssize_t sys_sendto(int fd, const void *buf, size_t len, int flags,
                   const struct sockaddr *addr, socklen_t addrlen) {
    if (sock_fd_invalid(fd)) return -EBADF;
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
    /* AF_UNIX named SOCK_DGRAM: sendto() delivers to the addressed,
     * unconnected destination (resolved by path), not a connected peer. */
    if (addr && addrlen > 2 && addr->sa_family == AF_UNIX) {
        afunix_sock_t *s = afunix_from_fd(fd);
        if (s && s->type == SOCK_DGRAM) {
            socklen_t pathlen = addrlen - 2;
            const char *path = ((const struct sockaddr_un *)addr)->sun_path;
            if (pathlen > AFUNIX_PATH_MAX) pathlen = AFUNIX_PATH_MAX;
            pathlen = afunix_norm_pathlen(path, pathlen);
            afunix_sock_t *dst = afunix_find_bound(path, pathlen);
            if (!dst && pathlen > 0 && path[0] != '\0') {
                /* inode-keyed fallback: reach the socket via an equivalent
                 * (e.g. symlinked) path — see afunix_find_bound_by_inode. */
                char kpath[AFUNIX_PATH_MAX + 1];
                memcpy(kpath, path, pathlen);
                kpath[pathlen] = '\0';
                fs_node_t *dnode = vfs_lookup(fs_root, kpath);
                if (dnode == NULL) return -ENOENT;
                dst = afunix_find_bound_by_inode(dnode->inode);
                if (!dst) return -ECONNREFUSED;
            } else if (!dst) {
                return -ECONNREFUSED;
            }
            if (dst->type != SOCK_DGRAM) return -ECONNREFUSED;
            int nonblock = 0;
            if (fd >= 0 && fd < MAX_FD && current_process) {
                file_t *ff = current_process->fds[fd];
                if (ff && (ff->f_flag & FNONBLOCK)) nonblock = 1;
            }
            return afunix_dgram_deliver(dst, (const uint8_t *)buf, len, nonblock);
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

/* Upper bound on a copied-in control buffer: enough for the largest
 * SCM_RIGHTS record we accept (AFUNIX_FDQ_MAX fds + header), generously
 * rounded up.  A controllen larger than this is rejected rather than
 * touched, which is fine — substrate has no other cmsg types. */
#define AFUNIX_CMSG_MAX  256
/* Cap on iov entries pulled into the kernel per sendmsg/recvmsg. */
#define AFUNIX_IOV_MAX   64

/* sendmsg with SCM_RIGHTS support — parse the cmsghdr area for any
 * SCM_RIGHTS records, bump file_t refcount for each fd, queue the
 * file_t on the peer socket's rx_fdq.  Then run the iov loop as a
 * plain send() chain.  The msghdr, its iovec array, and the control
 * buffer are all pulled into the kernel with copyin() before use —
 * never dereferenced straight off the user pointer. */
ssize_t sys_sendmsg(int fd, const struct msghdr *umsg, int flags) {
    if (!umsg) return -EFAULT;
    struct msghdr kmsg;
    if (copyin(umsg, &kmsg, sizeof(kmsg)) != 0) return -EFAULT;
    struct msghdr *msg = &kmsg;

    if (msg->msg_iovlen < 0 || msg->msg_iovlen > AFUNIX_IOV_MAX)
        return -EMSGSIZE;
    struct iovec_local kiov[AFUNIX_IOV_MAX];
    if (msg->msg_iovlen > 0) {
        if (!msg->msg_iov) return -EFAULT;
        if (copyin(msg->msg_iov, kiov,
                   (size_t)msg->msg_iovlen * sizeof(kiov[0])) != 0)
            return -EFAULT;
    }

    afunix_sock_t *s = afunix_from_fd(fd);

    /* SCM_RIGHTS plumbing only applies to AF_UNIX peers.  AF_INET
     * sockets fall through to the iov-only path below. */
    if (s && s->peer && msg->msg_control && msg->msg_controllen > 0) {
        size_t cmsglen = (size_t)msg->msg_controllen;
        if (cmsglen > AFUNIX_CMSG_MAX) return -EINVAL;
        unsigned char cmsgbuf[AFUNIX_CMSG_MAX];
        if (copyin(msg->msg_control, cmsgbuf, cmsglen) != 0)
            return -EFAULT;
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
    /* kiov was copied in above; iov_base entries are still user pointers,
     * but sys_send/sys_sendto copyin them on their own. */
    struct iovec_local *iov = kiov;
    for (int i = 0; i < (int)msg->msg_iovlen; i++) {
        ssize_t r;
        if (msg->msg_name && msg->msg_namelen > 0) {
            /* Honour msg_name as the datagram destination — the mirror of
             * the recvmsg msg_name fix.  rpcbind's libtirpc svc_dg_reply
             * sendmsg()s its reply with msg_name set to the client it just
             * recvmsg()'d from (on an *unconnected* UDP socket), so without
             * this the reply is sent with no destination and silently
             * dropped, leaving every RPC caller hanging.  sys_sendto routes
             * AF_INET/INET6 to afinet_sendto and otherwise falls back to
             * sys_send, so AF_UNIX traffic is unaffected. */
            r = sys_sendto(fd, iov[i].iov_base, iov[i].iov_len, flags,
                           (const struct sockaddr *)msg->msg_name,
                           (socklen_t)msg->msg_namelen);
        } else {
            r = sys_send(fd, iov[i].iov_base, iov[i].iov_len, flags);
        }
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
ssize_t sys_recvmsg(int fd, struct msghdr *umsg, int flags) {
    if (!umsg) return -EFAULT;
    /* Pull the msghdr into the kernel before touching any of its fields.
     * msg_name / iov_base / msg_control remain user pointers — the
     * helpers they are handed (sys_recvfrom, copyout) validate them. */
    struct msghdr kmsg;
    if (copyin(umsg, &kmsg, sizeof(kmsg)) != 0) return -EFAULT;
    struct msghdr *msg = &kmsg;

    if (msg->msg_iovlen < 0 || msg->msg_iovlen > AFUNIX_IOV_MAX)
        return -EMSGSIZE;
    struct iovec_local kiov[AFUNIX_IOV_MAX];
    if (msg->msg_iovlen > 0) {
        if (!msg->msg_iov) return -EFAULT;
        if (copyin(msg->msg_iov, kiov,
                   (size_t)msg->msg_iovlen * sizeof(kiov[0])) != 0)
            return -EFAULT;
    }
    msg->msg_flags = 0;

    ssize_t total = 0;
    struct iovec_local *iov = kiov;
    for (int i = 0; i < (int)msg->msg_iovlen; i++) {
        ssize_t r;
        if (i == 0 && msg->msg_name && msg->msg_namelen > 0) {
            /* Capture the datagram sender's address into msg_name on the
             * first iov read.  Datagram RPC servers — notably rpcbind's
             * libtirpc svc_dg, which recvmsg()s each request and sends
             * the reply back to msg_name — depend on this.  Without it
             * the source address stays empty and every reply is
             * undeliverable, so e.g. CDE's ttsession/dtsession hang
             * forever pinging an rpcbind that can never answer.  msg_name
             * is the caller's userspace buffer and msg_namelen its
             * capacity; pass the *user* msghdr's msg_namelen slot so
             * do_recv copyin/copyout's it in place (msg_namelen and
             * socklen_t are both 4 bytes).  The SCM_RIGHTS path below
             * passes msg_name == NULL, so it is unaffected. */
            r = sys_recvfrom(fd, iov[i].iov_base, iov[i].iov_len, flags,
                             (struct sockaddr *)msg->msg_name,
                             (socklen_t *)&umsg->msg_namelen);
        } else {
            r = sys_recv(fd, iov[i].iov_base, iov[i].iov_len, flags);
        }
        if (r < 0) return total > 0 ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }

    /* AF_UNIX rx_fdq drain.  Only applies once the iov loop above has
     * pulled at least one byte (recvmsg without data wouldn't carry
     * an SCM payload on a stream socket).  The control area is built in
     * a kernel buffer and copyout()'d — never written through the user
     * pointer directly. */
    afunix_sock_t *s = afunix_from_fd(fd);
    uint32_t out_controllen = 0;
    if (s && msg->msg_control && (size_t)msg->msg_controllen >= sizeof(struct kcmsghdr)) {
        size_t cmsgcap = (size_t)msg->msg_controllen;
        if (cmsgcap > AFUNIX_CMSG_MAX) cmsgcap = AFUNIX_CMSG_MAX;
        unsigned char cmsgbuf[AFUNIX_CMSG_MAX];
        mutex_lock(&s->lock);
        int nfds = s->rx_fdq_count;
        if (nfds > 0) {
            size_t need = KCMSG_ALIGN(sizeof(struct kcmsghdr) + (size_t)nfds * sizeof(int));
            if (need > cmsgcap) {
                /* Truncate — fewer fds than queued — but still deliver
                 * what fits and mark MSG_CTRUNC. */
                nfds = (int)((cmsgcap - sizeof(struct kcmsghdr)) / sizeof(int));
                if (nfds < 0) nfds = 0;
                msg->msg_flags |= MSG_CTRUNC;
            }
            /* Allocate fds in the receiver process. */
            int allocated[AFUNIX_FDQ_MAX];
            int got = 0;
            for (int i = 0; i < nfds; i++) {
                int newfd = proc_alloc_fd(current_process);
                if (newfd < 0) {
                    /* Out of fds — roll back everything installed so far
                     * and deliver nothing this call; the queued fds stay
                     * in rx_fdq for a later recvmsg.  proc_set_fd took
                     * ownership of the sender's reference for each slot,
                     * so clearing the slot must also drop that reference
                     * (proc_set_fd(...,NULL) only clears the table entry)
                     * — otherwise every rolled-back fd leaks its file_t. */
                    for (int j = 0; j < got; j++) {
                        file_t *rb = current_process->fds[allocated[j]];
                        proc_set_fd(current_process, allocated[j], NULL);
                        if (rb) file_close_ptr(rb);
                    }
                    got = 0;
                    msg->msg_flags |= MSG_CTRUNC;
                    break;
                }
                proc_set_fd(current_process, newfd, s->rx_fdq[i]);
                /* refcount was already bumped on the sender side;
                 * proc_set_fd takes ownership of that reference, so
                 * we do NOT bump again here. */
                allocated[got++] = newfd;
            }
            /* Build the cmsg in the kernel bounce buffer. */
            struct kcmsghdr *c = (struct kcmsghdr *)cmsgbuf;
            c->cmsg_len   = sizeof(*c) + (uint32_t)got * sizeof(int);
            c->cmsg_level = SOL_SOCKET;
            c->cmsg_type  = SCM_RIGHTS;
            int *outfds = (int *)(cmsgbuf + sizeof(*c));
            for (int i = 0; i < got; i++) outfds[i] = allocated[i];
            out_controllen = (uint32_t)(sizeof(*c) + (uint32_t)got * sizeof(int));
            /* Shift remaining unqueued fds down. */
            for (int i = got; i < s->rx_fdq_count; i++) {
                s->rx_fdq[i - got] = s->rx_fdq[i];
            }
            s->rx_fdq_count -= got;
            mutex_unlock(&s->lock);
            /* Copy the control area out to user space.  On copyout
             * failure the fds are already installed in the receiver's
             * table — userspace simply won't learn their numbers via the
             * cmsg; report EFAULT (a misbehaving caller's problem). */
            if (out_controllen &&
                copyout(cmsgbuf, msg->msg_control, out_controllen) != 0)
                return -EFAULT;
        } else {
            mutex_unlock(&s->lock);
            out_controllen = 0;
        }
    }
    /* Publish the updated ancillary length and flags back to the user
     * msghdr (msg_name / msg_namelen were already updated in place by
     * do_recv above). */
    if (copyout(&out_controllen, &umsg->msg_controllen,
                sizeof(umsg->msg_controllen)) != 0)
        return -EFAULT;
    if (copyout(&msg->msg_flags, &umsg->msg_flags,
                sizeof(umsg->msg_flags)) != 0)
        return -EFAULT;
    return total;
}

int sys_shutdown(int fd, int how) {
    if (sock_fd_invalid(fd)) return -EBADF;
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
    if (sock_fd_invalid(fd)) return -EBADF;
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
    if (sock_fd_invalid(fd)) return -EBADF;
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
    /* SO_REUSEADDR is the one option with observable behaviour — it relaxes
     * bind()'s EADDRINUSE check — so record it on the AF_INET socket.  All
     * other options are accepted silently. */
    if (level == 1 /*SOL_SOCKET*/ && optname == 2 /*SO_REUSEADDR*/) {
        extern int afinet_set_reuseaddr(int fd, int on);
        int on = (optval && optlen >= (socklen_t)sizeof(int)) ? *(const int *)optval : 0;
        afinet_set_reuseaddr(fd, on);   /* no-op on non-AF_INET fds */
    }
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
        #define SO_PEERCRED_K 17
        if (optname == SO_PEERCRED_K) {
            /* Report the connected peer's credentials as struct ucred
             * { pid, uid, gid }.  libICE/DCOP's peerIsUs() uses this to
             * confirm the dcopserver runs as the same user before it will
             * accept incoming calls; without it accept_calls stays false
             * and every inter-app DCOP call/send is rejected. */
            afunix_sock_t *u = afunix_from_fd(fd);
            if (!u)
                return -ENOPROTOOPT;   /* not an AF_UNIX socket */
            struct { uint32_t pid, uid, gid; } cr;
            afunix_sock_t *p = u->peer;
            if (p) {
                cr.pid = p->cr_pid; cr.uid = p->cr_uid; cr.gid = p->cr_gid;
            } else {
                /* Unconnected (or socketpair self-query): our own creds. */
                cr.pid = u->cr_pid; cr.uid = u->cr_uid; cr.gid = u->cr_gid;
            }
            socklen_t n = *optlen;
            if (n > (socklen_t)sizeof(cr)) n = sizeof(cr);
            memcpy(optval, &cr, n);
            *optlen = n;
            return 0;
        }
        if (optname == 2 /*SO_REUSEADDR*/) {
            extern int afinet_get_reuseaddr(int fd);
            int r = afinet_get_reuseaddr(fd);
            *(int *)optval = (r < 0) ? 0 : r;
            *optlen = sizeof(int);
            return 0;
        }
        if (optname == 7 /*SO_SNDBUF*/ || optname == 8 /*SO_RCVBUF*/) {
            *(int *)optval = 32 * 1024;    /* report a plausible buffer size */
            *optlen = sizeof(int);
            return 0;
        }
        if (optname == 6 /*SO_BROADCAST*/ || optname == 9 /*SO_KEEPALIVE*/ ||
            optname == 15 /*SO_REUSEPORT*/ || optname == SO_ACCEPTCONN_K) {
            *(int *)optval = 0;
            *optlen = sizeof(int);
            return 0;
        }
        /* Unknown SOL_SOCKET option — POSIX ENOPROTOOPT (was silently 0,
         * which let bogus getsockopt() calls "succeed"). */
        return -ENOPROTOOPT;
    }
    /* Non-SOL_SOCKET levels (IPPROTO_TCP/IP/...): stay lenient. */
    *(int *)optval = 0;
    *optlen = sizeof(int);
    return 0;
}
