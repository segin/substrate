#include <vfs/vfs.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/poll.h>
#include <kern/sched.h>
#include <vm/vm_kmem.h>
#include <string.h>
#include <stddef.h>
#include <sys/lock.h>
#include <kern/sleepq.h>
#include <errno.h>

#define PIPE_SIZE 4096

typedef struct {
    uint8_t *buffer;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    void    *wait_read;
    void    *wait_write;
    mutex_t lock;
    uint32_t readers_open;
    uint32_t writers_open;
} pipe_t;

typedef struct {
    pipe_t *pipe;
    uint8_t is_writer;
    uint8_t nonblock;   /* O_NONBLOCK on this endpoint — read/write
                           return -EAGAIN instead of blocking when
                           the buffer would force a wait. */
    struct fifo_reg *fifo;  /* NULL for anonymous pipes; set for
                               FIFO endpoints so close can refcount
                               the shared (dev,ino) registry entry. */
} pipe_endpoint_t;

/* ------------------------------------------------------------------ */
/* Named-FIFO registry.  open(2) on an S_IFIFO inode looks up (or
 * lazily creates) an entry here keyed by (dev, inum) and attaches
 * its endpoint to the entry's shared pipe_t.  Multiple opens of the
 * same inode get the same buffer.  The registry entry is kept alive
 * for as long as at least one endpoint refers to it; the on-disk
 * inode itself persists independently of the in-memory buffer.
 * ------------------------------------------------------------------ */

typedef struct fifo_reg {
    uint32_t dev;
    uint64_t inum;
    pipe_t  *pipe;
    int      refcount;       /* number of live endpoints */
    struct fifo_reg *next;
} fifo_reg_t;

static fifo_reg_t *g_fifo_registry;
static mutex_t     g_fifo_reg_lock;
static int         g_fifo_reg_lock_inited;

static void fifo_reg_lock_init(void) {
    if (!g_fifo_reg_lock_inited) {
        mutex_init(&g_fifo_reg_lock, "fifo_registry");
        g_fifo_reg_lock_inited = 1;
    }
}

/* Returns 0 on normal wake (data arrived, peer closed, etc.) and
 * -EINTR if psignal yanked us out via signal_interrupt_thread.
 * Order is load-bearing: set THREAD_F_INTERRUPTIBLE before letting
 * the thread land in THREAD_BLOCKED so psignal scans see the flag
 * and actually call signal_interrupt_thread on us. */
static int pipe_wait(void *chan, mutex_t *m) {
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;
    sleepq_add(chan, current_thread);
    mutex_unlock(m);
    if (current_thread->wait_chan == chan) {
        sched_yield();
    }
    mutex_lock(m);
    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
    if (current_thread->sig_pending & ~current_thread->sig_mask)
        return -EINTR;
    return 0;
}

static size_t pipe_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    pipe_endpoint_t *ep = (pipe_endpoint_t *)(uintptr_t)node->impl;
    pipe_t *p = ep ? ep->pipe : NULL;
    (void)offset;
    if (!p) {
        return 0;
    }

    mutex_lock(&p->lock);

    while (p->count == 0) {
        if (p->writers_open == 0) {
            mutex_unlock(&p->lock);
            return 0;
        }
        if (ep->nonblock) {
            mutex_unlock(&p->lock);
            /* Caller treats the negative return as a syscall errno. */
            return (size_t)-EAGAIN;
        }
        if (pipe_wait(p->wait_read, &p->lock) == -EINTR) {
            mutex_unlock(&p->lock);
            return (size_t)-EINTR;
        }
    }

    size_t i = 0;
    while (i < size && p->count > 0) {
        buffer[i++] = p->buffer[p->tail];
        p->tail = (p->tail + 1) % PIPE_SIZE;
        p->count--;
    }

    sleepq_wake_all(p->wait_write);
    mutex_unlock(&p->lock);
    return i;
}

