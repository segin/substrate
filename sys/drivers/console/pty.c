/*
 * pty.c — Unix98 pseudo-terminal driver.
 *
 * /dev/ptmx — multiplexer.  open() allocates a PTY pair, returning an
 * fd that backs onto the master side.  TIOCGPTN reports the slave's
 * index; userspace formats /dev/pts/<N>.  TIOCSPTLCK clears the
 * "newly-allocated" lock so the slave node becomes openable.
 *
 * /dev/pts/<N> — slave nodes.  Standard tty backed by the existing
 * N_TTY line discipline (tty.c).  Process group / session semantics
 * come for free since the slave is just a struct tty.
 *
 * Data path:
 *   - master->write(c) — fed to slave through tty_flip_buffer_push
 *     so the slave's iflag/lflag run.  Echo (if ECHO is on) cycles
 *     back through slave's write into the master read buffer.
 *   - slave->write(c)  — drained by the slave's tty_start_locked
 *     into the master read buffer; the master's read() consumes it.
 *
 * Packet mode (TIOCPKT/TIOCGPKT) prepends a 1-byte status byte to
 * each master read.  Non-data state changes (flushes, stop/start)
 * surface as status-only reads.
 */

#include <drivers/console/pty.h>

#include <sys/tty.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/signal.h>
#include <sys/major.h>
#include <sys/copy.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>
#include <kern/sched.h>
#include <kern/console.h>
#include <intr.h>
#include <stdio.h>
#include <string.h>

#define PTY_MAGIC 0x50545932 /* "PTY2" */

typedef struct pty_pair {
    uint32_t        magic;
    int             index;
    int             locked;          /* TIOCSPTLCK: 1 = slave can't open */
    int             packet_mode;     /* TIOCPKT enabled on master */
    uint8_t         packet_status;   /* pending packet status byte */
    int             master_open;     /* fd-count proxy for master */
    int             slave_open;      /* fd-count proxy for slave */
    int             dead;            /* either side hung up */
    int             master_nonblock; /* O_NONBLOCK on the master fd */
    int             is_bsd;          /* BSD-grid pair: static nodes, persistent */
    void           *bsd_owner;       /* bsd_pty_t* when is_bsd */

    struct tty     *master_tty;
    struct tty     *slave_tty;
    fs_node_t      *master_node;
    fs_node_t      *slave_node;

    /* Slave→master byte stream.  Master reads from here. */
    char            mr_data[PTY_MASTER_BUF_SIZE];
    int             mr_head;
    int             mr_tail;
    int             mr_count;

    spinlock_t      lock;
    int             read_wait;       /* opaque wait channel */

    /* Output captured from the slave's write_buf at its last close when the
     * master ring was full -- drained by the master ahead of EOF so a writer's
     * final bufferful isn't lost when it exits with no other slave holder. */
    char           *linger;
    int             linger_len;
    int             linger_pos;
} pty_pair_t;

static pty_pair_t *pty_pairs[PTY_MAX_PAIRS];
static spinlock_t  pty_table_lock = SPINLOCK_INIT("pty_table");

/*
 * BSD-style PTY grid.  Unlike the Unix98 ptmx/pts pair (allocated on
 * /dev/ptmx open, slave at the dynamic /dev/pts/N), a BSD pty is a
 * statically-named node: master /dev/pty[pq][0-9a-f], slave the matching
 * /dev/tty[pq][0-9a-f].  The master is claimed by opening it (EIO if it
 * is already open); the slave opens immediately (no unlockpt).  The
 * backing pty_pair_t is allocated lazily on first open and then kept for
 * the node's lifetime — reused across open/close cycles — so the static
 * fs_nodes never dangle.
 */
#define BSD_PTY_GROUPS  "pq"
#define BSD_PTY_NUMS    "0123456789abcdef"
#define BSD_PTY_NGROUP  2
#define BSD_PTY_NNUM    16
#define BSD_PTY_COUNT   (BSD_PTY_NGROUP * BSD_PTY_NNUM)

typedef struct bsd_pty {
    fs_node_t   master;     /* /dev/ptyXY */
    fs_node_t   slave;      /* /dev/ttyXY */
    pty_pair_t *pair;       /* lazily allocated, then persistent */
} bsd_pty_t;

static bsd_pty_t bsd_ptys[BSD_PTY_COUNT];

/* ------------------------------------------------------------------ */
/* Master read buffer                                                 */
/* ------------------------------------------------------------------ */

static int pty_mr_put(pty_pair_t *p, char c) {
    if (p->mr_count >= PTY_MASTER_BUF_SIZE) {
        return 0;
    }
    p->mr_data[p->mr_head] = c;
    p->mr_head = (p->mr_head + 1) % PTY_MASTER_BUF_SIZE;
    p->mr_count++;
    return 1;
}

