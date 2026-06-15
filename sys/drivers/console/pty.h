/*
 * pty.h — Unix98 pseudo-terminal subsystem.
 *
 * Brings up `/dev/ptmx` and the `/dev/pts/N` slave nodes.  Each open
 * of `/dev/ptmx` allocates a fresh master/slave pair; the master fd
 * is the file descriptor returned by open(); the slave shows up at
 * `/dev/pts/<N>` after unlockpt() (TIOCSPTLCK).
 *
 * The slave runs the existing N_TTY line discipline (tty.c).  The
 * master is a thin pass-through: writes to the master flow through
 * line-discipline input on the slave; data the slave writes is
 * buffered for the master to read.  No echo or canonical processing
 * happens on master writes themselves — that's all the slave's job.
 */
#ifndef _DRIVERS_CONSOLE_PTY_H
#define _DRIVERS_CONSOLE_PTY_H

#include <sys/tty.h>
#include <sys/types.h>

#define PTY_MAX_PAIRS 256
#define PTY_MASTER_BUF_SIZE TTY_BUF_SIZE

void pty_init(void);

/*
 * Allocate a fresh PTY pair.  Returns the slave index (>= 0) on
 * success or -errno on failure.  The caller becomes the controlling
 * holder of the master fd.  Slave nodes only become visible under
 * /dev/pts/ after unlockpt() (TIOCSPTLCK with *value == 0).
 *
 * `master_node_out` is set to the freshly-published master fs_node_t
 * — it is not under /dev/pts/ but is referenced by the ptmx open
 * path so the file descriptor backs onto it.
 */
int pty_alloc_pair(struct fs_node **master_node_out);

/*
 * Master-side fs_node callbacks.  Exposed because /dev/ptmx clones
 * into a per-fd master node and dispatches operations through these
 * directly.
 */
size_t pty_master_node_read(struct fs_node *node, off_t off, size_t sz,
                            uint8_t *buf);
size_t pty_master_node_write(struct fs_node *node, off_t off, size_t sz,
                             const uint8_t *buf);
int    pty_master_node_ioctl(struct fs_node *node, uint32_t req, void *arg);
void   pty_master_node_close(struct fs_node *node);
int    pty_master_node_poll(struct fs_node *node, void *waiter);

/* Mirror a master fd's O_NONBLOCK flag onto the pair.  Returns 1 if
 * `node` is a pty master, 0 otherwise. */
int    pty_set_nonblock(struct fs_node *node, int on);

/*
 * BSD-style pty grid (/dev/pty[pq][0-9a-f] master, /dev/tty[pq][0-9a-f]
 * slave).  The open(2) path consults these so an already-open master can
 * fail with -EIO (a busy master can't be reported from the void
 * node->open callback).  pty_is_bsd_master() identifies a BSD master
 * node; pty_bsd_master_open() claims it (0 on success, -errno on busy /
 * exhaustion).
 */
int    pty_is_bsd_master(struct fs_node *node);
int    pty_bsd_master_open(struct fs_node *node);

#endif /* _DRIVERS_CONSOLE_PTY_H */