static size_t pipe_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    pipe_endpoint_t *ep = (pipe_endpoint_t *)(uintptr_t)node->impl;
    pipe_t *p = ep ? ep->pipe : NULL;
    (void)offset;
    size_t written = 0;
    if (!p) {
        return 0;
    }

    mutex_lock(&p->lock);

    while (written < size) {
        if (p->readers_open == 0) {
            mutex_unlock(&p->lock);
            return written;
        }

        while (p->count == PIPE_SIZE) {
            if (p->readers_open == 0) {
                mutex_unlock(&p->lock);
                return written;
            }
            if (ep->nonblock) {
                mutex_unlock(&p->lock);
                /* If we've already accepted some bytes, return the
                 * short-count.  Otherwise surface EAGAIN. */
                if (written > 0) return written;
                return (size_t)-EAGAIN;
            }
            if (pipe_wait(p->wait_write, &p->lock) == -EINTR) {
                mutex_unlock(&p->lock);
                if (written > 0) return written;
                return (size_t)-EINTR;
            }
        }

        while (written < size && p->count < PIPE_SIZE) {
            p->buffer[p->head] = buffer[written++];
            p->head = (p->head + 1) % PIPE_SIZE;
            p->count++;
        }

        sleepq_wake_all(p->wait_read);
    }

    mutex_unlock(&p->lock);
    return written;
}

/* poll(2) callback.  Reports readability/writability of this
 * endpoint and (if no events are ready) registers the caller on
 * the pipe's "read-side" or "write-side" wait channel so a future
 * pipe_read/pipe_write/pipe_close can wake it.
 *
 * sys_poll only tracks one wait_chan per call, so a process
 * polling both ends of a pair (the classic bsdtar-stdin/gzip-
 * stdout pattern) can only sleep on whichever channel poll_fs
 * last wrote.  pipe_read/pipe_write/pipe_close already wake BOTH
 * wait_read and wait_write when state changes, so picking either
 * is correct.  We bias toward wait_write when the caller is the
 * writer (no POLLOUT means buffer is full → waiter wants to know
 * when the reader drains) and wait_read otherwise.  */
static int pipe_poll(fs_node_t *node, void *waiter) {
    pipe_endpoint_t *ep = (pipe_endpoint_t *)(uintptr_t)node->impl;
    pipe_t *p = ep ? ep->pipe : NULL;
    if (!p) return POLLERR;

    mutex_lock(&p->lock);
    int events = 0;
    int eof = (p->writers_open == 0 && p->count == 0);
    int no_readers = (p->readers_open == 0);

    if (ep->is_writer) {
        /* Writer side: POLLOUT when there's room (or all readers
         * gone — write would EPIPE, but poll surfaces it as POLLERR).  */
        if (no_readers) events |= POLLERR;
        else if (p->count < PIPE_SIZE) events |= POLLOUT | POLLWRNORM;
    } else {
        /* Reader side: POLLIN when data ready, POLLHUP when EOF.  */
        if (p->count > 0) events |= POLLIN | POLLRDNORM;
        if (eof) events |= POLLHUP;
    }

    if (events == 0 && waiter) {
        *(void **)waiter = ep->is_writer ? p->wait_write : p->wait_read;
    }
    mutex_unlock(&p->lock);
    return events;
}

static void pipe_close(fs_node_t *node) {
    pipe_endpoint_t *ep = (pipe_endpoint_t *)(uintptr_t)node->impl;
    if (!ep || !ep->pipe) {
        return;
    }

    pipe_t *p = ep->pipe;
    fifo_reg_t *fifo = ep->fifo;

    mutex_lock(&p->lock);
    if (ep->is_writer) {
        if (p->writers_open > 0) {
            p->writers_open--;
        }
        sleepq_wake_all(p->wait_read);
    } else {
        if (p->readers_open > 0) {
            p->readers_open--;
        }
        sleepq_wake_all(p->wait_write);
    }
    int can_free_pipe = (p->readers_open == 0 && p->writers_open == 0);
    /* Anonymous pipes free as soon as both ends are gone.  FIFOs
     * keep the pipe alive while the registry entry holds a refcount
     * (in case a new open arrives before the close completes). */
    int free_pipe_now = can_free_pipe && (fifo == NULL);
    mutex_unlock(&p->lock);

    node->impl = 0;
    kfree(ep, sizeof(*ep));
    kfree(node, sizeof(*node));

    if (free_pipe_now) {
        kfree(p->buffer, PIPE_SIZE);
        kfree(p, sizeof(*p));
    }

    /* FIFO bookkeeping: drop the registry refcount and tear down
     * the buffer only when the last opener is gone. */
    if (fifo) {
        mutex_lock(&g_fifo_reg_lock);
        fifo->refcount--;
        int drop = (fifo->refcount == 0);
        if (drop) {
            /* Unlink from registry */
            fifo_reg_t **pp = &g_fifo_registry;
            while (*pp && *pp != fifo) pp = &(*pp)->next;
            if (*pp == fifo) *pp = fifo->next;
        }
        mutex_unlock(&g_fifo_reg_lock);
        if (drop) {
            kfree(fifo->pipe->buffer, PIPE_SIZE);
            kfree(fifo->pipe, sizeof(*fifo->pipe));
            kfree(fifo, sizeof(*fifo));
        }
    }
}