static int pty_mr_get(pty_pair_t *p, char *out) {
    if (p->mr_count == 0) {
        return 0;
    }
    *out = p->mr_data[p->mr_tail];
    p->mr_tail = (p->mr_tail + 1) % PTY_MASTER_BUF_SIZE;
    p->mr_count--;
    return 1;
}

static void pty_mr_flush(pty_pair_t *p) {
    p->mr_head = p->mr_tail = p->mr_count = 0;
    if (p->packet_mode) {
        p->packet_status |= TIOCPKT_FLUSHREAD;
    }
}

/* ------------------------------------------------------------------ */
/* tty_driver glue                                                    */
/* ------------------------------------------------------------------ */

/*
 * Slave->master path: the slave runs the line discipline; whatever
 * comes out of tty_output_locked is queued in slave->write_buf and
 * eventually drains here.  We deposit the bytes in the master's
 * read buffer and wake any master reader.
 */
static int pty_slave_drv_write(struct tty *slave_tty,
                               const unsigned char *buf, int count) {
    pty_pair_t *p = slave_tty->driver_data;
    int n = 0;

    if (!p || p->magic != PTY_MAGIC || p->dead) {
        return -EIO;
    }

    spinlock_acquire(&p->lock);
    while (n < count && pty_mr_put(p, (char)buf[n])) {
        n++;
    }
    if (n > 0) {
        sched_wakeup(&p->read_wait);
    }
    spinlock_release(&p->lock);
    return n;
}

static int pty_slave_drv_write_room(struct tty *slave_tty) {
    pty_pair_t *p = slave_tty->driver_data;
    int room;

    if (!p || p->magic != PTY_MAGIC) return 0;
    spinlock_acquire(&p->lock);
    room = PTY_MASTER_BUF_SIZE - p->mr_count;
    spinlock_release(&p->lock);
    return room;
}

static int pty_drv_install(struct tty_driver *drv, struct tty *t) {
    (void)drv;
    (void)t;
    return 0;
}

/*
 * Slave close: the slave's last fd is gone — the session leader /
 * login shell on this PTY has exited.  Mark the pair dead so the
 * master side observes EOF: pty_master_node_poll() then reports
 * POLLHUP|POLLIN and pty_master_node_read() returns 0.  Without this
 * a master-side select()/read() — telnetd's socket<->pty forwarding
 * loop — blocked forever and the network connection it was bridging
 * was never closed.
 */
static void pty_slave_drv_close(struct tty *slave_tty) {
    pty_pair_t *p = slave_tty->driver_data;
    if (!p || p->magic != PTY_MAGIC) return;

    /*
     * The tty layer flushed the slave's write_buf to us just before this
     * close (tty_close), but the master ring may have been full, leaving a
     * tail behind.  Capture that tail into a linger buffer so the master can
     * still read it ahead of the EOF we raise below -- otherwise a writer's
     * final bufferful is discarded when it exits with no other slave holder.
     * kmalloc first (it may sleep) so the copy runs under the lock; the
     * residue is bounded by write_buf, so one TTY_BUF_SIZE buffer suffices.
     */
    char *lb = kmalloc(TTY_BUF_SIZE);
    int   ln = 0;
    if (lb) {
        uint32_t f = intr_disable();
        spinlock_acquire(&slave_tty->lock);
        tty_buffer_t *wb = &slave_tty->write_buf;
        while (wb->count > 0 && ln < TTY_BUF_SIZE) {
            lb[ln++] = wb->data[wb->tail];
            wb->tail = (wb->tail + 1) % TTY_BUF_SIZE;
            wb->count--;
        }
        spinlock_release(&slave_tty->lock);
        intr_restore(f);
    }

    char *free_lb = NULL;
    spinlock_acquire(&p->lock);
    if (lb && ln > 0) {
        p->linger     = lb;
        p->linger_len = ln;
        p->linger_pos = 0;
    } else {
        free_lb = lb;   /* nothing captured (or alloc failed) */
    }
    p->slave_open = 0;
    p->dead       = 1;
    sched_wakeup(&p->read_wait);   /* wake a blocked master poll/read */
    spinlock_release(&p->lock);
    if (free_lb) kfree(free_lb, TTY_BUF_SIZE);
}

static struct tty_driver pty_slave_driver = {
    .driver_name = "ptyslave",
    .name        = "pts",
    .install     = pty_drv_install,
    .close       = pty_slave_drv_close,
    .write       = pty_slave_drv_write,
    .write_room  = pty_slave_drv_write_room,
};

/*
 * Master "tty" exists only as a holder for termios/winsize; it doesn't
 * have a line discipline and doesn't serve hardware.  No driver write
 * callback is needed because the master's write path goes directly
 * to the slave's flip buffer.
 */
static struct tty_driver pty_master_driver = {
    .driver_name = "ptmx",
    .name        = "ptm",
    .install     = pty_drv_install,
};

/* ------------------------------------------------------------------ */
/* Allocation / teardown                                              */
/* ------------------------------------------------------------------ */

