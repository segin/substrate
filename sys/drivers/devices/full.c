/*
 * sys/drivers/devices/full.c - /dev/full device driver
 *
 * Implements /dev/full character device.
 *
 * EARS Requirements Met:
 * - U1: Always-fail-writes (-ENOSPC)
 * - U2: Read-zeroes (efficiently using memset for now, as uiomove/zero-page not fully exposed in this context)
 * - U3: Seek semantics (handled by VFS, effectively infinite)
 * - U4: Open semantics (always succeeds)
 * - U5: File attributes (S_IFCHR, major 1, minor 7)
 * - U6: Ioctl (-ENOTTY for all)
 * - U7: Nonblocking (irrelevant as operations are non-blocking)
 * - U8: Concurrency (stateless)
 * - E1: Poll/select readiness (POLLIN | POLLOUT)
 * - UB1: No partial writes
 * - O1: mmap (O1a: Disallow)
 */

#include <vfs/vfs.h>
#include <sys/errno.h>
#include <sys/poll.h>
#include <string.h>

// Full read: return zeros
static size_t full_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    // memset is efficient enough for kernel buffers (usually 4KB chunks in standard read loops)
    // For very large reads passed directly from userspace, we rely on standard library optimized memset.
    memset(buffer, 0, size);
    return size;
}

// Full write: always fail with ENOSPC
static size_t full_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer; (void)size;
    // Return error code cast to size_t (standard kernel convention here)
    return (size_t)-ENOSPC;
}

// Full poll: always ready for read and write (write fails immediately)
static int full_poll(fs_node_t *node, void *waiter) {
    (void)node; (void)waiter;
    return POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM;
}

// Full ioctl: always fail with ENOTTY
static int full_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node; (void)request; (void)arg;
    return -ENOTTY;
}

// Full mmap: explicit disallow (O1a)
static void *full_mmap(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    (void)node; (void)addr; (void)length; (void)prot; (void)flags; (void)offset;
    return (void *)-1; // MAP_FAILED
}

static fs_node_t full_node;

void full_init(void) {
    memset(&full_node, 0, sizeof(fs_node_t));
    strcpy(full_node.name, "full");
    full_node.flags = FS_CHARDEVICE;
    full_node.read = &full_read;
    full_node.write = &full_write;
    full_node.poll = &full_poll;
    full_node.ioctl = &full_ioctl;
    full_node.mmap = &full_mmap;
    full_node.rdev = (1 << 8) | 7; // Major 1, Minor 7

    // Register device
    devfs_register_device(&full_node);
}