void pipe_create(fs_node_t **read_node, fs_node_t **write_node) {
    pipe_t *p = (pipe_t *)kmalloc(sizeof(pipe_t));
    if (!p) {
        *read_node = NULL;
        *write_node = NULL;
        return;
    }
    memset(p, 0, sizeof(pipe_t));
    p->buffer = (uint8_t *)kmalloc(PIPE_SIZE);
    if (!p->buffer) {
        kfree(p, sizeof(*p));
        *read_node = NULL;
        *write_node = NULL;
        return;
    }
    memset(p->buffer, 0, PIPE_SIZE);
    p->wait_read = &p->head;
    p->wait_write = &p->tail;
    mutex_init(&p->lock, "pipe_lock");
    p->readers_open = 1;
    p->writers_open = 1;

    pipe_endpoint_t *read_ep = (pipe_endpoint_t *)kmalloc(sizeof(pipe_endpoint_t));
    pipe_endpoint_t *write_ep = (pipe_endpoint_t *)kmalloc(sizeof(pipe_endpoint_t));
    fs_node_t *rn = NULL;
    fs_node_t *wn = NULL;
    if (!read_ep || !write_ep) {
        if (read_ep) kfree(read_ep, sizeof(*read_ep));
        if (write_ep) kfree(write_ep, sizeof(*write_ep));
        kfree(p->buffer, PIPE_SIZE);
        kfree(p, sizeof(*p));
        *read_node = NULL;
        *write_node = NULL;
        return;
    }
    memset(read_ep, 0, sizeof(*read_ep));
    memset(write_ep, 0, sizeof(*write_ep));
    read_ep->pipe = p;
    write_ep->pipe = p;
    write_ep->is_writer = 1;

    rn = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    wn = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!rn || !wn) {
        if (rn) kfree(rn, sizeof(*rn));
        if (wn) kfree(wn, sizeof(*wn));
        kfree(read_ep, sizeof(*read_ep));
        kfree(write_ep, sizeof(*write_ep));
        kfree(p->buffer, PIPE_SIZE);
        kfree(p, sizeof(*p));
        *read_node = NULL;
        *write_node = NULL;
        return;
    }
    memset(rn, 0, sizeof(fs_node_t));
    strncpy(rn->name, "pipe_read", sizeof(rn->name) - 1);
    rn->name[sizeof(rn->name) - 1] = '\0';
    rn->flags = FS_PIPE;
    rn->read = &pipe_read;
    rn->poll = &pipe_poll;
    rn->close = &pipe_close;
    rn->impl = (uintptr_t)read_ep;

    memset(wn, 0, sizeof(fs_node_t));
    strncpy(wn->name, "pipe_write", sizeof(wn->name) - 1);
    wn->name[sizeof(wn->name) - 1] = '\0';
    wn->flags = FS_PIPE;
    wn->write = &pipe_write;
    wn->poll = &pipe_poll;
    wn->close = &pipe_close;
    wn->impl = (uintptr_t)write_ep;

    *read_node = rn;
    *write_node = wn;
}

/* Toggle O_NONBLOCK on a pipe endpoint.  Called from sys_pipe2 and
 * (eventually) fcntl(F_SETFL).  Safe to call on a non-pipe node —
 * returns -EINVAL in that case. */