static int pty_alloc_index_locked(void) {
    for (int i = 0; i < PTY_MAX_PAIRS; i++) {
        if (!pty_pairs[i]) {
            return i;
        }
    }
    return -1;
}

static void pty_publish_slave_node(pty_pair_t *p) {
    fs_node_t *node = kmalloc(sizeof(fs_node_t));
    if (!node) {
        return;
    }
    memset(node, 0, sizeof(*node));
    snprintf(node->name, sizeof(node->name), "pts/%d", p->index);
    node->flags = FS_CHARDEVICE;
    /*
     * /dev/pts/N: 0620 root:tty until the master's grantpt(3) is
     * invoked; grantpt() chowns to the calling user.  Matches
     * Unix98 / glibc behaviour.
     */
    node->mask  = 0620;
    node->uid   = GID_ROOT;
    node->gid   = GID_TTY;
    /* Unix98 PTY slaves live at major 136, minor = pair index. */
    node->rdev  = makedev(UNIX98_PTS_MAJOR, p->index & 0xFF);
    /* Wire up the same VFS callbacks as a regular tty.  The slave
     * tty is a struct tty so the existing tty_fs_* glue in tty.c
     * Just Works. */
    node->ptr   = (fs_node_t *)p->slave_tty;
    node->read  = tty_fs_read;
    node->write = tty_fs_write;
    node->ioctl = tty_fs_ioctl;
    node->open  = tty_fs_open;
    node->close = tty_fs_close;
    node->poll  = tty_fs_poll;

    p->slave_node = node;
    devfs_register_device(node);
    p->slave_tty->devnode = node;
}

static void pty_unpublish_slave_node(pty_pair_t *p) {
    if (p->slave_node) {
        devfs_unregister_device(p->slave_node);
        kfree(p->slave_node, sizeof(fs_node_t));
        p->slave_node = NULL;
    }
}

static void pty_destroy(pty_pair_t *p);

/*
 * Allocate the core of a PTY pair — index slot, pair struct, slave tty
 * and master tty — but publish NO fs_node.  Shared by the Unix98
 * (ptmx clone) and BSD (static grid) front ends, which differ only in
 * how the master/slave nodes are named and discovered.  Returns the
 * pair (locked=1, master_open=0) or NULL on exhaustion / OOM.
 */
static pty_pair_t *pty_pair_alloc_core(void) {
    int idx;
    pty_pair_t *p;

    spinlock_acquire(&pty_table_lock);
    idx = pty_alloc_index_locked();
    if (idx < 0) {
        spinlock_release(&pty_table_lock);
        return NULL;
    }
    p = kmalloc(sizeof(*p));
    if (!p) {
        spinlock_release(&pty_table_lock);
        return NULL;
    }
    memset(p, 0, sizeof(*p));
    p->magic  = PTY_MAGIC;
    p->index  = idx;
    p->locked = 1;
    spinlock_init(&p->lock, "pty_pair");
    pty_pairs[idx] = p;
    spinlock_release(&pty_table_lock);

    /* Allocate slave tty backed by our line-discipline-bypass driver. */
    p->slave_tty = tty_alloc(&pty_slave_driver, idx);
    if (!p->slave_tty) {
        spinlock_acquire(&pty_table_lock);
        pty_pairs[idx] = NULL;
        spinlock_release(&pty_table_lock);
        kfree(p, sizeof(*p));
        return NULL;
    }
    p->slave_tty->driver_data = p;
    /* Slave runs the standard line discipline — already in tty_alloc
     * via tty_default_termios(). */

    /* Master tty: same struct tty type but with a no-op driver.  We
     * don't put it in the global ttys[] array (use a different idx
     * range), but we do need it for ioctl plumbing.  Allocate
     * directly. */
    p->master_tty = kmalloc(sizeof(struct tty));
    if (!p->master_tty) {
        tty_free(p->slave_tty);
        spinlock_acquire(&pty_table_lock);
        pty_pairs[idx] = NULL;
        spinlock_release(&pty_table_lock);
        kfree(p, sizeof(*p));
        return NULL;
    }
    memset(p->master_tty, 0, sizeof(*p->master_tty));
    p->master_tty->magic = 0x5401; /* TTY_MAGIC */
    p->master_tty->driver = &pty_master_driver;
    p->master_tty->driver_data = p;
    spinlock_init(&p->master_tty->lock, "pty_master");
    tty_default_termios(&p->master_tty->termios);
    /* Master is "raw" — disable line discipline locally.  The slave
     * still applies it on input. */
    p->master_tty->termios.c_iflag = 0;
    p->master_tty->termios.c_oflag = 0;
    p->master_tty->termios.c_lflag = 0;
    p->master_tty->winsize = p->slave_tty->winsize;
    return p;
}

