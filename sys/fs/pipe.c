#include <stddef.h>
#include <string.h>

#include <arch/i386/intr.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

#define PIPE_SIZE 4096
/* POSIX requires a write of at most PIPE_BUF bytes to be atomic: it either
 * goes into the pipe as one contiguous run or the writer blocks until it
 * can.  PIPE_BUF must not exceed the buffer, or such a write could never be
 * satisfied and the writer would block forever. */
#define PIPE_BUF  PIPE_SIZE

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
    /*
     * PIPE-10: these two record which of the pipe's role counters THIS
     * endpoint incremented, so close decrements exactly those.  They are not
     * mutually exclusive: an O_RDWR FIFO open holds both.  The old code had
     * only is_writer, set to (accmode == O_WRONLY) -- so an O_RDWR open bumped
     * both readers_open and writers_open but close decremented only
     * readers_open, leaving writers_open stuck at 1 forever.  Readers of that
     * FIFO then never saw EOF (they blocked indefinitely) and a later
     * O_RDONLY open skipped its wait-for-writer loop entirely.
     */
    uint8_t is_reader;
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
    /*
     * PIPE-11: keyed on the inode number ALONE, this registry made
     * /mnt/a/fifo and /tmp/fifo collide whenever both happened to be inode 12
     * -- inode numbers restart per filesystem.  Two unrelated FIFOs owned by
     * different users then shared one pipe_t: A's writes were readable by B,
     * and the shared reader/writer counters gave both the wrong EOF and
     * blocking-open behaviour.  The mount pointer identifies the filesystem
     * (a mount cannot be unmounted while a file on it is open), so
     * (mp, inum) names exactly one file.
     */
    const void *mp;
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

/* Wake every thread parked on `chan`, via BOTH wake mechanisms.
 *
 * sleepq_wake_all walks the sleepq hash — wakes threads that
 * registered via sleepq_add / sleepq_wait (what pipe_wait does).
 * sched_wakeup walks the global thread list checking
 * thread->wait_chan == chan — wakes threads that landed in
 * THREAD_BLOCKED via sched_sleep / sched_sleep_until (what
 * sys_poll's select-multiplex path does).
 *
 * These two channel systems do NOT see each other.  Without
 * calling both, a thread sleeping in poll() never gets woken by
 * a pipe_write — visible as torture_pipe scenarios 9, 10, 13
 * timing out and bsdtar's filter driver hanging until its
 * internal short poll timeout re-checks. */
static inline void pipe_wake(void *chan) {
    sleepq_wake_all(chan);
    sched_wakeup(chan);
}

/* Returns 0 on normal wake (data arrived, peer closed, etc.) and
 * -EINTR if psignal yanked us out via signal_interrupt_thread.
 * Order is load-bearing: set THREAD_F_INTERRUPTIBLE before letting
 * the thread land in THREAD_BLOCKED so psignal scans see the flag
 * and actually call signal_interrupt_thread on us. */
static int pipe_wait(void *chan, mutex_t *m) {
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;
    /*
     * sleepq_add flips us to THREAD_BLOCKED, but we still hold the pipe
     * mutex `m` until the mutex_unlock below.  If a timer tick preempts us
     * in that window the scheduler deschedules us *while we hold the lock*
     * (a BLOCKED thread is never re-selected), and the peer that needs the
     * lock to write+wake us then deadlocks acquiring it.  For the thread
     * ping-pong that is recovered only by the slow ~50 ms deadline below
     * (so it crawls at ~50 ms/iter and the watchdog kills it); for the
     * fork ping-pong it wedges the kernel.  Hold interrupts off across the
     * register-then-release so we cannot be preempted while BLOCKED and
     * still holding the mutex.  Once the mutex is dropped we are safe to
     * preempt (a normal wake just flips us back to READY), so re-enable
     * before the sched_yield, which manages its own interrupt state.
     */
    uint32_t _pf = intr_disable();
    sleepq_add(chan, current_thread);
    /*
     * Fallback deadline against a lost sleepq wakeup.  sched_sleep /
     * sched_wakeup (and the sleepq park here) are not race-free: under
     * kernel preemption a wakeup can fire in the window between a peer's
     * readiness check and our landing on the queue, and be lost -- the
     * pipe then hangs forever (sleep_expiry == 0, no recovery).  Arm a
     * short deadline so sched_tick re-readies us even if the wakeup was
     * lost; we then re-check the pipe and proceed.  Pipe wakeups are
     * normally immediate, so this only fires on an actual loss.
     */
    if (current_thread->sleep_expiry == 0) {
        uint32_t hz = get_hz();
        uint64_t span = hz ? (hz / 20u) : 8u;   /* ~50 ms */
        if (span == 0) span = 1;
        current_thread->sleep_expiry = get_ticks() + span;
    }
    mutex_unlock(m);
    intr_restore(_pf);
    if (current_thread->wait_chan == chan) {
        sched_yield();
    }
    current_thread->sleep_expiry = 0;
    /*
     * If sched_tick re-readied us via the deadline it left us on the
     * sleepq (it can't dequeue from IRQ context) -- remove ourselves now,
     * in thread context, before anyone re-adds us.  A normal wakeup already
     * dequeued us and cleared wait_chan, so this is then a no-op.
     */
    sleepq_remove_thread(current_thread);
    mutex_lock(m);
    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
    if (current_thread->sig_pending & ~current_thread->sig_mask)
        return -EINTR;
    return 0;
}