int pipe_set_nonblock(fs_node_t *node, int nonblock) {
    if (!node) return -EINVAL;
    if ((node->flags & 0x7) != FS_PIPE) return -EINVAL;
    pipe_endpoint_t *ep = (pipe_endpoint_t *)(uintptr_t)node->impl;
    if (!ep) return -EINVAL;
    ep->nonblock = nonblock ? 1 : 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* fifo_open — open(2)-side handler for S_IFIFO inodes.
 *
 * Returns a freshly-allocated fs_node_t representing one end of the
 * shared FIFO buffer.  Caller (sys_open) replaces the inode node in
 * the file_t with this shadow node, so subsequent read/write hit
 * the pipe buffer instead of the on-disk inode's blocks.
 *
 * POSIX blocking semantics on first open of each role:
 *   O_RDONLY: block until a writer attaches (unless O_NONBLOCK).
 *   O_WRONLY: block until a reader attaches (with O_NONBLOCK,
 *             return ENXIO immediately if no reader).
 *   O_RDWR  : Linux extension — never blocks, opener counts as
 *             both a reader and a writer (we model it as a reader
 *             so subsequent O_WRONLY can connect; the FD itself
 *             can read AND write since the endpoint isn't role-
 *             pinned at this level).
 * ------------------------------------------------------------------ */

/* O_* flag values match include/fcntl.h.  We can't include that
 * here without dragging userspace headers into the kernel; mirror
 * the bits inline. */
#ifndef O_RDONLY
#define O_RDONLY    0x00
#endif
#ifndef O_WRONLY
#define O_WRONLY    0x01
#endif
#ifndef O_RDWR
#define O_RDWR      0x02
#endif
#ifndef O_ACCMODE
#define O_ACCMODE   0x03
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK  0x800
#endif

static fifo_reg_t *fifo_lookup_or_create(uint32_t dev, uint64_t inum) {
    fifo_reg_lock_init();
    mutex_lock(&g_fifo_reg_lock);
    fifo_reg_t *f = g_fifo_registry;
    while (f) {
        if (f->dev == dev && f->inum == inum) {
            mutex_unlock(&g_fifo_reg_lock);
            return f;
        }
        f = f->next;
    }
    /* Not found — allocate a new entry + its pipe_t. */
    f = (fifo_reg_t *)kmalloc(sizeof(*f));
    if (!f) { mutex_unlock(&g_fifo_reg_lock); return NULL; }
    memset(f, 0, sizeof(*f));
    f->dev  = dev;
    f->inum = inum;
    f->pipe = (pipe_t *)kmalloc(sizeof(pipe_t));
    if (!f->pipe) { kfree(f, sizeof(*f)); mutex_unlock(&g_fifo_reg_lock); return NULL; }
    memset(f->pipe, 0, sizeof(pipe_t));
    f->pipe->buffer = (uint8_t *)kmalloc(PIPE_SIZE);
    if (!f->pipe->buffer) {
        kfree(f->pipe, sizeof(*f->pipe));
        kfree(f, sizeof(*f));
        mutex_unlock(&g_fifo_reg_lock);
        return NULL;
    }
    memset(f->pipe->buffer, 0, PIPE_SIZE);
    f->pipe->wait_read  = &f->pipe->head;
    f->pipe->wait_write = &f->pipe->tail;
    mutex_init(&f->pipe->lock, "fifo_pipe");
    f->next = g_fifo_registry;
    g_fifo_registry = f;
    mutex_unlock(&g_fifo_reg_lock);
    return f;
}

/* Build an fs_node_t endpoint pinned to a shared (FIFO) pipe_t.
 * is_writer determines role; caller bumps refcount + role counters
 * before publishing the node. */
static fs_node_t *fifo_endpoint_new(fifo_reg_t *fifo, int is_writer) {
    pipe_endpoint_t *ep = (pipe_endpoint_t *)kmalloc(sizeof(*ep));
    if (!ep) return NULL;
    fs_node_t *n = (fs_node_t *)kmalloc(sizeof(*n));
    if (!n) { kfree(ep, sizeof(*ep)); return NULL; }
    memset(ep, 0, sizeof(*ep));
    memset(n, 0, sizeof(*n));
    ep->pipe      = fifo->pipe;
    ep->is_writer = is_writer ? 1 : 0;
    ep->fifo      = fifo;
    strncpy(n->name, is_writer ? "fifo_write" : "fifo_read",
            sizeof(n->name) - 1);
    n->name[sizeof(n->name) - 1] = '\0';
    n->flags = FS_PIPE;
    n->read  = &pipe_read;
    n->write = &pipe_write;
    n->poll  = &pipe_poll;
    n->close = &pipe_close;
    n->impl  = (uintptr_t)ep;
    return n;
}

int fifo_open(fs_node_t *inode, int oflags, fs_node_t **out) {
    if (!inode || !out) return -EINVAL;
    *out = NULL;

    int accmode  = oflags & O_ACCMODE;
    int nonblock = (oflags & O_NONBLOCK) ? 1 : 0;

    fifo_reg_t *fifo = fifo_lookup_or_create(0 /*dev*/, inode->inode);
    if (!fifo) return -ENOMEM;

    int is_writer = (accmode == O_WRONLY);
    fs_node_t *node = fifo_endpoint_new(fifo, is_writer);
    if (!node) return -ENOMEM;

    pipe_t *p = fifo->pipe;
    mutex_lock(&p->lock);
    /* Bump role counters + registry refcount before any potential
     * wait — guarantees the pipe stays alive for the duration. */
    if (accmode == O_RDONLY || accmode == O_RDWR) p->readers_open++;
    if (accmode == O_WRONLY || accmode == O_RDWR) p->writers_open++;
    mutex_lock(&g_fifo_reg_lock);
    fifo->refcount++;
    mutex_unlock(&g_fifo_reg_lock);

    /* O_RDWR never blocks. */
    if (accmode == O_RDONLY && !nonblock) {
        while (p->writers_open == 0) {
            sleepq_add(p->wait_read, current_thread);
            mutex_unlock(&p->lock);
            sched_yield();
            mutex_lock(&p->lock);
        }
    } else if (accmode == O_WRONLY) {
        if (p->readers_open == 0) {
            if (nonblock) {
                /* POSIX: open(O_WRONLY|O_NONBLOCK) on a FIFO with no
                 * reader returns ENXIO. */
                p->writers_open--;
                sleepq_wake_all(p->wait_read);  /* paranoia */
                mutex_unlock(&p->lock);
                /* Decrement refcount + free node since open failed. */
                mutex_lock(&g_fifo_reg_lock);
                fifo->refcount--;
                int drop = (fifo->refcount == 0);
                if (drop) {
                    fifo_reg_t **pp = &g_fifo_registry;
                    while (*pp && *pp != fifo) pp = &(*pp)->next;
                    if (*pp == fifo) *pp = fifo->next;
                }
                mutex_unlock(&g_fifo_reg_lock);
                pipe_endpoint_t *ep = (pipe_endpoint_t *)(uintptr_t)node->impl;
                kfree(ep, sizeof(*ep));
                kfree(node, sizeof(*node));
                if (drop) {
                    kfree(fifo->pipe->buffer, PIPE_SIZE);
                    kfree(fifo->pipe, sizeof(*fifo->pipe));
                    kfree(fifo, sizeof(*fifo));
                }
                return -ENXIO;
            }
            while (p->readers_open == 0) {
                sleepq_add(p->wait_write, current_thread);
                mutex_unlock(&p->lock);
                sched_yield();
                mutex_lock(&p->lock);
            }
        }
    }
    /* Wake any open() peers that may be sleeping on us. */
    if (accmode == O_RDONLY || accmode == O_RDWR) sleepq_wake_all(p->wait_write);
    if (accmode == O_WRONLY || accmode == O_RDWR) sleepq_wake_all(p->wait_read);
    mutex_unlock(&p->lock);

    pipe_endpoint_t *ep = (pipe_endpoint_t *)(uintptr_t)node->impl;
    ep->nonblock = nonblock;
    *out = node;
    return 0;
}