int pty_alloc_pair(fs_node_t **master_node_out) {
    pty_pair_t *p;
    int idx;

    if (!master_node_out) {
        return -EINVAL;
    }

    p = pty_pair_alloc_core();
    if (!p) {
        return -ENOMEM;
    }
    idx = p->index;

    /* Master fs_node — published as /dev/ptm<N>, but the caller
     * (ptmx open) is the only consumer; it doesn't need to be in
     * the directory tree.  Just create it on the heap and return. */
    fs_node_t *mn = kmalloc(sizeof(fs_node_t));
    if (!mn) {
        pty_destroy(p);
        return -ENOMEM;
    }
    memset(mn, 0, sizeof(*mn));
    snprintf(mn->name, sizeof(mn->name), "ptm%d", idx);
    mn->flags = FS_CHARDEVICE;
    mn->mask  = 0600;
    mn->ptr   = (fs_node_t *)p; /* impl pointer for master fops below */
    mn->read  = pty_master_node_read;
    mn->write = pty_master_node_write;
    mn->ioctl = pty_master_node_ioctl;
    mn->close = pty_master_node_close;
    mn->poll  = pty_master_node_poll;
    p->master_node = mn;
    p->master_open = 1;

    *master_node_out = mn;
    return idx;
}

static void pty_destroy(pty_pair_t *p) {
    if (!p) return;
    p->magic = 0;
    /*
     * A BSD-grid pair owns no heap fs_nodes (master_node / slave_node
     * stay NULL — the nodes live statically in bsd_ptys[]).  Detach the
     * owner so a future open re-binds a fresh pair, and make sure the
     * unpublish/kfree paths below never touch the static nodes.  (BSD
     * pairs are normally kept persistent and never reach here; this is
     * defensive.)
     */
    if (p->is_bsd && p->bsd_owner) {
        bsd_pty_t *bp = (bsd_pty_t *)p->bsd_owner;
        bp->pair = NULL;
        bp->master.ptr = NULL;
        bp->slave.ptr  = NULL;
        p->master_node = NULL;
        p->slave_node  = NULL;
    }
    pty_unpublish_slave_node(p);

    /*
     * Sweep the proc table so any process that still has this PTY's
     * slave_tty as its controlling terminal loses the dangling
     * reference.  Without this, `ps` (sys_proc_info) walking the
     * proc list later faults the kernel on the first
     * `target->tty->driver->driver_name` dereference of freed
     * memory.  proc_exit clears its own tty, but a session leader's
     * EXITED-but-not-yet-reaped state or a sibling thread sharing
     * the tty can still hold the pointer.
     */
    if (p->slave_tty) {
        extern struct process *proc_first(void);
        extern struct process *proc_next(struct process *);
        struct process *pr;
        for (pr = proc_first(); pr; pr = proc_next(pr)) {
            if (pr->tty == p->slave_tty) pr->tty = NULL;
        }
    }

    if (p->linger) {
        kfree(p->linger, TTY_BUF_SIZE);
        p->linger = NULL;
    }
    if (p->master_tty) {
        kfree(p->master_tty, sizeof(*p->master_tty));
    }
    if (p->slave_tty) {
        tty_free(p->slave_tty);
    }
    if (p->master_node) {
        kfree(p->master_node, sizeof(fs_node_t));
    }
    spinlock_acquire(&pty_table_lock);
    if (p->index >= 0 && p->index < PTY_MAX_PAIRS &&
        pty_pairs[p->index] == p) {
        pty_pairs[p->index] = NULL;
    }
    spinlock_release(&pty_table_lock);
    kfree(p, sizeof(*p));
}

/* ------------------------------------------------------------------ */
/* Master file ops                                                    */
/* ------------------------------------------------------------------ */

