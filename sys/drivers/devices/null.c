#include <sys/types.h>
#include <sys/errno.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <vfs/vfs.h>
#include <string.h>
#include <kern/console.h>
#include "null.h"

/*
 * /dev/null Implementation
 *
 * Requirements (EARS):
 * U1: Discard-writes-succeed (return full count, no ENOSPC).
 * U2: Read-EOF (return 0).
 * U3: Seek semantics (succeed, no effect on read).
 * U4: Open semantics (succeed).
 * U5: File attributes (S_IFCHR).
 * U6: Ioctl (return -1, errno=ENOTTY).
 * U7: Nonblocking (O_NONBLOCK irrelevant for write/read, works).
 * U8: Concurrency (stateless).
 * E1: Poll (POLLOUT always, POLLIN never).
 * UB1: No data on read.
 * O1: mmap (DISALLOW, return EINVAL/MAP_FAILED).
 * C1: Efficiency (O(1) discard).
 */

static fs_node_t null_node;

/*
 * null_open
 * Always succeeds.
 */
static void null_open(fs_node_t *node) {
    (void)node;
    /* U4: Open semantics - succeed */
}

/*
 * null_close
 * Always succeeds.
 */
static void null_close(fs_node_t *node) {
    (void)node;
}

/*
 * null_read
 * Always returns 0 (EOF).
 * U2, UB1.
 */
static size_t null_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    (void)offset;
    (void)size;
    (void)buffer;
    return 0;
}

/*
 * null_write
 * Discards all data and returns the requested size.
 * U1, C1.
 * O(1) complexity - no copying.
 */
static size_t null_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    (void)offset;
    (void)buffer;
    /*
     * Requirement U1: discard every write and return full count.
     * Requirement C1: Avoid per-byte loops. Simply returning 'size'
     * satisfies both requirements efficiently.
     */
    return size;
}

/*
 * null_ioctl
 * Always returns error (ENOTTY).
 * U6.
 */
static int null_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    (void)request;
    (void)arg;
    return -ENOTTY;
}

/*
 * null_poll
 * Always returns POLLOUT. Never returns POLLIN.
 * E1.
 */
static int null_poll(fs_node_t *node, void *waiter) {
    (void)node;
    (void)waiter;
    /*
     * Requirement E1:
     * - Always writable (POLLOUT).
     * - Not readable (POLLIN) because reads return EOF immediately,
     *   which is not considered "data available" in this interpretation.
     */
    return POLLOUT;
}

/*
 * null_mmap
 * Always fails with EINVAL (or MAP_FAILED semantics).
 * O1.
 */
static void *null_mmap(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    (void)node; (void)addr; (void)length; (void)prot; (void)flags; (void)offset;
    /*
     * Requirement O1: mmap disallowed.
     * There is no mechanism to set errno here directly via return,
     * but usually the caller (sys_mmap) checks for MAP_FAILED ((void*)-1).
     * If the kernel supported setting errno via return or thread-local, we'd do it.
     * For now, returning (void*)-1 signals failure.
     */
    return (void *)-1;
}

/*
 * null_init
 * Initialize and register the /dev/null device.
 */
void null_init(void) {
    memset(&null_node, 0, sizeof(fs_node_t));
    strcpy(null_node.name, "null");

    /* U5: File attributes - Character Device */
    null_node.flags = FS_CHARDEVICE;
    null_node.rdev = (1 << 8) | 3; // Major 1, Minor 3 (standard-ish for this OS)

    /* Set permissions (usually rw-rw-rw- or similar, handled by VFS/devfs defaults) */
    null_node.mask = 0666;
    null_node.uid = 0;
    null_node.gid = 0;

    /* Hook up operations */
    null_node.open = null_open;
    null_node.close = null_close;
    null_node.read = null_read;
    null_node.write = null_write;
    null_node.ioctl = null_ioctl;
    null_node.poll = null_poll;
    null_node.mmap = null_mmap;

    /*
     * Requirement U3: Seek semantics.
     * VFS handles lseek by updating file offset. Since read/write ignore
     * offset, seek effectively succeeds and does nothing, which is correct.
     */

    /* Register with devfs */
    devfs_register_device(&null_node);

    kprint("null: /dev/null registered\n");
}