/* Block during fifo_open() until a peer arrives.  Same enqueue-and-release
 * under intr_disable + lost-wakeup net as pipe_wait().
 *
 * PIPE-22: this used to omit THREAD_F_INTERRUPTIBLE deliberately, on the
 * reasoning that a looped interruptible wait would busy-spin on a pending
 * signal.  The cost was that `mkfifo /tmp/f; cat /tmp/f` with no writer parked
 * the process permanently -- psignal's scan skips non-interruptible threads,
 * so neither SIGTERM nor SIGKILL could reach it.  POSIX requires the blocking
 * FIFO open to be interruptible and fail with EINTR.  There is no busy-spin
 * because the caller does not loop on -EINTR: it unwinds and returns the
 * error.  Caller holds p->lock; it is still held on return.
 *
 * Returns 0 on a normal wake, -EINTR if a signal is pending.  */
static int fifo_open_wait(pipe_t *p, void *chan) {
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;
    uint32_t _pf = intr_disable();
    sleepq_add(chan, current_thread);
    if (current_thread->sleep_expiry == 0) {
        uint32_t hz = get_hz();
        uint64_t span = hz ? (hz / 20u) : 8u;   /* ~50 ms lost-wakeup net */
        if (span == 0) span = 1;
        current_thread->sleep_expiry = get_ticks() + span;
    }
    mutex_unlock(&p->lock);
    intr_restore(_pf);
    if (current_thread->wait_chan == chan)
        sched_yield();
    current_thread->sleep_expiry = 0;
    sleepq_remove_thread(current_thread);
    mutex_lock(&p->lock);
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

    /* Ring copy with at most two memcpy()s instead of a byte-at-a-time loop
     * with a per-byte modulo (a divide). */
    size_t i = (size < p->count) ? size : p->count;
    if (i > 0) {
        size_t first = PIPE_SIZE - p->tail;
        if (first > i) first = i;
        memcpy(buffer, p->buffer + p->tail, first);
        if (i > first) memcpy(buffer + first, p->buffer, i - first);
        p->tail = (p->tail + i) % PIPE_SIZE;
        p->count -= i;
    }

    pipe_wake(p->wait_write);
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
            /* POSIX: write to a pipe whose reader has closed must
             * raise SIGPIPE and return -EPIPE — not a short or
             * zero count.  Preserve the partial-write byte count
             * if we got some data through before the reader left.  */
            if (written > 0) return written;
            if (current_process) psignal(current_process, 13);   /* SIGPIPE */
            return (size_t)-EPIPE;
        }

        /*
         * PIPE-18: the wait condition used to be "buffer completely full",
         * so a writer resumed as soon as a single byte drained and copied
         * whatever fit.  With PIPE_SIZE 4096 any backlog guarantees a
         * full-buffer wait, so two processes each doing write(fd, buf, 4096)
         * interleaved their bytes -- breaking the multi-writer logging
         * pattern POSIX explicitly supports.  Require room for the whole
         * atomic unit (the request, capped at PIPE_BUF) before starting to
         * copy.  Writes larger than PIPE_BUF are not atomic by definition and
         * still proceed in chunks, which is why `need` is the smaller of the
         * two.
         */
        size_t remaining = size - written;
        size_t need = remaining > PIPE_BUF ? PIPE_BUF : remaining;
        while (PIPE_SIZE - p->count < need) {
            if (p->readers_open == 0) {
                mutex_unlock(&p->lock);
                if (written > 0) return written;
                if (current_process) psignal(current_process, 13);   /* SIGPIPE */
                return (size_t)-EPIPE;
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

        /* Ring copy with at most two memcpy()s instead of a byte-at-a-time
         * loop with a per-byte modulo. */
        size_t avail = PIPE_SIZE - p->count;
        size_t chunk = size - written;
        if (chunk > avail) chunk = avail;
        if (chunk > 0) {
            size_t first = PIPE_SIZE - p->head;
            if (first > chunk) first = chunk;
            memcpy(p->buffer + p->head, buffer + written, first);
            if (chunk > first) memcpy(p->buffer, buffer + written + first, chunk - first);
            p->head = (p->head + chunk) % PIPE_SIZE;
            p->count += chunk;
            written += chunk;
        }

        pipe_wake(p->wait_read);
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

    /*
     * PIPE-23: this branched solely on is_writer, which is 0 for an O_RDWR
     * endpoint -- the standard trick for holding a FIFO open without
     * blocking -- so such an endpoint never reported POLLOUT and was unusable
     * from a poll/select event loop.  Report each direction the endpoint
     * actually holds; a bidirectional endpoint gets both.
     */
    if (ep->is_writer) {
        /* Writer side: POLLOUT when there's room (or all readers
         * gone — write would EPIPE, but poll surfaces it as POLLERR).  */
        if (no_readers) events |= POLLERR;
        else if (p->count < PIPE_SIZE) events |= POLLOUT | POLLWRNORM;
    }
    if (ep->is_reader) {
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
    /* PIPE-10: drop exactly the counters this endpoint took (both, for an
     * O_RDWR FIFO endpoint). */
    if (ep->is_writer) {
        if (p->writers_open > 0) {
            p->writers_open--;
        }
        pipe_wake(p->wait_read);
    }
    if (ep->is_reader) {
        if (p->readers_open > 0) {
            p->readers_open--;
        }
        pipe_wake(p->wait_write);
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
    /* An anonymous pipe's ends are strictly unidirectional; each holds exactly
     * one of the role counters that pipe() set to 1 above. */
    read_ep->is_reader  = 1;
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
    strlcpy(rn->name, "pipe_read", sizeof(rn->name));
    rn->name[sizeof(rn->name) - 1] = '\0';
    rn->flags = FS_PIPE;
    rn->read = &pipe_read;
    rn->poll = &pipe_poll;
    rn->close = &pipe_close;
    rn->impl = (uintptr_t)read_ep;

    memset(wn, 0, sizeof(fs_node_t));
    strlcpy(wn->name, "pipe_write", sizeof(wn->name));
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

static fifo_reg_t *fifo_lookup_or_create(const void *mp, uint64_t inum) {
    fifo_reg_lock_init();
    mutex_lock(&g_fifo_reg_lock);
    fifo_reg_t *f = g_fifo_registry;
    while (f) {
        if (f->mp == mp && f->inum == inum) {
            /* Take the reference HERE, under the registry lock.  Returning an
             * unreferenced entry and letting the caller bump it after
             * mutex_lock(&p->lock) leaves a window in which the last existing
             * endpoint closes, drops refcount to 0, and frees f->pipe->buffer,
             * f->pipe and f -- so the caller then locks a freed mutex. */
            f->refcount++;
            mutex_unlock(&g_fifo_reg_lock);
            return f;
        }
        f = f->next;
    }
    /* Not found — allocate a new entry + its pipe_t. */
    f = (fifo_reg_t *)kmalloc(sizeof(*f));
    if (!f) { mutex_unlock(&g_fifo_reg_lock); return NULL; }
    memset(f, 0, sizeof(*f));
    f->mp   = mp;
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
    /* Same reference the hit path takes, so both return an entry the caller
     * already owns a count on. */
    f->refcount++;
    f->next = g_fifo_registry;
    g_fifo_registry = f;
    mutex_unlock(&g_fifo_reg_lock);
    return f;
}

/* Build an fs_node_t endpoint pinned to a shared (FIFO) pipe_t.
 * is_writer determines role; caller bumps refcount + role counters
 * before publishing the node. */
static fs_node_t *fifo_endpoint_new(fifo_reg_t *fifo, int is_reader, int is_writer) {
    pipe_endpoint_t *ep = (pipe_endpoint_t *)kmalloc(sizeof(*ep));
    if (!ep) return NULL;
    fs_node_t *n = (fs_node_t *)kmalloc(sizeof(*n));
    if (!n) { kfree(ep, sizeof(*ep)); return NULL; }
    memset(ep, 0, sizeof(*ep));
    memset(n, 0, sizeof(*n));
    ep->pipe      = fifo->pipe;
    ep->is_reader = is_reader ? 1 : 0;
    ep->is_writer = is_writer ? 1 : 0;
    ep->fifo      = fifo;
    strlcpy(n->name,
            (is_reader && is_writer) ? "fifo_rdwr"
                                     : (is_writer ? "fifo_write" : "fifo_read"),
            sizeof(n->name));
    n->name[sizeof(n->name) - 1] = '\0';
    n->flags = FS_PIPE;
    n->read  = &pipe_read;
    n->write = &pipe_write;
    n->poll  = &pipe_poll;
    n->close = &pipe_close;
    n->impl  = (uintptr_t)ep;
    return n;
}

/*
 * Unwind a fifo_open() that got as far as bumping the role counters.  Drops
 * exactly the counters this open took, releases the registry reference (and
 * tears the entry down if it was the last), frees the endpoint and node, and
 * returns `err`.  Caller holds p->lock; it is released here.
 *
 * PIPE-22 needs this on the EINTR path; the O_WRONLY|O_NONBLOCK ENXIO path
 * had grown its own copy of the same sequence, which now shares this one.
 */
static int fifo_open_unwind(fifo_reg_t *fifo, fs_node_t *node, pipe_t *p,
                            int is_reader, int is_writer, int err) {
    if (is_reader && p->readers_open > 0) p->readers_open--;
    if (is_writer && p->writers_open > 0) p->writers_open--;
    /* A peer may be blocked in its own open() waiting on the role we just
     * gave up; let it re-evaluate. */
    pipe_wake(p->wait_read);
    pipe_wake(p->wait_write);
    mutex_unlock(&p->lock);

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
    return err;
}

int fifo_open(fs_node_t *inode, int oflags, fs_node_t **out) {
    if (!inode || !out) return -EINVAL;
    *out = NULL;

    int accmode  = oflags & O_ACCMODE;
    int nonblock = (oflags & O_NONBLOCK) ? 1 : 0;

    fifo_reg_t *fifo = fifo_lookup_or_create(inode->mp, inode->inode);
    if (!fifo) return -ENOMEM;

    /* PIPE-10: an O_RDWR open holds BOTH roles.  Derive the pair once and use
     * it for the counter bumps below and for the endpoint, so open and close
     * cannot disagree about what was taken. */
    int is_reader = (accmode == O_RDONLY || accmode == O_RDWR);
    int is_writer = (accmode == O_WRONLY || accmode == O_RDWR);
    fs_node_t *node = fifo_endpoint_new(fifo, is_reader, is_writer);
    if (!node) {
        /* fifo_lookup_or_create() handed us a reference; release it, and tear
         * the entry down if we were the only holder. */
        mutex_lock(&g_fifo_reg_lock);
        fifo->refcount--;
        int drop = (fifo->refcount == 0);
        if (drop) {
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
        return -ENOMEM;
    }

    pipe_t *p = fifo->pipe;
    mutex_lock(&p->lock);
    /* Bump role counters before any potential wait.  The registry refcount
     * that keeps the pipe alive for the duration was already taken by
     * fifo_lookup_or_create() under the registry lock. */
    if (is_reader) p->readers_open++;
    if (is_writer) p->writers_open++;

    /* O_RDWR never blocks. */
    if (accmode == O_RDONLY && !nonblock) {
        while (p->writers_open == 0) {
            if (fifo_open_wait(p, p->wait_read) == -EINTR)
                return fifo_open_unwind(fifo, node, p, is_reader, is_writer,
                                        -EINTR);
        }
    } else if (accmode == O_WRONLY) {
        if (p->readers_open == 0) {
            if (nonblock) {
                /* POSIX: open(O_WRONLY|O_NONBLOCK) on a FIFO with no
                 * reader returns ENXIO. */
                return fifo_open_unwind(fifo, node, p, is_reader, is_writer,
                                        -ENXIO);
            }
            while (p->readers_open == 0) {
                if (fifo_open_wait(p, p->wait_write) == -EINTR)
                    return fifo_open_unwind(fifo, node, p, is_reader,
                                            is_writer, -EINTR);
            }
        }
    }
    /* Wake any open() peers that may be sleeping on us. */
    if (accmode == O_RDONLY || accmode == O_RDWR) pipe_wake(p->wait_write);
    if (accmode == O_WRONLY || accmode == O_RDWR) pipe_wake(p->wait_read);
    mutex_unlock(&p->lock);

    pipe_endpoint_t *ep = (pipe_endpoint_t *)(uintptr_t)node->impl;
    ep->nonblock = nonblock;
    *out = node;
    return 0;
}