size_t pty_master_node_read(fs_node_t *node, off_t offset, size_t size,
                            uint8_t *buffer) {
    pty_pair_t *p = (pty_pair_t *)node->ptr;
    size_t n = 0;
    (void)offset;

    if (!p || p->magic != PTY_MAGIC) return 0;
    if (size == 0) return 0;

    spinlock_acquire(&p->lock);

    /* Packet mode: emit pending status byte first, before any data.
     * If only a status change is pending and there's no data, this
     * is a status-only read. */
    if (p->packet_mode && p->packet_status) {
        buffer[0] = p->packet_status;
        p->packet_status = 0;
        n = 1;
        spinlock_release(&p->lock);
        return n;
    }

    while (n == 0) {
        char c;
        while (n < size && pty_mr_get(p, &c)) {
            if (n == 0 && p->packet_mode) {
                buffer[n++] = TIOCPKT_DATA;
                if (n >= size) {
                    /* Shouldn't happen for size>0, but be safe. */
                    break;
                }
            }
            buffer[n++] = (uint8_t)c;
        }
        /* Drain any linger buffer (output captured at the slave's last close
         * when the ring was full) after the ring, so it is delivered before
         * the dead-pty EOF below. */
        while (n < size && p->linger && p->linger_pos < p->linger_len) {
            if (n == 0 && p->packet_mode) {
                buffer[n++] = TIOCPKT_DATA;
                if (n >= size) break;
            }
            buffer[n++] = (uint8_t)p->linger[p->linger_pos++];
        }
        if (n > 0) break;
        if (p->dead) break;

        /* No data: a non-blocking master returns EAGAIN instead of
         * parking.  Terminal emulators (xterm) run the master fd
         * non-blocking and poll it through select(). */
        if (p->master_nonblock) {
            struct tty *st = p->slave_tty;
            spinlock_release(&p->lock);
            /* The ring is empty, but the slave may have output buffered
             * behind it (a burst larger than the ring).  Kick it so the
             * residue flows into the ring before we return — otherwise a
             * non-blocking reader (xterm) sees EAGAIN, parks in select(),
             * and the tail only appears on the next unrelated wakeup. */
            if (st) tty_start(st);
            return (size_t)-EAGAIN;
        }

        /* Block interruptibly until data arrives, the pty closes, or
         * a signal is delivered.  THREAD_F_INTERRUPTIBLE is required:
         * without it psignal() cannot wake the sleeper, so a process
         * wedged in read() on the master cannot be killed. */
        spinlock_release(&p->lock);
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        sched_sleep(&p->read_wait);
        current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        spinlock_acquire(&p->lock);

        if (current_thread->sig_pending & ~current_thread->sig_mask) {
            spinlock_release(&p->lock);
            return (size_t)-EINTR;
        }
    }

    struct tty *st = p->slave_tty;
    spinlock_release(&p->lock);

    /* We freed ring space — push any output the slave had buffered
     * behind the ring so it streams to the master without waiting for
     * the next slave write.  (tty_start takes the slave lock then the
     * pair lock, so it must run with p->lock released.) */
    if (n > 0 && st) tty_start(st);
    return n;
}

size_t pty_master_node_write(fs_node_t *node, off_t offset, size_t size,
                             const uint8_t *buffer) {
    pty_pair_t *p = (pty_pair_t *)node->ptr;
    (void)offset;

    if (!p || p->magic != PTY_MAGIC || p->dead) return 0;

    /* Forward each byte through the slave's line discipline. */
    for (size_t i = 0; i < size; i++) {
        tty_flip_buffer_push(p->slave_tty, (char)buffer[i]);
    }
    return size;
}

/*
 * poll(2) support for the PTY master.  Required by select/poll-driven
 * forwarders (e.g. telnetd) that watch the master for data written by
 * the slave-side process.  Returns POLLIN when there's anything
 * pending in the master's read buffer (or a packet-mode status byte),
 * POLLOUT whenever the slave is alive (we don't model slave back-
 * pressure today — flip-buffer push always accepts), and POLLHUP if
 * the pair has been torn down.  When nothing is ready and `waiter`
 * is non-NULL, publish &p->read_wait so the caller blocks on the
 * same channel `tty_flip_buffer_push` wakes from the slave side.
 */
int pty_master_node_poll(fs_node_t *node, void *waiter) {
    pty_pair_t *p = (pty_pair_t *)node->ptr;
    if (!p || p->magic != PTY_MAGIC) return POLLNVAL;

    int events = 0;

    spinlock_acquire(&p->lock);
    if (p->dead) {
        /* A hung-up master is read-ready — read() returns 0 (EOF).
         * Report POLLIN too so select()-based loops (telnetd's
         * socket<->pty forwarder) wake instead of blocking forever. */
        events |= POLLHUP | POLLIN | POLLRDNORM;
    }
    if (p->packet_mode && p->packet_status) {
        events |= POLLIN | POLLRDNORM;
    }
    if (p->mr_count > 0) {
        events |= POLLIN | POLLRDNORM;
    }
    /* Master→slave write goes through tty_flip_buffer_push which
     * doesn't currently back-pressure callers; treat the master as
     * always writable. */
    events |= POLLOUT | POLLWRNORM;
    spinlock_release(&p->lock);

    if ((events & (POLLIN | POLLRDNORM)) == 0 && waiter) {
        *(void **)waiter = &p->read_wait;
    }
    return events;
}

