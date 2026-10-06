/*
 * /dev/zero - Source of zeroed memory
 *
 * Implements a character device that:
 * - Returns an infinite stream of zero bytes on read.
 * - Accepts and discards any data written to it.
 * - Supports mmap() for zero-filled memory mappings.
 *
 * This implementation follows BSD-like semantics used by Substrate.
 */

#include <string.h>

#include <machine/pmap.h>
#include <machine/pmm.h>
#include <machine/vmparam.h>
#include <kern/console.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/syscall_impl.h>
#include <vfs/vfs.h>

static fs_node_t zero_node;

static void zero_open(fs_node_t *node) {
    (void)node;
}

static void zero_close(fs_node_t *node) {
    (void)node;
}

/*
 * zero_read - Returns zeros to the caller.
 */
static size_t zero_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    (void)offset;

    memset(buffer, 0, size);
    return size;
}

/*
 * zero_write - Discards all input.
 */
static size_t zero_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    (void)offset;
    (void)buffer;
    return size;
}

/*
 * zero_ioctl - No ioctls supported.
 */
static int zero_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    (void)request;
    (void)arg;
    return -ENOTTY;
}

/*
 * zero_poll - Always ready for read and write.
 */
static int zero_poll(fs_node_t *node, void *waiter) {
    (void)node;
    (void)waiter;
    return POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM;
}

/*
 * zero_mmap - Map zero-filled pages.
 *
 * Mapping /dev/zero is how a program asked for anonymous memory before
 * there was MAP_ANONYMOUS -- the System V Release 4 dynamic linker gets
 * all of its own this way -- and it means the same thing.
 */
static void *zero_mmap(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    (void)node;
    (void)offset;

    /* The same mapping asked for anonymously, so the address is chosen and
     * recorded by the map like any other and the pages come on demand. */
    return sys_mmap(addr, length, prot, flags | MAP_ANONYMOUS, -1, 0);
}

/*
 * zero_init - Initialize and register the device.
 */
void zero_init(void) {
    memset(&zero_node, 0, sizeof(fs_node_t));
    strlcpy(zero_node.name, "zero", sizeof(zero_node.name));
    zero_node.flags = FS_CHARDEVICE;
    zero_node.mask = 0666;
    zero_node.uid = 0;
    zero_node.gid = 0;
    zero_node.open = &zero_open;
    zero_node.close = &zero_close;
    zero_node.read = &zero_read;
    zero_node.write = &zero_write;
    zero_node.ioctl = &zero_ioctl;
    zero_node.poll = &zero_poll;
    zero_node.mmap = &zero_mmap;
    zero_node.rdev = (1 << 8) | 5;

    devfs_register_device(&zero_node);
    kprint("zero: /dev/zero initialized\n");
}
