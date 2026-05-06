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

#include "pty.h"

#include <sys/tty.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/signal.h>
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
} pty_pair_t;

static pty_pair_t *pty_pairs[PTY_MAX_PAIRS];
static spinlock_t  pty_table_lock = SPINLOCK_INIT("pty_table");

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

static struct tty_driver pty_slave_driver = {
    .driver_name = "ptyslave",
    .name        = "pts",
    .install     = pty_drv_install,
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
    node->mask  = 0620;
    node->uid   = 0;
    node->gid   = 0;
    /* Wire up the same VFS callbacks as a regular tty.  The slave
     * tty is a struct tty so the existing tty_fs_* glue in tty.c
     * Just Works. */
    node->ptr   = (fs_node_t *)p->slave_tty;
    node->read  = tty_fs_read;
    node->write = tty_fs_write;
    node->ioctl = tty_fs_ioctl;
    node->open  = tty_fs_open;
    node->close = tty_fs_close;

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

int pty_alloc_pair(fs_node_t **master_node_out) {
    int idx;
    pty_pair_t *p;

    if (!master_node_out) {
        return -EINVAL;
    }

    spinlock_acquire(&pty_table_lock);
    idx = pty_alloc_index_locked();
    if (idx < 0) {
        spinlock_release(&pty_table_lock);
        return -ENOSPC;
    }
    p = kmalloc(sizeof(*p));
    if (!p) {
        spinlock_release(&pty_table_lock);
        return -ENOMEM;
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
        return -ENOMEM;
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
        return -ENOMEM;
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

    /* Master fs_node — published as /dev/ptm<N>, but the caller
     * (ptmx open) is the only consumer; it doesn't need to be in
     * the directory tree.  Just create it on the heap and return. */
    fs_node_t *mn = kmalloc(sizeof(fs_node_t));
    if (!mn) {
        kfree(p->master_tty, sizeof(*p->master_tty));
        tty_free(p->slave_tty);
        spinlock_acquire(&pty_table_lock);
        pty_pairs[idx] = NULL;
        spinlock_release(&pty_table_lock);
        kfree(p, sizeof(*p));
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
    p->master_node = mn;
    p->master_open = 1;

    *master_node_out = mn;
    return idx;
}

static void pty_destroy(pty_pair_t *p) {
    if (!p) return;
    p->magic = 0;
    pty_unpublish_slave_node(p);
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
        if (n > 0) break;
        if (p->dead) break;

        /* Block until something arrives (or pty closes). */
        spinlock_release(&p->lock);
        sched_sleep(&p->read_wait);
        spinlock_acquire(&p->lock);
    }

    spinlock_release(&p->lock);
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

int pty_master_node_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    pty_pair_t *p = (pty_pair_t *)node->ptr;

    if (!p || p->magic != PTY_MAGIC) return -EIO;

    switch (request) {
    case TIOCGPTN:
        if (!arg) return -EINVAL;
        *(int *)arg = p->index;
        return 0;
    case TIOCSPTLCK: {
        if (!arg) return -EINVAL;
        int lock = *(int *)arg;
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
    case TIOCGPKT:
        if (!arg) return -EINVAL;
        *(int *)arg = p->packet_mode;
        return 0;
    case TIOCPKT:
        if (!arg) return -EINVAL;
        spinlock_acquire(&p->lock);
        p->packet_mode = *(int *)arg ? 1 : 0;
        if (!p->packet_mode) {
            p->packet_status = 0;
        }
        spinlock_release(&p->lock);
        return 0;
    case TIOCSIG: {
        if (!arg) return -EINVAL;
        int sig = *(int *)arg;
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
    spinlock_release(&p->lock);

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
static void ptmx_node_close(fs_node_t *node) {
    fs_node_t *master = (fs_node_t *)node->impl;
    if (master) {
        pty_master_node_close(master);
    }
    node->impl = 0;
}

void pty_init(void) {
    memset(pty_pairs, 0, sizeof(pty_pairs));

    memset(&ptmx_node, 0, sizeof(ptmx_node));
    strncpy(ptmx_node.name, "ptmx", sizeof(ptmx_node.name) - 1);
    ptmx_node.flags = FS_CHARDEVICE;
    ptmx_node.mask  = 0666;
    ptmx_node.uid   = 0;
    ptmx_node.gid   = 0;
    ptmx_node.open  = ptmx_open;
    ptmx_node.read  = ptmx_node_read;
    ptmx_node.write = ptmx_node_write;
    ptmx_node.ioctl = ptmx_node_ioctl;
    ptmx_node.close = ptmx_node_close;
    devfs_register_device(&ptmx_node);
}