int pty_master_node_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    pty_pair_t *p = (pty_pair_t *)node->ptr;

    if (!p || p->magic != PTY_MAGIC) return -EIO;

    /* `arg` is a user-space pointer.  The int-sized commands handled
     * directly here copy it in/out through copyin/copyout rather than
     * dereferencing it; the forwarded termios/winsize commands are
     * validated by tty_ioctl. */
    switch (request) {
    case TIOCGPTN: {
        if (!arg) return -EINVAL;
        int v = p->index;
        if (copyout(&v, arg, sizeof(v)) != 0) return -EFAULT;
        return 0;
    }
    case TIOCSPTLCK: {
        if (!arg) return -EINVAL;
        int lock;
        if (copyin(arg, &lock, sizeof(lock)) != 0) return -EFAULT;
        spinlock_acquire(&p->lock);
        int was_locked = p->locked;
        p->locked = lock ? 1 : 0;
        spinlock_release(&p->lock);
        /* Unlocking publishes the slave node into devfs. */
        if (was_locked && !lock && !p->slave_node) {
            pty_publish_slave_node(p);
        }
        return 0;
    }
    case TIOCGPKT: {
        if (!arg) return -EINVAL;
        int v = p->packet_mode;
        if (copyout(&v, arg, sizeof(v)) != 0) return -EFAULT;
        return 0;
    }
    case TIOCPKT: {
        if (!arg) return -EINVAL;
        int on;
        if (copyin(arg, &on, sizeof(on)) != 0) return -EFAULT;
        spinlock_acquire(&p->lock);
        p->packet_mode = on ? 1 : 0;
        if (!p->packet_mode) {
            p->packet_status = 0;
        }
        spinlock_release(&p->lock);
        return 0;
    }
    case TIOCSIG: {
        if (!arg) return -EINVAL;
        int sig;
        if (copyin(arg, &sig, sizeof(sig)) != 0) return -EFAULT;
        if (sig <= 0 || sig > 64) return -EINVAL;
        if (p->slave_tty && p->slave_tty->pgrp > 0) {
            (void)signal_send_group(p->slave_tty->pgrp, sig);
        }
        return 0;
    }
    case TCFLSH:
        spinlock_acquire(&p->lock);
        pty_mr_flush(p);
        spinlock_release(&p->lock);
        return 0;
    /* Window-size and termios are forwarded to the slave so callers
     * see the same view from either side. */
    case TIOCGWINSZ:
    case TIOCSWINSZ:
    case TCGETS:
    case TCSETS:
    case TCSETSW:
    case TCSETSF:
    case TIOCGPGRP:
    case TIOCSPGRP:
        return tty_ioctl(p->slave_tty, request, (unsigned long)arg);
    default:
        return -ENOTTY;
    }
}

void pty_master_node_close(fs_node_t *node) {
    pty_pair_t *p = (pty_pair_t *)node->ptr;

    if (!p || p->magic != PTY_MAGIC) return;

    spinlock_acquire(&p->lock);
    p->master_open = 0;
    p->dead = 1;
    /* Wake any reader / writer waiting on us. */
    sched_wakeup(&p->read_wait);
    int slave_open = p->slave_open;
    struct tty *slave_tty = p->slave_tty;
    spinlock_release(&p->lock);

    /* Hangup the slave: any pending slave read returns 0 (EOF)
     * after draining whatever's already in raw_buf.  Wake any
     * blocked slave reader so it can observe hung_up and return.
     * SIGHUP delivery to the slave's session is a follow-up. */
    if (slave_tty) {
        spinlock_acquire(&slave_tty->lock);
        slave_tty->hung_up = 1;
        spinlock_release(&slave_tty->lock);
        sched_wakeup(&slave_tty->read_wait);
        sched_wakeup(&slave_tty->poll_wait);
        /* Also wake a slave writer blocked in tty_write's flow-control loop,
         * so it observes hung_up and returns -EIO instead of hanging. */
        sched_wakeup(&slave_tty->write_wait);
    }

    /* If the slave is also closed (or never opened), free everything. */
    if (!slave_open) {
        pty_destroy(p);
    }
}

/* ------------------------------------------------------------------ */
/* /dev/ptmx                                                          */
/* ------------------------------------------------------------------ */

/*
 * /dev/ptmx is a clone device.  Each open returns a fresh master fd.
 * We can't represent that with a single shared fs_node_t, so the
 * ptmx node's open() pre-allocates a pair and stashes the master
 * fs_node into the calling thread's pending slot — the syscall layer
 * picks it up.
 *
 * A simpler approach (taken here): the ptmx node has read/write/ioctl
 * callbacks that consult current_process->pending_pty_master, which
 * is set by ptmx_open() and consumed by the next read/write/ioctl on
 * the same fd.  We keep that pointer per-fd via the fs_node_t.ptr
 * indirection: ptmx_open clones the master node and the caller's fd
 * references the clone.
 *
 * Since the existing VFS open layer doesn't natively support clone
 * devices, we override the ptmx node to allocate a pair on each open
 * and route subsequent operations to the freshly-published master.
 */

static fs_node_t ptmx_node;

static void ptmx_open(fs_node_t *node) {
    fs_node_t *master = NULL;
    int idx = pty_alloc_pair(&master);
    if (idx < 0 || !master) {
        /* No way to surface the error from the void open() callback;
         * a follow-up read/write will find ptr==NULL and fail. */
        node->impl = 0;
        return;
    }
    /* Stash the freshly-allocated master node.  Subsequent ops on
     * the ptmx fd dispatch through the master's fops directly because
     * the open syscall in this kernel returns the SAME fs_node_t
     * passed to open().  So instead, we mutate the ptmx fd's
     * fs_node_t pointer chain via node->ptr — reads/writes/ioctls
     * call our forwarding stubs which dereference node->ptr. */
    node->impl = (uintptr_t)master;
}

static size_t ptmx_node_read(fs_node_t *node, off_t off, size_t sz, uint8_t *b) {
    fs_node_t *master = (fs_node_t *)node->impl;
    if (!master) return 0;
    return pty_master_node_read(master, off, sz, b);
}
static size_t ptmx_node_write(fs_node_t *node, off_t off, size_t sz,
                              const uint8_t *b) {
    fs_node_t *master = (fs_node_t *)node->impl;
    if (!master) return 0;
    return pty_master_node_write(master, off, sz, b);
}
static int ptmx_node_ioctl(fs_node_t *node, uint32_t req, void *arg) {
    fs_node_t *master = (fs_node_t *)node->impl;
    if (!master) return -ENOTTY;
    return pty_master_node_ioctl(master, req, arg);
}
static int ptmx_node_poll(fs_node_t *node, void *waiter) {
    fs_node_t *master = (fs_node_t *)node->impl;
    if (!master) return POLLNVAL;
    return pty_master_node_poll(master, waiter);
}
static void ptmx_node_close(fs_node_t *node) {
    fs_node_t *master = (fs_node_t *)node->impl;
    if (master) {
        pty_master_node_close(master);
    }
    node->impl = 0;
}

/* Mirror a master fd's O_NONBLOCK flag down to the pair so that
 * pty_master_node_read() can honour it — the read callback only ever
 * sees the fs_node, never the file_t.  A posix_openpt() fd is a
 * per-open clone of `ptmx_node` (->read == ptmx_node_read), whose
 * ->impl points at the real per-pair master node; a directly opened
 * master node has ->read == pty_master_node_read.  Returns 1 if
 * `node` is a pty master, 0 otherwise (called unconditionally from
 * the F_SETFL / FIONBIO path). */
int pty_set_nonblock(fs_node_t *node, int on) {
    if (!node)
        return 0;
    fs_node_t *master = (node->read == ptmx_node_read)
        ? (fs_node_t *)node->impl
        : node;
    if (!master || master->read != pty_master_node_read)
        return 0;
    pty_pair_t *p = (pty_pair_t *)master->ptr;
    if (!p || p->magic != PTY_MAGIC)
        return 0;
    p->master_nonblock = on ? 1 : 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* BSD pty grid front end                                             */
/* ------------------------------------------------------------------ */

/* Bind a backing pair to a BSD node pair, wiring the static master and
 * slave fs_nodes onto it.  Returns the pair or NULL on exhaustion. */
static pty_pair_t *bsd_pty_ensure_pair(bsd_pty_t *bp) {
    if (bp->pair) {
        return bp->pair;
    }
    pty_pair_t *p = pty_pair_alloc_core();
    if (!p) {
        return NULL;
    }
    p->is_bsd    = 1;
    p->bsd_owner = bp;
    p->locked    = 0;                 /* BSD slave opens without unlockpt */
    bp->pair = p;
    bp->master.ptr = (fs_node_t *)p;          /* master fops dispatch via ptr */
    bp->slave.ptr  = (fs_node_t *)p->slave_tty;
    p->slave_tty->devnode = &bp->slave;
    return p;
}

/* True if `node` is one of the static BSD master nodes. */
int pty_is_bsd_master(fs_node_t *node) {
    if (!node) {
        return 0;
    }
    for (int i = 0; i < BSD_PTY_COUNT; i++) {
        if (&bsd_ptys[i].master == node) {
            return 1;
        }
    }
    return 0;
}

/*
 * Claim a BSD master.  Called from the open(2) path (which CAN fail the
 * open, unlike the void node->open callback) so an already-open master
 * returns -EIO — exactly what a legacy BSD pty scan loop probes for.
 */
int pty_bsd_master_open(fs_node_t *node) {
    bsd_pty_t *bp = (bsd_pty_t *)node->impl;
    pty_pair_t *p;

    if (!bp) {
        return -ENXIO;
    }
    if (bp->pair && bp->pair->master_open) {
        return -EIO;                  /* already claimed */
    }
    p = bsd_pty_ensure_pair(bp);
    if (!p) {
        return -ENOSPC;
    }

    /* (Re)initialise for this open: drop any stale buffered data and
     * clear the hangup left by the previous close. */
    char *stale_linger;
    spinlock_acquire(&p->lock);
    pty_mr_flush(p);
    stale_linger       = p->linger;   /* undrained residue from a prior session */
    p->linger          = NULL;
    p->linger_len      = 0;
    p->linger_pos      = 0;
    p->dead            = 0;
    p->master_open     = 1;
    p->master_nonblock = 0;
    spinlock_release(&p->lock);
    if (stale_linger) kfree(stale_linger, TTY_BUF_SIZE);
    if (p->slave_tty) {
        spinlock_acquire(&p->slave_tty->lock);
        p->slave_tty->hung_up = 0;
        spinlock_release(&p->slave_tty->lock);
    }
    node->ptr = (fs_node_t *)p;
    return 0;
}

/* Master close: hang up the slave but keep the pair allocated so the
 * static node can be reopened.  (Mirrors pty_master_node_close minus the
 * pty_destroy — BSD pairs are persistent.) */
static void pty_bsd_master_close(fs_node_t *node) {
    pty_pair_t *p = (pty_pair_t *)node->ptr;
    struct tty *st;

    if (!p || p->magic != PTY_MAGIC) {
        return;
    }
    spinlock_acquire(&p->lock);
    p->master_open = 0;
    p->dead        = 1;
    sched_wakeup(&p->read_wait);
    st = p->slave_tty;
    spinlock_release(&p->lock);

    if (st) {
        spinlock_acquire(&st->lock);
        st->hung_up = 1;
        spinlock_release(&st->lock);
        sched_wakeup(&st->read_wait);
        sched_wakeup(&st->poll_wait);
    }
}

/* Slave open: ensure a pair exists (the master usually opens first, but
 * a slave-first open must not dereference a NULL tty) then chain to the
 * standard tty open glue. */
static void pty_bsd_slave_open(fs_node_t *node) {
    bsd_pty_t *bp = (bsd_pty_t *)node->impl;

    if (bp && !bp->pair) {
        (void)bsd_pty_ensure_pair(bp);
    }
    if (bp && bp->pair) {
        node->ptr = (fs_node_t *)bp->pair->slave_tty;
    }
    if (node->ptr) {
        tty_fs_open(node);
    }
}

static void pty_bsd_init(void) {
    memset(bsd_ptys, 0, sizeof(bsd_ptys));

    for (int g = 0; g < BSD_PTY_NGROUP; g++) {
        for (int n = 0; n < BSD_PTY_NNUM; n++) {
            bsd_pty_t *bp = &bsd_ptys[g * BSD_PTY_NNUM + n];
            char gc = BSD_PTY_GROUPS[g];
            char nc = BSD_PTY_NUMS[n];

            /* Master /dev/ptyXY.  open is handled by the open(2) hook
             * (pty_bsd_master_open), not the void node->open. */
            snprintf(bp->master.name, sizeof(bp->master.name), "pty%c%c", gc, nc);
            bp->master.flags = FS_CHARDEVICE;
            bp->master.mask  = 0666;
            bp->master.uid   = GID_ROOT;
            bp->master.gid   = GID_TTY;
            bp->master.rdev  = makedev(PTY_MASTER_MAJOR,
                                       g * BSD_PTY_NNUM + n);
            bp->master.impl  = (uintptr_t)bp;
            bp->master.read  = pty_master_node_read;
            bp->master.write = pty_master_node_write;
            bp->master.ioctl = pty_master_node_ioctl;
            bp->master.close = pty_bsd_master_close;
            bp->master.poll  = pty_master_node_poll;
            devfs_register_device(&bp->master);

            /* Slave /dev/ttyXY — a standard tty once the pair exists. */
            snprintf(bp->slave.name, sizeof(bp->slave.name), "tty%c%c", gc, nc);
            bp->slave.flags = FS_CHARDEVICE;
            bp->slave.mask  = 0620;
            bp->slave.uid   = GID_ROOT;
            bp->slave.gid   = GID_TTY;
            bp->slave.rdev  = makedev(PTY_SLAVE_MAJOR,
                                      g * BSD_PTY_NNUM + n);
            bp->slave.impl  = (uintptr_t)bp;
            bp->slave.open  = pty_bsd_slave_open;
            bp->slave.read  = tty_fs_read;
            bp->slave.write = tty_fs_write;
            bp->slave.ioctl = tty_fs_ioctl;
            bp->slave.close = tty_fs_close;
            bp->slave.poll  = tty_fs_poll;
            devfs_register_device(&bp->slave);
        }
    }
}

void pty_init(void) {
    memset(pty_pairs, 0, sizeof(pty_pairs));

    memset(&ptmx_node, 0, sizeof(ptmx_node));
    strlcpy(ptmx_node.name, "ptmx", sizeof(ptmx_node.name));
    ptmx_node.flags = FS_CHARDEVICE;
    /*
     * /dev/ptmx is the clone-device entry point: anyone may open()
     * it to allocate a new master/slave pair.  Hence 0666 — the
     * actual access check happens on the resulting slave (/dev/pts/N).
     */
    ptmx_node.mask  = 0666;
    ptmx_node.uid   = GID_ROOT;
    ptmx_node.gid   = GID_TTY;
    ptmx_node.rdev  = makedev(TTYAUX_MAJOR, TTYAUX_MINOR_PTMX);
    ptmx_node.open  = ptmx_open;
    ptmx_node.read  = ptmx_node_read;
    ptmx_node.write = ptmx_node_write;
    ptmx_node.ioctl = ptmx_node_ioctl;
    ptmx_node.close = ptmx_node_close;
    ptmx_node.poll  = ptmx_node_poll;
    devfs_register_device(&ptmx_node);

    /* BSD-style static pty grid (/dev/pty[pq][0-9a-f] + /dev/tty...). */
    pty_bsd_init();
}
